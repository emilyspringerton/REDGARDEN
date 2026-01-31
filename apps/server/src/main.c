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
#include "../../../packages/simulation/local_game.h"

static int sock = -1;
static struct sockaddr_in bind_addr;
static unsigned int client_last_seq[MAX_CLIENTS];

static unsigned int get_server_time(void) {
#ifdef _WIN32
    return (unsigned int)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

static void server_net_init(void) {
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
    bind_addr.sin_port = htons(6969);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("FAILED TO BIND PORT 6969\n");
        exit(1);
    }
    printf("SERVER LISTENING ON PORT 6969\nWaiting...\n");
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

int main(void) {
    server_net_init();
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
