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
// reporting (packages/common/http_client.h).
//
// --lobby-size N (default 2): 1v1 (the originally-shipped, live-verified
// mode) uses arena_init/arena_update -- completely unchanged behavior.
// N > 2 (up to ARENA_MAX_HEROES) uses arena_init_teams/arena_update_teams
// (NORTHSTAR §13 cont'd, 10v10 scaling, S170-183 -- reverted after briefly being 7v7 under
// S170-178) -- every slot is a real network
// client (human or a real apps/arena_bot process); there is no internal
// bot-AI fallback in team mode, unlike 1v1's solo-vs-bot practice default.
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
    #include <signal.h>
    #include <execinfo.h>
#endif

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"
#include "../../../packages/simulation/arena_game.h"

static int lobby_size = 2; /* --lobby-size; 2 = original 1v1 mode, up to ARENA_MAX_HEROES for team mode */

static int sock = -1;
static struct sockaddr_in bind_addr;
static struct sockaddr_in clients[ARENA_MAX_HEROES];
static int client_active[ARENA_MAX_HEROES];
static int client_count = 0;
static unsigned char client_player_id[ARENA_MAX_HEROES][16];
static int client_has_player_id[ARENA_MAX_HEROES];

/* Draft phase (2026-07-24): heroes used to be hardcoded (Unicorn vs Duck) --
 * now every real player picks before the match clock starts. */
static int match_phase = ARENA_PHASE_WAITING;
static int hero_picked[ARENA_MAX_HEROES];
static int hero_pick[ARENA_MAX_HEROES];
static int picked_count = 0;

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

    for (int owner = 0; owner < lobby_size; owner++) {
        char pid[37];
        player_id_uuid_str(owner, pid);
        if (pid[0] == '\0') continue;
        int my_team = arena_state.heroes[owner].team;
        const char *result = ((my_team + 1) == winner) ? "win" : "loss";
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"player_id\":\"%s\",\"game\":\"redgarden-arena\",\"result\":\"%s\"}", pid, result);
        if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/game-result", token,
                            body, resp, sizeof(resp), &status) != 0 || status != 200) {
            fprintf(stderr, "WOTAN: game-result report failed for client %d (status=%d)\n", owner, status);
        }

        /* hero-result (2026-07-29, IDUNA Apple #11320): same fact, a second aggregate --
           "which heroes are strongest" (founder: "can we start crunching the data on the heroes
           that are the strongest? ... i want to start tracking it on okemily.com"), cross-player
           by hero_id rather than per-account. Reuses the same agent token already fetched above
           -- IDUNA's /api/v1/redgarden/hero-result requires the same redgarden.match.write
           permission game-result already needs, so no separate auth/permission wiring. */
        char hero_body[128];
        snprintf(hero_body, sizeof(hero_body),
                 "{\"hero_id\":%d,\"result\":\"%s\"}", (int)arena_state.heroes[owner].hero_id, result);
        if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/hero-result", token,
                            hero_body, resp, sizeof(resp), &status) != 0 || status != 200) {
            fprintf(stderr, "WOTAN: hero-result report failed for client %d hero_id=%d (status=%d)\n",
                    owner, (int)arena_state.heroes[owner].hero_id, status);
        }

        /* Reward-credit hook (REDGARDEN_GUI_NORTHSTAR.md Milestone 4, 2026-07-31): credit real
           Flow to this player's persistent DragonsNShit character, if they have one. Gated on
           character lookup succeeding, not on hero_id/job -- a REDGARDEN-only player (the
           common case, no DragonsNShit crossover) gets a real 404 here and is silently skipped,
           same "not every player is a DragonsNShit character" reality game-result/hero-result
           above don't need to care about but this genuinely does. Reuses the same agent token
           already fetched above -- the generic middleware.RequireAuth(keys) IDUNA's characters
           routes use has no extra permission to wire. Amounts (100 win / 25 loss) are this
           milestone's own call -- "exact reward shape not designed here" was the doc's honest
           framing, so these are a first real number, not a design review's output; tune later
           against real playtesting, not guessed twice. */
        char lookup_resp[512];
        int lookup_status = 0;
        char lookup_path[96]; /* "/api/v1/characters/by-player/" (30) + a 36-char UUID + NUL -- 64 was too tight, caught by -Wformat-truncation */
        snprintf(lookup_path, sizeof(lookup_path), "/api/v1/characters/by-player/%s", pid);
        if (http_get_json(iduna_host, iduna_port, lookup_path, token,
                           lookup_resp, sizeof(lookup_resp), &lookup_status) == 0 &&
            lookup_status == 200) {
            char char_id[64];
            if (http_extract_json_string_field(lookup_resp, "character_id", char_id, sizeof(char_id))) {
                int flow_reward = (((my_team + 1) == winner)) ? 100 : 25;
                char credit_path[128];
                snprintf(credit_path, sizeof(credit_path), "/api/v1/characters/%s/gold/credit", char_id);
                char credit_body[64];
                snprintf(credit_body, sizeof(credit_body), "{\"credit\":%d}", flow_reward);
                int credit_status = 0;
                if (http_patch_json(iduna_host, iduna_port, credit_path, token,
                                     credit_body, resp, sizeof(resp), &credit_status) != 0 ||
                    (credit_status != 200 && credit_status != 204)) {
                    fprintf(stderr, "WOTAN: Battlegrounds Flow credit failed for character %s (status=%d)\n",
                            char_id, credit_status);
                } else {
                    printf("WOTAN: credited %d Flow to DragonsNShit character %s (%s)\n",
                           flow_reward, char_id, result);
                }
            }
        }
        /* lookup_status != 200 (almost always 404) is the expected, common case -- not logged as
           an error, same "this is normal, not a failure" reasoning report_match_result's own
           game-result/hero-result calls above don't need because every player has those. */
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

// ---- Live match spectator reporting (2026-07-30, founder: "i want to watch the match on my
// phone web view") -- periodic POST of a compact match-state summary to IDUNA's own
// /api/v1/redgarden/live-match, which holds only the latest one for a simple polling web
// dashboard (okemily.com) to read back. Same WOTAN agent/permission (redgarden.match.write) as
// report_match_result below, a third aggregate over the same authoritative game-server state, not
// a new trust boundary. Token is cached and reused across calls (unlike report_match_result,
// which only ever runs once at match end) -- this fires every LIVE_MATCH_REPORT_INTERVAL_MS
// throughout the whole match, and re-logging in every single call would be a real, avoidable
// extra HTTP round-trip on top of the report itself. Best-effort: a failed POST here just means
// the spectator dashboard is a few seconds stale, not a real error worth failing loudly over --
// same reasoning IDUNA being briefly unreachable already doesn't stop a match from playing out. ----
#define LIVE_MATCH_REPORT_INTERVAL_MS 3000
#define LIVE_MATCH_TOKEN_REFRESH_MS   300000 /* 5 min -- cheap insurance against the JWT expiring mid-match, not measured against its actual TTL */
static char live_match_token[2048] = "";
static unsigned int live_match_token_fetched_ms = 0;

static void report_live_match_state(void) {
    if (!iduna_agent_configured) return;

    unsigned int now = get_server_time();
    if (live_match_token[0] == '\0' || (now - live_match_token_fetched_ms) > LIVE_MATCH_TOKEN_REFRESH_MS) {
        char resp[2048];
        int status = 0;
        char login_body[512];
        snprintf(login_body, sizeof(login_body),
                 "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
                 iduna_agent_name, iduna_agent_secret);
        if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                            login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
            return; /* best-effort -- try again next interval */
        }
        if (!http_extract_json_string_field(resp, "access_token", live_match_token, sizeof(live_match_token))) {
            return;
        }
        live_match_token_fetched_ms = now;
    }

    /* 16384: generous headroom over the realistic worst case (a full 20-hero roster at
       ~100 bytes/hero plus the fixed nodes/towers/wrapper overhead comes to well under 3KB) --
       same "size for real worst case, not exactly today's numbers" caution this file's other
       fixed buffers already take. */
    char body[16384];
    int n = 0;
    n += snprintf(body + n, sizeof(body) - n,
                  "{\"phase\":%d,\"match_elapsed_ms\":%d,\"winner\":%d,\"resources\":[%d,%d],\"nodes\":[",
                  match_phase, arena_state.match_elapsed_ms, arena_state.winner,
                  arena_state.resources[0], arena_state.resources[1]);
    for (int i = 0; i < ARENA_NODE_COUNT && n < (int)sizeof(body); i++) {
        ArenaNode *nd = &arena_state.nodes[i];
        n += snprintf(body + n, sizeof(body) - n, "%s{\"owner\":%d,\"capturing_team\":%d,\"x\":%.1f,\"z\":%.1f}",
                      i == 0 ? "" : ",", nd->owner, nd->capturing_team, nd->x, nd->z);
    }
    n += snprintf(body + n, sizeof(body) - n, "],\"towers\":[");
    for (int i = 0; i < ARENA_NODE_COUNT && n < (int)sizeof(body); i++) {
        ArenaTower *tw = &arena_state.towers[i];
        n += snprintf(body + n, sizeof(body) - n, "%s{\"alive\":%s,\"hp\":%d,\"max_hp\":%d}",
                      i == 0 ? "" : ",", tw->alive ? "true" : "false", tw->hp > 0 ? tw->hp : 0, tw->max_hp);
    }
    n += snprintf(body + n, sizeof(body) - n, "],\"heroes\":[");
    for (int i = 0; i < lobby_size && n < (int)sizeof(body); i++) {
        ArenaHero *h = &arena_state.heroes[i];
        /* x/z (2026-07-30, founder: "can we get coords too and show a little map with emojis")
           -- %.1f is plenty of precision for a small spectator map (a tenth of a unit is well
           under one emoji-marker's own on-screen size) and keeps the payload smaller than a full
           float's worth of decimal digits would for no real benefit here. */
        n += snprintf(body + n, sizeof(body) - n,
                      "%s{\"owner\":%d,\"team\":%d,\"hero_id\":%d,\"hp\":%d,\"max_hp\":%d,"
                      "\"alive\":%s,\"kills\":%d,\"deaths\":%d,\"flow\":%d,\"x\":%.1f,\"z\":%.1f}",
                      i == 0 ? "" : ",", i, h->team, (int)h->hero_id,
                      h->hp > 0 ? h->hp : 0, h->max_hp, h->alive ? "true" : "false",
                      h->kills, h->deaths, h->flow > 0 ? h->flow : 0, h->x, h->z);
    }
    n += snprintf(body + n, sizeof(body) - n, "]}");
    if (n >= (int)sizeof(body)) return; /* truncated -- drop rather than POST a corrupt/incomplete JSON body */

    char resp[256];
    int status = 0;
    http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/live-match", live_match_token,
                    body, resp, sizeof(resp), &status);
    if (status == 401) live_match_token[0] = '\0'; /* force a fresh login next interval */
}

// ---- match event log (same schema arena_replay.c already parses for the
// 1v1 case, so observer-mode/S170-30 keeps working against real 1v1
// networked matches; team-mode matches get their own generalized snapshot
// shape since arena_replay.c only ever knew about 2 heroes) ----
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
    fprintf(match_log_fp, "{\"event\":\"match_start\",\"port\":%d,\"lobby_size\":%d,\"ts_ms\":%u}\n",
            port, lobby_size, get_server_time());
    fflush(match_log_fp);
    printf("Match event log: %s\n", path);
}

static void match_log_snapshot(void) {
    if (!match_log_fp) return;
    if (lobby_size == 2) {
        ArenaHero *a = &arena_state.heroes[0];
        ArenaHero *b = &arena_state.heroes[1];
        fprintf(match_log_fp,
                "{\"event\":\"snapshot\",\"ts_ms\":%u,"
                "\"hero0\":{\"x\":%.2f,\"z\":%.2f,\"hp\":%d},"
                "\"hero1\":{\"x\":%.2f,\"z\":%.2f,\"hp\":%d}}\n",
                get_server_time(), a->x, a->z, a->hp, b->x, b->z, b->hp);
        fflush(match_log_fp);
        return;
    }
    fprintf(match_log_fp, "{\"event\":\"snapshot\",\"ts_ms\":%u,\"heroes\":[", get_server_time());
    for (int i = 0; i < lobby_size; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        fprintf(match_log_fp, "%s{\"owner\":%d,\"team\":%d,\"x\":%.2f,\"z\":%.2f,\"hp\":%d,\"alive\":%d}",
                i == 0 ? "" : ",", i, h->team, h->x, h->z, h->hp, h->alive);
    }
    fprintf(match_log_fp, "]}\n");
    fflush(match_log_fp);
}

static void match_log_win(int winner) {
    if (!match_log_fp) return;
    fprintf(match_log_fp, "{\"event\":\"match_end\",\"winner\":%d,\"ts_ms\":%u}\n", winner, get_server_time());
    fflush(match_log_fp);
}

/* match_log_draft_complete (2026-07-29, founder: "can we start crunching the data on the heroes
 * that are the strongest? does our match replay system let us start tracking stats like win
 * rate etc?"). Answer at the time this was asked: no -- neither this log's own snapshot/
 * match_end events nor report_match_result's IDUNA POST below ever recorded which hero_id a
 * given owner actually played, only x/y/hp/alive and win/loss by player_id. 5,860 match logs
 * already sitting in var/matches/ are unrecoverable for this because of that gap -- hero
 * identity was simply never written down anywhere. This closes it going forward: one line per
 * match, right when the draft actually finishes (same moment `match_phase` flips to
 * ARENA_PHASE_LIVE), giving every later `match_end` in the same file a hero_id to join against
 * without needing to reconstruct picks from the noisy per-tick snapshot stream. */
static void match_log_draft_complete(void) {
    if (!match_log_fp) return;
    fprintf(match_log_fp, "{\"event\":\"draft_complete\",\"ts_ms\":%u,\"heroes\":[", get_server_time());
    for (int i = 0; i < lobby_size; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        fprintf(match_log_fp, "%s{\"owner\":%d,\"team\":%d,\"hero_id\":%d}",
                i == 0 ? "" : ",", i, h->team, (int)h->hero_id);
    }
    fprintf(match_log_fp, "]}\n");
    fflush(match_log_fp);
}

/* ---- AI training corpus log (S170-194, NORTHSTAR §18.4 -- founder: "do the work to prepare
 * for unsupervised learning" / "target torch training on colab"). Separate file from
 * match_log_fp above on purpose: that log is the observer/replay-viewer's own minimal x/z/hp
 * schema (arena_replay.c's contract, not extensible without breaking observer mode); this one
 * is arena_corpus_record's own state+action JSONL shape, one line per active hero per tick,
 * already in the exact `{"text": "..."}` format scripts/colab_train.py (ported from
 * gpt2-alpine-c, see that file's own header) expects with zero conversion needed. */
static FILE *corpus_log_fp = NULL;

static void corpus_log_open(int port) {
    mkdir("var", 0755);
    mkdir("var/corpus", 0755);
    char path[256];
    snprintf(path, sizeof(path), "var/corpus/arena-corpus-%d-%ld.jsonl", port, (long)time(NULL));
    corpus_log_fp = fopen(path, "a");
    if (!corpus_log_fp) {
        printf("WARNING: could not open AI corpus log %s -- this match's training data will not be recorded\n", path);
        return;
    }
    printf("AI training corpus log: %s\n", path);
}

static void corpus_log_tick(unsigned int tick_ms) {
    if (!corpus_log_fp) return;
    char record[1024];
    for (int i = 0; i < lobby_size; i++) {
        if (!arena_state.heroes[i].active || !arena_state.heroes[i].alive) continue;
        arena_corpus_record(i, tick_ms, record, sizeof(record));
        if (record[0] != '\0') fprintf(corpus_log_fp, "%s\n", record);
    }
    fflush(corpus_log_fp);
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
    printf("ARENA SERVER LISTENING ON PORT %d\nWaiting for %d players...\n", port, lobby_size);
}

/* fill_hero_snapshot (2026-07-30, extracted while wiring Tyler's clone pool onto the wire for
 * the first time): every per-field copy from a real ArenaHero to its wire ArenaHeroSnapshot,
 * shared between the real-player loop and the new clone loop below rather than duplicated --
 * clones need every one of these same fields (position, HP, cooldowns, status effects, etc.) to
 * render and read correctly client-side, exactly like any other hero. is_clone/clone_owner are
 * the only fields the CALLER still sets itself, since a real hero and a clone entry set them
 * differently (see each call site). */
static void fill_hero_snapshot(ArenaHeroSnapshot *hs, const ArenaHero *h) {
    hs->x = h->x;
    hs->z = h->z;
    hs->hp = (uint16_t)(h->hp > 0 ? h->hp : 0);
    hs->max_hp = (uint16_t)h->max_hp;
    hs->alive = (uint8_t)h->alive;
    hs->hero_id = (uint8_t)h->hero_id;
    hs->cast_flash_slot = (uint8_t)h->cast_flash_slot;
    /* S170-137: cooldown/mp can both dip transiently negative between
       ticks (see tick_hero_kit's own `-= dt_ms`, never clamped back to
       0 until the next `> 0` gate) -- clamp before the signed->unsigned
       narrowing, same convention as hp's own clamp just above. */
    hs->q_cooldown_ms = (uint16_t)(h->q_cooldown_ms > 0 ? h->q_cooldown_ms : 0);
    hs->w_cooldown_ms = (uint16_t)(h->w_cooldown_ms > 0 ? h->w_cooldown_ms : 0);
    hs->r_cooldown_ms = (uint16_t)(h->r_cooldown_ms > 0 ? h->r_cooldown_ms : 0);
    hs->mp = (uint8_t)(h->mp > 0 ? h->mp : 0);
    hs->attack_target = (int8_t)h->attack_target; /* S170-162 */
    hs->flow = (uint16_t)(h->flow > 0 ? h->flow : 0); /* S170-175 */
    hs->flow_earned = (uint16_t)h->flow_earned;
    hs->xp = (uint16_t)h->xp;
    hs->kills = (uint8_t)h->kills;
    hs->deaths = (uint8_t)h->deaths;
    for (int s = 0; s < ARENA_SNAPSHOT_ITEM_SLOT_COUNT; s++) {
        hs->equipped_item[s] = (int8_t)h->equipped_item[s];
    }
    hs->w_active = (uint8_t)h->w_active; /* S170-180 bugfix */
    /* S170-184 bugfix: status effects were never synced at all, see ArenaHeroSnapshot's
       own doc comment. Clamp before the signed->unsigned narrowing, same convention as
       cooldowns' own clamp above (these can dip transiently negative between ticks too). */
    hs->silenced_ms = (uint16_t)(h->silenced_ms > 0 ? h->silenced_ms : 0);
    hs->rooted_ms = (uint16_t)(h->rooted_ms > 0 ? h->rooted_ms : 0);
    hs->intangible_ms = (uint16_t)(h->intangible_ms > 0 ? h->intangible_ms : 0);
    hs->burning_ms = (uint16_t)(h->burning_ms > 0 ? h->burning_ms : 0);
    hs->survive_floor_ms = (uint16_t)(h->survive_floor_ms > 0 ? h->survive_floor_ms : 0);
    hs->stunned_ms = (uint16_t)(h->stunned_ms > 0 ? h->stunned_ms : 0);
    hs->slowed_ms = (uint16_t)(h->slowed_ms > 0 ? h->slowed_ms : 0);
    hs->slow_pct_x100 = (uint8_t)(h->slow_pct * 100.0f);
    hs->berserker_ms = (uint16_t)(h->berserker_ms > 0 ? h->berserker_ms : 0); /* S170-190 */
    hs->regen_ms = (uint16_t)(h->regen_ms > 0 ? h->regen_ms : 0);
    hs->r_zone_x = h->r_zone_x; /* S170-200 */
    hs->r_zone_z = h->r_zone_z;
    hs->r_active_ms = (uint16_t)(h->r_active_ms > 0 ? h->r_active_ms : 0);
    hs->zone_radius_x10 = (h->hero_id == ARENA_HERO_CART) ? (uint8_t)(h->zone_radius * 10.0f) : 0; /* S202-42 */
    hs->casting_slot = (uint8_t)h->casting_slot; /* S170-203 */
    hs->cast_time_remaining_ms = (uint16_t)(h->cast_time_remaining_ms > 0 ? h->cast_time_remaining_ms : 0);
    hs->cast_total_ms = (uint16_t)(h->cast_total_ms > 0 ? h->cast_total_ms : 0);
    hs->blink_cooldown_ms = (uint16_t)(h->blink_cooldown_ms > 0 ? h->blink_cooldown_ms : 0); /* S170-205 */
    hs->donkey_glide_cooldown_ms = (uint16_t)(h->donkey_glide_cooldown_ms > 0 ? h->donkey_glide_cooldown_ms : 0); /* S170-206 */
    /* King buff status, 2026-08-20 -- see ArenaHeroSnapshot's own doc comment for the bit
       layout. team_id_is_valid guards the All-Seeing lookup the same way select_ctf_bot_intent
       already guards team-indexed King state elsewhere in this file's own package. */
    hs->king_buff_flags = 0;
    if (h->king_music_carrier) hs->king_buff_flags |= 0x01;
    if (h->king_growth_ms > 0) hs->king_buff_flags |= 0x02;
    if (h->king_wealth_ms > 0) hs->king_buff_flags |= 0x04;
    if (h->team >= 0 && h->team < 2 && arena_state.king_allseeing_team_ms[h->team] > 0) hs->king_buff_flags |= 0x08;
    hs->king_growth_stacks = (uint8_t)(h->king_growth_stacks > 0 ? h->king_growth_stacks : 0);
    hs->duck_smoke_x = h->duck_smoke_x; /* S202-10 */
    hs->duck_smoke_z = h->duck_smoke_z;
    hs->duck_smoke_ms = (uint16_t)(h->duck_smoke_ms > 0 ? h->duck_smoke_ms : 0);
}

static void server_broadcast(void) {
    /* S170-193: heroes[] now goes out as ARENA_SNAPSHOT_HERO_CHUNKS separate
       PACKET_ARENA_SNAPSHOT_HEROES packets instead of living inside this
       (now much smaller) world message -- see ArenaSnapshotHeroesMsg's own
       doc comment in protocol.h for the full MTU-fragmentation story. */
    char buffer[sizeof(NetHeader) + sizeof(ArenaSnapshotMsg)];
    NetHeader head = {0};
    head.type = PACKET_ARENA_SNAPSHOT;
    head.timestamp = get_server_time();

    ArenaSnapshotHeroesMsg chunks[ARENA_SNAPSHOT_HERO_CHUNKS] = {0};
    for (int c = 0; c < ARENA_SNAPSHOT_HERO_CHUNKS; c++) {
        chunks[c].chunk_index = (uint8_t)c;
        chunks[c].total_count = (uint8_t)lobby_size;
    }

    ArenaSnapshotMsg msg = {0};
    msg.count = (uint8_t)lobby_size;
    for (int i = 0; i < lobby_size; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        ArenaHeroSnapshot *hs = &chunks[i / ARENA_SNAPSHOT_HERO_CHUNK_SIZE].heroes[i % ARENA_SNAPSHOT_HERO_CHUNK_SIZE];
        fill_hero_snapshot(hs, h);
        hs->is_clone = 0;
        hs->clone_owner = -1;
        msg.picked[i] = (uint8_t)hero_picked[i];
    }
    /* 2026-07-30, Tyler "Divided We Stand" rework -- founder: "his kit was stubbed in." Real gap
       found while building independent clone control: this loop only ever covered
       0..lobby_size-1, so Tyler's puppet clones (ARENA_MAX_HEROES..ARENA_HEROES_ARRAY_SIZE-1)
       were NEVER synced to any client at all -- they existed and fought server-side but were
       completely invisible in every real networked match. Always fully populated regardless of
       `is_clone` (an inactive slot just carries is_clone=0, matching the "always synced, dead
       ones just sit inert" convention creeps/towers already use), so a freed clone slot
       correctly disappears client-side the instant it dies, no lingering stale entry. */
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        ArenaHeroSnapshot *hs = &chunks[i / ARENA_SNAPSHOT_HERO_CHUNK_SIZE].heroes[i % ARENA_SNAPSHOT_HERO_CHUNK_SIZE];
        int occupied = h->active && h->is_clone;
        hs->is_clone = (uint8_t)occupied;
        hs->clone_owner = occupied ? (int8_t)h->clone_owner : -1;
        if (!occupied) continue; /* leave the rest of the entry zeroed -- an empty slot */
        fill_hero_snapshot(hs, h);
    }
    msg.winner = (uint8_t)arena_state.winner;
    msg.phase = (uint8_t)match_phase;
    for (int i = 0; i < ARENA_SNAPSHOT_NODE_COUNT; i++) {
        ArenaNode *node = &arena_state.nodes[i];
        msg.nodes[i].x = node->x;
        msg.nodes[i].z = node->z;
        msg.nodes[i].owner = (uint8_t)node->owner;
        msg.nodes[i].capturing_team = (int8_t)node->capturing_team;
        msg.nodes[i].capture_progress_ms = (uint16_t)(node->capture_progress_ms > 0 ? node->capture_progress_ms : 0);
    }
    /* S170-136: projectiles are a sparse pool (most slots inactive at any
       given tick), unlike heroes/nodes which are always fully populated --
       pack only the active ones, front-to-back, same "count + fixed array"
       convention as everything else in this message. */
    for (int i = 0; i < ARENA_MAX_PROJECTILES && msg.projectile_count < ARENA_SNAPSHOT_MAX_PROJECTILES; i++) {
        ArenaProjectile *p = &arena_state.projectiles[i];
        if (!p->active) continue;
        int slot = msg.projectile_count++;
        msg.projectiles[slot].x = p->x;
        msg.projectiles[slot].z = p->z;
        msg.projectiles[slot].owner = (uint8_t)p->owner;
        msg.projectiles[slot].hero_id = (uint8_t)p->hero_id;
    }
    /* S170-146: node-guardian creeps are always fully populated (one per node,
       dead ones just sit at alive=0), same convention as heroes/nodes --
       fixed-size array, not sparse-packed. */
    for (int i = 0; i < ARENA_SNAPSHOT_CREEP_COUNT; i++) {
        ArenaCreep *cr = &arena_state.creeps[i];
        msg.creeps[i].x = cr->x;
        msg.creeps[i].z = cr->z;
        msg.creeps[i].hp = (uint16_t)(cr->hp > 0 ? cr->hp : 0);
        msg.creeps[i].max_hp = (uint16_t)cr->max_hp;
        msg.creeps[i].alive = (uint8_t)cr->alive;
        msg.creeps[i].flavor = (uint8_t)cr->flavor;
    }
    /* 2026-07-30: node towers -- always fully populated (one per node, a dead one just sits at
       alive=0 forever, no respawn), same convention as node-guardian creeps just above. */
    for (int i = 0; i < ARENA_SNAPSHOT_TOWER_COUNT; i++) {
        ArenaTower *tw = &arena_state.towers[i];
        msg.towers[i].x = tw->x;
        msg.towers[i].z = tw->z;
        msg.towers[i].hp = (uint16_t)(tw->hp > 0 ? tw->hp : 0);
        msg.towers[i].max_hp = (uint16_t)tw->max_hp;
        msg.towers[i].alive = (uint8_t)tw->alive;
    }
    /* S170-190: powerups are always fully populated (fixed at ARENA_POWERUP_COUNT), same
       "not sparse-packed" convention as node-guardian creeps just above. */
    for (int i = 0; i < ARENA_SNAPSHOT_POWERUP_COUNT; i++) {
        ArenaPowerup *pu = &arena_state.powerups[i];
        msg.powerups[i].x = pu->x;
        msg.powerups[i].z = pu->z;
        msg.powerups[i].kind = (uint8_t)pu->kind;
        msg.powerups[i].active = (uint8_t)pu->active;
    }
    /* S170-146: lane creeps are a sparse pool, same pack-only-active
       convention as projectiles above. */
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS && msg.lane_creep_count < ARENA_SNAPSHOT_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *lc = &arena_state.lane_creeps[i];
        if (!lc->active || !lc->alive) continue;
        int slot = msg.lane_creep_count++;
        msg.lane_creeps[slot].x = lc->x;
        msg.lane_creeps[slot].z = lc->z;
        msg.lane_creeps[slot].hp = (uint16_t)(lc->hp > 0 ? lc->hp : 0);
        msg.lane_creeps[slot].max_hp = (uint16_t)lc->max_hp;
        msg.lane_creeps[slot].team = (uint8_t)lc->team;
        msg.lane_creeps[slot].role = (uint8_t)lc->role; /* S170-218 */
    }
    msg.resources[0] = (uint16_t)arena_state.resources[0]; /* S170-153 */
    msg.resources[1] = (uint16_t)arena_state.resources[1];

    /* Jungle camps client-visibility fix, 2026-08-20: camp_minions/kings have been simulated
       server-side since Jungle Camps Milestones 1/2 (commits 1402702/3943f94) but never had a
       wire representation, so no client ever received or rendered them. Camp minions are a
       sparse pool, same pack-only-active convention as projectiles/lane creeps above. */
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS && msg.camp_minion_count < ARENA_SNAPSHOT_MAX_CAMP_MINIONS; i++) {
        ArenaCampMinion *cm = &arena_state.camp_minions[i];
        if (!cm->active || !cm->alive) continue;
        int slot = msg.camp_minion_count++;
        msg.camp_minions[slot].x = cm->x;
        msg.camp_minions[slot].z = cm->z;
        msg.camp_minions[slot].hp = (uint16_t)(cm->hp > 0 ? cm->hp : 0);
        msg.camp_minions[slot].max_hp = (uint16_t)cm->max_hp;
        msg.camp_minions[slot].camp_index = (uint8_t)cm->camp_index;
    }
    /* Kings are always fully populated (one per camp), same "not sparse-packed" convention as
       node towers/creeps above -- a not-yet-spawned or dead King just sits at alive=0. */
    for (int i = 0; i < ARENA_SNAPSHOT_CAMP_COUNT; i++) {
        ArenaKing *k = &arena_state.kings[i];
        msg.kings[i].x = k->x;
        msg.kings[i].z = k->z;
        msg.kings[i].hp = (uint16_t)(k->hp > 0 ? k->hp : 0);
        msg.kings[i].max_hp = (uint16_t)k->max_hp;
        msg.kings[i].alive = (uint8_t)k->alive;
    }

    /* Tree passive (2026-08-25): obstacles are always fully populated (fixed layout, never
       sparse), same convention as kings/towers/creeps above -- only ARENA_OBSTACLE_TREE entries
       carry a real value, rocks stay 0. */
    for (int i = 0; i < ARENA_SNAPSHOT_OBSTACLE_COUNT && i < ARENA_OBSTACLE_COUNT; i++) {
        msg.obstacle_hp[i] = (uint16_t)(arena_state.obstacles[i].hp > 0 ? arena_state.obstacles[i].hp : 0);
    }

    memcpy(buffer, &head, sizeof(NetHeader));
    memcpy(buffer + sizeof(NetHeader), &msg, sizeof(ArenaSnapshotMsg));

    /* S170-193: each hero chunk goes out as its own independent packet, same NetHeader
       framing as the world message above but its own (smaller) buffer/type -- three sends
       per client per broadcast tick now instead of one, each individually well under a real
       MTU. */
    char hero_buffer[sizeof(NetHeader) + sizeof(ArenaSnapshotHeroesMsg)];
    NetHeader hero_head = {0};
    hero_head.type = PACKET_ARENA_SNAPSHOT_HEROES;
    hero_head.timestamp = head.timestamp;
    memcpy(hero_buffer, &hero_head, sizeof(NetHeader));

    for (int i = 0; i < lobby_size; i++) {
        if (!client_active[i]) continue;
        sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&clients[i], sizeof(struct sockaddr_in));
        for (int c = 0; c < ARENA_SNAPSHOT_HERO_CHUNKS; c++) {
            memcpy(hero_buffer + sizeof(NetHeader), &chunks[c], sizeof(ArenaSnapshotHeroesMsg));
            sendto(sock, hero_buffer, sizeof(hero_buffer), 0, (struct sockaddr *)&clients[i], sizeof(struct sockaddr_in));
        }
    }

    /* cast_flash_slot is a one-tick wire signal (S170-124) -- already
       copied into msg above, clear the sim's own copy now so it doesn't
       leak into next tick's broadcast too. */
    for (int i = 0; i < lobby_size; i++) {
        arena_state.heroes[i].cast_flash_slot = 0;
    }
}

static void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) return;
    NetHeader *head = (NetHeader *)buffer;
    int client_id = -1;

    for (int i = 0; i < lobby_size; i++) {
        if (client_active[i] &&
            memcmp(&clients[i].sin_addr, &sender->sin_addr, sizeof(struct in_addr)) == 0 &&
            clients[i].sin_port == sender->sin_port) {
            client_id = i;
            break;
        }
    }

    if (client_id == -1 && head->type == PACKET_CONNECT) {
        if (!verify_connect_ticket(buffer, size)) return;
        if (match_phase != ARENA_PHASE_WAITING) return; // lobby's full/drafting/live -- no late joins in this pass
        for (int i = 0; i < lobby_size; i++) {
            if (!client_active[i]) {
                client_id = i;
                client_active[i] = 1;
                client_count++;
                clients[i] = *sender;
                const unsigned char *ticket_payload = (const unsigned char *)(buffer + sizeof(NetHeader));
                memcpy(client_player_id[client_id], ticket_payload, 16);
                client_has_player_id[client_id] = 1;

                NetHeader h = {0};
                h.type = PACKET_WELCOME;
                h.client_id = (uint8_t)client_id;
                h.timestamp = get_server_time();
                sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)sender, sizeof(struct sockaddr_in));
                printf("CLIENT %d CONNECTED (owner %d, %d/%d)\n", client_id, client_id, client_count, lobby_size);

                // Once the lobby is full, stop any internal bot AI (1v1's
                // solo-practice fallback -- team mode never enables it at
                // all, see main()) and enter the draft instead of going
                // straight to a live match.
                if (client_count == lobby_size) {
                    arena_bot_enabled = 0;
                    if (lobby_size == 2) {
                        arena_init(); /* 1v1: keeps the exact proven local-demo spawn/state shape */
                    } else {
                        arena_init_teams(); /* team mode: N-hero spawn, all placeholder hero_id until picks land */
                        /* 2026-07-30, founder: "add towers around the nodes so beginning of game
                           is a little slower" -- team-mode only, same "doesn't make sense in solo
                           practice" scope lane creep waves already carry (see
                           arena_tick_lane_creeps' own doc comment). Deliberately NOT inside
                           arena_init_teams() itself: that shared sim-level function is also called
                           directly by ~300 existing unit tests that place heroes at convenient
                           coordinates (some literally at the Blacksmith node's own (0,0)) never
                           expecting anything to auto-attack them there -- towers defaulting to
                           dead everywhere except this one real match-start call site keeps every
                           existing test's behavior unchanged while still shipping the real
                           feature for actual matches. */
                        arena_towers_reset();
                    }
                    match_phase = ARENA_PHASE_DRAFT;
                    printf("Lobby full (%d players) -- internal bot AI disabled, entering draft.\n", lobby_size);
                }
                break;
            }
        }
        return;
    }

    if (client_id == -1) return; // unknown sender, not a connect -- ignore

    if (head->type == PACKET_ARENA_PICK) {
        if (match_phase != ARENA_PHASE_DRAFT) return; // picks only mean anything during draft
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaPickCmd))) return;
        ArenaPickCmd *cmd = (ArenaPickCmd *)(buffer + sizeof(NetHeader));
        // S170-230: was hard-coded against ARENA_HERO_MNM, silently unpickable-over-network
        // for every hero added since (Weatherman, now Zagan) -- compare against the real
        // roster size instead so this doesn't go stale a third time.
        if (cmd->hero_id < 0 || cmd->hero_id >= ARENA_HERO_COUNT) return; // reject anything outside the real roster
        if (!hero_picked[client_id]) picked_count++;
        hero_pick[client_id] = cmd->hero_id;
        hero_picked[client_id] = 1;
        arena_state.heroes[client_id].hero_id = (ArenaHeroID)cmd->hero_id;
        printf("CLIENT %d picked hero_id=%d (%d/%d picked)\n", client_id, cmd->hero_id, picked_count, lobby_size);
        if (picked_count == lobby_size) {
            if (lobby_size == 2) {
                /* Keeps the exact proven 1v1 spawn/state shape rather than
                   relying on arena_init_teams' spread-out team spawns. */
                arena_init_with_heroes((ArenaHeroID)hero_pick[0], (ArenaHeroID)hero_pick[1]);
            }
            /* Team mode: hero_id was already applied per-slot above as each
               pick arrived; arena_init_teams' spawn positions/HP/team
               assignment stand as-is. */
            match_phase = ARENA_PHASE_LIVE;
            printf("All %d heroes picked -- match live.\n", lobby_size);
            match_log_draft_complete();
        }
        return;
    }

    if (match_phase != ARENA_PHASE_LIVE) return; // move/cast only mean anything in a live match

    if (head->type == PACKET_ARENA_MOVE) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaMoveCmd))) return;
        ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buffer + sizeof(NetHeader));
        /* 2026-07-30, Tyler clone-control rework: unit_owner names which of the sender's OWN
           units (itself, or one of its own active puppet clones) this command is for --
           arena_owner_controls is the same trust boundary this handler always enforced (a client
           can only ever affect its own hero), just widened to a small owned set instead of
           exactly one slot. An unauthorized unit_owner is silently dropped, same "malformed/
           out-of-bounds input is just ignored" convention every other handler here already uses. */
        if (arena_owner_controls(client_id, cmd->unit_owner)) {
            /* Real, server-side diagnostic (2026-08-26, founder: "AT ONE POINT I GOT STUCK
               SIDEWAYS AND HE WAS LIKE TRYING TO ROTATE TO RUN WHERE I WANTED BUT KIND
               ACOULDNT IT WAS WEIRD" -> "CAN WE ADD LOGS TO HELP DEBUG IT?" -- an
               intermittent, "after a while of playing" movement bug with no hard repro
               yet). Logs every real move command for a real hero (not a clone/creep) along
               with every real state that could silently block update_hero_motion from
               advancing (rooted_ms/stunned_ms/attack_windup_ms_remaining/casting_slot) --
               if any of those is stuck nonzero across many consecutive move commands, that's
               the real, readable-from-var/logs/matchmaker-bots.log signature of a "can't
               move/rotate" bug. Temporary, meant to come back out once this is root-caused,
               same discipline the earlier [abraham-debug] traces this session already used
               and later removed. */
            if (cmd->unit_owner >= 0 && cmd->unit_owner < ARENA_MAX_HEROES) {
                ArenaHero *mv_dbg = &arena_state.heroes[cmd->unit_owner];
                fprintf(stderr, "[move-debug] owner=%d target=(%.2f,%.2f) cur=(%.2f,%.2f) "
                                "facing=%.3f rooted_ms=%d stunned_ms=%d attack_windup_ms=%d "
                                "casting_slot=%d moving=%d\n",
                        cmd->unit_owner, cmd->target_x, cmd->target_z, mv_dbg->x, mv_dbg->z,
                        mv_dbg->facing_rad, mv_dbg->rooted_ms, mv_dbg->stunned_ms,
                        mv_dbg->attack_windup_ms_remaining, mv_dbg->casting_slot, mv_dbg->moving);
            }
            arena_set_move_target(cmd->unit_owner, cmd->target_x, cmd->target_z);
        }
    } else if (head->type == PACKET_ARENA_CAST) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaCastCmd))) return;
        ArenaCastCmd *cmd = (ArenaCastCmd *)(buffer + sizeof(NetHeader));
        /* S170-143: record the hover target BEFORE dispatching -- generic
           on the server side (any slot could consult it), the individual
           cast function decides whether it actually cares (only Doc
           Wheel's Q does today, via arena_hover_ally_or_nearest). */
        arena_set_hover_target(client_id, cmd->hover_target);
        /* S202-34: same "record right before dispatch, individual cast function decides
           whether it cares" shape as hover_target just above -- only Abraham's W does today. */
        arena_set_ground_target(client_id, cmd->has_ground_target, cmd->target_x, cmd->target_z);
        if (cmd->slot == 0) arena_cast_q(client_id);
        else if (cmd->slot == 1) arena_toggle_w(client_id);
        else if (cmd->slot == 2) arena_cast_r(client_id);
    } else if (head->type == PACKET_ARENA_ATTACK) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaAttackCmd))) return;
        ArenaAttackCmd *cmd = (ArenaAttackCmd *)(buffer + sizeof(NetHeader));
        /* Same commander_unit authorization as PACKET_ARENA_MOVE above -- see that branch's own
           doc comment. */
        if (arena_owner_controls(client_id, cmd->commander_unit)) {
            arena_set_attack_target(cmd->commander_unit, cmd->target_owner);
        }
    } else if (head->type == PACKET_ARENA_STOP) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaStopCmd))) return;
        ArenaStopCmd *cmd = (ArenaStopCmd *)(buffer + sizeof(NetHeader));
        /* Same commander/unit_owner authorization as PACKET_ARENA_MOVE/PACKET_ARENA_ATTACK
           above -- NORTHSTAR.md §24 Milestone 2. */
        if (arena_owner_controls(client_id, cmd->unit_owner)) {
            arena_stop_unit(cmd->unit_owner);
        }
    } else if (head->type == PACKET_ARENA_ATTACK_MOVE) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaAttackMoveCmd))) return;
        ArenaAttackMoveCmd *cmd = (ArenaAttackMoveCmd *)(buffer + sizeof(NetHeader));
        /* Same commander/unit_owner authorization as PACKET_ARENA_MOVE/PACKET_ARENA_ATTACK/
           PACKET_ARENA_STOP above -- NORTHSTAR.md §17.4 + §24 Milestone 2. */
        if (arena_owner_controls(client_id, cmd->unit_owner)) {
            arena_set_attack_move_target(cmd->unit_owner, cmd->target_x, cmd->target_z);
        }
    } else if (head->type == PACKET_ARENA_HOLD) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaHoldCmd))) return;
        ArenaHoldCmd *cmd = (ArenaHoldCmd *)(buffer + sizeof(NetHeader));
        /* Same commander/unit_owner authorization as every other group-order packet above --
           NORTHSTAR.md §24 Milestone 2. */
        if (arena_owner_controls(client_id, cmd->unit_owner)) {
            arena_hold_position(cmd->unit_owner);
        }
    } else if (head->type == PACKET_ARENA_PATROL) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaPatrolCmd))) return;
        ArenaPatrolCmd *cmd = (ArenaPatrolCmd *)(buffer + sizeof(NetHeader));
        /* Same commander/unit_owner authorization as every other group-order packet above --
           NORTHSTAR.md §24 Milestone 2. */
        if (arena_owner_controls(client_id, cmd->unit_owner)) {
            arena_set_patrol_target(cmd->unit_owner, cmd->target_x, cmd->target_z);
        }
    } else if (head->type == PACKET_ARENA_SHOP_BUY) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaShopBuyCmd))) return;
        ArenaShopBuyCmd *cmd = (ArenaShopBuyCmd *)(buffer + sizeof(NetHeader));
        /* arena_shop_buy itself validates range/proximity/Flow -- the
           server just forwards the sending client's own owner slot, same
           trust model as every other Arena*Cmd handler above. */
        arena_shop_buy(client_id, cmd->item_id);
    } else if (head->type == PACKET_ARENA_SHOP_SELL) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaShopSellCmd))) return;
        ArenaShopSellCmd *cmd = (ArenaShopSellCmd *)(buffer + sizeof(NetHeader));
        arena_shop_sell(client_id, (ArenaItemSlot)cmd->slot);
    } else if (head->type == PACKET_ARENA_APPLY_BUILD_TEMPLATE) {
        if (size < (int)(sizeof(NetHeader) + sizeof(ArenaApplyBuildTemplateCmd))) return;
        ArenaApplyBuildTemplateCmd *cmd = (ArenaApplyBuildTemplateCmd *)(buffer + sizeof(NetHeader));
        /* arena_hero_apply_build_template itself validates everything (range/proximity/Flow per
           item, via arena_shop_buy underneath) -- same trust model as PACKET_ARENA_SHOP_BUY. */
        arena_hero_apply_build_template(client_id, cmd->template_id);
    } else if (head->type == PACKET_ARENA_BLINK) {
        /* S170-205/S170-206: no payload -- arena_use_active_item itself figures out which
           active item (Blink Dagger or Donkey) the sending client actually has equipped and
           validates cooldown/stun/direction server-side, same trust model as every other
           Arena*Cmd handler above. */
        arena_use_active_item(client_id);
    }
}

#ifndef _WIN32
/* crash_signal_handler (2026-08-02, real live incident: matches spawned for a real human player
 * were disappearing entirely -- process gone, zero snapshots, zero trace of why, not even a
 * "match_end" -- while every controlled bot-only reproduction attempt (20 bots, every hero
 * including Warrior/Cart) ran clean. Without a signal handler there was nothing to look at after
 * the fact: this dumps the real match state (phase, who'd picked, which hero_id each owner had)
 * plus a real backtrace to stderr (captured into var/logs/bot-pool.log by the live systemd unit)
 * and into the match's own JSONL log if it's open, then re-raises so the OS still produces its
 * own core/exit-code behavior unchanged. Deliberately uses fprintf/backtrace_symbols_fd rather
 * than hand-rolling strictly async-signal-safe output -- the process is already crashing and
 * about to exit either way; real diagnostics now are worth more than a theoretically-cleaner
 * handler that tells us nothing next time this happens. POSIX-only (execinfo.h has no Windows
 * equivalent) -- arena_server is never cross-compiled for Windows (only apps/arena, the client,
 * is; see .github/workflows/ci.yml), so this doesn't need a Windows fallback. */
static void crash_signal_handler(int sig) {
    const char *sig_name = "?";
    switch (sig) {
        case SIGSEGV: sig_name = "SIGSEGV"; break;
        case SIGABRT: sig_name = "SIGABRT"; break;
        case SIGFPE:  sig_name = "SIGFPE";  break;
        case SIGBUS:  sig_name = "SIGBUS";  break;
        case SIGILL:  sig_name = "SIGILL";  break;
    }
    fprintf(stderr, "\n=== ARENA_SERVER CRASH: signal %d (%s) ===\n", sig, sig_name);
    fprintf(stderr, "match_phase=%d lobby_size=%d picked_count=%d\n", (int)match_phase, lobby_size, picked_count);
    for (int i = 0; i < lobby_size && i < ARENA_MAX_HEROES; i++) {
        fprintf(stderr, "  owner=%d picked=%d hero_id=%d team=%d alive=%d hp=%d\n",
                i, hero_picked[i], (int)arena_state.heroes[i].hero_id,
                arena_state.heroes[i].team, arena_state.heroes[i].alive, arena_state.heroes[i].hp);
    }
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "--- backtrace (%d frames) ---\n", n);
    fflush(stderr);
    backtrace_symbols_fd(bt, n, 2); /* fd 2 = stderr; safer under a signal than the malloc'ing backtrace_symbols() */
    if (match_log_fp) {
        fprintf(match_log_fp, "{\"event\":\"crash\",\"signal\":%d,\"signal_name\":\"%s\",\"match_phase\":%d,\"picked_count\":%d}\n",
                sig, sig_name, (int)match_phase, picked_count);
        fflush(match_log_fp);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

int main(int argc, char *argv[]) {
#ifndef _WIN32
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
#endif
    int port = 7200;
    unsigned int seed_arg = 0;
    int have_seed_arg = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lobby-size") == 0 && i + 1 < argc) {
            lobby_size = atoi(argv[++i]);
            if (lobby_size < 2) lobby_size = 2;
            if (lobby_size > ARENA_MAX_HEROES) lobby_size = ARENA_MAX_HEROES;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            /* DUNGEON_NORTHSTAR.md Milestone 1: "a way to pass a seed... into the spawned
             * process." The matchmaker now always passes one; when present, use it for this
             * process's own srand() below instead of time(NULL)^getpid() so a match's RNG
             * outcomes are reproducible from the seed the matchmaker handed the client too. */
            seed_arg = (unsigned int)strtoul(argv[++i], NULL, 10);
            have_seed_arg = 1;
        }
    }
    /* No srand() call existed anywhere in this file before (found while wiring The Cart's real
       rand()-based delivery outcomes, NORTHSTAR §24 Milestone 2, 2026-07-31) -- rand() without
       this would use the C library's default seed (1) every single server process start,
       making a "random" delivery completely predictable across restarts on the one binary that
       actually arbitrates real networked matches. apps/arena and apps/arena_bot already seed
       their own local RNG the same way; this was the one binary that never needed it before. */
    srand(have_seed_arg ? seed_arg : ((unsigned int)time(NULL) ^ (unsigned int)getpid()));
    load_ticket_secret();
    load_iduna_agent_config();
    server_net_init(port);
    match_log_open(port);
    corpus_log_open(port);
    /* Nothing is simulated yet -- arena_init()/arena_init_teams() runs once
       the lobby fills (server_handle_packet), not here, so the match clock
       genuinely can't start before real players are present (found live,
       2026-07-24, the earlier "sim starts too early" bug). */
    arena_bot_enabled = (lobby_size == 2); /* team mode never uses the local-practice bot fallback */

    int running = 1;
    int last_winner_logged = 0;
    unsigned int snapshot_log_timer_ms = 0;
    unsigned int live_match_report_timer_ms = 0; /* 2026-07-30: see report_live_match_state's own doc comment */
    /* Shutdown countdown once the match ends -- found live, 2026-07-24: this
       process used to just loop forever after match_end, still broadcasting
       PACKET_ARENA_SNAPSHOT every 16ms to clients that had long since moved
       on to their next match. For a persistent bot (one UDP socket reused
       across many matches, never explicitly disconnected -- there's no
       PACKET_DISCONNECT in this protocol), every prior match server it ever
       played on kept blasting stale packets at its socket forever, and that
       pileup was silently swallowing the real PACKET_WELCOME/PACKET_MATCH_FOUND
       for its *next* match, causing intermittent "failed to connect" -- not
       a client bug, a server-lifecycle bug. A few final broadcasts (so
       clients definitely see the winner) and then a real exit fixes both
       this and the unbounded zombie-process buildup from a long-running
       persistent bot pool. */
    int shutdown_ticks = -1;
    /* Connection timeout, separate from the match-end shutdown above --
       found live, 2026-07-24: a lobby that never fills (a stale/duplicate
       PACKET_FIND_MATCH retry racing the matchmaker's reply can spawn a
       server that no real client ever connects to -- see arena_bot's
       wait_for_match doc comment) sits in ARENA_PHASE_WAITING/DRAFT
       forever, and the match-end shutdown timer above only ever engages
       once `arena_state.winner != 0` -- a phantom match never reaches that,
       so it never engages either. Confirmed live: 8 of 9 spawned match
       servers in one soak-test run were exactly this (one match_start line
       and nothing else, still running unbounded). This is the defensive
       complement: no real connection progress within a reasonable window
       means give up and exit, regardless of whether the root-cause race
       above is ever fully closed. */
    unsigned int waiting_ticks_ms = 0;
    while (running) {
        char buffer[1024];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        while (len > 0) {
            server_handle_packet(&sender, buffer, len);
            len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&sender, &slen);
        }
        if (match_phase == ARENA_PHASE_LIVE) {
            if (lobby_size == 2) arena_update(16);
            else arena_update_teams(16);
            snapshot_log_timer_ms += 16;
            if (snapshot_log_timer_ms >= 500) {
                snapshot_log_timer_ms = 0;
                match_log_snapshot();
                corpus_log_tick(get_server_time()); /* S170-194: same 500ms cadence as the match-replay snapshot above, one sensible shared throttle rather than a second independent timer */
            }
            live_match_report_timer_ms += 16;
            if (live_match_report_timer_ms >= LIVE_MATCH_REPORT_INTERVAL_MS) {
                live_match_report_timer_ms = 0;
                report_live_match_state();
            }
            if (arena_state.winner != 0 && !last_winner_logged) {
                match_log_win(arena_state.winner);
                report_match_result(arena_state.winner);
                last_winner_logged = 1;
                shutdown_ticks = 0;
            }
        } else {
            waiting_ticks_ms += 16;
            if (waiting_ticks_ms > 60000) { /* 60s with no real progress -- give up, not a leak */
                printf("No lobby progress in 60s (phase=%d, %d/%d connected) -- shutting down.\n",
                       match_phase, client_count, lobby_size);
                running = 0;
            }
        }
        server_broadcast();
        if (shutdown_ticks >= 0) {
            shutdown_ticks++;
            if (shutdown_ticks > 60) { /* ~1s of final broadcasts at 16ms/tick, then exit for real */
                printf("Match over, shutting down.\n");
                running = 0;
            }
        }
        #ifdef _WIN32
        Sleep(16);
        #else
        usleep(16000);
        #endif
    }
    return 0;
}
