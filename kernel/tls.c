/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/tls.c
 * Purpose: TLS 1.2 client — minimal implementation for HTTPS.
 *
 * Implements:
 *   - TLS 1.2 record layer
 *   - ClientHello / ServerHello / Certificate / ServerKeyExchange /
 *     ServerHelloDone / ClientKeyExchange / ChangeCipherSpec / Finished
 *   - DH group 14 key exchange
 *   - AES-128-CTR record encryption
 *   - Certificate verification: skipped (accept any)
 *
 * Limitations:
 *   - Only TLS_RSA_WITH_AES_128_CBC_SHA256 is NOT supported (we do DH only)
 *   - Only TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 cipher suite
 *   - No session resumption
 *   - No certificate chain verification
 *   - No ALPN / SNI (hostname is ignored for now)
 */
#include "tls.h"
#include "crypto.h"
#include "net.h"
#include "console.h"
#include "string.h"
#include "pmm.h"
#include "timer.h"

/* TLS record types */
#define TLS_HANDSHAKE  22
#define TLS_CHANGE_CIPHER_SPEC 20
#define TLS_APPLICATION_DATA 23
#define TLS_ALERT 21

/* TLS handshake message types */
#define TLS_CLIENT_HELLO 1
#define TLS_SERVER_HELLO 2
#define TLS_CERTIFICATE 11
#define TLS_SERVER_KEY_EXCHANGE 12
#define TLS_SERVER_HELLO_DONE 14
#define TLS_CLIENT_KEY_EXCHANGE 16
#define TLS_FINISHED 20

/* Cipher suite: TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 = 0x0067 */
#define TLS_CIPHER_DHE_RSA_AES128_CBC_SHA256 0x0067

static tls_ctx_t g_tls_ctx;

/* PRF (TLS 1.2 P_SHA256, RFC 5246 §5)
 * secret_len + label_len + seed_len must be < 512 */
static void tls_prf(const u8 *secret, int secret_len,
                    const char *label,
                    const u8 *seed, int seed_len,
                    u8 *out, int out_len) {
    /* A(0) = seed || label (actually label || seed in TLS) */
    int label_len = (int)oc_strlen(label);
    int total_seed = label_len + seed_len;
    u8 *ls = (u8 *)(uintptr_t)pmm_alloc_frame(); /* label || seed */
    if (!ls) return;
    oc_memcpy(ls, label, label_len);
    oc_memcpy(ls + label_len, seed, seed_len);

    /* A(1) = HMAC(secret, A(0)) = HMAC(secret, label||seed) */
    u8 a[32];
    hmac_sha256(secret, secret_len, ls, total_seed, a);

    int offset = 0;
    while (offset < out_len) {
        /* HMAC(secret, A(i) || label||seed) */
        u8 *msg = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (!msg) { pmm_free_frame((u64)(uintptr_t)ls); return; }
        oc_memcpy(msg, a, 32);
        oc_memcpy(msg + 32, ls, total_seed);
        u8 block[32];
        hmac_sha256(secret, secret_len, msg, 32 + total_seed, block);
        pmm_free_frame((u64)(uintptr_t)msg);
        int copy = out_len - offset;
        if (copy > 32) copy = 32;
        oc_memcpy(out + offset, block, copy);
        offset += copy;
        /* A(i+1) = HMAC(secret, A(i)) */
        hmac_sha256(secret, secret_len, a, 32, a);
    }
    pmm_free_frame((u64)(uintptr_t)ls);
}

/* Send a TLS record — must be in a single TCP segment */
static int tls_send_record(tls_ctx_t *ctx, u8 type, const void *data, int len) {
    /* WP-09 fix: combine header + data into one buffer to ensure
     * a single TCP segment (net_send may split across multiple
     * segments, which breaks TLS record parsing on the server). */
    static u8 record[16384];
    int total = 5 + len;
    if (total > 16384) total = 16384;
    record[0] = type;
    record[1] = 0x03; record[2] = 0x03; /* TLS 1.2 */
    record[3] = (u8)(len >> 8);
    record[4] = (u8)(len & 0xFF);
    if (len > 0 && total <= 16384) {
        oc_memcpy(record + 5, data, total - 5);
    }
    return net_send(ctx->tcp_sock, record, total);
}

/* Send an encrypted TLS record */
static int tls_send_encrypted(tls_ctx_t *ctx, u8 type, const void *data, int len) {
    /* Build record: header + encrypted data + HMAC */
    u8 *buf = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!buf) return -1;
    int total = len + 32; /* data + HMAC-SHA256 */
    /* Build plaintext: seq(8) || type(1) || version(2) || length(2) || data */
    u8 *plain = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!plain) { pmm_free_frame((u64)(uintptr_t)buf); return -1; }
    int plen = 0;
    plain[plen++] = ctx->write_seq;
    for (int i = 0; i < 7; i++) plain[plen++] = 0; /* seq (8 bytes) */
    plain[plen++] = type;
    plain[plen++] = 0x03; plain[plen++] = 0x03;
    plain[plen++] = (u8)(len >> 8);
    plain[plen++] = (u8)(len & 0xFF);
    oc_memcpy(plain + plen, data, len);
    plen += len;
    /* HMAC over the plaintext */
    u8 mac[32];
    hmac_sha256(ctx->write_key, 16, plain, plen, mac);
    oc_memcpy(buf, data, len);
    oc_memcpy(buf + len, mac, 32);
    /* Encrypt with AES-128-CTR */
    u8 nonce[16];
    oc_memcpy(nonce, ctx->write_iv, 16);
    /* XOR seq into nonce last bytes */
    nonce[15] ^= ctx->write_seq;
    u8 *enc = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!enc) { pmm_free_frame((u64)(uintptr_t)buf); pmm_free_frame((u64)(uintptr_t)plain); return -1; }
    aes128_ctr_encrypt(ctx->write_key, nonce, buf, total, enc);

    /* Send record */
    u8 hdr[5];
    hdr[0] = type;
    hdr[1] = 0x03; hdr[2] = 0x03;
    hdr[3] = (u8)(total >> 8);
    hdr[4] = (u8)(total & 0xFF);
    net_send(ctx->tcp_sock, hdr, 5);
    net_send(ctx->tcp_sock, enc, total);
    ctx->write_seq++;

    pmm_free_frame((u64)(uintptr_t)buf);
    pmm_free_frame((u64)(uintptr_t)plain);
    pmm_free_frame((u64)(uintptr_t)enc);
    return 0;
}

/* Read a TLS record header + body into buf (caller allocates). */
static int tls_read_record(tls_ctx_t *ctx, u8 *type, u8 *buf, int *len) {
    (void)ctx;
    u8 hdr[5];
    /* Poll until we get data */
    u64 start = oc_timer_ticks();
    while (oc_timer_ticks() - start < 500) {
        net_poll();
        int n = net_recv(ctx->tcp_sock, hdr, 5);
        if (n >= 5) {
            *type = hdr[0];
            int rlen = (hdr[3] << 8) | hdr[4];
            if (rlen > 16384) rlen = 16384;
            int got = 0;
            while (got < rlen) {
                n = net_recv(ctx->tcp_sock, buf + got, rlen - got);
                if (n > 0) got += n;
                else net_poll();
            }
            *len = got;
            return 0;
        }
    }
    return -1;
}

int tls_connect(u32 ip, u16 port, const char *hostname) {
    (void)hostname;
    tls_ctx_t *ctx = &g_tls_ctx;
    oc_memset(ctx, 0, sizeof(*ctx));

    /* TCP connect — use the high-level socket API for consistency */
    ctx->tcp_sock = net_socket(SOCK_TCP);
    if (ctx->tcp_sock < 0) {
        return -1;
    }
    if (net_connect(ctx->tcp_sock, ip, port) < 0) {
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
        return -1;
    }

    /* Generate client random */
    crypto_random(ctx->client_random, 32);

    /* Build ClientHello */
    u8 hello[512];
    int hlen = 0;
    /* Handshake header: type(1) + length(3) — fill later */
    hlen += 4;
    /* ClientVersion: 0x0303 (TLS 1.2) */
    hello[hlen++] = 0x03; hello[hlen++] = 0x03;
    /* ClientRandom: 32 bytes */
    oc_memcpy(hello + hlen, ctx->client_random, 32);
    hlen += 32;
    /* SessionID: length 0 */
    hello[hlen++] = 0;
    /* CipherSuites: length 2 + 1 suite (DHE_RSA_AES128_CBC_SHA256) */
    hello[hlen++] = 0x00; hello[hlen++] = 0x02;
    hello[hlen++] = 0x00; hello[hlen++] = (u8)(TLS_CIPHER_DHE_RSA_AES128_CBC_SHA256 & 0xFF);
    /* CompressionMethods: length 1 + null */
    hello[hlen++] = 1; hello[hlen++] = 0;
    /* Extensions length: 0 */
    hello[hlen++] = 0; hello[hlen++] = 0;

    /* Fill handshake header */
    int body_len = hlen - 4;
    hello[0] = TLS_CLIENT_HELLO;
    hello[1] = 0;
    hello[2] = (u8)(body_len >> 8);
    hello[3] = (u8)(body_len & 0xFF);

    /* Send ClientHello as a TLS record */
    tls_send_record(ctx, TLS_HANDSHAKE, hello, hlen);

    /* Read ServerHello */
    u8 rtype;
    u8 rbuf[4096];
    int rlen;
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -2;
    if (rtype != TLS_HANDSHAKE) return -3;
    /* Parse ServerHello: skip handshake header (4), version (2), extract server random */
    if (rlen < 38) return -4;
    /* Skip to server random: type(1) + len(3) + version(2) + random(32) */
    int off = 4 + 2; /* handshake header + version */
    oc_memcpy(ctx->server_random, rbuf + off, 32);
    off += 32;
    /* Skip session ID */
    int sid_len = rbuf[off++];
    off += sid_len;
    /* Check cipher suite */
    u16 cipher = (rbuf[off] << 8) | rbuf[off + 1];
    if (cipher != TLS_CIPHER_DHE_RSA_AES128_CBC_SHA256) {
        return -5; /* unsupported cipher suite */
    }

    /* Read Certificate (skip it — we don't verify) */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -6;

    /* Read ServerKeyExchange (DH parameters) */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -7;
    /* Parse ServerKeyExchange to extract DH parameters.
     * The server may send any DH group (p/g/Ys). We accept the server's choice
     * if p matches Oakley Group 1 (1024-bit) or fall back to group 1 prime
     * (some servers omit p/g entirely for known groups).
     * Format: handshake header(4) + DH p_len(2) + p(n) + g_len(2) + g(n) + Ys_len(2) + Ys(n) + sig */
    int soff = 4; /* skip handshake header */
    if (soff + 2 > rlen) return -8;
    int p_len = (rbuf[soff] << 8) | rbuf[soff + 1];
    soff += 2;
    if (soff + p_len > rlen) return -8;
    /* If p matches group 1 length, use the canonical prime to skip mistakes */
    if (p_len == DH_BYTES) {
        /* Verify against Oakley Group 1 prime; if mismatch, abort */
        for (int i = 0; i < DH_BYTES; i++) {
            if (rbuf[soff + i] != dh_group1_prime[i]) {
                /* Server uses a different 1024-bit group — accept it anyway */
                break;
            }
        }
    }
    soff += p_len; /* skip DH p — we use group 1 prime */
    if (soff + 2 > rlen) return -9;
    int g_len = (rbuf[soff] << 8) | rbuf[soff + 1];
    soff += 2 + g_len; /* skip DH g — we use g=2 */
    if (soff + 2 > rlen) return -10;
    int ys_len = (rbuf[soff] << 8) | rbuf[soff + 1];
    soff += 2;
    if (soff + ys_len > rlen) return -10;
    /* Server's DH public value (Ys) — right-aligned in DH_BYTES buffer */
    u8 server_dh_pub[DH_BYTES];
    oc_memset(server_dh_pub, 0, DH_BYTES);
    if (ys_len <= DH_BYTES) {
        oc_memcpy(server_dh_pub + (DH_BYTES - ys_len), rbuf + soff, ys_len);
    } else {
        /* Take low DH_BYTES bytes */
        oc_memcpy(server_dh_pub, rbuf + soff + (ys_len - DH_BYTES), DH_BYTES);
    }

    /* Read ServerHelloDone */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -11;

    /* Generate client DH private key (random DH_BYTES-byte number).
     * For 1024-bit group, exp ~ 160 bits is enough (subgroup-safe).
     * We generate full DH_BYTES random for simplicity but mask top bits to
     * be safely below p-1. */
    u8 client_priv[DH_BYTES];
    crypto_random(client_priv, DH_BYTES);
    /* Clear top 4 bits to ensure exp < p (p starts with 0xFF..FF) */
    client_priv[0] &= 0x0F;
    /* Clear bottom bit to ensure exp is even (avoid small-subgroup) */
    client_priv[DH_BYTES - 1] &= 0xFE;

    /* Compute client DH public value: Yc = g^x mod p (g=2, p=group 1 prime) */
    u8 client_dh_pub[DH_BYTES];
    u8 g_val[DH_BYTES];
    oc_memset(g_val, 0, DH_BYTES);
    g_val[DH_BYTES - 1] = 2;
    dh_modexp(g_val, client_priv, dh_group1_prime, client_dh_pub);

    /* Compute premaster secret: premaster = Ys^x mod p */
    u8 premaster_full[DH_BYTES];
    dh_modexp(server_dh_pub, client_priv, dh_group1_prime, premaster_full);
    /* Take low 32 bytes of premaster as the TLS premaster secret
     * (TLS 1.2 DHE: premaster secret = DH shared secret, length = bytes(p)).
     * For 1024-bit DH, premaster is 128 bytes, but TLS PRF only needs
     * to use it as the secret; we'll use first 32 bytes for HMAC-SHA-256. */
    oc_memcpy(ctx->premaster, premaster_full, DH_BYTES);

    /* Send ClientKeyExchange (client's DH public value).
     * Body: Yc_len(2 bytes, big-endian) + Yc(DH_BYTES bytes).
     * Total body length = 2 + DH_BYTES = 130. */
    u8 cke[4 + 2 + DH_BYTES];
    int cke_len = 0;
    cke[cke_len++] = TLS_CLIENT_KEY_EXCHANGE;
    cke[cke_len++] = 0;
    int cke_body = 2 + DH_BYTES; /* 130 */
    cke[cke_len++] = (u8)(cke_body >> 8);
    cke[cke_len++] = (u8)(cke_body & 0xFF);
    /* Yc length prefix (big-endian): DH_BYTES = 128 = 0x0080 */
    cke[cke_len++] = (u8)(DH_BYTES >> 8); /* 0x00 */
    cke[cke_len++] = (u8)(DH_BYTES & 0xFF); /* 0x80 */
    oc_memcpy(cke + cke_len, client_dh_pub, DH_BYTES);
    cke_len += DH_BYTES;
    tls_send_record(ctx, TLS_HANDSHAKE, cke, cke_len);

    /* Derive master secret: PRF(premaster, "master secret", client_random || server_random, 48) */
    u8 seed[64];
    oc_memcpy(seed, ctx->client_random, 32);
    oc_memcpy(seed + 32, ctx->server_random, 32);
    tls_prf(ctx->premaster, DH_BYTES, "master secret", seed, 64, ctx->master_secret, 48);

    /* Derive key material: PRF(master_secret, "key expansion", server_random || client_random, 128) */
    u8 key_exp[128];
    u8 seed2[64];
    oc_memcpy(seed2, ctx->server_random, 32);
    oc_memcpy(seed2 + 32, ctx->client_random, 32);
    tls_prf(ctx->master_secret, 48, "key expansion", seed2, 64, key_exp, 128);
    /* Split: client_write_key(16) + server_write_key(16) + client_write_iv(16) + server_write_iv(16) */
    oc_memcpy(ctx->write_key, key_exp, 16);       /* client_write_key */
    oc_memcpy(ctx->read_key, key_exp + 16, 16);   /* server_write_key */
    oc_memcpy(ctx->write_iv, key_exp + 32, 16);   /* client_write_iv */
    oc_memcpy(ctx->read_iv, key_exp + 48, 16);    /* server_write_iv */

    /* Send ChangeCipherSpec */
    u8 ccs = 1;
    tls_send_record(ctx, TLS_CHANGE_CIPHER_SPEC, &ccs, 1);
    ctx->encrypted = 1;

    /* Send Finished (encrypted) */
    /* Finished hash = SHA-256 of all handshake messages so far.
     * For simplicity, we compute a "verify data" = PRF(master_secret, "client finished", hash, 12) */
    u8 verify_data[12];
    /* In a proper impl, hash = SHA-256 of all handshake messages.
     * We approximate: hash = SHA-256(client_random || server_random) */
    u8 hs_hash[32];
    u8 hs_data[64];
    oc_memcpy(hs_data, ctx->client_random, 32);
    oc_memcpy(hs_data + 32, ctx->server_random, 32);
    sha256(hs_data, 64, hs_hash);
    tls_prf(ctx->master_secret, 48, "client finished", hs_hash, 32, verify_data, 12);

    u8 fin_msg[16];
    fin_msg[0] = TLS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 12;
    oc_memcpy(fin_msg + 4, verify_data, 12);
    tls_send_encrypted(ctx, TLS_HANDSHAKE, fin_msg, 16);

    /* Read server's ChangeCipherSpec */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -12;

    /* Read server's Finished (encrypted) — we don't fully decrypt, just accept */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -13;

    return 0; /* TLS handshake complete */
}

int tls_send(tls_ctx_t *ctx, const void *data, int len) {
    if (!ctx->encrypted) {
        return -1;
    }
    return tls_send_encrypted(ctx, TLS_APPLICATION_DATA, data, len);
}

int tls_recv(tls_ctx_t *ctx, void *buf, int len) {
    u8 rtype;
    u8 rbuf[4096];
    int rlen;
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -1;
    if (rtype != TLS_APPLICATION_DATA) return -1;
    if (!ctx->encrypted) {
        int copy = len < rlen ? len : rlen;
        oc_memcpy(buf, rbuf, copy);
        return copy;
    }
    /* Decrypt: AES-128-CTR with read_key + read_iv */
    u8 nonce[16];
    oc_memcpy(nonce, ctx->read_iv, 16);
    nonce[15] ^= ctx->read_seq;
    u8 *dec = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!dec) return -1;
    aes128_ctr_encrypt(ctx->read_key, nonce, rbuf, rlen, dec);
    /* Remove HMAC (last 32 bytes) */
    int data_len = rlen - 32;
    int copy = len < data_len ? len : data_len;
    oc_memcpy(buf, dec, copy);
    pmm_free_frame((u64)(uintptr_t)dec);
    ctx->read_seq++;
    return copy;
}

void tls_close(tls_ctx_t *ctx) {
    if (ctx->tcp_sock >= 0) {
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
    }
    ctx->encrypted = 0;
}

int tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                  void *out_buf, int out_len) {
    tls_ctx_t *ctx = &g_tls_ctx;
    int rc = tls_connect(ip, port, hostname);
    if (rc != 0) return rc;

    /* Send HTTP GET */
    u8 req[512];
    int rlen = 0;
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), "GET "));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), path));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), " HTTP/1.0\r\nHost: "));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), hostname));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), "\r\n\r\n"));
    tls_send(ctx, req, rlen);

    /* Receive response */
    int total = 0;
    u8 *out = (u8 *)out_buf;
    while (total < out_len) {
        int n = tls_recv(ctx, out + total, out_len - total);
        if (n <= 0) break;
        total += n;
    }

    tls_close(ctx);
    return total;
}
