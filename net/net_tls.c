/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/tls.c
 * Purpose: Mainstream TLS client: TLS 1.3 (RFC 8446) primary, TLS 1.2
 *          ECDHE_RSA-GCM/ChaCha20 fallback, legacy DHE-CBC retained.
 *          Certificate chains verified against embedded public roots;
 *          hostname matched against SAN dNSName.
 */
#include "net_tls.h"
#include "crypto_core.h"
#include "crypto_sha512.h"
#include "crypto_aead.h"
#include "crypto_curve25519.h"
#include "crypto_ec_nist.h"
#include "crypto_rsa.h"
#include "net_core.h"
#include "mem_heap.h"   /* WP-10-wp08fix1: lazy handshake buffers */
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"

/* bring-up debugging (0 = quiet, production; 1 = verbose during bring-up) */
#define TLS_DBG 0
#if TLS_DBG
#include "screen_console.h"
#define TLS_DBG_P(msg) screen_console_puts("[tls-dbg] " msg "\n")
__attribute__((unused)) static void net_tls_dbg_hex(const char *t, const u8 *b, int n) {
    screen_console_puts(t);
    char hx[3];
    for (int i = 0; i < n; i++) {
        u8 v = b[i];
        hx[0] = "0123456789abcdef"[v >> 4];
        hx[1] = "0123456789abcdef"[v & 15];
        hx[2] = 0;
        screen_console_puts(hx);
    }
    screen_console_puts("\n");
}
#else
#define TLS_DBG_P(msg)
__attribute__((unused)) static void net_tls_dbg_hex(const char *t, const u8 *b, int n) { (void)t; (void)b; (void)n; }
#endif

/* record content types */
#define CT_CCS 20
#define CT_ALERT 21
#define CT_HANDSHAKE 22
#define CT_APPDATA 23

/* handshake message types */
#define HT_CLIENT_HELLO 1
#define HT_SERVER_HELLO 2
#define HT_NEW_SESSION_TICKET 4
#define HT_ENCRYPTED_EXTENSIONS 8
#define HT_CERTIFICATE 11
#define HT_SERVER_KEY_EXCHANGE 12
#define HT_CERTIFICATE_REQUEST 13
#define HT_SERVER_HELLO_DONE 14
#define HT_CERTIFICATE_VERIFY 15
#define HT_CLIENT_KEY_EXCHANGE 16
#define HT_FINISHED 20
#define HT_KEY_UPDATE 24

static net_tls_ctx_t g_tls;
static u8 g_th_srv[32];     /* transcript hash CH...server Finished */
static u8 g_x25519_priv[32];
static u8 g_x25519_pub[32];
static u8 g_ec_priv[48];
static u8 g_ec_pub[97];

/* Same-connection TLS 1.2 fallback (RFC 8446 4.1.2): the server answers
 * our dual-version ClientHello with a legacy TLS 1.2 ServerHello.  The
 * CH/SH messages are kept so the TLS 1.2 path can continue on this
 * connection without re-handshaking. */
static u8 g_tls12_ch[2048];
static int g_tls12_ch_len;
static u8 g_tls12_sh[8192];
static int g_tls12_sh_len;

/* DHE (0x0067) client public value, computed during ServerKeyExchange
 * processing and sent in ClientKeyExchange. */
static u8 g_dhe_yc[256];
static int g_dhe_yc_len;
static int g_tls12_ecdh_x25519;   /* server picked x25519 in SKE */

/* ---------------- helpers ---------------- */

static int net_tcp_read_exact(int sock, void *buf, int len) {
    u8 *p = (u8 *)buf;
    int got = 0;
    u64 deadline = core_timer_ticks() + 1500;   /* 15 s at 100 Hz */
    while (got < len) {
        int n = net_recv(sock, p + got, len - got);
        if (n < 0) return -1;
        if (n == 0) {
            if (core_timer_ticks() > deadline) return -1;
            net_poll();
            continue;
        }
        got += n;
    }
    return got;
}

static void transcript_push(net_tls_ctx_t *c, const void *data, int len) {
    if (c->transcript_len + len > (int)sizeof(c->transcript)) return;
    memcpy(c->transcript + c->transcript_len, data, len);
    c->transcript_len += len;
}

static void hs_log_push(net_tls_ctx_t *c, const void *data, int len) {
    if (c->hs_log_len + len > (int)sizeof(c->hs_log)) return;
    memcpy(c->hs_log + c->hs_log_len, data, len);
    c->hs_log_len += len;
}

/* HKDF-Expand-Label and Derive-Secret (RFC 8446 section 7.1) live in
 * crypto.c as net_tls13_ks_expand_label / net_tls13_ks_derive_secret so the
 * schedule can be verified with RFC 8448 vectors on the host. */
static void net_tls13_expand_label(const u8 *secret, int slen,
                               const char *label,
                               const u8 *context, int ctx_len,
                               u8 *out, int out_len) {
    net_tls13_ks_expand_label(secret, slen, label, context, ctx_len, out, out_len);
}

static void net_tls13_derive_secret(const u8 *secret, const char *label,
                                const u8 *thash, int thash_len, u8 out[32]) {
    net_tls13_ks_derive_secret(secret, label, thash, thash_len, out);
}

static void net_tls13_traffic_keys(const u8 *secret, u8 *key, u8 *iv) {
    net_tls13_expand_label(secret, 32, "key", NULL, 0, key, 16);
    net_tls13_expand_label(secret, 32, "iv", NULL, 0, iv, 12);
}

/* P0fix2 BUG-0027 (A14-21): decode a DER ECDSA signature into two 32-byte
 * big-endian integers r/s.  Returns 0 on success, -5 on any malformed or
 * out-of-bounds input.  (Extracted from the CertificateVerify handler so
 * tests/host_tls_p0_test.c can exercise the exact kernel code on the host.)
 * Every length and offset is bounds-checked against sig_len: the old inline
 * code took the DER INTEGER lengths (one byte, 0..255) straight into
 * memcpy(r + (32 - rl), ...), so rl=255 wrote r-223 .. r+31 — a stack smash
 * below the array. */
static int tls13_decode_ecdsa_sig(const u8 *sig, int sig_len, u8 r[32], u8 s[32]) {
    if (!sig || sig_len < 8) return -5;
    if (sig[0] != 0x30) return -5;
    int tot = sig[1];
    if (tot < 0 || 2 + tot > sig_len) return -5;
    int p2 = 2;
    if (p2 >= sig_len || sig[p2++] != 0x02) return -5;
    if (p2 >= sig_len) return -5;
    int rl = sig[p2++];
    if (rl < 1 || rl > 32 || p2 + rl > sig_len) return -5;
    memcpy(r + (32 - rl), sig + p2, rl);
    p2 += rl;
    if (p2 >= sig_len || sig[p2++] != 0x02) return -5;
    if (p2 >= sig_len) return -5;
    int sl = sig[p2++];
    if (sl < 1 || sl > 32 || p2 + sl > sig_len) return -5;
    memcpy(s + (32 - sl), sig + p2, sl);
    return 0;
}

static int transcript_hash(net_tls_ctx_t *c, u8 *out) {
    sha256(c->transcript, c->transcript_len, out);
    return 32;
}

/* nonce = static_iv XOR seq (12-byte BE counter, right-aligned 8 bytes) */
static void net_tls13_nonce(const u8 iv[12], u64 seq, u8 nonce[12]) {
    for (int i = 0; i < 12; i++) nonce[i] = iv[i];
    for (int i = 0; i < 8; i++)
        nonce[4 + i] ^= (u8)(seq >> (56 - 8 * i));
}

/* ---------------- TLS 1.3 record layer ---------------- */

static int net_tls13_recv_record(net_tls_ctx_t *c, int *ctype, u8 *buf, int buf_cap) {
    u8 hdr[5];
    if (net_tcp_read_exact(c->net_tcp_sock, hdr, 5) < 0) return -1;
    int type = hdr[0];
    int len = ((int)hdr[3] << 8) | hdr[4];
    /* P0fix2 BUG-0022 (A14-16): the old check allowed buf_cap+256, reading
     * up to 256 bytes past the caller's buffer (g_hsrec is a 16640-byte
     * heap block).  RFC 8446 max record = 2^14+256 = 16640 = buf_cap. */
    if (len < 0 || len > buf_cap) return -1;
    if (net_tcp_read_exact(c->net_tcp_sock, buf, len) < 0) return -1;

    if ((!c->hs_keys_active && !c->app_keys_active) || type != CT_APPDATA) {
        *ctype = type;
        return len;
    }
    if (len < 17) return -1;
    u8 nonce[12];
    net_tls13_nonce(c->riv, c->rseq, nonce);
    u8 aad[5] = { hdr[0], hdr[1], hdr[2], hdr[3], hdr[4] };
    static u8 inner[16640];
    int dlen = len - 16;
    int rc;
    if (c->cipher == CS_TLS13_AES256GCM_SHA384)
        rc = crypto_aes256_gcm_open(c->rkey, nonce, aad, 5, buf, dlen, inner, buf + dlen);
    else if (c->cipher == CS_TLS13_CHACHA20POLY1305_SHA256)
        rc = chacha20poly1305_open(c->rkey, nonce, aad, 5, buf, dlen, inner, buf + dlen);
    else
        rc = crypto_aes128_gcm_open(c->rkey, nonce, aad, 5, buf, dlen, inner, buf + dlen);
    if (rc != 0) {
        TLS_DBG_P("record decrypt failed (AEAD open)");
        char nb3[16];
        screen_console_puts("[tls-dbg] rseq=");
        u64_to_str((u64)c->rseq, nb3);
        screen_console_puts(nb3);
        screen_console_puts(" reclen=");
        u64_to_str((u64)len, nb3);
        screen_console_puts(nb3);
        screen_console_puts("\n");
        return -2;
    }
    c->rseq++;
    int end = dlen;
    while (end > 0 && inner[end - 1] == 0) end--;
    if (end == 0) return -1;
    *ctype = inner[end - 1];
    int payload = end - 1;
    if (payload > buf_cap) return -1;
    memcpy(buf, inner, payload);
    return payload;
}

static int net_tls13_send_record(net_tls_ctx_t *c, int ctype, const u8 *payload, int len) {
    static u8 rec[16640];
    if (len + 1 + 16 + 5 > (int)sizeof(rec)) return -1;
    if (c->hs_keys_active || c->app_keys_active) {
        memcpy(rec + 5, payload, len);
        rec[5 + len] = (u8)ctype;
        int plain = len + 1;
        u8 nonce[12];
        net_tls13_nonce(c->wiv, c->wseq, nonce);
        u8 hdr[5] = { CT_APPDATA, 3, 3, (u8)((plain + 16) >> 8), (u8)(plain + 16) };
        if (c->cipher == CS_TLS13_AES256GCM_SHA384)
            crypto_aes256_gcm_seal(c->wkey, nonce, hdr, 5, rec + 5, plain, rec + 5, rec + 5 + plain);
        else if (c->cipher == CS_TLS13_CHACHA20POLY1305_SHA256)
            chacha20poly1305_seal(c->wkey, nonce, hdr, 5, rec + 5, plain, rec + 5, rec + 5 + plain);
        else
            crypto_aes128_gcm_seal(c->wkey, nonce, hdr, 5, rec + 5, plain, rec + 5, rec + 5 + plain);
        c->wseq++;
        rec[0] = CT_APPDATA;
        rec[1] = 3; rec[2] = 3;
        rec[3] = (u8)((plain + 16) >> 8);
        rec[4] = (u8)(plain + 16);
        return net_send(c->net_tcp_sock, rec, 5 + plain + 16);
    }
    rec[0] = (u8)ctype;
    rec[1] = 3; rec[2] = 3;
    rec[3] = (u8)(len >> 8);
    rec[4] = (u8)len;
    memcpy(rec + 5, payload, len);
    return net_send(c->net_tcp_sock, rec, 5 + len);
}

static int net_tls13_send_hs(net_tls_ctx_t *c, int mtype, const u8 *body, int len) {
    static u8 msg[17000];
    if (len + 4 > (int)sizeof(msg)) return -1;
    msg[0] = (u8)mtype;
    msg[1] = 0;
    msg[2] = (u8)(len >> 8);
    msg[3] = (u8)len;
    memcpy(msg + 4, body, len);
    transcript_push(c, msg, len + 4);
    return net_tls13_send_record(c, CT_HANDSHAKE, msg, len + 4);
}

/* ---------------- ClientHello ---------------- */

static int build_client_hello(net_tls_ctx_t *c, u8 *ch, int cap) {
    int n = 0;
    crypto_random(c->client_random, 32);
    crypto_random(c->session_id, 32);
    crypto_random(g_x25519_priv, 32);
    crypto_x25519_public(g_x25519_priv, g_x25519_pub);
    crypto_random(g_ec_priv, 32);
    g_ec_priv[0] &= 0x7f;
    if (crypto_ec_pub_from_priv(EC_P256, g_ec_priv, 32, g_ec_pub, 65) != 0) return -1;

    ch[n++] = 3; ch[n++] = 3;
    memcpy(ch + n, c->client_random, 32); n += 32;
    ch[n++] = 32;
    memcpy(ch + n, c->session_id, 32); n += 32;
    u16 suites[] = {
        CS_TLS13_AES128GCM_SHA256, CS_TLS13_AES256GCM_SHA384,
        CS_TLS13_CHACHA20POLY1305_SHA256,
        CS_TLS12_ECDHE_RSA_AES128GCM, CS_TLS12_ECDHE_RSA_AES256GCM,
        CS_TLS12_ECDHE_RSA_CHACHA20, CS_TLS12_DHE_RSA_AES128_CBC_SHA256
    };
    int nsuites = (int)(sizeof(suites) / sizeof(suites[0]));
    ch[n++] = 0; ch[n++] = (u8)(nsuites * 2);
    for (int i = 0; i < nsuites; i++) {
        ch[n++] = (u8)(suites[i] >> 8);
        ch[n++] = (u8)suites[i];
    }
    ch[n++] = 1; ch[n++] = 0;

    int ext_len_pos = n;
    n += 2;
    int ext_len = 0;

    /* server_name (RFC 6066): list_len(2) || type(1)=0 || host_len(2) || host */
    {
        int hl = (int)strlen(c->hostname);
        int body_len = 2 + 1 + 2 + hl;
        ch[n++] = 0; ch[n++] = 0;
        ch[n++] = (u8)(body_len >> 8); ch[n++] = (u8)body_len;
        ch[n++] = (u8)((hl + 3) >> 8); ch[n++] = (u8)(hl + 3);
        ch[n++] = 0;   /* name_type: host_name */
        ch[n++] = (u8)(hl >> 8); ch[n++] = (u8)hl;
        memcpy(ch + n, c->hostname, hl); n += hl;
        ext_len += 4 + body_len;
    }
    /* supported_groups: ext_data = list_len(2) || group ids */
    {
        ch[n++] = 0; ch[n++] = 10;
        ch[n++] = 0; ch[n++] = 8;      /* ext data length: 2 + 3*2 */
        ch[n++] = 0; ch[n++] = 6;      /* named_group_list length */
        ch[n++] = 0; ch[n++] = 0x1d;
        ch[n++] = 0; ch[n++] = 0x17;
        ch[n++] = 0; ch[n++] = 0x18;
        ext_len += 12;
    }
    /* key_share: x25519 + secp256r1 */
    {
        int body_len = 2 + (2 + 2 + 32) + (2 + 2 + 65);
        ch[n++] = 0; ch[n++] = 51;
        ch[n++] = (u8)(body_len >> 8); ch[n++] = (u8)body_len;
        ch[n++] = (u8)((body_len - 2) >> 8); ch[n++] = (u8)(body_len - 2);
        ch[n++] = 0; ch[n++] = 0x1d;
        ch[n++] = 0; ch[n++] = 32;
        memcpy(ch + n, g_x25519_pub, 32); n += 32;
        ch[n++] = 0; ch[n++] = 0x17;
        ch[n++] = 0; ch[n++] = 65;
        memcpy(ch + n, g_ec_pub, 65); n += 65;
        ext_len += 4 + body_len;
    }
    /* signature_algorithms */
    {
        u16 algs[] = { 0x0804, 0x0401, 0x0805, 0x0806, 0x0403, 0x0501 };
        int na = (int)(sizeof(algs) / sizeof(algs[0]));
        ch[n++] = 0; ch[n++] = 13;
        ch[n++] = 0; ch[n++] = (u8)(na * 2 + 2);
        ch[n++] = 0; ch[n++] = (u8)(na * 2);
        for (int i = 0; i < na; i++) {
            ch[n++] = (u8)(algs[i] >> 8);
            ch[n++] = (u8)algs[i];
        }
        ext_len += 4 + na * 2 + 2;
    }
    /* supported_versions */
    {
        ch[n++] = 0; ch[n++] = 43;
        ch[n++] = 0; ch[n++] = 5;
        ch[n++] = 4;                    /* list length: 2 versions */
        ch[n++] = 3; ch[n++] = 4;
        ch[n++] = 3; ch[n++] = 3;
        ext_len += 9;
    }
    ch[ext_len_pos] = (u8)(ext_len >> 8);
    ch[ext_len_pos + 1] = (u8)ext_len;
    (void)cap;
    return n;
}

/* ---------------- TLS 1.3 handshake ---------------- */

static int net_tls13_recv_hs_p(net_tls_ctx_t *c, int expect, u8 *body, int cap,
                           int *len_out, int *ctype_out, int push);
static int net_tls13_recv_hs(net_tls_ctx_t *c, int expect, u8 *body, int cap,
                         int *len_out, int *ctype_out) {
    return net_tls13_recv_hs_p(c, expect, body, cap, len_out, ctype_out, 1);
}

/* Handshake reassembly: TLS 1.3 allows one handshake message to span
 * several records and several messages to share one record (RFC 8446
 * §5.1).  Feed record payloads into a byte stream and parse complete
 * messages out of it.
 *
 * WP-10-wp08fix1: g_hsbuf (20000 B) + the hs_feed record buffer
 * (16640 B) were 36 KiB of kernel .bss. The kernel image must stay
 * below the 0x400000 identity-window boundary (user address spaces
 * split 0x400000-0x600000 into an empty page table, so kernel .bss
 * above 0x400000 is unreachable from syscall context). Both buffers
 * are allocated once, lazily, from the kernel heap - same lifetime as
 * the old statics (never freed). */
static u8 *g_hsbuf;
static u8 *g_hsrec;
static int g_hslen;

#define TLS_HSBUF_SIZE   20000
#define TLS_HSREC_SIZE   16640
/* P0fix2 BUG-0023 (A14-17): RFC 5246 caps a TLS 1.2 CBC ciphertext record
 * at 2^14 + 2048 = 18432 bytes.  The old receive path allowed cap+2080 into
 * 16640-byte buffers, so both a hostile 18720-byte record and a legitimately
 * full CBC record overflowed.  All TLS 1.2 record buffers are now sized to
 * this maximum and the length check is exactly the buffer capacity. */
#define TLS12_REC_MAX    18432

static int tls_hs_ensure(void) {
    if (!g_hsbuf) {
        g_hsbuf = (u8 *)kmalloc(TLS_HSBUF_SIZE);
        g_hsrec = (u8 *)kmalloc(TLS_HSREC_SIZE);
        if (!g_hsbuf || !g_hsrec) {
            if (g_hsbuf) { kfree(g_hsbuf); g_hsbuf = NULL; }
            if (g_hsrec) { kfree(g_hsrec); g_hsrec = NULL; }
            return -1;
        }
    }
    return 0;
}

static int net_tls13_hs_feed(net_tls_ctx_t *c, int *ctype_out) {
    if (tls_hs_ensure() < 0) return -1;
    int payload = net_tls13_recv_record(c, ctype_out, g_hsrec, TLS_HSREC_SIZE);
    if (payload < 0) return -1;
    if (*ctype_out == CT_CCS) return 0;          /* legacy CCS: skip */
    if (*ctype_out != CT_HANDSHAKE && *ctype_out != CT_APPDATA) return -2;
    if (payload <= 0) return 0;
    if (g_hslen + payload > TLS_HSBUF_SIZE) return -3;
    memcpy(g_hsbuf + g_hslen, g_hsrec, payload);
    g_hslen += payload;
    return payload;
}

static int net_tls13_recv_hs_p(net_tls_ctx_t *c, int expect, u8 *body, int cap,
                           int *len_out, int *ctype_out, int push) {
    int rc;
    while (g_hslen < 4) {
        rc = net_tls13_hs_feed(c, ctype_out);
        if (rc < 0) return -1;
    }
    int mtype = g_hsbuf[0];
    int mlen = ((int)g_hsbuf[1] << 16) | ((int)g_hsbuf[2] << 8) | g_hsbuf[3];
    while (g_hslen < 4 + mlen) {
        rc = net_tls13_hs_feed(c, ctype_out);
        if (rc < 0) return -1;
    }
    if (expect >= 0 && mtype != expect) {
#if TLS_DBG
        {
            screen_console_puts("[tls-dbg] recv_hs mtype mismatch: got ");
            char b2[16]; u64_to_str((u64)mtype, b2); screen_console_puts(b2);
            screen_console_puts(" want ");
            u64_to_str((u64)expect, b2); screen_console_puts(b2);
            screen_console_puts("\n");
        }
#endif
        return -1;
    }
    if (mlen > cap) {
        TLS_DBG_P("recv_hs mlen > cap");
        return -1;
    }
    memcpy(body, g_hsbuf + 4, mlen);
    if (push) transcript_push(c, g_hsbuf, mlen + 4);
    /* consume the message from the reassembly stream */
    int rest = g_hslen - (4 + mlen);
    if (rest > 0) memcpy(g_hsbuf, g_hsbuf + 4 + mlen, rest);
    g_hslen = rest;
    *len_out = mlen;
    return mtype;
}

static int net_tls13_do_handshake(net_tls_ctx_t *c) {
    g_hslen = 0;                 /* reset handshake reassembly stream */
    static u8 ch[2048];          /* static: kernel stack is small */
    int ch_len = build_client_hello(c, ch, (int)sizeof(ch));
    if (ch_len < 0) return -1;
    {
        static u8 msg[2048];
        msg[0] = HT_CLIENT_HELLO;
        msg[1] = 0; msg[2] = (u8)(ch_len >> 8); msg[3] = (u8)ch_len;
        memcpy(msg + 4, ch, ch_len);
        transcript_push(c, msg, ch_len + 4);
        if (net_tls13_send_record(c, CT_HANDSHAKE, msg, ch_len + 4) < 0) return -1;
        /* keep the CH for the same-connection TLS 1.2 fallback */
        if (ch_len + 4 <= (int)sizeof(g_tls12_ch)) {
            memcpy(g_tls12_ch, msg, ch_len + 4);
            g_tls12_ch_len = ch_len + 4;
        }
    }

    /* ServerHello (static: kernel stack is small) */
    static u8 body[8192];
    int blen, ctype;
    if (net_tls13_recv_hs(c, HT_SERVER_HELLO, body, (int)sizeof(body), &blen, &ctype) < 0) {
        TLS_DBG_P("ServerHello recv/parse failed");
        return -1;
    }
    TLS_DBG_P("ServerHello received");
    net_tls_dbg_hex("SH first bytes: ", body, 16);
    int net_tls13_selected = 0;
    u8 share[32];
    int have_share = 0;
    u8 crypto_ec_share[65];
    int have_ec = 0;
    {
        int p = 0;
        if (body[0] != 3 || body[1] != 3) {
            TLS_DBG_P("ServerHello legacy version mismatch");
            return -1;
        }
        net_tls_dbg_hex("SH sid_len byte: ", body + 34, 1);
        net_tls_dbg_hex("SH cipher: ", body + 67, 2);
        net_tls_dbg_hex("SH ext_total: ", body + 70, 2);
        p += 2;
        memcpy(c->server_random, body + p, 32); p += 32;
        int sid_len = body[p++];
        p += sid_len;
        c->cipher = ((int)body[p] << 8) | body[p + 1]; p += 2;
        p += 1;
        int ext_total = ((int)body[p] << 8) | body[p + 1]; p += 2;
        int end = p + ext_total;
        while (p + 4 <= end) {
            int etype = ((int)body[p] << 8) | body[p + 1];
            int elen = ((int)body[p + 2] << 8) | body[p + 3];
            p += 4;
            if (etype == 43 && elen == 2) {
                /* ServerHello supported_versions: server_version directly
                 * (2 bytes, no list-length prefix) */
                int v = ((int)body[p] << 8) | body[p + 1];
                if (v == TLS13) net_tls13_selected = 1;
            } else if (etype == 51 && elen >= 4) {
                net_tls_dbg_hex("sh ext 51 body: ", body + p, elen > 8 ? 8 : elen);
                int sp = p;
                int group = ((int)body[sp] << 8) | body[sp + 1];
                int klen = ((int)body[sp + 2] << 8) | body[sp + 3];
                if (group == 0x1d && klen == 32) {
                    memcpy(share, body + sp + 4, 32);
                    have_share = 1;
                } else if (group == 0x17 && klen == 65) {
                    memcpy(crypto_ec_share, body + sp + 4, 65);
                    have_ec = 1;
                }
            }
            p += elen;
        }
        if (!net_tls13_selected) {
            TLS_DBG_P("server chose TLS 1.2 (supported_versions not seen)");
            /* Same-connection fallback: keep the ServerHello for the
             * TLS 1.2 path (RFC 8446 4.1.2 — version negotiation happens
             * on this connection; do NOT reconnect). */
            if (blen + 4 <= (int)sizeof(g_tls12_sh)) {
                g_tls12_sh[0] = HT_SERVER_HELLO;
                g_tls12_sh[1] = 0;
                g_tls12_sh[2] = (u8)(blen >> 8);
                g_tls12_sh[3] = (u8)blen;
                memcpy(g_tls12_sh + 4, body, blen);
                g_tls12_sh_len = blen + 4;
            }
            return -3;
        }
        c->version = TLS13;
        if (!have_share && !have_ec) {
            TLS_DBG_P("no key share from server");
            return -1;
        }
        /* key schedule */
        u8 shared[32];
        if (have_share) crypto_x25519_shared(g_x25519_priv, share, shared);
        else {
            /* server picked P-256 */
            u8 s[32];
            if (crypto_ec_ecdh(EC_P256, g_ec_priv, 32, crypto_ec_share, 65, s, 32) != 0)
                return -1;
            memcpy(shared, s, 32);
        }
        u8 zeros[32], empty_hash[32], derived[32];
        memset(zeros, 0, 32);
        sha256(NULL, 0, empty_hash);   /* Hash("") per RFC 8446 */
        crypto_hmac_sha256(zeros, 32, zeros, 32, c->hs_secret);   /* early secret */
        net_tls13_derive_secret(c->hs_secret, "derived", empty_hash, 32, derived);
        crypto_hmac_sha256(derived, 32, shared, 32, c->hs_secret);
        u8 th[32];
        int thl = transcript_hash(c, th);
        net_tls13_derive_secret(c->hs_secret, "c hs traffic", th, thl, c->c_hs);
        net_tls13_derive_secret(c->hs_secret, "s hs traffic", th, thl, c->s_hs);
        net_tls13_traffic_keys(c->c_hs, c->wkey, c->wiv);
        net_tls13_traffic_keys(c->s_hs, c->rkey, c->riv);
        c->wseq = c->rseq = 0;
        c->hs_keys_active = 1;
        TLS_DBG_P("handshake keys derived");
        net_tls_dbg_hex("DBG c_hs: ", c->c_hs, 32);
        net_tls_dbg_hex("DBG wkey: ", c->wkey, 16);
        net_tls_dbg_hex("DBG s_hs: ", c->s_hs, 32);
        net_tls_dbg_hex("DBG s_hs: ", c->s_hs, 32);
    }

    /* EncryptedExtensions (static: kernel stack is small)
     * P0fix2: sized to the max handshake record payload (16636) instead of
     * 12288 so a legal large Certificate is not rejected mid-handshake. */
    static u8 ebody[16640];
    int elen;
    if (net_tls13_recv_hs(c, HT_ENCRYPTED_EXTENSIONS, ebody, (int)sizeof(ebody), &elen, &ctype) < 0) {
        TLS_DBG_P("EncryptedExtensions failed");
        return -1;
    }
    /* Certificate */
    if (net_tls13_recv_hs(c, HT_CERTIFICATE, ebody, (int)sizeof(ebody), &elen, &ctype) < 0) {
        TLS_DBG_P("Certificate failed");
        return -1;
    }
    {
        int p = 0;
        int ctx_len = ebody[p++];
        p += ctx_len;
        p += 3;   /* certificate_request_context length (3 bytes) */
        int used = 0;
        c->ncerts = 0;
        while (p + 3 <= elen && used < (int)sizeof(c->chain_buf) && c->ncerts < 6) {
            int dlen = ((int)ebody[p] << 16) | ((int)ebody[p + 1] << 8) | ebody[p + 2];
            p += 3;
            if (p + dlen > elen) break;
            memcpy(c->chain_buf + used, ebody + p, dlen);
            if (crypto_x509_parse(&c->certs[c->ncerts], c->chain_buf + used, dlen) == X509_OK)
                c->ncerts++;
            used += dlen;
            p += dlen;
            if (p + 2 > elen) break;
            p += 2;   /* per-cert extensions length */
        }
    }
    {
        int vrc = crypto_x509_verify_chain(c->certs, c->ncerts, c->hostname);
        if (vrc != X509_OK) {
            screen_console_puts("[tls] x509 verify failed: ");
            screen_console_puts(crypto_x509_errstr(vrc));
            screen_console_puts("\n");
            return -4;
        }
    }
    c->verified = 1;

    /* CertificateVerify: NOT pushed to transcript before verification —
     * its signature covers the transcript up to Certificate. Pushed after. */
    if (net_tls13_recv_hs_p(c, HT_CERTIFICATE_VERIFY, ebody, (int)sizeof(ebody), &elen, &ctype, 0) < 0) {
        TLS_DBG_P("CertificateVerify recv failed");
        return -1;
    }
    {
        int alg = ((int)ebody[0] << 8) | ebody[1];
        int sig_len = ((int)ebody[2] << 8) | ebody[3];
        /* P0fix2 BUG-0027 (A14-21) part 1: sig_len was never checked against
         * the message bounds, so sig+sig_len could read far past ebody
         * (both the ECDSA and the RSA verify paths). */
        if (elen < 4 || sig_len < 0 || 4 + sig_len > elen) return -1;
        const u8 *sig = ebody + 4;
        /* signed content: 64 x 0x20 || "TLS 1.3, server CertificateVerify"
         * (33 chars) || 0x00 || transcript hash */
        static const char cv_context[] = "TLS 1.3, server CertificateVerify";
        u8 content[64 + 34 + 48];
        memset(content, 0x20, 64);
        memcpy(content + 64, cv_context, sizeof(cv_context) - 1);
        content[64 + 33] = 0;
        u8 *th = content + 64 + 34;
        int hl = transcript_hash(c, th);
        (void)hl;
        /* PSS/PKCS1 signatures cover SHA256(content blob), NOT the raw
         * transcript hash: mHash = Hash(0x20*64 || context || 0x00 || TH) */
        u8 mhash[48];
        sha256(content, 64 + 34 + (u32)hl, mhash);
        if (alg == 0x0804) {   /* rsa_pss_rsae_sha256 */
            const crypto_x509_cert_t *leaf = &c->certs[0];
            if (crypto_rsa_verify_pss(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                               leaf->rsa_e_len, RSA_SHA256, mhash, 32,
                               sig, sig_len) != 1) {
                char bl[48];
                screen_console_puts("[tls-dbg] PSS verify failed: transcript_len=");
                u64_to_str((u64)c->transcript_len, bl);
                screen_console_puts(bl);
                screen_console_puts(" alg=");
                u64_to_str((u64)alg, bl);
                screen_console_puts(bl);
                screen_console_puts(" sig_len=");
                u64_to_str((u64)sig_len, bl);
                screen_console_puts(bl);
                screen_console_puts("\n");
                net_tls_dbg_hex("th: ", th, 32);
                return -5;
            }
        } else if (alg == 0x0805) {   /* rsa_pss_rsae_sha384 */
            const crypto_x509_cert_t *leaf = &c->certs[0];
            sha384(c->transcript, c->transcript_len, th);
            sha384(content, 64 + 34 + 48, mhash);
            if (crypto_rsa_verify_pss(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                               leaf->rsa_e_len, RSA_SHA384, mhash, 48,
                               sig, sig_len) != 1)
                return -5;
        } else if (alg == 0x0403) {   /* ecdsa_secp256r1_sha256 */
            const crypto_x509_cert_t *leaf = &c->certs[0];
            u8 r[32], s[32];
            /* P0fix2 BUG-0027 (A14-21): the DER decode (with all the length
             * bounds checks it was missing) now lives in the extracted
             * tls13_decode_ecdsa_sig() so the host test suite exercises
             * the identical kernel code. */
            if (tls13_decode_ecdsa_sig(sig, sig_len, r, s) != 0)
                return -5;
            if (ecdsa_verify(EC_P256, leaf->ec_pub, leaf->ec_pub_len,
                             mhash, 32, r, 32, s, 32) != 1)
                return -5;
        } else {
            return -2;
        }
    }

    /* push CertificateVerify into the transcript now that its signature
     * has been verified (the signature covered the transcript WITHOUT it) */
    {
        u8 cv_hdr[4] = { HT_CERTIFICATE_VERIFY, 0, 0, 0 };
        cv_hdr[1] = (u8)((elen) >> 16);
        cv_hdr[2] = (u8)((elen) >> 8);
        cv_hdr[3] = (u8)(elen);
        transcript_push(c, cv_hdr, 4);
        transcript_push(c, ebody, elen);
    }

    /* server Finished: received without transcript push; verified over the
     * transcript up to and including CertificateVerify, then pushed. */
    if (net_tls13_recv_hs_p(c, HT_FINISHED, ebody, (int)sizeof(ebody), &elen, &ctype, 0) < 0) {
        TLS_DBG_P("server Finished recv failed");
        return -1;
    }
    {
        u8 fk[32];
        net_tls13_expand_label(c->s_hs, 32, "finished", NULL, 0, fk, 32);
        u8 th[32];
        int thl = transcript_hash(c, th);
        u8 expect[32];
        crypto_hmac_sha256(fk, 32, th, thl, expect);
        if (elen != 32 || memcmp(ebody, expect, 32) != 0) {
            TLS_DBG_P("server Finished verify_data mismatch");
            return -6;
        }
    }
    {
        u8 fin_hdr[4] = { HT_FINISHED, 0, 0, 32 };
        transcript_push(c, fin_hdr, 4);
        transcript_push(c, ebody, 32);
    }

    /* legacy ChangeCipherSpec (middlebox compat, RFC 8446 §D.4):
     * plaintext record sent right before client Finished */
    {
        u8 ccs[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x01 };
        net_send(c->net_tcp_sock, ccs, 6);
    }

    /* client Finished; snapshot the transcript hash AFTER server Finished
     * push and BEFORE client Finished push: per RFC 8446 §7.1 the server
     * application secret binds exactly that transcript. */
    {
        u8 fk[32];
        net_tls13_expand_label(c->c_hs, 32, "finished", NULL, 0, fk, 32);
        int thl = transcript_hash(c, g_th_srv);
        (void)thl;
        u8 vd[32];
        crypto_hmac_sha256(fk, 32, g_th_srv, 32, vd);
        if (net_tls13_send_hs(c, HT_FINISHED, vd, 32) < 0) return -1;
    }

    /* application secrets (RFC 8446 §7.1 key schedule): BOTH ap traffic
     * secrets bind the transcript through the SERVER Finished
     * (ClientHello...server Finished); the server derives them before it
     * sees our client Finished, so the client must match that view.
     * (RFC 8448 trace §3: c/s ap traffic share the same transcript hash.) */
    {
        u8 zeros[32], empty_hash[32], derived[32], master[32];
        memset(zeros, 0, 32);
        sha256(NULL, 0, empty_hash);
        net_tls13_derive_secret(c->hs_secret, "derived", empty_hash, 32, derived);
        crypto_hmac_sha256(derived, 32, zeros, 32, master);
        net_tls13_derive_secret(master, "c ap traffic", g_th_srv, 32, c->c_ap);
        net_tls13_derive_secret(master, "s ap traffic", g_th_srv, 32, c->s_ap);
        net_tls13_traffic_keys(c->c_ap, c->wkey, c->wiv);
        net_tls13_traffic_keys(c->s_ap, c->rkey, c->riv);
        c->wseq = c->rseq = 0;
        c->hs_keys_active = 0;   /* stop sealing with handshake keys */
        c->app_keys_active = 1;
        net_tls_dbg_hex("DBG s_ap key: ", c->rkey, 16);
        net_tls_dbg_hex("DBG s_ap iv : ", c->riv, 12);
    }
    return 0;
}

/* ---------------- TLS 1.2 ECDHE path (GCM / ChaCha20 / CBC) ---------------- */

static void net_tls12_prf(const u8 *secret, int slen, const char *label,
                      const u8 *seed, int seed_len, u8 *out, int out_len) {
    u8 a[32], chunk[32];
    static u8 seedbuf[128];   /* label + seed; callers pass hashes, fits */
    int ll = (int)strlen(label);
    memcpy(seedbuf, label, ll);
    memcpy(seedbuf + ll, seed, seed_len);
    int sl = ll + seed_len;
    /* RFC 5246 P_hash: A(0)=seed, A(i)=HMAC(secret, A(i-1)),
     * T(i)=HMAC(secret, A(i) || seed).  NOT HKDF-Expand. */
    crypto_hmac_sha256(secret, slen, seedbuf, sl, a);
    static u8 abuf[32 + 128];  /* static: kernel stacks are small */
    int done = 0;
    while (done < out_len) {
        memcpy(abuf, a, 32);
        memcpy(abuf + 32, seedbuf, sl);
        crypto_hmac_sha256(secret, slen, abuf, 32 + sl, chunk);
        int take = out_len - done < 32 ? out_len - done : 32;
        memcpy(out + done, chunk, take);
        done += take;
        if (done >= out_len) break;
        crypto_hmac_sha256(secret, slen, a, 32, a);
    }
}

static int net_tls12_recv_record(net_tls_ctx_t *c, int *ctype, u8 *buf, int cap) {
    u8 hdr[5];
    if (net_tcp_read_exact(c->net_tcp_sock, hdr, 5) < 0) return -1;
    int type = hdr[0];
    int len = ((int)hdr[3] << 8) | hdr[4];
    /* P0fix2 BUG-0023 (A14-17): the old check allowed cap+2080 while every
     * buffer on this path (rec/plain/mbuf) was 16640/17000 bytes; a hostile
     * 18720-byte record — and even a legitimately full 18432-byte CBC
     * record — overflowed them.  All callers now pass TLS12_REC_MAX-sized
     * buffers and this check is exactly the buffer capacity. */
    if (len < 0 || len > cap) return -1;
    if (net_tcp_read_exact(c->net_tcp_sock, buf, len) < 0) return -1;
    /* ChangeCipherSpec is ALWAYS a plaintext record, including the
     * server's one that arrives after we already switched to encrypted
     * sending (RFC 5246 6.2.1). */
    if (!c->net_tls12_encrypted || type == CT_CCS) {
        *ctype = type;
        return len;
    }
    if (c->cipher == CS_TLS12_DHE_RSA_AES128_CBC_SHA256) {
        /* CBC: IV(16) || blocks || MAC(32) || pad; keep simple: decrypt then
         * strip by last byte (CBC pad includes MAC inside) */
        static u8 plain[TLS12_REC_MAX];   /* P0fix2 BUG-0023: was 16640 */
        int blocks = len - 16;
        if (blocks < 48 || (blocks & 15)) return -1;
        crypto_aes128_cbc_decrypt(c->enc_key_r, buf, buf + 16, blocks, plain);
        /* verify MAC over seq||type||ver||len||content */
        u8 mac[32];
        static u8 mbuf[TLS12_REC_MAX + 64];   /* P0fix2 BUG-0023: was 17000 */
        u64 seq = c->seq12_r;
        for (int i = 0; i < 8; i++) mbuf[i] = (u8)(seq >> (56 - 8 * i));
        mbuf[8] = (u8)type; mbuf[9] = 3; mbuf[10] = 3;
        /* subtract padding: last byte of plaintext = pad length (excl. itself) */
        int pad = plain[blocks - 1];
        int real_content = blocks - 1 - pad - 32;
        if (real_content < 0) return -1;
        mbuf[11] = (u8)(real_content >> 8); mbuf[12] = (u8)real_content;
        memcpy(mbuf + 13, plain, real_content);
        crypto_hmac_sha256(c->mac_key_r, 32, mbuf, 13 + real_content, mac);
        u8 *rx_mac = plain + real_content;
        if (memcmp(mac, rx_mac, 32) != 0) return -2;
        c->seq12_r++;
        if (real_content > cap) return -1;
        memcpy(buf, plain, real_content);
        *ctype = type;
        return real_content;
    }
    /* AEAD: explicit nonce(8) || ct || tag(16) */
    if (len < 8 + 16) return -1;
    int dlen = len - 8 - 16;
    u8 nonce[12];
    memcpy(nonce, c->iv_fixed_r, 4);
    memcpy(nonce + 4, buf, 8);
    u8 aad[13];
    u64 seq = c->seq12_r;
    for (int i = 0; i < 8; i++) aad[i] = (u8)(seq >> (56 - 8 * i));
    aad[8] = hdr[0]; aad[9] = hdr[1]; aad[10] = hdr[2];
    /* AAD length = plaintext fragment length (excl. explicit nonce+tag) */
    aad[11] = (u8)(dlen >> 8); aad[12] = (u8)dlen;
    static u8 plain[TLS12_REC_MAX];   /* P0fix2 BUG-0023: was 16640 */
    int rc;
    if (c->cipher == CS_TLS12_ECDHE_RSA_CHACHA20)
        rc = chacha20poly1305_open(c->enc_key_r, nonce, aad, 13, buf + 8,
                                   dlen, plain, buf + 8 + dlen);
    else if (c->cipher == CS_TLS12_ECDHE_RSA_AES256GCM)
        rc = crypto_aes256_gcm_open(c->enc_key_r, nonce, aad, 13, buf + 8, dlen,
                             plain, buf + 8 + dlen);
    else
        rc = crypto_aes128_gcm_open(c->enc_key_r, nonce, aad, 13, buf + 8, dlen,
                             plain, buf + 8 + dlen);
    if (rc != 0) return -2;
    c->seq12_r++;
    if (dlen > cap) return -1;
    memcpy(buf, plain, dlen);
    *ctype = type;
    return dlen;
}

static int net_tls12_send_record(net_tls_ctx_t *c, int ctype, const u8 *payload, int len) {
    static u8 rec[16640];
    if (!c->net_tls12_encrypted) {
        rec[0] = (u8)ctype; rec[1] = 3; rec[2] = 3;
        rec[3] = (u8)(len >> 8); rec[4] = (u8)len;
        memcpy(rec + 5, payload, len);
        /* RFC 5246: only handshake messages go into verify_data;
         * ChangeCipherSpec / Alert records must NOT be logged */
        if (ctype == CT_HANDSHAKE) hs_log_push(c, payload, len);
        return net_send(c->net_tcp_sock, rec, 5 + len);
    }
    if (c->cipher == CS_TLS12_DHE_RSA_AES128_CBC_SHA256) {
        /* MAC || pad || data then encrypt: 16-byte random IV */
        int mac_len = 32;
        int total = len + mac_len;
        int pad = 16 - ((total + 1) % 16);
        if (pad < 0) pad += 16;
        int blocks = total + pad + 1;
        u8 iv[16];
        crypto_random(iv, 16);
        rec[0] = (u8)ctype; rec[1] = 3; rec[2] = 3;
        rec[3] = (u8)((16 + blocks) >> 8); rec[4] = (u8)(16 + blocks);
        memcpy(rec + 5, iv, 16);
        u8 *pt = rec + 21;
        memcpy(pt, payload, len);
        u64 seq = c->seq12_w;
        static u8 mbuf[17000];   /* static: kernel stacks are small */
        for (int i = 0; i < 8; i++) mbuf[i] = (u8)(seq >> (56 - 8 * i));
        mbuf[8] = (u8)ctype; mbuf[9] = 3; mbuf[10] = 3;
        mbuf[11] = (u8)(len >> 8); mbuf[12] = (u8)len;
        memcpy(mbuf + 13, payload, len);
        crypto_hmac_sha256(c->mac_key_w, 32, mbuf, 13 + len, pt + len);
        memset(pt + len + mac_len, (u8)pad, pad + 1);
        crypto_aes128_cbc_encrypt(c->enc_key_w, iv, pt, blocks, pt);
        c->seq12_w++;
        if (ctype == CT_HANDSHAKE) hs_log_push(c, payload, len);
        return net_send(c->net_tcp_sock, rec, 5 + 16 + blocks);
    }
    /* AEAD */
    u8 explicit_nonce[8];
    crypto_random(explicit_nonce, 8);
    u8 nonce[12];
    memcpy(nonce, c->iv_fixed_w, 4);
    memcpy(nonce + 4, explicit_nonce, 8);
    u64 seq = c->seq12_w;
    u8 aad[13];
    for (int i = 0; i < 8; i++) aad[i] = (u8)(seq >> (56 - 8 * i));
    aad[8] = (u8)ctype; aad[9] = 3; aad[10] = 3;
    aad[11] = (u8)(len >> 8); aad[12] = (u8)len;
    u8 *ct = rec + 13;
    int rc;
    if (c->cipher == CS_TLS12_ECDHE_RSA_CHACHA20)
        rc = chacha20poly1305_seal(c->enc_key_w, nonce, aad, 13, payload,
                                   len, ct, ct + len);
    else if (c->cipher == CS_TLS12_ECDHE_RSA_AES256GCM)
        rc = crypto_aes256_gcm_seal(c->enc_key_w, nonce, aad, 13, payload, len,
                             ct, ct + len);
    else
        rc = crypto_aes128_gcm_seal(c->enc_key_w, nonce, aad, 13, payload, len,
                             ct, ct + len);
    if (rc != 0) return -1;
    c->seq12_w++;
    /* handshake messages still go into the verify_data transcript even
     * when encrypted (RFC 5246 7.4.9) */
    if (ctype == CT_HANDSHAKE) hs_log_push(c, payload, len);
    int total = 8 + len + 16;
    rec[0] = (u8)ctype; rec[1] = 3; rec[2] = 3;
    rec[3] = (u8)(total >> 8); rec[4] = (u8)total;
    memcpy(rec + 5, explicit_nonce, 8);
    return net_send(c->net_tcp_sock, rec, 5 + total);
}

/* TLS 1.2 continuation: ServerHello already parsed by the caller. */
static int net_tls12_finish_after_hello(net_tls_ctx_t *c) {
    /* P0fix2 BUG-0024/0025 (A14-18/19): sized to TLS12_REC_MAX so any
     * in-cap handshake message fits; the old 12288-byte buffer was both
     * the target of the mlen overflow and, when intact, a needless
     * rejection of big-but-legal certificate messages. */
    static u8 body[TLS12_REC_MAX];   /* static: kernel stack is small */
    /* One shared record buffer for the four handshake record reads below
     * (P0fix2 BUG-0023: each was 16640; merging four into one TLS12_REC_MAX
     * buffer also keeps the size-asserted kernel image small). */
    static u8 rec[TLS12_REC_MAX];
    int blen, ctype;

    /* Certificate (plaintext) */
    {
        int payload = net_tls12_recv_record(c, &ctype, rec, (int)sizeof(rec));
        if (payload < 4 || ctype != CT_HANDSHAKE || rec[0] != HT_CERTIFICATE)
            return -1;
        int mlen = ((int)rec[1] << 16) | ((int)rec[2] << 8) | rec[3];
        /* P0fix2 BUG-0024 (A14-18): mlen was only checked against the
         * record payload (≤16636) while body held 12288 bytes — a hostile
         * Certificate message overflowed body by ~4.3 KB BEFORE any chain
         * verification ran.  mlen is now checked against both. */
        if (mlen < 0 || mlen + 4 > payload || mlen > (int)sizeof(body)) return -1;
        memcpy(body, rec + 4, mlen);
        blen = mlen;
        hs_log_push(c, rec, mlen + 4);
    }
    {
        int p = 0;
        int total = ((int)body[p] << 16) | ((int)body[p + 1] << 8) | body[p + 2];
        p += 3;
        (void)total;
        int used = 0;
        c->ncerts = 0;
        while (p + 3 <= blen && used < (int)sizeof(c->chain_buf) && c->ncerts < 6) {
            int dlen = ((int)body[p] << 16) | ((int)body[p + 1] << 8) | body[p + 2];
            p += 3;
            if (p + dlen > blen) break;
            memcpy(c->chain_buf + used, body + p, dlen);
            if (crypto_x509_parse(&c->certs[c->ncerts], c->chain_buf + used, dlen) == X509_OK)
                c->ncerts++;
            used += dlen;
            p += dlen;
        }
    }
    if (crypto_x509_verify_chain(c->certs, c->ncerts, c->hostname) != X509_OK)
        return -4;
    c->verified = 1;

    /* ServerKeyExchange (may be absent for fixed-DH; we require ECDHE) */
    {
        int payload = net_tls12_recv_record(c, &ctype, rec, (int)sizeof(rec));
        if (payload < 4 || ctype != CT_HANDSHAKE) return -1;
        int mlen = ((int)rec[1] << 16) | ((int)rec[2] << 8) | rec[3];
        /* P0fix2 BUG-0025 (A14-19): the ServerKeyExchange message length
         * was NEVER checked against either the record payload or body —
         * mlen up to 16 MB meant a massive OOB read from rec and an OOB
         * write into body. */
        if (mlen < 0 || mlen + 4 > payload || mlen > (int)sizeof(body)) return -1;
        memcpy(body, rec + 4, mlen);
        blen = mlen;
        hs_log_push(c, rec, mlen + 4);
        if (rec[0] != HT_SERVER_KEY_EXCHANGE) {
            if (rec[0] == HT_CERTIFICATE_REQUEST) return -2;  /* no client certs */
            return -1;
        }
        int p = 0;
        /* P0fix2 BUG-0026 (A14-20): ServerDHParams carries three 2-byte-
         * length integers, each up to 256 bytes on the wire = 774 bytes
         * total; the old 320-byte tail overflowed by 454 bytes.  776
         * covers the legal maximum and the copy below is bounds-checked. */
        static u8 sbuf[64 + 776];   /* static: kernel stacks are small */
        memcpy(sbuf, c->client_random, 32);
        memcpy(sbuf + 32, c->server_random, 32);
        if (c->cipher == CS_TLS12_DHE_RSA_AES128_CBC_SHA256) {
            /* RFC 5246 7.4.3 ServerDHParams:
             *   opaque p<1..2^16-2>, g<1..2^16-2>, Ys<1..2^16-2>      */
            int plen = ((int)body[p] << 8) | body[p + 1]; p += 2;
            if (plen < 1 || plen > 256 || p + plen > blen) return -2;
            u8 *crypto_dh_p = body + p; p += plen;
            int glen = ((int)body[p] << 8) | body[p + 1]; p += 2;
            if (glen < 1 || glen > 256 || p + glen > blen) return -2;
            u8 *crypto_dh_g = body + p; p += glen;
            int ylen = ((int)body[p] << 8) | body[p + 1]; p += 2;
            if (ylen < 1 || ylen > 256 || p + ylen > blen) return -2;
            u8 *crypto_dh_ys = body + p; p += ylen;
            int params_len = p;             /* params start right after sig len? no:
                                               signed content = CR || SR || ServerDHParams,
                                               which is body[0..p) here */
            (void)params_len;
            int algpair = ((int)body[p] << 8) | body[p + 1];
            p += 2;   /* SignatureAndHashAlgorithm */
            int sig_len = ((int)body[p] << 8) | body[p + 1]; p += 2;
            if (sig_len < 0 || p + sig_len > blen) return -2;
            /* signed content: client_random || server_random || ServerDHParams
             * (p now sits past the alg-pair and sig_len: back off 4) */
            if (p - 4 > (int)sizeof(sbuf) - 64) return -2;   /* P0fix2 BUG-0026 */
            memcpy(sbuf + 64, body, p - 4);
            u8 h[32];
            sha256(sbuf, 64 + (p - 4), h);
            const crypto_x509_cert_t *leaf = &c->certs[0];
            int vrc12;
            if (algpair == 0x0804 || algpair == 0x0805 || algpair == 0x0806)
                vrc12 = crypto_rsa_verify_pss(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                                       leaf->rsa_e_len, RSA_SHA256, h, 32,
                                       body + p, sig_len);
            else
                vrc12 = crypto_rsa_verify_pkcs1(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                                         leaf->rsa_e_len, RSA_SHA256, h, 32,
                                         body + p, sig_len);
            if (vrc12 != 1) {
                return -5;
            }
            /* ephemeral DH: x random, Yc = g^x mod p, Z = Ys^x mod p.
             * g and Ys arrive as variable-length big-endian integers;
             * zero-pad them to p's width for the bn modexp. */
            static u8 crypto_dhe_x[256], crypto_dhe_yc[256], crypto_dhe_z[256], crypto_dhe_g[256], crypto_dhe_ys[256];
            crypto_random(crypto_dhe_x, 256);
            crypto_dhe_x[255] |= 1;            /* non-zero-ish is fine for modexp */
            int klen = plen;            /* work in p's byte width (<=256) */
            memset(crypto_dhe_g, 0, klen);
            memcpy(crypto_dhe_g + (klen - glen), crypto_dh_g, glen);
            memset(crypto_dhe_ys, 0, klen);
            memcpy(crypto_dhe_ys + (klen - ylen), crypto_dh_ys, ylen);
            crypto_dh_modexp_n(crypto_dhe_g, crypto_dhe_x, crypto_dh_p, crypto_dhe_yc, klen);
            crypto_dh_modexp_n(crypto_dhe_ys, crypto_dhe_x, crypto_dh_p, crypto_dhe_z, klen);
            g_dhe_yc_len = klen;
            memcpy(g_dhe_yc, crypto_dhe_yc, klen);
            u8 seed[64];
            memcpy(seed, c->client_random, 32);
            memcpy(seed + 32, c->server_random, 32);
            net_tls12_prf(crypto_dhe_z, klen, "master secret", seed, 64,
                      c->master_secret, 48);
            u8 kseed[64];
            memcpy(kseed, c->server_random, 32);
            memcpy(kseed + 32, c->client_random, 32);
            u8 keyblock[104];
            net_tls12_prf(c->master_secret, 48, "key expansion", kseed, 64,
                      keyblock, sizeof(keyblock));
            /* CBC suite: MAC(32,32) + key(16,16) + fixed IV(4,4) */
            int ki = 0;
            memcpy(c->mac_key_w, keyblock + ki, 32); ki += 32;
            memcpy(c->mac_key_r, keyblock + ki, 32); ki += 32;
            memcpy(c->enc_key_w, keyblock + ki, 16); ki += 16;
            memcpy(c->enc_key_r, keyblock + ki, 16); ki += 16;
            memcpy(c->iv_fixed_w, keyblock + ki, 4); ki += 4;
            memcpy(c->iv_fixed_r, keyblock + ki, 4); ki += 4;
            c->seq12_w = c->seq12_r = 0;
        } else {
        /* ECParameters: curve_type(1)=3 named_curve(2), point.
         * Supported: secp256r1 (0x17) and x25519 (0x1d) — the same two
         * curves offered in the ClientHello key_share. */
        if (body[p++] != 3) return -2;
        int curve = ((int)body[p] << 8) | body[p + 1]; p += 2;
        int use_x25519 = (curve == 0x1d);
        g_tls12_ecdh_x25519 = use_x25519;
        if (!use_x25519 && curve != 0x17) return -2;
        int plen = body[p++];
        if (use_x25519 && plen != 32) return -2;
        if (!use_x25519 && plen != 65) return -2;
        u8 server_pub[65];
        memcpy(server_pub, body + p, plen); p += plen;
        /* signature over client_random + server_random + params:
         * TLS 1.2 prefixes it with SignatureAndHashAlgorithm (2 bytes) */
        p += 2;
        int sig_len = ((int)body[p] << 8) | body[p + 1]; p += 2;
        /* signed content: client_random || server_random || ServerECDHParams
         * (p now sits past the alg-pair and sig_len: back off 4) */
        int params_len = p - 4;   /* curve_type + curve + plen + point */
        static u8 sbuf2[64 + 80];  /* static: kernel stacks are small */
        memcpy(sbuf2, c->client_random, 32);
        memcpy(sbuf2 + 32, c->server_random, 32);
        memcpy(sbuf2 + 64, body, params_len);
        u8 h[32];
        sha256(sbuf2, 64 + params_len, h);
        const crypto_x509_cert_t *leaf = &c->certs[0];
        {
            /* p currently points at the signature start; the algorithm
             * pair sits 4 bytes back (alg-pair + sig_len). */
            int algpair2 = ((int)body[p - 4] << 8) | body[p - 3];
            int vrc12;
            if (algpair2 == 0x0804 || algpair2 == 0x0805 || algpair2 == 0x0806)
                vrc12 = crypto_rsa_verify_pss(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                                       leaf->rsa_e_len, RSA_SHA256, h, 32,
                                       body + p, sig_len);
            else
                vrc12 = crypto_rsa_verify_pkcs1(leaf->rsa_n, leaf->rsa_n_len, leaf->rsa_e,
                                         leaf->rsa_e_len, RSA_SHA256, h, 32,
                                         body + p, sig_len);
            if (vrc12 != 1)
                return -5;
        }
        /* shared secret */
        if (use_x25519) {
            if (crypto_x25519_shared(g_x25519_priv, server_pub, c->master_secret) != 0)
                return -1;
        } else if (crypto_ec_ecdh(EC_P256, g_ec_priv, 32, server_pub, 65,
                           c->master_secret, 32) != 0)
            return -1;
        /* x-coordinate as premaster (48-byte style: use 32-byte x directly) */
        u8 premaster[32];
        memcpy(premaster, c->master_secret, 32);
        u8 seed[64];
        memcpy(seed, c->client_random, 32);
        memcpy(seed + 32, c->server_random, 32);
        net_tls12_prf(premaster, 32, "master secret", seed, 64,
                  c->master_secret, 48);
        u8 kseed[64];
        memcpy(kseed, c->server_random, 32);
        memcpy(kseed + 32, c->client_random, 32);
        static u8 keyblock[72];   /* static: kernel stacks are small */
        net_tls12_prf(c->master_secret, 48, "key expansion", kseed, 64,
                  keyblock, sizeof(keyblock));
        /* GCM/CCM suite: 16 or 32 key each side + 4-byte fixed IV, no MAC */
        int ki = 0;
        int key_len = (c->cipher == CS_TLS12_ECDHE_RSA_AES256GCM) ? 32 : 16;
        memcpy(c->mac_key_w, keyblock + ki, 0); ki += 0;
        memcpy(c->mac_key_r, keyblock + ki, 0); ki += 0;
        memcpy(c->enc_key_w, keyblock + ki, key_len); ki += key_len;
        memcpy(c->enc_key_r, keyblock + ki, key_len); ki += key_len;
        memcpy(c->iv_fixed_w, keyblock + ki, 4); ki += 4;
        memcpy(c->iv_fixed_r, keyblock + ki, 4); ki += 4;
        c->seq12_w = c->seq12_r = 0;
        }
    }

    /* ServerHelloDone */
    {
        int payload = net_tls12_recv_record(c, &ctype, rec, (int)sizeof(rec));
        if (payload < 4 || ctype != CT_HANDSHAKE || rec[0] != HT_SERVER_HELLO_DONE)
            return -1;
        hs_log_push(c, rec, payload);
    }

    /* ClientKeyExchange — format depends on key exchange */
    {
        if (c->cipher == CS_TLS12_DHE_RSA_AES128_CBC_SHA256) {
            /* RFC 5246: opaque Yc<1..2^16-1> — 2-byte len || Yc */
            static u8 cke[4 + 2 + 256];   /* static: kernel stacks are small */
            int yl = g_dhe_yc_len;
            cke[0] = HT_CLIENT_KEY_EXCHANGE;
            cke[1] = 0;
            cke[2] = (u8)((yl + 2) >> 8);
            cke[3] = (u8)(yl + 2);
            cke[4] = (u8)(yl >> 8);
            cke[5] = (u8)yl;
            memcpy(cke + 6, g_dhe_yc, yl);
            int msglen = 4 + 2 + yl;
            if (net_tls12_send_record(c, CT_HANDSHAKE, cke, msglen) < 0) return -1;
        } else if (g_tls12_ecdh_x25519) {
            /* server chose x25519: CKE carries the 32-byte public key */
            static u8 ckex[4 + 1 + 32];
            ckex[0] = HT_CLIENT_KEY_EXCHANGE;
            ckex[1] = 0; ckex[2] = 0; ckex[3] = 33;
            ckex[4] = 32;
            memcpy(ckex + 5, g_x25519_pub, 32);
            if (net_tls12_send_record(c, CT_HANDSHAKE, ckex, 37) < 0) return -1;
        } else {
        static u8 cke[128];   /* static: kernel stacks are small */
        cke[0] = HT_CLIENT_KEY_EXCHANGE;
        cke[1] = 0; cke[2] = 0; cke[3] = 66;
        cke[4] = 65;
        memcpy(cke + 5, g_ec_pub, 65);
        if (net_tls12_send_record(c, CT_HANDSHAKE, cke, 70) < 0) return -1;
        }
    }
    /* ChangeCipherSpec */
    {
        u8 ccs[1] = { 1 };
        if (net_tls12_send_record(c, CT_CCS, ccs, 1) < 0) return -1;
    }
    c->net_tls12_encrypted = 1;
    /* client Finished (RFC 5246 7.4.9): verify_data = PRF(master,
     * "client finished", Hash(handshake_messages))[0..11] — the seed is
     * the 32-byte HASH of the handshake log, never the raw log. */
    {
        u8 hlog[32];
        sha256(c->hs_log, c->hs_log_len, hlog);
        u8 vd[12];
        net_tls12_prf(c->master_secret, 48, "client finished",
                  hlog, 32, vd, 12);
        u8 fin[4 + 12];
        fin[0] = HT_FINISHED; fin[1] = 0; fin[2] = 0; fin[3] = 12;
        memcpy(fin + 4, vd, 12);
        if (net_tls12_send_record(c, CT_HANDSHAKE, fin, 16) < 0) return -1;
    }
    /* server CCS + Finished */
    {
        int payload = net_tls12_recv_record(c, &ctype, rec, (int)sizeof(rec));
        if (payload < 1 || ctype != CT_CCS) return -1;
        payload = net_tls12_recv_record(c, &ctype, rec, (int)sizeof(rec));
        if (payload < 4 || ctype != CT_HANDSHAKE || rec[0] != HT_FINISHED)
            return -1;
        u8 hlog2[32];
        sha256(c->hs_log, c->hs_log_len, hlog2);
        u8 vd[12];
        net_tls12_prf(c->master_secret, 48, "server finished",
                  hlog2, 32, vd, 12);
        if (payload != 16 || memcmp(rec + 4, vd, 12) != 0) return -6;
    }
    return 0;
}

/* ---------------- public API ---------------- */

/* TLS 1.2 continuation on the SAME connection: the server negotiated
 * TLS 1.2 in response to our dual-version ClientHello.  The ClientHello
 * and ServerHello have already been sent/received by the TLS 1.3 probe.
 * NOTE: our ClientHello carries the same secp256r1 key_share that the
 * TLS 1.2 ECDHE path uses, and g_ec_priv/g_ec_pub still hold that pair,
 * so the key exchange continues without re-handshake. */
static int net_tls12_finish_after_hello(net_tls_ctx_t *c);
static int net_tls12_resume_from_sh(net_tls_ctx_t *c) {
    c->version = TLS12;
    c->hs_keys_active = 0;
    c->app_keys_active = 0;
    if (g_tls12_ch_len < 5 || g_tls12_sh_len < 5) return -1;
    hs_log_push(c, g_tls12_ch, g_tls12_ch_len);
    hs_log_push(c, g_tls12_sh, g_tls12_sh_len);
    u8 *b = g_tls12_sh + 4;
    int p = 2;
    memcpy(c->server_random, b + p, 32); p += 32;
    p += 1 + b[p];          /* session id */
    c->cipher = ((int)b[p] << 8) | b[p + 1]; p += 2;
    p += 1;                 /* compression */
    return net_tls12_finish_after_hello(c);
}

int net_tls_connect(u32 ip, u16 port, const char *hostname) {
    net_tls_recv_reset();   /* drop leftovers from any previous session */
    net_tls_ctx_t *c = &g_tls;
    memset(c, 0, sizeof(*c));
    c->hostname[0] = 0;
    if (hostname) {
        int hl = (int)strlen(hostname);
        if (hl >= (int)sizeof(c->hostname)) return -1;
        memcpy(c->hostname, hostname, hl + 1);
    }
    c->net_tcp_sock = net_socket(SOCK_TCP);
    if (c->net_tcp_sock < 0) return -1;
    if (net_connect(c->net_tcp_sock, ip, port) < 0) {
        net_close(c->net_tcp_sock);
        return -1;
    }
    int rc = net_tls13_do_handshake(c);
    if (rc == -3) {
        /* server negotiated TLS 1.2 on this same connection
         * (RFC 8446 4.1.2) — continue with the TLS 1.2 handshake */
        rc = net_tls12_resume_from_sh(c);
    }
    if (rc != 0) {
        net_close(c->net_tcp_sock);
        return rc;
    }
    return 0;
}

int net_tls_send(net_tls_ctx_t *c, const void *data, int len) {
    if (c->version == TLS13 || c->hs_keys_active)
        return net_tls13_send_record(c, CT_APPDATA, (const u8 *)data, len);
    return net_tls12_send_record(c, CT_APPDATA, (const u8 *)data, len);
}

/* Leftover storage for records larger than the caller's buffer: a TLS
 * record is an atomic unit, so bytes beyond len must be kept for the
 * next net_tls_recv call instead of being dropped (a 16 KiB record read
 * through a 4 KiB download chunk used to lose three quarters of every
 * record, corrupting every large transfer). */
static u8 g_tls_leftover[TLS12_REC_MAX];   /* P0fix2 BUG-0023: was 16640 */
static int g_tls_leftover_have;
static int g_tls_leftover_off;

void net_tls_recv_reset(void) {
    g_tls_leftover_have = 0;
    g_tls_leftover_off = 0;
}

int net_tls_recv(net_tls_ctx_t *c, void *buf, int len) {
    int ctype;
    static u8 tmp[TLS12_REC_MAX];   /* P0fix2 BUG-0023: was 16640 */

    if (len <= 0) return 0;

    /* 1. serve bytes left over from a previous oversized record */
    if (g_tls_leftover_have > 0) {
        int give = (g_tls_leftover_have > len) ? len : g_tls_leftover_have;
        memcpy(buf, g_tls_leftover + g_tls_leftover_off, give);
        g_tls_leftover_off += give;
        g_tls_leftover_have -= give;
        if (g_tls_leftover_have == 0) g_tls_leftover_off = 0;
        return give;
    }

    /* 2. read the next complete record */
    int n = (c->version == TLS13 || c->hs_keys_active)
                ? net_tls13_recv_record(c, &ctype, tmp, (int)sizeof(tmp))
                : net_tls12_recv_record(c, &ctype, tmp, (int)sizeof(tmp));
    if (n < 0) return n;
    if (ctype == CT_ALERT) {
        TLS_DBG_P("[tls-dbg] alert from server");
        net_tls_dbg_hex("[tls-dbg] alert level/desc: ", tmp, n > 2 ? 2 : n);
        return 0;
    }
    if (ctype == CT_HANDSHAKE && n >= 4 &&
        (tmp[0] == HT_KEY_UPDATE || tmp[0] == HT_NEW_SESSION_TICKET)) {
        /* post-handshake messages we do not act on; keep reading data */
        return net_tls_recv(c, buf, len);
    }
    /* 3. hand out what fits, keep the rest for the next call */
    if (n > len) {
        memcpy(buf, tmp, len);
        memcpy(g_tls_leftover, tmp + len, n - len);
        g_tls_leftover_have = n - len;
        g_tls_leftover_off = 0;
        return len;
    }
    memcpy(buf, tmp, n);
    return n;
}

void net_tls_close(net_tls_ctx_t *c) {
    net_tls_recv_reset();
    if (!c->net_tcp_sock) return;
    /* Only send the closing alert while the TCP connection is still
     * ESTABLISHED.  Servers that answer with "Connection: close" send
     * their close_notify/FIN right after the response, putting our side
     * into CLOSE_WAIT; writing then is guaranteed to fail and only
     * produced the noisy "[tcp] send fail ... (CLOSE_WAIT)" line.
     * net_close below always completes the local teardown. */
    if (net_tcp_established(c->net_tcp_sock)) {
        u8 alert[2] = { 1, 0 };
        if (c->version == TLS13)
            net_tls13_send_record(c, CT_ALERT, alert, 2);
        else
            net_tls12_send_record(c, CT_ALERT, alert, 2);
    }
    net_close(c->net_tcp_sock);
}

net_tls_ctx_t *net_tls_get_ctx(void) { return &g_tls; }

int net_tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                  void *out_buf, int out_len) {
    if (net_tls_connect(ip, port, hostname) != 0) return -1;
    net_tls_ctx_t *c = net_tls_get_ctx();
    /* P0fix2 BUG-0021 (A14-15): the request was built by unchecked
     * concatenation into req[1024] while the OTA layer legally passes a
     * 1663-byte CDN redirect path — path(1300)+host(100) alone overflowed
     * by ~420 bytes.  Sized for the legal maximum (4+1663+17+127+25+1 =
     * 1837) and the total length is now checked before any write. */
    char req[2048];
    int plen = (int)strlen(path);
    int hlen = (int)strlen(hostname);
    if (4 + plen + 17 + hlen + 25 + 1 > (int)sizeof(req)) {
        net_tls_close(c);
        return -1;
    }
    int rl = 0;
    memcpy(req + rl, "GET ", 4); rl += 4;
    memcpy(req + rl, path, plen); rl += plen;
    memcpy(req + rl, " HTTP/1.1\r\nHost: ", 17); rl += 17;
    memcpy(req + rl, hostname, hlen); rl += hlen;
    memcpy(req + rl, "\r\nConnection: close\r\n\r\n", 25); rl += 25;
    if (net_tls_send(c, req, rl) < 0) {
        net_tls_close(c);
        return -1;
    }
    int total = 0;
    for (;;) {
        int n = net_tls_recv(c, (u8 *)out_buf + total, out_len - total);
        if (n <= 0) break;
        total += n;
        if (total >= out_len) break;
    }
    net_tls_close(c);
    return total;
}
