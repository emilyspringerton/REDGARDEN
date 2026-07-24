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
#include "../../../packages/simulation/arena_replay.h"

/* ---------------- networked PvP (2026-07-24 pivot, NORTHSTAR §13) ----------------
 * Local-only mode (no --connect flag) is unchanged: my_owner stays 0,
 * arena_update() runs fully client-side against the built-in bot. In
 * network mode, apps/arena_server is authoritative -- this client only
 * sends move/cast commands and applies incoming snapshots, never calls
 * arena_update() itself. */
static int net_mode = 0;
static int my_owner = 0; /* which arena_state.heroes[] slot is "me" -- 0 in local mode always */

#ifndef _WIN32
#define ARENA_TICKET_PAYLOAD_LEN 20
#define ARENA_TICKET_TOTAL_LEN (ARENA_TICKET_PAYLOAD_LEN + 16)

static int net_sock = -1;
static struct sockaddr_in net_server_addr;

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
    snprintf(provider_sub, sizeof(provider_sub), "player-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
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
    if (iduna_agent_configured) {
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
    sendto(net_sock, &find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));

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
            sendto(net_sock, &find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));
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

static void net_send_move(float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_MOVE;
    ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static void net_send_cast(int slot) {
    char buf[sizeof(NetHeader) + sizeof(ArenaCastCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_CAST;
    ArenaCastCmd *cmd = (ArenaCastCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static int net_lobby_size = 2; /* set from the server's own msg->count once a snapshot arrives */

static void net_poll_snapshots(void) {
    char rbuf[2048];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    while (len > 0) {
        if (len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
            NetHeader *h = (NetHeader *)rbuf;
            if (h->type == PACKET_ARENA_SNAPSHOT) {
                ArenaSnapshotMsg *msg = (ArenaSnapshotMsg *)(rbuf + sizeof(NetHeader));
                net_lobby_size = msg->count;
                for (int i = 0; i < msg->count && i < ARENA_SNAPSHOT_MAX_HEROES; i++) {
                    ArenaHero *dst = &arena_state.heroes[i];
                    dst->x = msg->heroes[i].x;
                    dst->z = msg->heroes[i].z;
                    dst->hp = msg->heroes[i].hp;
                    dst->max_hp = msg->heroes[i].max_hp;
                    dst->alive = msg->heroes[i].alive;
                    dst->active = 1;
                    dst->team = (i < msg->count / 2) ? 0 : 1;
                    dst->hero_id = (ArenaHeroID)msg->heroes[i].hero_id;
                }
                arena_state.winner = msg->winner;
            }
        }
        len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    }
}
#endif

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
    mkdir("var", 0755);
    mkdir("var/matches", 0755);
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
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
static PFNGLUNIFORM4FPROC glUniform4f_;
static PFNGLUNIFORM3FPROC glUniform3f_;
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
    LOAD(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD(glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD(glUniform3f, PFNGLUNIFORM3FPROC);
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

static const char *FS_SRC =
    "#version 150\n"
    "in vec3 vNormal;\n"
    "uniform vec4 uColor;\n"
    "uniform vec3 uLightDir;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.2);\n"
    "    fragColor = vec4(uColor.rgb * diff, uColor.a);\n"
    "}\n";

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

/* ---------------- tiny immediate-mode HUD text (ported from apps/lobby) ---------------- */
static void draw_char(char c, float x, float y, float s) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (c >= '0' && c <= '9') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
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

/* ---------------- camera ---------------- */
static float cam_yaw = 45.0f, cam_pitch = 40.0f, cam_dist = 16.0f;

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

int main(int argc, char *argv[]) {
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

    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280, win_h = 720;
    SDL_Window *win = SDL_CreateWindow(
        observing ? "RED GARDEN — OBSERVER MODE" :
        (net_mode ? "RED GARDEN — MOBA (networked PvP)" : "RED GARDEN — MOBA (local)"),
        100, 100, win_w, win_h, SDL_WINDOW_OPENGL);
    if (!win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return 1; }

    if (!load_gl_functions()) {
        fprintf(stderr, "Failed to load required GL 3.x functions via SDL_GL_GetProcAddress\n");
        return 1;
    }

    GLuint prog = link_program(VS_SRC, FS_SRC);
    GLint loc_mvp = glGetUniformLocation_(prog, "uMVP");
    GLint loc_model = glGetUniformLocation_(prog, "uModel");
    GLint loc_color = glGetUniformLocation_(prog, "uColor");
    GLint loc_light = glGetUniformLocation_(prog, "uLightDir");

    build_ring_mesh(0.8f, 1.0f);
    Mesh cube_mesh = upload_mesh(CUBE_VERTS, CUBE_VERT_COUNT);
    Mesh plane_mesh = upload_mesh(PLANE_VERTS, PLANE_VERT_COUNT);
    Mesh ring_mesh = upload_mesh(RING_VERTS, RING_VERT_COUNT);

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
                cam_yaw += dx * 0.3f;
                cam_pitch += dy * 0.3f;
                if (cam_pitch < 10.0f) cam_pitch = 10.0f;
                if (cam_pitch > 80.0f) cam_pitch = 80.0f;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                cam_dist -= e.wheel.y * 1.0f;
                if (cam_dist < 4.0f) cam_dist = 4.0f;
                if (cam_dist > 30.0f) cam_dist = 30.0f;
            }
            /* Everything below drives a live match (movement clicks, kit
             * casts, restart-into-a-new-match) -- none of it applies while
             * observing a logged one. Camera control above still works, so
             * an observer can look around freely. */
            if (!observing && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                arena_state.winner == 0) {
                float gx, gz;
                float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
                if (screen_to_ground(e.button.x, e.button.y, win_w, win_h, 60.0f,
                                     focus_x, focus_z, &gx, &gz)) {
#ifndef _WIN32
                    if (net_mode) net_send_move(gx, gz);
                    else
#endif
                        arena_set_move_target(my_owner, gx, gz);
                    spawn_ring(gx, gz);
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
                }
            }
            /* The Unicorn's kit (docs/HEROES_VS0.md) — the local player's own
             * hero (my_owner) only, S170-18. R is already bound to "restart
             * match" in local mode, so the ultimate goes on E. In net_mode,
             * casts are sent to the server, which owns cooldowns/effects. */
            if (!observing && e.type == SDL_KEYDOWN && arena_state.winner == 0) {
#ifndef _WIN32
                if (net_mode) {
                    if (e.key.keysym.sym == SDLK_q) net_send_cast(0);
                    if (e.key.keysym.sym == SDLK_w) net_send_cast(1);
                    if (e.key.keysym.sym == SDLK_e) net_send_cast(2);
                } else
#endif
                {
                    if (e.key.keysym.sym == SDLK_q) { arena_cast_q(my_owner); arena_log_ability("Q"); }
                    if (e.key.keysym.sym == SDLK_w) { arena_toggle_w(my_owner); arena_log_ability("W"); }
                    if (e.key.keysym.sym == SDLK_e) { arena_cast_r(my_owner); arena_log_ability("R"); }
                }
            }
        }

        if (observing) {
            /* Drive the exact same ArenaState the live path draws from --
             * "same draw code, no second rendering path" (S170-30). */
            arena_replay_apply_at(&replay, observe_elapsed_ms, &arena_state);
        }
#ifndef _WIN32
        else if (net_mode) {
            /* apps/arena_server is authoritative -- apply its snapshots
               rather than running arena_update() locally (that would
               double-simulate and diverge from the server's own state). */
            net_poll_snapshots();
        }
#endif
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
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            rings[i].age_ms += dt;
            if (rings[i].age_ms >= RING_LIFETIME_MS) rings[i].active = 0;
        }

        glViewport(0, 0, win_w, win_h);
        glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
        Mat4 view = mat4_orbit_view(focus_x, 0, focus_z, cam_yaw, cam_pitch, cam_dist);
        Mat4 proj = mat4_perspective(60.0f, (float)win_w / (float)win_h, 0.1f, 100.0f);
        Mat4 vp = mat4_multiply(&proj, &view);

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

        /* nodes */
        for (int i = 0; i < ARENA_NODE_COUNT; i++) {
            Mat4 t = mat4_translate(arena_state.nodes[i].x, 0.15f, arena_state.nodes[i].z);
            Mat4 s = mat4_scale(1.2f, 0.3f, 1.2f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.85f, 0.7f, 0.1f, 1.0f);
            draw_mesh(&cube_mesh);
        }

        /* heroes -- ARENA_MAX_HEROES so team-mode matches (up to 10v10)
           render every real hero; local/1v1 heroes[2..] are simply never
           alive, so this loop is a no-op regression risk for that mode. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            Mat4 t = mat4_translate(h->x, 0.5f, h->z);
            Mat4 s = mat4_scale(0.9f, 1.0f, 0.9f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            if (i == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.8f, 0.95f, 1.0f); /* my hero: bright cyan */
            } else if (h->team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, 1.0f); /* teammate: blue */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f); /* enemy: red */
            }
            draw_mesh(&cube_mesh);
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
        }
        glColor3f(0.95f, 0.25f, 0.15f);
        draw_string(net_mode ? "ENEMY" : "BOT", 20, win_h - 70.0f, 14);
        {
            ArenaHero *h = &arena_state.heroes[1 - my_owner];
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

        {
            /* Own hero's kit status (docs/HEROES_VS0.md) — Q/W/E readiness. */
            ArenaHero *h = &arena_state.heroes[my_owner];
            char qbuf[24], wbuf[24], ebuf[24];
            snprintf(qbuf, sizeof(qbuf), "Q %s", h->q_cooldown_ms > 0 ? "CD" : "READY");
            snprintf(wbuf, sizeof(wbuf), "W %s", h->w_active ? "ON" : "OFF");
            snprintf(ebuf, sizeof(ebuf), "E %s%s", h->r_cooldown_ms > 0 ? "CD" : "READY",
                     h->r_active_ms > 0 ? " (ACTIVE)" : "");
            glColor3f(h->q_cooldown_ms > 0 ? 0.5f : 0.2f, h->q_cooldown_ms > 0 ? 0.5f : 1.0f, 0.9f);
            draw_string(qbuf, 20, win_h - 95.0f, 12);
            glColor3f(h->w_active ? 0.2f : 0.6f, h->w_active ? 1.0f : 0.6f, 0.3f);
            draw_string(wbuf, 120, win_h - 95.0f, 12);
            glColor3f(h->r_cooldown_ms > 0 ? 0.5f : 0.9f, 0.4f, h->r_cooldown_ms > 0 ? 0.5f : 0.9f);
            draw_string(ebuf, 220, win_h - 95.0f, 12);
        }

        if (arena_state.winner != 0) {
            if (arena_state.winner == my_owner + 1) {
                glColor3f(0.2f, 1.0f, 0.4f);
                draw_string("YOU WIN", win_w / 2.0f - 150, win_h / 2.0f, 24);
            } else {
                glColor3f(1.0f, 0.2f, 0.2f);
                draw_string("YOU LOSE", win_w / 2.0f - 160, win_h / 2.0f, 24);
            }
        }
        glEnable(GL_DEPTH_TEST);

        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
