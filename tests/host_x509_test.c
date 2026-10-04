/* Host X.509 chain validation test against REAL site certificate chains.
 * NOT kernel code. Fixtures: /tmp/chain_fix/<site>/cert{0,1,2}.der
 * (generated with openssl x509 -outform DER from openssl s_client -showcerts)
 * Build: gcc -O2 -Ikernel tests/host_x509_test.c kernel/x509.c kernel/rsa.c \
 *        kernel/bn.c kernel/crypto_ec_nist.c kernel/crypto.c kernel/sha512.c \
 *        kernel/rtc.c kernel/string.c
 */
#include <stdio.h>
#include "types.h"
#include "crypto_x509.h"
void *memset(void*,int,unsigned long);
void *memcpy(void*,const void*,unsigned long);
int   memcmp(const void*,const void*,unsigned long);

static int load_chain(const char *site, crypto_x509_cert_t *certs, int max_certs) {
    char path[512];
    static u8 bufs[8][8192];
    int count = 0;
    for (int i = 0; i < 8 && count < max_certs; i++) {
        snprintf(path, sizeof(path), "/tmp/chain_fix/%s/cert%d.der", site, i);
        FILE *f = fopen(path, "rb");
        if (!f) break;
        int cl = (int)fread(bufs[count], 1, sizeof(bufs[count]), f);
        fclose(f);
        if (crypto_x509_parse(&certs[count], bufs[count], cl) != X509_OK) {
            printf("parse fail at cert %d\n", i);
            return -1;
        }
        count++;
    }
    return count;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int fails = 0;
    const char *sites[3] = {
        "cubestudio-dev.github.io",
        "www.google.com",
        "www.cloudflare.com",
    };
    for (int s = 0; s < 3; s++) {
        crypto_x509_cert_t certs[8];
        int n = load_chain(sites[s], certs, 8);
        if (n <= 0) {
            printf("%-28s FAIL (load/parse error)\n", sites[s]);
            fails++;
            continue;
        }
        printf("%-28s parsed %d certs; leaf CN: %s; SAN[0]: %s\n",
               sites[s], n, certs[0].subject, certs[0].net_dns_names[0]);
        int rc = crypto_x509_verify_chain(certs, n, sites[s]);
        printf("%-28s verify: %d (%s) %s\n", sites[s], rc,
               crypto_x509_errstr(rc), rc == X509_OK ? "PASS" : "FAIL");
        if (rc != X509_OK) fails++;
        int rc2 = crypto_x509_verify_chain(certs, n, "evil.example.com");
        printf("%-28s wrong-host: %d (%s) %s\n", sites[s], rc2,
               crypto_x509_errstr(rc2), rc2 == X509_E_HOSTNAME ? "PASS" : "FAIL");
        if (rc2 != X509_E_HOSTNAME) fails++;
    }
    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
