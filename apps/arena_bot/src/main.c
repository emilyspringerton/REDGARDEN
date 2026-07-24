// apps/arena_bot/src/main.c — a persistent, networked MOBA bot.
//
// NORTHSTAR §13 cont'd (2026-07-24): "22 bots in the pool" needs bots that
// are actual network clients of apps/arena_server -- not the sim's internal
// hand-authored bot brain (arena_game.c's arena_bot_tick/bot_cast_kit_if_ready,
// which only exists for local solo-vs-bot practice and is explicitly
// disabled the moment any real client connects). This is that real client:
// gets a real WOTAN identity (same register+ticket-mint flow as
// apps/client/bot_main.c, ported rather than shared -- see that file's own
// doc comment on why this codebase duplicates per-binary orchestration
// logic instead of linking .c files across build targets), queues via the
// arena matchmaker, drafts a hero, plays using the snapshot data any client
// receives (no access to the authoritative ArenaState -- this bot only ever
// sees what apps/arena's own SDL2 client would see), and loops back to the
// matchmaker after the match ends so it keeps queuing indefinitely.
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

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"

#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
#define ARENA_MATCHMAKER_PORT 7778 /* separate queue from the card-RTS matchmaker's 7777 */

static int sock = -1;
static struct sockaddr_in server_addr;
static int my_owner = -1;

// ---- WOTAN identity (ported from apps/client/bot_main.c) ----
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
        if (colon) { port = atoi(colon + 1); *colon = '\0'; }
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

// Registered once per process lifetime (see main()), not once per match --
// found live, 2026-07-24: an earlier version called the full register+mint
// flow fresh on every single reconnect, with provider_sub keyed off
// time(NULL), so a "persistent" bot was actually registering a brand-new
// WOTAN identity every match instead of accumulating a stable win/loss
// record. player_game_stats confirmed it live: dozens of one-match player
// rows instead of one bot with a growing record. Fixed by splitting
// registration (once) from ticket-minting (every reconnect, since tickets
// are meant to be short-lived).
static char cached_player_id[64] = "";
static int has_cached_identity = 0;

// agent_login: POST /api/v1/auth/agent -> Bearer token. Called fresh each
// time (register once, but a JWT is only valid ~1hr per IDUNA's AgentAuthHandler,
// and re-logging-in is cheap) rather than cached across the whole process.
static int agent_login(char token[2048]) {
    char resp[4096];
    int status = 0;
    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: agent login failed (status=%d)\n", status);
        return 0;
    }
    if (!http_extract_json_string_field(resp, "access_token", token, 2048)) {
        fprintf(stderr, "[arena_bot] WOTAN: agent login response missing access_token\n");
        return 0;
    }
    return 1;
}

// register_wotan_identity_once: registers exactly one real player_id for
// this bot process's whole lifetime, cached in cached_player_id. Safe to
// call repeatedly -- a no-op after the first success (register is itself
// an idempotent upsert on the IDUNA side too, but there's no reason to hit
// the network again once we already have an identity).
static int register_wotan_identity_once(void) {
    if (has_cached_identity) return 1;
    char token[2048];
    if (!agent_login(token)) return 0;

    char resp[4096];
    int status = 0;
    char provider_sub[64];
    snprintf(provider_sub, sizeof(provider_sub), "arenabot-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
    char register_body[256];
    snprintf(register_body, sizeof(register_body),
             "{\"provider\":\"redgarden_bot\",\"provider_sub\":\"%s\"}", provider_sub);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/players/register", token,
                        register_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: player registration failed (status=%d)\n", status);
        return 0;
    }
    if (!http_extract_json_string_field(resp, "player_id", cached_player_id, sizeof(cached_player_id))) {
        fprintf(stderr, "[arena_bot] WOTAN: registration response missing player_id\n");
        return 0;
    }
    has_cached_identity = 1;
    printf("[arena_bot %d] WOTAN: real identity registered -- player_id=%s (kept for this process's lifetime)\n",
           (int)getpid(), cached_player_id);
    return 1;
}

// get_real_wotan_ticket: mints a fresh ticket for the cached identity --
// called once per (re)connect, unlike registration above.
static int get_real_wotan_ticket(unsigned char out[TICKET_TOTAL_LEN]) {
    if (!register_wotan_identity_once()) return 0;

    char token[2048];
    if (!agent_login(token)) return 0;

    char resp[4096];
    int status = 0;
    char ticket_body[128];
    snprintf(ticket_body, sizeof(ticket_body), "{\"player_id\":\"%s\"}", cached_player_id);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/ticket", token,
                        ticket_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket mint failed (status=%d)\n", status);
        return 0;
    }
    char ticket_hex[128];
    if (!http_extract_json_string_field(resp, "ticket", ticket_hex, sizeof(ticket_hex))) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket response missing ticket field\n");
        return 0;
    }
    if (!hex_decode(ticket_hex, out, TICKET_TOTAL_LEN)) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket field was not valid hex\n");
        return 0;
    }
    return 1;
}

static void mint_ticket_fallback(const char *secret, unsigned char out[TICKET_TOTAL_LEN]) {
    unsigned char payload[TICKET_PAYLOAD_LEN];
    for (int i = 0; i < 16; i++) payload[i] = (unsigned char)(rand() & 0xFF);
    uint32_t expires_at = (uint32_t)time(NULL) + 300;
    payload[16] = (unsigned char)(expires_at & 0xFF);
    payload[17] = (unsigned char)((expires_at >> 8) & 0xFF);
    payload[18] = (unsigned char)((expires_at >> 16) & 0xFF);
    payload[19] = (unsigned char)((expires_at >> 24) & 0xFF);
    unsigned char mac[32];
    hmac_sha256((const unsigned char *)secret, strlen(secret), payload, TICKET_PAYLOAD_LEN, mac);
    memcpy(out, payload, TICKET_PAYLOAD_LEN);
    memcpy(out + TICKET_PAYLOAD_LEN, mac, 16);
}

// ---- matchmaker + server connection ----
static void send_find_match(struct sockaddr_in *mm_addr) {
    NetHeader h = {0};
    h.type = PACKET_FIND_MATCH;
    sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)mm_addr, sizeof(*mm_addr));
}

static int wait_for_match(struct sockaddr_in *mm_addr) {
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
        /* Resend every ~5s (50 ticks), not ~1s -- found live, 2026-07-24: a
           1s retry interval racing a same-box matchmaker's near-instant
           reply meant an already-matched client's own stale retry packet
           could arrive at the matchmaker just after it had already paired
           and dequeued that client, silently re-enqueuing a phantom entry
           nobody would ever come back to claim. That phantom later got
           falsely paired with a genuinely new client, spawning a server
           with one real connection and one that would never arrive --
           found by noticing spawned match-log files with a match_start and
           nothing else, ever, despite zero logged "failed to connect"
           errors on either bot. A same-box matchmaker reply arrives in
           milliseconds; 5s of silence before resending makes this
           collision exceedingly rare without materially slowing down the
           legitimate "packet actually got lost" recovery path. */
        if (retry_ticks % 50 == 0) send_find_match(mm_addr);
        if (retry_ticks > 600) return -1; /* ~60s -- give up and let the caller retry from scratch */
    }
}

static int connect_to_server(const char *ip, int port) {
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    unsigned char ticket[TICKET_TOTAL_LEN];
    int have_ticket = 0;
    if (iduna_agent_configured) have_ticket = get_real_wotan_ticket(ticket);
    if (!have_ticket) {
        const char *secret = getenv("REDGARDEN_TICKET_SECRET");
        if (!secret || !secret[0]) {
            fprintf(stderr, "[arena_bot] no WOTAN identity and no REDGARDEN_TICKET_SECRET -- cannot connect\n");
            return 0;
        }
        fprintf(stderr, "[arena_bot] WOTAN: falling back to self-minted ticket (no real identity)\n");
        mint_ticket_fallback(secret, ticket);
    }

    char buf[sizeof(NetHeader) + TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    memcpy(buf + sizeof(NetHeader), ticket, TICKET_TOTAL_LEN);
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    for (int tries = 0; tries < 100; tries++) {
        char rbuf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)sizeof(NetHeader)) {
            NetHeader *rh = (NetHeader *)rbuf;
            if (rh->type == PACKET_WELCOME) {
                my_owner = rh->client_id;
                printf("[arena_bot %d] connected -- hero slot %d\n", (int)getpid(), my_owner);
                return 1;
            }
        }
#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
        if (tries % 10 == 0) {
            sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        }
    }
    return 0;
}

static void send_pick(int hero_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaPickCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_PICK;
    ArenaPickCmd *cmd = (ArenaPickCmd *)(buf + sizeof(NetHeader));
    cmd->hero_id = (uint8_t)hero_id;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void send_move(float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_MOVE;
    ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void send_cast(int slot) {
    char buf[sizeof(NetHeader) + sizeof(ArenaCastCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_CAST;
    ArenaCastCmd *cmd = (ArenaCastCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

// play_one_match runs the draft + live-play loop for a single match against
// the already-connected server. Returns once the match ends (winner != 0)
// or the connection goes quiet for too long.
static void play_one_match(void) {
    ArenaSnapshotMsg last = {0};
    int have_snapshot = 0;
    int picked = 0;
    uint32_t last_cast_ms = 0;
    int silent_ticks = 0;

    while (1) {
        char rbuf[2048];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        int got_one = 0;
        while (len > 0) {
            if (len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
                NetHeader *h = (NetHeader *)rbuf;
                if (h->type == PACKET_ARENA_SNAPSHOT) {
                    memcpy(&last, rbuf + sizeof(NetHeader), sizeof(ArenaSnapshotMsg));
                    have_snapshot = 1;
                    got_one = 1;
                }
            }
            len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        }
        silent_ticks = got_one ? 0 : silent_ticks + 1;
        if (silent_ticks > 1000) { /* ~10s of nothing at all -- server's gone */
            fprintf(stderr, "[arena_bot %d] no snapshots for 10s -- giving up on this match\n", (int)getpid());
            return;
        }

        if (have_snapshot) {
            if (last.phase == ARENA_PHASE_DRAFT && !picked) {
                /* Simple roster spread: pick based on owner slot so a full
                   lobby doesn't converge on one hero -- real draft strategy
                   is a later, separate concern. */
                int hero_id = my_owner % 11; /* ARENA_HERO_UNICORN..COURIER (S170-48) */
                send_pick(hero_id);
                picked = 1;
                printf("[arena_bot %d] drafted hero_id=%d\n", (int)getpid(), hero_id);
            } else if (last.phase == ARENA_PHASE_LIVE) {
                if (last.winner != 0) {
                    printf("[arena_bot %d] match ended, winner=%d\n", (int)getpid(), last.winner);
                    return;
                }
                if (my_owner >= 0 && my_owner < last.count && last.heroes[my_owner].alive) {
                    /* Nearest enemy from the snapshot -- this bot has no
                       access to the authoritative ArenaState, only what any
                       client sees, same information a human player's client
                       would have. */
                    float mx = last.heroes[my_owner].x, mz = last.heroes[my_owner].z;
                    int my_team = (my_owner < last.count / 2) ? 0 : 1;
                    int best = -1;
                    float best_dist = 0;
                    for (int i = 0; i < last.count; i++) {
                        if (!last.heroes[i].alive) continue;
                        int team = (i < last.count / 2) ? 0 : 1;
                        if (team == my_team) continue;
                        float dx = last.heroes[i].x - mx, dz = last.heroes[i].z - mz;
                        float dist = dx * dx + dz * dz;
                        if (best == -1 || dist < best_dist) { best = i; best_dist = dist; }
                    }
                    if (best != -1) {
                        send_move(last.heroes[best].x, last.heroes[best].z);
                        uint32_t now = (uint32_t)time(NULL) * 1000;
                        if (now - last_cast_ms > 2000) {
                            send_cast(0); /* Q -- server no-ops it if actually on cooldown */
                            last_cast_ms = now;
                        }
                    }
                }
            }
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000); /* 10 decisions/sec -- plenty for a first AI pass */
#endif
    }
}

int main(int argc, char *argv[]) {
    const char *ip = "127.0.0.1";
    int direct_port = 0; /* 0 = go through the arena matchmaker */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) ip = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) direct_port = atoi(argv[++i]);
    }

    setbuf(stdout, NULL);
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    load_iduna_agent_config();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        int game_port = direct_port;
        if (game_port == 0) {
            struct sockaddr_in mm_addr;
            mm_addr.sin_family = AF_INET;
            mm_addr.sin_port = htons(ARENA_MATCHMAKER_PORT);
            mm_addr.sin_addr.s_addr = inet_addr(ip);
            printf("[arena_bot %d] finding match via arena matchmaker %s:%d...\n", (int)getpid(), ip, ARENA_MATCHMAKER_PORT);
            send_find_match(&mm_addr);
            game_port = wait_for_match(&mm_addr);
            if (game_port < 0) {
                fprintf(stderr, "[arena_bot %d] matchmaker timeout, retrying\n", (int)getpid());
                continue;
            }
            printf("[arena_bot %d] matched -> game server port %d\n", (int)getpid(), game_port);
        }

        my_owner = -1;
        if (connect_to_server(ip, game_port)) {
            play_one_match();
        } else {
            fprintf(stderr, "[arena_bot %d] failed to connect to server on port %d\n", (int)getpid(), game_port);
        }

        if (direct_port != 0) break; /* direct-connect mode plays exactly one match, matching apps/arena's own --connect */
#ifdef _WIN32
        Sleep(1000);
#else
        usleep(1000000); /* brief pause before requeuing -- persistent, not a tight crash loop */
#endif
    }
    return 0;
}
