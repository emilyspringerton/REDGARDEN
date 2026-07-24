#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../packages/common/protocol.h"
#include "../../packages/common/hmac_sha256.h"
#include "../../packages/common/http_client.h"

#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
#define TICKET_TTL_SECONDS (5 * 60)
#define MATCHMAKER_PORT 7777

static int sock = -1;
static struct sockaddr_in server_addr;
static uint16_t bot_seq = 0;

static void init_network_to(struct sockaddr_in *addr, const char *ip, int port) {
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);
    addr->sin_addr.s_addr = inet_addr(ip);
}

static void open_socket(void) {
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1; ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
}

// mint_ticket builds a connect ticket using the same scheme as
// shankpit-460's emily-bot (apps2/emily-bot/main.go's mintTicket): 16
// pseudo-random bytes stand in for a player_id (a real client would carry a
// real IDUNA-issued one; for this headless test bot, any 16 bytes exercises
// the same server-side verification path), a 4-byte little-endian expiry,
// and a 16-byte truncated HMAC-SHA256 over the first 20 bytes. Writes
// TICKET_TOTAL_LEN bytes to out.
static void mint_ticket(const unsigned char *secret, size_t secret_len, unsigned char out[TICKET_TOTAL_LEN]) {
    unsigned char payload[TICKET_PAYLOAD_LEN];
    for (int i = 0; i < 16; i++) payload[i] = (unsigned char)(rand() & 0xFF);
    uint32_t expires_at = (uint32_t)time(NULL) + TICKET_TTL_SECONDS;
    payload[16] = (unsigned char)(expires_at & 0xFF);
    payload[17] = (unsigned char)((expires_at >> 8) & 0xFF);
    payload[18] = (unsigned char)((expires_at >> 16) & 0xFF);
    payload[19] = (unsigned char)((expires_at >> 24) & 0xFF);

    unsigned char mac[32];
    hmac_sha256(secret, secret_len, payload, TICKET_PAYLOAD_LEN, mac);

    memcpy(out, payload, TICKET_PAYLOAD_LEN);
    memcpy(out + TICKET_PAYLOAD_LEN, mac, TICKET_MAC_LEN);
}

// ---- WOTAN real identity (EMILY/BACKLOG.md S170-26/41, REDGARDEN
// NORTHSTAR §12 Phase A) ----
//
// The self-mint path above (mint_ticket) is the same known simplification
// shankpit-460's own reference bot uses: 16 random bytes standing in for a
// player_id, with no real IDUNA identity behind it at all. This section
// gives the bot a real one instead: log in as the REDGARDEN-BOTS M2M agent,
// register a real player (provider=redgarden_bot), and mint a real ticket
// for that player_id via IDUNA's RedgardenTicketHandler -- the same wire
// format as mint_ticket produces, just signed by IDUNA server-side for an
// identity that actually persists in the players/player_game_stats tables,
// instead of vanishing the moment this process exits.
//
// Same IDUNA_BASE_URL/IDUNA_AGENT_NAME/IDUNA_AGENT_SECRET env vars as
// apps/server/src/main.c's load_iduna_agent_config, so one env setup wires
// up both the server and its bots.
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
    }
}

static int hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    size_t hexlen = strlen(hex);
    if (hexlen != out_len * 2) return 0;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return 0;
        out[i] = (unsigned char)byte;
    }
    return 1;
}

// get_real_wotan_ticket does the full agent-login -> register -> mint-ticket
// round trip over HTTP. Returns 1 and fills out[TICKET_TOTAL_LEN] on
// success; returns 0 on any failure (network error, unexpected status,
// missing JSON field) so the caller can fall back to the self-mint path
// rather than crash or hang a headless test run on a transient IDUNA issue.
static int get_real_wotan_ticket(unsigned char out[TICKET_TOTAL_LEN]) {
    char resp[4096];
    int status = 0;

    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[bot] WOTAN: agent login failed (status=%d)\n", status);
        return 0;
    }
    char token[2048];
    if (!http_extract_json_string_field(resp, "access_token", token, sizeof(token))) {
        fprintf(stderr, "[bot] WOTAN: agent login response missing access_token\n");
        return 0;
    }

    char provider_sub[64];
    snprintf(provider_sub, sizeof(provider_sub), "bot-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
    char register_body[256];
    snprintf(register_body, sizeof(register_body),
             "{\"provider\":\"redgarden_bot\",\"provider_sub\":\"%s\"}", provider_sub);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/players/register", token,
                        register_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[bot] WOTAN: player registration failed (status=%d)\n", status);
        return 0;
    }
    char player_id[64];
    if (!http_extract_json_string_field(resp, "player_id", player_id, sizeof(player_id))) {
        fprintf(stderr, "[bot] WOTAN: registration response missing player_id\n");
        return 0;
    }

    char ticket_body[128];
    snprintf(ticket_body, sizeof(ticket_body), "{\"player_id\":\"%s\"}", player_id);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/ticket", token,
                        ticket_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[bot] WOTAN: ticket mint failed (status=%d)\n", status);
        return 0;
    }
    char ticket_hex[128];
    if (!http_extract_json_string_field(resp, "ticket", ticket_hex, sizeof(ticket_hex))) {
        fprintf(stderr, "[bot] WOTAN: ticket response missing ticket field\n");
        return 0;
    }
    if (!hex_decode(ticket_hex, out, TICKET_TOTAL_LEN)) {
        fprintf(stderr, "[bot] WOTAN: ticket field was not valid %d-byte hex\n", TICKET_TOTAL_LEN);
        return 0;
    }

    printf("[bot %d] WOTAN: real identity registered -- player_id=%s provider_sub=%s\n",
           (int)getpid(), player_id, provider_sub);
    return 1;
}

static void send_find_match(struct sockaddr_in *matchmaker_addr) {
    NetHeader h = {0};
    h.type = PACKET_FIND_MATCH;
    sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)matchmaker_addr, sizeof(*matchmaker_addr));
}

// wait_for_match blocks (polling with a short sleep) until PACKET_MATCH_FOUND
// arrives, then returns the assigned game-server port. Re-sends
// PACKET_FIND_MATCH periodically in case the initial UDP datagram was lost
// or the matchmaker hadn't started listening yet.
static int wait_for_match(struct sockaddr_in *matchmaker_addr) {
    char buf[64];
    int retry_ticks = 0;
    while (1) {
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)(sizeof(NetHeader) + sizeof(MatchFoundMsg))) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_MATCH_FOUND) {
                MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
                return msg->port;
            }
        }
        #ifdef _WIN32
        Sleep(100);
        #else
        usleep(100000);
        #endif
        retry_ticks++;
        if (retry_ticks % 10 == 0) {
            send_find_match(matchmaker_addr); // ~every 1s until matched
        }
    }
}

static void send_connect_with_ticket(const unsigned char ticket[TICKET_TOTAL_LEN]) {
    char buffer[sizeof(NetHeader) + TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buffer;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    memcpy(buffer + sizeof(NetHeader), ticket, TICKET_TOTAL_LEN);
    sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void send_card(uint8_t card_id, int16_t grid_x, int16_t grid_z) {
    char buffer[128];
    NetHeader head = {0};
    head.type = PACKET_CARD_PLAY;
    CardPlayCmd cmd = {0};
    cmd.sequence = ++bot_seq;
    cmd.timestamp = (uint32_t)time(NULL);
    cmd.card_id = card_id;
    cmd.grid_x = grid_x;
    cmd.grid_z = grid_z;
    memcpy(buffer, &head, sizeof(NetHeader));
    memcpy(buffer + sizeof(NetHeader), &cmd, sizeof(CardPlayCmd));
    sendto(sock, buffer, sizeof(NetHeader) + sizeof(CardPlayCmd), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

int main(int argc, char *argv[]) {
    const char *ip = "127.0.0.1";
    int direct_port = 0; // 0 = go through the matchmaker; nonzero = connect directly (legacy/local testing)
    const char *ticket_secret = getenv("REDGARDEN_TICKET_SECRET");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            direct_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ticket-secret") == 0 && i + 1 < argc) {
            ticket_secret = argv[++i];
        }
    }

    if (!ticket_secret || !ticket_secret[0]) {
        fprintf(stderr, "WARNING: no ticket secret (--ticket-secret or $REDGARDEN_TICKET_SECRET) — "
                         "connects will use an all-zero-payload ticket and are expected to be rejected "
                         "by any server with connect-ticket verification enabled\n");
        ticket_secret = "";
    }

    setbuf(stdout, NULL); // unbuffered — otherwise redirected output never flushes for a long-lived bot
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    open_socket();

    load_iduna_agent_config();

    // WOTAN real identity (S170-26/41): try the real register+mint flow
    // first when IDUNA is configured; fall back to the local self-mint
    // (same known-simplification path as shankpit-460's own reference bot)
    // on any failure, so a transient IDUNA hiccup can't hang or crash a
    // headless test run.
    unsigned char ticket[TICKET_TOTAL_LEN];
    int have_ticket = 0;
    if (iduna_agent_configured) {
        have_ticket = get_real_wotan_ticket(ticket);
        if (!have_ticket) {
            fprintf(stderr, "[bot %d] WOTAN: falling back to self-minted ticket (no real identity)\n",
                    (int)getpid());
        }
    }
    if (!have_ticket) {
        mint_ticket((const unsigned char *)ticket_secret, strlen(ticket_secret), ticket);
    }

    int game_port = direct_port;
    if (game_port == 0) {
        struct sockaddr_in matchmaker_addr;
        init_network_to(&matchmaker_addr, ip, MATCHMAKER_PORT);
        printf("[bot %d] finding match via matchmaker %s:%d...\n", (int)getpid(), ip, MATCHMAKER_PORT);
        send_find_match(&matchmaker_addr);
        game_port = wait_for_match(&matchmaker_addr);
        printf("[bot %d] matched -> game server port %d\n", (int)getpid(), game_port);
    }

    init_network_to(&server_addr, ip, game_port);
    send_connect_with_ticket(ticket);

    int tick = 0;
    while (1) {
        int16_t gx = rand() % GRID_DIM;
        int16_t gz = rand() % GRID_DIM;
        uint8_t card = (uint8_t)(tick % CARD_COUNT);
        send_card(card, gx, gz);
        #ifdef _WIN32
        Sleep(1000);
        #else
        usleep(1000000);
        #endif
        tick++;
    }
    return 0;
}
