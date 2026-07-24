#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"
#include "../../../packages/simulation/local_game.h"

static int sock = -1;
static struct sockaddr_in bind_addr;
static unsigned int client_last_seq[MAX_CLIENTS];

// WOTAN player identity (EMILY/BACKLOG.md S170-26, REDGARDEN NORTHSTAR §12
// Phase A). The connect ticket already carries a real IDUNA-minted
// player_id (see verify_connect_ticket below) but it was previously
// discarded right after verification. Captured here per client slot so
// Phase B (replay logging) can key match events to a real WOTAN player
// instead of just a slot index.
static unsigned char client_player_id[MAX_CLIENTS][16];
static int client_has_player_id[MAX_CLIENTS];

// IDUNA agent config — same env vars / parsing as shankpit-460's
// apps/server/src/main.c (S156-04), ported here for S170-26. Not yet used
// to report match results: REDGARDEN's match_winner (win/loss, card-RTS) is
// a different shape than shankpit-460's kills/deaths (FPS), and forcing one
// into the other's IDUNA schema would corrupt shared WOTAN profile
// semantics. This pass only loads the config and exposes it for Phase B;
// see the S170-26 backlog entry for the open schema question.
static char iduna_host[128] = "127.0.0.1";
static int iduna_port = 8080;
static char iduna_agent_name[128] = "";
static char iduna_agent_secret[256] = "";
static int iduna_agent_configured = 0;

// load_iduna_agent_config reads IDUNA_BASE_URL / IDUNA_AGENT_NAME /
// IDUNA_AGENT_SECRET at startup. IDUNA_BASE_URL may be "host:port" or a
// full "http://host:port" URL (scheme and any trailing path are stripped);
// defaults to 127.0.0.1:8080 if unset.
static void load_iduna_agent_config(void) {
    const char *base_url = getenv("IDUNA_BASE_URL");
    if (base_url && base_url[0]) {
        const char *host_start = base_url;
        if (strncmp(host_start, "http://", 7) == 0) host_start += 7;
        else if (strncmp(host_start, "https://", 8) == 0) host_start += 8;

        char host_buf[128];
        strncpy(host_buf, host_start, sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';
        char *slash = strchr(host_buf, '/');
        if (slash) *slash = '\0';

        char *colon = strchr(host_buf, ':');
        int port = iduna_port;
        if (colon) {
            port = atoi(colon + 1);
            *colon = '\0';
        }
        strncpy(iduna_host, host_buf, sizeof(iduna_host) - 1);
        iduna_host[sizeof(iduna_host) - 1] = '\0';
        if (port > 0) iduna_port = port;
    }

    const char *name = getenv("IDUNA_AGENT_NAME");
    const char *secret = getenv("IDUNA_AGENT_SECRET");
    if (name && name[0] && secret && secret[0]) {
        strncpy(iduna_agent_name, name, sizeof(iduna_agent_name) - 1);
        iduna_agent_name[sizeof(iduna_agent_name) - 1] = '\0';
        strncpy(iduna_agent_secret, secret, sizeof(iduna_agent_secret) - 1);
        iduna_agent_secret[sizeof(iduna_agent_secret) - 1] = '\0';
        iduna_agent_configured = 1;
        printf("IDUNA agent configured: name=%s host=%s:%d (player identity reporting available for Phase B)\n",
               iduna_agent_name, iduna_host, iduna_port);
    } else {
        printf("WARNING: IDUNA_AGENT_NAME/IDUNA_AGENT_SECRET not set -- WOTAN player identity features disabled (S170-26)\n");
    }
}

// Connect-ticket verification — same scheme as shankpit-460 (see
// shankpit-460/apps/server/src/main.c and IDUNA's ShankpitTicketHandler,
// internal/http/handlers/shankpit_ticket.go): player_id (16 raw bytes) ||
// expires_at (4-byte LE unix timestamp) || HMAC-SHA256(secret, the first
// 20 bytes) truncated to 16 bytes, appended after the NetHeader in a
// PACKET_CONNECT payload. REDGARDEN reuses the same wire format and the
// same self-contained hmac_sha256.h; test bots self-mint tickets with the
// shared secret (see apps/client/bot_main.c), matching the emily-bot
// pattern rather than requiring a real IDUNA JWT per bot.
#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
static unsigned char ticket_secret[256];
static int ticket_secret_len = 0;

// verify_connect_ticket fails closed: if REDGARDEN_TICKET_SECRET is unset,
// every ticket is rejected rather than silently accepting connects.
static int verify_connect_ticket(const char *buffer, int size) {
    if (ticket_secret_len == 0) return 0;
    if (size < (int)sizeof(NetHeader) + TICKET_TOTAL_LEN) return 0;

    const unsigned char *ticket = (const unsigned char *)(buffer + sizeof(NetHeader));
    const unsigned char *payload = ticket;
    const unsigned char *given_mac = ticket + TICKET_PAYLOAD_LEN;

    unsigned char expected_mac[32];
    hmac_sha256(ticket_secret, (size_t)ticket_secret_len, payload, TICKET_PAYLOAD_LEN, expected_mac);
    if (!hmac_sha256_verify(given_mac, expected_mac, TICKET_MAC_LEN)) {
        return 0;
    }

    unsigned int expires_at =
        (unsigned int)payload[16] | ((unsigned int)payload[17] << 8) |
        ((unsigned int)payload[18] << 16) | ((unsigned int)payload[19] << 24);
    if ((unsigned int)time(NULL) > expires_at) {
        return 0;
    }
    return 1;
}

static void load_ticket_secret(void) {
    const char *env = getenv("REDGARDEN_TICKET_SECRET");
    if (!env || !env[0]) {
        printf("WARNING: REDGARDEN_TICKET_SECRET not set — all connect attempts will be rejected (fail closed, not fail open)\n");
        return;
    }
    size_t len = strlen(env);
    if (len > sizeof(ticket_secret)) len = sizeof(ticket_secret);
    memcpy(ticket_secret, env, len);
    ticket_secret_len = (int)len;
    printf("REDGARDEN_TICKET_SECRET loaded (%d bytes)\n", ticket_secret_len);
}

static unsigned int get_server_time(void);

// Match event log (NORTHSTAR §10's minimum hook + §12 Phase B, EMILY/
// BACKLOG.md S170-28). Deliberately just a data-capture hook, not a replay
// system: no player-facing playback, no spectator wire protocol. One
// newline-delimited JSON event per line, appended to
// var/matches/<port>-<timestamp>.jsonl for this match's server process.
// Player_id fields are hex-encoded from the Phase A ticket capture so a
// replay/training pass can attribute events to a real WOTAN player, not
// just a slot index -- the whole reason Phase A had to land first.
static FILE *match_log_fp = NULL;

static void player_id_hex(int client_id, char out[33]) {
    if (!client_has_player_id[client_id]) {
        strcpy(out, "unregistered");
        return;
    }
    for (int i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02x", client_player_id[client_id][i]);
    }
}

static void match_log_open(int port) {
    mkdir("var", 0755);
    mkdir("var/matches", 0755);
    char path[256];
    snprintf(path, sizeof(path), "var/matches/%d-%ld.jsonl", port, (long)time(NULL));
    match_log_fp = fopen(path, "a");
    if (!match_log_fp) {
        printf("WARNING: could not open match log %s -- match will not be logged (S170-28)\n", path);
        return;
    }
    fprintf(match_log_fp, "{\"event\":\"match_start\",\"port\":%d,\"ts_ms\":%u}\n", port, get_server_time());
    fflush(match_log_fp);
    printf("Match event log: %s\n", path);
}

static void match_log_connect(int client_id) {
    if (!match_log_fp) return;
    char pid_hex[33];
    player_id_hex(client_id, pid_hex);
    fprintf(match_log_fp, "{\"event\":\"connect\",\"client_id\":%d,\"player_id\":\"%s\",\"ts_ms\":%u}\n",
            client_id, pid_hex, get_server_time());
    fflush(match_log_fp);
}

static void match_log_card_play(int client_id, uint8_t card_id, int16_t grid_x, int16_t grid_z) {
    if (!match_log_fp) return;
    char pid_hex[33];
    player_id_hex(client_id, pid_hex);
    fprintf(match_log_fp,
            "{\"event\":\"card_play\",\"client_id\":%d,\"player_id\":\"%s\",\"card_id\":%d,\"grid_x\":%d,\"grid_z\":%d,\"ts_ms\":%u}\n",
            client_id, pid_hex, (int)card_id, (int)grid_x, (int)grid_z, get_server_time());
    fflush(match_log_fp);
}

static void match_log_win(int winner) {
    if (!match_log_fp) return;
    fprintf(match_log_fp, "{\"event\":\"match_end\",\"winner\":%d,\"ts_ms\":%u}\n", winner, get_server_time());
    fflush(match_log_fp);
}

static unsigned int get_server_time(void) {
#ifdef _WIN32
    return (unsigned int)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

static void server_net_init(int port) {
    setbuf(stdout, NULL);
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1; ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("FAILED TO BIND PORT %d\n", port);
        exit(1);
    }
    printf("SERVER LISTENING ON PORT %d\nWaiting...\n", port);
}

static void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) return;
    NetHeader *head = (NetHeader *)buffer;
    int client_id = -1;

    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (local_state.client_active[i] &&
            memcmp(&local_state.clients[i].sin_addr, &sender->sin_addr, sizeof(struct in_addr)) == 0 &&
            local_state.clients[i].sin_port == sender->sin_port) {
            client_id = i;
            break;
        }
    }

    if (client_id == -1 && head->type == PACKET_CONNECT) {
        // Verify the connect ticket BEFORE allocating any slot — invalid,
        // missing, or expired tickets (or an unconfigured secret, which
        // fails closed) are silently dropped: no slot consumed, no Welcome.
        if (!verify_connect_ticket(buffer, size)) {
            return;
        }
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (!local_state.client_active[i]) {
                client_id = i;
                local_state.client_active[i] = 1;
                local_state.clients[i] = *sender;
                // Capture the ticket's player_id (first 16 bytes of the
                // payload verify_connect_ticket already authenticated) —
                // S170-26, the actual WOTAN-identity prerequisite Phase B
                // needs. Test bots self-mint tickets with random bytes here
                // (not a real registered IDUNA player_id); real players'
                // tickets carry the genuine one minted at login.
                const unsigned char *ticket_payload = (const unsigned char *)(buffer + sizeof(NetHeader));
                memcpy(client_player_id[client_id], ticket_payload, 16);
                client_has_player_id[client_id] = 1;
                NetHeader h = {0};
                h.type = PACKET_WELCOME;
                h.client_id = (uint8_t)client_id;
                h.timestamp = get_server_time();
                sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)sender, sizeof(struct sockaddr_in));
                printf("CLIENT %d CONNECTED\n", client_id);
                match_log_connect(client_id);
                break;
            }
        }
    }

    if (client_id != -1 && head->type == PACKET_CARD_PLAY) {
        if (size < (int)(sizeof(NetHeader) + sizeof(CardPlayCmd))) return;
        CardPlayCmd *cmd = (CardPlayCmd *)(buffer + sizeof(NetHeader));
        if (cmd->sequence <= client_last_seq[client_id]) return;
        client_last_seq[client_id] = cmd->sequence;
        local_apply_card((uint8_t)client_id, cmd->card_id, cmd->grid_x, cmd->grid_z);
        match_log_card_play(client_id, cmd->card_id, cmd->grid_x, cmd->grid_z);
    }
}

static void server_broadcast(void) {
    char buffer[4096];
    int cursor = 0;
    NetHeader head = {0};
    head.type = PACKET_SNAPSHOT;
    head.timestamp = get_server_time();

    uint16_t count = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) if (local_state.entities[i].active) count++;
    head.entity_count = count;

    memcpy(buffer + cursor, &head, sizeof(NetHeader));
    cursor += sizeof(NetHeader);
    memcpy(buffer + cursor, &count, sizeof(uint16_t));
    cursor += sizeof(uint16_t);

    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        NetEntity ne = {0};
        ne.id = e->id;
        ne.type = e->type;
        ne.owner = e->owner;
        ne.x = e->x;
        ne.z = e->z;
        ne.hp = (uint16_t)e->hp;
        memcpy(buffer + cursor, &ne, sizeof(NetEntity));
        cursor += sizeof(NetEntity);
        if (cursor > (int)(sizeof(buffer) - sizeof(NetEntity))) break;
    }

    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (local_state.client_active[i]) {
            sendto(sock, buffer, cursor, 0, (struct sockaddr *)&local_state.clients[i], sizeof(struct sockaddr_in));
        }
    }
}

int main(int argc, char *argv[]) {
    int port = 6969;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }
    load_ticket_secret();
    load_iduna_agent_config();
    server_net_init(port);
    match_log_open(port);
    local_init_match(2);
    int running = 1;
    int last_winner_logged = 0;
    while (running) {
        char buffer[1024];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        while (len > 0) {
            server_handle_packet(&sender, buffer, len);
            len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        }
        local_update(16);
        if (local_state.match_winner != 0 && !last_winner_logged) {
            match_log_win(local_state.match_winner);
            last_winner_logged = 1;
        }
        server_broadcast();
        #ifdef _WIN32
        Sleep(16);
        #else
        usleep(16000);
        #endif
    }
    return 0;
}
