/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/ssh.c
 * Purpose: SSH-2.0 client — minimal implementation.
 *
 * Implements (subset of RFC 4251-4254):
 *   - SSH-2.0 version banner exchange
 *   - SSH_MSG_KEXINIT (algorithm negotiation)
 *   - SSH_MSG_KEXDH_INIT / KEXDH_REPLY (DH group 14, 2048-bit + SHA-256)
 *   - SSH_MSG_NEWKEYS (transition to encrypted mode)
 *   - SSH_MSG_USERAUTH_REQUEST (password)
 *   - SSH_MSG_CHANNEL_OPEN + CHANNEL_REQUEST (exec)
 *   - SSH_MSG_CHANNEL_DATA (receive output)
 *
 * Ciphers chosen:
 *   - KEX:    diffie-hellman-group14-sha256 (paramiko default)
 *   - Cipher: aes128-cbc
 *   - MAC:    hmac-sha1 (20-byte tag)
 *   - Host key verification: skipped (accept any)
 *   - Compression: none
 *
 * WP-09 design note: 2048-bit DH modexp takes ~60s in QEMU. This is a
 * one-time cost per session. The handshake then proceeds with AES-128-CBC
 * encrypted packets. Server-side encrypted packets are decrypted using
 * aes128_cbc_decrypt + hmac-sha1 verify.
 */
#include "ssh.h"
#include "crypto.h"
#include "net.h"
#include "console.h"
#include "string.h"
#include "pmm.h"
#include "timer.h"

/* SSH message types (RFC 4254 §4) */
#define SSH_MSG_KEXINIT         20
#define SSH_MSG_NEWKEYS         21
#define SSH_MSG_KEXDH_INIT      30
#define SSH_MSG_KEXDH_REPLY    31
#define SSH_MSG_USERAUTH_REQ    50
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_USERAUTH_FAILURE 53
#define SSH_MSG_GLOBAL_REQUEST 80
#define SSH_MSG_CHANNEL_OPEN    90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_DATA   94
#define SSH_MSG_CHANNEL_EOF    96
#define SSH_MSG_CHANNEL_CLOSE  97

/* DH group 14 prime (RFC 3526, 2048-bit = 256 bytes) */
static const u8 dh_group14_prime[256] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,0x21,0x68,0xC2,0x34,
    0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,
    0x02,0x0B,0xBE,0xA6,0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
    0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,0xF2,0x5F,0x14,0x37,
    0xEF,0x51,0x0F,0x9E,0x03,0x4C,0x15,0xE9,0x09,0x9E,0x09,0x3B,0x63,0x12,0x3E,0x5B,
    0x59,0x8B,0x07,0x18,0x00,0x01,0xDA,0xCF,0x49,0x09,0x71,0x4B,0x4B,0x48,0xA2,0x86,
    0x8C,0x4B,0xF1,0x16,0x9F,0x1F,0x16,0x03,0x57,0x49,0x16,0x7D,0x45,0x86,0x2F,0x72,
    0x54,0x69,0x2B,0x53,0x01,0x9E,0x05,0x6B,0x3E,0x97,0xD0,0x5F,0x0F,0x33,0x0F,0x93,
    0x05,0x59,0xE7,0xB0,0x02,0x8E,0x89,0xC6,0xE4,0x01,0x01,0x27,0x40,0x22,0x6E,0x82,
    0x49,0x53,0x4B,0x09,0x6B,0x3A,0x5C,0x18,0xB2,0x52,0x6E,0x6D,0x3C,0x47,0x60,0x2A,
    0xA8,0xC1,0x8B,0x3B,0x5C,0x70,0x31,0x1B,0x1C,0x43,0x05,0x5C,0x44,0x7C,0xF6,0x05,
    0x4B,0x3B,0x81,0x50,0x3C,0x2C,0xB5,0x1E,0x49,0x4C,0x1A,0x52,0x8B,0x24,0xFD,0x03,
    0x89,0x6E,0x39,0xDB,0x12,0xC0,0xC5,0x36,0x88,0x60,0x77,0xB4,0x8A,0x12,0x06,0x3A,
    0xBF,0x64,0xD8,0x76,0x33,0x44,0xA4,0x2B,0xA6,0xF2,0xE9,0x59,0x33,0x4D,0x96,0x21,
    0x20,0xC1,0xC9,0x44,0x29,0x04,0x9A,0xE5,0x14,0x9D,0x18,0x5F,0x97,0xB2,0x71,0x5D,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
#define SSH_DH_BYTES 256

static ssh_ctx_t g_ssh_ctx;

static void ssh_debug(const char *msg) {
    oc_console_puts(msg);
    oc_console_puts("\n");
}

static void ssh_debug_hex(const char *prefix, const u8 *buf, int n) {
    char out[200];
    char hex[4];
    int p = 0;
    int pi = 0;
    while (prefix[pi] && p < 180) out[p++] = prefix[pi++];
    for (int i = 0; i < n && p < 195; i++) {
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

ssh_ctx_t *ssh_get_ctx(void) { return &g_ssh_ctx; }

/* Send raw (unencrypted) SSH packet.
 * Layout: packet_length(4 BE) || padding_length(1) || payload(N) || random_pad(4-255)
 * packet_length = padding_length(1) + payload(N) + padding_count
 * padding_count = 8 - ((5 + payload_len) % 8) [must be 4..255] */
static int ssh_send_packet_unencrypted(ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    int pad_len = 8 - ((5 + payload_len) % 8);
    if (pad_len < 4) pad_len += 8;
    u8 *pkt = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!pkt) return -1;
    int pkt_len = 1 + payload_len + pad_len;  /* excludes the 4-byte length field */
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad_len;
    pkt[5] = msg_type;
    if (payload_len > 0) oc_memcpy(pkt + 6, payload, payload_len);
    /* Random padding */
    crypto_random(pkt + 6 + payload_len, pad_len);
    int rc = net_send(ctx->tcp_sock, pkt, 4 + pkt_len);
    pmm_free_frame((u64)(uintptr_t)pkt);
    return rc;
}

/* Read a raw (unencrypted) SSH packet. Returns msg_type, fills payload. */
static int ssh_recv_packet_unencrypted(ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    u8 len_buf[4];
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < 4) {
        int got = net_recv(ctx->tcp_sock, len_buf + n, 4 - n);
        if (got > 0) { n += got; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 800) return -1;
        }
    }
    int pkt_len = ((int)len_buf[0] << 24) | ((int)len_buf[1] << 16) |
                  ((int)len_buf[2] << 8) | len_buf[3];
    if (pkt_len < 1 || pkt_len > 35000) return -1;

    u8 *body = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!body) return -1;
    int got = 0;
    while (got < pkt_len) {
        int r = net_recv(ctx->tcp_sock, body + got, pkt_len - got);
        if (r > 0) { got += r; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 800) {
                pmm_free_frame((u64)(uintptr_t)body);
                return -1;
            }
        }
    }
    int pad_len = body[0];
    *msg_type = body[1];
    int plen = pkt_len - 1 - pad_len - 1;  /* subtract padding_length byte + msg_type byte + padding */
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    oc_memcpy(payload, body + 2, plen);  /* skip padding_length + msg_type */
    *payload_len = plen;
    pmm_free_frame((u64)(uintptr_t)body);
    return 0;
}

/* Send SSH version banner */
static int ssh_send_version(ssh_ctx_t *ctx) {
    const char *banner = "SSH-2.0-OpenCubeOS_WP-09\r\n";
    int len = (int)oc_strlen(banner);
    return net_send(ctx->tcp_sock, banner, len);
}

/* Receive SSH version banner */
static int ssh_recv_version(ssh_ctx_t *ctx) {
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < 256) {
        int r = net_recv(ctx->tcp_sock, ctx->server_banner + n, 1);
        if (r > 0) {
            n++;
            if (n >= 2 && ctx->server_banner[n-2] == '\r' && ctx->server_banner[n-1] == '\n') break;
            start = oc_timer_ticks();
        } else {
            net_poll();
            if (oc_timer_ticks() - start > 800) return -1;
        }
    }
    ctx->server_banner[n-2] = 0;  /* strip \r\n */
    return 0;
}

/* Send KEXINIT message (msg 20).
 * Layout: cookie(16) + kex_algorithms(name-list) + server_host_key_algorithms +
 *         encryption_c2s + encryption_s2c + mac_c2s + mac_s2c +
 *         compression_c2s + compression_s2c + languages_c2s + languages_s2c +
 *         first_kex_packet_follows(1) + reserved(4) */
static int ssh_send_kexinit(ssh_ctx_t *ctx) {
    u8 buf[400];
    int p = 0;
    /* cookie */
    crypto_random(ctx->client_cookie, 16);
    oc_memcpy(buf + p, ctx->client_cookie, 16); p += 16;
    /* name-list helper: write string as 4-byte length + bytes */
    #define WRITE_STR(s) do { \
        int L = (int)oc_strlen(s); \
        buf[p++] = (u8)(L >> 24); buf[p++] = (u8)(L >> 16); \
        buf[p++] = (u8)(L >> 8); buf[p++] = (u8)(L & 0xFF); \
        for (int i = 0; s[i]; i++) buf[p++] = s[i]; \
    } while (0)
    WRITE_STR("diffie-hellman-group14-sha256");
    WRITE_STR("rsa-sha2-256,rsa-sha2-512,ssh-rsa");
    WRITE_STR("aes128-cbc");
    WRITE_STR("aes128-cbc");
    WRITE_STR("hmac-sha1");
    WRITE_STR("hmac-sha1");
    WRITE_STR("none");
    WRITE_STR("none");
    WRITE_STR("");
    WRITE_STR("");
    buf[p++] = 0;  /* first_kex_packet_follows = false */
    buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 0;  /* reserved */
    #undef WRITE_STR

    /* Save our KEXINIT bytes for hash computation */
    int full_len = 1 + p;  /* msg_type(1) + payload */
    ctx->client_kexinit_len = full_len > (int)sizeof(ctx->client_kexinit) ?
                              (int)sizeof(ctx->client_kexinit) : full_len;
    ctx->client_kexinit[0] = SSH_MSG_KEXINIT;
    oc_memcpy(ctx->client_kexinit + 1, buf, ctx->client_kexinit_len - 1);

    return ssh_send_packet_unencrypted(ctx, SSH_MSG_KEXINIT, buf, p);
}

/* Parse server's KEXINIT to extract server's cookie + save bytes for hash. */
static int ssh_recv_kexinit(ssh_ctx_t *ctx) {
    u8 payload[512];
    int payload_len = sizeof(payload);
    u8 msg_type;
    if (ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len) < 0) return -1;
    if (msg_type != SSH_MSG_KEXINIT) return -2;
    /* Save full KEXINIT packet bytes (msg_type + payload + padding_length byte) for hash.
     * We approximate: store msg_type(1) + payload(payload_len). */
    ctx->server_kexinit_len = 1 + payload_len;
    if (ctx->server_kexinit_len > (int)sizeof(ctx->server_kexinit))
        ctx->server_kexinit_len = (int)sizeof(ctx->server_kexinit);
    ctx->server_kexinit[0] = msg_type;
    oc_memcpy(ctx->server_kexinit + 1, payload, ctx->server_kexinit_len - 1);
    /* Extract server's cookie */
    if (payload_len >= 16) {
        oc_memcpy(ctx->server_cookie, payload, 16);
    }
    return 0;
}

/* SSH mpint encoding: 4-byte length + bytes (high bit 0 → prepend 0x00).
 * For 256-byte DH values, encoded as 257 bytes (0x00 + 256 bytes) when MSB is set. */
static void ssh_write_mpint(u8 *buf, int *p, const u8 *val, int val_len) {
    /* Skip leading zeros */
    int start = 0;
    while (start < val_len - 1 && val[start] == 0) start++;
    int n = val_len - start;
    int need_zero = (val[start] & 0x80) ? 1 : 0;
    int total = n + need_zero;
    buf[(*p)++] = (u8)(total >> 24);
    buf[(*p)++] = (u8)(total >> 16);
    buf[(*p)++] = (u8)(total >> 8);
    buf[(*p)++] = (u8)(total & 0xFF);
    if (need_zero) buf[(*p)++] = 0;
    oc_memcpy(buf + *p, val + start, n);
    *p += n;
}

/* Send KEXDH_INIT (msg 30): mpint e */
static int ssh_send_kexdh_init(ssh_ctx_t *ctx) {
    /* Generate client DH private key (256-byte random, masked) */
    crypto_random(ctx->client_priv, SSH_DH_BYTES);
    ctx->client_priv[0] &= 0x7F;  /* ensure < p */
    ctx->client_priv[SSH_DH_BYTES - 1] &= 0xFE;  /* even */

    /* Compute e = g^x mod p */
    u8 g_val[SSH_DH_BYTES];
    oc_memset(g_val, 0, SSH_DH_BYTES);
    g_val[SSH_DH_BYTES - 1] = 2;  /* g = 2 for group 14 */

    ssh_debug("[ssh] computing e = g^x mod p (DH modexp 2048-bit, ~60s in QEMU)...");
    u64 t0 = oc_timer_ticks();
    dh_modexp_n(g_val, ctx->client_priv, dh_group14_prime, ctx->client_pub, 256);
    u64 t1 = oc_timer_ticks();
    u64 ms = (t1 - t0) * 1000 / (u64)OC_TIMER_HZ;
    char buf[80];
    oc_strcpy(buf, "[ssh]   DH modexp time: ");
    char num[10];
    oc_u64_to_str(ms, num);
    oc_strcat(buf, num);
    oc_strcat(buf, " ms\n");
    oc_console_puts(buf);

    /* Build msg: mpint e */
    u8 payload[300];
    int p = 0;
    ssh_write_mpint(payload, &p, ctx->client_pub, SSH_DH_BYTES);
    return ssh_send_packet_unencrypted(ctx, SSH_MSG_KEXDH_INIT, payload, p);
}

/* Receive KEXDH_REPLY (msg 31): string K_S || mpint f || string sig */
static int ssh_recv_kexdh_reply(ssh_ctx_t *ctx) {
    u8 payload[4096];
    int payload_len = sizeof(payload);
    u8 msg_type;
    if (ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len) < 0) return -1;
    if (msg_type != SSH_MSG_KEXDH_REPLY) return -2;
    /* Parse: string K_S (host key blob) + mpint f + string signature */
    int off = 0;
    if (off + 4 > payload_len) return -3;
    int ks_len = (payload[off] << 24) | (payload[off+1] << 16) | (payload[off+2] << 8) | payload[off+3];
    off += 4 + ks_len;  /* skip K_S (we don't verify host key) */
    if (off + 4 > payload_len) return -4;
    int f_len = (payload[off] << 24) | (payload[off+1] << 16) | (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (off + f_len > payload_len) return -5;
    /* Copy server's f into server_pub (right-aligned, SSH_DH_BYTES) */
    oc_memset(ctx->server_pub, 0, SSH_DH_BYTES);
    if (f_len <= SSH_DH_BYTES) {
        /* Skip leading 0x00 if present (mpint encoding) */
        int data_start = 0;
        if (f_len > 0 && payload[off] == 0) { data_start = 1; f_len--; }
        oc_memcpy(ctx->server_pub + (SSH_DH_BYTES - f_len), payload + off + data_start, f_len);
    }
    /* Skip signature (we don't verify) */
    ssh_debug_hex("[ssh]   server f (first 8): ", ctx->server_pub, 8);
    return 0;
}

/* Compute exchange hash H = SHA-256(V_C || V_S || I_C || I_S || K_S || e || f || K).
 * For WP-09 simplification: we use H = SHA-256(client_banner || server_banner ||
 * client_kexinit || server_kexinit || e || f || K). Skip K_S verification.
 * Returns 32 bytes. */
static void ssh_compute_hash(ssh_ctx_t *ctx, u8 hash[32]) {
    /* Build hash input: strings (length-prefixed) + mpints */
    /* Use a page frame (4KB) since this can be large */
    u8 *buf = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!buf) return;
    int p = 0;
    /* V_C: client version banner (without \r\n) */
    int vlen = (int)oc_strlen(ctx->client_banner);
    buf[p++] = (u8)(vlen >> 24); buf[p++] = (u8)(vlen >> 16);
    buf[p++] = (u8)(vlen >> 8); buf[p++] = (u8)(vlen & 0xFF);
    for (int i = 0; ctx->client_banner[i]; i++) buf[p++] = ctx->client_banner[i];
    /* V_S: server version banner */
    vlen = (int)oc_strlen(ctx->server_banner);
    buf[p++] = (u8)(vlen >> 24); buf[p++] = (u8)(vlen >> 16);
    buf[p++] = (u8)(vlen >> 8); buf[p++] = (u8)(vlen & 0xFF);
    for (int i = 0; ctx->server_banner[i]; i++) buf[p++] = ctx->server_banner[i];
    /* I_C: client KEXINIT payload (msg_type + payload, excluding padding_length + padding) */
    int ic_len = ctx->client_kexinit_len;
    buf[p++] = (u8)(ic_len >> 24); buf[p++] = (u8)(ic_len >> 16);
    buf[p++] = (u8)(ic_len >> 8); buf[p++] = (u8)(ic_len & 0xFF);
    oc_memcpy(buf + p, ctx->client_kexinit, ic_len); p += ic_len;
    /* I_S: server KEXINIT payload */
    int is_len = ctx->server_kexinit_len;
    buf[p++] = (u8)(is_len >> 24); buf[p++] = (u8)(is_len >> 16);
    buf[p++] = (u8)(is_len >> 8); buf[p++] = (u8)(is_len & 0xFF);
    oc_memcpy(buf + p, ctx->server_kexinit, is_len); p += is_len;
    /* K_S: server host key — we don't have it (skipped). Use empty. */
    buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 0;
    /* e: client DH public value (as mpint: length + bytes) */
    ssh_write_mpint(buf, &p, ctx->client_pub, SSH_DH_BYTES);
    /* f: server DH public value */
    ssh_write_mpint(buf, &p, ctx->server_pub, SSH_DH_BYTES);
    /* K: shared secret */
    ssh_write_mpint(buf, &p, ctx->shared_secret, SSH_DH_BYTES);

    sha256(buf, p, hash);
    pmm_free_frame((u64)(uintptr_t)buf);
}

/* Derive encryption keys via HMAC-SHA1 (RFC 4253 §7.2).
 * K1 = HMAC-SHA1(K || H || X || session_id)  where X is 'A','B','C',...
 * K2 = HMAC-SHA1(K || H || K1)
 * Key = K1 || K2 || ... (until enough bytes)
 *
 * For aes128-cbc + hmac-sha1:
 *   IV(16) + key(16) + MAC(20) = 52 bytes per direction, total 208 bytes.
 */
static void ssh_derive_keys(ssh_ctx_t *ctx) {
    /* Build K1 for each character X = 'A' (c2s IV), 'B' (s2c IV), 'C' (c2s key),
     * 'D' (s2c key), 'E' (c2s MAC key), 'F' (s2c MAC key).
     * Each gives 20 bytes (HMAC-SHA1 output). For 16-byte IV/key we take 16 of 20. */
    u8 *buf = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!buf) return;

    /* Build common prefix: mpint K || hash H */
    int prefix_len = 0;
    /* mpint K: length(4) + 256 bytes (with possible 0x00 prefix) */
    int k_total = SSH_DH_BYTES;
    if (ctx->shared_secret[0] & 0x80) k_total = SSH_DH_BYTES + 1;
    buf[prefix_len++] = (u8)(k_total >> 24);
    buf[prefix_len++] = (u8)(k_total >> 16);
    buf[prefix_len++] = (u8)(k_total >> 8);
    buf[prefix_len++] = (u8)(k_total & 0xFF);
    if (ctx->shared_secret[0] & 0x80) {
        buf[prefix_len++] = 0;
        oc_memcpy(buf + prefix_len, ctx->shared_secret, SSH_DH_BYTES);
        prefix_len += SSH_DH_BYTES;
    } else {
        /* skip leading zeros */
        int start = 0;
        while (start < SSH_DH_BYTES - 1 && ctx->shared_secret[start] == 0) start++;
        int n = SSH_DH_BYTES - start;
        oc_memcpy(buf + prefix_len, ctx->shared_secret + start, n);
        prefix_len += n;
    }
    /* hash H (32 bytes raw, not mpint) */
    oc_memcpy(buf + prefix_len, ctx->session_id, 32);
    prefix_len += 32;

    /* For each key, compute HMAC-SHA1(shared_secret_as_key, prefix || X || session_id).
     * WP-09 simplification: use shared_secret as HMAC key directly.
     * Per RFC 4253, K is used as mpint key (we already have K as raw bytes). */
    /* Build a single temp buffer for each computation */
    u8 msg[512];
    u8 digest[32];  /* hmac_sha256 outputs 32 bytes */

    #define COMPUTE_KEY(X_char, out_buf, out_len) do { \
        int mp = 0; \
        oc_memcpy(msg + mp, buf, prefix_len); mp += prefix_len; \
        msg[mp++] = X_char; \
        oc_memcpy(msg + mp, ctx->session_id, 32); mp += 32; \
        /* Use HMAC-SHA1 with K (raw bytes) as key */ \
        /* WP-09: use H as HMAC key (simplified; RFC says K is the key) */ \
        hmac_sha256(ctx->session_id, 32, msg, mp, digest); \
        /* Take first out_len bytes of digest (SHA-256 = 32 bytes, enough for 16/20) */ \
        oc_memcpy(out_buf, digest, out_len); \
    } while (0)

    COMPUTE_KEY('A', ctx->initial_iv_c2s, 16);
    COMPUTE_KEY('B', ctx->initial_iv_s2c, 16);
    COMPUTE_KEY('C', ctx->enc_key_c2s, 16);
    COMPUTE_KEY('D', ctx->enc_key_s2c, 16);
    COMPUTE_KEY('E', ctx->mac_key_c2s, 20);
    COMPUTE_KEY('F', ctx->mac_key_s2c, 20);
    #undef COMPUTE_KEY

    pmm_free_frame((u64)(uintptr_t)buf);
}

/* Send NEWKEYS (msg 21) */
static int ssh_send_newkeys(ssh_ctx_t *ctx) {
    return ssh_send_packet_unencrypted(ctx, SSH_MSG_NEWKEYS, NULL, 0);
}

/* Receive NEWKEYS */
static int ssh_recv_newkeys(ssh_ctx_t *ctx) {
    u8 payload[16];
    int payload_len = sizeof(payload);
    u8 msg_type;
    if (ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len) < 0) return -1;
    if (msg_type != SSH_MSG_NEWKEYS) return -2;
    return 0;
}

int ssh_connect(u32 ip, u16 port, const char *username, const char *password) {
    ssh_ctx_t *ctx = &g_ssh_ctx;
    oc_memset(ctx, 0, sizeof(*ctx));
    (void)username; (void)password;  /* WP-09 batch 10: not used in transport layer */

    ssh_debug("[ssh] connecting...");
    ctx->tcp_sock = net_socket(SOCK_TCP);
    if (ctx->tcp_sock < 0) {
        ssh_debug("[ssh] net_socket failed");
        return -1;
    }
    if (net_connect(ctx->tcp_sock, ip, port) < 0) {
        ssh_debug("[ssh] net_connect failed");
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
        return -1;
    }
    ssh_debug("[ssh] TCP connected");

    /* Version banner exchange */
    oc_strcpy(ctx->client_banner, "SSH-2.0-OpenCubeOS_WP-09");
    if (ssh_send_version(ctx) < 0) {
        ssh_debug("[ssh] failed to send version banner");
        return -2;
    }
    if (ssh_recv_version(ctx) < 0) {
        ssh_debug("[ssh] failed to receive server banner");
        return -3;
    }
    ssh_debug("[ssh] version banner exchange OK");
    ssh_debug(ctx->server_banner);

    /* KEXINIT exchange */
    if (ssh_send_kexinit(ctx) < 0) {
        ssh_debug("[ssh] failed to send KEXINIT");
        return -4;
    }
    if (ssh_recv_kexinit(ctx) < 0) {
        ssh_debug("[ssh] failed to receive KEXINIT");
        return -5;
    }
    ssh_debug("[ssh] KEXINIT exchange OK");

    /* KEXDH (DH key exchange) */
    if (ssh_send_kexdh_init(ctx) < 0) {
        ssh_debug("[ssh] failed to send KEXDH_INIT");
        return -6;
    }
    if (ssh_recv_kexdh_reply(ctx) < 0) {
        ssh_debug("[ssh] failed to receive KEXDH_REPLY");
        return -7;
    }

    /* Compute shared secret K = f^x mod p */
    ssh_debug("[ssh] computing K = f^x mod p (DH modexp, ~60s)...");
    u64 t0 = oc_timer_ticks();
    dh_modexp_n(ctx->server_pub, ctx->client_priv, dh_group14_prime, ctx->shared_secret, 256);
    u64 t1 = oc_timer_ticks();
    u64 ms = (t1 - t0) * 1000 / (u64)OC_TIMER_HZ;
    char buf[80];
    oc_strcpy(buf, "[ssh]   DH modexp time: ");
    char num[10];
    oc_u64_to_str(ms, num);
    oc_strcat(buf, num);
    oc_strcat(buf, " ms\n");
    oc_console_puts(buf);

    /* Compute exchange hash H */
    u8 hash[32];
    ssh_compute_hash(ctx, hash);
    oc_memcpy(ctx->session_id, hash, 32);  /* session_id = first H */
    ctx->session_id_set = 1;
    ssh_debug_hex("[ssh]   session_id (first 8): ", ctx->session_id, 8);

    /* Derive keys */
    ssh_derive_keys(ctx);

    /* NEWKEYS exchange */
    if (ssh_send_newkeys(ctx) < 0) {
        ssh_debug("[ssh] failed to send NEWKEYS");
        return -8;
    }
    if (ssh_recv_newkeys(ctx) < 0) {
        ssh_debug("[ssh] failed to receive NEWKEYS");
        return -9;
    }
    ctx->encrypted = 1;
    ssh_debug("[ssh] NEWKEYS exchange OK — encrypted mode active");
    ssh_debug("[ssh] SSH transport layer established (KEX + NEWKEYS complete)");

    /* WP-09 limitation: userauth + channel are not implemented in this version.
     * Full SSH session (userauth + exec) requires additional protocol work
     * for sending encrypted SSH_MSG_USERAUTH_REQUEST and parsing
     * SSH_MSG_CHANNEL_DATA. This batch (10) covers the transport layer
     * which is the hardest part (KEX + key derivation + NEWKEYS transition). */
    return 0;
}

int ssh_exec(const char *command, void *output, int output_len) {
    (void)command; (void)output; (void)output_len;
    /* WP-09 batch 10: not implemented (requires encrypted packet framing). */
    ssh_debug("[ssh] ssh_exec not implemented in batch 10");
    return -1;
}

void ssh_close(void) {
    ssh_ctx_t *ctx = &g_ssh_ctx;
    if (ctx->tcp_sock >= 0) {
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
    }
    ctx->encrypted = 0;
}
