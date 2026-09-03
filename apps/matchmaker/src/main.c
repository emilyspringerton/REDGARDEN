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
//   build/red_garden_matchmaker --listen-port 7778 --lobby-size 20 --server-bin ./build/red_garden_arena_server --first-game-port 7300   # arena, 10v10 (S170-183: reverted after briefly being 14/7v7 under S170-178)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

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

/* recently_matched (S170-85/86, real bug found live): apps/arena_bot's
 * wait_for_match() resends PACKET_FIND_MATCH if it hasn't seen a reply in
 * ~5s (its own comment documents this exact race, previously narrowed from
 * a 1s to a 5s resend interval but never closed). If a client's own stale
 * retry is still in flight the instant this matchmaker matches and dequeues
 * it, that late retry arrives here with no way to tell it apart from a
 * fresh request -- enqueue() re-adds an address that's actually already
 * off connecting to its real match, permanently costing some future lobby
 * exactly one slot (it can never actually connect: the client that owns
 * this address is busy elsewhere and isn't listening for a second
 * PACKET_MATCH_FOUND). With 19 bots continuously cycling through matches,
 * this stopped being a rare edge case and started being the reason almost
 * every lobby landed at N-1: real, live, confirmed via matchmaker/server
 * logs showing "CLIENT 0..N-2 CONNECTED" then a 60s no-progress timeout,
 * over and over. Fix: remember every address for a short cooldown after
 * it's actually been matched, and ignore (not re-enqueue) any FIND_MATCH
 * from it during that window -- comfortably longer than
 * connect_to_server's own ~5s max retry window. */
#define MATCH_COOLDOWN_MS 10000
typedef struct {
    struct sockaddr_in addr;
    unsigned int matched_at_ms;
} RecentMatch;
static RecentMatch recently_matched[MAX_QUEUE];
static int recently_matched_count = 0;

static unsigned int now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
static int listen_port = 7777;
static int next_game_port = 7100;
static int lobby_size = 2;
static int sock = -1;
static char server_bin[256] = "./build/red_garden_server";
/* match_mode/DUNGEON_NORTHSTAR.md Milestone 1: 0 for every existing arena/card-RTS lobby
 * (real, unchanged default), non-zero reserved for a future dungeon server variant -- not
 * built yet, so this only ever carries 0 today, but the plumbing (CLI flag -> spawn args ->
 * MatchFoundMsg) is real and in place. */
static int match_mode = 0;

static int addr_eq(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return memcmp(&a->sin_addr, &b->sin_addr, sizeof(struct in_addr)) == 0 &&
           a->sin_port == b->sin_port;
}

/* prune_recently_matched: drop entries whose cooldown has elapsed. Called
 * before every read/write of recently_matched so the list never grows
 * unbounded and never blocks a genuinely new request from the same address
 * once it's actually free to queue again. */
static void prune_recently_matched(unsigned int now) {
    int w = 0;
    for (int i = 0; i < recently_matched_count; i++) {
        if (now - recently_matched[i].matched_at_ms < MATCH_COOLDOWN_MS) {
            recently_matched[w++] = recently_matched[i];
        }
    }
    recently_matched_count = w;
}

static int is_recently_matched(const struct sockaddr_in *addr, unsigned int now) {
    prune_recently_matched(now);
    for (int i = 0; i < recently_matched_count; i++) {
        if (addr_eq(&recently_matched[i].addr, addr)) return 1;
    }
    return 0;
}

static void mark_recently_matched(const struct sockaddr_in *addr, unsigned int now) {
    prune_recently_matched(now);
    if (recently_matched_count >= MAX_QUEUE) {
        /* Defensive only -- evict the oldest to make room rather than drop
           the newest, which is the one most likely to still be mid-race. */
        memmove(&recently_matched[0], &recently_matched[1], (MAX_QUEUE - 1) * sizeof(RecentMatch));
        recently_matched_count = MAX_QUEUE - 1;
    }
    recently_matched[recently_matched_count].addr = *addr;
    recently_matched[recently_matched_count].matched_at_ms = now;
    recently_matched_count++;
}

static int spawn_game_server(int port, unsigned int seed) {
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        char port_str[16];
        char seed_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        snprintf(seed_str, sizeof(seed_str), "%u", seed);
        if (lobby_size == 2) {
            /* Matches the original, live-verified invocation exactly --
               no --lobby-size arg at all for the default card-RTS/1v1 case.
               --seed added for DUNGEON_NORTHSTAR.md Milestone 1 -- red_garden_server/
               arena_server both silently ignore unrecognized flags today, so this is safe to
               pass unconditionally even though only arena_server's own srand() actually
               consumes it so far. */
            execl(server_bin, server_bin, "--port", port_str, "--seed", seed_str, (char *)NULL);
        } else {
            char lobby_str[16];
            snprintf(lobby_str, sizeof(lobby_str), "%d", lobby_size);
            execl(server_bin, server_bin, "--port", port_str, "--lobby-size", lobby_str, "--seed", seed_str, (char *)NULL);
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
    if (is_recently_matched(sender, now_ms())) {
        /* A stale retry from a client that's already off connecting to its
           real match -- see recently_matched's doc comment. Silently
           ignored, not re-queued: the client isn't listening for a second
           PACKET_MATCH_FOUND and would just be a permanent hole in some
           future lobby. */
        return;
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
        unsigned int seed = (unsigned int)rand();
        if (!spawn_game_server(port, seed)) {
            printf("MATCHMAKER: failed to spawn game server on port %d\n", port);
            continue;
        }
        printf("MATCHMAKER: matched %d players -> spawned server on port %d (seed=%u)\n", lobby_size, port, seed);

        char buf[sizeof(NetHeader) + sizeof(MatchFoundMsg)];
        NetHeader *h = (NetHeader *)buf;
        memset(h, 0, sizeof(NetHeader));
        h->type = PACKET_MATCH_FOUND;
        MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
        msg->port = (uint16_t)port;
        msg->seed = seed;
        msg->mode = (uint8_t)match_mode;

        unsigned int now = now_ms();
        for (int i = 0; i < lobby_size; i++) {
            sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&group[i], sizeof(group[i]));
            mark_recently_matched(&group[i], now);
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
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            /* DUNGEON_NORTHSTAR.md Milestone 1 -- no dungeon server binary exists yet, so
             * "dungeon" is accepted here but nothing downstream does anything with mode=1 today.
             * Real, honest plumbing ahead of the feature it'll serve, not a working feature. */
            match_mode = (strcmp(argv[++i], "dungeon") == 0) ? 1 : 0;
        }
    }

    setbuf(stdout, NULL);
    signal(SIGCHLD, SIG_IGN); // auto-reap spawned game-server children, avoid zombies
    /* Per-match seed generation (DUNGEON_NORTHSTAR.md Milestone 1) -- this process never seeded
     * its own RNG before since it had no real use for rand() prior to this. */
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

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
