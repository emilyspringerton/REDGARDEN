// apps/matchmaker/src/main.c — simple matchmaking for REDGARDEN.
//
// REDGARDEN's game server is one match per process (a single global
// ServerState, owners indexed 0-2 — see packages/simulation/local_game.h).
// So "matchmaking" here means: queue clients that ask to PACKET_FIND_MATCH,
// pair them two at a time, spawn a fresh red_garden_server on its own port
// for that pair, and tell both clients (via PACKET_MATCH_FOUND) which port
// to connect to. Each match gets an isolated process — no shared state
// between concurrent matches, no refactor of the single-match simulation.
//
// Run from the repo root (expects build/red_garden_server to exist, or
// pass --server-bin):
//   scripts/build.sh && build/red_garden_matchmaker
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #error "matchmaker requires fork(); not supported on Windows yet"
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
#endif

#include "../../../packages/common/protocol.h"

#define MATCHMAKER_PORT 7777
#define FIRST_GAME_PORT 7100
#define MAX_QUEUE 64

typedef struct {
    struct sockaddr_in addr;
} QueuedClient;

static QueuedClient wait_queue[MAX_QUEUE];
static int queue_count = 0;
static int next_game_port = FIRST_GAME_PORT;
static int sock = -1;
static char server_bin[256] = "./build/red_garden_server";

static int addr_eq(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return memcmp(&a->sin_addr, &b->sin_addr, sizeof(struct in_addr)) == 0 &&
           a->sin_port == b->sin_port;
}

static int spawn_game_server(int port) {
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        execl(server_bin, "red_garden_server", "--port", port_str, (char *)NULL);
        fprintf(stderr, "MATCHMAKER: failed to exec %s\n", server_bin);
        _exit(127);
    }
    return 1;
}

static void enqueue(struct sockaddr_in *sender) {
    for (int i = 0; i < queue_count; i++) {
        if (addr_eq(&wait_queue[i].addr, sender)) return; // already queued
    }
    if (queue_count >= MAX_QUEUE) return;
    wait_queue[queue_count].addr = *sender;
    queue_count++;
    printf("MATCHMAKER: queued %s:%d (queue=%d)\n",
           inet_ntoa(sender->sin_addr), ntohs(sender->sin_port), queue_count);
}

static void try_match(void) {
    while (queue_count >= 2) {
        struct sockaddr_in a = wait_queue[0].addr;
        struct sockaddr_in b = wait_queue[1].addr;
        for (int i = 2; i < queue_count; i++) wait_queue[i - 2] = wait_queue[i];
        queue_count -= 2;

        int port = next_game_port++;
        if (!spawn_game_server(port)) {
            printf("MATCHMAKER: failed to spawn game server on port %d\n", port);
            continue;
        }
        printf("MATCHMAKER: matched pair -> spawned server on port %d\n", port);

        char buf[sizeof(NetHeader) + sizeof(MatchFoundMsg)];
        NetHeader *h = (NetHeader *)buf;
        memset(h, 0, sizeof(NetHeader));
        h->type = PACKET_MATCH_FOUND;
        MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
        msg->port = (uint16_t)port;

        sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&a, sizeof(a));
        sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&b, sizeof(b));
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server-bin") == 0 && i + 1 < argc) {
            strncpy(server_bin, argv[++i], sizeof(server_bin) - 1);
        }
    }

    setbuf(stdout, NULL);
    signal(SIGCHLD, SIG_IGN); // auto-reap spawned game-server children, avoid zombies

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MATCHMAKER_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("FAILED TO BIND MATCHMAKER PORT %d\n", MATCHMAKER_PORT);
        exit(1);
    }
    printf("MATCHMAKER LISTENING ON PORT %d (server_bin=%s)\n", MATCHMAKER_PORT, server_bin);

    while (1) {
        char buffer[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender, &slen);
        while (len > 0) {
            if (len >= (int)sizeof(NetHeader)) {
                NetHeader *head = (NetHeader *)buffer;
                if (head->type == PACKET_FIND_MATCH) {
                    enqueue(&sender);
                }
            }
            len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender, &slen);
        }
        try_match();
        usleep(16000);
    }
    return 0;
}
