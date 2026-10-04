/* Host RSA verify test with openssl-generated fixtures. NOT kernel code.
 * Build: gcc -O2 -Ikernel tests/host_rsa_test.c kernel/rsa.c kernel/bn.c \
 *        kernel/crypto.c kernel/sha512.c kernel/string.c
 * Fixtures in /tmp/rsa_fix (regenerate via the script in this test's source).
 */
#include <stdio.h>
#include "types.h"
#include "crypto_rsa.h"
void *memset(void*,int,unsigned long);
void *memcpy(void*,const void*,unsigned long);
int   memcmp(const void*,const void*,unsigned long);

static unsigned char N[576], E[8], SIG[576], HASH[32], HASH_PSS[32], SIG_PSS[576];
static int N_LEN, E_LEN, SIG_LEN, SIG_PSS_LEN;

static int load(const char *dir, const char *name, unsigned char *buf, int max) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/tmp/rsa_fix";
    N_LEN = load(dir, "n.bin", N, sizeof(N));
    E_LEN = load(dir, "e.bin", E, sizeof(E));
    SIG_LEN = load(dir, "sig_pkcs1.bin", SIG, sizeof(SIG));
    int h1 = load(dir, "hash.bin", HASH, 32);
    SIG_PSS_LEN = load(dir, "sig_pss.bin", SIG_PSS, sizeof(SIG_PSS));
    int h2 = load(dir, "hash_pss.bin", HASH_PSS, 32);
    if (N_LEN <= 0 || E_LEN <= 0 || SIG_LEN <= 0 || h1 != 32 || h2 != 32) {
        printf("fixture load error\n");
        return 2;
    }
    int fails = 0;
    int v = crypto_rsa_verify_pkcs1(N, N_LEN, E, E_LEN, RSA_SHA256, HASH, 32, SIG, SIG_LEN);
    printf("%-42s %s (v=%d)\n", "pkcs1 verify openssl sig", v == 1 ? "PASS" : "FAIL", v);
    if (v != 1) fails++;
    SIG[100] ^= 0x40;
    v = crypto_rsa_verify_pkcs1(N, N_LEN, E, E_LEN, RSA_SHA256, HASH, 32, SIG, SIG_LEN);
    printf("%-42s %s (v=%d)\n", "pkcs1 corrupted rejected", v == 0 ? "PASS" : "FAIL", v);
    if (v != 0) fails++;
    SIG[100] ^= 0x40;
    v = crypto_rsa_verify_pss(N, N_LEN, E, E_LEN, RSA_SHA256, HASH_PSS, 32, SIG_PSS, SIG_PSS_LEN);
    printf("%-42s %s (v=%d)\n", "pss verify openssl sig (salt=32)", v == 1 ? "PASS" : "FAIL", v);
    if (v != 1) fails++;
    HASH_PSS[0] ^= 1;
    v = crypto_rsa_verify_pss(N, N_LEN, E, E_LEN, RSA_SHA256, HASH_PSS, 32, SIG_PSS, SIG_PSS_LEN);
    printf("%-42s %s (v=%d)\n", "pss wrong hash rejected", v == 0 ? "PASS" : "FAIL", v);
    if (v != 0) fails++;
    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
