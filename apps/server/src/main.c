#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include "../../../packages/simulation/local_game.h"

static int sock = -1;
static struct sockaddr_in bind_addr;
static unsigned int client_last_seq[MAX_CLIENTS];

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
                NetHeader h = {0};
                h.type = PACKET_WELCOME;
                h.client_id = (uint8_t)client_id;
                h.timestamp = get_server_time();
                sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)sender, sizeof(struct sockaddr_in));
                printf("CLIENT %d CONNECTED\n", client_id);
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
    server_net_init(port);
    local_init_match(2);
    int running = 1;
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
        server_broadcast();
        #ifdef _WIN32
        Sleep(16);
        #else
        usleep(16000);
        #endif
    }
    return 0;
}
