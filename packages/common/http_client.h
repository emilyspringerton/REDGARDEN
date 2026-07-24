// http_client.h — minimal blocking HTTP/1.1 client for talking to IDUNA
// (EMILY/BACKLOG.md S170-26, ported verbatim from shankpit-460's S156-04
// original). No TLS, no chunked-transfer decoding, no redirects, no
// persistent connections, no general JSON parsing — IDUNA is same-box
// HTTP-only and its responses here are controlled/trusted, not adversarial
// input, so a real HTTP/JSON stack would be scope creep. Same
// "self-contained, no external library" spirit as hmac_sha256.h.
//
// Linux/POSIX only. Windows builds get a stub that always fails closed
// (returns -1) — this codebase's Makefile has no Windows target today, so
// implementing real Winsock support here would be effort spent on a
// platform nothing actually builds for.
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

// http_post_json sends a blocking HTTP/1.1 POST of json_body to
// http://host:port/path with Content-Type: application/json and, if
// bearer_token is non-NULL and non-empty, an Authorization: Bearer header.
// On success (request sent and a response read) writes the numeric HTTP
// status code to *out_status and copies up to resp_buf_len-1 bytes of the
// response body into resp_buf (NUL-terminated), returning 0. Returns -1 on
// any socket-level failure (resolve/connect/send/recv/oversized request)
// without touching *out_status or resp_buf — callers must treat -1 as "no
// idea what happened," not "failed cleanly."
static int http_post_json(const char *host, int port, const char *path,
                           const char *bearer_token,
                           const char *json_body,
                           char *resp_buf, size_t resp_buf_len,
                           int *out_status) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    char req[4096];
    int body_len = (int)strlen(json_body);
    int req_len;
    if (bearer_token && bearer_token[0]) {
        req_len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            path, host, bearer_token, body_len, json_body);
    } else {
        req_len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            path, host, body_len, json_body);
    }
    if (req_len < 0 || req_len >= (int)sizeof(req)) { close(fd); return -1; }

    if (send(fd, req, (size_t)req_len, 0) != req_len) { close(fd); return -1; }

    static char raw[8192];
    size_t total = 0;
    ssize_t n;
    while (total < sizeof(raw) - 1 &&
           (n = recv(fd, raw + total, sizeof(raw) - 1 - total, 0)) > 0) {
        total += (size_t)n;
    }
    close(fd);
    if (total == 0) return -1;
    raw[total] = '\0';

    int status = 0;
    if (sscanf(raw, "HTTP/%*d.%*d %d", &status) != 1) return -1;
    *out_status = status;

    const char *body = strstr(raw, "\r\n\r\n");
    if (body && resp_buf_len > 0) {
        body += 4;
        strncpy(resp_buf, body, resp_buf_len - 1);
        resp_buf[resp_buf_len - 1] = '\0';
    } else if (resp_buf_len > 0) {
        resp_buf[0] = '\0';
    }
    return 0;
}
#else
static int http_post_json(const char *host, int port, const char *path,
                           const char *bearer_token,
                           const char *json_body,
                           char *resp_buf, size_t resp_buf_len,
                           int *out_status) {
    (void)host; (void)port; (void)path; (void)bearer_token; (void)json_body;
    (void)resp_buf; (void)resp_buf_len; (void)out_status;
    return -1;
}
#endif

// http_extract_json_string_field is a minimal, non-general JSON scanner:
// finds "field":"value" (string field only, single level of backslash
// escapes skipped rather than decoded) and copies value into out
// (NUL-terminated, truncated to out_len-1). Returns 1 if found, 0 if not.
// Deliberately not a real JSON parser — used only against IDUNA's own
// controlled response shape, never adversarial input.
static int http_extract_json_string_field(const char *json, const char *field,
                                           char *out, size_t out_len) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p = strchr(p + strlen(needle), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

#endif
