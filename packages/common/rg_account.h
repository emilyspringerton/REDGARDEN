// rg_account.h -- REDGARDEN player identity via IDUNA (name + saved progress + "login with IDUNA").
//
// Founder real-time, 2026-10-10: "update REDGARDEN for IDUNA OAUTH, same pattern as the deck
// tracker, same patterns as DEADWEIGHT where it gives you a name and allows you to save progress /
// login with IDUNA". Two real patterns combined:
//   * DEADWEIGHT's guest accounts (IDUNA/internal/http/handlers/game_online.go, game "redgarden"):
//     a guest player_id + guest_secret persisted locally, guest-login on every launch.
//   * the Hearthstone tracker's browser sign-in: the client opens the WOTAN connect page, the page
//     does the registration / IDUNA sign-in, then bounces the browser to a loopback callback
//     (http://127.0.0.1:<port>/cb) with the result. No password ever touches this process.
//
// Network: every IDUNA call shells out to `curl` (present on Linux/macOS and Windows 10+). This
// repo's own http_client.h is plain HTTP and POSIX-only, and production IDUNA is https; adding
// mbedTLS here (DEADWEIGHT's route) is a bigger lift than this feature needs. curl takes the body
// from a temp file and only validated characters ever reach the command line, so name/password
// text can never be shell-interpreted.
//
// Header-only, no SDL/GL dependency, so tests/test_rg_account.c can build it standalone.
#ifndef RG_ACCOUNT_H
#define RG_ACCOUNT_H

#ifdef _WIN32
#define _CRT_RAND_S /* rand_s() for the callback nonce */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #define RG_POPEN  _popen
  #define RG_PCLOSE _pclose
  #define RG_CLOSESOCK closesocket
  #define RG_NULLDEV "NUL"
  typedef SOCKET rg_sock_t;
  #define RG_BAD_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <sys/stat.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/select.h>
  #define RG_POPEN  popen
  #define RG_PCLOSE pclose
  #define RG_CLOSESOCK close
  #define RG_NULLDEV "/dev/null"
  typedef int rg_sock_t;
  #define RG_BAD_SOCK (-1)
#endif

#define RG_TICKET_LEN 36 /* 20-byte payload + 16-byte truncated HMAC, same as ARENA_TICKET_TOTAL_LEN */

typedef struct {
    char base_url[160];     /* API origin, e.g. https://wotan.okemily.com (no trailing slash) */
    char connect_url[220];  /* page that does name pick / sign-in, default <base>/redgarden/connect.html */
    char account_path[260]; /* persisted identity */
    char player_id[48];
    char secret[72];        /* guest_secret (empty for SSO / email accounts) */
    char name[48];
    char token[1700];       /* game-scoped player JWT */
    long exp;               /* token expiry (unix seconds), 0 = unknown */
    int  login_timeout_s;   /* how long to wait for the browser callback */
} RgAccount;

/* ---- small helpers ---- */

static int rg_safe_chars(const char *s, const char *extra) {
    if (!s || !*s) return 0;
    for (; *s; s++) {
        char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        if (extra && strchr(extra, c)) continue;
        return 0;
    }
    return 1;
}
static int rg_safe_url(const char *u) {
    if (strncmp(u, "http://", 7) != 0 && strncmp(u, "https://", 8) != 0) return 0;
    return rg_safe_chars(u, ":/._-");
}

/* Minimal flat-JSON string extraction (IDUNA's replies here are small, trusted, un-nested). */
static int rg_json_str(const char *json, const char *key, char *out, size_t n) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\') return 0; /* escapes never appear in the fields we read (uuid/hex/jwt) */
        out[i++] = *p++;
    }
    if (*p != '"') return 0;
    out[i] = '\0';
    return 1;
}
static int rg_json_long(const char *json, const char *key, long *out) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = v;
    return 1;
}

static int rg_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int rg_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    if (strlen(hex) != out_len * 2) return 0;
    for (size_t i = 0; i < out_len; i++) {
        int a = rg_hex_nibble(hex[2 * i]), b = rg_hex_nibble(hex[2 * i + 1]);
        if (a < 0 || b < 0) return 0;
        out[i] = (unsigned char)(a * 16 + b);
    }
    return 1;
}

/* %XX / '+' decode in place. */
static void rg_url_decode(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '%' && rg_hex_nibble(r[1]) >= 0 && rg_hex_nibble(r[2]) >= 0) {
            *w++ = (char)(rg_hex_nibble(r[1]) * 16 + rg_hex_nibble(r[2]));
            r += 2;
        } else if (*r == '+') {
            *w++ = ' ';
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}
/* Value of `key` in a raw query string "a=1&b=2", decoded, into out. */
static int rg_query_get(const char *q, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    const char *p = q;
    while (*p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < n) out[i++] = *p++;
            out[i] = '\0';
            rg_url_decode(out);
            return 1;
        }
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return 0;
}

static void rg_random_hex(char *out, size_t nbytes) {
    unsigned char b[32];
    if (nbytes > sizeof b) nbytes = sizeof b;
    int ok = 0;
#ifdef _WIN32
    for (size_t i = 0; i < nbytes; i++) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) { ok = 0; break; }
        b[i] = (unsigned char)(v & 0xff);
        ok = 1;
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { ok = fread(b, 1, nbytes, f) == nbytes; fclose(f); }
#endif
    if (!ok) { /* last resort: not cryptographic, but the nonce only has to beat blind guessing */
        srand((unsigned)time(NULL) ^ (unsigned)(size_t)out);
        for (size_t i = 0; i < nbytes; i++) b[i] = (unsigned char)(rand() & 0xff);
    }
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < nbytes; i++) { out[2 * i] = hx[b[i] >> 4]; out[2 * i + 1] = hx[b[i] & 15]; }
    out[2 * nbytes] = '\0';
}

/* ---- config + persistence ---- */

static void rg_account_init(RgAccount *a, const char *base_url, const char *connect_url, const char *account_path) {
    memset(a, 0, sizeof *a);
    snprintf(a->base_url, sizeof a->base_url, "%s", base_url && *base_url ? base_url : "https://wotan.okemily.com");
    size_t n = strlen(a->base_url);
    while (n > 0 && a->base_url[n - 1] == '/') a->base_url[--n] = '\0';
    if (connect_url && *connect_url) snprintf(a->connect_url, sizeof a->connect_url, "%s", connect_url);
    else snprintf(a->connect_url, sizeof a->connect_url, "%s/redgarden/connect.html", a->base_url);
    snprintf(a->account_path, sizeof a->account_path, "%s", account_path && *account_path ? account_path : "redgarden_account.txt");
    a->login_timeout_s = 180;
}

static void rg_account_load(RgAccount *a) {
    FILE *f = fopen(a->account_path, "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "player_id")) snprintf(a->player_id, sizeof a->player_id, "%s", v);
        else if (!strcmp(k, "secret")) snprintf(a->secret, sizeof a->secret, "%s", v);
        else if (!strcmp(k, "name")) snprintf(a->name, sizeof a->name, "%s", v);
        else if (!strcmp(k, "token")) snprintf(a->token, sizeof a->token, "%s", v);
        else if (!strcmp(k, "exp")) a->exp = strtol(v, NULL, 10);
    }
    fclose(f);
}

static void rg_account_save(const RgAccount *a) {
    FILE *f = fopen(a->account_path, "w");
    if (!f) { fprintf(stderr, "REDGARDEN: could not write %s (progress will not be remembered)\n", a->account_path); return; }
    fprintf(f, "player_id=%s\nsecret=%s\nname=%s\ntoken=%s\nexp=%ld\n", a->player_id, a->secret, a->name, a->token, a->exp);
    fclose(f);
#ifndef _WIN32
    chmod(a->account_path, 0600);
#endif
}

/* ---- HTTP via curl ---- */

/* Returns 0 on transport success (status in *status), -1 otherwise. method is GET/POST. */
static int rg_http(const RgAccount *a, const char *method, const char *path, const char *bearer,
                   const char *body, char *resp, size_t resp_n, int *status) {
    *status = 0;
    resp[0] = '\0';
    if (!rg_safe_url(a->base_url) || !rg_safe_chars(path, "/._-?=&")) return -1;
    if (bearer && *bearer && !rg_safe_chars(bearer, "._-")) return -1;

    char tmp[300] = "";
    if (body) {
        snprintf(tmp, sizeof tmp, "%s.req", a->account_path);
        FILE *bf = fopen(tmp, "wb");
        if (!bf) return -1;
        fwrite(body, 1, strlen(body), bf);
        fclose(bf);
    }
    const char *curl = getenv("REDGARDEN_CURL");
    if (!curl || !rg_safe_chars(curl, ":/\\._- ")) {
#ifdef _WIN32
        curl = "curl.exe";
#else
        curl = "curl";
#endif
    }
    char cmd[2600];
    int n = snprintf(cmd, sizeof cmd, "%s -sS --max-time 15 -X %s -H \"Accept: application/json\"", curl, method);
    if (body) n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -H \"Content-Type: application/json\" --data-binary @\"%s\"", tmp);
    if (bearer && *bearer) n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -H \"Authorization: Bearer %s\"", bearer);
    n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -w \"\\n%%{http_code}\" \"%s%s\" 2>%s", a->base_url, path, RG_NULLDEV);
    if (n <= 0 || (size_t)n >= sizeof cmd) { if (body) remove(tmp); return -1; }

    FILE *p = RG_POPEN(cmd, "r");
    if (!p) { if (body) remove(tmp); return -1; }
    static char buf[8192];
    size_t got = fread(buf, 1, sizeof buf - 1, p);
    RG_PCLOSE(p);
    if (body) remove(tmp);
    buf[got] = '\0';
    char *nl = strrchr(buf, '\n');
    if (!nl) return -1;
    *status = atoi(nl + 1);
    *nl = '\0';
    snprintf(resp, resp_n, "%s", buf);
    return *status > 0 ? 0 : -1;
}

/* ---- browser sign-in (loopback callback) ---- */

static void rg_open_browser(const char *url) {
    if (!rg_safe_chars(url, ":/._-?=&%")) return;
    char cmd[700];
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "rundll32 url.dll,FileProtocolHandler \"%s\"", url);
#elif defined(__APPLE__)
    snprintf(cmd, sizeof cmd, "open \"%s\" >/dev/null 2>&1 &", url);
#else
    snprintf(cmd, sizeof cmd, "xdg-open \"%s\" >/dev/null 2>&1 &", url);
#endif
    if (system(cmd) != 0) { /* url is also printed to the log; user can open it by hand */ }
}

/* Parse "GET /cb?... HTTP/1.1" and fill the account if state matches. 1 = accepted. */
static int rg_parse_callback(RgAccount *a, const char *req, const char *want_state) {
    if (strncmp(req, "GET /cb?", 8) != 0) return 0;
    const char *q = req + 8;
    const char *end = strpbrk(q, " \r\n");
    size_t qlen = end ? (size_t)(end - q) : strlen(q);
    static char query[6000];
    if (qlen >= sizeof query) return 0;
    memcpy(query, q, qlen);
    query[qlen] = '\0';

    char state[80] = "", pid[48] = "", name[48] = "", tok[1700] = "", exp[24] = "", secret[72] = "";
    if (!rg_query_get(query, "state", state, sizeof state) || strcmp(state, want_state) != 0) return 0;
    if (!rg_query_get(query, "player_id", pid, sizeof pid) || !rg_safe_chars(pid, "-")) return 0;
    if (!rg_query_get(query, "token", tok, sizeof tok) || !rg_safe_chars(tok, "._-")) return 0;
    rg_query_get(query, "name", name, sizeof name);
    rg_query_get(query, "exp", exp, sizeof exp);
    rg_query_get(query, "secret", secret, sizeof secret);
    if (secret[0] && !rg_safe_chars(secret, "")) return 0;

    snprintf(a->player_id, sizeof a->player_id, "%s", pid);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->token, sizeof a->token, "%s", tok);
    snprintf(a->secret, sizeof a->secret, "%s", secret);
    a->exp = strtol(exp, NULL, 10);
    return 1;
}

/* Opens the connect page and waits for the browser to call back. 1 = signed in. */
static int rg_browser_login(RgAccount *a) {
    rg_sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == RG_BAD_SOCK) return 0;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* loopback only, never the LAN */
    addr.sin_port = 0;
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(ls, 4) != 0) { RG_CLOSESOCK(ls); return 0; }
    socklen_t alen = sizeof addr;
    if (getsockname(ls, (struct sockaddr *)&addr, &alen) != 0) { RG_CLOSESOCK(ls); return 0; }
    int port = ntohs(addr.sin_port);

    char state[40];
    rg_random_hex(state, 16);
    char url[400];
    snprintf(url, sizeof url, "%s%sport=%d&state=%s", a->connect_url, strchr(a->connect_url, '?') ? "&" : "?", port, state);
    fprintf(stderr, "REDGARDEN: opening %s\nREDGARDEN: waiting up to %d s for you to pick a name / sign in with IDUNA in the browser\n",
            url, a->login_timeout_s);
    fflush(stderr);
    rg_open_browser(url);

    time_t deadline = time(NULL) + a->login_timeout_s;
    int ok = 0;
    while (!ok && time(NULL) < deadline) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(ls, &rs);
        struct timeval tv = { 1, 0 };
        if (select((int)ls + 1, &rs, NULL, NULL, &tv) <= 0) continue;
        rg_sock_t c = accept(ls, NULL, NULL);
        if (c == RG_BAD_SOCK) continue;
        static char req[8192];
        size_t have = 0;
        struct timeval rt = { 3, 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rt, sizeof rt);
        while (have < sizeof req - 1) {
            int r = (int)recv(c, req + have, (int)(sizeof req - 1 - have), 0);
            if (r <= 0) break;
            have += (size_t)r;
            req[have] = '\0';
            if (strstr(req, "\r\n")) break; /* the request line is all we need */
        }
        req[have] = '\0';
        int good = rg_parse_callback(a, req, state);
        const char *page = good
            ? "<!doctype html><meta charset=utf-8><title>REDGARDEN</title><body style=\"font:16px system-ui;background:#0e1a14;color:#eef2e6;padding:3rem\"><h1>You are connected.</h1><p>Go back to the game. You can close this tab.</p>"
            : "<!doctype html><meta charset=utf-8><title>REDGARDEN</title><body style=\"font:16px system-ui;padding:3rem\"><p>Invalid or expired request.</p>";
        char head[200];
        int hl = snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                          good ? "200 OK" : "400 Bad Request", (int)strlen(page));
        send(c, head, hl, 0);
        send(c, page, (int)strlen(page), 0);
        RG_CLOSESOCK(c);
        ok = good;
    }
    RG_CLOSESOCK(ls);
    return ok;
}

/* ---- token + ticket ---- */

/* Make sure a.token is a currently-valid player token. 1 = ok. */
static int rg_ensure_token(RgAccount *a) {
    time_t now = time(NULL);
    if (a->token[0] && (a->exp == 0 || a->exp > (long)now + 60)) return 1;

    if (a->player_id[0] && a->secret[0]) {
        char body[400], resp[4096];
        int st = 0;
        snprintf(body, sizeof body, "{\"player_id\":\"%s\",\"guest_secret\":\"%s\"}", a->player_id, a->secret);
        if (rg_safe_chars(a->player_id, "-") && rg_safe_chars(a->secret, "") &&
            rg_http(a, "POST", "/api/v1/games/redgarden/guest-login", NULL, body, resp, sizeof resp, &st) == 0 && st == 200) {
            char tok[1700], nm[48];
            long exp = 0;
            if (rg_json_str(resp, "token", tok, sizeof tok)) {
                snprintf(a->token, sizeof a->token, "%s", tok);
                if (rg_json_str(resp, "display_name", nm, sizeof nm)) snprintf(a->name, sizeof a->name, "%s", nm);
                rg_json_long(resp, "expires_at", &exp);
                a->exp = exp;
                rg_account_save(a);
                return 1;
            }
        }
        if (st == 401) { /* IDUNA says these credentials are gone: forget them and sign in fresh */
            a->secret[0] = '\0';
            a->token[0] = '\0';
        } else if (st != 0) {
            fprintf(stderr, "REDGARDEN: IDUNA guest-login failed (HTTP %d)\n", st);
        } else {
            fprintf(stderr, "REDGARDEN: IDUNA unreachable (is curl installed? is %s up?)\n", a->base_url);
            return 0; /* offline: do not pop a browser that cannot work either */
        }
    }
    if (rg_browser_login(a)) {
        rg_account_save(a);
        fprintf(stderr, "REDGARDEN: signed in as %s (%s)\n", a->name, a->player_id);
        return 1;
    }
    fprintf(stderr, "REDGARDEN: no IDUNA sign-in completed\n");
    return 0;
}

/* Mint a connect ticket for the signed-in player. 1 = out[RG_TICKET_LEN] filled. */
static int rg_account_ticket(RgAccount *a, unsigned char out[RG_TICKET_LEN]) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!rg_ensure_token(a)) return 0;
        char resp[1024], hex[96];
        int st = 0;
        if (rg_http(a, "POST", "/api/v1/redgarden/self-ticket", a->token, "{}", resp, sizeof resp, &st) != 0) return 0;
        if (st == 401) { a->token[0] = '\0'; a->exp = 0; continue; } /* expired mid-session: re-auth once */
        if (st != 200) { fprintf(stderr, "REDGARDEN: ticket request refused (HTTP %d)\n", st); return 0; }
        if (!rg_json_str(resp, "ticket", hex, sizeof hex) || !rg_hex_decode(hex, out, RG_TICKET_LEN)) {
            fprintf(stderr, "REDGARDEN: ticket reply malformed\n");
            return 0;
        }
        return 1;
    }
    return 0;
}

#endif /* RG_ACCOUNT_H */
