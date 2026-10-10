/* Standalone driver for packages/common/rg_account.h: tests/test_rg_account.py runs it against a stub
   IDUNA + a fake browser. Usage: test_rg_account <base_url> <account_file> <login_timeout_s> */
#include "../packages/common/rg_account.h"
int main(int argc, char **argv) {
    if (argc < 4) return 2;
    RgAccount a;
    rg_account_init(&a, argv[1], NULL, argv[2]);
    a.login_timeout_s = atoi(argv[3]);
    rg_account_load(&a);
    unsigned char t[RG_TICKET_LEN];
    if (!rg_account_ticket(&a, t)) { printf("NOTICKET\n"); return 1; }
    printf("TICKET ");
    for (int i = 0; i < RG_TICKET_LEN; i++) printf("%02x", t[i]);
    printf("\nNAME %s\nPID %s\n", a.name, a.player_id);
    return 0;
}
