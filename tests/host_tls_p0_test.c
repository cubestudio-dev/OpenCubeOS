/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 *
 * Host-side reproduction suite for the TLS P0 batch of WP-AUDIT-01-p0fix2
 * (BUG-0021..BUG-0027; this file covers 0022/0023/0024/0025/0026/0027).
 *
 * The kernel file net/net_tls.c is included directly so the tests exercise
 * the EXACT kernel code (including its statics).  Kernel services (TCP,
 * screen, heap, RNG, X.509 chain trust) are stubbed; the stubs only provide
 * transport and trust so the tested length-checking logic is reached.
 * Build with -fsanitize=address: on the UNFIXED baseline every malicious
 * case aborts with a heap/global-buffer-overflow; on the fixed code every
 * case returns a clean error and ASAN stays silent.
 *
 * Build (fixed kernel, PASS expected):
 *   gcc -fsanitize=address -g -O1 -Inet -Ikernel -Ikernel/crypto \
 *       -Ikernel/mem -Ikernel/lib -Idrivers/display -Ikernel/core \
 *       -Ikernel/arch/x86_64 tests/host_tls_p0_test.c -o /tmp/host_tls_p0
 * Build (baseline kernel, FAIL expected):
 *   git show <baseline>:oc-os/net/net_tls.c > /tmp/net_tls_base.c
 *   ... same command with tests/host_tls_p0_test.c including the baseline
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* lib_string.h declares strchr(const char*, char) — a different signature
 * from the host libc.  Include libc first, then rename the kernel decl. */
#define strchr oc_kernel_strchr_decl
#include "../net/net_tls.c"   /* code under test */
#undef strchr

#ifndef TLS12_REC_MAX
#define TLS12_REC_MAX 18432   /* baseline builds predate the P0fix2 macro */
#endif

/* ---------------- mock server byte stream ---------------- */
static u8 g_stream[300000];
static int g_stream_len, g_stream_off;

static void stream_set(const u8 *d, int n) {
    memcpy(g_stream, d, n);
    g_stream_len = n;
    g_stream_off = 0;
}

/* ---------------- kernel service stubs ---------------- */
int net_recv(int fd, void *buf, int len) {
    (void)fd;
    if (g_stream_off >= g_stream_len) return -1;   /* EOF */
    int give = len;
    if (give > g_stream_len - g_stream_off) give = g_stream_len - g_stream_off;
    memcpy(buf, g_stream + g_stream_off, give);
    g_stream_off += give;
    return give;
}
int net_send(int fd, const void *data, int len) { (void)fd; (void)data; return len; }
int net_socket(int type) { (void)type; return 7; }
int net_connect(int fd, u32 ip, u16 port) { (void)fd; (void)ip; (void)port; return 0; }
int net_close(int fd) { (void)fd; return 0; }
int net_tcp_established(int fd) { (void)fd; return 0; }
void net_poll(void) {}
void screen_console_puts(const char *s) { (void)s; }
void screen_console_putc(char c) { (void)c; }
usize u64_to_str(u64 v, char *out) { (void)v; out[0] = '0'; out[1] = 0; return 1; }
void *kmalloc(u64 size) { return malloc((size_t)size); }
void kfree(void *p) { free(p); }
void crypto_random(u8 *buf, int len) { for (int i = 0; i < len; i++) buf[i] = (u8)(i * 7 + 3); }
u64 core_timer_ticks(void) { return 12345; }

/* crypto primitives: boundary tests only, cryptography itself is stubbed */
void sha256(const u8 *data, int len, u8 out[32]) { (void)data; (void)len; memset(out, 0xAB, 32); }
void sha384(const u8 *data, int len, u8 out[48]) { (void)data; (void)len; memset(out, 0xCD, 48); }
void crypto_hmac_sha256(const u8 *k, int kl, const u8 *d, int n, u8 out[32]) {
    (void)k; (void)kl; (void)d; (void)n; memset(out, 0x11, 32);
}
void crypto_aes128_cbc_decrypt(const u8 key[16], const u8 iv[16], const u8 *in,
                               int in_len, u8 *out) {
    (void)key; (void)iv;
    for (int off = 0; off < in_len; off += 16)
        for (int i = 0; i < 16; i++) out[off + i] = (u8)(in[off + i] ^ 0x5A);
}
int crypto_aes128_gcm_open(const u8 *k, const u8 *n, const u8 *a, int al,
                           const u8 *ct, int cl, u8 *pt, const u8 *tag) {
    (void)k; (void)n; (void)a; (void)al; (void)tag;
    for (int i = 0; i < cl; i++) pt[i] = ct[i];
    return 0;
}
int crypto_aes256_gcm_open(const u8 *k, const u8 *n, const u8 *a, int al,
                           const u8 *ct, int cl, u8 *pt, const u8 *tag) {
    return crypto_aes128_gcm_open(k, n, a, al, ct, cl, pt, tag);
}
int chacha20poly1305_open(const u8 *k, const u8 *n, const u8 *a, int al,
                          const u8 *ct, int cl, u8 *pt, const u8 *tag) {
    return crypto_aes128_gcm_open(k, n, a, al, ct, cl, pt, tag);
}
int crypto_aes128_gcm_seal(const u8 *k, const u8 *n, const u8 *a, int al,
                           const u8 *pt, int pl, u8 *ct, u8 *tag) {
    (void)k; (void)n; (void)a; (void)al;
    for (int i = 0; i < pl; i++) ct[i] = pt[i];
    memset(tag, 0x77, 16); return 0;
}
int crypto_aes256_gcm_seal(const u8 *k, const u8 *n, const u8 *a, int al,
                           const u8 *pt, int pl, u8 *ct, u8 *tag) {
    return crypto_aes128_gcm_seal(k, n, a, al, pt, pl, ct, tag);
}
int chacha20poly1305_seal(const u8 *k, const u8 *n, const u8 *a, int al,
                          const u8 *pt, int pl, u8 *ct, u8 *tag) {
    return crypto_aes128_gcm_seal(k, n, a, al, pt, pl, ct, tag);
}

/* X.509 trust stubs: ONLY so the tests reach the ServerKeyExchange /
 * Certificate length checks.  (A real chain is impossible to forge; these
 * stubs are the trust anchor, the code under test is the length logic.) */
int crypto_x509_parse(crypto_x509_cert_t *c, const u8 *der, int len) {
    (void)der; (void)len;
    memset(c, 0, sizeof(*c));
    return X509_OK;
}
int crypto_x509_verify_chain(const crypto_x509_cert_t *ch, int n, const char *host) {
    (void)ch; (void)host;
    return (n > 0) ? X509_OK : X509_E_FORMAT;
}
int crypto_rsa_verify_pkcs1(const u8 *n, int nl, const u8 *e, int el, int alg,
                            const u8 *h, int hl, const u8 *sig, int sl) {
    (void)n; (void)nl; (void)e; (void)el; (void)alg; (void)h; (void)hl; (void)sig;
    return (sl > 0) ? 1 : 0;
}
int crypto_rsa_verify_pss(const u8 *n, int nl, const u8 *e, int el, int alg,
                          const u8 *h, int hl, const u8 *sig, int sl) {
    return crypto_rsa_verify_pkcs1(n, nl, e, el, alg, h, hl, sig, sl);
}
void crypto_dh_modexp_n(const u8 *b, const u8 *x, const u8 *m, u8 *out, int len) {
    (void)b; (void)x; (void)m;
    memset(out, 0x22, (size_t)len);
}
int crypto_x25519_shared(const u8 *priv, const u8 *pub, u8 *out) {
    (void)priv; (void)pub; memset(out, 0x33, 32); return 0;
}
int crypto_x25519_public(const u8 priv[32], u8 pub[32]) {
    (void)priv; memset(pub, 0x34, 32); return 0;
}
int crypto_ec_pub_from_priv(int curve, const u8 *priv, int priv_len,
                            u8 *pub_out, int pub_len) {
    (void)curve; (void)priv; (void)priv_len;
    memset(pub_out, 0x35, (size_t)pub_len); return 0;
}
const char *crypto_x509_errstr(int rc) { (void)rc; return "x509-err"; }
void crypto_aes128_cbc_encrypt(const u8 key[16], const u8 iv[16], const u8 *in,
                               int in_len, u8 *out) {
    (void)key; (void)iv;
    for (int off = 0; off < in_len; off += 16)
        for (int i = 0; i < 16; i++) out[off + i] = (u8)(in[off + i] ^ 0x5A);
}
int crypto_ec_ecdh(int curve, const u8 *priv, int pl, const u8 *pub, int publ,
                   u8 *out, int outl) {
    (void)curve; (void)priv; (void)pl; (void)pub; (void)publ;
    memset(out, 0x33, (size_t)outl); return 0;
}
int ecdsa_verify(int curve, const u8 *pub, int pub_len,
                 const u8 *hash, int hash_len,
                 const u8 *r, int r_len, const u8 *s, int s_len) {
    (void)curve; (void)pub; (void)pub_len; (void)hash; (void)hash_len;
    (void)r; (void)r_len; (void)s; (void)s_len;
    return 1;
}
void net_tls13_ks_expand_label(const u8 *secret, int slen, const char *label,
                               const u8 *context, int ctx_len,
                               u8 *out, int out_len) {
    (void)secret; (void)slen; (void)label; (void)context; (void)ctx_len;
    memset(out, 0x44, (size_t)out_len);
}
void net_tls13_ks_derive_secret(const u8 *secret, const char *label,
                                const u8 *thash, int thash_len, u8 out[32]) {
    (void)secret; (void)label; (void)thash; (void)thash_len;
    memset(out, 0x55, 32);
}

/* ---------------- test helpers ---------------- */
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, name) do { \
    if (cond) { printf("  PASS %s\n", name); g_pass++; } \
    else      { printf("  FAIL %s\n", name); g_fail++; } \
} while (0)

static net_tls_ctx_t fresh_ctx(int cipher, int encrypted) {
    net_tls_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.net_tcp_sock = 7;
    c.cipher = cipher;
    c.net_tls12_encrypted = encrypted;
    for (int i = 0; i < 32; i++) { c.client_random[i] = (u8)i; c.server_random[i] = (u8)(i ^ 0x5A); }
    return c;
}

static void put_rec(u8 *dst, int *off, u8 ctype, const u8 *payload, int plen) {
    dst[(*off)++] = ctype; dst[(*off)++] = 3; dst[(*off)++] = 3;
    dst[(*off)++] = (u8)(plen >> 8); dst[(*off)++] = (u8)plen;
    memcpy(dst + *off, payload, plen); *off += plen;
}

/* minimal TLS1.2 ServerHello handshake message (02 || len3 || 03 03 || rnd32
 * || sid_len 0 || cipher 2 || compression 0) with NO supported_versions ext */
static int build_sh_msg(u8 *out, int cipher16) {
    int o = 0;
    out[o++] = 2; out[o++] = 0; out[o++] = 0; out[o++] = 38;
    out[o++] = 3; out[o++] = 3;
    for (int i = 0; i < 32; i++) out[o++] = (u8)(i ^ 0xA5);
    out[o++] = 0;
    out[o++] = (u8)(cipher16 >> 8); out[o++] = (u8)cipher16;
    out[o++] = 0;
    return o;   /* 42 */
}

static void prefill_tls12_state(int cipher16) {
    u8 msg[64];
    int n = build_sh_msg(msg, cipher16);
    memcpy(g_tls12_ch, "\x16\x03\x03\x00\x04\x01\x00\x00\x00", 9);
    g_tls12_ch_len = 9;
    memcpy(g_tls12_sh, msg, n);
    g_tls12_sh_len = n;
}

/* ---------------- t22: BUG-0022 (A14-16) ---------------- */
static void t22(void) {
    printf("t22 BUG-0022: TLS1.3 record len=16896 into a 16640-byte buffer\n");
    net_tls_ctx_t c = fresh_ctx(0, 0);
    u8 *buf = (u8 *)malloc(16640);
    static u8 rec[5 + 16896];
    int off = 0;
    static u8 payload[16896];
    put_rec(rec, &off, 0x16, payload, 16896);
    stream_set(rec, off);
    int ct = 0;
    int rc = net_tls13_recv_record(&c, &ct, buf, 16640);
    /* baseline: ASAN aborts (heap-buffer-overflow). fixed: rc == -1 */
    EXPECT(rc == -1, "record len 16896 rejected on cap 16640");

    /* sanity: a legal 16640 record must still be accepted */
    off = 0; put_rec(rec, &off, 0x16, payload, 16640);
    stream_set(rec, off);
    rc = net_tls13_recv_record(&c, &ct, buf, 16640);
    EXPECT(rc == 16640, "record len 16640 (=cap) still accepted");
    free(buf);
}

/* ---------------- t23: BUG-0023 (A14-17) ---------------- */
static void t23(void) {
    printf("t23 BUG-0023: TLS1.2 record len=18720 into old-style cap 16640\n");
    static u8 payload[18720];
    static u8 rec[5 + 18720];
    int off = 0;
    put_rec(rec, &off, 0x16, payload, 18720);

    net_tls_ctx_t c = fresh_ctx(CS_TLS12_DHE_RSA_AES128_CBC_SHA256, 0);
    u8 *buf = (u8 *)malloc(16640);
    stream_set(rec, off);
    int ct = 0;
    int rc = net_tls12_recv_record(&c, &ct, buf, 16640);
    /* baseline: ASAN aborts (2080 bytes past the buffer). fixed: rc == -1 */
    EXPECT(rc == -1, "record len 18720 rejected on cap 16640");
    free(buf);

    /* a legitimately full 18432-byte CBC record must now FIT (it used to
     * overflow even with no attacker at all) */
    net_tls_ctx_t c2 = fresh_ctx(CS_TLS12_DHE_RSA_AES128_CBC_SHA256, 1);
    for (int i = 0; i < 32; i++) c2.mac_key_r[i] = (u8)i;
    for (int i = 0; i < 16; i++) c2.enc_key_r[i] = (u8)(i + 1);
    c2.seq12_r = 0;
    u8 *buf2 = (u8 *)malloc(TLS12_REC_MAX);
    memset(payload, 0xA5, 18432);
    /* plaintext tail: pad byte must leave content >= 0; last byte = pad */
    payload[18431] = 0;
    off = 0; put_rec(rec, &off, 0x16, payload, 18432);
    stream_set(rec, off);
    rc = net_tls12_recv_record(&c2, &ct, buf2, TLS12_REC_MAX);
    /* rc == -2 (MAC mismatch, expected with stubbed MAC) but NO overflow */
    EXPECT(rc == -2, "legal 18432-byte CBC record processed without overflow");
    free(buf2);
}

/* ---------------- t24: BUG-0024 (A14-18) ---------------- */
static void t24(void) {
    printf("t24 BUG-0024: Certificate message mlen=16384 vs body (old 12288)\n");
    prefill_tls12_state(0x0067);
    static u8 cert_body[4 + 16384];
    cert_body[0] = 0x0B; cert_body[1] = 0x00; cert_body[2] = 0x40; cert_body[3] = 0x00;
    static u8 rec[5 + 4 + 16384];
    int off = 0;
    put_rec(rec, &off, 0x16, cert_body, 4 + 16384);
    stream_set(rec, off);
    net_tls_ctx_t c = fresh_ctx(0x0067, 0);
    int rc = net_tls12_resume_from_sh(&c);
    /* baseline: ASAN aborts (4096 bytes past body[12288]). fixed: rc == -1 */
    EXPECT(rc == -1, "Certificate mlen 16384 rejected (body too small)");
}

/* ---------------- t25: BUG-0025 (A14-19) ---------------- */
static void t25(void) {
    printf("t25 BUG-0025: ServerKeyExchange mlen unchecked vs body\n");
    prefill_tls12_state(0x0067);
    /* record 1: small legal Certificate (parse stub OK, verify stub OK) */
    static u8 cert_msg[4 + 19];
    cert_msg[0] = 0x0B; cert_msg[1] = 0x00; cert_msg[2] = 0x00; cert_msg[3] = 19;
    cert_msg[4] = 0x00; cert_msg[5] = 0x00; cert_msg[6] = 19;
    cert_msg[7] = 0x00; cert_msg[8] = 0x00; cert_msg[9] = 13;
    memset(cert_msg + 10, 0x30, 13);
    /* record 2: SKE with message length 18716 (payload is 18720 -> the old
     * record-layer check passes; the old code then copies 18716 bytes into
     * body, which was 12288 on the baseline and is 18432 fixed) */
    static u8 ske_body[4 + 18716];
    ske_body[0] = 0x0C; ske_body[1] = 0x00; ske_body[2] = 0x49; ske_body[3] = 0x1C;
    memset(ske_body + 4, 0x41, 18716);
    static u8 rec[5 + 4 + 19 + 5 + 4 + 18716];
    int off = 0;
    put_rec(rec, &off, 0x16, cert_msg, 4 + 19);
    put_rec(rec, &off, 0x16, ske_body, 4 + 18716);
    stream_set(rec, off);
    net_tls_ctx_t c = fresh_ctx(0x0067, 0);
    int rc = net_tls12_resume_from_sh(&c);
    /* baseline: ASAN aborts (OOB write into body + OOB read from rec).
     * fixed: rc == -1 (mlen > sizeof(body)) */
    EXPECT(rc == -1, "SKE mlen 18716 rejected (beyond body capacity)");
}

/* ---------------- t26: BUG-0026 (A14-20) ---------------- */
static void t26(void) {
    printf("t26 BUG-0026: DHE ServerDHParams 3x256B vs sbuf tail (old 320)\n");
    prefill_tls12_state(0x0067);
    static u8 cert_msg[4 + 19];
    cert_msg[0] = 0x0B; cert_msg[1] = 0x00; cert_msg[2] = 0x00; cert_msg[3] = 19;
    cert_msg[4] = 0x00; cert_msg[5] = 0x00; cert_msg[6] = 19;
    cert_msg[7] = 0x00; cert_msg[8] = 0x00; cert_msg[9] = 13;
    memset(cert_msg + 10, 0x30, 13);
    /* SKE: p/g/Ys each 2-byte length 0x00FF + 256 bytes, alg 0x0804,
     * sig_len 32 -> params 774 bytes total */
    static u8 ske[4 + 810];
    int o = 0;
    ske[o++] = 0x0C; ske[o++] = 0x00; ske[o++] = 0x03; ske[o++] = 0x2A;
    for (int k = 0; k < 3; k++) {
        ske[o++] = 0x01; ske[o++] = 0x00;   /* 0x0100 = 256 */
        for (int i = 0; i < 256; i++) ske[o++] = (u8)(i + k);
    }
    ske[o++] = 0x08; ske[o++] = 0x04;
    ske[o++] = 0x00; ske[o++] = 0x20;
    for (int i = 0; i < 32; i++) ske[o++] = (u8)(i ^ 0x11);
    static u8 rec[5 + 4 + 19 + 5 + 814];
    int off = 0;
    put_rec(rec, &off, 0x16, cert_msg, 4 + 19);
    put_rec(rec, &off, 0x16, ske, o);
    stream_set(rec, off);
    net_tls_ctx_t c = fresh_ctx(0x0067, 0);
    int rc = net_tls12_resume_from_sh(&c);
    /* baseline: ASAN aborts (774 bytes into a 320-byte sbuf tail).
     * fixed: params fit (sbuf tail 776); the handshake continues and ends
     * cleanly at EOF while waiting for ServerHelloDone (rc == -1). */
    printf("  (t26 rc=%d cipher=0x%04x ncerts=%d)\n", rc, c.cipher, c.ncerts);
    EXPECT(rc == -1, "full-size legal DHE params handled without overflow");
}

/* ---------------- t27: BUG-0027 (A14-21) ---------------- */
#ifdef HOST_TLS_BASELINE
/* Line-for-line copy of the BASELINE inline DER decode (see the fix diff):
 * the two memcpy targets/lengths are attacker-controlled.  Used only to
 * demonstrate the defect behaviour deterministically under ASAN. */
static int baseline_decode_demo(const u8 *sig, int sig_len) {
    u8 r[32], s[32];
    const u8 *q = sig;
    if (q[0] != 0x30) return -5;
    int tot = q[1]; (void)tot;
    int p2 = 2;
    if (q[p2++] != 0x02) return -5;
    int rl = q[p2++];
    memcpy(r + (32 - rl), q + p2, rl);          /* rl=255: writes r-223.. */
    p2 += rl;
    if (q[p2++] != 0x02) return -5;
    int sl = q[p2++];
    memcpy(s + (32 - sl), q + p2, sl);
    return 0;
}
#endif

static void t27(void) {
    printf("t27 BUG-0027: ECDSA CertificateVerify DER decode bounds\n");
#ifdef HOST_TLS_BASELINE
    /* baseline demonstration: rl=0xFF with a short signature buffer */
    u8 sig[10] = { 0x30, 0x06, 0x02, 0xFF, 1, 2, 3, 4, 5, 6 };
    int rc = baseline_decode_demo(sig, 10);
    (void)rc;
    printf("  (baseline demo returned %d without ASAN abort = unexpected)\n", rc);
#else
    u8 r[32], s[32];
    /* legal signature: r=32 bytes, s=32 bytes */
    u8 ok[70];
    int o = 0;
    ok[o++] = 0x30; ok[o++] = 68;
    ok[o++] = 0x02; ok[o++] = 32; for (int i = 0; i < 32; i++) ok[o++] = (u8)i;
    ok[o++] = 0x02; ok[o++] = 32; for (int i = 0; i < 32; i++) ok[o++] = (u8)(i ^ 1);
    EXPECT(tls13_decode_ecdsa_sig(ok, o, r, s) == 0, "legal 32/32 signature decodes");
    EXPECT(r[31] == 31 && s[31] == 30, "r/s big-endian right-aligned");

    /* rl = 255 must be rejected (baseline wrote r-223 .. r+32) */
    u8 bad1[10] = { 0x30, 0x06, 0x02, 0xFF, 1, 2, 3, 4, 5, 6 };
    EXPECT(tls13_decode_ecdsa_sig(bad1, 10, r, s) == -5, "rl=255 rejected");
    /* rl = 33 must be rejected */
    u8 bad2[40] = { 0x30, 38, 0x02, 33 };
    memset(bad2 + 4, 1, 36);
    EXPECT(tls13_decode_ecdsa_sig(bad2, 40, r, s) == -5, "rl=33 rejected");
    /* rl = 0 must be rejected */
    u8 bad3[10] = { 0x30, 0x06, 0x02, 0x00, 0x02, 0x02, 1, 2, 3, 4 };
    EXPECT(tls13_decode_ecdsa_sig(bad3, 10, r, s) == -5, "rl=0 rejected");
    /* sl = 255 must be rejected */
    u8 bad4[70];
    o = 0;
    bad4[o++] = 0x30; bad4[o++] = 68;
    bad4[o++] = 0x02; bad4[o++] = 32; for (int i = 0; i < 32; i++) bad4[o++] = 1;
    bad4[o++] = 0x02; bad4[o++] = 32; for (int i = 0; i < 32; i++) bad4[o++] = 2;
    bad4[37] = 0xFF;   /* overwrite the s INTEGER length byte (index 37) */
    EXPECT(tls13_decode_ecdsa_sig(bad4, 70, r, s) == -5, "sl=255 rejected");
    /* truncated: sig_len claims more than the buffer logically holds */
    u8 bad5[10] = { 0x30, 0x50, 0x02, 0x20 };
    memset(bad5 + 4, 1, 6);
    EXPECT(tls13_decode_ecdsa_sig(bad5, 10, r, s) == -5, "tot beyond sig_len rejected");
    /* r bytes run past sig_len */
    u8 bad6[10] = { 0x30, 0x08, 0x02, 0x20, 1, 2, 3, 4, 5, 6 };
    EXPECT(tls13_decode_ecdsa_sig(bad6, 10, r, s) == -5, "r past sig_len rejected");
    /* too short */
    EXPECT(tls13_decode_ecdsa_sig(ok, 4, r, s) == -5, "sig_len < 8 rejected");
#endif
}

int main(void) {
    printf("=== host_tls_p0_test (WP-AUDIT-01-p0fix2 TLS P0 suite) ===\n");
    t22();
    t23();
    t24();
    t25();
    t26();
    t27();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
