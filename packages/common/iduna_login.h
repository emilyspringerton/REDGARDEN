/*
 * iduna_login.h -- IDUNA SSO login for native apps (single-header C, POSIX + Winsock).
 *
 * Implements the "native loopback" profile of the IDUNA SSO contract (IDUNA/sdk/README.md):
 *   1. listen on 127.0.0.1:<ephemeral>
 *   2. open the system browser at  <login_base>/api/v1/auth/sso/login?redirect_uri=http://127.0.0.1:<port>/cb?state=<S>
 *   3. IDUNA's hosted page signs the user in (Google / Battle.net / Twitch / email) and redirects to
 *      redirect_uri#sso_token=<jwt>&player_id=..&display_name=..   (fragment: never sent to a server)
 *   4. our /cb page's JS reads the fragment and POSTs it back to /token on the same loopback listener
 *   5. we verify the state nonce, hand the token to the caller.
 *
 * Non-blocking: call iduna_login_begin() once, then iduna_login_poll() every frame.
 * The password / OAuth ceremony only ever happens on IDUNA's own page, never in the game.
 *
 * Usage:
 *   IdunaLogin L;
 *   if (iduna_login_begin(&L, NULL) == 0) {          // NULL = https://iam.okemily.com
 *       // each frame:
 *       int r = iduna_login_poll(&L);                // 0 pending, 1 signed in, <0 failed/timeout
 *       if (r == 1) use(L.token, L.player_id, L.display_name);
 *   }
 *   iduna_login_close(&L);
 *
 * Header-only: everything is static inline, no implementation macro needed.
 * Security: the token is held in memory only; persistence is the caller's decision (use a 0600 file).
 */
#ifndef IDUNA_LOGIN_H
#define IDUNA_LOGIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET iduna_sock_t;
#  define IDUNA_BAD_SOCK INVALID_SOCKET
#  define iduna_sock_close closesocket
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
typedef int iduna_sock_t;
#  define IDUNA_BAD_SOCK (-1)
#  define iduna_sock_close close
#endif

#define IDUNA_LOGIN_DEFAULT_BASE "https://iam.okemily.com"
#define IDUNA_LOGIN_TIMEOUT_SEC 300
#define IDUNA_LOGIN_TOKEN_MAX 4096

typedef struct {
    iduna_sock_t listen_fd;
    int port;
    char state[33];
    char login_url[1024];
    time_t deadline;
    int done;                       /* 1 once token received */
    char token[IDUNA_LOGIN_TOKEN_MAX];
    char player_id[64];
    char display_name[128];
} IdunaLogin;

static const char IDUNA_LOGIN_BOUNCE_HTML[] =
    "<!doctype html><meta charset=utf-8><title>IDUNA login</title>"
    "<body style=\"font-family:Georgia,serif;background:#fdf6e3;color:#073642;text-align:center;padding-top:15vh\">"
    "<h2 id=m>Finishing sign-in&hellip;</h2><p id=s></p><script>"
    "var h=location.hash.replace(/^#/,''),q=new URLSearchParams(location.search);"
    "history.replaceState(null,'',location.pathname);"
    "if(!h||h.indexOf('sso_token=')<0){document.getElementById('m').textContent='Sign-in did not complete.';}"
    "else{fetch('/token',{method:'POST',body:'state='+encodeURIComponent(q.get('state')||'')+'&'+h})"
    ".then(function(r){document.getElementById('m').textContent=r.ok?'Signed in.':'Sign-in rejected.';"
    "document.getElementById('s').textContent=r.ok?'You can close this tab and return to the game.':'';})"
    ".catch(function(){document.getElementById('m').textContent='Could not reach the game.';});}"
    "</script></body>";

/* ---- helpers ---------------------------------------------------------------------------- */

static inline void iduna__set_nonblock(iduna_sock_t s) {
#ifdef _WIN32
    u_long one = 1; ioctlsocket(s, FIONBIO, &one);
#else
    int f = fcntl(s, F_GETFL, 0); if (f >= 0) fcntl(s, F_SETFL, f | O_NONBLOCK);
#endif
}

static inline int iduna__hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent-decode src[0..n) into dst (cap bytes incl. NUL). '+' stays '+' (encodeURIComponent never emits it). */
static inline void iduna__urldecode(char *dst, size_t cap, const char *src, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (src[i] == '%' && i + 2 < n && iduna__hexval(src[i+1]) >= 0 && iduna__hexval(src[i+2]) >= 0) {
            dst[o++] = (char)(iduna__hexval(src[i+1]) * 16 + iduna__hexval(src[i+2]));
            i += 2;
        } else dst[o++] = src[i];
    }
    dst[o] = 0;
}

/* Find key in an application/x-www-form-urlencoded string; decode its value into dst. Returns 1 if found. */
static inline int iduna__form_get(const char *form, const char *key, char *dst, size_t cap) {
    size_t kl = strlen(key);
    const char *p = form;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        if (seglen > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            iduna__urldecode(dst, cap, p + kl + 1, seglen - kl - 1);
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

static inline void iduna__percent_encode(char *dst, size_t cap, const char *src) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 4 < cap; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') dst[o++] = (char)*p;
        else { dst[o++] = '%'; dst[o++] = hex[*p >> 4]; dst[o++] = hex[*p & 15]; }
    }
    dst[o] = 0;
}

static inline void iduna__random_state(char out[33]) {
    static const char hex[] = "0123456789abcdef";
    unsigned char b[16];
    int ok = 0;
#ifndef _WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { ok = fread(b, 1, sizeof b, f) == sizeof b; fclose(f); }
#endif
    if (!ok) { /* fallback: time+address mix; the state is CSRF defence on a loopback port, not a secret key */
        unsigned long long x = (unsigned long long)time(NULL) * 6364136223846793005ULL ^ (unsigned long long)(size_t)out;
        for (int i = 0; i < 16; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; b[i] = (unsigned char)(x >> 33); }
    }
    for (int i = 0; i < 16; i++) { out[i*2] = hex[b[i] >> 4]; out[i*2+1] = hex[b[i] & 15]; }
    out[32] = 0;
}

/* Only [A-Za-z0-9%:/?=&._~-] reaches here (built from encoded parts + a validated base), so quoting is safe. */
static inline void iduna__open_browser(const char *url) {
    char cmd[1400];
#ifdef _WIN32
    /* `start` needs no extra link library (ShellExecute would need -lshell32) */
    snprintf(cmd, sizeof cmd, "start \"\" \"%s\"", url); (void)!system(cmd);
#elif defined(__APPLE__)
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1 &", url); (void)!system(cmd);
#else
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", url); (void)!system(cmd);
#endif
}

static inline int iduna__base_ok(const char *b) {
    if (strncmp(b, "https://", 8) != 0 && strncmp(b, "http://127.0.0.1", 16) != 0 && strncmp(b, "http://localhost", 16) != 0) return 0;
    for (const char *p = b; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr(":/._-", *p))) return 0;
    return 1;
}

/* ---- API -------------------------------------------------------------------------------- */

/* Start a login. login_base NULL => IDUNA_LOGIN_DEFAULT_BASE. open_browser=0 only prints/keeps the URL (tests).
 * Returns 0 on success (L->login_url holds the URL), <0 on error. */
static inline int iduna_login_begin_ex(IdunaLogin *L, const char *login_base, int open_browser) {
    memset(L, 0, sizeof *L);
    L->listen_fd = IDUNA_BAD_SOCK;
    if (!login_base || !*login_base) login_base = IDUNA_LOGIN_DEFAULT_BASE;
    if (!iduna__base_ok(login_base)) return -1;
#ifdef _WIN32
    WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -2;
#endif
    iduna_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == IDUNA_BAD_SOCK) return -3;
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 4) != 0) { iduna_sock_close(s); return -4; }
    socklen_t al = sizeof a;
    if (getsockname(s, (struct sockaddr *)&a, &al) != 0) { iduna_sock_close(s); return -5; }
    L->port = ntohs(a.sin_port);
    iduna__set_nonblock(s);
    L->listen_fd = s;
    iduna__random_state(L->state);
    L->deadline = time(NULL) + IDUNA_LOGIN_TIMEOUT_SEC;

    char redirect[200], enc[400];
    snprintf(redirect, sizeof redirect, "http://127.0.0.1:%d/cb?state=%s", L->port, L->state);
    iduna__percent_encode(enc, sizeof enc, redirect);
    snprintf(L->login_url, sizeof L->login_url, "%s/api/v1/auth/sso/login?redirect_uri=%s", login_base, enc);
    if (open_browser) iduna__open_browser(L->login_url);
    return 0;
}
static inline int iduna_login_begin(IdunaLogin *L, const char *login_base) { return iduna_login_begin_ex(L, login_base, 1); }

static inline void iduna__reply(iduna_sock_t c, int code, const char *ctype, const char *body) {
    char hdr[256];
    int bl = (int)strlen(body);
    int n = snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
        "Referrer-Policy: no-referrer\r\nConnection: close\r\n\r\n",
        code, code == 200 ? "OK" : (code == 403 ? "Forbidden" : "Not Found"), ctype, bl);
    send(c, hdr, n, 0);
    if (bl) send(c, body, bl, 0);
}

/* Serve one pending connection, if any. Returns 0 pending, 1 signed in, <0 failed/timeout. */
static inline int iduna_login_poll(IdunaLogin *L) {
    if (L->done) return 1;
    if (L->listen_fd == IDUNA_BAD_SOCK) return -1;
    if (time(NULL) > L->deadline) return -2;
    iduna_sock_t c = accept(L->listen_fd, NULL, NULL);
    if (c == IDUNA_BAD_SOCK) return 0;

    /* the accepted socket is blocking; bound the read so a stalled client can't wedge the frame loop */
#ifdef _WIN32
    DWORD tmo = 1500; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
#else
    struct timeval tv = { 1, 500000 }; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    char req[8192]; size_t have = 0; int hdr_end = -1;
    while (have < sizeof req - 1) {
        int n = (int)recv(c, req + have, sizeof req - 1 - have, 0);
        if (n <= 0) break;
        have += (size_t)n; req[have] = 0;
        const char *e = strstr(req, "\r\n\r\n");
        if (e) {
            hdr_end = (int)(e - req) + 4;
            const char *cl = strstr(req, "Content-Length:");
            if (!cl) cl = strstr(req, "content-length:");
            size_t want = cl ? (size_t)strtoul(cl + 15, NULL, 10) : 0;
            if (have >= (size_t)hdr_end + want) break;
        }
    }
    req[have] = 0;
    if (hdr_end < 0) { iduna_sock_close(c); return 0; }

    /* DNS-rebinding / foreign-page guard: only our own loopback origin may talk to us */
    char host_ok[48]; snprintf(host_ok, sizeof host_ok, "127.0.0.1:%d", L->port);
    const char *hh = strstr(req, "\r\nHost:"); if (!hh) hh = strstr(req, "\r\nhost:");
    int host_good = 0;
    if (hh) { hh += 7; while (*hh == ' ') hh++; host_good = strncmp(hh, host_ok, strlen(host_ok)) == 0; }
    if (!host_good) { iduna__reply(c, 403, "text/plain", "forbidden"); iduna_sock_close(c); return 0; }

    int result = 0;
    if (strncmp(req, "GET /cb", 7) == 0 && (req[7] == '?' || req[7] == ' ')) {
        iduna__reply(c, 200, "text/html; charset=utf-8", IDUNA_LOGIN_BOUNCE_HTML);
    } else if (strncmp(req, "POST /token ", 12) == 0) {
        const char *body = req + hdr_end;
        char st[64] = "", tok[IDUNA_LOGIN_TOKEN_MAX] = "";
        iduna__form_get(body, "state", st, sizeof st);
        int got = iduna__form_get(body, "sso_token", tok, sizeof tok);
        if (!got || !tok[0] || strcmp(st, L->state) != 0) {
            iduna__reply(c, 403, "text/plain", "bad state");
        } else {
            memcpy(L->token, tok, sizeof L->token);
            iduna__form_get(body, "player_id", L->player_id, sizeof L->player_id);
            iduna__form_get(body, "display_name", L->display_name, sizeof L->display_name);
            L->done = 1;
            iduna__reply(c, 200, "text/plain", "ok");
            result = 1;
        }
    } else {
        iduna__reply(c, 404, "text/plain", "not found");
    }
    iduna_sock_close(c);
    return result;
}

static inline void iduna_login_close(IdunaLogin *L) {
    if (L->listen_fd != IDUNA_BAD_SOCK) { iduna_sock_close(L->listen_fd); L->listen_fd = IDUNA_BAD_SOCK; }
}

#endif /* IDUNA_LOGIN_H */
