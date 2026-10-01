/* Host-side TLS 1.3 key schedule test against RFC 8448 "Implementation
 * Notes" example vectors (single-run X25519, AES-128-GCM, no session
 * resumption).  Verifies the exact HKDF chain used by kernel/tls.c:
 *   early secret -> derived -> handshake secret -> hs traffic secrets
 *   -> master secret -> ap traffic secrets
 * NOT kernel code.
 * Build: gcc -O2 -Ikernel tests/host_tls13_ks_test.c kernel/crypto.c \
 *        kernel/sha512.c kernel/string.c tests/host_pmm_stub.c
 */
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "string.h"
#include "crypto.h"

static void hex2bin(const char *h, u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v; sscanf(h + 2 * i, "%2x", &v);
        out[i] = (u8)v;
    }
}
static void dump(const char *t, const u8 *b, int n) {
    printf("%s", t);
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}
static int check(const char *name, const u8 *got, const char *want_hex, int n) {
    u8 want[64];
    hex2bin(want_hex, want, n);
    int ok = 1;
    for (int i = 0; i < n; i++) if (got[i] != want[i]) { ok = 0; break; }
    printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) dump("    got:  ", got, n);
    return ok ? 0 : 1;
}

/* RFC 8448 section 3 (hash of "" is tested implicitly) */
static const char *RFC_EMPTY_HASH =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

int main(void) {
    int fails = 0;
    /* sha256("") */
    u8 eh[32];
    sha256(NULL, 0, eh);
    fails += check("sha256(empty)", eh, RFC_EMPTY_HASH, 32);

    /* RFC 8448 3: client handshake traffic secret
     * inputs: early secret -> derived("") -> HKDF-Extract(shared)
     *         -> Derive-Secret("c hs traffic", TH1)                    */
    u8 early[32], derived[32], hs_secret[32], c_hs[32], s_hs[32];
    u8 shared[32], th1[32], zeros[32];
    hex2bin(
        "8bd4054fb55b9d63fdfbacf9f04b9f0d35e6d63f537563efd46272900f89492d",
        shared, 32);
    hex2bin(
        "860c06edc07858ee8e78f0e7428c58edd6b43f2ca3e6e95f02ed063cf0e1cad8",
        th1, 32);
    oc_memset(zeros, 0, 32);

    hmac_sha256(zeros, 32, zeros, 32, early);           /* early secret */
    fails += check("early secret", early,
        "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a", 32);

    tls13_ks_derive_secret(early, "derived", eh, 32, derived);
    fails += check("derived secret", derived,
        "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba", 32);

    hmac_sha256(derived, 32, shared, 32, hs_secret);    /* handshake secret */
    fails += check("handshake secret", hs_secret,
        "1dc826e93606aa6fdc0aadc12f741b01046aa6b99f691ed221a9f0ca043fbeac", 32);

    tls13_ks_derive_secret(hs_secret, "c hs traffic", th1, 32, c_hs);
    fails += check("client handshake traffic secret", c_hs,
        "b3eddb126e067f35a780b3abf45e2d8f3b1a950738f52e9600746a0e27a55a21", 32);
    tls13_ks_derive_secret(hs_secret, "s hs traffic", th1, 32, s_hs);
    fails += check("server handshake traffic secret", s_hs,
        "b67b7d690cc16c4e75e54213cb2d37b4e9c912bcded9105d42befd59d391ad38", 32);

    /* RFC 8448 3: application traffic secrets — both bind the SAME
     * transcript (ClientHello...server Finished, hash 960810...). */
    u8 master[32], c_ap[32], s_ap[32], th_ap[32];
    hex2bin(
        "9608102a0f1ccc6db6250b7b7e417b1a000eaada3daae4777a7686c9ff83df13",
        th_ap, 32);
    tls13_ks_derive_secret(hs_secret, "derived", eh, 32, derived);
    hmac_sha256(derived, 32, zeros, 32, master);
    fails += check("master secret", master,
        "18df06843d13a08bf2a449844c5f8a478001bc4d4c627984d5a41da8d0402919", 32);
    tls13_ks_derive_secret(master, "c ap traffic", th_ap, 32, c_ap);
    fails += check("client application traffic secret", c_ap,
        "9e40646ce79a7f9dc05af8889bce6552875afa0b06df0087f792ebb7c17504a5", 32);
    tls13_ks_derive_secret(master, "s ap traffic", th_ap, 32, s_ap);
    fails += check("server application traffic secret", s_ap,
        "a11af9f05531f856ad47116b45a950328204b4f44bfb6b3a4b4f1f3fcb631643", 32);

    /* finished keys (RFC 8448 3: client finished key, empty context) */
    u8 c_fk[32];
    tls13_ks_expand_label(c_hs, 32, "finished", NULL, 0, c_fk, 32);
    fails += check("client finished key", c_fk,
        "b80ad01015fb2f0bd65ff7d4da5d6bf83f84821d1f87fdc7d3c75b5a7b42d9c4", 32);

    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
