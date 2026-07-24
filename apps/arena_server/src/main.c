// apps/arena_server/src/main.c — server-authoritative UDP for apps/arena.
//
// NORTHSTAR §13 (2026-07-24 pivot): apps/arena is the product now, and the
// real gap between "bots fighting bots" and actual PvP is that apps/arena
// had zero networking at all -- a human could only ever fight the sim's own
// local bot, never another connection. This is that missing server, one
// match per process (mirrors apps/server's own "one match per process"
// design, packages/simulation/local_game.h's own doc comment) but driving
// arena_game.c instead of local_game.c.
//
// Ports the already-proven pieces from apps/server/src/main.c verbatim
// rather than re-deriving them: connect-ticket verification
// (packages/common/hmac_sha256.h), IDUNA agent config + WOTAN match-result
// reporting (packages/common/http_client.h). Two hero slots only (1v1) --
// 10v10/N-player is explicitly deferred until 1v1 human PvP is proven fun.
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
#include "../../../packages/simulation/arena_game.h"

#define ARENA_MAX_CLIENTS 2

static int sock = -1;
static struct sockaddr_in bind_addr;
static struct sockaddr_in clients[ARENA_MAX_CLIENTS];
static int client_active[ARENA_MAX_CLIENTS];
static unsigned char client_player_id[ARENA_MAX_CLIENTS][16];
static int client_has_player_id[ARENA_MAX_CLIENTS];

// ---- connect-ticket verification (ported verbatim from apps/server) ----
#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
static unsigned char ticket_secret[256];
static int ticket_secret_len = 0;

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
        printf("WARNING: REDGARDEN_TICKET_SECRET not set -- all connect attempts will be rejected (fail closed, not fail open)\n");
        return;
    }
    size_t len = strlen(env);
    if (len > sizeof(ticket_secret)) len = sizeof(ticket_secret);
    memcpy(ticket_secret, env, len);
    ticket_secret_len = (int)len;
    printf("REDGARDEN_TICKET_SECRET loaded (%d bytes)\n", ticket_secret_len);
}

// ---- IDUNA agent config + WOTAN match-result reporting (ported verbatim
// from apps/server, same env vars) ----
static char iduna_host[128] = "127.0.0.1";
static int iduna_port = 8080;
static char iduna_agent_name[128] = "";
static char iduna_agent_secret[256] = "";
static int iduna_agent_configured = 0;

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
        printf("IDUNA agent configured: name=%s host=%s:%d (WOTAN match-result reporting available)\n",
               iduna_agent_name, iduna_host, iduna_port);
    } else {
        printf("WARNING: IDUNA_AGENT_NAME/IDUNA_AGENT_SECRET not set -- WOTAN match-result reporting disabled\n");
    }
}

static void player_id_uuid_str(int client_id, char out[37]) {
    if (!client_has_player_id[client_id]) { out[0] = '\0'; return; }
    const unsigned char *b = client_player_id[client_id];
    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void report_match_result(int winner) {
    if (!iduna_agent_configured) return;

    char resp[2048];
    int status = 0;
    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: agent login failed, skipping match-result report (status=%d)\n", status);
        return;
    }
    char token[2048];
    if (!http_extract_json_string_field(resp, "access_token", token, sizeof(token))) {
        fprintf(stderr, "WOTAN: agent login response missing access_token, skipping report\n");
        return;
    }

    for (int owner = 0; owner < ARENA_MAX_CLIENTS; owner++) {
        char pid[37];
        player_id_uuid_str(owner, pid);
        if (pid[0] == '\0') continue;
        const char *result = ((owner + 1) == winner) ? "win" : "loss";
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"player_id\":\"%s\",\"game\":\"redgarden-arena\",\"result\":\"%s\"}", pid, result);
        if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/game-result", token,
                            body, resp, sizeof(resp), &status) != 0 || status != 200) {
            fprintf(stderr, "WOTAN: game-result report failed for client %d (status=%d)\n", owner, status);
        }
    }
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

// ---- match event log (same schema arena_replay.c already parses, so
// observer-mode/S170-30 keeps working against real networked matches too) ----
static FILE *match_log_fp = NULL;

static void match_log_open(int port) {
    mkdir("var", 0755);
    mkdir("var/matches", 0755);
    char path[256];
    snprintf(path, sizeof(path), "var/matches/arena-server-%d-%ld.jsonl", port, (long)time(NULL));
    match_log_fp = fopen(path, "a");
    if (!match_log_fp) {
        printf("WARNING: could not open match log %s -- match will not be logged\n", path);
        return;
    }
    fprintf(match_log_fp, "{\"event\":\"match_start\",\"port\":%d,\"ts_ms\":%u}\n", port, get_server_time());
    fflush(match_log_fp);
    printf("Match event log: %s\n", path);
}

static void match_log_snapshot(void) {
    if (!match_log_fp) return;
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    fprintf(match_log_fp,
            "{\"event\":\"snapshot\",\"ts_ms\":%u,"
            "\"hero0\":{\"x\":%.2f,\"z\":%.2f,\"hp\":%d},"
            "\"hero1\":{\"x\":%.2f,\"z\":%.2f,\"hp\":%d}}\n",
            get_server_time(), a->x, a->z, a->hp, b->x, b->z, b->hp);
    fflush(match_log_fp);
}

static void match_log_win(int winner) {
    if (!match_log_fp) return;
    fprintf(match_log_fp, "{\"event\":\"match_end\",\"winner\":%d,\"ts_ms\":%u}\n", winner, get_server_time());
    fflush(match_log_fp);
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
    printf("ARENA SERVER LISTENING ON PORT %d\nWaiting for 2 players...\n", port);
}

static void server_broadcast(void) {
    char buffer[sizeof(NetHeader) + sizeof(ArenaSnapshotMsg)];
    NetHeader head = {0};
    head.type = PACKET_ARENA_SNAPSHOT;
    head.timestamp = get_server_time();

    ArenaSnapshotMsg msg = {0};
    for (int i = 0; i < 2; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        msg.heroes[i].x = h->x;
        msg.heroes[i].z = h->z;
        msg.heroes[i].hp = (uint16_t)(h->hp > 0 ? h->hp : 0);
        msg.heroes[i].max_hp = (uint16_t)h->max_hp;
        msg.heroes[i].alive = (uint8_t)h->alive;
        msg.heroes[i].hero_id = (uint8_t)h->hero_id;
    }
    msg.winner = (uint8_t)arena_state.winner;

    memcpy(buffer, &head, sizeof(NetHeader));
    memcpy(buffer + sizeof(NetHeader), &msg, sizeof(ArenaSnapshotMsg));

    for (int i = 0; i < ARENA_MAX_CLIENTS; i++) {
        if (client_active[i]) {
            sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&clients[i], sizeof(struct sockaddr_in));
        }
    }
}

static void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) return;
    NetHeader *head = (NetHeader *)buffer;
    int client_id = -1;

    for (int i = 0; i < ARENA_MAX_CLIENTS; i++) {
        if (client_active[i] &&
            memcmp(&clients[i].sin_addr, &sender->sin_addr, sizeof(struct in_addr)) == 0 &&
            clients[i].sin_port == sender->sin_port) {
            client_id = i;
            break;
        }
    }

    if (client_id == -1 && head->type == PACKET_CONNECT) {
        if (!verify_connect_ticket(buffer, size)) return;
        for (int i = 0; i < ARENA_MAX_CLIENTS; i++) {
            if (!client_active[i]) {
                client_id = i;
                client_active[i] = 1;
                clients[i] = *sender;
                const unsigned char *ticket_payload = (const unsigned char *)(buffer + sizeof(NetHeader));
                memcpy(client_player_id[client_id], ticket_payload, 16);
                client_has_player_id[client_id] = 1;

                NetHeader h = {0};
                h.type = PACKET_WELCOME;
                h.client_id = (uint8_t)client_id;
                h.timestamp = get_server_time();
                sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)sender, sizeof(struct sockaddr_in));
                printf("CLIENT %d CONNECTED (owner %d)\n", client_id, client_id);

                // Once the second real player connects, stop the internal
                // hand-authored bot from driving owner 1 -- see
                // arena_bot_enabled's doc comment in arena_game.h.
                if (client_id == 1) {
                    arena_bot_enabled = 0;
                    printf("Second player connected -- internal bot AI disabled, real PvP now.\n");
                }
                break;
            }
        }
        return;
    }

    if (client_id == -1) return; // unknown sender, not a connect -- ignore

    if (head->type == PACKET_ARENA_MOVE) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaMoveCmd))) return;
        ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buffer + sizeof(NetHeader));
        arena_set_move_target(client_id, cmd->target_x, cmd->target_z);
    } else if (head->type == PACKET_ARENA_CAST) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaCastCmd))) return;
        ArenaCastCmd *cmd = (ArenaCastCmd *)(buffer + sizeof(NetHeader));
        if (cmd->slot == 0) arena_cast_q(client_id);
        else if (cmd->slot == 1) arena_toggle_w(client_id);
        else if (cmd->slot == 2) arena_cast_r(client_id);
    }
}

int main(int argc, char *argv[]) {
    int port = 7200;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }
    load_ticket_secret();
    load_iduna_agent_config();
    server_net_init(port);
    match_log_open(port);
    arena_init();
    arena_bot_enabled = 1; // until/unless a second real client connects

    int running = 1;
    int last_winner_logged = 0;
    unsigned int snapshot_log_timer_ms = 0;
    while (running) {
        char buffer[1024];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        while (len > 0) {
            server_handle_packet(&sender, buffer, len);
            len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        }
        /* Don't run the sim clock until both real players have joined --
         * found live, 2026-07-24: with only one real connection, the
         * default arena_bot_enabled=1 (correct for local solo-vs-bot
         * practice) meant the bot started fighting an empty second slot
         * immediately, and the match could fully resolve before a second
         * real player ever got the chance to connect. Real PvP means the
         * clock starts when both sides are actually present, not before. */
        int both_connected = client_active[0] && client_active[1];
        if (both_connected) {
            arena_update(16);
            snapshot_log_timer_ms += 16;
            if (snapshot_log_timer_ms >= 500) {
                snapshot_log_timer_ms = 0;
                match_log_snapshot();
            }
            if (arena_state.winner != 0 && !last_winner_logged) {
                match_log_win(arena_state.winner);
                report_match_result(arena_state.winner);
                last_winner_logged = 1;
            }
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
