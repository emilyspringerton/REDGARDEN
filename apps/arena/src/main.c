/* RED GARDEN — single-hero click-to-move arena demo.
 *
 * New, additive client: does not touch apps/lobby or the existing card-RTS.
 * Modern-GL (shader) rendering on purpose -- this sidesteps the GL/glu.h
 * dependency that blocks apps/lobby on this box (no libglu1-mesa-dev
 * installed): a shader pipeline only needs GL/gl.h + SDL_GL_GetProcAddress
 * function loading, no GLU, no GLEW/GLAD.
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
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
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../../packages/common/mat4.h"
#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"
#include "../../../packages/simulation/arena_game.h"
#include "../../../packages/simulation/arena_ai_bridge.h"
#include "../../../packages/simulation/arena_replay.h"
#include "../../../packages/goldenband/gband.h"
#include "../../../packages/goldenband/gband_rig.h"
#include "../../../packages/goldenband/gband_mesh_rig.h"

/* ---------------- networked PvP (2026-07-24 pivot, NORTHSTAR §13) ----------------
 * Local-only mode (no --connect flag) is unchanged: my_owner stays 0,
 * arena_update() runs fully client-side against the built-in bot. In
 * network mode, apps/arena_server is authoritative -- this client only
 * sends move/cast commands and applies incoming snapshots, never calls
 * arena_update() itself. */
static int net_mode = 0;
static int my_owner = 0; /* which arena_state.heroes[] slot is "me" -- 0 in local mode always */

/* Toggleable APM overlay (S170-71): off by default, F11 flips it. Ring buffer of action
 * timestamps (moves + Q/W/E casts) so the on-screen number is a real trailing-60s rate, not a
 * running-average-since-launch. */
#define APM_RING_CAP 512
static int show_apm = 0;
static int show_ability_help = 0; /* S170-151, founder: "H should show an overlay with character ability descriptions" */
static int shop_open = 0; /* S170-175, founder: "do a first pass shop interface" -- B toggles, same "works in any mode" precedent as F11/H */
static int force_box_rig = 0; /* S144-07: F9 A/B-toggles Tyler's real skinned mesh vs. the box-rig */
static int shop_page = 0; /* S170-231, founder: "too many items per page more pages navigate pages with shift 1 2 3" -- 0-indexed, Shift+1/2/3 jump straight to page 1/2/3 */
static int shop_was_in_range = 0; /* S170-231, founder: "pop the shop window up when you get close to the shop enough to buy" -- edge-triggered latch for the proximity auto-open/close below, so it only fires on the in-range/out-of-range transition and never fights a manual B press made while standing still */

/* Settings pane (3424324/343543, kanban cruise-queue cards: "REDGARDEN settings pane" /
 * "REDGARDEN settings volume slider"). Escape toggles it, same "works in any mode" precedent
 * F11/H/B already established -- unlike those, Escape wasn't bound to anything yet. master_volume
 * scales every play_tone() call (see that function's own doc comment) rather than each call site
 * scaling itself, so this is the one real place a player-controlled volume setting needs to live.
 * Session-only (not persisted to disk) -- this client has no existing settings-file/config
 * mechanism to hang a save onto; a real "remember it next launch" pass is separate, later work. */
static int show_settings_pane = 0;
static float master_volume = 1.0f; /* 0.0 (muted) .. 1.0 (full) */
static int settings_volume_dragging = 0; /* mouse-down-and-held on the slider track/handle */

/* Shop panel layout (S170-175): shared by the click hit-test in the event
 * loop and the draw call in the render pass, so a click always lands on
 * exactly the row it visually appears over -- both sites compute these from
 * win_w/win_h with the same formula rather than caching last frame's
 * positions (unlike g_hover_target's own per-frame cache, this layout only
 * depends on the window size, not any per-frame simulation state, so
 * there's no staleness to guard against). */
#define SHOP_ROW_H 20.0f
#define SHOP_COL_W 260.0f
/* S170-231: replaced the old 2-column x 15-row single giant page (S170-210's
 * fix -- all 27 items visible and clickable at once, founder: "too many
 * items per page") with a single buy column, one page of SHOP_ITEMS_PER_PAGE
 * at a time. 9 was chosen to exactly match the existing 1-9 quick-buy
 * keybind range, so every item on screen always has a live keybind instead
 * of only the first 9 of a much longer list. SHOP_PAGE_COUNT is a ceiling
 * division so it grows on its own the next time ARENA_ITEM_COUNT grows,
 * the self-scaling behavior SHOP_ITEMS_PER_COL was supposed to have but
 * didn't (S170-210 had to hand-bump it). */
#define SHOP_ITEMS_PER_PAGE 9
#define SHOP_PAGE_COUNT ((ARENA_ITEM_COUNT + SHOP_ITEMS_PER_PAGE - 1) / SHOP_ITEMS_PER_PAGE)
/* SHOP_BUILDS_PAGE (2026-08-25, build templates): one virtual page past the real item catalog
   pages -- shop_page reaching this value means "show build presets, not a catalog page," same
   page-button strip, its own row content (see the click handler and draw pass). */
#define SHOP_BUILDS_PAGE SHOP_PAGE_COUNT
#define SHOP_PAGE_BTN_W 30.0f
#define SHOP_PAGE_BTN_H 18.0f
#define SHOP_PAGE_BTN_GAP 6.0f
/* Panel height needs to fit whichever column has more rows: the buy column
 * now only ever shows SHOP_ITEMS_PER_PAGE items plus one row's worth of
 * height for the page buttons above it, but the equipped/sell column still
 * always shows every ARENA_ITEM_SLOT_COUNT loadout slot at once -- it isn't
 * the catalog, so pagination doesn't apply to it. */
#define SHOP_PANEL_ROWS ((SHOP_ITEMS_PER_PAGE + 1) > ARENA_ITEM_SLOT_COUNT ? (SHOP_ITEMS_PER_PAGE + 1) : ARENA_ITEM_SLOT_COUNT)
static void shop_panel_origin(int win_w, int win_h, float *sp_x, float *sp_y_top) {
    (void)win_w;
    *sp_x = 40.0f;
    *sp_y_top = (float)win_h - 70.0f;
}

/* Settings pane geometry (3424324/343543): same "click hit-test and render pass share one
 * formula" discipline shop_panel_origin's own doc comment establishes, so a click always lands
 * on exactly the slider it visually appears over. Centered on screen, same real reason the
 * ability-help panel above centers on win_w -- it's a modal-feeling overlay, not anchored HUD. */
#define SETTINGS_PANEL_W 360.0f
#define SETTINGS_PANEL_H 150.0f
#define SETTINGS_SLIDER_W 260.0f
#define SETTINGS_SLIDER_H 14.0f
static void settings_panel_origin(int win_w, int win_h, float *panel_x, float *panel_y) {
    *panel_x = (float)win_w / 2.0f - SETTINGS_PANEL_W / 2.0f;
    *panel_y = (float)win_h / 2.0f - SETTINGS_PANEL_H / 2.0f;
}
static void settings_slider_track(int win_w, int win_h, float *track_x, float *track_y) {
    float panel_x, panel_y;
    settings_panel_origin(win_w, win_h, &panel_x, &panel_y);
    *track_x = panel_x + (SETTINGS_PANEL_W - SETTINGS_SLIDER_W) / 2.0f;
    *track_y = panel_y + SETTINGS_PANEL_H / 2.0f - SETTINGS_SLIDER_H / 2.0f;
}
static const char *ARENA_ITEM_SLOT_NAMES[ARENA_ITEM_SLOT_COUNT] = {
    "WEAPON", "HEAD", "BODY", "HANDS", "LEGS", "FEET", "RING", "NECK", "BACK", "WAIST", "TRINKET"
};
static uint32_t apm_ring[APM_RING_CAP];
static int apm_ring_head = 0;
static int apm_ring_count = 0;

static void apm_record_action(uint32_t now_ms) {
    apm_ring[apm_ring_head] = now_ms;
    apm_ring_head = (apm_ring_head + 1) % APM_RING_CAP;
    if (apm_ring_count < APM_RING_CAP) apm_ring_count++;
}

static int apm_compute(uint32_t now_ms) {
    int count = 0;
    for (int i = 0; i < apm_ring_count; i++) {
        int idx = (apm_ring_head - 1 - i + APM_RING_CAP) % APM_RING_CAP;
        if (now_ms - apm_ring[idx] > 60000) break; /* ring is time-ordered -- stop at the first stale entry */
        count++;
    }
    return count;
}

/* This whole networking section (through the matching closing comment
 * below) used to be #ifndef _WIN32-only, with main() stubbing out
 * --connect/--queue entirely on Windows as a result. Now that the
 * platform-specific internals (winsock includes, ioctlsocket/fcntl,
 * closesocket/close, GetCurrentProcessId/getpid, mkdir) are each guarded
 * individually where they actually differ, this compiles and works on
 * both -- S170-54, found by actually watching the Windows cross-compile
 * fail rather than assuming the workflow alone would catch it. */
#define ARENA_TICKET_PAYLOAD_LEN 20
#define ARENA_TICKET_TOTAL_LEN (ARENA_TICKET_PAYLOAD_LEN + 16)

static int net_sock = -1;
static struct sockaddr_in net_server_addr;

/* g_supplied_ticket_hex (REDGARDEN_GUI_NORTHSTAR.md Milestone 3, 2026-07-31): a real,
 * already-minted ticket handed to this client externally -- GoblinFoxDragon/apps2/mud's
 * `battlegrounds` command mints one for a real DragonsNShit character via IDUNA and prints the
 * exact command line to run this binary with it, since a telnet session can't launch this
 * process itself. Takes priority over both existing ticket paths in net_connect (WOTAN
 * self-registration, self-minted dev fallback) -- neither of those carry a real DragonsNShit
 * identity, and self-registration would silently mint a throwaway one instead. */
static const char *g_supplied_ticket_hex = NULL;

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

/* Same register+mint round trip as apps/client/bot_main.c's
 * get_real_wotan_ticket -- ported here rather than shared via a header,
 * since this codebase duplicates per-binary orchestration logic (see
 * apps/server vs apps/arena_server) rather than linking .c files across
 * build targets. */
static int get_real_wotan_ticket(unsigned char out[ARENA_TICKET_TOTAL_LEN]) {
    char resp[4096];
    int status = 0;

    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: agent login failed (status=%d)\n", status);
        return 0;
    }
    char token[2048];
    if (!http_extract_json_string_field(resp, "access_token", token, sizeof(token))) {
        fprintf(stderr, "WOTAN: agent login response missing access_token\n");
        return 0;
    }

    char provider_sub[64];
#ifdef _WIN32
    snprintf(provider_sub, sizeof(provider_sub), "player-%lu-%u",
             (unsigned long)GetCurrentProcessId(), (unsigned int)time(NULL));
#else
    snprintf(provider_sub, sizeof(provider_sub), "player-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
#endif
    char register_body[256];
    snprintf(register_body, sizeof(register_body),
             "{\"provider\":\"redgarden_bot\",\"provider_sub\":\"%s\"}", provider_sub);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/players/register", token,
                        register_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: player registration failed (status=%d)\n", status);
        return 0;
    }
    char player_id[64];
    if (!http_extract_json_string_field(resp, "player_id", player_id, sizeof(player_id))) {
        fprintf(stderr, "WOTAN: registration response missing player_id\n");
        return 0;
    }

    char ticket_body[128];
    snprintf(ticket_body, sizeof(ticket_body), "{\"player_id\":\"%s\"}", player_id);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/ticket", token,
                        ticket_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: ticket mint failed (status=%d)\n", status);
        return 0;
    }
    char ticket_hex[128];
    if (!http_extract_json_string_field(resp, "ticket", ticket_hex, sizeof(ticket_hex))) {
        fprintf(stderr, "WOTAN: ticket response missing ticket field\n");
        return 0;
    }
    if (!hex_decode(ticket_hex, out, ARENA_TICKET_TOTAL_LEN)) {
        fprintf(stderr, "WOTAN: ticket field was not valid %d-byte hex\n", ARENA_TICKET_TOTAL_LEN);
        return 0;
    }
    printf("WOTAN: real identity registered -- player_id=%s\n", player_id);
    return 1;
}

/* Self-mint fallback, same scheme as bot_main.c's mint_ticket -- used only
 * if IDUNA isn't configured/reachable, so local network-mode testing
 * without a running IDUNA doesn't hard-fail. */
static void mint_ticket_fallback(const char *secret, unsigned char out[ARENA_TICKET_TOTAL_LEN]) {
    unsigned char payload[ARENA_TICKET_PAYLOAD_LEN];
    for (int i = 0; i < 16; i++) payload[i] = (unsigned char)(rand() & 0xFF);
    uint32_t expires_at = (uint32_t)time(NULL) + 300;
    payload[16] = (unsigned char)(expires_at & 0xFF);
    payload[17] = (unsigned char)((expires_at >> 8) & 0xFF);
    payload[18] = (unsigned char)((expires_at >> 16) & 0xFF);
    payload[19] = (unsigned char)((expires_at >> 24) & 0xFF);
    unsigned char mac[32];
    hmac_sha256((const unsigned char *)secret, strlen(secret), payload, ARENA_TICKET_PAYLOAD_LEN, mac);
    memcpy(out, payload, ARENA_TICKET_PAYLOAD_LEN);
    memcpy(out + ARENA_TICKET_PAYLOAD_LEN, mac, 16);
}

static int net_connect(const char *host, int port) {
    net_sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(net_sock, FIONBIO, &mode);
#else
    int flags = fcntl(net_sock, F_GETFL, 0);
    fcntl(net_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    net_server_addr.sin_family = AF_INET;
    net_server_addr.sin_port = htons((uint16_t)port);
    net_server_addr.sin_addr.s_addr = inet_addr(host);

    unsigned char ticket[ARENA_TICKET_TOTAL_LEN];
    int have_ticket = 0;
    if (g_supplied_ticket_hex) {
        have_ticket = hex_decode(g_supplied_ticket_hex, ticket, ARENA_TICKET_TOTAL_LEN);
        if (!have_ticket) {
            fprintf(stderr, "--ticket: not valid %d-byte hex\n", ARENA_TICKET_TOTAL_LEN);
        }
    }
    if (!have_ticket && iduna_agent_configured) {
        have_ticket = get_real_wotan_ticket(ticket);
    }
    if (!have_ticket) {
        const char *secret = getenv("REDGARDEN_TICKET_SECRET");
        if (!secret || !secret[0]) {
            fprintf(stderr, "No WOTAN identity and no REDGARDEN_TICKET_SECRET -- cannot connect.\n");
            return 0;
        }
        fprintf(stderr, "WOTAN: falling back to self-minted ticket (no real identity)\n");
        mint_ticket_fallback(secret, ticket);
    }

    char buf[sizeof(NetHeader) + ARENA_TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    memcpy(buf + sizeof(NetHeader), ticket, ARENA_TICKET_TOTAL_LEN);
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));

    /* Wait (briefly, blocking with retries) for PACKET_WELCOME so we know
       our own hero slot before the render loop starts. */
    for (int tries = 0; tries < 100; tries++) {
        char rbuf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)sizeof(NetHeader)) {
            NetHeader *rh = (NetHeader *)rbuf;
            if (rh->type == PACKET_WELCOME) {
                my_owner = rh->client_id;
                printf("Connected -- assigned hero slot %d\n", my_owner);
                return 1;
            }
        }
        SDL_Delay(50);
        if (tries % 10 == 0) {
            sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
        }
    }
    fprintf(stderr, "Timed out waiting for server welcome.\n");
    return 0;
}

/* net_find_and_connect -- queue into the matchmaker's pool (the same one
 * apps/arena_bot's persistent bots queue into) instead of connecting to an
 * already-known server:port. Reuses net_connect's ticket-minting/PACKET_CONNECT
 * handshake for the actual game connection once a match is assigned; only the
 * "how do I find a port" step differs from --connect. Lets a real human join
 * whatever match the bot pool is currently matchmaking into (NORTHSTAR §13,
 * "the human will join the bot games to validate, bot-first feedback loop"). */
static int net_find_and_connect(const char *mm_host, int mm_port) {
    net_sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(net_sock, FIONBIO, &mode);
#else
    int flags = fcntl(net_sock, F_GETFL, 0);
    fcntl(net_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in mm_addr = {0};
    mm_addr.sin_family = AF_INET;
    mm_addr.sin_port = htons((uint16_t)mm_port);
    mm_addr.sin_addr.s_addr = inet_addr(mm_host);

    NetHeader find = {0};
    find.type = PACKET_FIND_MATCH;
    sendto(net_sock, (const char *)&find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));

    printf("Queuing for a match at %s:%d ...\n", mm_host, mm_port);
    int game_port = -1;
    for (int retry_ticks = 0; retry_ticks < 1200; retry_ticks++) {
        char buf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)(sizeof(NetHeader) + sizeof(MatchFoundMsg))) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_MATCH_FOUND) {
                MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
                game_port = msg->port;
                break;
            }
        }
        SDL_Delay(100);
        /* Resend every ~5s, not every tick -- same same-box retry-race
           reasoning as apps/arena_bot's wait_for_match (found live, S170-43):
           resending too eagerly can race the matchmaker's own near-instant
           reply and re-enqueue a phantom entry. */
        if (retry_ticks % 50 == 0 && retry_ticks > 0) {
            sendto(net_sock, (const char *)&find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));
        }
    }
    if (game_port < 0) {
        fprintf(stderr, "Timed out waiting for a match (60s). Is the matchmaker/bot pool running?\n");
        return 0;
    }
    printf("Match found on port %d -- connecting...\n", game_port);
    /* net_connect opens its own fresh socket; close the queue socket first. */
#ifdef _WIN32
    closesocket(net_sock);
#else
    close(net_sock);
#endif
    net_sock = -1;
    return net_connect(mm_host, game_port);
}

/* net_send_move (unit_owner added 2026-07-30, Tyler clone-control rework -- founder: "clones
 * multi control drag click all of it"): which of the LOCAL PLAYER'S OWN commandable units (its
 * own hero, or one of its own active Tyler clones) this specific move command is for. Almost
 * always just my_owner (every hero except Tyler always has exactly one controllable unit, so
 * every call site not touched by the new selection system still passes my_owner and behaves
 * byte-for-byte as before). Server-side authorization (arena_owner_controls) is what actually
 * enforces a client can't move anything it doesn't control -- this is just which of the caller's
 * own units the command names. */
static void net_send_move(int unit_owner, float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_MOVE;
    ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    cmd->unit_owner = (uint8_t)unit_owner;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_attack (S170-162, NORTHSTAR SS17's click-to-attack system):
 * PACKET_ARENA_ATTACK's client-side sender -- locks commander_unit onto
 * target_owner. Sent instead of (never alongside) net_send_move whenever
 * the click landed on a live enemy hero, matching SS17.1's "right-click
 * ground vs right-click a unit" split, just on this game's own established
 * single-left-click convention rather than LoL's literal right-click.
 * commander_unit (2026-07-30, same clone-control rework as net_send_move's own unit_owner): which
 * of the local player's own units does the locking. */
static void net_send_attack(int commander_unit, int target_owner) {
    char buf[sizeof(NetHeader) + sizeof(ArenaAttackCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_ATTACK;
    ArenaAttackCmd *cmd = (ArenaAttackCmd *)(buf + sizeof(NetHeader));
    cmd->target_owner = (uint8_t)target_owner;
    cmd->commander_unit = (uint8_t)commander_unit;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_stop (NORTHSTAR.md §24 Milestone 2, 2026-07-31): PACKET_ARENA_STOP's client-side
 * sender -- same shape as net_send_move/net_send_attack, unit_owner is which of the local
 * player's own commandable units (self, or one of Tyler's own active clones) this stop is for. */
static void net_send_stop(int unit_owner) {
    char buf[sizeof(NetHeader) + sizeof(ArenaStopCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_STOP;
    ArenaStopCmd *cmd = (ArenaStopCmd *)(buf + sizeof(NetHeader));
    cmd->unit_owner = (uint8_t)unit_owner;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_attack_move (NORTHSTAR.md §17.4 + §24 Milestone 2, 2026-07-31): PACKET_ARENA_ATTACK_MOVE's
 * client-side sender -- same shape as net_send_move, unit_owner is which of the local player's
 * own commandable units this attack-move is for. */
static void net_send_attack_move(int unit_owner, float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaAttackMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_ATTACK_MOVE;
    ArenaAttackMoveCmd *cmd = (ArenaAttackMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    cmd->unit_owner = (uint8_t)unit_owner;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_hold (NORTHSTAR.md §24 Milestone 2, 2026-07-31): PACKET_ARENA_HOLD's client-side
 * sender -- same shape as net_send_stop. */
static void net_send_hold(int unit_owner) {
    char buf[sizeof(NetHeader) + sizeof(ArenaHoldCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_HOLD;
    ArenaHoldCmd *cmd = (ArenaHoldCmd *)(buf + sizeof(NetHeader));
    cmd->unit_owner = (uint8_t)unit_owner;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_patrol (NORTHSTAR.md §24 Milestone 2, 2026-07-31): PACKET_ARENA_PATROL's client-side
 * sender -- same shape as net_send_attack_move. */
static void net_send_patrol(int unit_owner, float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaPatrolCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_PATROL;
    ArenaPatrolCmd *cmd = (ArenaPatrolCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    cmd->unit_owner = (uint8_t)unit_owner;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* PACKET_ARENA_SHOP_BUY/SELL's client-side senders (S170-175). Same shape
 * as net_send_attack -- server infers "which hero" from the sending
 * client's own slot, all real validation (proximity, Flow balance) happens
 * server-side in arena_shop_buy/arena_shop_sell. */
static void net_send_shop_buy(int item_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaShopBuyCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_SHOP_BUY;
    ArenaShopBuyCmd *cmd = (ArenaShopBuyCmd *)(buf + sizeof(NetHeader));
    cmd->item_id = (uint8_t)item_id;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static void net_send_shop_sell(int slot) {
    char buf[sizeof(NetHeader) + sizeof(ArenaShopSellCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_SHOP_SELL;
    ArenaShopSellCmd *cmd = (ArenaShopSellCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* PACKET_ARENA_APPLY_BUILD_TEMPLATE's client-side sender (2026-08-25, build templates). Same
   shape as net_send_shop_buy -- server infers "which hero" from the sending client's own slot,
   all real validation happens server-side in arena_hero_apply_build_template. */
static void net_send_apply_build_template(int template_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaApplyBuildTemplateCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_APPLY_BUILD_TEMPLATE;
    ArenaApplyBuildTemplateCmd *cmd = (ArenaApplyBuildTemplateCmd *)(buf + sizeof(NetHeader));
    cmd->template_id = (uint8_t)template_id;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* net_send_active_item (S170-205/S170-206): no payload -- arena_use_active_item derives
 * everything (which item is actually equipped, direction) server-side from the sending client's
 * own owner slot alone. Named generically, not net_send_blink, since PACKET_ARENA_BLINK now
 * covers Donkey's Paper Glide too -- one tilde key, whichever active item the hero actually has
 * equipped. */
static void net_send_active_item(void) {
    char buf[sizeof(NetHeader)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_BLINK;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

/* g_hover_target (S170-143, "hover casting like in wow macros"): which
 * hero slot the mouse is currently over, updated once per frame by the
 * health-bar hover pass below (S170-69's own hit-test, reused rather than
 * duplicated) and read by the QWE keybind handler when a cast fires. Up to
 * one frame stale relative to the mouse's exact current position (the
 * keybind handler runs earlier in the same frame's event loop than the
 * hover pass that updates this) -- imperceptible at any real frame rate,
 * same latency class as any other "read last frame's computed HUD state"
 * value in this file. */
static int g_hover_target = -1;

/* g_ground_target_pending_slot (S202-34, Abraham's Fireball): 0 = not
 * aiming anything right now. 1/2/3 (Q/W/R) = the local player pressed a
 * ground-targeted ability's key and the client is waiting for the
 * confirming click, same two-phase "press ability, then click a point"
 * flow the founder described ("the targeter is green when you are ready
 * to cast"). Set on keydown for a ground-targeted slot (only Abraham's W
 * today) instead of casting immediately; the next ordinary left-click is
 * intercepted (spawns the real cast with that click's world point instead
 * of issuing a move/attack command) and clears this back to 0. Right-click
 * cancels without casting. Local-player-only, same scope every other
 * client-only input-mode flag in this file already has. */
static int g_ground_target_pending_slot = 0;

/* g_last_vp (2026-07-30, Tyler clone-control rework): the view-projection matrix from the most
 * recently rendered frame, needed so the drag-select box-test (event-loop code, which runs
 * BEFORE this frame's own `vp` is computed in the render pass further down) can call
 * world_to_screen at all -- same "up to one frame stale, imperceptible at any real frame rate"
 * idiom g_hover_target's own doc comment just above already establishes for exactly this reason.
 * Updated once per frame right after `vp` itself is computed in the render pass. */
static Mat4 g_last_vp;

/* Multi-unit selection + drag-select (2026-07-30, Tyler "Divided We Stand" rework -- founder:
 * "clones multi control drag click all of it"): real RTS multi-unit control, now that clones are
 * independently commandable (the old auto-follow-Tyler mirroring is gone, see
 * arena_update_teams' own doc comment in arena_game.c). selected_unit_count == 0 is the default
 * and ONLY state every hero other than Tyler (and Tyler himself before ever dragging) will ever
 * be in -- it means "nothing explicitly selected," which selected_or_self() below resolves to
 * {my_owner}, so every existing single-click-to-move/attack behavior is completely unchanged
 * unless a player actually drags a selection box over their own clones. */
#define ARENA_MAX_SELECTED_UNITS (1 + ARENA_TYLER_R_CLONE_COUNT)
static int selected_units[ARENA_MAX_SELECTED_UNITS];
static int selected_unit_count = 0;
static int left_drag_active = 0;         /* true from a qualifying LEFT mousedown until the matching mouseup */
static int left_drag_start_x = 0, left_drag_start_y = 0; /* raw SDL screen coords (top-down) at mousedown */
#define ARENA_DRAG_SELECT_THRESHOLD_PX 6.0f /* below this on release, it's an ordinary click; at or above, a box-select */

/* selected_or_self: the actual list of units the next click/drag-release command should act on.
 * Fills `out` (must hold at least ARENA_MAX_SELECTED_UNITS ints) and returns how many. */
static int selected_or_self(int *out) {
    if (selected_unit_count == 0) {
        out[0] = my_owner;
        return 1;
    }
    for (int i = 0; i < selected_unit_count; i++) out[i] = selected_units[i];
    return selected_unit_count;
}

/* net_send_cast (S202-34: gained has_ground_target/target_x/target_z): pass
 * has_ground_target=0, target_x=target_z=0.0f for every ordinary unit-
 * targeted/self-targeted cast (Q/E and every hero's W except Abraham's) --
 * see ArenaCastCmd's own doc comment in protocol.h. */
static void net_send_cast(int slot, int hover_target, int has_ground_target, float target_x, float target_z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaCastCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_CAST;
    ArenaCastCmd *cmd = (ArenaCastCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    cmd->hover_target = (int8_t)hover_target;
    cmd->has_ground_target = (uint8_t)has_ground_target;
    cmd->target_x = target_x;
    cmd->target_z = target_z;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static void net_send_pick(int hero_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaPickCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_PICK;
    ArenaPickCmd *cmd = (ArenaPickCmd *)(buf + sizeof(NetHeader));
    cmd->hero_id = (uint8_t)hero_id;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static int net_lobby_size = 2; /* set from the server's own msg->count once a snapshot arrives */
static uint8_t net_phase = ARENA_PHASE_WAITING;
static int net_picked = 0; /* have we sent our PACKET_ARENA_PICK for the current draft yet */
static uint32_t net_last_pick_send_ms = 0; /* for retry -- see net_poll_snapshots' resend logic */
/* net_picked_hero_id (S170-182): which hero_id the player actually clicked on the draft
 * screen, so the resend-on-no-progress safety net below can resend the SAME real pick rather
 * than recomputing one -- replaces the old auto-draft's net_draft_offset (S170-105/166), which
 * only ever existed to derive a hero_id with no human input at all; a real click already gives
 * one directly. Reset to -1 whenever net_picked resets to 0 (a fresh draft is about to start). */
static int net_picked_hero_id = -1;

/* Defined further down alongside the other particle-effect state
   (spawn_ring/AttackFlash) -- forward-declared here so net_poll_snapshots
   can consume the wire's cast_flash_slot the instant a snapshot arrives. */
static void spawn_spell_flash(float x, float z, int slot, int hero_id);
static void play_tone(float freq_hz, float duration_ms, float volume);
static void play_cast_tone(int slot);
static void trigger_squish(int owner);
/* Tree passive (2026-08-25): same forward-declare-early reasoning as trigger_squish just above --
   net_poll_snapshots needs to fire this the instant a snapshot shows a tree obstacle's hp
   decreased, well before this file's own real definition (alongside trigger_squish/compute_squish
   further down). See that definition's own doc comment for the full "why obstacle-indexed, not
   hero-indexed" story. */
static void trigger_tree_squish(int obstacle_index);
/* Last-known obstacle hp, purely to detect a decrease (a hit) vs. an increase (regen) between
   consecutive snapshots -- ARENA_OBSTACLE_COUNT-sized, not ARENA_SNAPSHOT_OBSTACLE_COUNT, so it
   stays correct even if the two constants ever drift (the apply loop already clamps to the
   smaller of the two). _valid starts at 0/false (static zero-init) so the very first snapshot
   never misreads its own initial value as a "decrease" from an uninitialized 0. */
static uint16_t obstacle_hp_prev[ARENA_OBSTACLE_COUNT];
static int obstacle_hp_prev_valid[ARENA_OBSTACLE_COUNT];
#define ARENA_AUDIO_HEARING_RADIUS 15.0f /* how far from the local player's own hero a cast/hit sound is still audible */

static void net_poll_snapshots(uint32_t now_ms) {
    /* CRITICAL BUG FOUND LIVE (S170-192): this was a fixed char rbuf[2048] -- see
       apps/arena_bot/src/main.c's own identical fix for the full story. Same truncation, same
       root cause, same fix: sized dynamically to the real current packet size. This one hit
       the actual human client, not just bots -- a real networked match's snapshots have been
       silently truncated and rejected this whole time, which plausibly explains at least part
       of the "frozen match" symptom reported earlier this session (a genuinely different,
       already-fixed live-pool binary mismatch was the other confirmed cause; this may have
       been compounding it, or affecting matches that got past that first issue). */
    /* S170-193: sized for whichever of the two snapshot packet types is larger (see
       ARENA_SNAPSHOT_RECV_BUF_SIZE's own doc comment in protocol.h) -- the world message and
       each hero chunk arrive as independent packets now, not one combined message. */
    char rbuf[ARENA_SNAPSHOT_RECV_BUF_SIZE];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    while (len > 0) {
        if (len >= (int)sizeof(NetHeader)) {
            NetHeader *h = (NetHeader *)rbuf;
            if (h->type == PACKET_ARENA_SNAPSHOT_HEROES && len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotHeroesMsg))) {
                /* S170-193: one ARENA_SNAPSHOT_HERO_CHUNK_SIZE-hero slice -- self-contained
                   (total_count travels with the chunk itself), so this branch never depends on
                   whether the world packet or the other chunk has arrived yet this tick. */
                ArenaSnapshotHeroesMsg *chunk = (ArenaSnapshotHeroesMsg *)(rbuf + sizeof(NetHeader));
                int base = chunk->chunk_index * ARENA_SNAPSHOT_HERO_CHUNK_SIZE;
                for (int j = 0; j < ARENA_SNAPSHOT_HERO_CHUNK_SIZE; j++) {
                    int i = base + j;
                    /* 2026-07-30, Tyler clone-control rework: outer bound widened from
                       ARENA_SNAPSHOT_MAX_HEROES (20, real heroes only) to
                       ARENA_SNAPSHOT_HEROES_ARRAY_SIZE (28, + Tyler's clone pool) -- see
                       ArenaHeroSnapshot's own is_clone/clone_owner doc comment in protocol.h for
                       why clones need syncing at all. A real-hero slot the current lobby doesn't
                       use (i >= chunk->total_count) is `continue`d, not `break`ed, so the scan
                       keeps going far enough to still reach the clone range later in this same
                       chunk -- `break` here would have silently dropped every clone again. */
                    if (i >= ARENA_SNAPSHOT_HEROES_ARRAY_SIZE) break;
                    int is_clone_slot = (i >= ARENA_SNAPSHOT_MAX_HEROES);
                    if (!is_clone_slot && i >= chunk->total_count) continue;
                    if (is_clone_slot && !chunk->heroes[j].is_clone) {
                        /* An empty clone slot -- explicitly clear active so a clone that just
                           died (shared fate or otherwise) disappears client-side the instant its
                           slot frees, same "no lingering corpse" behavior the sim itself has. */
                        arena_state.heroes[i].active = 0;
                        continue;
                    }
                    ArenaHero *dst = &arena_state.heroes[i];
                    dst->x = chunk->heroes[j].x;
                    dst->z = chunk->heroes[j].z;
                    dst->hp = chunk->heroes[j].hp;
                    dst->max_hp = chunk->heroes[j].max_hp;
                    dst->alive = chunk->heroes[j].alive;
                    dst->active = 1;
                    dst->hero_id = (ArenaHeroID)chunk->heroes[j].hero_id;
                    if (is_clone_slot) {
                        /* A clone's slot index falls outside the normal "first half team 0,
                           second half team 1" range the ordinary derivation below assumes -- it
                           inherits its owner's own already-known team instead. Safe regardless of
                           chunk arrival order: a hero's team never changes mid-match, so whatever
                           value is already sitting in arena_state.heroes[clone_owner].team (from
                           this tick or an earlier one) is always correct. */
                        dst->is_clone = 1;
                        dst->clone_owner = chunk->heroes[j].clone_owner;
                        dst->team = (dst->clone_owner >= 0 && dst->clone_owner < ARENA_MAX_HEROES)
                            ? arena_state.heroes[dst->clone_owner].team : 0;
                    } else {
                        dst->is_clone = 0;
                        dst->clone_owner = -1;
                        dst->team = (i < chunk->total_count / 2) ? 0 : 1;
                    }
                    /* S170-137: ability-tile readiness needs real cooldown/mana state, not the
                       zeroed default net_mode left them at forever (see the field's own doc
                       comment in protocol.h). */
                    dst->q_cooldown_ms = chunk->heroes[j].q_cooldown_ms;
                    dst->w_cooldown_ms = chunk->heroes[j].w_cooldown_ms;
                    dst->r_cooldown_ms = chunk->heroes[j].r_cooldown_ms;
                    dst->mp = chunk->heroes[j].mp;
                    dst->attack_target = chunk->heroes[j].attack_target; /* S170-162: synced for every hero so the lock reads clearly to any hero watching the fight */
                    /* S170-175: Flow/XP economy + equipped items, for the character pane and stats page below. */
                    dst->flow = chunk->heroes[j].flow;
                    dst->flow_earned = chunk->heroes[j].flow_earned;
                    dst->xp = chunk->heroes[j].xp;
                    dst->kills = chunk->heroes[j].kills;
                    dst->deaths = chunk->heroes[j].deaths;
                    for (int s = 0; s < ARENA_SNAPSHOT_ITEM_SLOT_COUNT && s < ARENA_ITEM_SLOT_COUNT; s++) {
                        dst->equipped_item[s] = chunk->heroes[j].equipped_item[s];
                    }
                    dst->w_active = chunk->heroes[j].w_active; /* S170-180 bugfix: was never synced, so the W tile's "active" highlight was always wrong in net_mode */
                    /* S170-184 bugfix: status effects were never synced either -- the status
                       label above the health bar (hero_status_label) has been silently
                       non-functional in every net_mode match, same class of bug as w_active
                       just above. */
                    dst->silenced_ms = chunk->heroes[j].silenced_ms;
                    dst->rooted_ms = chunk->heroes[j].rooted_ms;
                    dst->intangible_ms = chunk->heroes[j].intangible_ms;
                    dst->burning_ms = chunk->heroes[j].burning_ms;
                    dst->survive_floor_ms = chunk->heroes[j].survive_floor_ms;
                    dst->stunned_ms = chunk->heroes[j].stunned_ms;
                    dst->slowed_ms = chunk->heroes[j].slowed_ms;
                    dst->slow_pct = (float)chunk->heroes[j].slow_pct_x100 / 100.0f;
                    dst->berserker_ms = chunk->heroes[j].berserker_ms; /* S170-190 */
                    dst->regen_ms = chunk->heroes[j].regen_ms;
                    dst->r_zone_x = chunk->heroes[j].r_zone_x; /* S170-200 */
                    dst->r_zone_z = chunk->heroes[j].r_zone_z;
                    dst->r_active_ms = chunk->heroes[j].r_active_ms;
                    dst->zone_radius = (float)chunk->heroes[j].zone_radius_x10 / 10.0f; /* S202-42 -- Cart only, 0 for every other hero */
                    dst->casting_slot = chunk->heroes[j].casting_slot; /* S170-203 */
                    dst->cast_time_remaining_ms = chunk->heroes[j].cast_time_remaining_ms;
                    dst->cast_total_ms = chunk->heroes[j].cast_total_ms;
                    dst->blink_cooldown_ms = chunk->heroes[j].blink_cooldown_ms; /* S170-205 */
                    dst->donkey_glide_cooldown_ms = chunk->heroes[j].donkey_glide_cooldown_ms; /* S170-206 */
                    /* King buff status, 2026-08-20 -- see ArenaHeroSnapshot's own doc comment
                       for the king_buff_flags bit layout. Real ms/stack values aren't needed
                       client-side (the buff HUD is on/off + stack count, not a countdown), so
                       these just carry a nonzero sentinel except king_growth_stacks, which is
                       the real number. */
                    uint8_t kbf = chunk->heroes[j].king_buff_flags;
                    dst->king_music_carrier = (kbf & 0x01) ? 1 : 0;
                    dst->king_growth_ms = (kbf & 0x02) ? 1 : 0;
                    dst->king_wealth_ms = (kbf & 0x04) ? 1 : 0;
                    dst->king_allseeing_display = (kbf & 0x08) ? 1 : 0;
                    dst->king_growth_stacks = chunk->heroes[j].king_growth_stacks;
                    dst->duck_smoke_x = chunk->heroes[j].duck_smoke_x; /* S202-10 */
                    dst->duck_smoke_z = chunk->heroes[j].duck_smoke_z;
                    dst->duck_smoke_ms = chunk->heroes[j].duck_smoke_ms;
                    if (chunk->heroes[j].cast_flash_slot > 0) {
                        spawn_spell_flash(dst->x, dst->z, chunk->heroes[j].cast_flash_slot, dst->hero_id);
                        trigger_squish(i);
                        /* Hearing range (S170-92): a real 20-hero match can have several
                           casts landing every second across the whole map -- unfiltered,
                           that's noise, not legibility. Only sound cues for casts within a
                           reasonable radius of the local player's own hero, same "you can
                           hear nearby fights, not the whole battlefield" scoping real games
                           use for audio falloff. */
                        float adx = dst->x - arena_state.heroes[my_owner].x;
                        float adz = dst->z - arena_state.heroes[my_owner].z;
                        if (adx * adx + adz * adz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                            play_cast_tone(chunk->heroes[j].cast_flash_slot);
                        }
                    }
                }
            } else if (h->type == PACKET_ARENA_SNAPSHOT && len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
                ArenaSnapshotMsg *msg = (ArenaSnapshotMsg *)(rbuf + sizeof(NetHeader));
                net_lobby_size = msg->count;
                net_phase = msg->phase;
                if (net_phase != ARENA_PHASE_DRAFT) {
                    net_picked = 0; /* reset so the next draft (after a requeue) picks again */
                    net_picked_hero_id = -1;
                }
                /* S170-182: draft used to auto-pick the instant ARENA_PHASE_DRAFT started (no
                   pick UI existed yet, S170-66/68's own "fine for now" call) -- now a real
                   click-to-pick screen (draw_draft_screen, rendered from the main loop whenever
                   net_phase == ARENA_PHASE_DRAFT && !net_picked) drives net_send_pick instead.
                   No auto-fallback-on-timeout yet if the player never clicks; a real human
                   present to see the screen is the whole point of building it, and this repo's
                   own convention is to scope an ask to what was actually asked rather than
                   inventing a timeout nobody requested -- flagged as a real, deliberate gap for
                   a future pass if AFK-in-draft turns out to matter in practice. */
                arena_state.winner = msg->winner;
                for (int i = 0; i < ARENA_SNAPSHOT_NODE_COUNT && i < ARENA_NODE_COUNT; i++) {
                    ArenaNode *dst = &arena_state.nodes[i];
                    dst->x = msg->nodes[i].x;
                    dst->z = msg->nodes[i].z;
                    dst->owner = msg->nodes[i].owner;
                    dst->capturing_team = msg->nodes[i].capturing_team;
                    dst->capture_progress_ms = msg->nodes[i].capture_progress_ms;
                }
                /* S170-136: projectiles are sparse (only some slots active),
                   so mirror the wire message's own "active count" directly
                   rather than reusing arena_state.projectiles[]' own active
                   flags -- the render loop below just walks 0..count. */
                {
                    int pcount = msg->projectile_count;
                    if (pcount > ARENA_SNAPSHOT_MAX_PROJECTILES) pcount = ARENA_SNAPSHOT_MAX_PROJECTILES;
                    if (pcount > ARENA_MAX_PROJECTILES) pcount = ARENA_MAX_PROJECTILES;
                    for (int i = 0; i < pcount; i++) {
                        ArenaProjectile *dst = &arena_state.projectiles[i];
                        dst->active = 1;
                        dst->x = msg->projectiles[i].x;
                        dst->z = msg->projectiles[i].z;
                        dst->owner = msg->projectiles[i].owner;
                        dst->hero_id = (ArenaHeroID)msg->projectiles[i].hero_id;
                    }
                    for (int i = pcount; i < ARENA_MAX_PROJECTILES; i++) {
                        arena_state.projectiles[i].active = 0;
                    }
                }
                /* S170-146: node-guardian creeps -- always fully populated, same
                   convention as heroes/nodes above (not sparse-packed like
                   projectiles/lane creeps below). */
                {
                    int ccount = ARENA_SNAPSHOT_CREEP_COUNT;
                    if (ccount > ARENA_MAX_CREEPS) ccount = ARENA_MAX_CREEPS;
                    for (int i = 0; i < ccount; i++) {
                        ArenaCreep *dst = &arena_state.creeps[i];
                        dst->x = msg->creeps[i].x;
                        dst->z = msg->creeps[i].z;
                        dst->hp = msg->creeps[i].hp;
                        dst->max_hp = msg->creeps[i].max_hp;
                        dst->alive = msg->creeps[i].alive;
                        dst->flavor = (ArenaCreepFlavor)msg->creeps[i].flavor;
                    }
                }
                /* 2026-07-30: node towers -- always fully populated, same convention as
                   node-guardian creeps just above. */
                {
                    int twcount = ARENA_SNAPSHOT_TOWER_COUNT;
                    if (twcount > ARENA_NODE_COUNT) twcount = ARENA_NODE_COUNT;
                    for (int i = 0; i < twcount; i++) {
                        ArenaTower *dst = &arena_state.towers[i];
                        dst->x = msg->towers[i].x;
                        dst->z = msg->towers[i].z;
                        dst->hp = msg->towers[i].hp;
                        dst->max_hp = msg->towers[i].max_hp;
                        dst->alive = msg->towers[i].alive;
                    }
                }
                /* S170-190: powerups -- always fully populated, same convention as
                   node-guardian creeps just above. */
                {
                    int pcount2 = ARENA_SNAPSHOT_POWERUP_COUNT;
                    if (pcount2 > ARENA_POWERUP_COUNT) pcount2 = ARENA_POWERUP_COUNT;
                    for (int i = 0; i < pcount2; i++) {
                        ArenaPowerup *dst = &arena_state.powerups[i];
                        dst->x = msg->powerups[i].x;
                        dst->z = msg->powerups[i].z;
                        dst->kind = (ArenaPowerupKind)msg->powerups[i].kind;
                        dst->active = msg->powerups[i].active;
                    }
                }
                /* S170-146: lane creeps -- sparse pool, same "mirror the wire
                   message's own active count" convention as projectiles above. */
                {
                    int lcount = msg->lane_creep_count;
                    if (lcount > ARENA_SNAPSHOT_MAX_LANE_CREEPS) lcount = ARENA_SNAPSHOT_MAX_LANE_CREEPS;
                    if (lcount > ARENA_MAX_LANE_CREEPS) lcount = ARENA_MAX_LANE_CREEPS;
                    for (int i = 0; i < lcount; i++) {
                        ArenaLaneCreep *dst = &arena_state.lane_creeps[i];
                        dst->active = 1;
                        dst->alive = 1;
                        dst->x = msg->lane_creeps[i].x;
                        dst->z = msg->lane_creeps[i].z;
                        dst->hp = msg->lane_creeps[i].hp;
                        dst->max_hp = msg->lane_creeps[i].max_hp;
                        dst->team = msg->lane_creeps[i].team;
                        dst->role = (ArenaLaneCreepRole)msg->lane_creeps[i].role; /* S170-218 */
                    }
                    for (int i = lcount; i < ARENA_MAX_LANE_CREEPS; i++) {
                        arena_state.lane_creeps[i].active = 0;
                        arena_state.lane_creeps[i].alive = 0;
                    }
                }
                /* Jungle camps client-visibility fix, 2026-08-20: camp minions/Kings were
                   simulated server-side since Milestones 1/2 but never had a wire
                   representation until now -- same parse pattern as lane creeps above. */
                {
                    int ccount = msg->camp_minion_count;
                    if (ccount > ARENA_SNAPSHOT_MAX_CAMP_MINIONS) ccount = ARENA_SNAPSHOT_MAX_CAMP_MINIONS;
                    if (ccount > ARENA_MAX_CAMP_MINIONS) ccount = ARENA_MAX_CAMP_MINIONS;
                    for (int i = 0; i < ccount; i++) {
                        ArenaCampMinion *dst = &arena_state.camp_minions[i];
                        dst->active = 1;
                        dst->alive = 1;
                        dst->x = msg->camp_minions[i].x;
                        dst->z = msg->camp_minions[i].z;
                        dst->hp = msg->camp_minions[i].hp;
                        dst->max_hp = msg->camp_minions[i].max_hp;
                        dst->camp_index = msg->camp_minions[i].camp_index;
                    }
                    for (int i = ccount; i < ARENA_MAX_CAMP_MINIONS; i++) {
                        arena_state.camp_minions[i].active = 0;
                        arena_state.camp_minions[i].alive = 0;
                    }
                }
                /* Kings are always fully populated (one per camp), same convention as
                   node towers/creeps -- not sparse-packed. */
                for (int i = 0; i < ARENA_SNAPSHOT_CAMP_COUNT && i < ARENA_CAMP_COUNT; i++) {
                    ArenaKing *dst = &arena_state.kings[i];
                    dst->x = msg->kings[i].x;
                    dst->z = msg->kings[i].z;
                    dst->hp = msg->kings[i].hp;
                    dst->max_hp = msg->kings[i].max_hp;
                    dst->alive = msg->kings[i].alive;
                    dst->active = msg->kings[i].alive; /* client only needs "is it here to draw" */
                }
                arena_state.resources[0] = msg->resources[0]; /* S170-153 */
                arena_state.resources[1] = msg->resources[1];
                /* Tree passive (2026-08-25): obstacles are always fully populated, same
                   convention as kings just above -- only tree obstacles carry a real value.
                   Any decrease vs. the last snapshot's own value fires the local hit-reaction
                   squish (compute_tree_squish's own doc comment); a regen-driven increase does
                   not -- same "only a hit looks like a hit" reasoning a heal-flash-vs-damage-flash
                   distinction elsewhere in this file already draws. */
                for (int i = 0; i < ARENA_SNAPSHOT_OBSTACLE_COUNT && i < ARENA_OBSTACLE_COUNT; i++) {
                    uint16_t new_hp = msg->obstacle_hp[i];
                    if (obstacle_hp_prev_valid[i] && new_hp < obstacle_hp_prev[i]) {
                        trigger_tree_squish(i);
                    }
                    arena_state.obstacles[i].hp = new_hp;
                    obstacle_hp_prev[i] = new_hp;
                    obstacle_hp_prev_valid[i] = 1;
                }
            }
        }
        len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    }
    /* Pick retry (S170-99, real bug found live: a genuinely full 20/20 lobby stalled in
     * ARENA_PHASE_DRAFT and died on the server's own 60s no-progress timeout). Root cause:
     * net_send_pick(), unlike net_connect()/net_find_and_connect(), was a single fire-and-
     * forget UDP send with no retry at all -- rock-solid over localhost loopback (bots, which
     * is all this path was ever tested against), but a real external connection can drop that
     * one unacknowledged packet, and net_picked latching to 1 on send (not on confirmation)
     * meant it would never be resent. Resend every 1s while still in draft and not yet live --
     * harmless if the original arrived (server's own PACKET_ARENA_PICK handling just re-records
     * the same hero_id), the actual fix if it didn't. */
    if (net_phase == ARENA_PHASE_DRAFT && net_picked && net_picked_hero_id >= 0 && now_ms - net_last_pick_send_ms > 1000) {
        net_send_pick(net_picked_hero_id); /* resend the SAME real pick, not a recomputed one */
        net_last_pick_send_ms = now_ms;
    }
}
/* end of the S170-54 cross-platform networking section */

/* Match event log — MOBA half of NORTHSTAR §12 Phase B (EMILY/BACKLOG.md
 * S170-29), extending apps/server's S170-28 pattern to this demo. Same
 * "minimum hook, not a replay system" philosophy: one JSON line per event
 * to var/matches/arena-<timestamp>.jsonl. Unlike apps/server, this client
 * has no networking or connect-ticket auth at all, so there's no real WOTAN
 * player_id to attach -- "local_player"/"local_bot" are honest placeholders,
 * not a guess at an identity that doesn't exist yet. Real identity
 * attribution for arena replays is blocked on arena getting connect-ticket
 * auth in the first place, which is out of scope here. */
static FILE *arena_log_fp = NULL;
static uint32_t arena_log_elapsed_ms = 0;
static uint32_t arena_log_since_snapshot_ms = 0;
#define ARENA_LOG_SNAPSHOT_INTERVAL_MS 500

static void arena_log_open(void) {
#ifdef _WIN32
    mkdir("var");
    mkdir("var/matches");
#else
    mkdir("var", 0755);
    mkdir("var/matches", 0755);
#endif
    char path[256];
    snprintf(path, sizeof(path), "var/matches/arena-%ld.jsonl", (long)time(NULL));
    if (arena_log_fp) fclose(arena_log_fp);
    arena_log_fp = fopen(path, "a");
    if (!arena_log_fp) {
        fprintf(stderr, "WARNING: could not open arena match log %s -- match will not be logged (S170-29)\n", path);
        return;
    }
    arena_log_elapsed_ms = 0;
    arena_log_since_snapshot_ms = 0;
    fprintf(arena_log_fp, "{\"event\":\"match_start\",\"ts_ms\":0}\n");
    fflush(arena_log_fp);
    printf("Arena match event log: %s\n", path);
}

static void arena_log_snapshot(void) {
    if (!arena_log_fp) return;
    ArenaHero *p = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    fprintf(arena_log_fp,
            "{\"event\":\"snapshot\",\"ts_ms\":%u,"
            "\"player\":{\"id\":\"local_player\",\"x\":%.2f,\"z\":%.2f,\"hp\":%d},"
            "\"bot\":{\"id\":\"local_bot\",\"x\":%.2f,\"z\":%.2f,\"hp\":%d}}\n",
            arena_log_elapsed_ms, p->x, p->z, p->hp, b->x, b->z, b->hp);
    fflush(arena_log_fp);
}

static void arena_log_ability(const char *ability) {
    if (!arena_log_fp) return;
    fprintf(arena_log_fp, "{\"event\":\"ability_cast\",\"player_id\":\"local_player\",\"ability\":\"%s\",\"ts_ms\":%u}\n",
            ability, arena_log_elapsed_ms);
    fflush(arena_log_fp);
}

static void arena_log_win(int winner) {
    if (!arena_log_fp) return;
    const char *winner_id = (winner == 1) ? "local_player" : "local_bot";
    fprintf(arena_log_fp, "{\"event\":\"match_end\",\"winner\":\"%s\",\"ts_ms\":%u}\n", winner_id, arena_log_elapsed_ms);
    fflush(arena_log_fp);
}

/* ---------------- manually-loaded GL 3.x function pointers ---------------- */
static PFNGLCREATESHADERPROC glCreateShader_;
static PFNGLSHADERSOURCEPROC glShaderSource_;
static PFNGLCOMPILESHADERPROC glCompileShader_;
static PFNGLGETSHADERIVPROC glGetShaderiv_;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_;
static PFNGLCREATEPROGRAMPROC glCreateProgram_;
static PFNGLATTACHSHADERPROC glAttachShader_;
static PFNGLLINKPROGRAMPROC glLinkProgram_;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_;
static PFNGLUSEPROGRAMPROC glUseProgram_;
static PFNGLDELETESHADERPROC glDeleteShader_;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_;
static PFNGLGENBUFFERSPROC glGenBuffers_;
static PFNGLBINDBUFFERPROC glBindBuffer_;
static PFNGLBUFFERDATAPROC glBufferData_;
static PFNGLBUFFERSUBDATAPROC glBufferSubData_; /* S144-07: dynamic skinned-mesh vertex updates */
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
static PFNGLUNIFORM4FPROC glUniform4f_;
static PFNGLUNIFORM3FPROC glUniform3f_;
static PFNGLUNIFORM1FPROC glUniform1f_; /* S180-09: outline pass width uniform */
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_;

#define LOAD(name, type) name##_ = (type)SDL_GL_GetProcAddress(#name)

static int load_gl_functions(void) {
    LOAD(glCreateShader, PFNGLCREATESHADERPROC);
    LOAD(glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD(glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD(glGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD(glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD(glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    LOAD(glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD(glDeleteShader, PFNGLDELETESHADERPROC);
    LOAD(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD(glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD(glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD(glBufferData, PFNGLBUFFERDATAPROC);
    LOAD(glBufferSubData, PFNGLBUFFERSUBDATAPROC); /* S144-07: dynamic skinned-mesh vertex updates */
    LOAD(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD(glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD(glUniform3f, PFNGLUNIFORM3FPROC);
    LOAD(glUniform1f, PFNGLUNIFORM1FPROC);
    LOAD(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);
    return glCreateShader_ && glShaderSource_ && glCompileShader_ && glLinkProgram_ &&
           glUseProgram_ && glGenVertexArrays_ && glBindVertexArray_ && glGenBuffers_ &&
           glBufferData_ && glVertexAttribPointer_ && glUniformMatrix4fv_;
}

/* ---------------- shader source ---------------- */
static const char *VS_SRC =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

/* Cel-shading (S180-09, founder real-time: "iterate dragonsnshit interface
 * and graphics ... the abraxas FFXI gen is an awesome cell shaded low poly
 * look ... that as the gold standard ... like cell shading engine then
 * source engine quality before unreal engine quality"). Quantizing the
 * diffuse term into discrete bands instead of a smooth gradient is the
 * whole visual difference between "flat-shaded primitive" and "cel-shaded
 * character" -- it needs zero new geometry/art, just this one change,
 * which is why it's the first concrete step toward that reference image's
 * look rather than something blocked on real 3D models. Paired with the
 * outline pass below (VS_OUTLINE_SRC/FS_OUTLINE_SRC) for the hard black
 * silhouette line the reference also has. */
static const char *FS_SRC =
    "#version 150\n"
    "in vec3 vNormal;\n"
    "uniform vec4 uColor;\n"
    "uniform vec3 uLightDir;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);\n"
    "    float band;\n"
    "    if (diff > 0.75) band = 1.0;\n"
    "    else if (diff > 0.35) band = 0.65;\n"
    "    else band = 0.35;\n"
    "    fragColor = vec4(uColor.rgb * band, uColor.a);\n"
    "}\n";

/* Outline pass: classic "inverted hull" cel-shading technique (expand each
 * vertex outward along its own normal by a small world-space amount, draw
 * back-facing-only in solid near-black, BEFORE the real front-facing
 * cel-shaded draw) -- Wind Waker's rendering approach, not invented here.
 * Applied per hero-box primitive (draw_hero_box_facing below), not as a
 * whole-character screen-space post-process, since this renderer has no
 * FBO/post-process pipeline yet -- a real, known limitation: a multi-box
 * hero silhouette gets an outline seam between its own boxes, not one
 * single clean silhouette line. Good enough for "lay the groundwork";
 * worth revisiting with a proper edge-detection post-process pass when
 * this moves toward "Source engine" tier. */
static const char *VS_OUTLINE_SRC =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "uniform mat4 uOutlineMVP;\n"
    "uniform float uOutlineWidth;\n"
    "void main() {\n"
    "    vec3 expanded = aPos + aNormal * uOutlineWidth;\n"
    "    gl_Position = uOutlineMVP * vec4(expanded, 1.0);\n"
    "}\n";

static const char *FS_OUTLINE_SRC =
    "#version 150\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(0.05, 0.05, 0.08, 1.0);\n"
    "}\n";

/* Set once at startup (main()), read by draw_hero_box_facing on every hero
 * draw call -- same "fixed-for-the-session globals bridging rendering
 * state across this file's helper functions" pattern g_gband_loc_mvp/
 * g_gband_loc_model already use below. g_outline_prog == 0 means "outline
 * pass unavailable" (link failure) -- checked, not assumed. */
static GLuint g_outline_prog = 0;
static GLint g_outline_loc_mvp = -1;
static GLint g_outline_loc_width = -1;
/* The main cel-shaded program's handle -- draw_hero_box_facing needs this
 * to switch back after its outline pre-pass runs on g_outline_prog, since
 * glUseProgram_(prog) is otherwise only ever called once per frame, well
 * before the per-hero draw loop (see the 3D render pass), not re-bound
 * per draw call. */
static GLuint g_main_prog = 0;

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, vs);
    glAttachShader_(prog, fs);
    glBindAttribLocation_(prog, 0, "aPos");
    glBindAttribLocation_(prog, 1, "aNormal");
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
    }
    glDeleteShader_(vs);
    glDeleteShader_(fs);
    return prog;
}

/* ---------------- meshes ---------------- */
/* Unit cube, -0.5..0.5, position+normal interleaved, 36 verts. */
static const float CUBE_VERTS[] = {
    /* +X */  0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,
              0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,   0.5f,-0.5f, 0.5f, 1,0,0,
    /* -X */ -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,
             -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,  -0.5f,-0.5f,-0.5f,-1,0,0,
    /* +Y */ -0.5f, 0.5f,-0.5f, 0,1,0,  -0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,
             -0.5f, 0.5f,-0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f,-0.5f, 0,1,0,
    /* -Y */ -0.5f,-0.5f, 0.5f, 0,-1,0, -0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,
             -0.5f,-0.5f, 0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f, 0.5f, 0,-1,0,
    /* +Z */ -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,
             -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,  -0.5f, 0.5f, 0.5f, 0,0,1,
    /* -Z */  0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,
              0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,  0.5f, 0.5f,-0.5f, 0,0,-1,
};
#define CUBE_VERT_COUNT 36

/* Flat 1x1 ground quad in the XZ plane, normal up. */
static const float PLANE_VERTS[] = {
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, 0.5f, 0,1,0,
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0,  0.5f, 0,1,0,  -0.5f, 0, 0.5f, 0,1,0,
};
#define PLANE_VERT_COUNT 6

#define RING_SEGMENTS 24
#define RING_VERT_COUNT (RING_SEGMENTS * 6)
static float RING_VERTS[RING_VERT_COUNT * 6]; /* filled at startup: pos.xyz + normal.xyz per vertex */

static void build_ring_mesh(float inner_r, float outer_r) {
    int vi = 0;
    for (int i = 0; i < RING_SEGMENTS; i++) {
        float a0 = (float)i / RING_SEGMENTS * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / RING_SEGMENTS * 2.0f * (float)M_PI;
        float ix0 = cosf(a0) * inner_r, iz0 = sinf(a0) * inner_r;
        float ox0 = cosf(a0) * outer_r, oz0 = sinf(a0) * outer_r;
        float ix1 = cosf(a1) * inner_r, iz1 = sinf(a1) * inner_r;
        float ox1 = cosf(a1) * outer_r, oz1 = sinf(a1) * outer_r;
        float quad[6][3] = {
            {ix0, 0, iz0}, {ox0, 0, oz0}, {ox1, 0, oz1},
            {ix0, 0, iz0}, {ox1, 0, oz1}, {ix1, 0, iz1},
        };
        for (int v = 0; v < 6; v++) {
            RING_VERTS[vi++] = quad[v][0];
            RING_VERTS[vi++] = quad[v][1];
            RING_VERTS[vi++] = quad[v][2];
            RING_VERTS[vi++] = 0; RING_VERTS[vi++] = 1; RING_VERTS[vi++] = 0;
        }
    }
}

/* disc_mesh (S170-200, founder: "zone abilities dont read at all we need true aoe cast
 * circle... show cast radius... circle on the ground... nice shader spell effect simple but
 * nice"): a flat, unit-radius filled circle (triangle fan from the center), same "build once at
 * unit scale, mat4_scale to the real size at draw time" idiom as ring_mesh above -- reused for
 * every zone ability's ground-radius render rather than approximating a fill out of ring_mesh's
 * own thin annulus, which reads as a wire outline, not a real area, at a glance across a busy
 * team fight. */
#define DISC_SEGMENTS 24
#define DISC_VERT_COUNT (DISC_SEGMENTS * 3)
static float DISC_VERTS[DISC_VERT_COUNT * 6];

static void build_disc_mesh(void) {
    int vi = 0;
    for (int i = 0; i < DISC_SEGMENTS; i++) {
        float a0 = (float)i / DISC_SEGMENTS * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / DISC_SEGMENTS * 2.0f * (float)M_PI;
        float x0 = cosf(a0), z0 = sinf(a0);
        float x1 = cosf(a1), z1 = sinf(a1);
        float tri[3][3] = {
            {0, 0, 0}, {x0, 0, z0}, {x1, 0, z1},
        };
        for (int v = 0; v < 3; v++) {
            DISC_VERTS[vi++] = tri[v][0];
            DISC_VERTS[vi++] = tri[v][1];
            DISC_VERTS[vi++] = tri[v][2];
            DISC_VERTS[vi++] = 0; DISC_VERTS[vi++] = 1; DISC_VERTS[vi++] = 0;
        }
    }
}

typedef struct { GLuint vao, vbo; int count; } Mesh;

static Mesh upload_mesh(const float *verts, int vert_count) {
    Mesh m; m.count = vert_count;
    glGenVertexArrays_(1, &m.vao);
    glBindVertexArray_(m.vao);
    glGenBuffers_(1, &m.vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, m.vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(float) * 6 * vert_count, verts, GL_STATIC_DRAW);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray_(1);
    glBindVertexArray_(0);
    return m;
}

static void draw_mesh(const Mesh *m) {
    glBindVertexArray_(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
    glBindVertexArray_(0);
}

/* gband_rig callback plumbing (S144-06): gband_rig.c never links against
 * this file's dynamically-loaded GL function pointers or its Mesh struct
 * directly (see gband_rig.h's own doc comment on why) -- these two thin
 * wrappers are the bridge instead. g_gband_loc_mvp/g_gband_loc_model are set
 * once per call at the hero-draw call site, since loc_mvp/loc_model are
 * this program's shader uniform locations, fixed for the whole session but
 * not otherwise accessible as globals. */
static GLint g_gband_loc_mvp, g_gband_loc_model;
static void gband_cb_set_mvp_model(const Mat4 *mvp, const Mat4 *model) {
    glUniformMatrix4fv_(g_gband_loc_mvp, 1, GL_FALSE, mvp->m);
    glUniformMatrix4fv_(g_gband_loc_model, 1, GL_FALSE, model->m);
}
static void gband_cb_draw_mesh(const void *m) { draw_mesh((const Mesh *)m); }

/* gband_mesh_rig callback plumbing (S144-07): a persistent dynamic VAO/VBO,
 * re-uploaded (glBufferData with fresh contents each call -- simplest
 * correct approach for a single test character, not perf-critical) rather
 * than the static cube_mesh's one-time upload, since skinned vertex data
 * changes every frame. */
static Mesh g_gband_mesh_dynamic;
static int g_gband_mesh_dynamic_ready = 0;
/* S144-07 real bug found live: the original 512 cap was sized for the
 * synthetic proof mesh (56 tris = 168 flattened verts) and silently
 * dropped every draw call for the real founder-modeled Tyler (974 tris =
 * 2922 flattened verts) -- gband_mesh_cb_draw_skinned's own bounds check
 * just returned early, no error, no crash, just an invisible hero. 8192
 * gives real headroom above the current real model. */
#define GBAND_MESH_DYNAMIC_MAX_VERTS 8192

static void gband_mesh_dynamic_init(void) {
    glGenVertexArrays_(1, &g_gband_mesh_dynamic.vao);
    glBindVertexArray_(g_gband_mesh_dynamic.vao);
    glGenBuffers_(1, &g_gband_mesh_dynamic.vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_gband_mesh_dynamic.vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(float) * 6 * GBAND_MESH_DYNAMIC_MAX_VERTS, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray_(1);
    glBindVertexArray_(0);
    g_gband_mesh_dynamic.count = 0;
    g_gband_mesh_dynamic_ready = 1;
}

static void gband_mesh_cb_draw_skinned(const float *verts6, int vert_count, const Mat4 *mvp, const Mat4 *model) {
    if (!g_gband_mesh_dynamic_ready || vert_count > GBAND_MESH_DYNAMIC_MAX_VERTS) return;
    glBindBuffer_(GL_ARRAY_BUFFER, g_gband_mesh_dynamic.vbo);
    /* Founder-reported lag (2026-08-05, real skinned Tyler): the CPU
       skinning math itself benchmarked at ~0.12ms/frame against the real
       2922-vert mesh (not the bottleneck) -- glBufferSubData with no
       orphan hint is the likelier culprit, a well-known way to force a
       CPU/GPU sync stall (the driver has to wait for last frame's draw to
       finish reading this buffer before it'll let the CPU overwrite it).
       Re-specifying with glBufferData(NULL) first tells the driver "don't
       care about old contents, hand me a fresh buffer" so it can keep
       last frame's version alive for the in-flight draw instead of
       stalling. Standard technique for per-frame dynamic vertex data. */
    glBufferData_(GL_ARRAY_BUFFER, sizeof(float) * 6 * GBAND_MESH_DYNAMIC_MAX_VERTS, NULL, GL_DYNAMIC_DRAW);
    glBufferSubData_(GL_ARRAY_BUFFER, 0, sizeof(float) * 6 * vert_count, verts6);
    g_gband_mesh_dynamic.count = vert_count;
    glUniformMatrix4fv_(g_gband_loc_mvp, 1, GL_FALSE, mvp->m);
    glUniformMatrix4fv_(g_gband_loc_model, 1, GL_FALSE, model->m);
    draw_mesh(&g_gband_mesh_dynamic);
}

/* one box of a hero model, in hero-local space (dx/dy/dz offset from the hero's
   footprint, sx/sy/sz box scale) -- dy is measured from the ground, not from the
   hero's own translate, since hero translate is already y=0.5 (see caller) */
/* squish (S170-128, "add charming squish animations" -> "for movement also spell casts"):
 * 1.0 = neutral. <1.0 = squashed (short and wide, feet still on the ground). >1.0 = stretched
 * (tall and thin). Applied uniformly to every box in a hero's silhouette so the whole model
 * squishes together, not one accent piece independently of the body. The Y scale AND Y offset
 * both get multiplied by squish (not just scale) so a squashed hero's boxes compress toward
 * the ground plane instead of scaling around each box's own center and clipping into the
 * floor or floating above it. X/Z get the inverse relationship (a squashed hero reads wider,
 * a stretched one reads thinner) for a cheap volume-preserving cartoon feel, not physically
 * exact but the "charming" part of squash-and-stretch was never about being exact. */
/* draw_hero_box_facing (S170-171): see hero_facing_rad's own doc comment.
 * dx/dy/dz are still hero-LOCAL offsets (unchanged meaning from
 * draw_hero_box below), now rotated by facing_rad about the hero's own
 * origin before translating out to hero_x/hero_z in world space -- a
 * silhouette's asymmetric pieces (a horn, a bill, a front nub) sweep
 * around together as one rigid shape instead of staying frozen pointing
 * at a fixed +Z regardless of which way the hero's actually walking.
 * squish is still applied in hero-local space first (unchanged behavior
 * from draw_hero_box), so a squashed/stretched hero still rotates as
 * itself, not stretched along the world axes. facing_rad=0.0f is
 * identical to the old draw_hero_box behavior exactly (mat4_rotate_y(0)
 * is the identity), so draw_hero_box below keeps every existing call site
 * working unchanged via a thin wrapper. */
static void draw_hero_box_facing(float hero_x, float hero_z, float facing_rad, float dx, float dy, float dz,
                                  float sx, float sy, float sz, float squish, const Mat4 *vp,
                                  GLint loc_mvp, GLint loc_model, const Mesh *cube_mesh) {
    float squish_xz = 2.0f - squish;
    if (squish_xz < 0.4f) squish_xz = 0.4f;
    Mat4 local_t = mat4_translate(dx * squish_xz, dy * squish, dz * squish_xz);
    Mat4 local_s = mat4_scale(sx * squish_xz, sy * squish, sz * squish_xz);
    Mat4 local = mat4_multiply(&local_t, &local_s);
    Mat4 world_t = mat4_translate(hero_x, 0.0f, hero_z);
    Mat4 rot = mat4_rotate_y(facing_rad);
    Mat4 world = mat4_multiply(&world_t, &rot);
    Mat4 model = mat4_multiply(&world, &local);
    Mat4 mvp = mat4_multiply(vp, &model);

    if (g_outline_prog) {
        /* Outline pre-pass (S180-09): back-facing triangles only, expanded
         * outward along their own normal in mesh-local space (so the
         * resulting world-space outline thickness scales naturally with
         * this box's own size, via the same mvp), solid near-black. Culls
         * GL_FRONT (keeps only back faces) -- verified empirically against
         * CUBE_VERTS' real winding via a live Xvfb screenshot (0.30 width
         * exaggerated test first, to confirm the technique itself was
         * correct before tuning down -- too thin to read at 0.06, clean at
         * 0.12). Must restore glUseProgram_(prog) and disable culling
         * before returning -- neither is otherwise touched again this
         * frame (see g_main_prog's own doc comment). Known limitation on
         * box primitives specifically: a cube's hard 90-degree face
         * transitions mean a wide outline value produces "ear"-like
         * artifacts at corners rather than a clean rim (visible testing
         * this at 0.30) -- 0.12 stays well clear of that. */
        glUseProgram_(g_outline_prog);
        glUniformMatrix4fv_(g_outline_loc_mvp, 1, GL_FALSE, mvp.m);
        glUniform1f_(g_outline_loc_width, 0.12f);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        draw_mesh(cube_mesh);
        glDisable(GL_CULL_FACE);
        glUseProgram_(g_main_prog);
    }

    glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
    draw_mesh(cube_mesh);
}

static void draw_hero_box(float hero_x, float hero_z, float dx, float dy, float dz,
                           float sx, float sy, float sz, float squish, const Mat4 *vp,
                           GLint loc_mvp, GLint loc_model, const Mesh *cube_mesh) {
    draw_hero_box_facing(hero_x, hero_z, 0.0f, dx, dy, dz, sx, sy, sz, squish, vp, loc_mvp, loc_model, cube_mesh);
}

/* S170-118 -- real per-hero silhouette instead of one generic cube for all 18.
   Every box shares the caller's relationship color (self/team/enemy, see the
   call site) so team/self legibility -- already solved by S170-89/96 -- is
   never overridden by per-hero identity; only SHAPE encodes which hero this is.
   Reuses the silhouette concepts already designed for the 7 SHANKPIT skins
   (apps/lobby/src/main.c draw_player_skin_*) where a hero overlaps one, expressed
   here as axis-aligned draw_mesh() boxes -- originally couldn't rotate to
   face movement direction at all (this renderer had no mat4_rotate and
   SHANKPIT's immediate-mode glPushMatrix/glRotatef code can't port
   verbatim); mat4_rotate_y (S170-171) closed that gap, so every box below
   now rotates together as one rigid silhouette via facing_rad instead of
   staying frozen pointing at a fixed +Z regardless of which way the hero's
   actually walking -- see hero_facing_rad's own doc comment for how
   facing_rad itself gets computed. */
static void draw_hero_model(ArenaHeroID hero_id, float hero_x, float hero_z, float facing_rad, float squish, const Mat4 *vp,
                             GLint loc_mvp, GLint loc_model, const Mesh *cube_mesh) {
#define BOX(dx, dy, dz, sx, sy, sz) \
    draw_hero_box_facing(hero_x, hero_z, facing_rad, dx, dy, dz, sx, sy, sz, squish, vp, loc_mvp, loc_model, cube_mesh)
    switch (hero_id) {
        case ARENA_HERO_UNICORN: /* SHANKPIT SKIN_UNICORN: body + tapered horn */
            BOX(0.0f, 0.55f, 0.0f, 0.85f, 1.1f, 0.85f);
            BOX(0.0f, 1.25f, 0.35f, 0.14f, 0.4f, 0.14f);
            break;
        case ARENA_HERO_DUCK: /* SHANKPIT SKIN_DUCK: squat wide body + forward bill */
            BOX(0.0f, 0.35f, 0.0f, 1.0f, 0.7f, 1.0f);
            BOX(0.0f, 0.35f, 0.55f, 0.3f, 0.16f, 0.35f);
            break;
        case ARENA_HERO_GHOST: /* SHANKPIT SKIN_GHOST: tall tapered legless body */
            BOX(0.0f, 0.8f, 0.0f, 0.55f, 1.6f, 0.55f);
            break;
        case ARENA_HERO_FROG: /* SHANKPIT SKIN_FROG: wide flat body + bulging eyes */
            BOX(0.0f, 0.3f, 0.0f, 1.1f, 0.55f, 1.05f);
            BOX(-0.25f, 0.68f, 0.3f, 0.2f, 0.2f, 0.2f);
            BOX(0.25f, 0.68f, 0.3f, 0.2f, 0.2f, 0.2f);
            break;
        case ARENA_HERO_DOC_WHEEL: /* wide flat base "wheel" + upright body */
            BOX(0.0f, 0.55f, 0.0f, 0.65f, 1.0f, 0.65f);
            BOX(0.0f, 0.12f, 0.0f, 1.15f, 0.16f, 1.15f);
            break;
        case ARENA_HERO_TREE: /* SHANKPIT SKIN_TREE: narrow trunk + wide canopy */
            BOX(0.0f, 0.5f, 0.0f, 0.4f, 1.6f, 0.4f);
            BOX(0.0f, 1.25f, 0.0f, 1.05f, 0.55f, 1.05f);
            break;
        case ARENA_HERO_PIZZA: /* SHANKPIT SKIN_PIZZA: flat wide wedge */
            BOX(0.0f, 0.18f, 0.0f, 1.3f, 0.3f, 1.3f);
            break;
        case ARENA_HERO_FLAMEL: /* alchemist -- body + a small flame-accent box */
            BOX(0.0f, 0.6f, 0.0f, 0.8f, 1.2f, 0.8f);
            BOX(0.3f, 1.35f, 0.0f, 0.2f, 0.3f, 0.2f);
            break;
        case ARENA_HERO_MORRIGAN: /* raven-goddess -- body + two side wing slabs */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(-0.55f, 0.9f, 0.0f, 0.35f, 0.55f, 0.15f);
            BOX(0.55f, 0.9f, 0.0f, 0.35f, 0.55f, 0.15f);
            break;
        case ARENA_HERO_DAGDA: /* bruiser king -- one big bulky box */
            BOX(0.0f, 0.65f, 0.0f, 1.2f, 1.3f, 1.2f);
            break;
        case ARENA_HERO_COURIER: /* Ratatoskr -- thin tall messenger + tail-flick accent */
            BOX(0.0f, 0.7f, 0.0f, 0.65f, 1.4f, 0.65f);
            BOX(0.0f, 1.1f, -0.45f, 0.18f, 0.5f, 0.18f);
            break;
        case ARENA_HERO_LOKI: /* duality -- main body + a smaller offset "double" */
            BOX(0.0f, 0.6f, 0.0f, 0.8f, 1.2f, 0.8f);
            BOX(0.5f, 0.4f, 0.35f, 0.4f, 0.8f, 0.4f);
            break;
        case ARENA_HERO_GARY: /* off-duty security, marksman -- boxy body + a long rifle/scope
                                  bar held out to the side, not a chest-mounted slab (S170-131:
                                  was near-identical to Abraham's grimoire silhouette) */
            BOX(0.0f, 0.65f, 0.0f, 0.8f, 1.3f, 0.8f);
            BOX(0.55f, 0.55f, 0.15f, 0.55f, 0.08f, 0.08f);
            break;
        case ARENA_HERO_FLUTE_DEBT: /* thin tall body + horizontal flute accent */
            BOX(0.0f, 0.7f, 0.0f, 0.65f, 1.4f, 0.65f);
            BOX(0.45f, 0.95f, 0.0f, 0.55f, 0.1f, 0.1f);
            break;
        case ARENA_HERO_BACON_PUCK: /* two merged heroes -- two half-width bodies side by side */
            BOX(-0.32f, 0.6f, 0.0f, 0.55f, 1.2f, 0.75f);
            BOX(0.32f, 0.5f, 0.0f, 0.55f, 1.0f, 0.75f);
            break;
        case ARENA_HERO_ABRAHAM: /* mage -- body + a flat "grimoire" accent + a small floating
                                     arcane orb above it (S170-131: the book alone read almost
                                     identically to Gary's old clipboard slab at the same spot) */
            BOX(0.0f, 0.65f, 0.0f, 0.8f, 1.3f, 0.8f);
            BOX(0.0f, 0.65f, 0.45f, 0.3f, 0.4f, 0.08f);
            BOX(0.0f, 1.05f, 0.4f, 0.14f, 0.14f, 0.14f);
            break;
        case ARENA_HERO_ADA: /* mech pilot -- boxy, oversized mech-like frame */
            BOX(0.0f, 0.7f, 0.0f, 1.0f, 1.4f, 1.0f);
            BOX(0.0f, 1.55f, 0.0f, 0.4f, 0.3f, 0.4f);
            break;
        case ARENA_HERO_TYLER: /* deliberately unremarkable plain humanoid, per character */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            break;
        case ARENA_HERO_PAIMON: /* Court Voice -- robed commander body + a raised scepter accent */
            BOX(0.0f, 0.65f, 0.0f, 0.85f, 1.3f, 0.85f);
            BOX(0.35f, 1.3f, 0.0f, 0.12f, 0.5f, 0.12f);
            break;
        case ARENA_HERO_NOOR1: /* the snowman form (S170-104) -- three stacked boxes, decreasing size */
            BOX(0.0f, 0.40f, 0.0f, 0.55f, 0.40f, 0.55f);
            BOX(0.0f, 0.95f, 0.0f, 0.40f, 0.35f, 0.40f);
            BOX(0.0f, 1.40f, 0.0f, 0.28f, 0.28f, 0.28f);
            break;
        case ARENA_HERO_CAIN: /* weathered wanderer body + the mark itself, front and center on
                                  the forehead -- Genesis's own imagery, not an incidental
                                  shoulder detail (S170-105, enlarged+repositioned S170-131: at
                                  the old size/spot it was nearly lost against Tyler's
                                  deliberately bare identical-base body) */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(0.0f, 1.32f, 0.36f, 0.22f, 0.2f, 0.06f);
            break;
        case ARENA_HERO_GUNNR: /* shieldmaiden -- body + a flat shield accent (S170-93) */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(-0.65f, 0.65f, 0.0f, 0.10f, 0.55f, 0.45f);
            break;
        case ARENA_HERO_VASSAGO: /* soft foresight -- slender cloaked body + a small floating orb (S170-93) */
            BOX(0.0f, 0.60f, 0.0f, 0.55f, 1.2f, 0.55f);
            BOX(0.0f, 1.55f, 0.0f, 0.16f, 0.16f, 0.16f);
            break;
        case ARENA_HERO_HE_XIANGU: /* immortal ascetic -- slender robed body + a small crescent accent (S170-93) */
            BOX(0.0f, 0.62f, 0.0f, 0.5f, 1.25f, 0.5f);
            BOX(0.0f, 1.5f, 0.35f, 0.2f, 0.06f, 0.06f);
            break;
        case ARENA_HERO_BELETH: /* the Detonation -- body + three angled shard accents radiating
                                    outward, a burst pattern no other silhouette on the roster
                                    uses (S170-93) */
            BOX(0.0f, 0.6f, 0.0f, 0.7f, 1.25f, 0.7f);
            BOX(0.4f, 0.9f, 0.25f, 0.12f, 0.12f, 0.35f);
            BOX(-0.4f, 0.9f, 0.25f, 0.12f, 0.12f, 0.35f);
            BOX(0.0f, 1.15f, -0.35f, 0.12f, 0.12f, 0.35f);
            break;
        case ARENA_HERO_MNM: /* the Shapeshifting Crab -- wide low shell + two forward claw
                                 accents, low center of gravity distinct from every other
                                 silhouette on the roster (S170-134) */
            BOX(0.0f, 0.35f, 0.0f, 1.15f, 0.6f, 1.0f);
            BOX(0.75f, 0.35f, 0.35f, 0.25f, 0.25f, 0.4f);
            BOX(-0.75f, 0.35f, 0.35f, 0.25f, 0.25f, 0.4f);
            break;
        case ARENA_HERO_ZAGAN: /* the Standstill's Confessor -- tall, narrow, motionless-reading
                                   silhouette (the stillness/confession theme, not a demon-horns
                                   cliche): a slim body, a flat halo ring above the head (he
                                   "presides"), a small chest-height ledger accent (S170-230) */
            BOX(0.0f, 0.5f, 0.0f, 0.55f, 1.0f, 0.55f);
            BOX(0.0f, 1.25f, 0.0f, 0.5f, 0.06f, 0.5f);
            BOX(0.0f, 0.55f, 0.4f, 0.25f, 0.2f, 0.08f);
            break;
        default:
            BOX(0.0f, 0.5f, 0.0f, 0.9f, 1.0f, 0.9f);
            break;
    }
#undef BOX
}

/* ---------------- tiny immediate-mode HUD text (ported from apps/lobby) ---------------- */
static void draw_char(char c, float x, float y, float s) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); /* fold lowercase -- one glyph set, not two */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (c >= '0' && c <= '9') {
        /* Real 7-segment-style digits (S170-185, founder: "ensure our font can render
           numbers"). Real bug fixed here: every digit used to draw the exact same generic box
           outline, indistinguishable from any other -- every numeric HUD value this game shows
           (HP/MP, ability cooldown countdown, Flow/XP/item costs, K/D, APM) was effectively
           illegible as a SPECIFIC number, just "some digits are here." Standard 7-segment
           mapping, same GL_LINES stroke style as every other glyph in this font. */
        int seg_top = 0, seg_top_left = 0, seg_top_right = 0, seg_mid = 0;
        int seg_bot_left = 0, seg_bot_right = 0, seg_bot = 0;
        switch (c) {
        case '0': seg_top = seg_top_left = seg_top_right = seg_bot_left = seg_bot_right = seg_bot = 1; break;
        case '1': seg_top_right = seg_bot_right = 1; break;
        case '2': seg_top = seg_top_right = seg_mid = seg_bot_left = seg_bot = 1; break;
        case '3': seg_top = seg_top_right = seg_mid = seg_bot_right = seg_bot = 1; break;
        case '4': seg_top_left = seg_top_right = seg_mid = seg_bot_right = 1; break;
        case '5': seg_top = seg_top_left = seg_mid = seg_bot_right = seg_bot = 1; break;
        case '6': seg_top = seg_top_left = seg_mid = seg_bot_left = seg_bot_right = seg_bot = 1; break;
        case '7': seg_top = seg_top_right = seg_bot_right = 1; break;
        case '8': seg_top = seg_top_left = seg_top_right = seg_mid = seg_bot_left = seg_bot_right = seg_bot = 1; break;
        case '9': seg_top = seg_top_left = seg_top_right = seg_mid = seg_bot_right = seg_bot = 1; break;
        }
        if (seg_top) { glVertex2f(x, y + s); glVertex2f(x + s, y + s); }
        if (seg_top_left) { glVertex2f(x, y + s); glVertex2f(x, y + s / 2); }
        if (seg_top_right) { glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2); }
        if (seg_mid) { glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2); }
        if (seg_bot_left) { glVertex2f(x, y + s / 2); glVertex2f(x, y); }
        if (seg_bot_right) { glVertex2f(x + s, y + s / 2); glVertex2f(x + s, y); }
        if (seg_bot) { glVertex2f(x, y); glVertex2f(x + s, y); }
    } else if (c == 'W') {
        glVertex2f(x, y + s); glVertex2f(x + s * 0.25f, y);
        glVertex2f(x + s * 0.25f, y); glVertex2f(x + s * 0.5f, y + s * 0.6f);
        glVertex2f(x + s * 0.5f, y + s * 0.6f); glVertex2f(x + s * 0.75f, y);
        glVertex2f(x + s * 0.75f, y); glVertex2f(x + s, y + s);
    } else if (c == 'I') {
        glVertex2f(x + s / 2, y); glVertex2f(x + s / 2, y + s);
    } else if (c == 'N') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'L') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
    } else if (c == 'O') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    } else if (c == 'S') {
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
    } else if (c == 'E') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'U') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'Y') {
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s / 2, y);
    } else if (c == 'H') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
    } else if (c == 'P') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
    } else if (c == ' ') {
    /* The rest of the alphabet + a handful of punctuation marks (S170's font-glyph gap, found
       live: tonight's new hero names -- Gary, Bacon+Puck, Abraham, Ada -- use letters this font
       never covered, falling through to the generic missing-glyph box below for most of their
       own names). Same simple GL_LINES stroke style as the letters above, not a real font. */
    } else if (c == 'A') {
        glVertex2f(x, y); glVertex2f(x + s / 2, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s * 0.25f, y + s * 0.4f); glVertex2f(x + s * 0.75f, y + s * 0.4f);
    } else if (c == 'B') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s * 0.7f, y + s);
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.7f, y + s / 2);
        glVertex2f(x + s * 0.7f, y + s / 2); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.7f, y + s / 2);
        glVertex2f(x + s * 0.7f, y + s / 2); glVertex2f(x + s * 0.7f, y);
        glVertex2f(x + s * 0.7f, y); glVertex2f(x, y);
    } else if (c == 'C') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
    } else if (c == 'D') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s * 0.6f, y + s);
        glVertex2f(x + s * 0.6f, y + s); glVertex2f(x + s, y + s * 0.7f);
        glVertex2f(x + s, y + s * 0.7f); glVertex2f(x + s, y + s * 0.3f);
        glVertex2f(x + s, y + s * 0.3f); glVertex2f(x + s * 0.6f, y);
        glVertex2f(x + s * 0.6f, y); glVertex2f(x, y);
    } else if (c == 'F') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'G') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s * 0.5f);
        glVertex2f(x + s * 0.5f, y + s * 0.5f); glVertex2f(x + s, y + s * 0.5f);
    } else if (c == 'J') {
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.7f, y + s * 0.2f);
        glVertex2f(x + s * 0.7f, y + s * 0.2f); glVertex2f(x + s * 0.3f, y);
        glVertex2f(x + s * 0.3f, y); glVertex2f(x, y + s * 0.2f);
    } else if (c == 'K') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y);
    } else if (c == 'M') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
    } else if (c == 'Q') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x + s * 0.55f, y + s * 0.35f); glVertex2f(x + s, y);
    } else if (c == 'R') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s, y);
    } else if (c == 'T') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s / 2, y);
    } else if (c == 'V') {
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y);
        glVertex2f(x + s / 2, y); glVertex2f(x + s, y + s);
    } else if (c == 'X') {
        glVertex2f(x, y); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
    } else if (c == 'Z') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
    } else if (c == '-') {
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
    } else if (c == '+') {
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s / 2, y); glVertex2f(x + s / 2, y + s);
    } else if (c == '\'' || c == '"') {
        glVertex2f(x + s * 0.5f, y + s * 0.75f); glVertex2f(x + s * 0.5f, y + s);
    } else if (c == '.') {
        glVertex2f(x + s * 0.4f, y); glVertex2f(x + s * 0.6f, y);
    } else if (c == ',') {
        glVertex2f(x + s * 0.5f, y); glVertex2f(x + s * 0.3f, y - s * 0.25f);
    } else if (c == ':') {
        glVertex2f(x + s * 0.4f, y + s * 0.7f); glVertex2f(x + s * 0.6f, y + s * 0.7f);
        glVertex2f(x + s * 0.4f, y + s * 0.25f); glVertex2f(x + s * 0.6f, y + s * 0.25f);
    } else if (c == '!') {
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s / 2, y + s * 0.3f);
        glVertex2f(x + s * 0.4f, y); glVertex2f(x + s * 0.6f, y);
    } else if (c == '(') {
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.3f, y + s * 0.5f);
        glVertex2f(x + s * 0.3f, y + s * 0.5f); glVertex2f(x + s * 0.7f, y);
    } else if (c == ')') {
        glVertex2f(x + s * 0.3f, y + s); glVertex2f(x + s * 0.7f, y + s * 0.5f);
        glVertex2f(x + s * 0.7f, y + s * 0.5f); glVertex2f(x + s * 0.3f, y);
    /* S170-151, founder: "ensure our font has all necessary glyphs" --
       found live ahead of the H-overlay ability-description panel: real
       ability text (percentages, semicolons in lists, question marks)
       would have silently fallen through to the generic missing-glyph box
       below, the same class of gap this font's own comment already
       flagged once before for hero names. Same simple GL_LINES stroke
       style as every other glyph here, not a real font. */
    } else if (c == '%') {
        glVertex2f(x, y); glVertex2f(x + s, y + s); /* the diagonal stroke */
        glVertex2f(x + s * 0.15f, y + s * 0.85f); glVertex2f(x + s * 0.15f, y + s * 0.7f); /* top-left ring, drawn as a short stroke */
        glVertex2f(x + s * 0.85f, y + s * 0.3f); glVertex2f(x + s * 0.85f, y + s * 0.15f); /* bottom-right ring */
    } else if (c == '?') {
        glVertex2f(x + s * 0.15f, y + s * 0.8f); glVertex2f(x + s * 0.5f, y + s);
        glVertex2f(x + s * 0.5f, y + s); glVertex2f(x + s * 0.85f, y + s * 0.8f);
        glVertex2f(x + s * 0.85f, y + s * 0.8f); glVertex2f(x + s * 0.5f, y + s * 0.55f);
        glVertex2f(x + s * 0.5f, y + s * 0.55f); glVertex2f(x + s * 0.5f, y + s * 0.35f);
        glVertex2f(x + s * 0.4f, y); glVertex2f(x + s * 0.6f, y); /* the dot */
    } else if (c == ';') {
        glVertex2f(x + s * 0.4f, y + s * 0.7f); glVertex2f(x + s * 0.6f, y + s * 0.7f); /* the dot, same as ':' */
        glVertex2f(x + s * 0.5f, y); glVertex2f(x + s * 0.3f, y - s * 0.25f); /* the tail, same as ',' */
    } else if (c == '/') {
        glVertex2f(x, y); glVertex2f(x + s, y + s);
    } else if (c == '&') {
        glVertex2f(x + s, y); glVertex2f(x + s * 0.3f, y + s * 0.55f);
        glVertex2f(x + s * 0.3f, y + s * 0.55f); glVertex2f(x + s * 0.65f, y + s * 0.8f);
        glVertex2f(x + s * 0.65f, y + s * 0.8f); glVertex2f(x + s * 0.4f, y + s);
        glVertex2f(x + s * 0.4f, y + s); glVertex2f(x + s * 0.1f, y + s * 0.75f);
        glVertex2f(x + s * 0.1f, y + s * 0.75f); glVertex2f(x + s * 0.75f, y);
    } else {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    }
    glEnd();
}

static void draw_string(const char *str, float x, float y, float size) {
    while (*str) {
        draw_char(*str, x, y, size);
        x += size * 1.2f;
        str++;
    }
}

/* hero_status_label (S170-133, founder: "text label above health bar above hero shows status
 * effects like stun silence root slow etc"): composes a short space-separated tag string from
 * whichever generic status-effect fields are currently active on this hero. Stun and slow
 * (S170-184, founder: "add more status effects use GFD [as a reference]") now have real generic
 * fields (stunned_ms/slowed_ms) closing the exact gap this function's own comment used to flag
 * here -- no kit applies them yet (arena_apply_stun/arena_apply_slow are the wiring hooks for a
 * future pass), but the HUD affordance is real infrastructure now, not aspirational. Returns 1
 * if buf has anything to draw. */
static int hero_status_label(const ArenaHero *h, char *buf, size_t bufsize) {
    buf[0] = '\0';
    size_t used = 0;
#define APPEND_TAG(tag) do { \
        int n = snprintf(buf + used, bufsize - used, "%s%s", used > 0 ? " " : "", tag); \
        if (n > 0 && (size_t)n < bufsize - used) used += (size_t)n; \
    } while (0)
    if (h->silenced_ms > 0) APPEND_TAG("SILENCED");
    if (h->rooted_ms > 0) APPEND_TAG("ROOTED");
    if (h->intangible_ms > 0) APPEND_TAG("INTANGIBLE");
    if (h->burning_ms > 0) APPEND_TAG("BURNING");
    if (h->survive_floor_ms > 0) {
        /* S170-210: name the source when it's Donkey's own Immortal's Fold, instead of
           the generic tag -- "clear something is happening" per the founder's own ask,
           not just "clear something happened" (that's what the FOLD_FLASH burst above
           the health bar and the distinct proc tone are for). */
        if (h->equipped_item[ARENA_ITEM_SLOT_BACK] == ARENA_DONKEY_ITEM_ID) APPEND_TAG("DONKEY FOLD");
        else APPEND_TAG("UNKILLABLE");
    }
    if (h->stunned_ms > 0) APPEND_TAG("STUNNED");
    if (h->slowed_ms > 0) APPEND_TAG("SLOWED");
    if (h->berserker_ms > 0) APPEND_TAG("BERSERKER");
    if (h->regen_ms > 0) APPEND_TAG("REGEN");
#undef APPEND_TAG
    return used > 0;
}

/* draw_queuing_screen (S170-115, real bug found live): net_find_and_connect()/net_connect() both
 * block the whole event loop for up to 60s -- with no frame rendered during that whole wait, the
 * window shows whatever was on screen before the click and never updates, which is genuinely
 * indistinguishable from a hang. The matchmaker log confirmed it: 13+ distinct source ports from
 * the same external IP in a few minutes, consistent with the founder force-quitting an apparently
 * frozen window and relaunching, over and over, each relaunch a fresh queue attempt that
 * abandoned the previous one mid-match. This renders one real "please wait" frame and presents
 * it (SDL_GL_SwapWindow) *before* the blocking call starts, so the last thing on screen is an
 * honest status, not a stale frame. Doesn't make the wait non-blocking -- that's a bigger
 * rearchitecture -- but makes the wait visibly a wait, not a crash. */
static void draw_queuing_screen(SDL_Window *win, int win_w, int win_h) {
    glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, 0, win_h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(0.6f, 1.0f, 0.7f);
    draw_string("QUEUING FOR MATCH", win_w / 2.0f - 190, win_h / 2.0f + 20, 20);
    glColor3f(0.7f, 0.8f, 0.75f);
    draw_string("PLEASE WAIT - THIS CAN TAKE UP TO 60 SECONDS", win_w / 2.0f - 300, win_h / 2.0f - 20, 12);
    draw_string("THE WINDOW WILL NOT RESPOND UNTIL A MATCH IS FOUND", win_w / 2.0f - 330, win_h / 2.0f - 44, 12);
    SDL_GL_SwapWindow(win);
}

/* Draft/pick screen (S170-182, split out from the old S170-69 northstar item -- "a real draft
 * hero-select UI" replacing the pure auto-pick that shipped instead, S170-66/68). One shared
 * grid layout, computed identically here and in draft_screen_hero_at() below, so a click always
 * lands on the tile it's visually over -- same "compute the same formula in both places" idiom
 * as the shop panel's own layout. */
#define DRAFT_GRID_COLS 6
#define DRAFT_CELL_W 190.0f
#define DRAFT_CELL_H 56.0f

/* draft_grid_origin: centers the grid, but clamps/shrinks it to the actual window bounds.
 *
 * Before this fix, gx0/gy_top were computed purely from DRAFT_GRID_COLS*DRAFT_CELL_W centered
 * on win_w/2 with no clamp -- fine at the 1280x720 default, but the window is resizable
 * (win_w/win_h track SDL_WINDOWEVENT_RESIZED), and on any narrower window (e.g. ~960px, a
 * common non-fullscreen/half-screen size) the rightmost column (hero_id % 6 == 5, which
 * includes Tyler at hero_id 17) rendered mostly or fully past the right edge -- unclickable
 * or only clickable in a sliver with its own label cut off, so a player who happened to want
 * a hero in that column could never send PACKET_ARENA_PICK. Server-side that's indistinguishable
 * from an AFK client: match sits at N/20 picked forever and dies on the 60s no-progress
 * timeout (see the resend-on-unpick comment near net_last_pick_send_ms above -- that fix
 * covers a pick getting dropped in flight, not a pick that can never be clicked in the first
 * place). Returns the cell size actually used so callers hit-test/draw against the same
 * shrunk grid, not the nominal DRAFT_CELL_W/H. */
static void draft_grid_origin(int win_w, int win_h, float *gx0, float *gy_top, float *cell_w, float *cell_h) {
    int rows = (ARENA_HERO_COUNT + DRAFT_GRID_COLS - 1) / DRAFT_GRID_COLS;
    float cw = DRAFT_CELL_W, ch = DRAFT_CELL_H;
    float grid_w = DRAFT_GRID_COLS * cw;
    float grid_h_avail = (float)win_h - 150.0f; /* leave room for the title/subtitle above */
    if (grid_w > (float)win_w) {
        cw = (float)win_w / DRAFT_GRID_COLS;
        grid_w = (float)win_w;
    }
    if ((float)rows * ch > grid_h_avail && grid_h_avail > 0.0f) {
        ch = grid_h_avail / (float)rows;
    }
    float gx = win_w / 2.0f - grid_w / 2.0f;
    if (gx < 0.0f) gx = 0.0f;
    if (gx + grid_w > win_w) gx = (float)win_w - grid_w;
    *gx0 = gx;
    *gy_top = win_h - 130.0f;
    *cell_w = cw;
    *cell_h = ch;
}

/* draft_screen_hero_at: hit-test a screen-space point (SDL's top-down mouse coords, NOT this
 * HUD's own bottom-up ortho space -- callers pass raw e.button.x/y) against the draft grid.
 * Returns the hero_id under that point, or -1 if none. */
static int draft_screen_hero_at(int mouse_x, int mouse_y, int win_w, int win_h) {
    float bx = (float)mouse_x, by = (float)(win_h - mouse_y);
    float gx0, gy_top, cell_w, cell_h;
    draft_grid_origin(win_w, win_h, &gx0, &gy_top, &cell_w, &cell_h);
    for (int hero_id = 0; hero_id < ARENA_HERO_COUNT; hero_id++) {
        int col = hero_id % DRAFT_GRID_COLS, row = hero_id / DRAFT_GRID_COLS;
        float cell_x = gx0 + (float)col * cell_w;
        float cell_top = gy_top - (float)row * cell_h;
        float cell_bottom = cell_top - (cell_h - 6.0f);
        if (bx >= cell_x + 3.0f && bx <= cell_x + cell_w - 3.0f && by >= cell_bottom && by <= cell_top) {
            return hero_id;
        }
    }
    return -1;
}

static void draw_draft_screen(SDL_Window *win, int win_w, int win_h, int hover_hero_id) {
    glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, 0, win_h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glColor3f(0.6f, 1.0f, 0.7f);
    draw_string("PICK YOUR HERO", win_w / 2.0f - 150.0f, win_h - 60.0f, 18);
    glColor3f(0.6f, 0.7f, 0.65f);
    draw_string("CLICK A TILE TO DRAFT IT", win_w / 2.0f - 160.0f, win_h - 90.0f, 10);

    float gx0, gy_top, cell_w, cell_h;
    draft_grid_origin(win_w, win_h, &gx0, &gy_top, &cell_w, &cell_h);
    for (int hero_id = 0; hero_id < ARENA_HERO_COUNT; hero_id++) {
        int col = hero_id % DRAFT_GRID_COLS, row = hero_id / DRAFT_GRID_COLS;
        float cell_x = gx0 + (float)col * cell_w;
        float cell_top = gy_top - (float)row * cell_h;
        float cell_bottom = cell_top - (cell_h - 6.0f);
        int hovered = (hero_id == hover_hero_id);
        glColor4f(hovered ? 0.2f : 0.1f, hovered ? 0.45f : 0.18f, hovered ? 0.25f : 0.16f, 0.9f);
        glRectf(cell_x + 3.0f, cell_bottom, cell_x + cell_w - 3.0f, cell_top);
        glColor3f(hovered ? 0.6f : 0.35f, hovered ? 1.0f : 0.55f, hovered ? 0.7f : 0.5f);
        glLineWidth(hovered ? 2.0f : 1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(cell_x + 3.0f, cell_bottom); glVertex2f(cell_x + cell_w - 3.0f, cell_bottom);
        glVertex2f(cell_x + cell_w - 3.0f, cell_top); glVertex2f(cell_x + 3.0f, cell_top);
        glEnd();
        glLineWidth(1.0f);
        glColor3f(hovered ? 0.9f : 0.75f, hovered ? 1.0f : 0.85f, hovered ? 0.95f : 0.8f);
        draw_string(arena_hero_name(hero_id), cell_x + 12.0f, cell_bottom + (cell_h - 6.0f) / 2.0f - 4.0f, 9);
    }
    SDL_GL_SwapWindow(win);
}

/* ---------------- placement rings ---------------- */
#define MAX_RINGS 6
#define RING_LIFETIME_MS 500.0f
typedef struct { float x, z, age_ms; int active; } Ring;
static Ring rings[MAX_RINGS];

static void spawn_ring(float x, float z) {
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!rings[i].active) {
            rings[i].active = 1;
            rings[i].x = x;
            rings[i].z = z;
            rings[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- attack flashes (S170-122, "add basic animations for auto
 * attacks") ---------------- */
/* Neither the wire snapshot (ArenaHeroSnapshot, deliberately minimal --
 * position/HP/alive/hero_id only) nor the local sim's per-hero state expose
 * a clean "an auto-attack just landed" signal that's available uniformly in
 * every render mode (local demo, net_mode, and replay/observe). What IS
 * available everywhere is HP itself -- so a frame-to-frame HP decrease on
 * any hero is treated as "something hit them" and gets a brief flash at
 * their position. This also catches ability damage, not just melee autos,
 * but for a first basic pass that's an honest, correctly-scoped simplification
 * rather than a wire-protocol change to carry real attack events. */
#define MAX_ATTACK_FLASHES ARENA_MAX_HEROES
#define ATTACK_FLASH_LIFETIME_MS 180.0f
typedef struct { float x, z, age_ms; int active; } AttackFlash;
static AttackFlash attack_flashes[MAX_ATTACK_FLASHES];
static int prev_hero_hp[ARENA_MAX_HEROES];
static int prev_hero_hp_valid[ARENA_MAX_HEROES];
/* S170-145 ("when auto attacks hit a creep or a hero it should show visual
 * indication of such"): the hero-side HP-delta flash already existed
 * (S170-122); creeps had none at all -- same idiom, mirrored for both
 * node-guardian and lane creep pools. Local-mode/1v1-demo only, same scope as
 * node-guardian/lane creeps' own sim-only (not wire-synced) status. */
static int prev_donkey_fold_active[ARENA_MAX_HEROES];
static int prev_donkey_fold_valid[ARENA_MAX_HEROES];
static int prev_creep_hp[ARENA_MAX_CREEPS];
static int prev_creep_hp_valid[ARENA_MAX_CREEPS];
static int prev_lane_creep_hp[ARENA_MAX_LANE_CREEPS];
static int prev_lane_creep_hp_valid[ARENA_MAX_LANE_CREEPS];
/* Ghost Q lightning burst (founder: "ghost's Q should have a cool crackle
 * lightning shader spell animation showing where the spell hit"): a
 * projectile slot's active->inactive transition (whether from a real hit or
 * a whiff/max-range fizzle -- this client has no wire signal that
 * distinguishes the two, same honest scoping tradeoff AttackFlash's own doc
 * comment already accepts for HP-delta) is the edge this watches. The
 * ArenaProjectile struct's own doc comment already earmarks hero_id for
 * exactly this ("client can pick a distinct visual per spell"). No
 * prev_x/prev_z needed alongside this: the snapshot-apply path only ever
 * flips `active` to 0 on despawn, it never clears x/z/hero_id, so the slot's
 * last-known position is still readable in the same frame the burst fires. */
static int prev_projectile_active[ARENA_MAX_PROJECTILES];

/* HealFlash (S170-143, "ensure we show cast animation on the target and the
 * self so its legible to all heroes on the battlefield"): AttackFlash's own
 * "reconstruct the event from a frame-to-frame HP delta" idiom, mirrored for
 * the increase direction instead of the decrease one. Generic (any heal,
 * from any source -- not Doc Wheel-specific), same reasoning as
 * AttackFlash's own doc comment: correctly-scoped without a wire-protocol
 * change to carry a real heal event. Warm green, visually distinct from the
 * attack flash's orange-white and every spell-cast ring color, so a heal
 * landing on a hero reads as a heal at a glance, on the TARGET's own
 * position -- which may be far from the caster, the actual gap this closes
 * (cast_flash_slot already covers "the caster's own position," this covers
 * the other half). */
#define MAX_HEAL_FLASHES ARENA_MAX_HEROES
#define HEAL_FLASH_LIFETIME_MS 260.0f
typedef struct { float x, z, age_ms; int active; } HealFlash;
static HealFlash heal_flashes[MAX_HEAL_FLASHES];

static void spawn_heal_flash(float x, float z) {
    for (int i = 0; i < MAX_HEAL_FLASHES; i++) {
        if (!heal_flashes[i].active) {
            heal_flashes[i].active = 1;
            heal_flashes[i].x = x;
            heal_flashes[i].z = z;
            heal_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* FoldFlash (S170-210, founder: "ensure donkey has affordances so its clear
 * something is happening when it procs on the 25% health thing"): Immortal's
 * Fold sets survive_floor_ms, which already drives the generic UNKILLABLE
 * status tag -- but that tag is silent about WHY, and gives no one-shot "it
 * just happened" pop the way heal-flash/attack-flash give every other HP
 * event on this battlefield. Same "reconstruct the event from a frame-to-
 * frame edge" idiom (no wire-protocol change needed: survive_floor_ms and
 * equipped_item are both already synced), just watching for a 0-to-active
 * transition instead of an HP delta. Bigger and longer-lived than a heal
 * flash on purpose -- a near-death save reads as a bigger deal than a Doc
 * Wheel tick. */
#define MAX_FOLD_FLASHES ARENA_MAX_HEROES
#define FOLD_FLASH_LIFETIME_MS 480.0f
typedef struct { float x, z, age_ms; int active; } FoldFlash;
static FoldFlash fold_flashes[MAX_FOLD_FLASHES];

static void spawn_fold_flash(float x, float z) {
    for (int i = 0; i < MAX_FOLD_FLASHES; i++) {
        if (!fold_flashes[i].active) {
            fold_flashes[i].active = 1;
            fold_flashes[i].x = x;
            fold_flashes[i].z = z;
            fold_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* LightningBurst: the impact half of Ghost's Q crackle effect, at the exact
 * spot the shot disappeared. Deliberately not the flat translate+scale
 * ring_mesh every other flash above uses -- a burst of jittered, radiating
 * box slivers reads as an electric discharge in a way a plain filled disc
 * never would, and reuses draw_hero_box_facing exactly as-is (no new
 * primitive). See spawn_lightning_burst's call site for the detection edge. */
#define MAX_LIGHTNING_BURSTS ARENA_MAX_PROJECTILES
#define LIGHTNING_BURST_LIFETIME_MS 300.0f
typedef struct { float x, z, age_ms; int active; } LightningBurst;
static LightningBurst lightning_bursts[MAX_LIGHTNING_BURSTS];

static void spawn_lightning_burst(float x, float z) {
    for (int i = 0; i < MAX_LIGHTNING_BURSTS; i++) {
        if (!lightning_bursts[i].active) {
            lightning_bursts[i].active = 1;
            lightning_bursts[i].x = x;
            lightning_bursts[i].z = z;
            lightning_bursts[i].age_ms = 0;
            return;
        }
    }
}

static void spawn_attack_flash(float x, float z) {
    for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
        if (!attack_flashes[i].active) {
            attack_flashes[i].active = 1;
            attack_flashes[i].x = x;
            attack_flashes[i].z = z;
            attack_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- squish (S170-128, "add charming squish animations" ->
 * "for movement also spell casts") ---------------- */
/* One timer per hero slot, not a pooled particle array like the flashes above --
 * squish is a continuous property of the hero's own model, not a spawned object
 * at a fixed world position, so it's simplest to key it directly by owner index.
 * A large/negative age means "not currently animating," read by compute_squish
 * as neutral (1.0, no visual change at all) without needing a separate active flag. */
#define SQUISH_ANIM_MS 260.0f
static float squish_age_ms[ARENA_MAX_HEROES];
static int prev_hero_moving[ARENA_MAX_HEROES];
static int prev_hero_moving_valid[ARENA_MAX_HEROES];

/* Abraham's Fireball windup animation (S202-34, founder: "give a micro rotation animation...
 * quick and smooth ease in and out use golden band as a parena mod" + "abraham should squish
 * way down using golden ratio to about 20 percent... windup animation go medium fast and then
 * snap up quite quick as a flick... take .4 seconds"). Purely client-side visual state, same
 * "never networked, derived from server-authoritative casting_slot/cast_target_x/z" idiom
 * hero_facing_rad's own movement-derived interpolation already uses -- the actual fireball
 * direction/damage is entirely server-authoritative (arena_toggle_w/tick_hero_kit), this is
 * ONLY what the local screen shows while that real windup is in progress. */
static float abraham_windup_age_ms[ARENA_MAX_HEROES];
static int abraham_windup_active[ARENA_MAX_HEROES]; /* edge-detects casting_slot's 0->2 transition per hero */
static float abraham_windup_start_facing[ARENA_MAX_HEROES];
static float abraham_windup_target_facing[ARENA_MAX_HEROES];
/* golden-ratio phase split (same 1.618034f this file's own arena_game.h already uses for
 * ARENA_HALF_EXTENT, not a new constant invented here): squish-down gets the LONGER share
 * (1/phi =~ 61.8% of the total windup, "medium fast"), snap-up gets the shorter remainder
 * (~38.2%, "quite quick as a flick"). */
#define ABRAHAM_WINDUP_PHI 1.618034f
#define ABRAHAM_WINDUP_SQUISH_DOWN_MS ((float)ARENA_ABRAHAM_FIREBALL_WINDUP_MS / ABRAHAM_WINDUP_PHI)
#define ABRAHAM_WINDUP_SQUISH_UP_MS ((float)ARENA_ABRAHAM_FIREBALL_WINDUP_MS - ABRAHAM_WINDUP_SQUISH_DOWN_MS)
#define ABRAHAM_WINDUP_SQUISH_BOTTOM 0.2f /* "squish way down... to about 20 percent" -- literal founder number, not phi-derived */

static GBClip g_windup_ease_clip;
static int g_windup_ease_loaded = 0;

/* windup_ease_sample: real GOLDENBAND playback (see gband.h) when the baked
 * assets/anim/rotation_ease.gband clip loaded successfully; a hand-computed
 * identical smoothstep as a fail-soft fallback otherwise (same "missing/
 * corrupt assets fail soft, never a crash" convention gband_rig_init's own
 * doc comment already establishes for the skinned-mesh pipeline) -- either
 * way the caller gets a real eased [0,1] progress value back. */
static float windup_ease_sample(float progress01) {
    if (progress01 < 0.0f) progress01 = 0.0f;
    if (progress01 > 1.0f) progress01 = 1.0f;
    if (g_windup_ease_loaded && g_windup_ease_clip.duration_ticks > 0) {
        uint32_t tick = (uint32_t)(progress01 * (float)(g_windup_ease_clip.duration_ticks - 1) + 0.5f);
        const float *sample = gb_sample(&g_windup_ease_clip, tick);
        return sample[0];
    }
    return progress01 * progress01 * (3.0f - 2.0f * progress01);
}

/* abraham_windup_squish: golden-ratio two-phase envelope described above, both phases sampled
 * through the same real baked ease curve windup_ease_sample already provides (so both the
 * rotation and the squish genuinely come from the one GOLDENBAND asset, not two different math
 * shapes) -- returns 1.0 (neutral, no squish) outside the windup window. */
static float abraham_windup_squish(float age_ms) {
    if (age_ms < 0.0f || age_ms > (float)ARENA_ABRAHAM_FIREBALL_WINDUP_MS) return 1.0f;
    if (age_ms <= ABRAHAM_WINDUP_SQUISH_DOWN_MS) {
        float t = ABRAHAM_WINDUP_SQUISH_DOWN_MS > 0.0f ? age_ms / ABRAHAM_WINDUP_SQUISH_DOWN_MS : 1.0f;
        float eased = windup_ease_sample(t);
        return 1.0f + (ABRAHAM_WINDUP_SQUISH_BOTTOM - 1.0f) * eased; /* 1.0 -> 0.2 */
    }
    float t = ABRAHAM_WINDUP_SQUISH_UP_MS > 0.0f ? (age_ms - ABRAHAM_WINDUP_SQUISH_DOWN_MS) / ABRAHAM_WINDUP_SQUISH_UP_MS : 1.0f;
    float eased = windup_ease_sample(t);
    return ABRAHAM_WINDUP_SQUISH_BOTTOM + (1.0f - ABRAHAM_WINDUP_SQUISH_BOTTOM) * eased; /* 0.2 -> 1.0, the flick */
}

/* hero_facing_rad/prev_hero_x/prev_hero_z (S170-171, founder: "heroes and
 * creeps should rotate to show what direction they are facing currently
 * they just float around there is no front of the model"): facing is
 * derived purely from observed motion -- how far a hero's own position
 * moved since last frame -- rather than needing target_x/target_z wired
 * over the wire (net_mode's ArenaHeroSnapshot never carried a remote
 * hero's move destination, only its current x/z, and adding that would be
 * a wire-protocol change this doesn't need). Persists the last known
 * facing when a hero is stationary (fighting in place, dead-stopped at its
 * target) rather than snapping to some default -- a hero that just
 * stopped should still visibly be looking at whatever it was walking
 * toward, not spinning back to face +Z. */
static float hero_facing_rad[ARENA_MAX_HEROES];
static float prev_hero_facing_x[ARENA_MAX_HEROES];
static float prev_hero_facing_z[ARENA_MAX_HEROES];
static int prev_hero_facing_valid[ARENA_MAX_HEROES];
#define ARENA_FACING_MOVE_EPSILON 0.01f /* ignore sub-pixel jitter, only turn to face real movement */

/* Same facing-from-motion idiom as heroes above, applied to node-guardian/lane
 * creeps too (S170-171: "heroes AND creeps should rotate"). Both creep
 * pools are entirely client-computed already (node-guardian creeps march now,
 * S170-161; lane creeps always have) -- no wire changes needed, same
 * "derive from observed position deltas" trick, just indexed by creep
 * slot instead of hero owner. */
static float creep_facing_rad[ARENA_MAX_CREEPS];
static float prev_creep_facing_x[ARENA_MAX_CREEPS];
static float prev_creep_facing_z[ARENA_MAX_CREEPS];
static int prev_creep_facing_valid[ARENA_MAX_CREEPS];

static float lane_creep_facing_rad[ARENA_MAX_LANE_CREEPS];
static float prev_lane_creep_facing_x[ARENA_MAX_LANE_CREEPS];
static float prev_lane_creep_facing_z[ARENA_MAX_LANE_CREEPS];
static int prev_lane_creep_facing_valid[ARENA_MAX_LANE_CREEPS];

/* update_facing_from_motion: shared helper -- if the entity moved more
 * than ARENA_FACING_MOVE_EPSILON since the position stored in *prev_x/*prev_z,
 * updates *facing to the new movement direction; otherwise leaves *facing
 * untouched (holds the last real heading through a stop, doesn't snap to
 * a default). Always refreshes *prev_x/*prev_z to the current position for
 * next frame's comparison. */
static void update_facing_from_motion(float cur_x, float cur_z, float *prev_x, float *prev_z,
                                       int *valid, float *facing) {
    if (*valid) {
        float mdx = cur_x - *prev_x;
        float mdz = cur_z - *prev_z;
        if (mdx * mdx + mdz * mdz > ARENA_FACING_MOVE_EPSILON * ARENA_FACING_MOVE_EPSILON) {
            *facing = atan2f(mdx, mdz);
        }
    }
    *prev_x = cur_x;
    *prev_z = cur_z;
    *valid = 1;
}

static void trigger_squish(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    squish_age_ms[owner] = 0.0f;
}

/* compute_squish: a decaying cosine -- starts squashed (short, wide), bounces past
 * neutral into a slight stretch, settles back to 1.0. Classic squash-and-stretch
 * bounce-back, cheap to compute, no physics simulation needed for something this
 * short-lived and purely cosmetic. */
static float compute_squish(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 1.0f;
    float t = squish_age_ms[owner];
    if (t < 0.0f || t >= SQUISH_ANIM_MS) return 1.0f;
    float amplitude = 0.32f;
    float decay = expf(-t / (SQUISH_ANIM_MS * 0.35f));
    float wobble = cosf(t / SQUISH_ANIM_MS * 3.14159265f * 2.4f);
    return 1.0f - amplitude * decay * wobble;
}

/* Tree passive (2026-08-25, founder real-time: "the tree he attacks never does or anything have
 * it jiggle animate extra squishy"): same decaying-cosine squash-and-stretch bounce as
 * trigger_squish/compute_squish just above, array-indexed by obstacle instead of hero owner --
 * trees are static world objects, not spawned/despawned entities, so a plain
 * ARENA_OBSTACLE_COUNT-sized array (like squish_age_ms's own ARENA_MAX_HEROES sizing) is the
 * simplest fit, no pooling needed. Triggered from net_poll_snapshots the instant a snapshot shows
 * a tree's hp decreased (see that call site's own doc comment) -- purely cosmetic, the real hp
 * change already happened server-side by the time this fires. */
static float tree_squish_age_ms[ARENA_OBSTACLE_COUNT];

static void trigger_tree_squish(int obstacle_index) {
    if (obstacle_index < 0 || obstacle_index >= ARENA_OBSTACLE_COUNT) return;
    tree_squish_age_ms[obstacle_index] = 0.0f;
}

static float compute_tree_squish(int obstacle_index) {
    if (obstacle_index < 0 || obstacle_index >= ARENA_OBSTACLE_COUNT) return 1.0f;
    float t = tree_squish_age_ms[obstacle_index];
    if (t < 0.0f || t >= SQUISH_ANIM_MS) return 1.0f;
    float amplitude = 0.32f;
    float decay = expf(-t / (SQUISH_ANIM_MS * 0.35f));
    float wobble = cosf(t / SQUISH_ANIM_MS * 3.14159265f * 2.4f);
    return 1.0f - amplitude * decay * wobble;
}

/* ---------------- spell flashes (S170-124, "add particle effects to
 * spells") ---------------- */
/* Unlike auto-attacks (S170-122, HP-delta is a decent-enough proxy), a real
 * "a spell was just cast" signal doesn't exist in HP alone -- several kits
 * have no damage component on some slots (Frog's Q rewinds position/HP with
 * no damage at all; Unicorn's W is a pure toggle). Carried over the wire for
 * real instead: ArenaHeroSnapshot.cast_flash_slot (0/1/2/3 = none/Q/W/R),
 * a one-tick signal the server sets the instant a cast clears its gate and
 * clears again right after broadcasting it. Slot gets its own color/size so
 * Q/W/R read as visually distinct tiers, same convention as any real MOBA
 * (bigger, brighter effect for the ultimate). */
#define MAX_SPELL_FLASHES (ARENA_MAX_HEROES * 2)
#define SPELL_FLASH_LIFETIME_MS 260.0f
typedef struct { float x, z, age_ms; int slot; int hero_id; int active; } SpellFlash;
static SpellFlash spell_flashes[MAX_SPELL_FLASHES];

/* hero_flash_color (founder: "ensure each spell is unique show different
 * color cast circles"): before this, every cast's color came purely from
 * its Q/W/R slot (cyan/violet/gold, S170-124) -- correct for "which tier of
 * ability" but every hero's Q looked identical to every other hero's Q,
 * with 26 heroes now on the roster that's not "unique" at all. Golden-angle
 * HSV hue rotation (hue = hero_id * 137.508 deg mod 360, the same
 * technique used for generating N maximally-distinct sequential colors
 * without hand-picking each one) gives every hero_id its own real, distinct
 * hue -- deterministic, needs no per-hero table to maintain as the roster
 * keeps growing. Slot still controls SIZE in the render loop below (Q
 * small, W bigger, R biggest) -- that "which tier" legibility stays, this
 * only replaces what controlled color. */
static void hero_flash_color(int hero_id, float *r, float *g, float *b) {
    float hue = fmodf((float)hero_id * 137.508f, 360.0f);
    float s = 0.75f, v = 1.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rr, gg, bb;
    if (hue < 60)       { rr = c; gg = x; bb = 0; }
    else if (hue < 120)  { rr = x; gg = c; bb = 0; }
    else if (hue < 180)  { rr = 0; gg = c; bb = x; }
    else if (hue < 240)  { rr = 0; gg = x; bb = c; }
    else if (hue < 300)  { rr = x; gg = 0; bb = c; }
    else                 { rr = c; gg = 0; bb = x; }
    *r = rr + m; *g = gg + m; *b = bb + m;
}

static void spawn_spell_flash(float x, float z, int slot, int hero_id) {
    for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
        if (!spell_flashes[i].active) {
            spell_flashes[i].active = 1;
            spell_flashes[i].x = x;
            spell_flashes[i].z = z;
            spell_flashes[i].slot = slot;
            spell_flashes[i].hero_id = hero_id;
            spell_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- ability recast tiles (S170-127, "add the ability frame
 * cooldown timer tiles from shankpit og engine as recast time affordances"
 * -> "make it like overwatch recast frames for q w e") ---------------- */
/* Peak-cooldown tracking for the local player's own Q/W/E, one float each --
 * see the call site's own comment for why this exists (no per-hero max-
 * cooldown table to compute a wipe fraction against otherwise). */
static float q_cooldown_peak_ms = 0.0f;
static float w_cooldown_peak_ms = 0.0f;
static float r_cooldown_peak_ms = 0.0f;
static float blink_cooldown_peak_ms = 0.0f; /* S170-205 */
static float donkey_glide_cooldown_peak_ms = 0.0f; /* S170-206 */

/* draw_ability_tile: one Overwatch-style square ability icon -- bordered
 * tile, a radial dark wedge (GL_TRIANGLE_FAN from the tile's center)
 * sweeping clockwise from 12 o'clock that shrinks as cooldown counts down
 * (SHANKPIT's draw_ability_one_tile() only ever showed a flat color swap +
 * a number, no progress wipe -- REDGARDEN's 19-hero, 3-slot roster spans
 * cooldowns from ~2s to 26s+, where "how much is left" matters more than
 * SHANKPIT's single fixed-cooldown blade dash), a big centered countdown
 * number while on cooldown, and a keybind label below. `active` lights the
 * tile a bright toggle-green regardless of cooldown state, matching the
 * existing "W is ON" HUD convention this replaces. `peak_ms` is the
 * caller's own persistent float -- updated here, not reset by this
 * function, so it survives across frames.
 *
 * S170-137: `mana_blocked` (mp below this slot's flat cost) is a second,
 * independent way a ready-looking (cooldown_ms == 0) ability can still be
 * uncastable -- the mana layer (S170-132) already lets a cast whiff for
 * lack of mp with the cooldown untouched, so a tile that only ever read
 * cooldown_ms would keep telling the player an ability is ready right up
 * until they try it and nothing happens. Shares the same dimmed
 * background/border treatment as on_cooldown (one "not actually castable"
 * visual language), but skips the radial wipe and countdown number --
 * there's no fixed timer to animate, just "wait for regen" -- printing
 * "MP" in their place instead so the reason reads differently from a real
 * cooldown. */
static void draw_ability_tile(float x, float y, float size, int cooldown_ms, float *peak_ms,
                               int active, int mana_blocked, const char *keybind, const char *ability_name,
                               float base_r, float base_g, float base_b) {
    if (cooldown_ms > 0) {
        if ((float)cooldown_ms > *peak_ms) *peak_ms = (float)cooldown_ms;
    } else {
        *peak_ms = 0.0f;
    }
    int on_cooldown = cooldown_ms > 0;
    int not_ready = on_cooldown || mana_blocked;
    float frac_remaining = (on_cooldown && *peak_ms > 0.0f) ? (float)cooldown_ms / *peak_ms : 0.0f;
    if (frac_remaining > 1.0f) frac_remaining = 1.0f;

    float bg_r = active ? 0.15f : (not_ready ? 0.10f : 0.08f);
    float bg_g = active ? 0.45f : (not_ready ? 0.10f : 0.08f);
    float bg_b = active ? 0.20f : (not_ready ? 0.12f : 0.10f);
    glColor4f(bg_r, bg_g, bg_b, 0.85f);
    glRectf(x, y, x + size, y + size);

    /* Border: the ability's own base color at full brightness when ready
       or active, dimmed to near-gray while on cooldown or mana-blocked --
       same "ready pops, cooldown recedes" legibility Overwatch's own icon
       border uses. */
    float border_scale = (not_ready && !active) ? 0.35f : 1.0f;
    glColor4f(base_r * border_scale + (1.0f - border_scale) * 0.3f,
              base_g * border_scale + (1.0f - border_scale) * 0.3f,
              base_b * border_scale + (1.0f - border_scale) * 0.3f, 0.95f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + size, y);
    glVertex2f(x + size, y + size); glVertex2f(x, y + size);
    glEnd();
    glLineWidth(1.0f);

    /* Radial cooldown wipe: a dark wedge from the tile's center, starting
       at 12 o'clock, sweeping clockwise for frac_remaining * 360 degrees --
       shrinks toward nothing as the ability approaches ready, exactly the
       "watch the pie empty" affordance real ability HUDs use. */
    if (on_cooldown && frac_remaining > 0.0f) {
        float cx = x + size / 2.0f, cy = y + size / 2.0f;
        float radius = size * 0.75f; /* overshoots the tile corners so the wedge always fully covers it */
        int segments = 24;
        int sweep_segments = (int)(segments * frac_remaining);
        if (sweep_segments < 1) sweep_segments = 1;
        glColor4f(0.0f, 0.0f, 0.0f, 0.72f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int s = 0; s <= sweep_segments; s++) {
            float t = (float)s / (float)segments;
            float angle = -3.14159265f / 2.0f + t * 2.0f * 3.14159265f; /* start at 12 o'clock, sweep clockwise */
            glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
        }
        glEnd();
    }

    if (on_cooldown) {
        char buf[8];
        int seconds = (int)ceilf((float)cooldown_ms / 1000.0f);
        if (seconds < 1) seconds = 1;
        snprintf(buf, sizeof(buf), "%d", seconds);
        float text_size = size * 0.05f;
        float approx_w = (float)strlen(buf) * text_size * 3.8f;
        glColor3f(1.0f, 0.95f, 0.95f);
        draw_string(buf, x + (size - approx_w) / 2.0f, y + size * 0.4f, text_size);
    } else if (mana_blocked) {
        float text_size = size * 0.05f;
        float approx_w = 2.0f * text_size * 3.8f; /* "MP" is always 2 chars */
        glColor3f(0.55f, 0.75f, 1.0f);
        draw_string("MP", x + (size - approx_w) / 2.0f, y + size * 0.4f, text_size);
    }

    glColor3f(0.92f, 0.96f, 1.0f);
    draw_string(keybind, x + size / 2.0f - 3.0f, y - 12.0f, 8.0f);
    glColor3f(0.75f, 0.8f, 0.85f);
    draw_string(ability_name, x, y - 24.0f, 6.0f);
}

/* ---------------- camera ---------------- */
static float cam_yaw = 45.0f, cam_pitch = 40.0f, cam_dist = 16.0f;
/* cam_locked (NORTHSTAR §15.1, founder: "specdd unlockable and lockable camera and fog of
 * war"): the orbit pivot already hard-follows arena_state.heroes[my_owner] every frame
 * unconditionally (focus_x/focus_z below), so "locked" only ever meant freezing the
 * yaw/pitch orbit angle itself -- the one way a player can currently look away from their
 * own hero. Zoom (cam_dist, mouse wheel) stays free even while locked, per §15.1's own
 * resolved open question ("most real MOBAs lock rotation/pan but leave zoom free"). Starts
 * unlocked (today's behavior, unchanged) -- no settings-persistence layer exists to
 * remember a preference across matches. */
static int cam_locked = 0;

static void camera_basis(float focus_x, float focus_z,
                          float *eye_x, float *eye_y, float *eye_z,
                          float *fwd_x, float *fwd_y, float *fwd_z,
                          float *right_x, float *right_y, float *right_z,
                          float *up_x, float *up_y, float *up_z) {
    float yaw = cam_yaw * (float)M_PI / 180.0f;
    float pitch = cam_pitch * (float)M_PI / 180.0f;
    *eye_x = focus_x + cam_dist * cosf(pitch) * sinf(yaw);
    *eye_y = cam_dist * sinf(pitch);
    *eye_z = focus_z + cam_dist * cosf(pitch) * cosf(yaw);
    float fx = focus_x - *eye_x, fy = -*eye_y, fz = focus_z - *eye_z;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    *fwd_x = fx / flen; *fwd_y = fy / flen; *fwd_z = fz / flen;
    float upx = 0, upy = 1, upz = 0;
    float rx = *fwd_y * upz - *fwd_z * upy;
    float ry = *fwd_z * upx - *fwd_x * upz;
    float rz = *fwd_x * upy - *fwd_y * upx;
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    *right_x = rx / rlen; *right_y = ry / rlen; *right_z = rz / rlen;
    *up_x = *right_y * *fwd_z - *right_z * *fwd_y;
    *up_y = *right_z * *fwd_x - *right_x * *fwd_z;
    *up_z = *right_x * *fwd_y - *right_y * *fwd_x;
}

/* Intersects the mouse ray with the y=0 ground plane. Returns 1 on hit. */
static int screen_to_ground(int mx, int my, int w, int h, float fov_deg,
                             float focus_x, float focus_z, float *out_x, float *out_z) {
    float eye_x, eye_y, eye_z, fx, fy, fz, rx, ry, rz, ux, uy, uz;
    camera_basis(focus_x, focus_z, &eye_x, &eye_y, &eye_z, &fx, &fy, &fz, &rx, &ry, &rz, &ux, &uy, &uz);
    float ndc_x = (2.0f * mx / w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / h);
    float aspect = (float)w / (float)h;
    float tan_fov = tanf(fov_deg * 0.5f * (float)M_PI / 180.0f);
    float dx = fx + ndc_x * tan_fov * aspect * rx + ndc_y * tan_fov * ux;
    float dy = fy + ndc_x * tan_fov * aspect * ry + ndc_y * tan_fov * uy;
    float dz = fz + ndc_x * tan_fov * aspect * rz + ndc_y * tan_fov * uz;
    if (fabsf(dy) < 1e-5f) return 0;
    float t = -eye_y / dy;
    if (t <= 0) return 0;
    *out_x = eye_x + t * dx;
    *out_z = eye_z + t * dz;
    return 1;
}

/* world_to_screen: inverse of screen_to_ground's job -- projects a 3D world point through
 * the same view-projection matrix the 3D pass draws with, into the 2D HUD's bottom-up pixel
 * space (S170-89, per-hero floating health bars). Mat4 is column-major (mat4.h's own
 * mat4_multiply indexes m[col*4+row]), so the manual point transform below follows the same
 * convention. Returns 0 if the point is behind the camera (w <= 0), meaningless to project. */
static int world_to_screen(const Mat4 *vp, float wx, float wy, float wz, int win_w, int win_h,
                            float *sx, float *sy) {
    float px[4] = {wx, wy, wz, 1.0f};
    float clip[4];
    for (int row = 0; row < 4; row++) {
        float sum = 0.0f;
        for (int col = 0; col < 4; col++) sum += vp->m[col * 4 + row] * px[col];
        clip[row] = sum;
    }
    if (clip[3] <= 0.01f) return 0;
    float ndc_x = clip[0] / clip[3];
    float ndc_y = clip[1] / clip[3];
    *sx = (ndc_x * 0.5f + 0.5f) * win_w;
    *sy = (ndc_y * 0.5f + 0.5f) * win_h;
    return 1;
}

/* ---------------- audio (S170-92, "add little musical sound effects... for
 * legibility via midi") ---------------- */
/* Real scope call, not guessed: raw SDL2 core audio (SDL_OpenAudioDevice +
 * SDL_QueueAudio), no SDL2_mixer. The backlog item's own open questions --
 * whether a new mixer dependency is acceptable, what the Windows-bundle
 * story is for a second DLL alongside SDL2.dll -- both dissolve if nothing
 * new gets linked at all: SDL2 core already has an audio subsystem, already
 * ships in every build (Linux and the mingw cross-compile alike), so short
 * procedurally-synthesized tones need zero new toolchain/CI/bundling work.
 * "Via midi" read as "short, distinct musical notes per event," not literal
 * .mid file playback -- a simple sine tone per cue is the honest match for
 * that intent at this scope ("little," per the founder's own word).
 * Graceful degradation: if no audio device is available (this box is
 * headless; a real player's box might also have no sound hardware, or it's
 * muted), audio_dev stays 0 and every play_tone() call is a silent no-op --
 * never a crash. */
static SDL_AudioDeviceID audio_dev = 0;

static void audio_init(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "[arena client] no audio device available (%s) -- sound effects disabled\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audio_dev, 0);
}

/* play_tone: synthesizes duration_ms of a sine wave at freq_hz and queues it
 * for immediate playback. Linear fade-out over the last ~15ms avoids the
 * audible click a hard-cut sine wave would otherwise produce.
 *
 * `volume` is each call site's own relative mix level for that specific cue; master_volume
 * (3424324/343543, the settings-pane volume slider) scales on top of that here, the one real
 * place a player-controlled master level needs to apply, rather than threading it through every
 * individual play_tone call site. master_volume == 0.0 makes every cue silent without touching
 * audio_dev at all -- same "no crash, just no sound" degradation this function already documents
 * for a missing audio device. */
static void play_tone(float freq_hz, float duration_ms, float volume) {
    if (audio_dev == 0) return;
    volume *= master_volume;
    if (volume <= 0.0f) return;
    int sample_rate = 44100;
    int n = (int)(sample_rate * duration_ms / 1000.0f);
    if (n <= 0) return;
    int16_t *buf = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    if (!buf) return;
    int fade_samples = sample_rate * 15 / 1000;
    if (fade_samples > n) fade_samples = n;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)sample_rate;
        float env = 1.0f;
        if (i > n - fade_samples) env = (float)(n - i) / (float)fade_samples;
        float sample = sinf(2.0f * 3.14159265f * freq_hz * t) * volume * env;
        buf[i] = (int16_t)(sample * 32000.0f);
    }
    SDL_QueueAudio(audio_dev, buf, (Uint32)n * sizeof(int16_t));
    free(buf);
}

/* play_cast_tone: one distinct note per ability slot -- an ascending triad
   (Q/W/R -> A4/C#5/E5), same "which slot just fired" legibility the spell
   flash's cyan/violet/gold color tiers already give visually, mirrored in
   sound so it reads even without looking at the cast location. */
static void play_cast_tone(int slot) {
    switch (slot) {
        case 1: play_tone(440.0f, 90.0f, 0.3f); break;  /* Q: A4 */
        case 2: play_tone(554.0f, 110.0f, 0.3f); break; /* W: C#5 */
        default: play_tone(659.0f, 140.0f, 0.32f); break; /* R: E5, longest and loudest -- the ultimate */
    }
}

int main(int argc, char *argv[]) {
    /* No srand() call existed anywhere in this file before -- mint_ticket_fallback's own
       rand()-based nonce (used only when IDUNA isn't reachable) was silently using the default
       seed=1 sequence, identical every single launch, a real if minor pre-existing weakness. */
    srand((unsigned int)time(NULL));
    /* squish_age_ms[] zero-initializes with the rest of static storage, but 0.0f reads as
       "animation just started" (compute_squish's own neutral sentinel is anything >=
       SQUISH_ANIM_MS) -- without this every hero would appear squashed for one frame the instant
       the game launches, before any real trigger fires. Push every slot past the animation
       window so compute_squish() reads neutral (1.0f) until trigger_squish() actually resets it. */
    for (int squish_init_i = 0; squish_init_i < ARENA_MAX_HEROES; squish_init_i++) {
        squish_age_ms[squish_init_i] = SQUISH_ANIM_MS + 1.0f;
    }
    /* Same zero-init pitfall as squish_age_ms just above, same fix -- without this every tree
       obstacle would appear squashed for one frame on launch (compute_tree_squish, 2026-08-25). */
    for (int tree_squish_init_i = 0; tree_squish_init_i < ARENA_OBSTACLE_COUNT; tree_squish_init_i++) {
        tree_squish_age_ms[tree_squish_init_i] = SQUISH_ANIM_MS + 1.0f;
    }
    /* Observer mode (NORTHSTAR §12 Phase C, EMILY/BACKLOG.md S170-30):
     * `red_garden_arena --observe var/matches/arena-<ts>.jsonl` plays back
     * a logged match through this exact same renderer instead of driving
     * ArenaState from live input/bot AI -- "same draw code, no second
     * rendering path" per the founder's requirement. */
    ArenaReplay replay;
    int observing = 0;
    uint32_t observe_elapsed_ms = 0;
    const char *connect_host = NULL;
    int connect_port = 7200;
    const char *queue_host = NULL;
    int queue_port = 7778; /* apps/matchmaker's documented arena listen-port */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--observe") == 0 && i + 1 < argc) {
            if (!arena_replay_load(argv[i + 1], &replay)) {
                fprintf(stderr, "--observe: could not open %s\n", argv[i + 1]);
                return 1;
            }
            observing = 1;
            printf("OBSERVER MODE: replaying %s (%d snapshots)\n", argv[i + 1], replay.count);
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            /* Real networked PvP (NORTHSTAR §13): connect to a real
               apps/arena_server instead of running the local sim. */
            connect_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            connect_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--queue") == 0 && i + 1 < argc) {
            /* Join whatever match the persistent bot pool is currently
               matchmaking into, instead of connecting to an already-known
               server (S170-44: "moba player can join bot pool games"). */
            queue_host = argv[++i];
        } else if (strcmp(argv[i], "--matchmaker-port") == 0 && i + 1 < argc) {
            queue_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ticket") == 0 && i + 1 < argc) {
            g_supplied_ticket_hex = argv[++i];
        }
    }
#ifdef _WIN32
    /* Sockets need WSAStartup before any socket() call on Windows -- only
       needed if this run actually uses the network (--connect/--queue),
       same "only pay for what you use" reasoning as everywhere else in
       this file. Harmless to call unconditionally, but scoped here to
       keep it next to what actually needs it. */
    if (connect_host || queue_host) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    if (connect_host) {
        net_mode = 1;
        load_iduna_agent_config();
        if (!net_connect(connect_host, connect_port)) {
            fprintf(stderr, "Failed to connect to arena server at %s:%d\n", connect_host, connect_port);
            return 1;
        }
    } else if (queue_host) {
        net_mode = 1;
        load_iduna_agent_config();
        if (!net_find_and_connect(queue_host, queue_port)) {
            fprintf(stderr, "Failed to join a match via matchmaker at %s:%d\n", queue_host, queue_port);
            return 1;
        }
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    audio_init();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280, win_h = 720;
    SDL_Window *win = SDL_CreateWindow(
        observing ? "KNIGHTS OF THE VOID — OBSERVER MODE" :
        (net_mode ? "KNIGHTS OF THE VOID (networked PvP)" : "KNIGHTS OF THE VOID (local)"),
        100, 100, win_w, win_h, SDL_WINDOW_OPENGL);
    if (!win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return 1; }
    /* Hover cursor indicators (S170-69, founder northstar: "nice cursor indicators for hover
       over enemy vers aly etc"). The color-coded YOU/ALLY/ENEMY bracket+label below already
       covers "aly etc"; this is the literal cursor-shape half that was still missing -- a real
       OS cursor swap, same crosshair-over-a-valid-target convention real MOBAs use, not just an
       in-HUD label. SDL_CreateSystemCursor never fails in practice on a real display driver, but
       degrades to a NULL cursor (SDL_SetCursor silently no-ops on NULL) rather than crashing if
       it somehow does -- this is a pure visual affordance, not load-bearing for gameplay. */
    SDL_Cursor *cursor_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor *cursor_enemy = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

    if (!load_gl_functions()) {
        fprintf(stderr, "Failed to load required GL 3.x functions via SDL_GL_GetProcAddress\n");
        return 1;
    }

    GLuint prog = link_program(VS_SRC, FS_SRC);
    g_main_prog = prog;
    GLint loc_mvp = glGetUniformLocation_(prog, "uMVP");
    GLint loc_model = glGetUniformLocation_(prog, "uModel");
    GLint loc_color = glGetUniformLocation_(prog, "uColor");
    GLint loc_light = glGetUniformLocation_(prog, "uLightDir");

    /* S180-09 cel-shading outline pass -- see VS_OUTLINE_SRC/FS_OUTLINE_SRC's
     * own doc comment. g_outline_prog stays 0 (falsy) if this link somehow
     * fails, which draw_hero_box_facing checks before using it -- degrades
     * to the old no-outline draw rather than crashing. */
    g_outline_prog = link_program(VS_OUTLINE_SRC, FS_OUTLINE_SRC);
    g_outline_loc_mvp = glGetUniformLocation_(g_outline_prog, "uOutlineMVP");
    g_outline_loc_width = glGetUniformLocation_(g_outline_prog, "uOutlineWidth");

    build_ring_mesh(0.8f, 1.0f);
    build_disc_mesh(); /* S170-200 */
    Mesh cube_mesh = upload_mesh(CUBE_VERTS, CUBE_VERT_COUNT);
    Mesh plane_mesh = upload_mesh(PLANE_VERTS, PLANE_VERT_COUNT);
    Mesh ring_mesh = upload_mesh(RING_VERTS, RING_VERT_COUNT);
    Mesh disc_mesh = upload_mesh(DISC_VERTS, DISC_VERT_COUNT); /* S170-200 */

    /* S144-06: real .gband motion data driving Tyler's silhouette instead of
     * a plain box, via GOLDENBAND's sampler (packages/goldenband). Path is
     * relative to CWD -- this binary, like every other asset-loading path in
     * this repo, is expected to run from the repo root. Missing/corrupt
     * assets fail soft: gband_rig_ready() gates the call site below back to
     * the plain box, never a crash or a missing hero. */
    gband_rig_init("assets/goldenband");

    /* S144-07: real vertex-weighted skinned Tyler, founder-modeled in
     * Blender (GOLDENBAND/incoming/TYLER-rigged3.blend -> tyler_body.gskel/
     * .gmesh via export_gband_rig.py) -- see gband_mesh_rig.h. Falls back
     * to gband_rig.c's box-rig (and that falls back to the plain box) if
     * these assets are ever missing, same layered-safety pattern as S144-06.
     * synthetic_body.gskel/.gmesh (the original proof-of-concept mesh) stay
     * in assets/goldenband/ for regression-checking the skinning pipeline
     * itself, but are no longer what actually renders for Tyler. */
    gband_mesh_rig_init("assets/goldenband", "tyler_body");
    gband_mesh_dynamic_init();

    /* S202-34: Abraham's Fireball windup animation -- a real baked GOLDENBAND scalar ease
     * curve (assets/anim/rotation_ease.gband, 16 ticks @ 40Hz, tools/gbtool's own bake-ease
     * subcommand), not a skinned pose. gb_init's own fail-soft return (0 on any failure) is
     * respected here the same way gband_rig_init's callers already do -- windup_ease_sample
     * falls back to an identical hand-computed smoothstep if this load fails, so a missing/
     * corrupt asset degrades the animation's exact source, never crashes the client. */
    g_windup_ease_loaded = gb_init("assets/anim/rotation_ease.gband", &g_windup_ease_clip);

    glEnable(GL_DEPTH_TEST);

    arena_init();
    /* In net_mode, apps/arena_server is authoritative and writes its own
       match log -- a local log here would be redundant and would wrongly
       claim "local_player"/"local_bot" identities for a real match. */
    if (!observing && !net_mode) arena_log_open();

    int dragging_cam = 0;
    int last_mx = 0, last_my = 0;
    int running = 1;
    int win_logged = 0;
    uint32_t last_tick = SDL_GetTicks();

    while (running) {
        uint32_t now = SDL_GetTicks();
        uint32_t dt = now - last_tick;
        last_tick = now;
        if (observing) {
            observe_elapsed_ms += dt;
        } else {
            arena_log_elapsed_ms += dt;
        }

        /* Shop proximity auto-open/close (S170-231, founder: "have it pop the shop
           window up when you get close to the shop enough to buy"): edge-triggered
           against the exact same ARENA_SHOP_RADIUS arena_shop_buy itself enforces
           server-side around the player's OWN team's shop (arena_shop_position), so
           "the panel is showing" and "you're actually close enough to buy" always
           agree. Edge-triggered on shop_was_in_range rather than "open whenever in
           range every frame" so it doesn't fight the manual B toggle -- closing the
           panel with B while standing in range stays closed until you actually leave
           and come back, and a manual B-open isn't stomped shut again next frame. */
        if (!observing && my_owner >= 0 && my_owner < ARENA_MAX_HEROES && arena_state.heroes[my_owner].alive) {
            ArenaHero *shop_me = &arena_state.heroes[my_owner];
            float shx, shz;
            arena_shop_position(shop_me->team, &shx, &shz);
            float sdx = shop_me->x - shx, sdz = shop_me->z - shz;
            int shop_in_range = (sdx * sdx + sdz * sdz) <= (ARENA_SHOP_RADIUS * ARENA_SHOP_RADIUS);
            if (shop_in_range && !shop_was_in_range) shop_open = 1;
            else if (!shop_in_range && shop_was_in_range) shop_open = 0;
            shop_was_in_range = shop_in_range;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = e.window.data1; win_h = e.window.data2;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 1; last_mx = e.button.x; last_my = e.button.y;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 0;
            }
            if (e.type == SDL_MOUSEMOTION && dragging_cam) {
                int dx = e.motion.x - last_mx, dy = e.motion.y - last_my;
                last_mx = e.motion.x; last_my = e.motion.y;
                /* S170-193-adjacent, NORTHSTAR §15.1: locked mode makes right-drag rotation a
                   no-op (mouse deltas still consumed above so drag tracking doesn't jump the
                   instant unlock happens) -- zoom below stays ungated regardless of lock state. */
                if (!cam_locked) {
                    cam_yaw += dx * 0.3f;
                    cam_pitch += dy * 0.3f;
                    if (cam_pitch < 10.0f) cam_pitch = 10.0f;
                    if (cam_pitch > 80.0f) cam_pitch = 80.0f;
                }
            }
            if (e.type == SDL_MOUSEWHEEL) {
                cam_dist -= e.wheel.y * 1.0f;
                if (cam_dist < 4.0f) cam_dist = 4.0f;
                if (cam_dist > 30.0f) cam_dist = 30.0f;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_c) {
                cam_locked = !cam_locked; /* NORTHSTAR §15.1, same "works in any mode" precedent as F11/H/B below */
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                show_apm = !show_apm; /* S170-71: works in any mode, not gated on net_mode/observing */
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F9) {
                force_box_rig = !force_box_rig; /* S144-07: dev-only A/B toggle -- forces Tyler
                    back to the box-rig even though the real skinned mesh is available, for
                    comparing the two live. Repurposed from the pre-real-asset synthetic proof
                    rig toggle, same key. Same "works in any mode" precedent as F11/H/B above. */
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F10) {
                /* Real, client-side debug tool (2026-08-26, founder: "AT ONE POINT I GOT
                   STUCK SIDEWAYS AND HE WAS LIKE TRYING TO ROTATE TO RUN WHERE I WANTED BUT
                   KIND ACOULDNT" -> "MAYBE WE MAKE A RESET CHARACER BUTTON THAT RESETTTS THE
                   ROTATION AND IF YOU PRESS IT IT LOGS IF IT ACTUALLY DID ANYTHING OR NOT" ->
                   "SO IF WE FEEL LIKE ITS WORKING IIT CAN BE INVVESTIGATED WITH THE DATA").
                   Forces the local player's own client-side hero_facing_rad (see that array's
                   own doc comment -- purely visual, movement-derived interpolation, no wire
                   protocol involved) back to a known value and logs the before/after so a real
                   session transcript can show whether this ever actually needed to fire (i.e.
                   whether facing really was stuck at some stale value) the next time the bug
                   recurs -- same "works in any mode" precedent as F9/F11/C/H/B above. */
                if (my_owner >= 0 && my_owner < ARENA_MAX_HEROES) {
                    float before = hero_facing_rad[my_owner];
                    hero_facing_rad[my_owner] = 0.0f;
                    prev_hero_facing_valid[my_owner] = 0; /* forces a fresh baseline next frame, not a stale delta against the old position */
                    printf("[reset-rotation] owner=%d before=%.4f after=%.4f changed=%s\n",
                           my_owner, before, hero_facing_rad[my_owner],
                           (before != 0.0f) ? "yes" : "no");
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_h) {
                show_ability_help = !show_ability_help; /* same "works in any mode" precedent as F11 above */
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                show_settings_pane = !show_settings_pane; /* 3424324, same "works in any mode" precedent as F11/H/B above */
                if (!show_settings_pane) settings_volume_dragging = 0; /* closing mid-drag shouldn't leave a stuck drag state for next open */
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_b) {
                shop_open = !shop_open; /* S170-175, same "works in any mode" precedent as F11/H above -- arena_shop_buy/sell themselves reject a purchase made out of range, so toggling far from a shop is harmless, not broken */
            }
            /* Quick-buy + page nav (S170-175, extended S170-231 founder: "navigate pages
               with shift 1 2 3"): plain 1-9 buys the corresponding item on the CURRENT
               page the instant it's pressed, no confirm step -- the keybind-path half of
               NORTHSTAR §2's "both keybind and click paths must resolve instantly"
               constraint, mirroring real MOBA quick-buy hotkeys. Shift+1/2/3 jumps
               straight to that page instead of buying -- reuses the exact keys the
               founder already associates with "the shop," rather than inventing a
               separate pair of prev/next keys. Only live while the panel is open, same
               "the affordance you're looking at is the one the key acts on" rule the QWE
               ability keys already follow. */
            if (shop_open && !observing && e.type == SDL_KEYDOWN &&
                e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9) {
                int slot_in_page = (int)(e.key.keysym.sym - SDLK_1);
                if (e.key.keysym.mod & KMOD_SHIFT) {
                    if (slot_in_page < SHOP_PAGE_COUNT) shop_page = slot_in_page;
                } else {
                    int item_id = shop_page * SHOP_ITEMS_PER_PAGE + slot_in_page;
                    if (item_id < ARENA_ITEM_COUNT) {
                        if (net_mode) net_send_shop_buy(item_id);
                        else arena_shop_buy(my_owner, item_id);
                    }
                }
            }
            /* Shop panel clicks (S170-175): the click-path half of the same instant-resolve
               constraint above -- one click buys or sells, no confirm dialog. Hit-tests
               against the exact same geometry shop_panel_origin()/SHOP_ROW_H/SHOP_COL_W the
               render pass draws with, so a click always lands on the row it visually
               overlaps. SDL mouse Y is top-down, this HUD's ortho draw space is bottom-up --
               same flip the requeue OK-button hit-test above already uses. */
            /* S170-229, founder real-time: "clicking on item in shop to buy should not cause
               player to move" -- real bug: this block and the movement-click block below it are
               two separate, sequential `if`s with no shared state, so a click that bought (or
               sold, or simply landed on empty shop-panel space) fell straight through to the
               ordinary move-command handler too, every time. shop_click_consumed is checked by
               that later block below -- set true here whenever the shop is open AND the click
               falls anywhere inside the panel's own bounding box (not just directly on a
               buy/sell row), same rectangle the render pass itself draws
               (sp_x-10..sp_x-10+panel_w, sp_y_top-panel_h..sp_y_top+26) -- clicking blank space
               inside an open shop panel shouldn't move the player either, matching how any real
               game UI panel blocks click-through to the world underneath it. */
            int shop_click_consumed = 0;
            if (shop_open && !observing && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float bx = (float)e.button.x, by = (float)(win_h - e.button.y);
                float sp_x, sp_y_top;
                shop_panel_origin(win_w, win_h, &sp_x, &sp_y_top);
                float panel_w = SHOP_COL_W + SHOP_COL_W + 40.0f;
                float panel_h = (float)SHOP_PANEL_ROWS * SHOP_ROW_H + 40.0f;
                if (bx >= sp_x - 10.0f && bx <= sp_x - 10.0f + panel_w &&
                    by >= sp_y_top - panel_h && by <= sp_y_top + 26.0f) {
                    shop_click_consumed = 1;
                }
                int handled = 0;
                /* Page-nav buttons (S170-231, "and buttons" -- the click-path affordance
                   for the same page switch Shift+1/2/3 does by keybind): sit in the band
                   directly above the buy list, one small box per page, current page drawn
                   highlighted solid in the render pass below. Checked before the buy grid
                   since they occupy the row directly above it. */
                /* +1 tab: SHOP_BUILDS_PAGE (2026-08-25, build templates) -- one virtual page past
                   the real item pages, same page-button strip, own row content below. */
                for (int p = 0; p < SHOP_PAGE_COUNT + 1 && !handled; p++) {
                    float btn_x = sp_x + (float)p * (SHOP_PAGE_BTN_W + SHOP_PAGE_BTN_GAP);
                    float btn_top = sp_y_top;
                    float btn_bottom = sp_y_top - SHOP_PAGE_BTN_H;
                    if (bx >= btn_x && bx <= btn_x + SHOP_PAGE_BTN_W && by >= btn_bottom && by <= btn_top) {
                        shop_page = p;
                        handled = 1;
                    }
                }
                if (shop_page == SHOP_BUILDS_PAGE) {
                    /* Build templates (2026-08-25): each row auto-buys that template's items in
                       order, same real arena_shop_buy path every individual item purchase
                       already uses -- see arena_hero_apply_build_template's own doc comment. */
                    for (int row = 0; row < ARENA_BUILD_TEMPLATE_COUNT && !handled; row++) {
                        float row_top = sp_y_top - SHOP_ROW_H - (float)row * SHOP_ROW_H;
                        float row_bottom = row_top - (SHOP_ROW_H - 2.0f);
                        if (bx >= sp_x && bx <= sp_x + SHOP_COL_W - 8.0f && by >= row_bottom && by <= row_top) {
                            if (net_mode) net_send_apply_build_template(row);
                            else arena_hero_apply_build_template(my_owner, row);
                            handled = 1;
                        }
                    }
                } else {
                    for (int row = 0; row < SHOP_ITEMS_PER_PAGE && !handled; row++) {
                        int item_id = shop_page * SHOP_ITEMS_PER_PAGE + row;
                        if (item_id >= ARENA_ITEM_COUNT) break;
                        float row_top = sp_y_top - SHOP_ROW_H - (float)row * SHOP_ROW_H;
                        float row_bottom = row_top - (SHOP_ROW_H - 2.0f);
                        if (bx >= sp_x && bx <= sp_x + SHOP_COL_W - 8.0f && by >= row_bottom && by <= row_top) {
                            if (net_mode) net_send_shop_buy(item_id);
                            else arena_shop_buy(my_owner, item_id);
                            handled = 1;
                        }
                    }
                }
                if (!handled) {
                    float sell_x = sp_x + SHOP_COL_W + 20.0f;
                    for (int slot = 0; slot < ARENA_ITEM_SLOT_COUNT; slot++) {
                        float row_top = sp_y_top - (float)slot * SHOP_ROW_H;
                        float row_bottom = row_top - (SHOP_ROW_H - 2.0f);
                        if (bx >= sell_x && bx <= sell_x + SHOP_COL_W - 8.0f && by >= row_bottom && by <= row_top) {
                            if (arena_state.heroes[my_owner].equipped_item[slot] >= 0) {
                                if (net_mode) net_send_shop_sell(slot);
                                else arena_shop_sell(my_owner, (ArenaItemSlot)slot);
                            }
                            break;
                        }
                    }
                }
            }
            /* Settings pane click/drag (3424324/343543): same "compute the click hit-test
               against the exact geometry the render pass itself draws" discipline the shop
               panel above already follows (settings_slider_track is shared with the draw call
               below). settings_click_consumed mirrors shop_click_consumed's own doc comment --
               a click anywhere inside the pane (not just directly on the slider) must not also
               fall through to the movement handler. The slider itself resolves on mouse-down AND
               continues tracking on mouse-motion while held (settings_volume_dragging), the
               standard "grab and drag a slider handle" affordance -- unlike the shop's
               single-click-resolves rows, a volume slider needs continuous drag feedback, not
               just a one-shot click. */
            int settings_click_consumed = 0;
            if (show_settings_pane && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float bx = (float)e.button.x, by = (float)(win_h - e.button.y);
                float panel_x, panel_y;
                settings_panel_origin(win_w, win_h, &panel_x, &panel_y);
                if (bx >= panel_x && bx <= panel_x + SETTINGS_PANEL_W &&
                    by >= panel_y && by <= panel_y + SETTINGS_PANEL_H) {
                    settings_click_consumed = 1;
                }
                float track_x, track_y;
                settings_slider_track(win_w, win_h, &track_x, &track_y);
                /* Generous vertical grab margin (+-10px around the thin track) -- a slider this
                   thin is hard to hit exactly, same "forgiving hit-test" reasoning the shop
                   panel's own row bounding boxes already use (SHOP_ROW_H - 2.0f margin). */
                if (bx >= track_x && bx <= track_x + SETTINGS_SLIDER_W &&
                    by >= track_y - 10.0f && by <= track_y + SETTINGS_SLIDER_H + 10.0f) {
                    settings_volume_dragging = 1;
                    float frac = (bx - track_x) / SETTINGS_SLIDER_W;
                    if (frac < 0.0f) frac = 0.0f;
                    if (frac > 1.0f) frac = 1.0f;
                    master_volume = frac;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                settings_volume_dragging = 0;
            }
            if (show_settings_pane && settings_volume_dragging && e.type == SDL_MOUSEMOTION) {
                float bx = (float)e.motion.x;
                float track_x, track_y;
                settings_slider_track(win_w, win_h, &track_x, &track_y);
                (void)track_y;
                float frac = (bx - track_x) / SETTINGS_SLIDER_W;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                master_volume = frac;
            }
            /* S202-34: right-click cancels ground-target aiming without casting -- checked
               before camera-drag engages so a cancel-click doesn't also start dragging the
               camera the same frame (harmless either way, but this reads cleaner). */
            if (g_ground_target_pending_slot != 0 && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                g_ground_target_pending_slot = 0;
            }
            /* ground_target_click_consumed (same "consumed this exact click" idiom as
               shop_click_consumed just above): the confirming click for a pending targeted
               ability -- intercepted here, BEFORE the ordinary drag-select/move-click flow
               below, so this click fires the ability instead of also issuing a move command or
               starting a box-select. Fires on mouse-DOWN (not up, unlike the ordinary click
               flow) since there's no drag-vs-click ambiguity to resolve here -- a targeted
               confirm is always a single deliberate click. A local flag, not a re-check of
               g_ground_target_pending_slot (which this same block clears), so the
               ordinary-click guard below can't be fooled into firing for this same click the
               instant the global goes back to 0.
               Bacon+Puck's Shadow Step (2026-08-26, founder: "have it click on a hero to
               teleport roughly behind them"): reads g_hover_target (the hero currently under
               the cursor, the same per-frame hit-test the plain click-to-attack flow already
               computes -- see g_hover_target's own declaration comment) instead of calling
               screen_to_ground for a ground point. A miss (no hero under the cursor when the
               click lands) is a real no-op, same "real commitment" convention as every other
               targeted ability -- nothing is sent, the cooldown/mana never get touched, per the
               founder's own earlier "dont have it blow the cooldown and do nothing" fix for
               Abraham's own cast. screen_to_ground/the reticle draw pass stay real, general
               machinery any future GROUND-targeted ability can still reuse -- not touched here,
               this ability just doesn't need them. */
            int ground_target_click_consumed = 0;
            if (!observing && g_ground_target_pending_slot != 0 && e.type == SDL_MOUSEBUTTONDOWN &&
                e.button.button == SDL_BUTTON_LEFT && arena_state.winner == 0 &&
                my_owner >= 0 && my_owner < ARENA_MAX_HEROES) {
                ground_target_click_consumed = 1;
                if (g_hover_target >= 0) {
                    int slot = g_ground_target_pending_slot;
                    if (net_mode) {
                        net_send_cast(slot - 1, g_hover_target, 0, 0.0f, 0.0f);
                    } else {
                        arena_set_hover_target(my_owner, g_hover_target);
                        if (slot == 1) { arena_toggle_w(my_owner); arena_log_ability("W"); }
                    }
                    apm_record_action(now);
                }
                g_ground_target_pending_slot = 0;
            }
            /* Everything below drives a live match (movement clicks, kit
             * casts, restart-into-a-new-match) -- none of it applies while
             * observing a logged one. Camera control above still works, so
             * an observer can look around freely. S170-229: !shop_click_consumed
             * -- see that variable's own doc comment above; a click the shop
             * panel already handled (or that just landed on its own blank
             * space) must not also fall through and move the player.
             * !settings_click_consumed (3424324/343543): same idiom, for the settings pane.
             *
             * 2026-07-30, Tyler clone-control rework: this used to act directly on
             * SDL_MOUSEBUTTONDOWN. Now it only RECORDS the down-position here -- the actual
             * decision (was this a click or a drag-select?) happens on mouse-UP below, the
             * standard RTS/MOBA convention (League, Dota, WC3 all resolve it exactly this way)
             * that needs no new keybind. For every hero except Tyler (and Tyler himself unless
             * he's actually dragged a selection box), selected_unit_count stays 0 forever, so
             * the mouseup branch below resolves to exactly one commander (my_owner) and behaves
             * byte-for-byte like the old mousedown-triggered code -- zero behavior change for
             * the other 27 heroes' existing muscle memory. */
            if (!observing && !shop_click_consumed && !ground_target_click_consumed &&
                !settings_click_consumed &&
                e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                arena_state.winner == 0) {
                left_drag_active = 1;
                left_drag_start_x = e.button.x;
                left_drag_start_y = e.button.y;
            }
            if (!observing && left_drag_active && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                left_drag_active = 0;
                float ddx = (float)(e.button.x - left_drag_start_x);
                float ddy = (float)(e.button.y - left_drag_start_y);
                if (arena_state.winner == 0 && sqrtf(ddx * ddx + ddy * ddy) >= ARENA_DRAG_SELECT_THRESHOLD_PX) {
                    /* Box-select: which of the local player's own units (itself, or its own
                       active Tyler clones) fall inside the screen-space rectangle the drag
                       spanned. Reuses g_last_vp (see its own doc comment) since this frame's own
                       vp isn't computed yet at this point in the loop, same reasoning
                       g_hover_target's one-frame staleness already established. An empty box
                       (nothing of the player's own falls inside it) resets to "just self" rather
                       than leaving the player stuck with zero controllable units -- real RTS
                       precedent for "you can never fully deselect your only unit." */
                    float rx0 = fminf((float)left_drag_start_x, (float)e.button.x);
                    float rx1 = fmaxf((float)left_drag_start_x, (float)e.button.x);
                    float ry0 = fminf((float)left_drag_start_y, (float)e.button.y);
                    float ry1 = fmaxf((float)left_drag_start_y, (float)e.button.y);
                    int new_units[ARENA_MAX_SELECTED_UNITS];
                    int new_count = 0;
                    for (int cand = 0; cand < ARENA_HEROES_ARRAY_SIZE && new_count < ARENA_MAX_SELECTED_UNITS; cand++) {
                        ArenaHero *ch = &arena_state.heroes[cand];
                        if (!ch->active || !ch->alive) continue;
                        if (cand != my_owner && !(ch->is_clone && ch->clone_owner == my_owner)) continue;
                        float sx, sy;
                        if (!world_to_screen(&g_last_vp, ch->x, 1.0f, ch->z, win_w, win_h, &sx, &sy)) continue;
                        float sy_top = (float)win_h - sy; /* world_to_screen's sy is bottom-up, drag coords are SDL top-down */
                        if (sx >= rx0 && sx <= rx1 && sy_top >= ry0 && sy_top <= ry1) {
                            new_units[new_count++] = cand;
                        }
                    }
                    selected_unit_count = new_count;
                    for (int i = 0; i < new_count; i++) selected_units[i] = new_units[i];
                    apm_record_action(now);
                } else if (arena_state.winner == 0) {
                    /* An ordinary click -- same attack-vs-move decision as before (S170-162,
                       NORTHSTAR SS17.1's "right-click ground vs right-click a unit" split), now
                       dispatched to every currently-selected unit instead of hardcoded to
                       my_owner. */
                    int commanders[ARENA_MAX_SELECTED_UNITS];
                    int commander_count = selected_or_self(commanders);

                    int enemy_click_target = -1;
                    if (net_mode && net_lobby_size > 2 && g_hover_target >= 0 && g_hover_target < net_lobby_size
                        && my_owner >= 0 && my_owner < ARENA_MAX_HEROES) {
                        ArenaHero *hovered = &arena_state.heroes[g_hover_target];
                        if (hovered->active && hovered->alive && hovered->team != arena_state.heroes[my_owner].team) {
                            enemy_click_target = g_hover_target;
                        }
                    }
                    if (enemy_click_target >= 0) {
                        /* Team-mode-only, same reasoning the original single-unit version of
                           this branch already documented -- enemy_click_target can only ever be
                           set under net_mode && net_lobby_size > 2 above, so every commander here
                           is real, net_send_attack is always the right call, no local-mode
                           fallback needed (Tyler's own clones are team-mode only too). */
                        for (int k = 0; k < commander_count; k++) net_send_attack(commanders[k], enemy_click_target);
                        apm_record_action(now);
                    } else {
                        float gx, gz;
                        float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
                        if (screen_to_ground(e.button.x, e.button.y, win_w, win_h, 60.0f,
                                             focus_x, focus_z, &gx, &gz)) {
                            /* Attack-move / Patrol (NORTHSTAR.md §17.4 + §24 Milestone 2,
                               2026-07-31): real LoL/WC3 "hold A/P, then click ground" -- checked
                               via this frame's held-key state, same "held, not toggled" idiom
                               the Tab scoreboard already uses, not a separate keydown event/mode
                               toggle. Patrol checked first: if both happened to be held (an
                               unusual chord, not a real player intent either way), patrol wins
                               rather than leaving the outcome to whichever branch happened to be
                               written first with no comment explaining why. */
                            const Uint8 *ks = SDL_GetKeyboardState(NULL);
                            int patrol = ks[SDL_SCANCODE_P];
                            int attack_move = ks[SDL_SCANCODE_A];
                            for (int k = 0; k < commander_count; k++) {
                                if (patrol) {
                                    if (net_mode) net_send_patrol(commanders[k], gx, gz);
                                    else arena_set_patrol_target(commanders[k], gx, gz);
                                } else if (attack_move) {
                                    if (net_mode) net_send_attack_move(commanders[k], gx, gz);
                                    else arena_set_attack_move_target(commanders[k], gx, gz);
                                } else if (net_mode) net_send_move(commanders[k], gx, gz);
                                else arena_set_move_target(commanders[k], gx, gz);
                            }
                            spawn_ring(gx, gz);
                            apm_record_action(now);
                        }
                    }
                }
            }
            /* Draft pick-screen click (S170-182): only meaningful while the draft screen is
               actually showing (draw_draft_screen's own gate above, net_phase==DRAFT &&
               !net_picked) -- picking twice can't happen since net_picked flips true on the
               very first valid click. */
            if (net_mode && !observing && net_phase == ARENA_PHASE_DRAFT && !net_picked &&
                e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int hero_id = draft_screen_hero_at(e.button.x, e.button.y, win_w, win_h);
                if (hero_id >= 0) {
                    net_send_pick(hero_id);
                    net_picked = 1;
                    net_picked_hero_id = hero_id;
                    net_last_pick_send_ms = now;
                    printf("[arena client] drafted hero_id=%d for slot %d\n", hero_id, my_owner);
                    fflush(stdout);
                }
            }
            /* Requeue-after-win OK button (S170-66/68: "we need to requeue after
             * a game after an ok button"). Only meaningful in net_mode -- local
             * practice mode already has its own R-to-restart below. Click box
             * matches the one drawn under the YOU WIN/YOU LOSE text further down;
             * SDL mouse y is top-down, the ortho HUD draw space is bottom-up, so
             * flip before hit-testing against those same screen-space bounds. */
            if (net_mode && !observing && e.type == SDL_MOUSEBUTTONDOWN &&
                e.button.button == SDL_BUTTON_LEFT && arena_state.winner != 0) {
                float bx = e.button.x, by = win_h - e.button.y;
                float ok_left = win_w / 2.0f - 90, ok_right = win_w / 2.0f + 90;
                float ok_bottom = win_h / 2.0f - 70, ok_top = win_h / 2.0f - 30;
                if (bx >= ok_left && bx <= ok_right && by >= ok_bottom && by <= ok_top) {
                    printf("[arena client] requeuing for another match...\n");
                    fflush(stdout);
#ifdef _WIN32
                    if (net_sock >= 0) closesocket(net_sock);
#else
                    if (net_sock >= 0) close(net_sock);
#endif
                    net_sock = -1;
                    memset(&arena_state, 0, sizeof(arena_state));
                    /* S170-148 bugfix: obstacles (and fountains, position-only/
                       always-recomputed so no explicit call needed there) are
                       never wire-synced -- the memset above just wiped this
                       client's own local obstacles[] to all-zero with nothing to
                       repopulate it, since server_broadcast() never sends this
                       static layout in the first place. Was the real cause of
                       "first game had jungle rocks and trees, subsequent games
                       didn't" -- every match after the first requeue silently
                       lost its jungle terrain. */
                    arena_obstacles_reset_layout();
                    memset(rings, 0, sizeof(rings));
                    win_logged = 0;
                    net_picked = 0;
                    selected_unit_count = 0; /* 2026-07-30: a stale clone owner-index from the previous match means nothing in this one */
                    net_phase = ARENA_PHASE_WAITING;
                    draw_queuing_screen(win, win_w, win_h);
                    int reconnected = queue_host ? net_find_and_connect(queue_host, queue_port)
                                                  : net_connect(connect_host, connect_port);
                    if (!reconnected) {
                        fprintf(stderr, "[arena client] requeue failed -- matchmaker/bot pool may be down\n");
                    } else {
                        printf("[arena client] requeue connected -- hero slot %d\n", my_owner);
                    }
                    fflush(stdout);
                }
            }
            if (!net_mode && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                if (observing) {
                    observe_elapsed_ms = 0; /* restart playback from the beginning */
                    arena_state.winner = 0;
                } else {
                    arena_init();
                    memset(rings, 0, sizeof(rings));
                    arena_log_open(); /* fresh match -> fresh log file, S170-29 */
                    win_logged = 0;
                    selected_unit_count = 0;
                }
            }
            /* The Unicorn's kit (docs/HEROES_VS0.md) — the local player's own
             * hero (my_owner) only, S170-18. R is already bound to "restart
             * match" in local mode, so the ultimate goes on E. In net_mode,
             * casts are sent to the server, which owns cooldowns/effects. */
            if (!observing && e.type == SDL_KEYDOWN && arena_state.winner == 0) {
                if (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_e) {
                    apm_record_action(now);
                }
                /* S202-34, then the 2026-08-26 auto-target redesign (see arena_toggle_w's own
                   ARENA_HERO_ABRAHAM case for the full founder-quote chain): Abraham's W used
                   to require a two-phase "green reticle, click to confirm" ground-targeting
                   aim mode -- removed entirely now that the server auto-targets the nearest
                   enemy itself, so W presses the same as every other hero's: immediate cast,
                   no separate input mode.
                   Same aiming-mode machinery revived here for Bacon+Puck's own W (Shadow Step,
                   2026-08-26, founder: "use the targeting system you had for abraham fireball
                   before we changed it... but have it click on a hero to teleport roughly
                   behind them") -- g_ground_target_pending_slot re-enters the same real
                   targeting mode, but the confirm click now reads g_hover_target (a hero under
                   the cursor) instead of calling screen_to_ground for a ground point; see that
                   confirm block's own doc comment below. */
                int is_bacon_puck = (my_owner >= 0 && my_owner < ARENA_MAX_HEROES &&
                                     arena_state.heroes[my_owner].hero_id == ARENA_HERO_BACON_PUCK);
                if (is_bacon_puck && e.key.keysym.sym == SDLK_w) {
                    g_ground_target_pending_slot = 1; /* W */
                } else if (net_mode) {
                    if (e.key.keysym.sym == SDLK_q) net_send_cast(0, g_hover_target, 0, 0.0f, 0.0f);
                    if (e.key.keysym.sym == SDLK_w) net_send_cast(1, g_hover_target, 0, 0.0f, 0.0f);
                    if (e.key.keysym.sym == SDLK_e) net_send_cast(2, g_hover_target, 0, 0.0f, 0.0f);
                } else {
                    /* S170-143: local 1v1 demo casts directly (no wire hop), so the
                       hover target has to be set on the sim explicitly here -- the
                       networked path's equivalent is apps/arena_server's own
                       arena_set_hover_target() call in server_handle_packet(). */
                    arena_set_hover_target(my_owner, g_hover_target);
                    if (e.key.keysym.sym == SDLK_q) { arena_cast_q(my_owner); arena_log_ability("Q"); }
                    if (e.key.keysym.sym == SDLK_w) { arena_toggle_w(my_owner); arena_log_ability("W"); }
                    if (e.key.keysym.sym == SDLK_e) { arena_cast_r(my_owner); arena_log_ability("R"); }
                }
                /* Active item (S170-205/S170-206, founder: "add blink dagger 1400 flow it gives
                   a new keybind on screen for tilda" -> "tilda should make the hero do the
                   paper airplane glide thing"): a dedicated key, not one of Q/W/E, since it's an
                   item activation, not a kit ability -- same "the affordance you're looking at
                   is the one the key acts on" precedent as every other keybind in this file. One
                   key for whichever active item is actually equipped (Blink Dagger or Donkey),
                   same as arena_use_active_item's own server-side dispatch. apm_record_action
                   deliberately NOT called here -- item actives aren't kit abilities, same
                   reasoning the shop's own quick-buy keys (1-9) don't count toward APM either. */
                if (e.key.keysym.sym == SDLK_BACKQUOTE) {
                    if (net_mode) net_send_active_item();
                    else arena_use_active_item(my_owner);
                }
                /* Stop (NORTHSTAR.md §24 Milestone 2, 2026-07-31, founder: "the unit controls
                   are supposed to be for tyler") -- the first of the real WC3 group-order
                   vocabulary that section names, real RTS convention (S = Stop). Applies to
                   the whole currently-selected group, same selected_or_self() resolution
                   move/attack clicks already use in the mouse handler above -- a Tyler player
                   who's drag-selected several clones stops all of them at once, matching real
                   WC3's own group-order behavior, not just the clicked-on unit. */
                if (e.key.keysym.sym == SDLK_s) {
                    int commanders[ARENA_MAX_SELECTED_UNITS];
                    int commander_count = selected_or_self(commanders);
                    for (int k = 0; k < commander_count; k++) {
                        if (net_mode) net_send_stop(commanders[k]);
                        else arena_stop_unit(commanders[k]);
                    }
                    apm_record_action(now);
                }
                /* Hold Position (NORTHSTAR.md §24 Milestone 2, 2026-07-31) -- third of the real
                   WC3 group-order vocabulary. Real WC3/StarCraft convention is "H", already
                   bound to the ability-help toggle in this file (SDLK_h above) -- "D" (Defend,
                   the exact synonym several other RTS UIs use for this same order) is free and
                   thematically close enough not to need inventing a new mnemonic. Same
                   selected_or_self() group application as Stop just above. */
                if (e.key.keysym.sym == SDLK_d) {
                    int commanders[ARENA_MAX_SELECTED_UNITS];
                    int commander_count = selected_or_self(commanders);
                    for (int k = 0; k < commander_count; k++) {
                        if (net_mode) net_send_hold(commanders[k]);
                        else arena_hold_position(commanders[k]);
                    }
                    apm_record_action(now);
                }
            }
        }

        if (observing) {
            /* Drive the exact same ArenaState the live path draws from --
             * "same draw code, no second rendering path" (S170-30). */
            arena_replay_apply_at(&replay, observe_elapsed_ms, &arena_state);
        }
        else if (net_mode) {
            /* apps/arena_server is authoritative -- apply its snapshots
               rather than running arena_update() locally (that would
               double-simulate and diverge from the server's own state). */
            net_poll_snapshots(now);
        }
        else if (arena_state.winner == 0) {
            arena_update(dt);
            arena_log_since_snapshot_ms += dt;
            if (arena_log_since_snapshot_ms >= ARENA_LOG_SNAPSHOT_INTERVAL_MS) {
                arena_log_snapshot();
                arena_log_since_snapshot_ms = 0;
            }
        } else if (!win_logged && !net_mode) {
            arena_log_win(arena_state.winner);
            win_logged = 1;
        }

        /* S170-182: heroes[] isn't meaningful during ARENA_PHASE_DRAFT (protocol.h's own
           ArenaHeroSnapshot doc comment already says so) -- render the pick screen instead of
           the normal match view for as long as the draft is still waiting on this client's own
           pick, same "replace the frame's content entirely" idiom draw_queuing_screen already
           uses for its own blocking wait. */
        if (net_mode && !observing && net_phase == ARENA_PHASE_DRAFT && !net_picked) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            int hover_hero_id = draft_screen_hero_at(mx, my, win_w, win_h);
            draw_draft_screen(win, win_w, win_h, hover_hero_id);
            SDL_Delay(16);
            continue;
        }

        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            rings[i].age_ms += dt;
            if (rings[i].age_ms >= RING_LIFETIME_MS) rings[i].active = 0;
        }
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) {
                prev_hero_hp_valid[i] = 0;
                prev_hero_moving_valid[i] = 0;
                prev_hero_facing_valid[i] = 0;
                prev_donkey_fold_valid[i] = 0;
                continue;
            }
            /* S170-171: update facing from observed movement this frame,
               before anything below reads hero_facing_rad[i] for drawing. */
            update_facing_from_motion(h->x, h->z, &prev_hero_facing_x[i], &prev_hero_facing_z[i],
                                       &prev_hero_facing_valid[i], &hero_facing_rad[i]);
            /* Movement-start squish (S170-128, "for movement also spell casts"):
               same transition-detection idiom as the HP-delta check just below,
               fired once per departure, not every frame spent moving. */
            if (prev_hero_moving_valid[i] && !prev_hero_moving[i] && h->moving) {
                trigger_squish(i);
            }
            prev_hero_moving[i] = h->moving;
            prev_hero_moving_valid[i] = 1;
            if (prev_hero_hp_valid[i] && h->hp < prev_hero_hp[i]) {
                spawn_attack_flash(h->x, h->z);
                trigger_squish(i);
                float hdx = h->x - arena_state.heroes[my_owner].x;
                float hdz = h->z - arena_state.heroes[my_owner].z;
                if (hdx * hdx + hdz * hdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_tone(220.0f, 60.0f, 0.35f); /* short low thud, distinct from the higher/longer cast tones */
                }
            } else if (prev_hero_hp_valid[i] && h->hp > prev_hero_hp[i]) {
                /* S170-143: the target's half of "show cast animation on the target and
                   the self" -- a heal-flash fires wherever the HP increase actually
                   happened, which for Doc Wheel's hover-cast Q may be a hero standing
                   far from the caster. */
                spawn_heal_flash(h->x, h->z);
                trigger_squish(i);
                float hdx = h->x - arena_state.heroes[my_owner].x;
                float hdz = h->z - arena_state.heroes[my_owner].z;
                if (hdx * hdx + hdz * hdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_tone(660.0f, 90.0f, 0.3f); /* brighter, higher tone -- distinct from the attack thud */
                }
            }
            prev_hero_hp[i] = h->hp;
            prev_hero_hp_valid[i] = 1;

            /* S170-210: Donkey's Immortal's Fold, edge-detected off the wearer's own
               equipped_item + survive_floor_ms (both already synced -- no new wire
               state needed). */
            int donkey_fold_active = (h->equipped_item[ARENA_ITEM_SLOT_BACK] == ARENA_DONKEY_ITEM_ID) &&
                                      h->survive_floor_ms > 0;
            if (prev_donkey_fold_valid[i] && !prev_donkey_fold_active[i] && donkey_fold_active) {
                spawn_fold_flash(h->x, h->z);
                trigger_squish(i);
                float fdx = h->x - arena_state.heroes[my_owner].x;
                float fdz = h->z - arena_state.heroes[my_owner].z;
                if (fdx * fdx + fdz * fdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_tone(880.0f, 140.0f, 0.4f); /* bright, longer than the heal/attack tones -- a near-death save deserves to stand out */
                }
            }
            prev_donkey_fold_active[i] = donkey_fold_active;
            prev_donkey_fold_valid[i] = 1;
        }
        /* S170-145: creep-side half of "auto attacks hit a creep or a hero should
           show visual indication" -- same HP-delta idiom as heroes above, both
           creep pools, reusing the exact same attack_flashes visual (a hit is a
           hit, no need for a creep-specific look). */
        for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
            ArenaCreep *cr = &arena_state.creeps[i];
            if (!cr->alive) {
                prev_creep_hp_valid[i] = 0;
                prev_creep_facing_valid[i] = 0;
                continue;
            }
            if (prev_creep_hp_valid[i] && cr->hp < prev_creep_hp[i]) {
                spawn_attack_flash(cr->x, cr->z);
            }
            prev_creep_hp[i] = cr->hp;
            prev_creep_hp_valid[i] = 1;
            update_facing_from_motion(cr->x, cr->z, &prev_creep_facing_x[i], &prev_creep_facing_z[i],
                                       &prev_creep_facing_valid[i], &creep_facing_rad[i]);
        }
        for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
            ArenaLaneCreep *lc = &arena_state.lane_creeps[i];
            if (!lc->active || !lc->alive) {
                prev_lane_creep_hp_valid[i] = 0;
                prev_lane_creep_facing_valid[i] = 0;
                continue;
            }
            if (prev_lane_creep_hp_valid[i] && lc->hp < prev_lane_creep_hp[i]) {
                spawn_attack_flash(lc->x, lc->z);
            }
            prev_lane_creep_hp[i] = lc->hp;
            prev_lane_creep_hp_valid[i] = 1;
            update_facing_from_motion(lc->x, lc->z, &prev_lane_creep_facing_x[i], &prev_lane_creep_facing_z[i],
                                       &prev_lane_creep_facing_valid[i], &lane_creep_facing_rad[i]);
        }
        for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
            ArenaProjectile *p = &arena_state.projectiles[i];
            if (prev_projectile_active[i] && !p->active && p->hero_id == ARENA_HERO_GHOST) {
                spawn_lightning_burst(p->x, p->z);
            }
            prev_projectile_active[i] = p->active;
        }
        for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
            if (!attack_flashes[i].active) continue;
            attack_flashes[i].age_ms += dt;
            if (attack_flashes[i].age_ms >= ATTACK_FLASH_LIFETIME_MS) attack_flashes[i].active = 0;
        }
        for (int i = 0; i < MAX_LIGHTNING_BURSTS; i++) {
            if (!lightning_bursts[i].active) continue;
            lightning_bursts[i].age_ms += dt;
            if (lightning_bursts[i].age_ms >= LIGHTNING_BURST_LIFETIME_MS) lightning_bursts[i].active = 0;
        }
        for (int i = 0; i < MAX_HEAL_FLASHES; i++) {
            if (!heal_flashes[i].active) continue;
            heal_flashes[i].age_ms += dt;
            if (heal_flashes[i].age_ms >= HEAL_FLASH_LIFETIME_MS) heal_flashes[i].active = 0;
        }
        for (int i = 0; i < MAX_FOLD_FLASHES; i++) {
            if (!fold_flashes[i].active) continue;
            fold_flashes[i].age_ms += dt;
            if (fold_flashes[i].age_ms >= FOLD_FLASH_LIFETIME_MS) fold_flashes[i].active = 0;
        }
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            if (squish_age_ms[i] >= 0.0f && squish_age_ms[i] < SQUISH_ANIM_MS) {
                squish_age_ms[i] += dt;
            }
        }
        /* Abraham's Fireball windup (S202-34): edge-detects casting_slot's transition into
           slot 2 (W) for an Abraham hero to START the animation (captures the hero's CURRENT
           on-screen facing as the rotation's start point, and the real server-authoritative
           cast_target_x/z as its end point, via the same atan2f(dx,dz) convention
           update_facing_from_motion already uses), then just ticks age_ms by dt every frame
           it's active -- draw_hero_model's own call site (below) reads the resulting
           interpolated facing/squish, this loop never touches rendering directly. Cleared the
           instant casting_slot leaves 2, whether that's real completion OR a real interrupt
           (silence/movement) -- either way there's no windup left to animate. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *awh = &arena_state.heroes[i];
            int is_winding_up = (awh->active && awh->hero_id == ARENA_HERO_ABRAHAM && awh->casting_slot == 2);
            if (is_winding_up && !abraham_windup_active[i]) {
                abraham_windup_active[i] = 1;
                abraham_windup_age_ms[i] = 0.0f;
                abraham_windup_start_facing[i] = hero_facing_rad[i];
                float wdx = awh->cast_target_x - awh->x, wdz = awh->cast_target_z - awh->z;
                abraham_windup_target_facing[i] = (wdx * wdx + wdz * wdz > 0.0001f)
                    ? atan2f(wdx, wdz) : hero_facing_rad[i];
            } else if (!is_winding_up) {
                abraham_windup_active[i] = 0;
            }
            if (abraham_windup_active[i]) {
                abraham_windup_age_ms[i] += (float)dt;
                /* Overrides whatever update_facing_from_motion computed for this hero earlier
                   this same frame -- Abraham is typically stationary during the windup (any
                   real movement interrupts the cast server-side anyway, see arena_toggle_w's
                   own doc comment), so the motion-derived system alone would just leave him
                   facing wherever he already was, never actually turning toward the target.
                   Shortest-path angle interpolation (wrapped to (-pi, pi]) so a target behind
                   Abraham turns him the short way, not spinning the long way around. */
                float progress = abraham_windup_age_ms[i] / (float)ARENA_ABRAHAM_FIREBALL_WINDUP_MS;
                float eased = windup_ease_sample(progress);
                float diff = abraham_windup_target_facing[i] - abraham_windup_start_facing[i];
                while (diff > (float)M_PI) diff -= 2.0f * (float)M_PI;
                while (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
                hero_facing_rad[i] = abraham_windup_start_facing[i] + diff * eased;
            }
        }
        for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
            if (tree_squish_age_ms[i] >= 0.0f && tree_squish_age_ms[i] < SQUISH_ANIM_MS) {
                tree_squish_age_ms[i] += dt;
            }
        }
        /* Local-mode cast_flash_slot drain (S170-124): net_mode already spawns spell
           flashes directly off the wire snapshot inside net_poll_snapshots and never
           writes this field locally, so this loop is a no-op there -- it only ever
           fires for the local 1v1 demo, where arena_cast_q/toggle_w/cast_r are called
           directly (both the human's own key presses and the internal bot AI), with no
           server-side broadcast/clear step to do this job instead. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (h->cast_flash_slot > 0) {
                spawn_spell_flash(h->x, h->z, h->cast_flash_slot, h->hero_id);
                trigger_squish(i);
                float sdx = h->x - arena_state.heroes[my_owner].x;
                float sdz = h->z - arena_state.heroes[my_owner].z;
                if (sdx * sdx + sdz * sdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_cast_tone(h->cast_flash_slot);
                }
                h->cast_flash_slot = 0;
            }
        }
        for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
            if (!spell_flashes[i].active) continue;
            spell_flashes[i].age_ms += dt;
            if (spell_flashes[i].age_ms >= SPELL_FLASH_LIFETIME_MS) spell_flashes[i].active = 0;
        }

        glViewport(0, 0, win_w, win_h);
        {
            /* 2026-08-25: day/night ambient tint replaces the old fixed clear color -- only in
             * the actual in-match render loop (not draw_queuing_screen/draw_draft_screen's own
             * pre-match glClearColor calls, which stay fixed since day/night doesn't apply
             * before a match's own clock is running). See arena_game.h's own doc comment on
             * arena_daynight_ambient_rgb for the SHANKPIT retro_lighting.c source this ports. */
            float dn_r, dn_g, dn_b;
            arena_daynight_ambient_rgb(&dn_r, &dn_g, &dn_b);
            glClearColor(dn_r, dn_g, dn_b, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
        Mat4 view = mat4_orbit_view(focus_x, 0, focus_z, cam_yaw, cam_pitch, cam_dist);
        Mat4 proj = mat4_perspective(60.0f, (float)win_w / (float)win_h, 0.1f, 100.0f);
        Mat4 vp = mat4_multiply(&proj, &view);
        g_last_vp = vp; /* 2026-07-30: see this variable's own doc comment -- next frame's drag-select box-test reads this */

        glUseProgram_(prog);
        glUniform3f_(loc_light, 0.4f, 0.8f, 0.3f);

        /* ground */
        {
            Mat4 model = mat4_scale(ARENA_HALF_EXTENT * 2.2f, 1.0f, ARENA_HALF_EXTENT * 2.2f);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.08f, 0.18f, 0.10f, 1.0f);
            draw_mesh(&plane_mesh);
        }

        /* nodes -- colored RELATIVE to the local viewer's own team (S170-149
           bugfix, founder: "i cap a node but it flips wrong color"). Was
           hardcoded absolute (owner==1 always blue, owner==2 always red)
           while every hero on this same map is colored RELATIVE to the
           viewer (self/ally = blue-ish, enemy = red) -- for a team-0
           viewer those two conventions happen to agree by coincidence, but
           for a team-1 viewer their OWN team's node rendered in the exact
           red already reserved for enemy heroes on their own screen: a
           node they just captured looked identical to an enemy-held one.
           Now: "my team owns this" is always the same blue an ally-colored
           hero already uses, "the enemy owns this" is always the same red
           an enemy-colored hero already uses, regardless of which team the
           local player is actually on. Gold for neutral/contested is
           unchanged -- it was never team-relative to begin with. */
        for (int i = 0; i < ARENA_NODE_COUNT; i++) {
            Mat4 t = mat4_translate(arena_state.nodes[i].x, 0.15f, arena_state.nodes[i].z);
            Mat4 s = mat4_scale(1.2f, 0.3f, 1.2f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            int owner = arena_state.nodes[i].owner;
            int my_team = arena_state.heroes[my_owner].team;
            if (owner == 0) {
                glUniform4f_(loc_color, 0.85f, 0.7f, 0.1f, 1.0f); /* neutral/contested */
            } else if (owner == my_team + 1) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, 1.0f); /* my team's -- same blue as an ally hero */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f); /* enemy team's -- same red as an enemy hero */
            }
            draw_mesh(&cube_mesh);
        }

        /* jungle obstacles (S170-138, "add rocks and trees so we naturally
           start to create some lanes"): boxes only, same "boxes for now"
           silhouette approach as the hero models below -- trunk+canopy for a
           tree (mirrors ARENA_HERO_TREE's own two-box shape), one squat box
           for a rock. Purely a draw of where the sim's own obstacles[] array
           already is (packages/simulation/arena_game.c's
           arena_obstacles_reset_layout) -- the collision that actually
           carves the map into lanes happens sim-side in
           resolve_hero_obstacle_collision, this is just rendering it. */
        for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
            const ArenaObstacle *o = &arena_state.obstacles[i];
            if (o->kind == ARENA_OBSTACLE_TREE) {
                /* Tree passive (2026-08-25, founder: "have it jiggle animate extra squishy"):
                   canopy gets the hit-reaction squish, same draw_hero_box squish param heroes
                   already use (see compute_tree_squish's own doc comment) -- trunk stays rigid
                   (1.0f, unsquished) so the tree reads as rooted in place, only the leafy top
                   reacts, closer to a real branch shaking off a hit than the whole trunk wobbling. */
                float squish = compute_tree_squish(i);
                glUniform4f_(loc_color, 0.32f, 0.22f, 0.12f, 1.0f); /* trunk: brown */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 0.7f, 0.0f,
                              o->radius * 0.35f, o->radius * 1.4f, o->radius * 0.35f,
                              1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
                glUniform4f_(loc_color, 0.15f, 0.45f, 0.18f, 1.0f); /* canopy: green */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 1.7f, 0.0f,
                              o->radius, o->radius * 0.9f, o->radius,
                              squish, &vp, loc_mvp, loc_model, &cube_mesh);
            } else {
                glUniform4f_(loc_color, 0.45f, 0.44f, 0.42f, 1.0f); /* rock: grey */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 0.55f, 0.0f,
                              o->radius, o->radius * 0.55f, o->radius * 0.9f,
                              1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            }
        }

        /* Healing fountains (S170-147, "add healing fountains at 2 corners
           of the map across from each other"): a base + pillar silhouette,
           distinct from every tree/rock/hero/node/creep shape already on
           this map, in a bright cyan-white that reads as "healing" the same
           way the heal-flash (S170-143) already does. Position comes from
           arena_fountain_position() -- the same sim-side source of truth
           the server's own arena_tick_fountains() ticks against, so the
           client never needs this synced over the wire (same "static,
           deterministic layout" precedent as jungle obstacles). A faint
           ring at the actual heal radius (ARENA_FOUNTAIN_RADIUS) makes the
           "how close do I need to be" affordance visible, not just implied. */
        for (int i = 0; i < ARENA_FOUNTAIN_COUNT; i++) {
            float fx, fz;
            arena_fountain_position(i, &fx, &fz);
            glUniform4f_(loc_color, 0.15f, 0.55f, 0.9f, 1.0f); /* base: deep cyan-blue */
            draw_hero_box(fx, fz, 0.0f, 0.15f, 0.0f, 1.6f, 0.15f, 1.6f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            glUniform4f_(loc_color, 0.4f, 0.95f, 1.0f, 1.0f); /* pillar: bright cyan-white */
            draw_hero_box(fx, fz, 0.0f, 1.3f, 0.0f, 0.4f, 1.1f, 0.4f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
        }

        /* Warsong Gulch-style powerups (S170-190, founder: "add berserker and health regen
           powerups like from warsong gulch in between the nodes"): a small floating orb,
           distinct from the fountains' own taller pillar shape -- Berserker in fiery
           orange-red (damage, aggression), Regen in a healing green (same color family the
           heal-flash and status label already use for "good things happening to your HP").
           Position + active state come over the wire (unlike fountains, powerups have real
           dynamic state a static client-side layout can't represent) -- simply not drawn at
           all while inactive (just grabbed, on cooldown), the same "gone until it respawns"
           read a real WSG pickup gives. */
        for (int i = 0; i < ARENA_SNAPSHOT_POWERUP_COUNT; i++) {
            ArenaPowerup *pu = &arena_state.powerups[i];
            if (!pu->active) continue;
            if (pu->kind == ARENA_POWERUP_BERSERKER) {
                glUniform4f_(loc_color, 0.9f, 0.25f, 0.1f, 1.0f);
            } else {
                glUniform4f_(loc_color, 0.2f, 0.9f, 0.3f, 1.0f);
            }
            draw_hero_box(pu->x, pu->z, 0.0f, 0.7f, 0.0f, 0.6f, 0.6f, 0.6f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
        }

        /* Shop structures (S170-175, "have there be 2 shops in the other 2
           corners of the maps that don't have fountains"): a base + counter
           silhouette, distinct from the fountains' pillar shape above --
           amber/gold reads as "currency" the same way this HUD's own Flow
           number will below, with a team-relative trim on top (same
           self/ally/enemy convention as nodes/heroes) since arena_shop_buy
           only lets a hero spend at their OWN team's shop, not either one.
           Position from arena_shop_position() -- same "static, deterministic
           layout, never synced over the wire" precedent as fountains and
           jungle obstacles. */
        for (int team = 0; team < 2; team++) {
            float shx, shz;
            arena_shop_position(team, &shx, &shz);
            glUniform4f_(loc_color, 0.75f, 0.6f, 0.15f, 1.0f); /* base: amber/gold */
            draw_hero_box(shx, shz, 0.0f, 0.2f, 0.0f, 1.8f, 0.4f, 1.4f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            int my_team = arena_state.heroes[my_owner].team;
            if (team == my_team) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, 1.0f); /* my team's shop: ally blue */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f); /* enemy team's shop: enemy red */
            }
            draw_hero_box(shx, shz, 0.0f, 0.55f, 0.0f, 1.4f, 0.3f, 1.0f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
        }

        /* heroes -- ARENA_MAX_HEROES so team-mode matches (up to 10v10, S170-183 -- reverted
           after briefly being 7v7 under S170-178) render every real hero; local/1v1 heroes[2..] are simply never
           alive, so this loop is a no-op regression risk for that mode. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            /* intangible_ms (Ghost's Not a Ghost, Frog's R vanish, Bacon Puck's Q, etc. --
               any kit that grants the shared can't-be-hit status) reads as the skinmodel
               going see-through for its duration, same "can't touch this" read a real MOBA
               gives untargetable heroes, on top of the INTANGIBLE text tag already above
               the health bar. Alpha blending needs GL_BLEND on and depth writes off for
               this hero's boxes only -- everyone else stays fully opaque with normal
               depth writes, same convention as the ring/flash effects below. */
            int is_intangible = h->intangible_ms > 0;
            float alpha = is_intangible ? 0.35f : 1.0f;
            if (i == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.8f, 0.95f, alpha); /* my hero: bright cyan */
            } else if (h->team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, alpha); /* teammate: blue */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, alpha); /* enemy: red */
            }
            if (is_intangible) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            /* S170-118: per-hero_id silhouette (multi-box), not one generic cube --
               relationship color above still wins for self/team/enemy legibility. */
            g_gband_loc_mvp = loc_mvp;
            g_gband_loc_model = loc_model;
            if (h->hero_id == ARENA_HERO_TYLER && !force_box_rig && gband_mesh_rig_ready()) {
                /* S144-07: real, founder-modeled skinned mesh -- see
                   gband_mesh_rig.h. This is what actually renders for Tyler
                   now; F9 force-falls-back to the box-rig for A/B comparison. */
                gband_mesh_rig_draw(i, h->x, h->z, hero_facing_rad[i], (float)dt, &vp, gband_mesh_cb_draw_skinned);
            } else if (h->hero_id == ARENA_HERO_TYLER && gband_rig_ready()) {
                /* S144-06: real GOLDENBAND-driven skeleton box-rig -- see
                   gband_rig.h. Falls back to the plain box below (via
                   gband_rig_ready()) if even this is missing, so a missing/
                   corrupt asset never means a missing hero. */
                gband_rig_draw(i, h->x, h->z, hero_facing_rad[i], (float)dt, &vp,
                                gband_cb_set_mvp_model, gband_cb_draw_mesh, &cube_mesh);
            } else {
                /* S202-34: Abraham's fireball windup overrides the ordinary landing-squish
                   animation while active -- the two never overlap in practice (the windup
                   only starts while stationary, landing-squish only starts on landing) but if
                   they somehow did, the windup wins, since it's tied to a real in-progress
                   server-authoritative cast rather than a one-off cosmetic bounce. */
                float squish = abraham_windup_active[i] ? abraham_windup_squish(abraham_windup_age_ms[i]) : compute_squish(i);
                draw_hero_model(h->hero_id, h->x, h->z, hero_facing_rad[i], squish, &vp, loc_mvp, loc_model, &cube_mesh);
            }
            if (is_intangible) {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }

        /* Tyler's puppet clones (2026-07-30, "his kit was stubbed in" -- real gap found while
           building independent clone control: clones have existed and fought server-side since
           S170-141, but were never drawn at all -- this loop, hero_facing_rad[]/squish_age_ms[]
           above are all sized exactly ARENA_MAX_HEROES, so simply widening the real-hero loop
           above to ARENA_HEROES_ARRAY_SIZE would read those two arrays out of bounds for every
           clone slot. Kept deliberately separate and simpler instead of resizing and re-verifying
           several tightly-coupled per-hero tracking arrays shared with real heroes: facing is
           computed inline from the clone's own current target direction (no smoothed per-frame
           tracking the way hero_facing_rad gets for real heroes -- a clone snaps to face its
           target instantly rather than easing into it, a real but minor simplification, flagged
           not faked) and squish is fixed at 1.0 (no move/cast pulse animation). Same self/team/
           enemy relationship-color convention as real heroes: `clone_owner == my_owner` reads as
           "one of MY OWN clones" (bright cyan, same as piloting the real body), same
           team-vs-team check otherwise. */
        for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) continue;
            float clone_facing = 0.0f;
            float cfdx = h->target_x - h->x, cfdz = h->target_z - h->z;
            if (cfdx * cfdx + cfdz * cfdz > 0.0001f) clone_facing = atan2f(cfdx, cfdz);
            if (h->clone_owner == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.8f, 0.95f, 1.0f); /* my own clone: bright cyan, same as piloting the real body */
            } else if (h->team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, 1.0f); /* ally's clone: blue */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f); /* enemy's clone: red */
            }
            draw_hero_model(h->hero_id, h->x, h->z, clone_facing, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
        }

        /* Selection rings (2026-07-30, "clones multi control drag click all of it"): a ring
           under every currently drag-selected unit (self and/or owned clones), same ring_mesh
           idiom the aggro-radius circles below already use. Only drawn once selected_unit_count
           is actually nonzero -- the default "nothing explicitly selected, just controlling
           myself" state (every hero except Tyler, forever) shows nothing extra at all, zero
           visual change for the other 27 heroes. */
        for (int s = 0; s < selected_unit_count; s++) {
            int sel = selected_units[s];
            if (sel < 0 || sel >= ARENA_HEROES_ARRAY_SIZE) continue;
            ArenaHero *sh = &arena_state.heroes[sel];
            if (!sh->active || !sh->alive) continue;
            Mat4 seltr = mat4_translate(sh->x, 0.03f, sh->z);
            Mat4 selsc = mat4_scale(1.1f, 1.0f, 1.1f);
            Mat4 selmodel = mat4_multiply(&seltr, &selsc);
            Mat4 selmvp = mat4_multiply(&vp, &selmodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, selmvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, selmodel.m);
            glUniform4f_(loc_color, 0.3f, 1.0f, 0.3f, 0.8f); /* bright green -- "selected," distinct from every relationship/threat color already in use */
            draw_mesh(&ring_mesh);
        }

        /* Node-guardian creeps (S170-51, rendered for the first time S170-145 --
           "when auto attacks hit a creep... show visual indication," which
           is moot on a creep nobody can see). Local-mode/1v1-demo only,
           same not-yet-networked scope as lane creeps below. A small
           diamond-oriented box (45-degree Y rotation via two half-scale
           overlapping boxes would need mat4_rotate this renderer doesn't
           have -- kept as an axis-aligned box, distinguished from a lane
           creep instead by SIZE (bigger -- node-guardian creeps are the tougher,
           standalone objective) and by flavor-color matching the node
           ownership convention exactly (gold = neutral/contested, same
           blue/red team colors otherwise), not team-relative like heroes/
           lane creeps -- a node-guardian creep's color tells you whose territory
           it's tied to, the actual thing that matters about it. */
        for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
            ArenaCreep *cr = &arena_state.creeps[i];
            if (!cr->alive) continue;
            float cr_r, cr_g, cr_b;
            switch (cr->flavor) {
                case ARENA_CREEP_TEAM0: cr_r = 0.15f; cr_g = 0.35f; cr_b = 0.95f; break;
                case ARENA_CREEP_TEAM1: cr_r = 0.95f; cr_g = 0.25f; cr_b = 0.15f; break;
                default: cr_r = 0.85f; cr_g = 0.7f; cr_b = 0.1f; break; /* neutral -- matches node coloring */
            }
            /* S170-171: body + a small forward-facing nub, same asymmetric-
               silhouette idiom as hero models -- a plain cube reads
               identically from every angle, so rotating it toward its
               march direction (S170-161's team creeps genuinely march now)
               would have been invisible without something off-center to
               actually show the turn. */
            glUniform4f_(loc_color, cr_r, cr_g, cr_b, 1.0f);
            draw_hero_box_facing(cr->x, cr->z, creep_facing_rad[i], 0.0f, 0.45f, 0.0f, 0.75f, 0.75f, 0.75f, 1.0f,
                                  &vp, loc_mvp, loc_model, &cube_mesh);
            glUniform4f_(loc_color, cr_r * 0.6f, cr_g * 0.6f, cr_b * 0.6f, 1.0f); /* darker nub, same hue -- reads as a "front," not a second creep */
            draw_hero_box_facing(cr->x, cr->z, creep_facing_rad[i], 0.0f, 0.45f, 0.5f, 0.22f, 0.22f, 0.22f, 1.0f,
                                  &vp, loc_mvp, loc_model, &cube_mesh);
            /* S170-212: aggro-radius ring, same ring_mesh + flavor color already computed above
               for the body -- lets a player see the boundary before taking an unexpected hit,
               rather than learning it that way, particularly valuable since a marching team
               creep's position (S170-161) is already unpredictable in a way a fixed camp
               wouldn't be. Outline only (no filled disc, unlike the S170-200 zone-ability
               circles this reuses ring_mesh from) and no pulse -- this is a static, always-on
               passive boundary, not a "something just happened here" effect. */
            Mat4 aatr = mat4_translate(cr->x, 0.03f, cr->z);
            Mat4 aasc = mat4_scale(ARENA_CREEP_AGGRO_RADIUS, 1.0f, ARENA_CREEP_AGGRO_RADIUS);
            Mat4 aamodel = mat4_multiply(&aatr, &aasc);
            Mat4 aamvp = mat4_multiply(&vp, &aamodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, aamvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, aamodel.m);
            glUniform4f_(loc_color, cr_r, cr_g, cr_b, 0.3f);
            draw_mesh(&ring_mesh);
        }

        /* Node towers (2026-07-30, founder: "add towers around the nodes so beginning of game is
           a little slower"). Deliberately NOT team-relative/flavor-colored like node-guardian
           creeps -- a tower is always neutral-hostile to both teams, so it stays a fixed stone
           gray regardless of who's looking, distinct from every team-colored thing already on
           this map. Tall base+spire silhouette (unlike the squat creep box or the shop's own
           base+counter shape) reads as "structure," matching this map's own real MOBA turret
           precedent. Color darkens toward damaged-red as HP drops -- the "legible before you
           need it" telegraph this session's own NORTHSTAR §22.5 named as a real gap for jungle
           camps applies just as much here: a nearly-dead tower should read as nearly dead at a
           glance, not just via a number. Aggro-radius ring reuses the exact same ring_mesh idiom
           node-guardian creeps already use just above, for the same "see the boundary before
           taking a hit" reason. */
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            ArenaTower *tw = &arena_state.towers[n];
            if (!tw->alive) continue;
            float hp_frac = tw->max_hp > 0 ? (float)tw->hp / (float)tw->max_hp : 0.0f;
            float tw_r = 0.55f + 0.35f * (1.0f - hp_frac); /* healthy: neutral gray, damaged: reddening */
            float tw_g = 0.55f * hp_frac + 0.15f * (1.0f - hp_frac);
            float tw_b = 0.6f * hp_frac + 0.15f * (1.0f - hp_frac);
            glUniform4f_(loc_color, tw_r * 0.6f, tw_g * 0.6f, tw_b * 0.6f, 1.0f); /* base */
            draw_hero_box(tw->x, tw->z, 0.0f, 0.7f, 0.0f, 1.0f, 0.7f, 1.0f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            glUniform4f_(loc_color, tw_r, tw_g, tw_b, 1.0f); /* spire */
            draw_hero_box(tw->x, tw->z, 0.0f, 2.2f, 0.0f, 0.55f, 1.8f, 0.55f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);

            Mat4 twtr = mat4_translate(tw->x, 0.03f, tw->z);
            Mat4 twsc = mat4_scale(ARENA_TOWER_AGGRO_RADIUS, 1.0f, ARENA_TOWER_AGGRO_RADIUS);
            Mat4 twmodel = mat4_multiply(&twtr, &twsc);
            Mat4 twmvp = mat4_multiply(&vp, &twmodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, twmvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, twmodel.m);
            glUniform4f_(loc_color, 0.85f, 0.85f, 0.9f, 0.3f);
            draw_mesh(&ring_mesh);
        }

        /* Lane creeps (S170-138, wire-synced since S170-146 -- populated in BOTH the local
           1v1 demo, which simulates arena_update() directly, AND real networked matches via
           the server's own ArenaLaneCreepSnapshot pack loop; this render loop draws real data
           either way, correcting an earlier version of this comment that claimed otherwise).
           Small flat-topped boxes (distinct silhouette from the taller hero models) in the
           same self/team/enemy-adjacent blue/red team-color convention as nodes and heroes
           above, so a wave reads as "which side" at a glance without being mistaken for a
           hero. S170-218: melee vs. caster now get distinct silhouettes on top of that -- a
           caster is narrower and taller (a ranged unit reads as lighter/less physically
           imposing) with a bright glowing accent instead of melee's darker plate accent,
           mirroring the real HP/damage/range tradeoff the sim itself gives them. */
        for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
            ArenaLaneCreep *lc = &arena_state.lane_creeps[i];
            if (!lc->active || !lc->alive) continue;
            float lc_r, lc_g, lc_b;
            if (lc->team == arena_state.heroes[my_owner].team) {
                lc_r = 0.15f; lc_g = 0.35f; lc_b = 0.95f; /* friendly wave: blue */
            } else {
                lc_r = 0.95f; lc_g = 0.25f; lc_b = 0.15f; /* enemy wave: red */
            }
            int is_caster = (lc->role == ARENA_LANE_CREEP_CASTER);
            /* S170-171: same body + forward-nub idiom as node-guardian creeps above
               -- a lane creep marching its waypoint route (arena_game.c's
               lane_creep_waypoint) now visibly faces the way it's actually
               walking instead of floating along sideways. */
            glUniform4f_(loc_color, lc_r, lc_g, lc_b, 1.0f);
            if (is_caster) {
                draw_hero_box_facing(lc->x, lc->z, lane_creep_facing_rad[i], 0.0f, 0.45f, 0.0f, 0.38f, 0.65f, 0.38f, 1.0f,
                                      &vp, loc_mvp, loc_model, &cube_mesh);
                glUniform4f_(loc_color, 0.3f + lc_r * 0.7f, 0.3f + lc_g * 0.7f, 0.3f + lc_b * 0.7f, 1.0f); /* bright glowing accent, not a darker plate */
                draw_hero_box_facing(lc->x, lc->z, lane_creep_facing_rad[i], 0.0f, 0.45f, 0.35f, 0.14f, 0.14f, 0.14f, 1.0f,
                                      &vp, loc_mvp, loc_model, &cube_mesh);
            } else {
                draw_hero_box_facing(lc->x, lc->z, lane_creep_facing_rad[i], 0.0f, 0.35f, 0.0f, 0.55f, 0.55f, 0.55f, 1.0f,
                                      &vp, loc_mvp, loc_model, &cube_mesh);
                glUniform4f_(loc_color, lc_r * 0.6f, lc_g * 0.6f, lc_b * 0.6f, 1.0f);
                draw_hero_box_facing(lc->x, lc->z, lane_creep_facing_rad[i], 0.0f, 0.35f, 0.35f, 0.16f, 0.16f, 0.16f, 1.0f,
                                      &vp, loc_mvp, loc_model, &cube_mesh);
            }
        }

        /* Jungle camp minions + Kings, 2026-08-20: simulated server-side since Jungle Camps
           Milestones 1/2 but never rendered anywhere -- a pure wire-protocol gap (see
           ArenaCampMinionSnapshot/ArenaKingSnapshot's own doc comments in protocol.h), not a
           new gameplay feature. Neutral olive color for camp minions (distinct from lane
           creeps' team blue/red -- these belong to neither team) and per-camp thematic colors
           for Kings, boss-scaled (taller/wider than a lane creep, matching their real boss-tier
           HP/damage per docs2/JUNGLE_CAMPS_NORTHSTAR.md), so a King reads as a real objective
           at a glance rather than blending into the jungle-minion silhouette below it. */
        for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
            ArenaCampMinion *cm = &arena_state.camp_minions[i];
            if (!cm->active || !cm->alive) continue;
            glUniform4f_(loc_color, 0.55f, 0.5f, 0.2f, 1.0f); /* olive: neutral, neither team */
            draw_hero_box(cm->x, cm->z, 0.0f, 0.35f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f,
                           &vp, loc_mvp, loc_model, &cube_mesh);
        }
        {
            static const float king_color[ARENA_CAMP_COUNT][3] = {
                {0.85f, 0.7f, 0.15f},  /* 0 = North/Wealth: gold */
                {0.25f, 0.75f, 0.2f},  /* 1 = South/Growth: green */
                {0.75f, 0.2f, 0.75f},  /* 2 = East/Music: magenta */
                {0.2f, 0.6f, 0.85f},   /* 3 = West/All-Seeing: cyan-blue */
            };
            for (int i = 0; i < ARENA_CAMP_COUNT; i++) {
                ArenaKing *k = &arena_state.kings[i];
                if (!k->active || !k->alive) continue;
                glUniform4f_(loc_color, king_color[i][0], king_color[i][1], king_color[i][2], 1.0f);
                draw_hero_box(k->x, k->z, 0.0f, 0.8f, 0.0f, 1.1f, 1.6f, 1.1f, 1.0f,
                               &vp, loc_mvp, loc_model, &cube_mesh);
            }
        }

        /* projectiles (S170-136): the first travelling skill-shot in this
           arena. Small, bright, and shape-distinct from every hero
           silhouette on purpose -- this needs to read as "an incoming shot"
           at a glance, not blend into the hero-model system above. Same
           self/team/enemy color convention as heroes so a player can tell
           at a glance whether an in-flight shot is a threat (enemy, red)
           before it arrives -- the actual dodge affordance this ability was
           built for. */
        for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
            ArenaProjectile *p = &arena_state.projectiles[i];
            if (!p->active) continue;
            /* p->team isn't synced over the wire (owner is enough -- the
               firer's team is already known client-side via the heroes
               array, no need for a second field carrying the same fact). */
            if (p->owner < 0 || p->owner >= ARENA_MAX_HEROES) {
                /* 2026-07-30: a tower's shot (ARENA_PROJECTILE_NO_OWNER over the wire) has no real
                   hero slot to look up -- indexing heroes[p->owner] here would read out of bounds.
                   Fixed neutral ember-orange instead of the self/ally/enemy convention below,
                   matching the tower's own stone-and-ember visual theme (see the tower draw pass'
                   own doc comment) rather than borrowing a hero-relative color that wouldn't mean
                   anything for a shot with no firing hero. */
                glUniform4f_(loc_color, 0.95f, 0.45f, 0.1f, 1.0f); /* tower shot: ember orange */
            } else if (p->owner == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.95f, 1.0f, 1.0f); /* my own shot: bright cyan-white */
            } else if (arena_state.heroes[p->owner].team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.4f, 0.6f, 1.0f, 1.0f); /* ally's shot: light blue */
            } else {
                glUniform4f_(loc_color, 1.0f, 0.85f, 0.15f, 1.0f); /* enemy shot: hot yellow -- the thing you need to dodge */
            }
            Mat4 pt = mat4_translate(p->x, 0.8f, p->z);
            Mat4 ps = mat4_scale(0.35f, 0.35f, 0.35f);
            Mat4 pmodel = mat4_multiply(&pt, &ps);
            Mat4 pmvp = mat4_multiply(&vp, &pmodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
            draw_mesh(&cube_mesh);
            /* Ghost's Q crackle (founder: "ghost's Q should have a cool crackle
               lightning shader spell animation"): a handful of thin, randomly-angled
               box slivers zigzagging around the shot's own position, fully re-rolled
               every frame so they flicker like a live electric discharge instead of
               sitting static -- same "boxes for now" convention as every hero
               silhouette in this renderer (draw_hero_box_facing), no new draw
               primitive needed. Bright electric cyan-white, distinct from every
               owner-relationship color above since it's a spell-identity cue, not a
               threat-relationship one. */
            if (p->hero_id == ARENA_HERO_GHOST) {
                glUniform4f_(loc_color, 0.65f, 0.95f, 1.0f, 1.0f);
                for (int seg = 0; seg < 4; seg++) {
                    float jitter_angle = ((float)(rand() % 360)) * (float)M_PI / 180.0f;
                    float jx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.8f;
                    float jz = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.8f;
                    float jy = 0.5f + (float)(rand() % 100) / 100.0f * 0.6f;
                    draw_hero_box_facing(p->x, p->z, jitter_angle, jx, jy, jz,
                                          0.04f, 0.04f, 0.3f, 1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
                }
            }
        }

        /* placement rings */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            float t01 = rings[i].age_ms / RING_LIFETIME_MS;
            float scale = 0.3f + t01 * 1.5f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(rings[i].x, 0.03f, rings[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.2f, 1.0f, 0.5f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* Ground-target reticle (S202-34, founder: "the targeter is green
           when you are ready to cast"): a real-time green ring following
           the live mouse position while g_ground_target_pending_slot is
           set (Abraham's W aiming mode) -- distinct from the placement
           rings above (those decay/fade and mark a past click; this one
           doesn't fade and tracks the CURRENT mouse position every frame,
           since it's telling the player where the shot will go, not
           confirming where one already went). */
        if (g_ground_target_pending_slot != 0 && my_owner >= 0 && my_owner < ARENA_MAX_HEROES) {
            int rmx, rmy;
            SDL_GetMouseState(&rmx, &rmy);
            float rgx, rgz;
            float rfocus_x = arena_state.heroes[my_owner].x, rfocus_z = arena_state.heroes[my_owner].z;
            if (screen_to_ground(rmx, rmy, win_w, win_h, 60.0f, rfocus_x, rfocus_z, &rgx, &rgz)) {
                Mat4 tr = mat4_translate(rgx, 0.03f, rgz);
                Mat4 sc = mat4_scale(0.6f, 1.0f, 0.6f);
                Mat4 model = mat4_multiply(&tr, &sc);
                Mat4 mvp = mat4_multiply(&vp, &model);
                glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
                glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
                glUniform4f_(loc_color, 0.15f, 1.0f, 0.15f, 0.85f); /* bright green, per founder's own spec */
                draw_mesh(&ring_mesh);
            }
        }
        /* attack flashes (S170-122): quick, small, orange-white burst right
           on the hit hero -- visually distinct from the slower green
           placement ring above (move-click feedback) so the two don't read
           as the same thing. */
        for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
            if (!attack_flashes[i].active) continue;
            float t01 = attack_flashes[i].age_ms / ATTACK_FLASH_LIFETIME_MS;
            float scale = 0.5f + t01 * 0.4f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(attack_flashes[i].x, 0.05f, attack_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 1.0f, 0.75f, 0.15f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* Zone-ability ground circles (S170-200, founder: "zone abilities dont read at all we
           need true aoe cast circle... show cast radius... circle on the ground... nice shader
           spell effect simple but nice showing to all participants that the spell was cast
           there so it reads"). Unlike every flash effect around this block (a brief pop that
           fades in well under a second regardless of the ability's real shape), this is the
           actual, radius-accurate footprint of a real lingering zone (Ghost/Flamel/Morrigan/
           Paimon/NOOR-1/Vassago/He Xiangu's R, or Beleth's fuse-marked detonation point) --
           drawn every frame for as long as the real server-side zone is real (r_active_ms > 0,
           synced per S170-200's own protocol.h doc comment), at its real position (r_zone_x/z)
           and real size (arena_hero_r_zone_radius) -- not the caster's current position, which
           may have long since walked away from a zone that stays fixed where it was cast.
           Drawn as a filled disc (so the AREA reads, not just an outline) plus a brighter
           boundary ring at the exact edge, both gently pulsing so an active zone reads as
           "still live," not a static decal easy to stop noticing. Every hero in the match sees
           this identically -- it's driven by synced server state, not a local-only effect. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *zh = &arena_state.heroes[i];
            if (!zh->active || !zh->alive || zh->r_active_ms <= 0) continue;
            /* S202-42: Cart is a real gap in arena_hero_r_zone_radius -- his W (delivery,
               ARENA_CART_W_RADIUS) and R (ARENA_CART_R_RADIUS) share the same r_active_ms/
               zone_radius fields (see ArenaHero.zone_radius's own doc comment on why: "which
               radius applies is genuinely ambiguous without this -- set by whichever of W/R
               most recently activated the zone"), so a fixed per-hero-id constant can't be
               right for him -- arena_hero_r_zone_radius() correctly has no CART case at all
               (returns 0.0, silently never drawing Cart's own already-active zone). zh->zone_
               radius is the real answer here, already set correctly at cast time -- read it
               directly for Cart instead of routing through the per-hero-constant function. */
            float zone_r = (zh->hero_id == ARENA_HERO_CART) ? zh->zone_radius : arena_hero_r_zone_radius(zh->hero_id);
            if (zone_r <= 0.0f) continue;
            float pulse = 0.7f + 0.3f * sinf((float)now * 0.005f);
            float zr, zg, zb;
            hero_flash_color(zh->hero_id, &zr, &zg, &zb);
            Mat4 ztr = mat4_translate(zh->r_zone_x, 0.04f, zh->r_zone_z);
            Mat4 zsc = mat4_scale(zone_r, 1.0f, zone_r);
            Mat4 zmodel = mat4_multiply(&ztr, &zsc);
            Mat4 zmvp = mat4_multiply(&vp, &zmodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, zmvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, zmodel.m);
            glUniform4f_(loc_color, zr, zg, zb, 0.16f * pulse);
            draw_mesh(&disc_mesh);
            glUniform4f_(loc_color, zr, zg, zb, 0.55f + 0.25f * pulse);
            draw_mesh(&ring_mesh);
        }
        /* Duck's Smoke Bomb (S202-10): same real, radius-accurate ground-footprint
           rendering as the zone-ability circles just above (disc + brighter boundary
           ring, gently pulsing), drawn at duck_smoke_x/z with ARENA_DUCK_W_RADIUS --
           but a flat smoke-gray rather than hero_flash_color, and noticeably more
           opaque (this is meant to read as "you cannot see into this," not just a
           cast-radius indicator). Every hero in the match sees this identically --
           driven by synced server state (duck_smoke_ms, protocol.h), not a
           local-only effect, same "the whole battlefield should read clearly"
           convention as every other zone circle here. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *sh = &arena_state.heroes[i];
            if (!sh->active || sh->duck_smoke_ms <= 0) continue;
            float pulse = 0.7f + 0.3f * sinf((float)now * 0.004f);
            Mat4 str = mat4_translate(sh->duck_smoke_x, 0.04f, sh->duck_smoke_z);
            Mat4 ssc = mat4_scale(ARENA_DUCK_W_RADIUS, 1.0f, ARENA_DUCK_W_RADIUS);
            Mat4 smodel = mat4_multiply(&str, &ssc);
            Mat4 smvp = mat4_multiply(&vp, &smodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, smvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, smodel.m);
            glUniform4f_(loc_color, 0.55f, 0.55f, 0.58f, 0.45f * pulse);
            draw_mesh(&disc_mesh);
            glUniform4f_(loc_color, 0.8f, 0.8f, 0.82f, 0.6f + 0.2f * pulse);
            draw_mesh(&ring_mesh);
        }
        /* Cast-radius preview (S170-200's own "click affordances that show cast radius" half):
           while YOUR OWN hero's R is a real zone ability and is actually castable right now
           (alive, not silenced/stunned, off cooldown, enough mana -- the exact gate arena_cast_r
           itself checks before doing anything), show a faint outline-only ring at your hero's
           own live position, at the ability's real radius, so you always know what you're about
           to commit to before pressing E -- every zone ability in this roster casts at the
           caster's own current position (no ground-click targeting exists in this input model
           at all), so "where would it land" is always simply "here," making a live self-centered
           preview the honest, buildable affordance rather than a full click-to-place targeting
           system, which would need its own separate aiming input mode and wire command. Own
           hero only (not every hero's own readiness -- that's not information a player should
           see about anyone but themselves) and suppressed while observing a replay, since there
           is no upcoming cast to preview there. */
        if (!observing) {
            ArenaHero *me_zone = &arena_state.heroes[my_owner];
            float my_zone_r = arena_hero_r_zone_radius(me_zone->hero_id);
            if (my_zone_r > 0.0f && me_zone->alive && me_zone->silenced_ms <= 0 &&
                me_zone->stunned_ms <= 0 && me_zone->r_cooldown_ms <= 0 && me_zone->mp >= ARENA_MP_COST_R) {
                Mat4 ptr = mat4_translate(me_zone->x, 0.04f, me_zone->z);
                Mat4 psc = mat4_scale(my_zone_r, 1.0f, my_zone_r);
                Mat4 pmodel = mat4_multiply(&ptr, &psc);
                Mat4 pmvp = mat4_multiply(&vp, &pmodel);
                glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
                glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
                glUniform4f_(loc_color, 0.9f, 0.9f, 0.95f, 0.35f);
                draw_mesh(&ring_mesh);
            }
        }
        /* Duck W cast-radius preview (S202-10): same reasoning as the R-zone preview
           just above, for Smoke Bomb's own W slot -- Duck's R (Total Telekinesis) has
           no zone of its own, so this doesn't collide with the block above. */
        if (!observing) {
            ArenaHero *me_smoke = &arena_state.heroes[my_owner];
            if (me_smoke->hero_id == ARENA_HERO_DUCK && me_smoke->alive &&
                me_smoke->silenced_ms <= 0 && me_smoke->stunned_ms <= 0 &&
                me_smoke->w_cooldown_ms <= 0 && me_smoke->mp >= ARENA_MP_COST_W) {
                Mat4 ptr = mat4_translate(me_smoke->x, 0.04f, me_smoke->z);
                Mat4 psc = mat4_scale(ARENA_DUCK_W_RADIUS, 1.0f, ARENA_DUCK_W_RADIUS);
                Mat4 pmodel = mat4_multiply(&ptr, &psc);
                Mat4 pmvp = mat4_multiply(&vp, &pmodel);
                glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
                glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
                glUniform4f_(loc_color, 0.8f, 0.8f, 0.82f, 0.35f);
                draw_mesh(&ring_mesh);
            }
        }
        /* Cart cast-radius previews (S202-42): same reasoning as the R-zone/Duck-W previews
           just above, but Cart is the one hero with TWO different real zone radii sharing the
           generic r_active_ms/zone_radius state (W = delivery, R = the other zone) -- see
           arena_hero_r_zone_radius's own doc comment on why a single per-hero-id constant can't
           cover him. Shows up to both rings at once (independently gated on each slot's own
           cooldown/mana), using the fixed ARENA_CART_W_RADIUS/R_RADIUS constants directly --
           correct here since these are "what WOULD this cast at" previews, before any real cast
           (and its own zone_radius) exists yet. */
        if (!observing) {
            ArenaHero *me_cart = &arena_state.heroes[my_owner];
            if (me_cart->hero_id == ARENA_HERO_CART && me_cart->alive &&
                me_cart->silenced_ms <= 0 && me_cart->stunned_ms <= 0) {
                if (me_cart->w_cooldown_ms <= 0 && me_cart->mp >= ARENA_MP_COST_W) {
                    Mat4 ptr = mat4_translate(me_cart->x, 0.04f, me_cart->z);
                    Mat4 psc = mat4_scale(ARENA_CART_W_RADIUS, 1.0f, ARENA_CART_W_RADIUS);
                    Mat4 pmodel = mat4_multiply(&ptr, &psc);
                    Mat4 pmvp = mat4_multiply(&vp, &pmodel);
                    glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
                    glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
                    glUniform4f_(loc_color, 0.9f, 0.9f, 0.95f, 0.35f);
                    draw_mesh(&ring_mesh);
                }
                if (me_cart->r_cooldown_ms <= 0 && me_cart->mp >= ARENA_MP_COST_R) {
                    Mat4 ptr = mat4_translate(me_cart->x, 0.04f, me_cart->z);
                    Mat4 psc = mat4_scale(ARENA_CART_R_RADIUS, 1.0f, ARENA_CART_R_RADIUS);
                    Mat4 pmodel = mat4_multiply(&ptr, &psc);
                    Mat4 pmvp = mat4_multiply(&vp, &pmodel);
                    glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
                    glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
                    glUniform4f_(loc_color, 0.95f, 0.85f, 0.6f, 0.35f); /* distinct warm tint from W's cool one so the two rings read as different abilities when both show at once */
                    draw_mesh(&ring_mesh);
                }
            }
        }
        /* heal flashes (S170-143): quick, warm-green burst on whoever's HP
           just went up -- the target's own visual, distinct from the
           attack flash's orange-white, the placement ring's cooler green,
           and every spell-cast color, so a heal reads as a heal at a
           glance, wherever it landed. */
        for (int i = 0; i < MAX_HEAL_FLASHES; i++) {
            if (!heal_flashes[i].active) continue;
            float t01 = heal_flashes[i].age_ms / HEAL_FLASH_LIFETIME_MS;
            float scale = 0.5f + t01 * 0.5f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(heal_flashes[i].x, 0.05f, heal_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.35f, 1.0f, 0.45f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* fold flashes (S170-210): a bright gold-white burst, bigger and slower to fade
           than the heal flash above -- Immortal's Fold procs at a moment the wearer is
           about to die, so it needs to read as more urgent than a routine heal tick. */
        for (int i = 0; i < MAX_FOLD_FLASHES; i++) {
            if (!fold_flashes[i].active) continue;
            float t01 = fold_flashes[i].age_ms / FOLD_FLASH_LIFETIME_MS;
            float scale = 0.7f + t01 * 1.1f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(fold_flashes[i].x, 0.05f, fold_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 1.0f, 0.85f, 0.25f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* Ghost Q lightning bursts (founder: "...showing where the spell hit"): the
           impact half of the crackle effect -- where the in-flight crackle above is a
           tight zigzag riding the shot itself, this is a bigger radial burst of the
           same jittered box-sliver look, fired once at the exact spot the shot
           disappeared (see spawn_lightning_burst's own call site) and expanding/fading
           out over its lifetime. Deliberately not ring_mesh like every flash above --
           a flat disc reads as a generic pop; radiating electric slivers read as a
           lightning strike specifically, distinct from the plain orange-white
           attack_flash every other ability's hit already produces. */
        for (int i = 0; i < MAX_LIGHTNING_BURSTS; i++) {
            if (!lightning_bursts[i].active) continue;
            float t01 = lightning_bursts[i].age_ms / LIGHTNING_BURST_LIFETIME_MS;
            float alpha = 1.0f - t01;
            float spread = 0.3f + t01 * 1.1f;
            glUniform4f_(loc_color, 0.65f, 0.95f, 1.0f, alpha);
            for (int seg = 0; seg < 8; seg++) {
                float burst_angle = ((float)seg / 8.0f) * 2.0f * (float)M_PI +
                                     ((float)(rand() % 100) / 100.0f - 0.5f) * 0.6f;
                float bx = cosf(burst_angle) * spread;
                float bz = sinf(burst_angle) * spread;
                draw_hero_box_facing(lightning_bursts[i].x, lightning_bursts[i].z, burst_angle,
                                      bx * 0.5f, 0.15f, bz * 0.5f, 0.04f, 0.04f, spread * 0.5f, 1.0f,
                                      &vp, loc_mvp, loc_model, &cube_mesh);
            }
        }
        /* spell flashes (S170-124, per-hero color S170-142): SIZE still
           ramps by ability tier (Q small, W bigger, R biggest, same
           low-basic to high-ultimate shape any real MOBA uses), but COLOR
           now comes from hero_flash_color(hero_id) instead of the slot --
           26 heroes' worth of Q casts no longer all look like the same
           identical cyan circle; each hero's cast reads as genuinely its
           own spell, "which slot" is now legible size, "whose spell" is
           legible color. */
        for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
            if (!spell_flashes[i].active) continue;
            float t01 = spell_flashes[i].age_ms / SPELL_FLASH_LIFETIME_MS;
            float alpha = 1.0f - t01;
            float base_scale, rr, gg, bb;
            switch (spell_flashes[i].slot) {
                case 1: base_scale = 0.6f; break;  /* Q: smallest */
                case 2: base_scale = 0.8f; break;  /* W: bigger */
                default: base_scale = 1.1f; break; /* R: biggest */
            }
            hero_flash_color(spell_flashes[i].hero_id, &rr, &gg, &bb);
            float scale = base_scale + t01 * 0.6f;
            Mat4 tr = mat4_translate(spell_flashes[i].x, 0.08f, spell_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, rr, gg, bb, alpha);
            draw_mesh(&ring_mesh);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* ---- 2D HUD pass (legacy immediate mode, compatibility profile) ---- */
        glUseProgram_(0);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, win_w, 0, win_h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Enhanced cursor hover state (S170-69 revisited): which hero, if any, the mouse is
           currently over, and its screen-space bar position -- found in the same pass as the
           health bars below (cheapest place to do it, world_to_screen already runs there for
           every hero) and consumed just after the loop to draw a highlight + tooltip on top of
           everything. SDL mouse Y is top-down; world_to_screen's sy is bottom-up (matches this
           HUD's own glOrtho), same flip the OK-button hit test already uses. */
        int raw_mx, raw_my;
        SDL_GetMouseState(&raw_mx, &raw_my);
        float mouse_hx = (float)raw_mx, mouse_hy = (float)(win_h - raw_my);
        int hovered_i = -1;
        float hovered_sx = 0, hovered_sy = 0;
        float hovered_best_dist_sq = 30.0f * 30.0f; /* hover radius */

        /* Per-hero floating health bars (S170-89: "health bar hovers over hero") -- every
           alive hero, not just YOU/nearest-enemy's fixed HUD bars, so a 20-hero team match
           actually shows damage landing on whoever's in view. Reuses the same vp matrix the
           3D pass just drew with, projected into this 2D HUD's pixel space.
           2026-07-30: widened to ARENA_HEROES_ARRAY_SIZE so Tyler's clones get real health bars
           too -- safe here (unlike the hero-model draw loop above) since this loop only ever
           reads arena_state.heroes[] fields directly, no separate ARENA_MAX_HEROES-sized side
           array to worry about. */
        for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            float sx, sy;
            if (!world_to_screen(&vp, h->x, 1.6f, h->z, win_w, win_h, &sx, &sy)) continue;
            if (sx < -40 || sx > win_w + 40 || sy < -20 || sy > win_h + 20) continue;
            /* is_mine (2026-07-30): "this is a unit I pilot" -- true for my own real hero (the
               only way this could ever be true before clones existed) OR one of my own active
               Tyler clones, so a clone I command gets the same "this is mine" cyan treatment my
               own body already gets, not the generic ally-blue every teammate's clone also uses. */
            int is_mine = (i == my_owner) || (h->is_clone && h->clone_owner == my_owner);
            float frac = h->max_hp > 0 ? (float)h->hp / h->max_hp : 0.0f;
            float bw = 40.0f, bh = 5.0f;
            glColor3f(0.1f, 0.1f, 0.1f);
            glBegin(GL_QUADS);
            glVertex2f(sx - bw / 2, sy); glVertex2f(sx + bw / 2, sy);
            glVertex2f(sx + bw / 2, sy + bh); glVertex2f(sx - bw / 2, sy + bh);
            glEnd();
            if (is_mine) glColor3f(0.1f, 0.8f, 0.95f);
            else if (h->team == arena_state.heroes[my_owner].team) glColor3f(0.15f, 0.55f, 0.95f);
            else glColor3f(0.9f, 0.25f, 0.15f);
            glBegin(GL_QUADS);
            glVertex2f(sx - bw / 2, sy); glVertex2f(sx - bw / 2 + bw * frac, sy);
            glVertex2f(sx - bw / 2 + bw * frac, sy + bh); glVertex2f(sx - bw / 2, sy + bh);
            glEnd();
            /* Cast bar (S170-203, founder: "switch gary w to aimed shot just like wow hunter
               cast time" -> "ensure cast bar affordance shown to user"). Drawn for ANY hero
               currently casting -- Gary's Aimed Shot is the first ability to ever set
               casting_slot, not the only one this is meant to support. Below the health bar
               (screen space here is Y-up, so "below" is sy MINUS the bar height, not plus) so
               it doesn't collide with the name/status stack already above it. Visible to every
               hero watching, not just the caster -- same "reads to the whole battlefield"
               convention every other cast/status affordance in this file already holds itself
               to; a cast bar only the caster can see isn't the affordance that was asked for. */
            if (h->casting_slot != 0 && h->cast_total_ms > 0) {
                float cast_frac = 1.0f - (float)h->cast_time_remaining_ms / (float)h->cast_total_ms;
                if (cast_frac < 0.0f) cast_frac = 0.0f;
                if (cast_frac > 1.0f) cast_frac = 1.0f;
                float cbh = 4.0f;
                float cby = sy - cbh - 2.0f;
                glColor3f(0.1f, 0.1f, 0.1f);
                glBegin(GL_QUADS);
                glVertex2f(sx - bw / 2, cby); glVertex2f(sx + bw / 2, cby);
                glVertex2f(sx + bw / 2, cby + cbh); glVertex2f(sx - bw / 2, cby + cbh);
                glEnd();
                glColor3f(0.95f, 0.8f, 0.2f); /* gold -- distinct from HP's relationship color, the real WoW cast-bar convention this ability is modeled on */
                glBegin(GL_QUADS);
                glVertex2f(sx - bw / 2, cby); glVertex2f(sx - bw / 2 + bw * cast_frac, cby);
                glVertex2f(sx - bw / 2 + bw * cast_frac, cby + cbh); glVertex2f(sx - bw / 2, cby + cbh);
                glEnd();
                glColor3f(0.95f, 0.85f, 0.4f);
                draw_string(arena_ability_name(h->hero_id, h->casting_slot - 1), sx - bw / 2, cby - 10.0f, 7);
            }
            /* S170-162, founder: "up our visual affordances for auto
               attacks so its readable" / "auto target should still have
               visual affordances." A pulsing amber outline around the
               health bar of anyone CURRENTLY locked as someone's
               attack_target (synced per-hero now, protocol.h's own doc
               comment) -- reads to every hero watching the fight, not just
               the two actually involved, same "legible to the whole
               battlefield" bar this session's other status affordances
               (rooted name color, cast flashes) already hold themselves
               to. O(hero count) extra scan per hero, cheap at this
               roster's real size (<=20). */
            for (int a = 0; a < ARENA_HEROES_ARRAY_SIZE; a++) {
                ArenaHero *attacker = &arena_state.heroes[a];
                if (a == i || !attacker->active || !attacker->alive) continue;
                if (attacker->attack_target != i) continue;
                float pulse = 0.6f + 0.4f * sinf((float)now * 0.008f);
                glColor4f(1.0f, 0.75f, 0.15f, pulse);
                glLineWidth(2.0f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(sx - bw / 2 - 1.5f, sy - 1.5f);
                glVertex2f(sx + bw / 2 + 1.5f, sy - 1.5f);
                glVertex2f(sx + bw / 2 + 1.5f, sy + bh + 1.5f);
                glVertex2f(sx - bw / 2 - 1.5f, sy + bh + 1.5f);
                glEnd();
                glLineWidth(1.0f);
                break;
            }
            /* S170-96: name label above the bar -- with 17+ heroes in the
               roster now, a colored bar alone doesn't say who's who at a
               glance. arena_hero_name() is the same token vocabulary the
               Game AI bridge already uses (lowercase, e.g. "morrigan"),
               reused here rather than inventing a separate display-name
               table. draw_string's own size param is roughly the glyph
               height in pixels; centered by eye against the bar width,
               not measured -- good enough for a short lowercase token. */
            /* Rooted name-label color override (founder: "when the hero is rooted change the
               color of their name label to green"): wins over the usual self/ally/enemy
               relationship color for this one draw call only -- rootedness is a battlefield-wide
               readable state (matches the existing status-effect label a few lines below, which
               already surfaces "ROOTED" as text), so a glance at the name color alone should say
               it too, without needing to read the smaller status line above it. */
            if (h->rooted_ms > 0) glColor3f(0.25f, 0.95f, 0.35f);
            draw_string(arena_hero_name(h->hero_id), sx - bw / 2, sy + bh + 2.0f, 10);
            if (h->rooted_ms > 0) {
                if (is_mine) glColor3f(0.1f, 0.8f, 0.95f);
                else if (h->team == arena_state.heroes[my_owner].team) glColor3f(0.15f, 0.55f, 0.95f);
                else glColor3f(0.9f, 0.25f, 0.15f);
            }

            /* Status-effect label (S170-133): a further line above the name, only drawn when
               something's actually active -- most heroes most ticks have nothing to show, and an
               always-present empty line would just be clutter. */
            char status_buf[64];
            if (hero_status_label(h, status_buf, sizeof(status_buf))) {
                glColor3f(0.95f, 0.75f, 0.15f);
                draw_string(status_buf, sx - bw / 2, sy + bh + 14.0f, 9);
                if (is_mine) glColor3f(0.1f, 0.8f, 0.95f);
                else if (h->team == arena_state.heroes[my_owner].team) glColor3f(0.15f, 0.55f, 0.95f);
                else glColor3f(0.9f, 0.25f, 0.15f);
            }

            float hdx = mouse_hx - sx, hdy = mouse_hy - (sy + bh / 2);
            float hdist_sq = hdx * hdx + hdy * hdy;
            if (hdist_sq < hovered_best_dist_sq) {
                hovered_best_dist_sq = hdist_sq;
                hovered_i = i;
                hovered_sx = sx;
                hovered_sy = sy;
            }
        }
        g_hover_target = hovered_i; /* S170-143: publish this frame's hover result for the QWE keybind handler to read next frame */
        if (hovered_i < 0) SDL_SetCursor(cursor_default); /* S170-69: hovering empty ground/terrain -- no lingering crosshair from a previous hover */

        /* King health bars + name tags, 2026-08-20. Founder, real-time: "the 4 kings need
           health bars and name tags." Same world_to_screen + glRectf HP-bar idiom heroes use
           just above, scaled up (wider/taller bar) to read as boss-tier at a glance rather than
           blending into hero-sized UI, name drawn in that King's own thematic color (matching
           the boss-model colors from S188-01) instead of the self/ally/enemy relationship color
           heroes use -- a King belongs to neither team, same reasoning camp minions' neutral
           olive body color already uses. */
        {
            static const char *king_names[ARENA_CAMP_COUNT] = {
                "WEALTH KING", "GROWTH KING", "MUSIC KING", "ALL-SEEING KING"
            };
            static const float king_hud_color[ARENA_CAMP_COUNT][3] = {
                {0.85f, 0.7f, 0.15f}, {0.25f, 0.75f, 0.2f}, {0.75f, 0.2f, 0.75f}, {0.2f, 0.6f, 0.85f}
            };
            for (int ki = 0; ki < ARENA_CAMP_COUNT; ki++) {
                ArenaKing *k = &arena_state.kings[ki];
                if (!k->active || !k->alive) continue;
                float ksx, ksy;
                if (!world_to_screen(&vp, k->x, 2.4f, k->z, win_w, win_h, &ksx, &ksy)) continue;
                if (ksx < -60 || ksx > win_w + 60 || ksy < -20 || ksy > win_h + 20) continue;
                float kfrac = k->max_hp > 0 ? (float)k->hp / k->max_hp : 0.0f;
                float kbw = 80.0f, kbh = 8.0f;
                glColor3f(0.1f, 0.1f, 0.1f);
                glBegin(GL_QUADS);
                glVertex2f(ksx - kbw / 2, ksy); glVertex2f(ksx + kbw / 2, ksy);
                glVertex2f(ksx + kbw / 2, ksy + kbh); glVertex2f(ksx - kbw / 2, ksy + kbh);
                glEnd();
                glColor3f(king_hud_color[ki][0], king_hud_color[ki][1], king_hud_color[ki][2]);
                glBegin(GL_QUADS);
                glVertex2f(ksx - kbw / 2, ksy); glVertex2f(ksx - kbw / 2 + kbw * kfrac, ksy);
                glVertex2f(ksx - kbw / 2 + kbw * kfrac, ksy + kbh); glVertex2f(ksx - kbw / 2, ksy + kbh);
                glEnd();
                draw_string(king_names[ki], ksx - kbw / 2, ksy + kbh + 3.0f, 12);
            }
        }

        /* Procedural minimap (S181-04, RENDERING_QUALITY_NORTHSTAR.md's #1
         * priority item -- "the single biggest visual gap versus the Duck
         * reference... no minimap exists at all today"). Top-right circular
         * radar: terrain-colored disc, bordered ring, a dot per alive hero
         * (same relationship-color convention the health bars above use)
         * and per ArenaNode (owner-colored). World position -> minimap
         * position via ARENA_HALF_EXTENT (the real map half-extent,
         * S170-191's golden-ratio scale), clamped to the circle radius so
         * a hero out past the normal play area doesn't plot outside the
         * minimap's own border. Immediate-mode GL_TRIANGLE_FAN/GL_LINE_LOOP,
         * matching this same 2D HUD pass's existing ability-pie-timer/ring
         * drawing convention -- no new VBO/mesh needed for something drawn
         * once per frame in ortho pixel space. */
        {
            float mm_cx = win_w - 74.0f;
            float mm_cy = win_h - 74.0f;
            float mm_r = 60.0f;

            glColor4f(0.24f, 0.26f, 0.15f, 0.88f);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(mm_cx, mm_cy);
            for (int mi = 0; mi <= 32; mi++) {
                float a = (float)mi / 32.0f * 2.0f * (float)M_PI;
                glVertex2f(mm_cx + cosf(a) * mm_r, mm_cy + sinf(a) * mm_r);
            }
            glEnd();

            glColor3f(0.78f, 0.72f, 0.5f);
            glLineWidth(2.0f);
            glBegin(GL_LINE_LOOP);
            for (int mi = 0; mi < 32; mi++) {
                float a = (float)mi / 32.0f * 2.0f * (float)M_PI;
                glVertex2f(mm_cx + cosf(a) * mm_r, mm_cy + sinf(a) * mm_r);
            }
            glEnd();

            int my_team = arena_state.heroes[my_owner].team;
            for (int n = 0; n < ARENA_NODE_COUNT; n++) {
                ArenaNode *node = &arena_state.nodes[n];
                float nx = node->x / ARENA_HALF_EXTENT;
                float nz = node->z / ARENA_HALF_EXTENT;
                float ndist = sqrtf(nx * nx + nz * nz);
                if (ndist > 1.0f) { nx /= ndist; nz /= ndist; }
                float px = mm_cx + nx * mm_r;
                float py = mm_cy + nz * mm_r;
                if (node->owner == 0) glColor3f(0.6f, 0.6f, 0.6f);
                else if (node->owner == my_team + 1) glColor3f(0.15f, 0.55f, 0.95f);
                else glColor3f(0.9f, 0.25f, 0.15f);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(px, py);
                for (int mi = 0; mi <= 8; mi++) {
                    float a = (float)mi / 8.0f * 2.0f * (float)M_PI;
                    glVertex2f(px + cosf(a) * 3.0f, py + sinf(a) * 3.0f);
                }
                glEnd();
            }

            for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
                ArenaHero *h = &arena_state.heroes[i];
                if (!h->alive) continue;
                float hx = h->x / ARENA_HALF_EXTENT;
                float hz = h->z / ARENA_HALF_EXTENT;
                float hdist2 = sqrtf(hx * hx + hz * hz);
                if (hdist2 > 1.0f) { hx /= hdist2; hz /= hdist2; }
                float px = mm_cx + hx * mm_r;
                float py = mm_cy + hz * mm_r;
                int is_mine = (i == my_owner) || (h->is_clone && h->clone_owner == my_owner);
                if (is_mine) glColor3f(0.1f, 0.8f, 0.95f);
                else if (h->team == my_team) glColor3f(0.15f, 0.55f, 0.95f);
                else glColor3f(0.9f, 0.25f, 0.15f);
                float dot_r = is_mine ? 4.0f : 3.0f;
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(px, py);
                for (int mi = 0; mi <= 8; mi++) {
                    float a = (float)mi / 8.0f * 2.0f * (float)M_PI;
                    glVertex2f(px + cosf(a) * dot_r, py + sinf(a) * dot_r);
                }
                glEnd();
            }
        }
        if (hovered_i >= 0) {
            ArenaHero *hh = &arena_state.heroes[hovered_i];
            float bw = 40.0f, bh = 5.0f;
            /* Relationship color, same convention as the bar fill above --
               self/ally/enemy read identically everywhere in this HUD. */
            float rr, gg, bb;
            const char *relation;
            if (hovered_i == my_owner) { rr = 0.1f; gg = 0.8f; bb = 0.95f; relation = "YOU"; }
            else if (hh->team == arena_state.heroes[my_owner].team) { rr = 0.15f; gg = 0.55f; bb = 0.95f; relation = "ALLY"; }
            else { rr = 0.95f; gg = 0.25f; bb = 0.15f; relation = "ENEMY"; }
            /* S170-69: crosshair over a live enemy (a real, hittable click-to-attack target),
               default arrow over anything else -- self, an ally, or a dead enemy corpse aren't
               valid attack targets, so the cursor shouldn't imply one. */
            SDL_SetCursor((relation[0] == 'E' && hh->alive) ? cursor_enemy : cursor_default);

            /* Bracket outline around the bar -- distinct from the bar's own
               border (which is always drawn, hover or not): a wider,
               brighter box just outside it. */
            glColor3f(rr, gg, bb);
            glLineWidth(2.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(hovered_sx - bw / 2 - 3, hovered_sy - 3);
            glVertex2f(hovered_sx + bw / 2 + 3, hovered_sy - 3);
            glVertex2f(hovered_sx + bw / 2 + 3, hovered_sy + bh + 3);
            glVertex2f(hovered_sx - bw / 2 - 3, hovered_sy + bh + 3);
            glEnd();

            /* Tooltip near the cursor: relationship + name + real HP numbers,
               not just the bar's fractional fill. */
            char tip[64];
            snprintf(tip, sizeof(tip), "%s - %s (%d/%d)", relation, arena_hero_name(hh->hero_id), hh->hp, hh->max_hp);
            glColor3f(rr, gg, bb);
            draw_string(tip, mouse_hx + 14.0f, mouse_hy + 6.0f, 11);
        }

        glColor3f(0.1f, 0.8f, 0.95f);
        draw_string("YOU", 20, win_h - 40.0f, 14);
        glColor3f(1.0f, 1.0f, 1.0f);
        {
            ArenaHero *h = &arena_state.heroes[my_owner];
            float frac = (float)h->hp / h->max_hp;
            glColor3f(0.2f, 0.2f, 0.2f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(290, win_h - 38.0f);
            glVertex2f(290, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
            glColor3f(0.1f, 0.9f, 0.3f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(90 + 200 * frac, win_h - 38.0f);
            glVertex2f(90 + 200 * frac, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
            /* S170-148 ("mana as a resource should be visible to the player"):
               a real persistent mana bar, not just the ability tiles' occasional
               "MP" text when a cast is blocked -- sits in the existing gap
               between the HP bar and the enemy/bot bar below, no other HUD
               coordinates need to move. Dims toward grey while in combat
               (combat_timer_ms > 0) so the "why isn't this refilling" question
               the gate itself creates has a visible answer right on the bar,
               not just implied by it staying still. */
            /* ARENA_MP_MAX, not h->max_mp -- max_mp is deliberately not part of
               the wire snapshot (it's flat/roster-wide, see ArenaHeroSnapshot's
               own doc comment), so a net_mode hero's local max_mp field is
               never populated and would silently read 0. */
            float mp_frac = (float)h->mp / (float)ARENA_MP_MAX;
            glColor3f(0.15f, 0.15f, 0.2f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 48.0f); glVertex2f(290, win_h - 48.0f);
            glVertex2f(290, win_h - 40.0f); glVertex2f(90, win_h - 40.0f);
            glEnd();
            if (h->combat_timer_ms > 0) glColor3f(0.25f, 0.35f, 0.55f); /* in combat: dim, not regenerating */
            else glColor3f(0.25f, 0.55f, 1.0f); /* out of combat: bright, actively regenerating */
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 48.0f); glVertex2f(90 + 200 * mp_frac, win_h - 48.0f);
            glVertex2f(90 + 200 * mp_frac, win_h - 40.0f); glVertex2f(90, win_h - 40.0f);
            glEnd();
        }
        glColor3f(0.95f, 0.25f, 0.15f);
        draw_string(net_mode ? "NEAREST ENEMY" : "BOT", 20, win_h - 70.0f, 14);
        {
            /* heroes[1 - my_owner] only ever made sense for exactly 2 heroes (1v1) -- in
               team mode (S170-79 finding, real bug, not cosmetic) it either mislabels a
               teammate as ENEMY (heroes[1] is always team 0 same as heroes[0] for
               my_owner==0) or reads a negative out-of-bounds index for any my_owner > 1.
               arena_nearest_enemy() is the real team-aware lookup already used server-side. */
            ArenaHero *h = net_mode ? arena_nearest_enemy(my_owner) : &arena_state.heroes[1 - my_owner];
            if (h) {
                float frac = (float)h->hp / h->max_hp;
                glColor3f(0.2f, 0.2f, 0.2f);
                glBegin(GL_QUADS);
                glVertex2f(90, win_h - 68.0f); glVertex2f(290, win_h - 68.0f);
                glVertex2f(290, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
                glEnd();
                glColor3f(0.9f, 0.3f, 0.1f);
                glBegin(GL_QUADS);
                glVertex2f(90, win_h - 68.0f); glVertex2f(90 + 200 * frac, win_h - 68.0f);
                glVertex2f(90 + 200 * frac, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
                glEnd();
            }
        }

        if (net_mode && net_lobby_size > 2) {
            /* S170-153 ("true arathi basin node control resource management
               as a win con instead of team wipe"): the resource race is the
               actual win condition now, so it needs to be as visible as the
               HP/MP bars above -- a tug-of-war bar top-center (classic
               Arathi Basin resource-bar convention). Physical layout stays
               fixed (team 0's number always on the left, team 1's always on
               the right, matching the map's own -x/+x base layout), but the
               FILL COLOR is perspective-relative -- founder, real-time,
               caught live: "i think the color of the bar ticking up may
               just be wrong." It was: team 0 was hardcoded blue and team 1
               hardcoded red regardless of which team the local viewer is
               actually on, the exact same absolute-vs-relative mistake
               S170-149 already found and fixed for node coloring (that
               fix's own comment: "node coloring was absolute...while hero
               coloring is perspective-relative"). A team-1 player watching
               their OWN progress bar climb saw it in "enemy" red -- readable
               as the enemy winning, not as their own team's progress. Now
               colored the same way hero name labels already are: MY team's
               fill is always blue, the opponent's is always red, regardless
               of which raw team index either side actually is. Gated to net
               team matches only: local arena_update() 1v1 and 2-player net
               matches both run the non-team sim, which never populates
               resources[] (stays 0/0), so the bar would be meaningless
               there. */
            int my_team = (my_owner >= 0 && my_owner < ARENA_MAX_HEROES) ? arena_state.heroes[my_owner].team : 0;
            float bar_w = 360.0f, bar_h = 16.0f;
            float bar_x = win_w / 2.0f - bar_w / 2.0f;
            float bar_y = win_h - 20.0f;
            float frac0 = (float)arena_state.resources[0] / (float)ARENA_RESOURCE_CAP;
            float frac1 = (float)arena_state.resources[1] / (float)ARENA_RESOURCE_CAP;
            if (frac0 < 0.0f) frac0 = 0.0f;
            if (frac0 > 1.0f) frac0 = 1.0f;
            if (frac1 < 0.0f) frac1 = 0.0f;
            if (frac1 > 1.0f) frac1 = 1.0f;

            glColor3f(0.12f, 0.12f, 0.15f);
            glRectf(bar_x, bar_y - bar_h, bar_x + bar_w, bar_y);

            if (my_team == 0) glColor3f(0.25f, 0.55f, 1.0f); else glColor3f(1.0f, 0.3f, 0.25f); /* team 0's fill, colored relative to viewer */
            glRectf(bar_x, bar_y - bar_h, bar_x + bar_w * 0.5f * frac0, bar_y);
            if (my_team == 1) glColor3f(0.25f, 0.55f, 1.0f); else glColor3f(1.0f, 0.3f, 0.25f); /* team 1's fill, colored relative to viewer */
            glRectf(bar_x + bar_w - bar_w * 0.5f * frac1, bar_y - bar_h, bar_x + bar_w, bar_y);

            glColor3f(0.6f, 0.65f, 0.7f);
            glBegin(GL_LINES);
            glVertex2f(bar_x + bar_w / 2.0f, bar_y - bar_h); glVertex2f(bar_x + bar_w / 2.0f, bar_y);
            glEnd();

            char resbuf[32];
            if (my_team == 0) glColor3f(0.6f, 0.8f, 1.0f); else glColor3f(1.0f, 0.6f, 0.55f);
            snprintf(resbuf, sizeof(resbuf), "%d", arena_state.resources[0]);
            draw_string(resbuf, bar_x - 36.0f, bar_y - bar_h + 3.0f, 11);
            if (my_team == 1) glColor3f(0.6f, 0.8f, 1.0f); else glColor3f(1.0f, 0.6f, 0.55f);
            snprintf(resbuf, sizeof(resbuf), "%d", arena_state.resources[1]);
            draw_string(resbuf, bar_x + bar_w + 8.0f, bar_y - bar_h + 3.0f, 11);
        }

        /* King buff status HUD, 2026-08-20. Founder, real-time: "i killed the purple king and
           couldnt even tell if i got a buff" -- root cause was that the buff state itself never
           reached the client at all (ArenaHeroSnapshot's own doc comment), not just a missing
           widget; this is the widget half now that the data exists. Bottom-right per founder's
           own "buff frames in the bottom right" -- one square tile per active King buff, stacked
           upward, in that King's own thematic color (matching the boss-model colors from S188-01:
           gold/green/magenta/cyan for Wealth/Growth/Music/All-Seeing) so a player who missed the
           kill can still tell WHICH King they picked up from color alone. Deliberately reuses
           the resource bar's plain glRectf+draw_string idiom, not draw_ability_tile's radial-wipe
           machinery -- these are persistent on/off buffs with no cooldown to animate. */
        if (my_owner >= 0 && my_owner < ARENA_MAX_HEROES) {
            ArenaHero *me = &arena_state.heroes[my_owner];
            struct { int active; float r, g, b; const char *label; int stacks; } buffs[4] = {
                { me->king_wealth_ms > 0,        0.85f, 0.7f,  0.15f, "BULWARK",     0 },
                { me->king_growth_ms > 0,        0.25f, 0.75f, 0.2f,  "BLOODROAR",   me->king_growth_stacks },
                { me->king_music_carrier,        0.75f, 0.2f,  0.75f, "CATCHY SONG", 0 },
                { me->king_allseeing_display > 0,0.2f,  0.6f,  0.85f, "FARSIGHT",    0 },
            };
            float tile = 44.0f, gap = 8.0f;
            float bx = win_w - tile - 16.0f;
            float by = 16.0f;
            int shown = 0;
            for (int bi = 0; bi < 4; bi++) {
                if (!buffs[bi].active) continue;
                float ty = by + shown * (tile + gap);
                glColor4f(buffs[bi].r * 0.5f, buffs[bi].g * 0.5f, buffs[bi].b * 0.5f, 0.9f);
                glRectf(bx, ty, bx + tile, ty + tile);
                glColor3f(buffs[bi].r, buffs[bi].g, buffs[bi].b);
                glBegin(GL_LINE_LOOP);
                glVertex2f(bx, ty); glVertex2f(bx + tile, ty);
                glVertex2f(bx + tile, ty + tile); glVertex2f(bx, ty + tile);
                glEnd();
                draw_string(buffs[bi].label, bx - 6.0f, ty + tile + 4.0f, 7);
                if (buffs[bi].stacks > 0) {
                    char stackbuf[8];
                    snprintf(stackbuf, sizeof(stackbuf), "%d", buffs[bi].stacks);
                    draw_string(stackbuf, bx + tile / 2.0f - 4.0f, ty + tile / 2.0f - 5.0f, 12);
                }
                shown++;
            }
        }

        {
            /* Own hero's kit status -- real Overwatch-style recast-time tiles (S170-127,
               "add the ability frame cooldown timer tiles from shankpit og engine as recast
               time affordances" -> "make it like overwatch recast frames for q w e"). Ported
               the tile visual language from SHANKPIT's apps/lobby/src/main.c
               draw_ability_one_tile() (bordered square, background/border color swap on
               cooldown, big centered countdown number, keybind label) and added a real radial
               wipe on top -- REDGARDEN has 3 slots with very different cooldown lengths across
               19 heroes, not SHANKPIT's single fixed-cooldown ability, so a flat color tint
               alone doesn't show *how much* cooldown is left the way Overwatch's ability icons
               do. No per-hero max-cooldown table exists client-side to compute that fraction
               against, so it's tracked locally instead: remember the highest cooldown_ms value
               seen since it last hit 0 (arms the instant a cast starts it counting down from
               its real peak) and wipe the fraction of that peak still remaining -- self-
               correcting per-hero-per-slot with no new wire data needed.

               S170-137: readiness is no longer cooldown-only. `mp` reaches the client now
               (net_poll_snapshots, protocol.h's ArenaHeroSnapshot) instead of sitting zeroed
               forever in net_mode, so each tile can flag "off cooldown but can't actually
               afford it" against this slot's own flat ARENA_MP_COST_*. */
            ArenaHero *h = &arena_state.heroes[my_owner];
            /* S170-151, founder: "move the cast frames bottom center" --
               same real MOBA convention (LoL/Dota both anchor the ability
               bar bottom-center) this HUD's old top-left placement didn't
               follow. Retime countdown (the radial wipe + seconds-remaining
               text) and the mana_blocked dark/"MP" state are unchanged --
               both already existed (S170-127/137), this is a pure
               reposition, not new tile behavior. */
            float tile_size = 56.0f;
            float tile_pitch = 66.0f; /* size + 10px gap, unchanged from before */
            float tiles_total_w = tile_pitch * 2.0f + tile_size;
            float tiles_x0 = win_w / 2.0f - tiles_total_w / 2.0f;
            float tiles_y = 90.0f; /* near the bottom edge, leaving room below for the keybind/name labels */
            draw_ability_tile(tiles_x0, tiles_y, tile_size, h->q_cooldown_ms, &q_cooldown_peak_ms,
                               0, h->mp < ARENA_MP_COST_Q, "Q", arena_ability_name(h->hero_id, 0), 0.3f, 0.7f, 1.0f);
            /* S170-181: toggle heroes only need mp > 0 to activate (drained over time, not
               charged up front); the instant-effect W heroes (Ghost, Frog, etc.) still pay
               the old flat ARENA_MP_COST_W, so the tile's own "can I afford this" gate has to
               ask which mana-cost model this hero's W actually uses. */
            int w_mana_blocked = arena_hero_w_is_toggle(h->hero_id)
                                      ? (!h->w_active && h->mp <= 0)
                                      : (!h->w_active && h->mp < ARENA_MP_COST_W);
            /* S170-203: OR'd with casting_slot != 0 so Gary's W tile highlights while his Aimed
               Shot is mid-cast, same "active" affordance the R tile already gives r_active_ms --
               w_active itself stays permanently 0 for him now (he's not in the toggle list
               anymore), so this only ever adds the new condition, never changes behavior for
               any hero whose W really is a toggle. */
            draw_ability_tile(tiles_x0 + tile_pitch, tiles_y, tile_size, h->w_cooldown_ms, &w_cooldown_peak_ms,
                               h->w_active || h->casting_slot != 0, w_mana_blocked, "W", arena_ability_name(h->hero_id, 1), 0.7f, 0.3f, 1.0f);
            draw_ability_tile(tiles_x0 + tile_pitch * 2.0f, tiles_y, tile_size, h->r_cooldown_ms, &r_cooldown_peak_ms,
                               h->r_active_ms > 0, h->mp < ARENA_MP_COST_R, "E", arena_ability_name(h->hero_id, 2), 1.0f, 0.85f, 0.2f);
            /* Blink Dagger (S170-205, founder: "add blink dagger 1400 flow it gives a new
               keybind on screen for tilda"): a 4th tile, separate from the Q/W/E row's own
               fixed-width layout (tile_pitch * 3.0f, one pitch past E) -- only drawn while the
               local player actually has it equipped, same "the affordance you're looking at is
               the one the key acts on" precedent, not a permanently-visible empty slot for an
               item most heroes will never buy. Never mana-blocked (items in this catalog don't
               cost mana to use, only Flow to buy) and never shows an "active" highlight (an
               instant reposition has no sustained active state to highlight, same as Q). */
            if (h->equipped_item[ARENA_ITEM_SLOT_TRINKET] == ARENA_BLINK_DAGGER_ITEM_ID) {
                draw_ability_tile(tiles_x0 + tile_pitch * 3.0f, tiles_y, tile_size, h->blink_cooldown_ms, &blink_cooldown_peak_ms,
                                   0, 0, "~", "BLINK DAGGER", 0.55f, 0.85f, 0.95f);
            }
            /* Donkey (S170-206, same tilde key as Blink Dagger, same "only drawn while equipped"
               precedent -- see this same block's own doc comment two lines up). Different item
               slot (Back, not Trinket) than Blink Dagger, so a hero could in principle show both
               tiles at once if they somehow bought both -- tile_pitch * 4.0f, one slot further
               right, so they don't overlap. */
            if (h->equipped_item[ARENA_ITEM_SLOT_BACK] == ARENA_DONKEY_ITEM_ID) {
                draw_ability_tile(tiles_x0 + tile_pitch * 4.0f, tiles_y, tile_size, h->donkey_glide_cooldown_ms, &donkey_glide_cooldown_peak_ms,
                                   0, 0, "~", "PAPER GLIDE", 0.75f, 0.85f, 0.95f);
            }

            /* Ability-help overlay (S170-151, "H should show an overlay with
               character ability descriptions"): a real quick-reference panel,
               not just the tiles' own terse ability-name labels -- the
               description text (arena_ability_description) needed the S170-151
               font-glyph pass right above this feature specifically so it
               wouldn't fall through to the missing-glyph box mid-panel. Drawn
               above the ability tiles it documents, toggled by H, works in any
               mode (net or local) since it's read-only against the local
               player's own already-known hero_id. */
            if (show_ability_help) {
                float panel_w = 640.0f, panel_h = 190.0f;
                float panel_x = win_w / 2.0f - panel_w / 2.0f;
                float panel_y = tiles_y + tile_size + 30.0f;
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(0.05f, 0.08f, 0.1f, 0.88f);
                glRectf(panel_x, panel_y, panel_x + panel_w, panel_y + panel_h);
                glColor4f(0.4f, 0.55f, 0.65f, 0.9f);
                glLineWidth(2.0f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(panel_x, panel_y); glVertex2f(panel_x + panel_w, panel_y);
                glVertex2f(panel_x + panel_w, panel_y + panel_h); glVertex2f(panel_x, panel_y + panel_h);
                glEnd();
                glLineWidth(1.0f);
                glDisable(GL_BLEND);

                glColor3f(0.9f, 0.95f, 1.0f);
                draw_string(arena_hero_name(h->hero_id), panel_x + 16.0f, panel_y + panel_h - 26.0f, 14);
                const char *slot_labels[3] = {"Q", "W", "E"};
                float row_y = panel_y + panel_h - 60.0f;
                for (int slot = 0; slot < 3; slot++) {
                    glColor3f(0.55f, 0.85f, 1.0f);
                    draw_string(slot_labels[slot], panel_x + 16.0f, row_y, 10);
                    glColor3f(0.85f, 0.9f, 0.7f);
                    draw_string(arena_ability_name(h->hero_id, slot), panel_x + 42.0f, row_y, 9);
                    glColor3f(0.8f, 0.82f, 0.85f);
                    draw_string(arena_ability_description(h->hero_id, slot), panel_x + 42.0f, row_y - 16.0f, 8);
                    row_y -= 44.0f;
                }
            }
        }

        /* Settings pane (3424324/343543, "REDGARDEN settings pane" / "REDGARDEN settings volume
           slider"): Escape toggles it. Unlike the ability-help/character-stat panes above, this
           one is drawn unconditionally (not gated on my_owner/hero_id) since it's a real client
           preference, not match-state readout -- it makes sense to open even while observing a
           logged match. Geometry shared with the click/drag handler above via
           settings_panel_origin/settings_slider_track, same discipline shop_panel_origin's own
           doc comment establishes. */
        if (show_settings_pane) {
            float panel_x, panel_y;
            settings_panel_origin(win_w, win_h, &panel_x, &panel_y);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.05f, 0.08f, 0.1f, 0.92f);
            glRectf(panel_x, panel_y, panel_x + SETTINGS_PANEL_W, panel_y + SETTINGS_PANEL_H);
            glColor4f(0.4f, 0.55f, 0.65f, 0.9f);
            glLineWidth(2.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(panel_x, panel_y); glVertex2f(panel_x + SETTINGS_PANEL_W, panel_y);
            glVertex2f(panel_x + SETTINGS_PANEL_W, panel_y + SETTINGS_PANEL_H); glVertex2f(panel_x, panel_y + SETTINGS_PANEL_H);
            glEnd();
            glLineWidth(1.0f);
            glDisable(GL_BLEND);

            glColor3f(0.9f, 0.95f, 1.0f);
            draw_string("SETTINGS", panel_x + 16.0f, panel_y + SETTINGS_PANEL_H - 26.0f, 14);
            glColor3f(0.7f, 0.75f, 0.8f);
            draw_string("ESC TO CLOSE", panel_x + SETTINGS_PANEL_W - 130.0f, panel_y + SETTINGS_PANEL_H - 26.0f, 8);

            glColor3f(0.85f, 0.9f, 0.7f);
            draw_string("MASTER VOLUME", panel_x + 16.0f, panel_y + SETTINGS_PANEL_H / 2.0f + 26.0f, 9);

            /* Slider track + filled portion + handle -- same immediate-mode-quad idiom every
               other bar/pie affordance in this HUD already uses (ability cooldown pies,
               HP/MP bars above). Filled portion drawn first, then the handle on top of it. */
            float track_x, track_y;
            settings_slider_track(win_w, win_h, &track_x, &track_y);
            glColor4f(0.15f, 0.18f, 0.2f, 1.0f);
            glRectf(track_x, track_y, track_x + SETTINGS_SLIDER_W, track_y + SETTINGS_SLIDER_H);
            glColor4f(0.4f, 0.75f, 0.95f, 1.0f);
            glRectf(track_x, track_y, track_x + SETTINGS_SLIDER_W * master_volume, track_y + SETTINGS_SLIDER_H);
            glColor4f(0.4f, 0.55f, 0.65f, 0.9f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(track_x, track_y); glVertex2f(track_x + SETTINGS_SLIDER_W, track_y);
            glVertex2f(track_x + SETTINGS_SLIDER_W, track_y + SETTINGS_SLIDER_H); glVertex2f(track_x, track_y + SETTINGS_SLIDER_H);
            glEnd();
            float handle_x = track_x + SETTINGS_SLIDER_W * master_volume;
            glColor4f(0.95f, 0.98f, 1.0f, 1.0f);
            glRectf(handle_x - 3.0f, track_y - 4.0f, handle_x + 3.0f, track_y + SETTINGS_SLIDER_H + 4.0f);

            char vol_line[16];
            snprintf(vol_line, sizeof(vol_line), "%d%%", (int)(master_volume * 100.0f + 0.5f));
            glColor3f(0.9f, 0.95f, 1.0f);
            draw_string(vol_line, track_x + SETTINGS_SLIDER_W + 14.0f, track_y, 9);
        }

        /* Character stat pane (S170-175, founder: "we need a character display pane that
           shows current stats"): the local player's own hero only (same "local player's own
           kit only" scope the ability tiles above already hold themselves to), always
           visible -- unlike the shop/scoreboard below, this isn't a toggle, it's the
           persistent "how am I doing" readout a real MOBA's own stat panel always shows.
           Bottom-left, clear of the QWE tiles (bottom-center) and the enemy/BOT bar
           (top-left). */
        {
            ArenaHero *me = &arena_state.heroes[my_owner];
            float px = 20.0f, py = 130.0f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.05f, 0.08f, 0.1f, 0.82f);
            glRectf(px, py, px + 190.0f, py + 108.0f);
            glDisable(GL_BLEND);
            glColor3f(0.9f, 0.95f, 1.0f);
            draw_string(arena_hero_name(me->hero_id), px + 8.0f, py + 90.0f, 11);
            char line[48];
            glColor3f(0.8f, 0.9f, 0.85f);
            snprintf(line, sizeof(line), "HP %d/%d", me->hp > 0 ? me->hp : 0, me->max_hp);
            draw_string(line, px + 8.0f, py + 72.0f, 8);
            glColor3f(0.6f, 0.8f, 1.0f);
            snprintf(line, sizeof(line), "MP %d/%d", me->mp, ARENA_MP_MAX);
            draw_string(line, px + 8.0f, py + 58.0f, 8);
            glColor3f(0.85f, 0.7f, 0.5f);
            snprintf(line, sizeof(line), "AD %d  ARMOR %d", me->item_bonus_ad, (int)arena_hero_armor(me));
            draw_string(line, px + 8.0f, py + 44.0f, 8);
            glColor3f(0.95f, 0.8f, 0.2f);
            snprintf(line, sizeof(line), "FLOW %d (EARNED %d)", me->flow, me->flow_earned);
            draw_string(line, px + 8.0f, py + 30.0f, 8);
            glColor3f(0.6f, 0.95f, 0.6f);
            snprintf(line, sizeof(line), "XP %d", me->xp);
            draw_string(line, px + 8.0f, py + 16.0f, 8);
            glColor3f(0.9f, 0.6f, 0.6f);
            snprintf(line, sizeof(line), "K/D %d/%d", me->kills, me->deaths);
            draw_string(line, px + 8.0f, py + 2.0f, 8);
        }

        /* Damage log panel (S189-01, "go ahead and add the damage log to REDGARDEN"): real
           combat-log feed, rolling last-N (see ArenaDamageLogEntry's own doc comment in
           arena_game.h for the full design/scope reasoning, including why attacker
           attribution is only real for direct hero-vs-hero melee, not ability/creep/tower
           damage). Right side, clear of every other HUD panel (stat pane bottom-left, shop
           panel its own placement, ability tiles bottom-center) -- persistent, not a toggle,
           same "always visible" convention the character stat pane above already uses,
           matching how a real MOBA's combat log is always on. Most recent entry at the top,
           oldest at the bottom, standard rolling-feed order. */
        {
            float dlog_w = 260.0f;
            float dlog_x = (float)win_w - dlog_w - 20.0f;
            float dlog_y_top = (float)win_h - 20.0f;
            float row_h = 16.0f;
            float dlog_h = (float)ARENA_DAMAGE_LOG_CAPACITY * row_h + 12.0f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.05f, 0.08f, 0.1f, 0.75f);
            glRectf(dlog_x, dlog_y_top - dlog_h, dlog_x + dlog_w, dlog_y_top);
            glDisable(GL_BLEND);
            glColor3f(0.75f, 0.8f, 0.85f);
            draw_string("DAMAGE LOG", dlog_x + 8.0f, dlog_y_top - 14.0f, 9);
            /* Walk backwards from the most recently written entry -- damage_log_head always
               points at the NEXT write slot, so head-1 (wrapped) is the most recent one. */
            for (int row = 0; row < arena_state.damage_log_count && row < ARENA_DAMAGE_LOG_CAPACITY; row++) {
                int idx = (arena_state.damage_log_head - 1 - row + ARENA_DAMAGE_LOG_CAPACITY) % ARENA_DAMAGE_LOG_CAPACITY;
                const ArenaDamageLogEntry *e = &arena_state.damage_log[idx];
                char entry_line[64];
                if (e->source_hero_id < ARENA_HERO_COUNT) {
                    snprintf(entry_line, sizeof(entry_line), "%s hit %s for %d",
                             arena_hero_name(e->source_hero_id), arena_hero_name(e->target_hero_id), e->amount);
                } else {
                    snprintf(entry_line, sizeof(entry_line), "%s took %d dmg",
                             arena_hero_name(e->target_hero_id), e->amount);
                }
                glColor3f(0.85f, 0.85f, 0.8f);
                draw_string(entry_line, dlog_x + 8.0f, dlog_y_top - 14.0f - (float)(row + 1) * row_h, 8);
            }
        }

        /* Shop panel (S170-175, founder: "do a first pass shop interface... buying an item
           auto equips it for now no bag you can sell it back for less but no unequip into
           bag for now"). Left two columns are the buy list (catalog order, same grouping the
           data itself already has -- specific weapons, then weird, then generic), a third
           column is the local hero's own loadout (click an occupied slot to sell it back).
           Every click/keypress here is a single instant action against the server-authoritative
           arena_shop_buy/arena_shop_sell (or the local-mode equivalents) -- no confirm step,
           satisfying this repo's own cross-cutting "high-APM... both keybind and click paths
           must resolve instantly, no menu-diving" constraint (NORTHSTAR §2) the same way the
           QWE ability keys already do. */
        if (shop_open) {
            ArenaHero *me = &arena_state.heroes[my_owner];
            float sp_x, sp_y_top;
            shop_panel_origin(win_w, win_h, &sp_x, &sp_y_top);
            float panel_w = SHOP_COL_W + SHOP_COL_W + 40.0f;
            float panel_h = (float)SHOP_PANEL_ROWS * SHOP_ROW_H + 40.0f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.05f, 0.08f, 0.1f, 0.92f);
            glRectf(sp_x - 10.0f, sp_y_top - panel_h, sp_x - 10.0f + panel_w, sp_y_top + 26.0f);
            glColor4f(0.75f, 0.6f, 0.15f, 0.9f);
            glLineWidth(2.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(sp_x - 10.0f, sp_y_top - panel_h); glVertex2f(sp_x - 10.0f + panel_w, sp_y_top - panel_h);
            glVertex2f(sp_x - 10.0f + panel_w, sp_y_top + 26.0f); glVertex2f(sp_x - 10.0f, sp_y_top + 26.0f);
            glEnd();
            glLineWidth(1.0f);
            glDisable(GL_BLEND);

            char hbuf[48];
            glColor3f(0.95f, 0.85f, 0.3f);
            snprintf(hbuf, sizeof(hbuf), "SHOP -- FLOW %d (B TO CLOSE)", me->flow);
            draw_string(hbuf, sp_x, sp_y_top + 8.0f, 10);

            /* Page-nav buttons (S170-231, "and buttons"): one small box per page,
               current page filled solid amber, others outlined only -- same
               affordability-color-coding instinct the item rows below already use
               to make state legible at a glance, applied here to "which page." Click
               hit-test for these lives in the event loop above, same box geometry. */
            /* +1 tab: SHOP_BUILDS_PAGE (2026-08-25, build templates), labeled "B" instead of a
               page number since it isn't one of the real catalog pages. */
            for (int p = 0; p < SHOP_PAGE_COUNT + 1; p++) {
                float btn_x = sp_x + (float)p * (SHOP_PAGE_BTN_W + SHOP_PAGE_BTN_GAP);
                float btn_top = sp_y_top;
                float btn_bottom = sp_y_top - SHOP_PAGE_BTN_H;
                if (p == shop_page) {
                    glColor3f(0.75f, 0.6f, 0.15f);
                    glRectf(btn_x, btn_bottom, btn_x + SHOP_PAGE_BTN_W, btn_top);
                    glColor3f(0.05f, 0.05f, 0.05f);
                } else {
                    glColor3f(0.3f, 0.3f, 0.32f);
                    glBegin(GL_LINE_LOOP);
                    glVertex2f(btn_x, btn_bottom); glVertex2f(btn_x + SHOP_PAGE_BTN_W, btn_bottom);
                    glVertex2f(btn_x + SHOP_PAGE_BTN_W, btn_top); glVertex2f(btn_x, btn_top);
                    glEnd();
                    glColor3f(0.7f, 0.7f, 0.72f);
                }
                char pbuf[4];
                if (p == SHOP_BUILDS_PAGE) snprintf(pbuf, sizeof(pbuf), "B");
                else snprintf(pbuf, sizeof(pbuf), "%d", p + 1);
                draw_string(pbuf, btn_x + 9.0f, btn_bottom + 4.0f, 8);
            }

            if (shop_page == SHOP_BUILDS_PAGE) {
                /* Build templates (2026-08-25): each row auto-buys that template's ordered item
                   list, same real arena_shop_buy path individual item rows already use. Affordable
                   color-coding reuses "can I afford the FIRST unowned item in it right now" as the
                   legibility signal, same instinct the item rows below already apply per-item. */
                for (int row = 0; row < ARENA_BUILD_TEMPLATE_COUNT; row++) {
                    const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[row];
                    float row_y = sp_y_top - SHOP_ROW_H - (float)row * SHOP_ROW_H - 12.0f;
                    int next_cost = -1;
                    for (int i = 0; i < tmpl->item_count; i++) {
                        int iid = tmpl->item_ids[i];
                        if (iid < 0 || iid >= ARENA_ITEM_COUNT) continue;
                        if (me->equipped_item[ARENA_ITEMS[iid].slot] == iid) continue;
                        next_cost = ARENA_ITEMS[iid].cost;
                        break;
                    }
                    if (next_cost < 0) glColor3f(0.6f, 0.7f, 0.95f); /* fully owned already */
                    else if (me->flow >= next_cost) glColor3f(0.5f, 0.9f, 0.5f);
                    else glColor3f(0.6f, 0.35f, 0.35f);
                    char rowbuf[96];
                    snprintf(rowbuf, sizeof(rowbuf), "%d %s -- %s", row + 1, tmpl->name, tmpl->desc);
                    draw_string(rowbuf, sp_x, row_y, 7);
                }
            } else {
                for (int row = 0; row < SHOP_ITEMS_PER_PAGE; row++) {
                    int item_id = shop_page * SHOP_ITEMS_PER_PAGE + row;
                    if (item_id >= ARENA_ITEM_COUNT) break;
                    const ArenaItemDef *def = &ARENA_ITEMS[item_id];
                    float row_y = sp_y_top - SHOP_ROW_H - (float)row * SHOP_ROW_H - 12.0f;
                    if (me->flow >= def->cost) glColor3f(0.5f, 0.9f, 0.5f);
                    else glColor3f(0.6f, 0.35f, 0.35f);
                    char rowbuf[64];
                    snprintf(rowbuf, sizeof(rowbuf), "%d %s %d", row + 1, def->name, def->cost);
                    draw_string(rowbuf, sp_x, row_y, 7);
                }
            }

            float sell_x = sp_x + SHOP_COL_W + 20.0f;
            glColor3f(0.85f, 0.85f, 0.9f);
            draw_string("EQUIPPED (CLICK TO SELL)", sell_x, sp_y_top + 8.0f, 8);
            for (int slot = 0; slot < ARENA_ITEM_SLOT_COUNT; slot++) {
                float row_y = sp_y_top - (float)slot * SHOP_ROW_H - 12.0f;
                int item_id = me->equipped_item[slot];
                char rowbuf[64];
                if (item_id >= 0 && item_id < ARENA_ITEM_COUNT) {
                    glColor3f(0.7f, 0.85f, 1.0f);
                    snprintf(rowbuf, sizeof(rowbuf), "%s: %s", ARENA_ITEM_SLOT_NAMES[slot], ARENA_ITEMS[item_id].name);
                } else {
                    glColor3f(0.4f, 0.42f, 0.45f);
                    snprintf(rowbuf, sizeof(rowbuf), "%s: --", ARENA_ITEM_SLOT_NAMES[slot]);
                }
                draw_string(rowbuf, sell_x, row_y, 7);
            }
        }

        /* Scoreboard (S170-175, founder: "stats page shows team and individual kd ratio flow
           and xp"). Held, not toggled -- real MOBA "hold Tab" convention, and simpler than a
           toggle since it needs no event-loop state at all: just read this frame's keyboard
           state. Two columns, one per team, each hero's own K/D/Flow/XP plus a team-aggregate
           row at the bottom of each column (kills/deaths/flow_earned/xp summed across every
           active hero on that side) -- "team and individual," per the founder's own ask,
           both in the same view rather than two separate screens. */
        {
            const Uint8 *keystate = SDL_GetKeyboardState(NULL);
            if (keystate[SDL_SCANCODE_TAB]) {
                float panel_w = 560.0f, panel_h = 420.0f;
                float panel_x = win_w / 2.0f - panel_w / 2.0f;
                float panel_y = win_h / 2.0f - panel_h / 2.0f;
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(0.03f, 0.05f, 0.07f, 0.92f);
                glRectf(panel_x, panel_y, panel_x + panel_w, panel_y + panel_h);
                glColor4f(0.4f, 0.55f, 0.65f, 0.9f);
                glLineWidth(2.0f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(panel_x, panel_y); glVertex2f(panel_x + panel_w, panel_y);
                glVertex2f(panel_x + panel_w, panel_y + panel_h); glVertex2f(panel_x, panel_y + panel_h);
                glEnd();
                glLineWidth(1.0f);
                glDisable(GL_BLEND);

                for (int team = 0; team < 2; team++) {
                    float col_x = panel_x + 20.0f + (float)team * (panel_w / 2.0f);
                    float row_y = panel_y + panel_h - 30.0f;
                    if (team == arena_state.heroes[my_owner].team) glColor3f(0.4f, 0.75f, 1.0f);
                    else glColor3f(1.0f, 0.5f, 0.45f);
                    draw_string(team == 0 ? "TEAM 0" : "TEAM 1", col_x, row_y, 11);
                    row_y -= 22.0f;
                    int team_kills = 0, team_deaths = 0, team_flow_earned = 0, team_xp = 0;
                    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
                        ArenaHero *hh = &arena_state.heroes[i];
                        if (!hh->active || hh->team != team) continue;
                        team_kills += hh->kills; team_deaths += hh->deaths;
                        team_flow_earned += hh->flow_earned; team_xp += hh->xp;
                        if (row_y < panel_y + 50.0f) continue; /* panel's fixed height caps visible rows -- team aggregate below still counts everyone */
                        glColor3f(0.85f, 0.87f, 0.9f);
                        char rowbuf[64];
                        snprintf(rowbuf, sizeof(rowbuf), "%s %d/%d  F%d  XP%d",
                                 arena_hero_name(hh->hero_id), hh->kills, hh->deaths, hh->flow_earned, hh->xp);
                        draw_string(rowbuf, col_x, row_y, 8);
                        row_y -= 18.0f;
                    }
                    glColor3f(0.95f, 0.85f, 0.3f);
                    char aggbuf[64];
                    snprintf(aggbuf, sizeof(aggbuf), "TEAM K/D %d/%d  FLOW %d  XP %d",
                             team_kills, team_deaths, team_flow_earned, team_xp);
                    draw_string(aggbuf, col_x, panel_y + 22.0f, 8);
                }
            }
        }

        if (show_apm) {
            char apmbuf[24];
            snprintf(apmbuf, sizeof(apmbuf), "APM %d", apm_compute(now));
            glColor3f(0.9f, 0.9f, 0.3f);
            draw_string(apmbuf, win_w - 140.0f, win_h - 30.0f, 14);
        }

        if (cam_locked) {
            /* NORTHSTAR §15.1: the only on-screen sign the C toggle did anything -- nothing
               else in the frame changes shape when locked (the pivot already always follows
               my_owner), so without this a player could easily forget which mode they're in. */
            glColor3f(0.5f, 0.85f, 1.0f);
            draw_string("CAM LOCKED (C)", win_w - 140.0f, win_h - 50.0f, 12);
        }

        if (arena_state.winner != 0) {
            /* S170-149 bugfix, real founder bug report: "i cap a node...
               and then they kill the other team but it says i loose."
               `winner` encodes which TEAM won (1=team0, 2=team1), but this
               was comparing it against `my_owner` -- the raw client_id/hero
               SLOT INDEX (0..19 in a real team match, only ever equal to
               team index by coincidence for owner 0, and only correct for
               owner 1 in the literal 1v1 case where owner IS team). Any
               real team-mode player past owner 1 got a flipped result --
               shown "YOU LOSE" after their own team's real win, or vice
               versa. Compare against the hero's actual team instead. */
            if (arena_state.winner == arena_state.heroes[my_owner].team + 1) {
                glColor3f(0.2f, 1.0f, 0.4f);
                draw_string("YOU WIN", win_w / 2.0f - 150, win_h / 2.0f, 24);
            } else {
                glColor3f(1.0f, 0.2f, 0.2f);
                draw_string("YOU LOSE", win_w / 2.0f - 160, win_h / 2.0f, 24);
            }
            if (net_mode) {
                /* Requeue OK button -- bounds must match the click hit-test above. */
                glColor3f(0.15f, 0.35f, 0.2f);
                glBegin(GL_QUADS);
                glVertex2f(win_w / 2.0f - 90, win_h / 2.0f - 70);
                glVertex2f(win_w / 2.0f + 90, win_h / 2.0f - 70);
                glVertex2f(win_w / 2.0f + 90, win_h / 2.0f - 30);
                glVertex2f(win_w / 2.0f - 90, win_h / 2.0f - 30);
                glEnd();
                glColor3f(0.6f, 1.0f, 0.7f);
                draw_string("OK - REQUEUE", win_w / 2.0f - 78, win_h / 2.0f - 55, 14);
            }
        }
        glEnable(GL_DEPTH_TEST);

        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    if (audio_dev != 0) SDL_CloseAudioDevice(audio_dev);
    if (cursor_default) SDL_FreeCursor(cursor_default);
    if (cursor_enemy) SDL_FreeCursor(cursor_enemy);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
