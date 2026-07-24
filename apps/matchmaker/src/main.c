// apps/matchmaker/src/main.c — matchmaking for REDGARDEN's UDP game servers.
//
// One binary, two roles depending on how it's launched (2026-07-24,
// NORTHSTAR §13 cont'd): the original card-RTS role (--lobby-size 2,
// default, spawns red_garden_server) is unchanged in behavior. Passing
// --lobby-size N (N>2) with --server-bin pointed at red_garden_arena_server
// makes this the arena team-mode matchmaker instead -- groups N queued
// clients (instead of a hardcoded pair) before spawning a match, and passes
// --lobby-size through to the spawned server so it knows to run
// arena_update_teams() rather than the 1v1 arena_update().
//
// REDGARDEN's game servers are one match per process (a single global
// ServerState/ArenaState -- see packages/simulation/local_game.h and
// arena_game.h's own doc comments). So "matchmaking" here means: queue
// clients that ask to PACKET_FIND_MATCH, group them lobby_size at a time,
// spawn a fresh game-server process on its own port for that group, and
// tell every client in the group (via PACKET_MATCH_FOUND) which port to
// connect to. Each match gets an isolated process -- no shared state
// between concurrent matches, no refactor of the single-match simulations.
//
// Run from the repo root (expects the target server binary to exist, or
// pass --server-bin):
//   scripts/build.sh && build/red_garden_matchmaker   # card-RTS, 1v1 pairs
//   build/red_garden_matchmaker --listen-port 7778 --lobby-size 20 --server-bin ./build/red_garden_arena_server --first-game-port 7300   # arena, 10v10
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

#define MAX_QUEUE 64

typedef struct {
    struct sockaddr_in addr;
} QueuedClient;

static QueuedClient wait_queue[MAX_QUEUE];
static int queue_count = 0;
static int listen_port = 7777;
static int next_game_port = 7100;
static int lobby_size = 2;
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
        if (lobby_size == 2) {
            /* Matches the original, live-verified invocation exactly --
               no --lobby-size arg at all for the default card-RTS/1v1 case. */
            execl(server_bin, server_bin, "--port", port_str, (char *)NULL);
        } else {
            char lobby_str[16];
            snprintf(lobby_str, sizeof(lobby_str), "%d", lobby_size);
            execl(server_bin, server_bin, "--port", port_str, "--lobby-size", lobby_str, (char *)NULL);
        }
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
    printf("MATCHMAKER: queued %s:%d (queue=%d/%d)\n",
           inet_ntoa(sender->sin_addr), ntohs(sender->sin_port), queue_count, lobby_size);
}

static void try_match(void) {
    while (queue_count >= lobby_size) {
        struct sockaddr_in group[MAX_QUEUE];
        for (int i = 0; i < lobby_size; i++) group[i] = wait_queue[i].addr;
        for (int i = lobby_size; i < queue_count; i++) wait_queue[i - lobby_size] = wait_queue[i];
        queue_count -= lobby_size;

        int port = next_game_port++;
        if (!spawn_game_server(port)) {
            printf("MATCHMAKER: failed to spawn game server on port %d\n", port);
            continue;
        }
        printf("MATCHMAKER: matched %d players -> spawned server on port %d\n", lobby_size, port);

        char buf[sizeof(NetHeader) + sizeof(MatchFoundMsg)];
        NetHeader *h = (NetHeader *)buf;
        memset(h, 0, sizeof(NetHeader));
        h->type = PACKET_MATCH_FOUND;
        MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
        msg->port = (uint16_t)port;

        for (int i = 0; i < lobby_size; i++) {
            sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&group[i], sizeof(group[i]));
        }
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server-bin") == 0 && i + 1 < argc) {
            strncpy(server_bin, argv[++i], sizeof(server_bin) - 1);
        } else if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--first-game-port") == 0 && i + 1 < argc) {
            next_game_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lobby-size") == 0 && i + 1 < argc) {
            lobby_size = atoi(argv[++i]);
            if (lobby_size < 2) lobby_size = 2;
            if (lobby_size > MAX_QUEUE) lobby_size = MAX_QUEUE;
        }
    }

    setbuf(stdout, NULL);
    signal(SIGCHLD, SIG_IGN); // auto-reap spawned game-server children, avoid zombies

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)listen_port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("FAILED TO BIND MATCHMAKER PORT %d\n", listen_port);
        exit(1);
    }
    printf("MATCHMAKER LISTENING ON PORT %d (lobby_size=%d, server_bin=%s)\n",
           listen_port, lobby_size, server_bin);

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
