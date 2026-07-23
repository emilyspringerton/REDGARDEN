// hmac_sha256_test.c — verifies hmac_sha256.h against the official RFC 4231
// HMAC-SHA256 test vectors (test cases 1 and 2) before this implementation
// is trusted for anything live. Not wired into the Makefile's normal build
// (no test runner exists yet in this repo for C code) — run manually:
//   gcc -Ipackages/common packages/common/hmac_sha256_test.c -o /tmp/hmac_test && /tmp/hmac_test
#include <stdio.h>
#include <string.h>
#include "hmac_sha256.h"

static int hex_to_bytes(const char *hex, uint8_t *out, int max_len) {
    int len = 0;
    while (*hex && len < max_len) {
        unsigned int byte;
        if (sscanf(hex, "%2x", &byte) != 1) return -1;
        out[len++] = (uint8_t)byte;
        hex += 2;
    }
    return len;
}

static void bytes_to_hex(const uint8_t *bytes, int len, char *out) {
    for (int i = 0; i < len; i++) sprintf(out + i*2, "%02x", bytes[i]);
    out[len*2] = 0;
}

static int run_case(const char *name, const char *key_hex, const char *data, const char *expected_hex) {
    uint8_t key[64];
    int key_len = hex_to_bytes(key_hex, key, sizeof(key));
    uint8_t mac[32];
    hmac_sha256(key, key_len, (const uint8_t*)data, strlen(data), mac);
    char got_hex[65];
    bytes_to_hex(mac, 32, got_hex);
    int pass = strcmp(got_hex, expected_hex) == 0;
    printf("%s %s\n  got:      %s\n  expected: %s\n", pass ? "PASS" : "FAIL", name, got_hex, expected_hex);
    return pass;
}

int main(void) {
    int all_pass = 1;
    // RFC 4231 test case 1
    all_pass &= run_case(
        "RFC4231 case 1",
        "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        "Hi There",
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"
    );
    // RFC 4231 test case 2
    all_pass &= run_case(
        "RFC4231 case 2",
        "4a656665",
        "what do ya want for nothing?",
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
    );

    if (all_pass) {
        printf("\nALL PASS\n");
        return 0;
    }
    printf("\nFAILED\n");
    return 1;
}
