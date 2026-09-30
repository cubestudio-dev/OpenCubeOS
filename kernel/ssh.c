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

/* DH group 14 prime now lives in crypto.c (dh_group14_prime, declared in crypto.h). */
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
    /* WP-09 fix: pkt_len must include msg_type byte. Original was 1 byte short,
     * causing server to read 0-byte payload and IndexError. */
    int pad_len = 8 - ((6 + payload_len) % 8);  /* 4(length) + 1(pad_len) + 1(msg_type) + payload */
    if (pad_len < 4) pad_len += 8;
    u8 *pkt = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!pkt) return -1;
    int pkt_len = 1 + 1 + payload_len + pad_len;  /* pad_len_byte(1) + msg_type(1) + payload + pad */
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad_len;
    pkt[5] = msg_type;
    if (payload_len > 0) oc_memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_len);
    int rc = net_send(ctx->tcp_sock, pkt, 4 + pkt_len);
    pmm_free_frame((u64)(uintptr_t)pkt);
    /* WP-09 fix: sequence number MUST advance for EVERY packet, including
     * unencrypted KEX packets (RFC 4253 §6.4). MAC covers seq, so if the
     * 3 KEX-phase packets don't advance write_seq, the first encrypted
     * packet (USERAUTH) is sent with seq=0 while the peer expects 3 ->
     * "Mismatched MAC" on the server. Root cause #2 of K mismatch-era
     * USERAUTH failure (after the group14 prime fix). */
    if (rc >= 0) ctx->write_seq++;
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
    {
        char dbg2[80]; oc_strcpy(dbg2, "[ssh]   recv encrypted: type=");
        char nn[10]; oc_u64_to_str((u64)*msg_type, nn); oc_strcat(dbg2, nn);
        oc_strcat(dbg2, " plen="); oc_u64_to_str((u64)plen, nn); oc_strcat(dbg2, nn);
        oc_strcat(dbg2, "\n"); oc_console_puts(dbg2);
    }
    oc_memcpy(payload, body + 2, plen);  /* skip padding_length + msg_type */
    *payload_len = plen;
    pmm_free_frame((u64)(uintptr_t)body);
    /* WP-09 fix: read_seq must advance for every received packet, same
     * rationale as write_seq above (affects verifying peer MACs later). */
    ctx->read_seq++;
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
    WRITE_STR("hmac-sha2-256");
    WRITE_STR("hmac-sha2-256");
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
    static u8 payload[4096];  /* paramiko KEXINIT can be 800+ bytes */
    int payload_len = sizeof(payload);
    u8 msg_type;
    if (ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len) < 0) return -1;
    if (msg_type != SSH_MSG_KEXINIT) return -2;
    /* WP-09 fix: paramiko's remote_kex_init = cMSG_KEXINIT + m.get_so_far()
     * which INCLUDES the msg_type byte. Both local_kex_init and remote_kex_init
     * include msg_type. We must match this for exchange hash H to be correct. */
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
    /* WP-09 fix: match paramiko's add_mpint() which uses deflate_long(i, formfactor)
     * where formfactor = int(i.bit_length() / 8) + 1. This pads to a minimum size. */
    /* Skip leading zeros */
    int start = 0;
    while (start < val_len - 1 && val[start] == 0) start++;
    int n = val_len - start;
    int need_zero = (val[start] & 0x80) ? 1 : 0;
    int total = n + need_zero;
    /* Compute formfactor = int(bit_length / 8) + 1 */
    int first_byte_bits = 0;
    u8 fb = val[start];
    for (int b = 7; b >= 0; b--) {
        if (fb & (1 << b)) { first_byte_bits = b + 1; break; }
    }
    if (n == 1 && fb == 0) first_byte_bits = 0;
    int bit_length = (n - 1) * 8 + first_byte_bits;
    int formfactor = bit_length / 8 + 1;
    if (formfactor < 1) formfactor = 1;
    /* Use the larger of total or formfactor */
    int final_len = (total > formfactor) ? total : formfactor;
    buf[(*p)++] = (u8)(final_len >> 24);
    buf[(*p)++] = (u8)(final_len >> 16);
    buf[(*p)++] = (u8)(final_len >> 8);
    buf[(*p)++] = (u8)(final_len & 0xFF);
    /* Pad with leading zeros to reach final_len */
    int pad = final_len - total;
    for (int i = 0; i < pad; i++) buf[(*p)++] = 0;
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
    off += 4;
    if (off + ks_len > payload_len) return -3;
    /* WP-09 fix: save K_S (server host key blob) for exchange hash H.
     * Without K_S in the hash, all derived keys are wrong. */
    if (ks_len <= (int)sizeof(ctx->server_host_key)) {
        oc_memcpy(ctx->server_host_key, payload + off, ks_len);
        ctx->server_host_key_len = ks_len;
    }
    off += ks_len;  /* skip K_S */
    if (off + 4 > payload_len) return -4;
    int f_len = (payload[off] << 24) | (payload[off+1] << 16) | (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (off + f_len > payload_len) return -5;
    /* Copy server's f into server_pub (right-aligned, SSH_DH_BYTES=256 bytes).
     * mpint encoding may have a leading 0x00 for sign extension when MSB
     * is set (which it always is for 2048-bit DH values — top byte 0x80+).
     * Typical f_len is 257 (0x00 + 256 bytes). We handle:
     *   - f_len == 256: copy directly
     *   - f_len == 257 (leading 0x00): strip leading byte, copy 256
     *   - f_len < 256: right-align in 256-byte buffer (rare for DH)
     *   - f_len > 257: take low 256 bytes (drop high padding) */
    oc_memset(ctx->server_pub, 0, SSH_DH_BYTES);
    int data_off = off;
    int copy_len = f_len;
    /* Strip leading 0x00 (mpint sign extension byte) */
    if (copy_len > 0 && payload[data_off] == 0) {
        data_off++;
        copy_len--;
    }
    if (copy_len > SSH_DH_BYTES) {
        /* Take low SSH_DH_BYTES bytes */
        data_off += (copy_len - SSH_DH_BYTES);
        copy_len = SSH_DH_BYTES;
    }
    /* Right-align in SSH_DH_BYTES buffer */
    oc_memcpy(ctx->server_pub + (SSH_DH_BYTES - copy_len), payload + data_off, copy_len);
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
    /* K_S: server host key blob — saved from KEXDH_REPLY */
    int ks_total = ctx->server_host_key_len;
    buf[p++] = (u8)(ks_total >> 24); buf[p++] = (u8)(ks_total >> 16);
    buf[p++] = (u8)(ks_total >> 8); buf[p++] = (u8)(ks_total & 0xFF);
    if (ks_total > 0 && p + ks_total < 3500) {
        oc_memcpy(buf + p, ctx->server_host_key, ks_total);
        p += ks_total;
    }
    /* e: client DH public value (as mpint: length + bytes) */
    ssh_write_mpint(buf, &p, ctx->client_pub, SSH_DH_BYTES);
    /* f: server DH public value */
    ssh_write_mpint(buf, &p, ctx->server_pub, SSH_DH_BYTES);
    /* K: shared secret */
    ssh_write_mpint(buf, &p, ctx->shared_secret, SSH_DH_BYTES);

    sha256(buf, p, hash);
    pmm_free_frame((u64)(uintptr_t)buf);

    /* WP-09 debug: print hash input components for comparison with server */
    {
        char dbg[160];
        oc_strcpy(dbg, "[ssh] H input: V_C=");
        char n[10];
        int vc_len = (int)oc_strlen(ctx->client_banner);
        oc_u64_to_str((u64)vc_len, n); oc_strcat(dbg, n);
        oc_strcat(dbg, " V_S=");
        int vs_len = (int)oc_strlen(ctx->server_banner);
        oc_u64_to_str((u64)vs_len, n); oc_strcat(dbg, n);
        oc_strcat(dbg, " I_C=");
        oc_u64_to_str((u64)ctx->client_kexinit_len, n); oc_strcat(dbg, n);
        oc_strcat(dbg, " I_S=");
        oc_u64_to_str((u64)ctx->server_kexinit_len, n); oc_strcat(dbg, n);
        oc_strcat(dbg, " K_S=");
        oc_u64_to_str((u64)ctx->server_host_key_len, n); oc_strcat(dbg, n);
        oc_strcat(dbg, "\n"); oc_console_puts(dbg);
    }
    ssh_debug_hex("[ssh] I_C[0..15]: ", ctx->client_kexinit, 16);
    ssh_debug_hex("[ssh] I_S[0..15]: ", ctx->server_kexinit, 16);
    ssh_debug_hex("[ssh] H (first 16): ", hash, 16);

    /* WP-09 debug: print K and H for comparison with server.
     * (Derived keys are printed after ssh_derive_keys(), see below.) */
    ssh_debug_hex("[ssh] K (first 8): ", ctx->shared_secret, 8);
}

/* Derive encryption keys via plain SHA-256 (paramiko's _compute_key algorithm,
 * RFC 4253 §7.2 alternative form).
 * K1 = SHA-256(K_mpint || H || X || session_id)  → 32 bytes
 * K2 = SHA-256(K_mpint || H || K1)  → 32 bytes (only if K1 isn't enough)
 * Key = K1 || K2 || ... (until enough bytes)
 *
 * For aes128-cbc + hmac-sha2-256, each key is 16/16/32 bytes — K1 (32) is
 * enough for one key (no K2 needed).
 *
 * K_mpint: K encoded as mpint (4-byte length + bytes, with leading 0x00 if
 * MSB is set for sign extension).
 */
static void ssh_derive_keys(ssh_ctx_t *ctx) {
    /* Build K_mpint: strip leading zeros, add 0x00 if MSB set (sign extension).
     * This matches paramiko's add_mpint() which uses deflate_long(). */
    int start = 0;
    while (start < SSH_DH_BYTES - 1 && ctx->shared_secret[start] == 0) start++;
    int n = SSH_DH_BYTES - start;
    int need_zero = (ctx->shared_secret[start] & 0x80) ? 1 : 0;
    int k_total = n + need_zero;

    u8 k_mpint[264];
    k_mpint[0] = (u8)(k_total >> 24);
    k_mpint[1] = (u8)(k_total >> 16);
    k_mpint[2] = (u8)(k_total >> 8);
    k_mpint[3] = (u8)(k_total & 0xFF);
    if (need_zero) k_mpint[4] = 0;
    oc_memcpy(k_mpint + 4 + need_zero, ctx->shared_secret + start, n);
    int k_mpint_len = 4 + k_total;

    /* For each key, compute K1 = SHA-256(K_mpint || H(32) || X(1) || session_id(32)) */
    u8 msg[512];
    u8 digest[32];

    #define COMPUTE_KEY(X_char, out_buf, out_len) do { \
        int mp = 0; \
        oc_memcpy(msg + mp, k_mpint, k_mpint_len); mp += k_mpint_len; \
        oc_memcpy(msg + mp, ctx->session_id, 32); mp += 32;  /* H */ \
        msg[mp++] = X_char;  /* X */ \
        oc_memcpy(msg + mp, ctx->session_id, 32); mp += 32;  /* session_id */ \
        sha256(msg, mp, digest); \
        oc_memcpy(out_buf, digest, out_len); \
    } while (0)

    COMPUTE_KEY('A', ctx->initial_iv_c2s, 16);
    COMPUTE_KEY('B', ctx->initial_iv_s2c, 16);
    COMPUTE_KEY('C', ctx->enc_key_c2s, 16);
    COMPUTE_KEY('D', ctx->enc_key_s2c, 16);
    COMPUTE_KEY('E', ctx->mac_key_c2s, 32);
    COMPUTE_KEY('F', ctx->mac_key_s2c, 32);
    #undef COMPUTE_KEY
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
    /* Save username/password for ssh_exec's USERAUTH step */
    if (username) oc_strncpy(ctx->username, username, sizeof(ctx->username)-1);
    if (password) oc_strncpy(ctx->password, password, sizeof(ctx->password)-1);

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
    /* WP-09 debug: now print the real derived key material. */
    ssh_debug_hex("[ssh] enc_key_c2s (first 8): ", ctx->enc_key_c2s, 8);
    ssh_debug_hex("[ssh] iv_c2s (first 8): ", ctx->initial_iv_c2s, 8);
    /* WP-09 fix: initialize the rolling CBC IVs (first packet uses the
     * initial IV; every subsequent packet uses the last ciphertext block
     * of the previous packet, RFC 4253 §6.3). */
    oc_memcpy(ctx->iv_c2s_next, ctx->initial_iv_c2s, 16);
    oc_memcpy(ctx->iv_s2c_next, ctx->initial_iv_s2c, 16);

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

    /* WP-09 batch 13: Send USERAUTH_REQUEST (password) */
    /* USERAUTH_REQUEST format:
     *   byte SSH_MSG_USERAUTH_REQUEST (50)
     *   string user
     *   string service ("ssh-connection")
     *   string method ("password")
     *   byte FALSE (0)
     *   string password
     */
    {
        u8 payload[256];
        int p = 0;
        /* msg_type is added by ssh_send_packet_encrypted, NOT in payload */
        /* user name */
        int ulen = (int)oc_strlen(ctx->username);
        payload[p++] = (u8)(ulen >> 24); payload[p++] = (u8)(ulen >> 16);
        payload[p++] = (u8)(ulen >> 8); payload[p++] = (u8)(ulen & 0xFF);
        for (int i = 0; i < ulen; i++) payload[p++] = ctx->username[i];
        /* service = "ssh-connection" (14 bytes) */
        const char *svc = "ssh-connection";
        int slen = 14;
        payload[p++] = (u8)(slen >> 24); payload[p++] = (u8)(slen >> 16);
        payload[p++] = (u8)(slen >> 8); payload[p++] = (u8)(slen & 0xFF);
        for (int i = 0; i < slen; i++) payload[p++] = svc[i];
        /* method = "password" (8 bytes) */
        const char *mth = "password";
        int mlen = 8;
        payload[p++] = (u8)(mlen >> 24); payload[p++] = (u8)(mlen >> 16);
        payload[p++] = (u8)(mlen >> 8); payload[p++] = (u8)(mlen & 0xFF);
        for (int i = 0; i < mlen; i++) payload[p++] = mth[i];
        /* FALSE (no old password) */
        payload[p++] = 0;
        /* password */
        int plen = (int)oc_strlen(ctx->password);
        payload[p++] = (u8)(plen >> 24); payload[p++] = (u8)(plen >> 16);
        payload[p++] = (u8)(plen >> 8); payload[p++] = (u8)(plen & 0xFF);
        for (int i = 0; i < plen; i++) payload[p++] = ctx->password[i];

        extern int ssh_send_packet_encrypted(ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len);
        if (ssh_send_packet_encrypted(ctx, SSH_MSG_USERAUTH_REQ, payload, p) < 0) {
            ssh_debug("[ssh] failed to send USERAUTH_REQUEST");
            return -10;
        }
        ssh_debug("[ssh] sent USERAUTH_REQUEST (password)");

        /* Read response: USERAUTH_SUCCESS (52) or USERAUTH_FAILURE (53) */
        u8 rtype;
        u8 rbuf[256];
        int rlen = sizeof(rbuf);
        extern int ssh_recv_packet_encrypted(ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len);
        if (ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            ssh_debug("[ssh] failed to receive USERAUTH response");
            return -11;
        }
        if (rtype == SSH_MSG_USERAUTH_SUCCESS) {
            ssh_debug("[ssh] USERAUTH_SUCCESS — authenticated");
        } else if (rtype == SSH_MSG_USERAUTH_FAILURE) {
            ssh_debug("[ssh] USERAUTH_FAILURE — wrong password");
            return -12;
        } else {
            char b[60]; oc_strcpy(b, "[ssh] unexpected msg type ");
            char num2[10]; oc_u64_to_str((u64)rtype, num2);
            oc_strcat(b, num2); oc_strcat(b, "\n");
            oc_console_puts(b);
            return -13;
        }
    }

    return 0;
}

/* ============================================================
 * Encrypted packet framing (after NEWKEYS)
 *
 * Outgoing packet layout (RFC 4253 §6, aes128-cbc + hmac-sha2-256):
 *   uint32 packet_length (NOT encrypted; = 1 + payload_len + padding_count)
 *   byte   padding_length (encrypted)
 *   byte[] payload (encrypted)
 *   byte[] random_padding (encrypted)
 *   byte[] MAC = HMAC-SHA256(mac_key, seq(4) || unencrypted_header(4) || encrypted_body)
 *
 * Notes:
 *   - packet_length + MAC_total = 16-byte aligned (block_size + mac_size)
 *   - IV is initial_iv_c2s for first packet, then for CBC it chains
 *     (last encrypted block of previous packet = IV for next)
 * ============================================================ */

int ssh_send_packet_encrypted(ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    /* WP-09 SSH encrypted packet format (RFC 4253 §6, post-NEWKEYS):
     * The ENTIRE packet (including the 4-byte packet_length field) is encrypted.
     * Layout: encrypted_block_chain (length(4) + pad_len(1) + msg_type(1) + payload + pad)
     *         + MAC(32) [computed over unencrypted seq + unencrypted header+body]
     *
     * Paramiko's check: (packet_length - 12) % block_size == 0
     *   where 12 = bytes 5-16 of the first 16-byte block (after the 4-byte length).
     * This means packet_length % 16 == 12, so (4 + packet_length) % 16 == 0.
     */
    int block_size = 16;
    int mac_size = 32;
    int min_pad = 4;
    int needed = 4 + 1 + 1 + payload_len;  /* length(4) + pad_len(1) + msg_type(1) + payload */
    int pad_count = block_size - (needed % block_size);
    if (pad_count < min_pad) pad_count += block_size;
    int packet_length = 1 + 1 + payload_len + pad_count;  /* excludes 4-byte length field */
    int total_unenc = 4 + packet_length;  /* length(4) + body */
    int total_send = total_unenc + mac_size;

    static u8 pkt[16384];
    if (total_send > (int)sizeof(pkt)) return -1;

    /* Build unencrypted: length(4) + pad_len(1) + msg_type(1) + payload + padding */
    pkt[0] = (u8)(packet_length >> 24);
    pkt[1] = (u8)(packet_length >> 16);
    pkt[2] = (u8)(packet_length >> 8);
    pkt[3] = (u8)(packet_length & 0xFF);
    pkt[4] = (u8)pad_count;
    pkt[5] = msg_type;
    if (payload_len > 0) oc_memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_count);

    /* Compute MAC over: seq(4) || unencrypted (total_unenc bytes) */
    static u8 mac_input[8192];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->write_seq >> 24);
    mac_input[mi++] = (u8)(ctx->write_seq >> 16);
    mac_input[mi++] = (u8)(ctx->write_seq >> 8);
    mac_input[mi++] = (u8)(ctx->write_seq & 0xFF);
    if (total_unenc > (int)sizeof(mac_input) - 4) return -1;
    oc_memcpy(mac_input + mi, pkt, total_unenc); mi += total_unenc;
    u8 mac[32];
    hmac_sha256(ctx->mac_key_c2s, 32, mac_input, mi, mac);

    /* Encrypt the ENTIRE unencrypted packet (including 4-byte length field).
     * RFC 4253 §6: "Once a party has sent SSH_MSG_NEWKEYS, all subsequent
     * data MUST be encrypted, including the length field."
     *
     * WP-09 note: paramiko reads 16 bytes (block_size) at a time. The first
     * block includes the 4-byte length + 12 bytes of body. After decryption,
     * paramiko checks (packet_length - 12) % 16 == 0, which means
     * (4 + packet_length) must be a multiple of 16. */
    static u8 enc[16384];
    if (total_unenc > (int)sizeof(enc)) return -1;
    aes128_cbc_encrypt(ctx->enc_key_c2s, ctx->iv_c2s_next, pkt, total_unenc, enc);
    /* WP-09 fix: CBC chaining — the IV for the next outgoing packet is the
     * last ciphertext block of this one. */
    oc_memcpy(ctx->iv_c2s_next, enc + total_unenc - 16, 16);

    /* Reassemble: encrypted (total_unenc bytes) + MAC (32 bytes) */
    static u8 out[16384];
    if (total_send > (int)sizeof(out)) return -1;
    oc_memcpy(out, enc, total_unenc);
    oc_memcpy(out + total_unenc, mac, mac_size);

    int rc = net_send(ctx->tcp_sock, out, total_send);
    if (rc < 0) {
        ssh_debug("[ssh] net_send failed in ssh_send_packet_encrypted");
    }
    ctx->write_seq++;
    return rc;
}

int ssh_recv_packet_encrypted(ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    /* WP-09 SSH encrypted packet receive (RFC 4253 §6, post-NEWKEYS):
     * The ENTIRE packet is encrypted, including the 4-byte packet_length field.
     * Algorithm:
     *   1. Read block_size (16) bytes → first encrypted block
     *   2. Decrypt → first 4 bytes = packet_length, remaining 12 bytes = part of body
     *   3. Read (packet_length - 12 + mac_size) more bytes
     *   4. Decrypt remaining body
     *   5. Verify MAC
     *   6. Extract msg_type + payload (skip padding_length + padding)
     */
    int block_size = 16;
    int mac_size = 32;

    /* Read first 16 bytes (encrypted) */
    u8 first_block[16];
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < block_size) {
        int got = net_recv(ctx->tcp_sock, first_block + n, block_size - n);
        if (got > 0) { n += got; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 800) return -1;
        }
    }

    /* Decrypt first block to get packet_length */
    u8 dec_first[16];
    /* WP-09 fix: use the rolling IV (equals the last ciphertext block of the
     * previous packet), NOT the fixed initial IV. */
    aes128_cbc_decrypt(ctx->enc_key_s2c, ctx->iv_s2c_next, first_block, block_size, dec_first);
    int packet_length = ((int)dec_first[0] << 24) | ((int)dec_first[1] << 16) |
                        ((int)dec_first[2] << 8) | dec_first[3];
    if (packet_length < 1 || packet_length > 35000) {
        ssh_debug("[ssh] invalid packet_length in encrypted packet");
        return -1;
    }

    /* Read remaining encrypted body + MAC */
    int leftover = block_size - 4;  /* 12 bytes already in dec_first */
    int remaining = packet_length - leftover;  /* bytes to read more */
    if (remaining < 0 || remaining % block_size != 0) {
        ssh_debug("[ssh] Invalid packet blocking (from server)");
        return -1;
    }

    static u8 rest_buf[16384];
    n = 0;
    while (n < remaining + mac_size) {
        int r = net_recv(ctx->tcp_sock, rest_buf + n, (remaining + mac_size) - n);
        if (r > 0) { n += r; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 800) return -1;
        }
    }

    /* Decrypt remaining body (encrypted part, not MAC) */
    static u8 dec_rest[16384];
    if (remaining > 0) {
        /* WP-09 fix: the IV for the rest of THIS packet is the first
         * ciphertext block we just read (first_block) — CBC chains block to
         * block within the packet too. */
        aes128_cbc_decrypt(ctx->enc_key_s2c, first_block, rest_buf, remaining, dec_rest);
    }
    /* WP-09 fix: roll the incoming IV = last ciphertext block of this packet
     * (rest_buf's last block if any, else first_block itself). */
    if (remaining >= 16) {
        oc_memcpy(ctx->iv_s2c_next, rest_buf + remaining - 16, 16);
    } else {
        oc_memcpy(ctx->iv_s2c_next, first_block, 16);
    }

    /* Combine decrypted body: dec_first[4..15] + dec_rest[0..remaining-1] */
    static u8 body[16384];
    int body_len = leftover + remaining;  /* = packet_length */
    if (body_len > (int)sizeof(body)) return -1;
    oc_memcpy(body, dec_first + 4, leftover);  /* first 12 decrypted body bytes */
    if (remaining > 0) oc_memcpy(body + leftover, dec_rest, remaining);

    /* Verify MAC: HMAC-SHA256(mac_key_s2c, seq(4) + unencrypted_packet(total_unenc)) */
    /* unencrypted_packet = length(4) + body(packet_length) = 4 + packet_length */
    /* We don't have the unencrypted length bytes directly — but we know packet_length,
     * so we reconstruct: seq(4) + packet_length(4 BE) + body(packet_length bytes) */
    static u8 mac_input[16384];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->read_seq >> 24);
    mac_input[mi++] = (u8)(ctx->read_seq >> 16);
    mac_input[mi++] = (u8)(ctx->read_seq >> 8);
    mac_input[mi++] = (u8)(ctx->read_seq & 0xFF);
    /* Reconstruct unencrypted packet: length(4) + body */
    mac_input[mi++] = (u8)(packet_length >> 24);
    mac_input[mi++] = (u8)(packet_length >> 16);
    mac_input[mi++] = (u8)(packet_length >> 8);
    mac_input[mi++] = (u8)(packet_length & 0xFF);
    oc_memcpy(mac_input + mi, body, body_len); mi += body_len;
    u8 expected_mac[32];
    hmac_sha256(ctx->mac_key_s2c, 32, mac_input, mi, expected_mac);
    int mac_ok = 1;
    for (int i = 0; i < mac_size; i++) {
        if (rest_buf[remaining + i] != expected_mac[i]) { mac_ok = 0; break; }
    }
    if (!mac_ok) {
        ssh_debug("[ssh] MAC verify FAIL on incoming encrypted packet");
        return -1;
    }

    /* Extract msg_type + payload from body.
     * body[0] = padding_length, body[1] = msg_type, body[2..] = payload, body[..] = padding */
    int pad_len = body[0];
    *msg_type = body[1];
    int plen = body_len - 1 - pad_len - 1;  /* padding_length byte + msg_type + padding */
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    {
        char dbg2[80]; oc_strcpy(dbg2, "[ssh]   recv encrypted: type=");
        char nn[10]; oc_u64_to_str((u64)*msg_type, nn); oc_strcat(dbg2, nn);
        oc_strcat(dbg2, " plen="); oc_u64_to_str((u64)plen, nn); oc_strcat(dbg2, nn);
        oc_strcat(dbg2, "\n"); oc_console_puts(dbg2);
    }
    oc_memcpy(payload, body + 2, plen);  /* skip padding_length + msg_type */
    *payload_len = plen;

    ctx->read_seq++;
    return 0;
}

/* ssh_exec: open session channel, send exec request, read output. */
int ssh_exec(const char *command, void *output, int output_len) {
    ssh_ctx_t *ctx = &g_ssh_ctx;
    if (!ctx->encrypted) {
        ssh_debug("[ssh] ssh_exec: not encrypted (transport not established)");
        return -1;
    }

    /* SSH_MSG_CHANNEL_OPEN (90) — session channel
     * Format: byte(90) + string("session") + u32(sender_channel) + u32(window_size) + u32(max_packet_size) */
    {
        u8 payload[64];
        int p = 0;
        /* msg_type added by ssh_send_packet_encrypted */
        const char *ctype = "session";
        int clen = 7;
        payload[p++] = (u8)(clen >> 24); payload[p++] = (u8)(clen >> 16);
        payload[p++] = (u8)(clen >> 8); payload[p++] = (u8)(clen & 0xFF);
        for (int i = 0; i < clen; i++) payload[p++] = ctype[i];
        /* sender_channel = 0 (our channel id) */
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 0;
        /* window_size = 65536 */
        payload[p++] = 0; payload[p++] = 1; payload[p++] = 0; payload[p++] = 0;
        /* max_packet_size = 16384 */
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0x40; payload[p++] = 0;

        if (ssh_send_packet_encrypted(ctx, SSH_MSG_CHANNEL_OPEN, payload, p) < 0) {
            ssh_debug("[ssh] failed to send CHANNEL_OPEN");
            return -2;
        }
        ssh_debug("[ssh] sent CHANNEL_OPEN (session)");
    }

    /* Read CHANNEL_OPEN_CONFIRMATION (91) or CHANNEL_OPEN_FAILURE (92) */
    {
        u8 rtype;
        u8 rbuf[256];
        int rlen = sizeof(rbuf);
        if (ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            ssh_debug("[ssh] failed to receive CHANNEL_OPEN_CONFIRMATION");
            return -3;
        }
        if (rtype != SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            ssh_debug("[ssh] CHANNEL_OPEN failed");
            return -4;
        }
        /* Parse: sender_channel(4) + recipient_channel(4) + window(4) + max_packet(4) */
        if (rlen >= 8) {
            /* rbuf starts after msg_type — actually our recv_packet_encrypted
             * strips msg_type, so rbuf[0..3] = sender_channel (server's channel) */
            ctx->server_channel_id = ((u32)rbuf[0] << 24) | ((u32)rbuf[1] << 16) |
                                     ((u32)rbuf[2] << 8) | rbuf[3];
        }
        ssh_debug("[ssh] got CHANNEL_OPEN_CONFIRMATION");
    }

    /* SSH_MSG_CHANNEL_REQUEST (98) — exec
     * Format: byte(98) + u32(recipient_channel) + string("exec") + byte(want_reply) + string(command) */
    {
        u8 payload[512];
        int p = 0;
        /* recipient_channel = server_channel_id */
        payload[p++] = (u8)(ctx->server_channel_id >> 24);
        payload[p++] = (u8)(ctx->server_channel_id >> 16);
        payload[p++] = (u8)(ctx->server_channel_id >> 8);
        payload[p++] = (u8)(ctx->server_channel_id & 0xFF);
        /* request type = "exec" */
        const char *req = "exec";
        int reqlen = 4;
        payload[p++] = (u8)(reqlen >> 24); payload[p++] = (u8)(reqlen >> 16);
        payload[p++] = (u8)(reqlen >> 8); payload[p++] = (u8)(reqlen & 0xFF);
        for (int i = 0; i < reqlen; i++) payload[p++] = req[i];
        /* want_reply = TRUE */
        payload[p++] = 1;
        /* command string */
        int clen = (int)oc_strlen(command);
        payload[p++] = (u8)(clen >> 24); payload[p++] = (u8)(clen >> 16);
        payload[p++] = (u8)(clen >> 8); payload[p++] = (u8)(clen & 0xFF);
        for (int i = 0; i < clen; i++) payload[p++] = command[i];

        if (ssh_send_packet_encrypted(ctx, SSH_MSG_CHANNEL_REQUEST, payload, p) < 0) {
            ssh_debug("[ssh] failed to send CHANNEL_REQUEST exec");
            return -5;
        }
        ssh_debug("[ssh] sent CHANNEL_REQUEST (exec)");
    }

    /* Read CHANNEL_DATA (94) records until EOF/CLOSE */
    int total = 0;
    u8 *out = (u8 *)output;
    while (total < output_len) {
        u8 rtype;
        u8 rbuf[4096];
        int rlen = sizeof(rbuf);
        if (ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            ssh_debug("[ssh] timeout reading channel data");
            break;
        }
        if (rtype == SSH_MSG_CHANNEL_DATA) {
            /* WP-09 fix: CHANNEL_DATA payload = u32 recipient_channel +
             * string(data) = u32 chan + u32 data_len + data bytes.
             * The old code copied from rbuf+4, which copied the 4-byte
             * data_len prefix into the output — a leading NUL made the
             * shell print an empty string even though data arrived. */
            if (rlen >= 8) {
                u32 dlen = ((u32)rbuf[4] << 24) | ((u32)rbuf[5] << 16) |
                           ((u32)rbuf[6] << 8) | (u32)rbuf[7];
                if (dlen > (u32)(rlen - 8)) dlen = (u32)(rlen - 8);
                int copy = ((int)dlen > output_len - total) ? (output_len - total) : (int)dlen;
                if (copy > 0) {
                    oc_memcpy(out + total, rbuf + 8, copy);
                    total += copy;
                }
            }
        } else if (rtype == SSH_MSG_CHANNEL_EOF) {
            ssh_debug("[ssh] got CHANNEL_EOF");
            break;
        } else if (rtype == SSH_MSG_CHANNEL_CLOSE) {
            ssh_debug("[ssh] got CHANNEL_CLOSE");
            break;
        } else {
            /* Ignore other messages (CHANNEL_REQUEST success, WINDOW_ADJUST, etc.) */
            char dbg[64]; oc_strcpy(dbg, "[ssh]   (exec loop) ignored msg type ");
            char nn[10]; oc_u64_to_str((u64)rtype, nn); oc_strcat(dbg, nn); oc_strcat(dbg, "\n");
            oc_console_puts(dbg);
        }
    }
    ssh_debug("[ssh] exec complete");
    return total;
}

void ssh_close(void) {
    ssh_ctx_t *ctx = &g_ssh_ctx;
    if (ctx->tcp_sock >= 0) {
        net_close(ctx->tcp_sock);
        ctx->tcp_sock = -1;
    }
    ctx->encrypted = 0;
}
