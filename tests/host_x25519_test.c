/* Host-side unit test for curve25519.c — RFC 7748 vectors. NOT kernel code.
 * Build: gcc -O2 -Ikernel tests/host_x25519_test.c kernel/curve25519.c \
 *        kernel/string.c -o /tmp/crypto_x25519_test
 */
#include <stdio.h>
#include "types.h"
#include "crypto_curve25519.h"

void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
int   memcmp(const void *a, const void *b, unsigned long n);

static int hex2bin(const char *h, u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (u8)v;
    }
    return 0;
}
static int fails = 0;
static void check(const char *name, int ok) {
    printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) fails++;
}

/* RFC 7748 5.2 Test vector 1 */
static const char *TV1_SCALAR =
    "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4";
static const char *TV1_U =
    "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c";
static const char *TV1_OUT =
    "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552";
/* RFC 7748 5.2 Test vector 2 */
static const char *TV2_SCALAR =
    "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d";
static const char *TV2_U =
    "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493";
static const char *TV2_OUT =
    "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957";
/* RFC 7748 6.1 */
static const char *ALICE_PRIV =
    "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
static const char *ALICE_PUB =
    "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
static const char *BOB_PRIV =
    "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
static const char *BOB_PUB =
    "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
static const char *SHARED =
    "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";

int main(void) {
    u8 s[32], u[32], want[32], got[32];

    hex2bin(TV1_SCALAR, s, 32);
    hex2bin(TV1_U, u, 32);
    hex2bin(TV1_OUT, want, 32);
    x25519(got, s, u);
    check("x25519 RFC7748 5.2 vector1", memcmp(got, want, 32) == 0);

    hex2bin(TV2_SCALAR, s, 32);
    hex2bin(TV2_U, u, 32);
    hex2bin(TV2_OUT, want, 32);
    x25519(got, s, u);
    check("x25519 RFC7748 5.2 vector2", memcmp(got, want, 32) == 0);

    u8 ap[32], appub[32], bp[32], bppub[32], sh1[32], sh2[32];
    hex2bin(ALICE_PRIV, ap, 32);
    hex2bin(ALICE_PUB, want, 32);
    crypto_x25519_public(ap, appub);
    check("x25519_public alice RFC7748 6.1", memcmp(appub, want, 32) == 0);

    hex2bin(BOB_PRIV, bp, 32);
    hex2bin(BOB_PUB, want, 32);
    crypto_x25519_public(bp, bppub);
    check("x25519_public bob RFC7748 6.1", memcmp(bppub, want, 32) == 0);

    hex2bin(SHARED, want, 32);
    crypto_x25519_shared(ap, bppub, sh1);
    crypto_x25519_shared(bp, appub, sh2);
    check("x25519 shared alice==bob", memcmp(sh1, sh2, 32) == 0);
    check("x25519 shared == RFC7748 6.1", memcmp(sh1, want, 32) == 0);

    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
