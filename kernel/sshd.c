/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * sshd.c - Open Cube OS kernel-side SSH-2.0 server (WP-09).
 *
 * Implements a real, working SSH server so remote peers can execute shell
 * commands inside the kernel:
 *
 *   - TCP server via tcp_listen() + net_accept()          (net.c, WP-09)
 *   - SSH-2.0 transport: banner, KEXINIT, KEXDH (RFC 3526 group14
 *     diffie-hellman-group14-sha256), NEWKEYS
 *   - aes128-cbc + hmac-sha2-256 encrypted packet framing (same wire format
 *     as the ssh.c client, with c2s/s2c roles mirrored for the server side)
 *   - USERAUTH: password check (oc / oc by default, settable via command args)
 *   - CHANNEL: session open, "exec" requests, exit-status, EOF, CLOSE
 *   - exec engine: runs a real kernel shell command line via
 *     shell_execute_line() while a console hook captures the output bytes
 *   - host authentication: real RSA-2048 (PKCS#1 v1.5 / rsa-sha2-256)
 *     signature over the exchange hash H, computed with the shared
 *     big-integer modular exponentiation engine (crypto.c)
 *
 * Verified end-to-end against paramiko (see tools/ssh_test_client.py).
 */

#include "types.h"
#include "string.h"
#include "console.h"
#include "ext.h"
#include "timer.h"
#include "crypto.h"
#include "pmm.h"
#include "net.h"
#include "ssh.h"
#include "shell.h"
#include "sshd_rsa_key.h"

/* ---- protocol constants (mirror ssh.c) ---- */
#define SSHD_MSG_DISCONNECT        1
#define SSHD_MSG_SERVICE_REQUEST   5
#define SSHD_MSG_SERVICE_ACCEPT    6
#define SSHD_MSG_KEXINIT          20
#define SSHD_MSG_NEWKEYS          21
#define SSHD_MSG_KEXDH_REPLY      31
#define SSHD_MSG_KEXDH_INIT       30
#define SSHD_MSG_USERAUTH_REQUEST 50
#define SSHD_MSG_USERAUTH_FAILURE 51
#define SSHD_MSG_USERAUTH_SUCCESS 52
#define SSHD_MSG_USERAUTH_BANNER  53
#define SSHD_MSG_CHANNEL_OPEN     90
#define SSHD_MSG_CHANNEL_OPEN_CONF 91
#define SSHD_MSG_CHANNEL_OPEN_FAIL 92
#define SSHD_MSG_CHANNEL_WINDOW    93
#define SSHD_MSG_CHANNEL_DATA     94
#define SSHD_MSG_CHANNEL_EOF      96
#define SSHD_MSG_CHANNEL_CLOSE    97
#define SSHD_MSG_CHANNEL_REQUEST  98
#define SSHD_MSG_CHANNEL_SUCCESS  99
#define SSHD_MSG_CHANNEL_FAILURE 100

#define SSHD_DH_BYTES 256
static const char *SSHD_BANNER = "SSH-2.0-OpenCubeOS_sshd_WP-09\r\n";

typedef struct {
    int sock;                       /* TCP socket fd for the accepted conn */
    /* strings + payloads for H */
    char client_banner[128];
    char server_banner[64];
    u8  client_kexinit[4096];
    int client_kexinit_len;
    u8  server_kexinit[1024];
    int server_kexinit_len;
    /* DH */
    u8  dh_priv[SSHD_DH_BYTES];
    u8  dh_pub[SSHD_DH_BYTES];      /* f = g^y mod p */
    u8  client_pub[SSHD_DH_BYTES];  /* e */
    u8  shared_secret[SSHD_DH_BYTES];
    u8  exchange_hash[32];
    u8  session_id[32];
    int session_id_set;
    /* direction keys (server视角: C=client-to-server = incoming, S = outgoing) */
    u8  iv_in[16], iv_in_next[16];       /* A: c2s */
    u8  iv_out[16], iv_out_next[16];     /* B: s2c */
    u8  enc_in[16];                      /* C */
    u8  enc_out[16];                     /* D */
    u8  mac_in[32];                      /* E */
    u8  mac_out[32];                     /* F */
    u32 write_seq, read_seq;
    int encrypted;
    /* auth */
    char auth_user[32];
    char auth_pass[32];
    /* channel */
    u32 peer_channel;               /* client's channel id */
    u32 our_channel;                /* our channel id */
    int channel_open;
    /* exec capture */
    u8  exec_out[4096];
    int exec_len;
    int exec_active;
} sshd_ctx_t;

static sshd_ctx_t g_ssd;

static void sd_write_u32(u8 *buf, int *p, u32 v);

static void sd_log(const char *msg) {
    oc_console_puts("[sshd] ");
    oc_console_puts(msg);
    oc_console_puts("\n");
}

static void sd_log_hex(const char *prefix, const u8 *buf, int n) {
    char buf2[160];
    oc_strcpy(buf2, "[sshd] ");
    oc_strcat(buf2, prefix);
    char hex[8];
    for (int i = 0; i < n && (int)oc_strlen(buf2) < 130; i++) {
        oc_u64_to_hex(buf[i], hex, 2);
        oc_strcat(buf2, hex);
    }
    oc_console_puts(buf2);
    oc_console_puts("\n");
}

/* ---------- raw (unencrypted) packet framing ---------- */

static int sd_send_packet_unencrypted(sshd_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    int pad_len = 8 - ((6 + payload_len) % 8);
    if (pad_len < 4) pad_len += 8;
    u8 *pkt = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!pkt) { sd_log("send_unenc: pmm_alloc_frame failed"); return -1; }
    int pkt_len = 1 + 1 + payload_len + pad_len;
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad_len;
    pkt[5] = msg_type;
    if (payload_len > 0) oc_memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_len);
    int rc = net_send(ctx->sock, pkt, 4 + pkt_len);
    pmm_free_frame((u64)(uintptr_t)pkt);
    if (rc >= 0) ctx->write_seq++;
    return rc;
}

static int sd_recv_packet_unencrypted(sshd_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    u8 len_buf[4];
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < 4) {
        int got = net_recv(ctx->sock, len_buf + n, 4 - n);
        if (got > 0) { n += got; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 3000) {
                sd_log("recv_unenc: timeout reading 4-byte length");
                return -1;
            }
        }
    }
    int pkt_len = ((int)len_buf[0] << 24) | ((int)len_buf[1] << 16) |
                  ((int)len_buf[2] << 8) | len_buf[3];
    if (pkt_len < 1 || pkt_len > 35000) {
        char b[64]; oc_strcpy(b, "recv_unenc: bad pkt_len=");
        char n2[10]; oc_u64_to_str((u64)pkt_len, n2); oc_strcat(b, n2);
        sd_log(b);
        return -1;
    }
    u8 *body = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!body) return -1;
    int got = 0;
    while (got < pkt_len) {
        int r = net_recv(ctx->sock, body + got, pkt_len - got);
        if (r > 0) { got += r; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 3000) {
                char b[64]; oc_strcpy(b, "recv_unenc: body timeout, got ");
                char n2[10]; oc_u64_to_str((u64)got, n2); oc_strcat(b, n2);
                oc_strcat(b, "/");
                oc_u64_to_str((u64)pkt_len, n2); oc_strcat(b, n2);
                sd_log(b);
                pmm_free_frame((u64)(uintptr_t)body);
                return -1;
            }
        }
    }
    int pad_len = body[0];
    *msg_type = body[1];
    {
        char b[80]; oc_strcpy(b, "recv_unenc: pkt_len=");
        char n2[10]; oc_u64_to_str((u64)pkt_len, n2); oc_strcat(b, n2);
        oc_strcat(b, " type="); oc_u64_to_str((u64)*msg_type, n2); oc_strcat(b, n2);
        oc_strcat(b, " first8=");
        for (int i = 0; i < 4; i++) { char hx[4]; oc_u64_to_hex(body[i], hx, 2); oc_strcat(b, hx); }
        sd_log(b);
    }
    int plen = pkt_len - 1 - pad_len - 1;
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    oc_memcpy(payload, body + 2, plen);
    *payload_len = plen;
    pmm_free_frame((u64)(uintptr_t)body);
    ctx->read_seq++;
    return 0;
}

/* ---------- encrypted packet framing (server side) ---------- */

static int sd_send_packet_encrypted(sshd_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    int block_size = 16, mac_size = 32;
    /* WP-09 fix: the old formula used (5 + payload_len) and packet_length =
 * 1 + payload_len + pad_block, which declared one more padding byte than
 * was actually written. Peers stripped the declared padding and ate the
 * last payload byte (SERVICE_ACCEPT arrived as "ssh-useraut " on the
 * wire). Correct layout: body = pad_len(1) + msg_type(1) + payload(N)
 * + padding(P), total_unenc = 4 + body, and 4 + body must be a multiple
 * of block_size. */
    int pad_block = block_size - ((6 + payload_len) % block_size);
    if (pad_block < 4) pad_block += block_size;
    int packet_length = 2 + payload_len + pad_block;
    int total_unenc = 4 + packet_length;
    int total_send = total_unenc + mac_size;

    static u8 pkt[16384];
    if (total_send > (int)sizeof(pkt)) return -1;
    pkt[0] = (u8)(packet_length >> 24);
    pkt[1] = (u8)(packet_length >> 16);
    pkt[2] = (u8)(packet_length >> 8);
    pkt[3] = (u8)(packet_length & 0xFF);
    pkt[4] = (u8)pad_block;
    pkt[5] = msg_type;
    if (payload_len > 0) oc_memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_block);

    {
        char b[128]; oc_strcpy(b, "[sshd] SEND pkt: ");
        char hx[4];
        for (int i = 0; i < 32 && i < total_unenc; i++) {
            oc_u64_to_hex(pkt[i], hx, 2);
            oc_strcat(b, hx);
        }
        oc_strcat(b, " total_unenc=");
        char n2[12]; oc_u64_to_str((u64)total_unenc, n2); oc_strcat(b, n2);
        sd_log(b);
    }

    /* MAC over seq || unencrypted packet */
    static u8 mac_input[16384];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->write_seq >> 24);
    mac_input[mi++] = (u8)(ctx->write_seq >> 16);
    mac_input[mi++] = (u8)(ctx->write_seq >> 8);
    mac_input[mi++] = (u8)(ctx->write_seq & 0xFF);
    if (total_unenc > (int)sizeof(mac_input) - 4) return -1;
    oc_memcpy(mac_input + mi, pkt, total_unenc); mi += total_unenc;
    u8 mac[32];
    hmac_sha256(ctx->mac_out, 32, mac_input, mi, mac);

    static u8 enc[16384];
    if (total_unenc > (int)sizeof(enc)) return -1;
    aes128_cbc_encrypt(ctx->enc_out, ctx->iv_out_next, pkt, total_unenc, enc);
    oc_memcpy(ctx->iv_out_next, enc + total_unenc - 16, 16);  /* CBC chain */

    static u8 out[16384];
    oc_memcpy(out, enc, total_unenc);
    oc_memcpy(out + total_unenc, mac, mac_size);
    int rc = net_send(ctx->sock, out, total_send);
    if (rc >= 0) ctx->write_seq++;
    return rc;
}

static int sd_recv_packet_encrypted(sshd_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    int block_size = 16, mac_size = 32;
    u8 first_block[16];
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < block_size) {
        int got = net_recv(ctx->sock, first_block + n, block_size - n);
        if (got > 0) { n += got; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 3000) return -1;
        }
    }
    u8 dec_first[16];
    aes128_cbc_decrypt(ctx->enc_in, ctx->iv_in_next, first_block, block_size, dec_first);
    int packet_length = ((int)dec_first[0] << 24) | ((int)dec_first[1] << 16) |
                        ((int)dec_first[2] << 8) | dec_first[3];
    {
        char b[64]; oc_strcpy(b, "[sshd] recv_enc: pkt_len=");
        char n2[12]; oc_u64_to_str((u64)packet_length, n2); oc_strcat(b, n2);
        sd_log(b);
    }
    if (packet_length < 1 || packet_length > 35000) {
        char b[64]; oc_strcpy(b, "recv: bad packet_length=");
        char n[10]; oc_u64_to_str((u64)packet_length, n); oc_strcat(b, n);
        sd_log(b);
        return -1;
    }
    int leftover = block_size - 4;
    int remaining = packet_length - leftover;
    if (remaining < 0 || remaining % block_size != 0) {
        char b[64]; oc_strcpy(b, "recv: bad blocking, remaining=");
        char n[10]; oc_u64_to_str((u64)remaining, n); oc_strcat(b, n);
        sd_log(b);
        return -1;
    }

    static u8 rest_buf[16384];
    n = 0;
    while (n < remaining + mac_size) {
        int r = net_recv(ctx->sock, rest_buf + n, (remaining + mac_size) - n);
        if (r > 0) { n += r; start = oc_timer_ticks(); }
        else {
            net_poll();
            if (oc_timer_ticks() - start > 3000) return -1;
        }
    }
    static u8 dec_rest[16384];
    if (remaining > 0) {
        aes128_cbc_decrypt(ctx->enc_in, first_block, rest_buf, remaining, dec_rest);
    }
    if (remaining >= 16) oc_memcpy(ctx->iv_in_next, rest_buf + remaining - 16, 16);
    else oc_memcpy(ctx->iv_in_next, first_block, 16);

    static u8 body[16384];
    int body_len = leftover + remaining;
    oc_memcpy(body, dec_first + 4, leftover);
    if (remaining > 0) oc_memcpy(body + leftover, dec_rest, remaining);

    static u8 mac_input[16384];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->read_seq >> 24);
    mac_input[mi++] = (u8)(ctx->read_seq >> 16);
    mac_input[mi++] = (u8)(ctx->read_seq >> 8);
    mac_input[mi++] = (u8)(ctx->read_seq & 0xFF);
    mac_input[mi++] = (u8)(packet_length >> 24);
    mac_input[mi++] = (u8)(packet_length >> 16);
    mac_input[mi++] = (u8)(packet_length >> 8);
    mac_input[mi++] = (u8)(packet_length & 0xFF);
    oc_memcpy(mac_input + mi, body, body_len); mi += body_len;
    u8 expected_mac[32];
    hmac_sha256(ctx->mac_in, 32, mac_input, mi, expected_mac);
    for (int i = 0; i < mac_size; i++) {
        if (rest_buf[remaining + i] != expected_mac[i]) {
            sd_log("MAC verify FAIL on incoming packet");
            sd_log_hex("  expected MAC (first 8): ", expected_mac, 8);
            sd_log_hex("  received  MAC (first 8): ", rest_buf + remaining, 8);
            return -1;
        }
    }

    int pad_len = body[0];
    *msg_type = body[1];
    {
        char b[80]; oc_strcpy(b, "[sshd] recv_enc OK: type=");
        char n2[12]; oc_u64_to_str((u64)*msg_type, n2); oc_strcat(b, n2);
        oc_strcat(b, " plen="); oc_u64_to_str((u64)(body_len - 2 - pad_len), n2); oc_strcat(b, n2);
        sd_log(b);
    }
    int plen = body_len - 1 - pad_len - 1;
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    oc_memcpy(payload, body + 2, plen);
    *payload_len = plen;
    ctx->read_seq++;
    return 0;
}

/* ---------- mpint helpers ---------- */

static int sd_write_mpint(u8 *buf, int *p, const u8 *val, int val_len) {
    int start = 0;
    while (start < val_len - 1 && val[start] == 0) start++;
    int n = val_len - start;
    int need_zero = (val[start] & 0x80) ? 1 : 0;
    int total = n + need_zero;
    int final_len = total;
    if (final_len < 1) final_len = 1;
    buf[(*p)++] = (u8)(final_len >> 24);
    buf[(*p)++] = (u8)(final_len >> 16);
    buf[(*p)++] = (u8)(final_len >> 8);
    buf[(*p)++] = (u8)(final_len & 0xFF);
    for (int i = 0; i < need_zero; i++) buf[(*p)++] = 0;
    oc_memcpy(buf + *p, val + start, n);
    *p += n;
    return 4 + total;
}

static void sd_write_str(u8 *buf, int *p, const void *data, int len) {
    buf[(*p)++] = (u8)(len >> 24);
    buf[(*p)++] = (u8)(len >> 16);
    buf[(*p)++] = (u8)(len >> 8);
    buf[(*p)++] = (u8)(len & 0xFF);
    oc_memcpy(buf + *p, data, len);
    *p += len;
}

static void sd_write_cstr(u8 *buf, int *p, const char *s) {
    int len = (int)oc_strlen(s);
    sd_write_str(buf, p, s, len);
}

static u32 sd_read_u32(const u8 *b) {
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
}

/* ---------- key derivation (server directions) ---------- */

static void sd_derive_keys(sshd_ctx_t *ctx) {
    /* K_mpint */
    int start = 0;
    while (start < SSHD_DH_BYTES - 1 && ctx->shared_secret[start] == 0) start++;
    int n = SSHD_DH_BYTES - start;
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

    u8 msg[512];
    u8 digest[32];
#define SD_COMPUTE_KEY(X_char, out_buf, out_len) do { \
        int mp = 0; \
        oc_memcpy(msg + mp, k_mpint, k_mpint_len); mp += k_mpint_len; \
        oc_memcpy(msg + mp, ctx->exchange_hash, 32); mp += 32; \
        msg[mp++] = X_char; \
        oc_memcpy(msg + mp, ctx->session_id, 32); mp += 32; \
        sha256(msg, mp, digest); \
        oc_memcpy(out_buf, digest, out_len); \
    } while (0)
    SD_COMPUTE_KEY('A', ctx->iv_in, 16);
    SD_COMPUTE_KEY('B', ctx->iv_out, 16);
    SD_COMPUTE_KEY('C', ctx->enc_in, 16);
    SD_COMPUTE_KEY('D', ctx->enc_out, 16);
    SD_COMPUTE_KEY('E', ctx->mac_in, 32);
    SD_COMPUTE_KEY('F', ctx->mac_out, 32);
#undef SD_COMPUTE_KEY
    oc_memcpy(ctx->iv_in_next, ctx->iv_in, 16);
    oc_memcpy(ctx->iv_out_next, ctx->iv_out, 16);
}

/* ---------- exchange hash ---------- */

static void sd_compute_hash(sshd_ctx_t *ctx, u8 hash[32]) {
    u8 *buf = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!buf) return;
    int p = 0;
    /* client = V_C (cookie in I_C etc); we are V_S */
    sd_write_cstr(buf, &p, ctx->client_banner);
    sd_write_cstr(buf, &p, ctx->server_banner);
    sd_write_str(buf, &p, ctx->client_kexinit, ctx->client_kexinit_len);
    sd_write_str(buf, &p, ctx->server_kexinit, ctx->server_kexinit_len);

    /* K_S: ssh-rsa host key blob = string "ssh-rsa" + mpint e + mpint N */
    {
        u8 ks[400];
        int kp = 0;
        sd_write_cstr(ks, &kp, "ssh-rsa");
        u8 em[3] = {0x01, 0x00, 0x01};  /* e = 65537 */
        sd_write_mpint(ks, &kp, em, 3);
        sd_write_mpint(ks, &kp, sshd_rsa_n, 256);
        sd_write_str(buf, &p, ks, kp);
    }

    sd_write_mpint(buf, &p, ctx->client_pub, SSHD_DH_BYTES);  /* e */
    sd_write_mpint(buf, &p, ctx->dh_pub, SSHD_DH_BYTES);      /* f */
    sd_write_mpint(buf, &p, ctx->shared_secret, SSHD_DH_BYTES); /* K */
    sha256(buf, p, hash);
    pmm_free_frame((u64)(uintptr_t)buf);
}

/* ---------- exec engine ---------- */

static void sd_run_exec(sshd_ctx_t *ctx, const char *cmd) {
    sd_log("exec: running command via shell_execute_captured()");
    ctx->exec_len = 0;
    /* WP-09 fix: use the shell's own capture mechanism. The old code swapped
     * the boot-time console hook (the serial mirror) for a private capture
     * hook and then set the hook to NULL afterwards, which permanently
     * silenced serial output: the kernel kept running (CHANNEL_DATA was sent
     * and the client received the output) but every later log line and the
     * shell prompt vanished from the serial console, so the test harness saw
     * a "hang". shell_execute_captured() chains to the previous hook and
     * restores it when done. */
    shell_execute_captured(cmd, (char *)ctx->exec_out, (int)sizeof(ctx->exec_out) - 1);
    ctx->exec_out[sizeof(ctx->exec_out) - 1] = 0;
    ctx->exec_len = (int)oc_strlen((const char *)ctx->exec_out);
    ctx->exec_active = 0;
    char b[64];
    oc_strcpy(b, "exec: captured ");
    char n[10];
    oc_u64_to_str((u64)ctx->exec_len, n);
    oc_strcat(b, n);
    oc_strcat(b, " bytes of output");
    sd_log(b);
}

/* ---------- protocol phases ---------- */

static int sd_send_version(sshd_ctx_t *ctx) {
    int len = (int)oc_strlen(SSHD_BANNER);
    int rc = net_send(ctx->sock, SSHD_BANNER, len);
    oc_strcpy(ctx->server_banner, "SSH-2.0-OpenCubeOS_sshd_WP-09");
    return (rc == len) ? 0 : -1;
}

static int sd_recv_version(sshd_ctx_t *ctx) {
    int n = 0;
    u64 start = oc_timer_ticks();
    while (n < (int)sizeof(ctx->client_banner) - 1) {
        char ch;
        int r = net_recv(ctx->sock, &ch, 1);
        if (r > 0) {
            ctx->client_banner[n++] = ch;
            if (n >= 2 && ctx->client_banner[n-2] == '\r' && ctx->client_banner[n-1] == '\n') break;
            start = oc_timer_ticks();
        } else {
            net_poll();
            if (oc_timer_ticks() - start > 3000) return -1;
        }
    }
    ctx->client_banner[n] = 0;
    /* strip CRLF for H */
    if (n >= 2) ctx->client_banner[n-2] = 0;
    else if (n >= 1) ctx->client_banner[n-1] = 0;
    sd_log_hex("client banner: ", (const u8 *)ctx->client_banner,
               (int)oc_strlen(ctx->client_banner) > 16 ? 16 : (int)oc_strlen(ctx->client_banner));
    return 0;
}

static const char *SSHD_KEX_KEXALGOS = "diffie-hellman-group14-sha256";
static const char *SSHD_KEX_HOSTKEYS = "rsa-sha2-256";
static const char *SSHD_KEX_CIPHERS  = "aes128-cbc";
static const char *SSHD_KEX_MACS     = "hmac-sha2-256";
static const char *SSHD_KEX_COMP     = "none";

static int sd_send_kexinit(sshd_ctx_t *ctx) {
    u8 cookie[16];
    crypto_random(cookie, 16);
    u8 payload[1024];
    int p = 0;
    oc_memcpy(payload + p, cookie, 16); p += 16;
    sd_write_cstr(payload, &p, SSHD_KEX_KEXALGOS);
    sd_write_cstr(payload, &p, SSHD_KEX_HOSTKEYS);
    sd_write_cstr(payload, &p, SSHD_KEX_CIPHERS);
    sd_write_cstr(payload, &p, SSHD_KEX_CIPHERS);
    sd_write_cstr(payload, &p, SSHD_KEX_MACS);
    sd_write_cstr(payload, &p, SSHD_KEX_MACS);
    sd_write_cstr(payload, &p, SSHD_KEX_COMP);
    sd_write_cstr(payload, &p, SSHD_KEX_COMP);
    sd_write_cstr(payload, &p, "");   /* languages client->server */
    sd_write_cstr(payload, &p, "");   /* languages server->client */
    payload[p++] = 0;                  /* first_kex_packet_follows */
    payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; /* reserved */
    /* save for H (payload starts at msg_type in the wire; for H we include msg_type) */
    u8 wire[1100];
    wire[0] = SSHD_MSG_KEXINIT;
    oc_memcpy(wire + 1, payload, p);
    oc_memcpy(ctx->server_kexinit, wire, p + 1);
    ctx->server_kexinit_len = p + 1;
    return sd_send_packet_unencrypted(ctx, SSHD_MSG_KEXINIT, payload, p);
}

static int sd_recv_kexinit(sshd_ctx_t *ctx) {
    u8 rtype;
    u8 payload[4096];
    int plen = sizeof(payload);
    if (sd_recv_packet_unencrypted(ctx, &rtype, payload, &plen) < 0) return -1;
    if (rtype != SSHD_MSG_KEXINIT) return -2;
    u8 wire[4096];
    wire[0] = rtype;
    oc_memcpy(wire + 1, payload, plen);
    oc_memcpy(ctx->client_kexinit, wire, plen + 1);
    ctx->client_kexinit_len = plen + 1;
    return 0;
}

static int sd_send_kexdh_reply(sshd_ctx_t *ctx) {
    /* server DH: y, f = g^y mod p, K = e^y mod p */
    crypto_random(ctx->dh_priv, SSHD_DH_BYTES);
    ctx->dh_priv[0] &= 0x7F;
    u8 g_val[SSHD_DH_BYTES];
    oc_memset(g_val, 0, SSHD_DH_BYTES);
    g_val[SSHD_DH_BYTES - 1] = 2;
    sd_log("computing f = g^y mod p (2048-bit modexp, ~26s)...");
    u64 t0 = oc_timer_ticks();
    dh_modexp_n(g_val, ctx->dh_priv, dh_group14_prime, ctx->dh_pub, SSHD_DH_BYTES);
    u64 t1 = oc_timer_ticks();
    sd_log("computing K = e^y mod p (~26s)...");
    dh_modexp_n(ctx->client_pub, ctx->dh_priv, dh_group14_prime, ctx->shared_secret, SSHD_DH_BYTES);
    u64 t2 = oc_timer_ticks();
    char b[80];
    oc_strcpy(b, "DH modexp times: ");
    char num[10];
    oc_u64_to_str((t1 - t0) * 1000 / (u64)OC_TIMER_HZ, num);
    oc_strcat(b, num); oc_strcat(b, " / ");
    oc_u64_to_str((t2 - t1) * 1000 / (u64)OC_TIMER_HZ, num);
    oc_strcat(b, num); oc_strcat(b, " ms");
    sd_log(b);

    /* exchange hash H */
    sd_compute_hash(ctx, ctx->exchange_hash);
    oc_memcpy(ctx->session_id, ctx->exchange_hash, 32);
    ctx->session_id_set = 1;
    sd_log_hex("H (first 16): ", ctx->exchange_hash, 16);

    /* RSA signature over H: sig = RSASSA-PKCS1-v1_5-SIGN(d, H)
     * EM = 0x00 0x01 FF..FF 0x00 || DigestInfo(SHA-256) || H  (256 bytes) */
    u8 em[256];
    oc_memset(em, 0xFF, 256);
    em[0] = 0x00; em[1] = 0x01;
    static const u8 digest_info[] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    int di_len = (int)sizeof(digest_info);
    /* EM = 0x00 0x01 || PS(0xFF..) || 0x00 || DigestInfo(SHA-256) || digest
     * total 256 bytes; PS length = 256 - 3 - 19 - 32 = 202.
     * WP-09 fix: paramiko's RSAKey.verify_ssh_sig() hashes `data` (the
     * exchange hash H) with SHA-256 before verification, so the EM must
     * carry SHA256(H) — not H itself (RFC 4253 §8.2 semantics as
     * implemented by OpenSSH/paramiko). */
    u8 h_digest[32];
    sha256(ctx->exchange_hash, 32, h_digest);
    int t_len = di_len + 32;
    oc_memcpy(em + (256 - t_len), digest_info, di_len);
    oc_memcpy(em + (256 - 32), h_digest, 32);
    em[256 - t_len - 1] = 0x00;  /* separator byte before DigestInfo */
    sd_log("computing RSA signature (2048-bit modexp, ~26s)...");
    u8 sig[256];
    u64 t3 = oc_timer_ticks();
    dh_modexp_n(em, sshd_rsa_d, sshd_rsa_n, sig, 256);
    u64 t4 = oc_timer_ticks();
    oc_strcpy(b, "RSA sign time: ");
    oc_u64_to_str((t4 - t3) * 1000 / (u64)OC_TIMER_HZ, num);
    oc_strcat(b, num); oc_strcat(b, " ms");
    sd_log(b);

    /* KEXDH_REPLY: string K_S + mpint f + string signature */
    u8 blob[1400];
    int p = 0;
    /* K_S = string "ssh-rsa" + mpint e + mpint N */
    {
        u8 ks[400];
        int kp = 0;
        sd_write_cstr(ks, &kp, "ssh-rsa");
        u8 em3[3] = {0x01, 0x00, 0x01};
        sd_write_mpint(ks, &kp, em3, 3);
        sd_write_mpint(ks, &kp, sshd_rsa_n, 256);
        sd_write_str(blob, &p, ks, kp);
    }
    sd_write_mpint(blob, &p, ctx->dh_pub, SSHD_DH_BYTES);
    /* signature blob: string "rsa-sha2-256" + string sig */
    {
        u8 sbuf[400];
        int sp = 0;
        sd_write_cstr(sbuf, &sp, "rsa-sha2-256");
        sd_write_str(sbuf, &sp, sig, 256);
        sd_write_str(blob, &p, sbuf, sp);
    }
    {
        int rc = sd_send_packet_unencrypted(ctx, SSHD_MSG_KEXDH_REPLY, blob, p);
        char b[64]; oc_strcpy(b, "KEXDH_REPLY send rc=");
        char n[10]; oc_u64_to_str((u64)(rc < 0 ? -rc : rc), n); oc_strcat(b, n);
        oc_strcat(b, " (p=");
        oc_u64_to_str((u64)p, n); oc_strcat(b, n); oc_strcat(b, ")");
        sd_log(b);
        return rc;
    }
}

static int sd_do_userauth(sshd_ctx_t *ctx, u8 payload[], int plen) {
    /* payload: string user + string service + string method + [method data] */
    int off = 0;
    if (off + 4 > plen) return -1;
    int ulen = (int)sd_read_u32(payload + off); off += 4;
    if (off + ulen > plen) return -1;
    oc_memcpy(ctx->auth_user, payload + off, ulen < 31 ? ulen : 31);
    ctx->auth_user[ulen < 31 ? ulen : 31] = 0;
    off += ulen;
    if (off + 4 > plen) return -1;
    int slen = (int)sd_read_u32(payload + off); off += 4;   /* service */
    off += slen;
    if (off + 4 > plen) return -1;
    int mlen = (int)sd_read_u32(payload + off); off += 4;   /* method */
    off += mlen;
    /* WP-09 fix: fixed-length compare — oc_strcmp relied on the byte after
     * "password" being NUL (it is the FALSE boolean in practice, but that is
     * luck, not correctness). */
    int is_password = (mlen == 8 && oc_memcmp((const char *)(payload + (off - mlen)), "password", 8) == 0);
    if (!is_password) {
        /* list allowed methods */
        u8 fail[16];
        int fp = 0;
        sd_write_cstr(fail, &fp, "password");
        fail[fp++] = 0;  /* partial success = FALSE */
        sd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_FAILURE, fail, fp);
        return -2;
    }
    if (off + 1 > plen) return -1;
    /* boolean FALSE (0) then string password */
    off += 1;
    if (off + 4 > plen) return -1;
    int pwlen = (int)sd_read_u32(payload + off); off += 4;
    if (off + pwlen > plen) return -1;
    char pw[32];
    oc_memcpy(pw, payload + off, pwlen < 31 ? pwlen : 31);
    pw[pwlen < 31 ? pwlen : 31] = 0;

    char b[96];
    oc_strcpy(b, "auth attempt user=");
    oc_strcat(b, ctx->auth_user);
    sd_log(b);
    if (oc_strcmp(ctx->auth_user, g_ssd.auth_user) == 0 &&
        oc_strcmp(pw, g_ssd.auth_pass) == 0) {
        sd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_SUCCESS, (const u8 *)0, 0);
        sd_log("USERAUTH_SUCCESS sent");
        return 0;
    }
    u8 fail[16];
    int fp = 0;
    sd_write_cstr(fail, &fp, "password");
    fail[fp++] = 0;
    sd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_FAILURE, fail, fp);
    sd_log("USERAUTH_FAILURE sent (bad credentials)");
    return -3;
}

static void sd_handle_exec(sshd_ctx_t *ctx, u8 payload[], int plen) {
    {
        char b[160]; oc_strcpy(b, "[sshd] chan_req payload: ");
        char hx[4];
        for (int i = 0; i < 48 && i < plen; i++) {
            oc_u64_to_hex(payload[i], hx, 2);
            oc_strcat(b, hx);
        }
        sd_log(b);
    }
    int off = 4;
    if (off + 4 > plen) return;
    int reqlen = (int)sd_read_u32(payload + off); off += 4;
    if (off + reqlen > plen) return;
    /* WP-09 fix: "exec" is NOT NUL-terminated in the payload — the next byte
     * is want_reply (0x01). oc_strcmp would read past the name into the
     * following field and never match, so every exec request was answered
     * with CHANNEL_FAILURE. Compare the fixed 4 bytes instead. */
    int is_exec = (reqlen == 4 && oc_memcmp((const char *)(payload + off), "exec", 4) == 0);
    off += reqlen;
    if (off >= plen) return;
    int want_reply = payload[off]; off += 1;
    if (!is_exec) {
        if (want_reply) sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_FAILURE, payload, 4);
        return;
    }
    if (off + 4 > plen) return;
    int clen = (int)sd_read_u32(payload + off); off += 4;
    /* WP-09-FIX BUG-013: the old hard limit of 256 bytes rejected most
     * real exec commands (a 405-byte command got CHANNEL_FAILURE) even
     * though the exec output buffer is 4096. Raise the limit to 1024
     * bytes (keeps the on-stack buffer modest). */
    if (off + clen > plen || clen < 0 || clen >= 1024) {
        if (want_reply) sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_FAILURE, payload, 4);
        return;
    }
    char cmd[1024];
    oc_memcpy(cmd, payload + off, clen);
    cmd[clen] = 0;

    char b[96];
    oc_strcpy(b, "exec request: '");
    oc_strcat(b, cmd);
    oc_strcat(b, "'");
    sd_log(b);

    if (want_reply) sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_SUCCESS, payload, 4);
    sd_run_exec(ctx, cmd);
    /* CHANNEL_DATA: u32 recipient + string data */
    {
        u8 dp[64 + 4096];
        int dp_len = 0;
        dp_len = 4;
        sd_write_str(dp, &dp_len, ctx->exec_out, ctx->exec_len);
        dp[0] = (u8)(ctx->peer_channel >> 24);
        dp[1] = (u8)(ctx->peer_channel >> 16);
        dp[2] = (u8)(ctx->peer_channel >> 8);
        dp[3] = (u8)(ctx->peer_channel & 0xFF);
        sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_DATA, dp, dp_len);
    }
    /* exit status 0 as CHANNEL_REQUEST "exit-status".
     * WP-09 fix: the buffer must hold 4 (chan) + 15 (string "exit-status")
     * + 1 (want_reply) + 4 (status) = 24 bytes; the old u8 es[16] overflowed
     * the stack buffer by 8 bytes while writing the reply. */
    {
        u8 es[32];
        int ep = 0;
        es[ep++] = (u8)(ctx->peer_channel >> 24);
        es[ep++] = (u8)(ctx->peer_channel >> 16);
        es[ep++] = (u8)(ctx->peer_channel >> 8);
        es[ep++] = (u8)(ctx->peer_channel & 0xFF);
        sd_write_cstr(es, &ep, "exit-status");
        es[ep++] = 0;
        es[ep++] = 0; es[ep++] = 0; es[ep++] = 0; es[ep++] = 0;
        sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_REQUEST, es, ep);
    }
    /* EOF. WP-09 fix: do NOT send CLOSE here — the client needs time to
     * read CHANNEL_DATA and process CHANNEL_SUCCESS first. The client
     * closes (or the session loop ends) instead. */
    {
        u8 ch[4];
        ch[0] = (u8)(ctx->peer_channel >> 24);
        ch[1] = (u8)(ctx->peer_channel >> 16);
        ch[2] = (u8)(ctx->peer_channel >> 8);
        ch[3] = (u8)(ctx->peer_channel & 0xFF);
        sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_EOF, ch, 4);
    }
    sd_log("exec session complete");
}

static int sd_serve_connection(sshd_ctx_t *ctx) {
    u8 msg_type;
    u8 payload[4096];
    int plen;

    if (sd_send_version(ctx) < 0) return -1;
    if (sd_recv_version(ctx) < 0) return -2;
    sd_log("banner exchange OK");

    if (sd_recv_kexinit(ctx) < 0) return -3;
    if (sd_send_kexinit(ctx) < 0) return -4;
    sd_log("KEXINIT exchange OK");

    /* KEXDH_INIT */
    plen = sizeof(payload);
    if (sd_recv_packet_unencrypted(ctx, &msg_type, payload, &plen) < 0) return -5;
    if (msg_type != SSHD_MSG_KEXDH_INIT) return -6;
    /* payload: mpint e */
    if (plen < 4) return -7;
    int e_len = (int)sd_read_u32(payload);
    /* right-align into 256 bytes */
    oc_memset(ctx->client_pub, 0, SSHD_DH_BYTES);
    {
        const u8 *ed = payload + 4;
        int el = e_len;
        if (el > 0 && ed[0] == 0) { ed++; el--; }
        if (el > SSHD_DH_BYTES) { ed += (el - SSHD_DH_BYTES); el = SSHD_DH_BYTES; }
        oc_memcpy(ctx->client_pub + (SSHD_DH_BYTES - el), ed, el);
    }
    sd_log_hex("client e (first 8): ", ctx->client_pub, 8);

    if (sd_send_kexdh_reply(ctx) < 0) return -8;
    /* our NEWKEYS */
    if (sd_send_packet_unencrypted(ctx, SSHD_MSG_NEWKEYS, (const u8 *)0, 0) < 0) return -9;
    /* client NEWKEYS */
    plen = sizeof(payload);
    if (sd_recv_packet_unencrypted(ctx, &msg_type, payload, &plen) < 0) return -10;
    { char b[48]; oc_strcpy(b, "got type after REPLY: ");
              char n[10]; oc_u64_to_str((u64)msg_type, n); oc_strcat(b, n); sd_log(b); }
            if (msg_type != SSHD_MSG_NEWKEYS) return -11;
    sd_derive_keys(ctx);
    ctx->encrypted = 1;
    sd_log("NEWKEYS exchange OK — encrypted mode active");

    /* USERAUTH loop. RFC 4251: the client first requests the ssh-userauth
     * service (SERVICE_REQUEST) and must receive SERVICE_ACCEPT before it
     * sends USERAUTH_REQUEST — ignoring the request deadlocks both sides. */
    for (;;) {
        plen = sizeof(payload);
        if (sd_recv_packet_encrypted(ctx, &msg_type, payload, &plen) < 0) return -12;
        if (msg_type == SSHD_MSG_DISCONNECT) return -13;
        if (msg_type == SSHD_MSG_SERVICE_REQUEST) {
            u8 acc[48];
            int ap = 0;
            if (plen >= 4) {
                int slen = (int)sd_read_u32(payload);
                if (slen > 0 && slen <= 32 && 4 + slen <= plen) {
                    sd_write_str(acc, &ap, payload + 4, slen);
                }
            }
            sd_log_hex("SERVICE_REQUEST payload: ", payload, plen);
            sd_log_hex("SERVICE_ACCEPT payload:   ", acc, ap);
            sd_send_packet_encrypted(ctx, SSHD_MSG_SERVICE_ACCEPT, acc, ap);
            sd_log("SERVICE_ACCEPT sent (ssh-userauth)");
            continue;
        }
        if (msg_type == SSHD_MSG_USERAUTH_REQUEST) {
            if (sd_do_userauth(ctx, payload, plen) == 0) break;
        }
        /* ignore others */
    }
    sd_log("client authenticated");

    /* channel loop */
    for (;;) {
        plen = sizeof(payload);
        if (sd_recv_packet_encrypted(ctx, &msg_type, payload, &plen) < 0) return -14;
        if (msg_type == SSHD_MSG_CHANNEL_OPEN) {
            /* expect string "session" + sender chan + window + max packet.
             * WP-09 fix: fixed-length compare (channel type is not
             * NUL-terminated in the payload; matching worked only because
             * paramiko's first channel id is 0). */
            if (plen >= 11 && oc_memcmp((const char *)(payload + 4), "session", 7) == 0) {
                ctx->peer_channel = sd_read_u32(payload + 4 + 7);
                ctx->our_channel = 0;
                u8 conf[16];
                int cp = 0;
                sd_write_u32(conf, &cp, ctx->peer_channel);
                sd_write_u32(conf, &cp, ctx->our_channel);
                sd_write_u32(conf, &cp, 0x00020000);  /* our window */
                sd_write_u32(conf, &cp, 16384);       /* our max packet */
                sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_OPEN_CONF, conf, cp);
                ctx->channel_open = 1;
                sd_log("session channel open (CONFIRMATION sent)");
            } else {
                u8 fail[16];
                int fp = 0;
                u32 rc4 = 3;  /* ADMIN_PROHIBITED */
                fail[fp++] = (u8)(rc4 >> 24); fail[fp++] = (u8)(rc4 >> 16);
                fail[fp++] = (u8)(rc4 >> 8); fail[fp++] = (u8)rc4;
                sd_write_cstr(fail, &fp, "only session channels supported");
                sd_write_u32(fail, &fp, 0);
                sd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_OPEN_FAIL, fail, fp);
            }
        } else if (msg_type == SSHD_MSG_CHANNEL_REQUEST) {
            sd_handle_exec(ctx, payload, plen);
            /* WP-09 fix: keep the session loop alive so the client can read
             * CHANNEL_DATA and drive the close handshake. */
            continue;
        } else if (msg_type == SSHD_MSG_CHANNEL_CLOSE) {
            return 0;
        } else if (msg_type == SSHD_MSG_DISCONNECT) {
            return 0;
        }
        /* ignore everything else */
    }
}

/* ---------- command entry ---------- */

static void sd_write_u32(u8 *buf, int *p, u32 v) {
    buf[(*p)++] = (u8)(v >> 24);
    buf[(*p)++] = (u8)(v >> 16);
    buf[(*p)++] = (u8)(v >> 8);
    buf[(*p)++] = (u8)(v & 0xFF);
}

int g_tcp_data_trace = 1;

int sshd_main(u16 port, const char *user, const char *pass) {
    sshd_ctx_t *ctx = &g_ssd;
    oc_memset(ctx, 0, sizeof(*ctx));
    oc_strcpy(ctx->auth_user, user);
    oc_strcpy(ctx->auth_pass, pass);

    int ls = net_socket(SOCK_TCP);
    if (ls < 0) { sd_log("listen socket alloc failed"); return 1; }
    net_bind(ls, 0, port);
    if (tcp_listen(port, (tcp_handler_fn)0) != 0) {
        sd_log("tcp_listen failed");
        return 1;
    }
    char b[64];
    oc_strcpy(b, "listening on port ");
    char n[10];
    oc_u64_to_str((u64)port, n);
    oc_strcat(b, n);
    oc_strcat(b, " (waiting for one connection)");
    sd_log(b);

    u32 cip = 0;
    u16 cport = 0;
    int fd = net_accept(ls, &cip, &cport);
    if (fd < 0) { sd_log("accept timeout"); return 1; }
    ctx->sock = fd;
    oc_strcpy(b, "connection from ");
    oc_u64_to_str((u64)((cip >> 24) & 0xFF), n); oc_strcat(b, n); oc_strcat(b, ".");
    oc_u64_to_str((u64)((cip >> 16) & 0xFF), n); oc_strcat(b, n); oc_strcat(b, ".");
    oc_u64_to_str((u64)((cip >> 8) & 0xFF), n); oc_strcat(b, n); oc_strcat(b, ".");
    oc_u64_to_str((u64)(cip & 0xFF), n); oc_strcat(b, n);
    sd_log(b);

    int rc = sd_serve_connection(ctx);
    if (rc == 0) sd_log("session finished cleanly");
    else {
        char b2[48];
        oc_strcpy(b2, "session failed (code ");
        oc_u64_to_str((u64)(-rc), n);
        oc_strcat(b2, n); oc_strcat(b2, ")");
        sd_log(b2);
    }
    net_close(fd);
    net_close(ls);
    return (rc == 0) ? 0 : 1;
}
