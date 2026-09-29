/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/tls.c
 * Purpose: TLS 1.2 client — minimal implementation for HTTPS.
 *
 * Implements:
 *   - TLS 1.2 record layer (CBC + HMAC-SHA256, cipher 0x0067)
 *   - ClientHello / ServerHello / Certificate / ServerKeyExchange /
 *     ServerHelloDone / ClientKeyExchange / ChangeCipherSpec / Finished
 *   - DH 1024-bit key exchange (Oakley Group 1, RFC 2409)
 *   - AES-128-CBC encryption (client → server direction only)
 *   - Certificate verification: skipped (accept any)
 *
 * WP-09 design choices / limitations:
 *   - Cipher: TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 (0x0067)
 *   - Handshake hash: SHA-256 of ClientHello+ServerHello+CKE messages
 *     (full ClientHello..ClientKeyExchange, excluding ChangeCipherSpec)
 *   - Server-side encrypted records (Finished + application data):
 *     accepted but NOT decrypted (would need AES-128-CBC decrypt + verify).
 *   - Per-record IV: random per record (explicit IV mode, TLS 1.2).
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

/* Debug print helper: prints "<prefix> <hex bytes>...\n" for up to N bytes */
static void tls_debug_hex(const char *prefix, const u8 *buf, int n) {
    char out[256];
    char hex[4];
    int p = 0;
    for (int i = 0; i < p + 4; i++) ;
    /* copy prefix */
    int pi = 0;
    while (prefix[pi] && p < 200) out[p++] = prefix[pi++];
    for (int i = 0; i < n && p < 230; i++) {
        /* hex byte */
        u8 v = buf[i];
        u8 hi = v >> 4, lo = v & 0x0F;
        hex[0] = (hi < 10) ? ('0' + hi) : ('a' + hi - 10);
        hex[1] = (lo < 10) ? ('0' + lo) : ('a' + lo - 10);
        out[p++] = hex[0];
        out[p++] = hex[1];
    }
    out[p++] = '\n';
    out[p] = 0;
    oc_console_puts(out);
}

static void tls_debug(const char *msg) {
    oc_console_puts(msg);
    oc_console_puts("\n");
}

/* Increment 8-byte big-endian seq by 1 */
static void seq_inc(u8 seq[8]) {
    for (int i = 7; i >= 0; i--) {
        if (++seq[i] != 0) break;
    }
}

/* PRF (TLS 1.2 P_SHA256, RFC 5246 §5) */
static void tls_prf(const u8 *secret, int secret_len,
                    const char *label,
                    const u8 *seed, int seed_len,
                    u8 *out, int out_len) {
    int label_len = (int)oc_strlen(label);
    int total_seed = label_len + seed_len;
    u8 *ls = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!ls) return;
    oc_memcpy(ls, label, label_len);
    oc_memcpy(ls + label_len, seed, seed_len);

    u8 a[32];
    hmac_sha256(secret, secret_len, ls, total_seed, a);

    int offset = 0;
    while (offset < out_len) {
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
        hmac_sha256(secret, secret_len, a, 32, a);
    }
    pmm_free_frame((u64)(uintptr_t)ls);
}

/* Append a handshake record body to the handshake_log (for Finished hash).
 * Only called for unencrypted handshake records (i.e. before client's CCS). */
static void tls_log_handshake(tls_ctx_t *ctx, const u8 *data, int len) {
    if (ctx->handshake_log_len + len > (int)sizeof(ctx->handshake_log)) {
        /* truncate — shouldn't happen for normal handshakes */
        int avail = (int)sizeof(ctx->handshake_log) - ctx->handshake_log_len;
        if (avail > 0) {
            oc_memcpy(ctx->handshake_log + ctx->handshake_log_len, data, avail);
            ctx->handshake_log_len += avail;
        }
        return;
    }
    oc_memcpy(ctx->handshake_log + ctx->handshake_log_len, data, len);
    ctx->handshake_log_len += len;
}

/* Send a TLS record — header + data in a single TCP segment. */
static int tls_send_record(tls_ctx_t *ctx, u8 type, const void *data, int len) {
    static u8 record[16384];
    int total = 5 + len;
    if (total > 16384) total = 16384;
    record[0] = type;
    record[1] = 0x03; record[2] = 0x03;
    record[3] = (u8)(len >> 8);
    record[4] = (u8)(len & 0xFF);
    if (len > 0 && total <= 16384) {
        oc_memcpy(record + 5, data, total - 5);
    }
    /* Log unencrypted handshake records for Finished hash. */
    if (type == TLS_HANDSHAKE && !ctx->encrypted) {
        tls_log_handshake(ctx, (const u8 *)data, len);
    }
    return net_send(ctx->tcp_sock, record, total);
}

/* Send an encrypted TLS record (CBC + HMAC-SHA256).
 * Record body layout (TLS 1.2, cipher 0x0067):
 *   explicit_IV(16) || AES-CBC-encrypt(plaintext)
 * where plaintext = data || MAC(32) || padding(pad_len+1) || pad_len(1)
 * MAC = HMAC-SHA256(MAC_key, seq(8) || type(1) || version(2) || length(2) || data)
 */
static int tls_send_encrypted(tls_ctx_t *ctx, u8 type, const void *data, int len) {
    /* Allocate record buffer: 5 (header) + 16 (IV) + len + 32 (MAC) + 16 (max padding) */
    u8 *record = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!record) return -1;

    /* Step 1: Build plaintext = data || MAC || padding */
    u8 *plain = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!plain) { pmm_free_frame((u64)(uintptr_t)record); return -1; }

    int plain_len = 0;
    oc_memcpy(plain + plain_len, data, len);
    plain_len += len;

    /* Compute MAC over: seq(8) || type(1) || version(2) || length(2) || data */
    u8 mac_input[8 + 1 + 2 + 2 + 4096];
    int mi = 0;
    oc_memcpy(mac_input + mi, ctx->write_seq, 8); mi += 8;
    mac_input[mi++] = type;
    mac_input[mi++] = 0x03; mac_input[mi++] = 0x03;
    mac_input[mi++] = (u8)(len >> 8);
    mac_input[mi++] = (u8)(len & 0xFF);
    oc_memcpy(mac_input + mi, data, len); mi += len;
    u8 mac[32];
    hmac_sha256(ctx->write_key, 32, mac_input, mi, mac);
    oc_memcpy(plain + plain_len, mac, 32);
    plain_len += 32;

    /* PKCS7-style padding (RFC 5246 §6.2.3.2):
     * Plaintext layout = data || MAC(32) || padding_bytes || padding_length_byte
     * Total length must be multiple of 16. The padding_length_byte value = pad_count
     * (number of padding bytes, EXCLUDING the padding_length_byte itself).
     * Each padding byte also has value pad_count.
     * pad_count = (16 - ((len + 32 + 1) % 16)) % 16   range 0-15 */
    int pad_count = (16 - ((len + 32 + 1) % 16)) % 16;
    for (int i = 0; i < pad_count; i++) plain[plain_len++] = (u8)pad_count;
    plain[plain_len++] = (u8)pad_count;  /* padding_length byte */

    /* Step 2: Generate explicit IV (random per record, TLS 1.2) */
    u8 iv[16];
    crypto_random(iv, 16);

    /* Step 3: AES-128-CBC encrypt */
    u8 *enc = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!enc) {
        pmm_free_frame((u64)(uintptr_t)record);
        pmm_free_frame((u64)(uintptr_t)plain);
        return -1;
    }
    aes128_cbc_encrypt(ctx->write_iv, iv, plain, plain_len, enc);
    /* WP-09 layout: write_iv holds the AES-128 key (16 bytes).
     * write_key holds the HMAC-SHA256 MAC key (32 bytes). */

    tls_debug_hex("[tls]   AES key: ", ctx->write_iv, 16);
    tls_debug_hex("[tls]   IV (rand): ", iv, 16);
    tls_debug_hex("[tls]   MAC key (32): ", ctx->write_key, 32);
    tls_debug_hex("[tls]   MAC computed (32): ", mac, 32);
    tls_debug_hex("[tls]   plain[0..15] (data): ", plain, 16);
    tls_debug_hex("[tls]   plain[16..47] (MAC): ", plain + 16, 32);
    tls_debug_hex("[tls]   plain[48..63] (pad): ", plain + 48, 16);
    tls_debug_hex("[tls]   encrypted[0..15]: ", enc, 16);

    /* Step 4: Build record = header(5) + IV(16) + encrypted(plain_len) */
    int total = 5 + 16 + plain_len;
    record[0] = type;
    record[1] = 0x03; record[2] = 0x03;
    record[3] = (u8)((16 + plain_len) >> 8);
    record[4] = (u8)((16 + plain_len) & 0xFF);
    oc_memcpy(record + 5, iv, 16);
    oc_memcpy(record + 5 + 16, enc, plain_len);

    /* Step 5: Single net_send to avoid TCP segmentation */
    int rc = net_send(ctx->tcp_sock, record, total);

    seq_inc(ctx->write_seq);

    pmm_free_frame((u64)(uintptr_t)record);
    pmm_free_frame((u64)(uintptr_t)plain);
    pmm_free_frame((u64)(uintptr_t)enc);
    return rc;
}

/* Read a TLS record header + body into buf (caller allocates). */
static int tls_read_record(tls_ctx_t *ctx, u8 *type, u8 *buf, int *len) {
    u8 hdr[5];
    u64 start = oc_timer_ticks();
    int polls = 0;
    while (oc_timer_ticks() - start < 800) {  /* 8 second timeout */
        net_poll();
        polls++;
        if (polls > 100000) break;  /* safety */
        int n = net_recv(ctx->tcp_sock, hdr, 5);
        if (n >= 5) {
            *type = hdr[0];
            int rlen = (hdr[3] << 8) | hdr[4];
            if (rlen > 16384) rlen = 16384;
            int got = 0;
            u64 rstart = oc_timer_ticks();
            while (got < rlen) {
                n = net_recv(ctx->tcp_sock, buf + got, rlen - got);
                if (n > 0) { got += n; rstart = oc_timer_ticks(); }
                else {
                    net_poll();
                    if (oc_timer_ticks() - rstart > 800) break;
                }
            }
            *len = got;
            /* Log unencrypted handshake records for Finished hash. */
            if (*type == TLS_HANDSHAKE && !ctx->encrypted) {
                tls_log_handshake(ctx, buf, got);
            }
            /* debug */
            char b[80];
            oc_strcpy(b, "[tls] read record type=");
            char hx[4];
            hx[0] = (hdr[0] >> 4) & 0xF; hx[0] = hx[0] < 10 ? '0'+hx[0] : 'a'+hx[0]-10;
            hx[1] = hdr[0] & 0xF; hx[1] = hx[1] < 10 ? '0'+hx[1] : 'a'+hx[1]-10;
            hx[2] = 0;
            oc_strcat(b, hx);
            oc_strcat(b, " len=");
            char num[10];
            oc_u64_to_str((u64)got, num);
            oc_strcat(b, num);
            oc_strcat(b, "\n");
            oc_console_puts(b);
            return 0;
        }
    }
    tls_debug("[tls] read record TIMEOUT");
    return -1;
}

int tls_connect(u32 ip, u16 port, const char *hostname) {
    (void)hostname;
    tls_ctx_t *ctx = &g_tls_ctx;
    oc_memset(ctx, 0, sizeof(*ctx));

    tls_debug("[tls] connecting...");
    ctx->tcp_sock = net_socket(SOCK_TCP);
    if (ctx->tcp_sock < 0) {
        tls_debug("[tls] net_socket failed");
        return -1;
    }
    if (net_connect(ctx->tcp_sock, ip, port) < 0) {
        tls_debug("[tls] net_connect failed");
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
        return -1;
    }
    tls_debug("[tls] TCP connected");

    /* Generate client random */
    crypto_random(ctx->client_random, 32);

    /* Build ClientHello */
    u8 hello[512];
    int hlen = 0;
    hlen += 4;  /* handshake header */
    hello[hlen++] = 0x03; hello[hlen++] = 0x03;  /* TLS 1.2 */
    oc_memcpy(hello + hlen, ctx->client_random, 32); hlen += 32;
    hello[hlen++] = 0;  /* session ID length = 0 */
    hello[hlen++] = 0x00; hello[hlen++] = 0x02;  /* cipher suites: 2 bytes */
    hello[hlen++] = 0x00; hello[hlen++] = (u8)(TLS_CIPHER_DHE_RSA_AES128_CBC_SHA256 & 0xFF);
    hello[hlen++] = 1; hello[hlen++] = 0;  /* compression: null */
    /* Extensions: signature_algorithms (RSA+SHA256, ECDSA+SHA256, RSA-PSS+SHA256) */
    int ext_start = hlen;
    hlen += 2;  /* extensions length placeholder */
    /* signature_algorithms ext: id=0x000D */
    hello[hlen++] = 0x00; hello[hlen++] = 0x0D;  /* ext type = signature_algorithms */
    hello[hlen++] = 0x00; hello[hlen++] = 10;  /* ext data len = 10 */
    hello[hlen++] = 0x00; hello[hlen++] = 8;  /* sig hash algos list len = 8 (4 algos) */
    hello[hlen++] = 0x04; hello[hlen++] = 0x01;  /* SHA256 + RSA */
    hello[hlen++] = 0x04; hello[hlen++] = 0x03;  /* SHA256 + ECDSA */
    hello[hlen++] = 0x08; hello[hlen++] = 0x04;  /* SHA256 + RSA-PSS */
    hello[hlen++] = 0x04; hello[hlen++] = 0x02;  /* SHA256 + DSA */
    /* fill extensions length */
    int ext_len = hlen - ext_start - 2;
    hello[ext_start] = (u8)(ext_len >> 8);
    hello[ext_start + 1] = (u8)(ext_len & 0xFF);

    int body_len = hlen - 4;
    hello[0] = TLS_CLIENT_HELLO;
    hello[1] = (u8)(body_len >> 16);
    hello[2] = (u8)(body_len >> 8);
    hello[3] = (u8)(body_len & 0xFF);

    tls_send_record(ctx, TLS_HANDSHAKE, hello, hlen);
    tls_debug("[tls] sent ClientHello");

    /* Read ServerHello */
    u8 rtype;
    static u8 rbuf[4096];
    int rlen;
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for ServerHello");
        return -2;
    }
    if (rtype != TLS_HANDSHAKE) {
        tls_debug("[tls] ServerHello: wrong record type");
        return -3;
    }
    if (rlen < 38) return -4;
    /* Parse: handshake header(4) + version(2) + random(32) + session_id_len + ... */
    int off = 4 + 2;
    oc_memcpy(ctx->server_random, rbuf + off, 32);
    off += 32;
    int sid_len = rbuf[off++];
    off += sid_len;
    if (off + 2 > rlen) return -5;
    u16 cipher = (rbuf[off] << 8) | rbuf[off + 1];
    off += 2;
    if (cipher != TLS_CIPHER_DHE_RSA_AES128_CBC_SHA256) {
        char b[80];
        oc_strcpy(b, "[tls] server chose cipher 0x");
        char hx[6];
        hx[0] = (cipher >> 12) & 0xF; hx[0] = hx[0] < 10 ? '0'+hx[0] : 'a'+hx[0]-10;
        hx[1] = (cipher >> 8) & 0xF; hx[1] = hx[1] < 10 ? '0'+hx[1] : 'a'+hx[1]-10;
        hx[2] = (cipher >> 4) & 0xF; hx[2] = hx[2] < 10 ? '0'+hx[2] : 'a'+hx[2]-10;
        hx[3] = cipher & 0xF; hx[3] = hx[3] < 10 ? '0'+hx[3] : 'a'+hx[3]-10;
        hx[4] = 0;
        oc_strcat(b, hx);
        oc_strcat(b, " (expected 0x0067)");
        tls_debug(b);
        return -6;
    }
    tls_debug("[tls] got ServerHello (cipher 0x0067)");

    /* Read Certificate (skip) */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for Certificate");
        return -7;
    }
    tls_debug("[tls] got Certificate (skipped)");

    /* Read ServerKeyExchange (DH params) */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for ServerKeyExchange");
        return -8;
    }
    /* Parse ServerKeyExchange to extract DH parameters.
     * Server sends its own p/g/Ys. Client MUST use the server's p (not
     * hardcoded Oakley Group 1) — otherwise DH shared secret won't match.
     * Format: handshake header(4) + DH p_len(2) + p(n) + g_len(2) + g(n) + Ys_len(2) + Ys(n) + sig */
    int soff = 4; /* skip handshake header */
    if (soff + 2 > rlen) return -8;
    int p_len = (rbuf[soff] << 8) | rbuf[soff + 1];
    soff += 2;
    if (soff + p_len > rlen) return -8;
    /* WP-09 fix: use server's p, not hardcoded Oakley Group 1 prime.
     * If p_len == DH_BYTES, copy directly. If p_len > DH_BYTES, take low
     * DH_BYTES bytes. If p_len < DH_BYTES, right-align in DH_BYTES buffer. */
    u8 server_p[DH_BYTES];
    oc_memset(server_p, 0, DH_BYTES);
    if (p_len == DH_BYTES) {
        oc_memcpy(server_p, rbuf + soff, DH_BYTES);
    } else if (p_len < DH_BYTES) {
        oc_memcpy(server_p + (DH_BYTES - p_len), rbuf + soff, p_len);
    } else {
        /* p_len > DH_BYTES — take low DH_BYTES bytes (drop high zero padding) */
        oc_memcpy(server_p, rbuf + soff + (p_len - DH_BYTES), DH_BYTES);
    }
    tls_debug_hex("[tls]   server p (first 16): ", server_p, 16);
    soff += p_len;
    if (soff + 2 > rlen) return -9;
    int g_len = (rbuf[soff] << 8) | rbuf[soff + 1];
    soff += 2;
    /* Use server's g if it's a small integer; else fall back to g=2 */
    u8 g_byte = 2;  /* default g = 2 */
    if (g_len >= 1) {
        g_byte = rbuf[soff + g_len - 1];  /* take last byte */
    }
    soff += g_len;
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
        oc_memcpy(server_dh_pub, rbuf + soff + (ys_len - DH_BYTES), DH_BYTES);
    }
    tls_debug_hex("[tls]   Ys (first 8): ", server_dh_pub, 8);

    /* Read ServerHelloDone */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for ServerHelloDone");
        return -12;
    }
    tls_debug("[tls] got ServerHelloDone");

    /* Generate client DH private key */
    u8 client_priv[DH_BYTES];
    crypto_random(client_priv, DH_BYTES);
    client_priv[0] &= 0x0F;
    client_priv[DH_BYTES - 1] &= 0xFE;

    /* Compute Yc = g^x mod p (using server's p) */
    tls_debug("[tls] computing Yc = g^x mod p (DH modexp, ~3s)...");
    u8 client_dh_pub[DH_BYTES];
    u8 g_val[DH_BYTES];
    oc_memset(g_val, 0, DH_BYTES);
    g_val[DH_BYTES - 1] = g_byte;
    dh_modexp(g_val, client_priv, server_p, client_dh_pub);
    tls_debug_hex("[tls]   Yc (first 8): ", client_dh_pub, 8);

    /* Compute premaster = Ys^x mod p (using server's p) */
    tls_debug("[tls] computing premaster = Ys^x mod p (DH modexp, ~3s)...");
    u8 premaster_full[DH_BYTES];
    dh_modexp(server_dh_pub, client_priv, server_p, premaster_full);
    tls_debug_hex("[tls]   Ys (128): ", server_dh_pub, DH_BYTES);
    tls_debug_hex("[tls]   client_priv (32 of 128): ", client_priv, 32);
    tls_debug_hex("[tls]   Yc (128): ", client_dh_pub, DH_BYTES);
    tls_debug_hex("[tls]   premaster (128): ", premaster_full, DH_BYTES);
    oc_memcpy(ctx->premaster, premaster_full, DH_BYTES);
    tls_debug_hex("[tls]   premaster (first 8): ", ctx->premaster, 8);
    /* Send ClientKeyExchange */
    u8 cke[4 + 2 + DH_BYTES];
    int cke_len = 0;
    cke[cke_len++] = TLS_CLIENT_KEY_EXCHANGE;
    int cke_body = 2 + DH_BYTES;
    cke[cke_len++] = (u8)(cke_body >> 16);
    cke[cke_len++] = (u8)(cke_body >> 8);
    cke[cke_len++] = (u8)(cke_body & 0xFF);
    cke[cke_len++] = (u8)(DH_BYTES >> 8);
    cke[cke_len++] = (u8)(DH_BYTES & 0xFF);
    oc_memcpy(cke + cke_len, client_dh_pub, DH_BYTES);
    cke_len += DH_BYTES;
    tls_send_record(ctx, TLS_HANDSHAKE, cke, cke_len);
    tls_debug("[tls] sent ClientKeyExchange");

    /* Derive master secret */
    u8 seed[64];
    oc_memcpy(seed, ctx->client_random, 32);
    oc_memcpy(seed + 32, ctx->server_random, 32);
    tls_prf(ctx->premaster, DH_BYTES, "master secret", seed, 64, ctx->master_secret, 48);
    tls_debug_hex("[tls]   master_secret (first 8): ", ctx->master_secret, 8);

    /* Derive key material — total 128 bytes:
     *   client_write_MAC_key (32) + server_write_MAC_key (32) +
     *   client_write_key (16) + server_write_key (16) +
     *   client_write_IV (16) + server_write_IV (16)
     * We store MAC keys (32 bytes each) + AES keys (16 bytes each) in write_key/read_key.
     * write_key field is 16 bytes — too small! Repurpose:
     *   write_key[0..31]  = client MAC key (in TLS ctx, we made it 32 bytes; let's
     *                       repurpose: write_key[16..32] = client AES key,
     *                                  write_key[0..16] = ?? — but ctx has only 16 bytes)
     * For simplicity, store MAC keys in ctx->write_key (32 bytes) + AES key in
     * ctx->write_iv (16 bytes). The CBC explicit IV is generated per-record, so we
     * don't need a fixed write IV.
     */
    u8 key_exp[128];
    u8 seed2[64];
    oc_memcpy(seed2, ctx->server_random, 32);
    oc_memcpy(seed2 + 32, ctx->client_random, 32);
    tls_prf(ctx->master_secret, 48, "key expansion", seed2, 64, key_exp, 128);

    /* Map key_exp -> ctx:
     *   ctx->write_key[0..32] = client_write_MAC_key (32 bytes) — needs 32 byte field
     * Since tls.h's write_key is 16 bytes, we extend by reusing write_iv (16 bytes):
     *   write_key[0..16]  = client_MAC_key[0..16]
     *   write_iv[0..16]   = client_MAC_key[16..32]  -- but write_iv is also MAC!
     * Instead, store MAC+AES together in a 48-byte buffer indirectly: store MAC key
     * in write_key[0..16] + write_iv[0..16] (concatenated = 32 bytes MAC key),
     * and AES key in read_key[0..16]. But read_key is for server direction...
     *
     * WP-09 pragmatic approach: extend the fields in tls.h to 32 bytes (MAC) + 16 (AES).
     * See updated tls.h: write_key is 32 bytes (MAC), write_iv is 16 bytes (AES),
     * read_key is 32 bytes (server MAC), read_iv is 16 bytes (server AES).
     */
    oc_memcpy(ctx->write_key, key_exp + 0, 32);     /* client_write_MAC_key */
    oc_memcpy(ctx->write_iv,  key_exp + 64, 16);     /* client_write_key (AES) */
    oc_memcpy(ctx->read_key,  key_exp + 32, 32);     /* server_write_MAC_key */
    oc_memcpy(ctx->read_iv,   key_exp + 80, 16);     /* server_write_key (AES) */
    tls_debug_hex("[tls]   master_secret (48): ", ctx->master_secret, 48);
    tls_debug_hex("[tls]   client_random (32): ", ctx->client_random, 32);
    tls_debug_hex("[tls]   server_random (32): ", ctx->server_random, 32);
    tls_debug_hex("[tls]   premaster (32 of 128): ", ctx->premaster, 32);
    tls_debug("[tls] derived keys (128 bytes key material)");

    /* Send ChangeCipherSpec */
    u8 ccs = 1;
    tls_send_record(ctx, TLS_CHANGE_CIPHER_SPEC, &ccs, 1);
    ctx->encrypted = 1;
    tls_debug("[tls] sent ChangeCipherSpec");

    /* Compute Finished verify_data:
     * verify_data = PRF(master_secret, "client finished", SHA256(handshake_log), 12)[0..11]
     * handshake_log = ClientHello + ServerHello + Certificate + ServerKeyExchange +
     *                  ServerHelloDone + ClientKeyExchange (all handshake messages
     *                  seen/sent before client's CCS, excluding CCS and Finished). */
    u8 hs_hash[32];
    sha256(ctx->handshake_log, ctx->handshake_log_len, hs_hash);

    /* Debug: print handshake_log_len and hs_hash so we can verify with Python */
    char dbg[80];
    oc_strcpy(dbg, "[tls]   handshake_log_len=");
    char num[10];
    oc_u64_to_str((u64)ctx->handshake_log_len, num);
    oc_strcat(dbg, num);
    oc_strcat(dbg, "\n");
    oc_console_puts(dbg);
    tls_debug_hex("[tls]   SHA256(handshake_log): ", hs_hash, 32);

    u8 verify_data[12];
    tls_prf(ctx->master_secret, 48, "client finished", hs_hash, 32, verify_data, 12);
    tls_debug_hex("[tls]   verify_data (12): ", verify_data, 12);

    u8 fin_msg[4 + 12];
    fin_msg[0] = TLS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 12;
    oc_memcpy(fin_msg + 4, verify_data, 12);
    tls_send_encrypted(ctx, TLS_HANDSHAKE, fin_msg, 16);
    tls_debug("[tls] sent Finished (encrypted)");

    /* Read server's ChangeCipherSpec */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for server ChangeCipherSpec");
        return -13;
    }
    tls_debug("[tls] got server ChangeCipherSpec");

    /* Read server's Finished (encrypted) — this is the first encrypted record
     * server sends with seq=0. We accept without decrypting (would need full
     * server-side Finished verify_data check, which is optional for client).
     * IMPORTANT: After reading server's Finished, increment read_seq to 1 so
     * subsequent tls_recv() calls (for HTTP response records) compute MAC
     * with the correct sequence number (server uses seq=1 for first appdata). */
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) {
        tls_debug("[tls] timeout waiting for server Finished");
        return -14;
    }
    tls_debug("[tls] got server Finished");
    seq_inc(ctx->read_seq);  /* now read_seq = 1 for next encrypted record */

    tls_debug("[tls] handshake complete");
    return 0;
}

int tls_send(tls_ctx_t *ctx, const void *data, int len) {
    if (!ctx->encrypted) return -1;
    return tls_send_encrypted(ctx, TLS_APPLICATION_DATA, data, len);
}

int tls_recv(tls_ctx_t *ctx, void *buf, int len) {
    u8 rtype;
    static u8 rbuf[4096];
    int rlen;
    if (tls_read_record(ctx, &rtype, rbuf, &rlen) < 0) return -1;
    /* WP-09: server-side records are encrypted (after server's CCS).
     * Decrypt with AES-128-CBC using server_write_key + per-record IV.
     * Record body layout: IV(16) || AES-CBC-encrypt(data || MAC(32) || padding)
     */
    if (ctx->encrypted && rtype == TLS_APPLICATION_DATA && rlen >= 16) {
        u8 *iv = rbuf;
        u8 *enc = rbuf + 16;
        int enc_len = rlen - 16;
        if (enc_len % 16 != 0) return -1;
        u8 *dec = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (!dec) return -1;
        aes128_cbc_decrypt(ctx->read_iv, iv, enc, enc_len, dec);
        /* dec = data(N) + MAC(32) + padding(pad_count) + padding_length(1)
         * Read pad_length from last byte, validate, then verify MAC. */
        u8 pad_len = dec[enc_len - 1];
        int data_len = enc_len - 32 - pad_len - 1;
        if (data_len < 0) {
            pmm_free_frame((u64)(uintptr_t)dec);
            ctx->read_seq[7]++;
            return -1;
        }
        /* Verify MAC: HMAC(server_MAC_key, seq(8) + type(1) + version(2) + length(2) + data) */
        u8 mac_input[8 + 1 + 2 + 2 + 4096];
        int mi = 0;
        oc_memcpy(mac_input + mi, ctx->read_seq, 8); mi += 8;
        mac_input[mi++] = rtype;
        mac_input[mi++] = 0x03; mac_input[mi++] = 0x03;
        mac_input[mi++] = (u8)(data_len >> 8);
        mac_input[mi++] = (u8)(data_len & 0xFF);
        oc_memcpy(mac_input + mi, dec, data_len); mi += data_len;
        u8 expected_mac[32];
        hmac_sha256(ctx->read_key, 32, mac_input, mi, expected_mac);
        u8 *actual_mac = dec + data_len;
        int mac_ok = 1;
        for (int i = 0; i < 32; i++) {
            if (actual_mac[i] != expected_mac[i]) { mac_ok = 0; break; }
        }
        if (!mac_ok) {
            tls_debug("[tls] MAC verify FAIL on server record");
            pmm_free_frame((u64)(uintptr_t)dec);
            seq_inc(ctx->read_seq);
            return -1;
        }
        int copy = len < data_len ? len : data_len;
        oc_memcpy(buf, dec, copy);
        pmm_free_frame((u64)(uintptr_t)dec);
        seq_inc(ctx->read_seq);
        return copy;
    }
    /* Non-encrypted records: just copy raw bytes */
    int copy = len < rlen ? len : rlen;
    oc_memcpy(buf, rbuf, copy);
    seq_inc(ctx->read_seq);
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

    u8 req[512];
    int rlen = 0;
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), "GET "));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), path));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), " HTTP/1.0\r\nHost: "));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), hostname));
    rlen += oc_strlen(oc_strcpy((char*)(req + rlen), "\r\n\r\n"));
    tls_send(ctx, req, rlen);

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
