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

static void send_connect_with_ticket(const unsigned char *secret, size_t secret_len) {
    char buffer[sizeof(NetHeader) + TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buffer;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    mint_ticket(secret, secret_len, (unsigned char *)(buffer + sizeof(NetHeader)));
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
    send_connect_with_ticket((const unsigned char *)ticket_secret, strlen(ticket_secret));

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
