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

static int sock = -1;
static struct sockaddr_in server_addr;
static uint16_t bot_seq = 0;

static void init_network(const char *ip, int port) {
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1; ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
}

static void send_connect(void) {
    NetHeader h = {0};
    h.type = PACKET_CONNECT;
    sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
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
    int port = 6969;
    if (argc > 1) ip = argv[1];

    init_network(ip, port);
    send_connect();
    srand((unsigned int)time(NULL));

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
