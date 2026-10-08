/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/ssh.c
 * Purpose: SSH-2.0 client.
 *
 * Implements (subset of RFC 4251-4254):
 *   - SSH-2.0 version banner exchange
 *   - SSH_MSG_KEXINIT (algorithm negotiation), incl. the strict-kex
 *     extension tokens (RFC 9144, Terrapin hardening)
 *   - SSH_MSG_KEXDH_INIT / KEXDH_REPLY: curve25519-sha256 (preferred,
 *     RFC 8731) or diffie-hellman-group14-sha256 (RFC 8268)
 *   - SSH_MSG_NEWKEYS transition to encrypted mode; with strict-kex both
 *     sequence numbers restart at 0 after every NEWKEYS (RFC 9144)
 *   - byte-threshold rekey: at 64 MiB per direction the client starts a
 *     new KEXINIT at a packet boundary, reusing the KEX machinery (the
 *     rekey exchange itself runs under the still-active keys)
 *   - SSH_MSG_SERVICE_REQUEST / SERVICE_ACCEPT service dance before
 *     userauth (RFC 4252 S5)
 *   - SSH_MSG_USERAUTH_REQUEST: password, or publickey when the password
 *     is empty
 *   - SSH_MSG_CHANNEL_OPEN + CHANNEL_REQUEST (exec) + CHANNEL_DATA
 *
 * Algorithms / trust model:
 *   - Cipher: aes128-ctr (preferred), aes128-cbc fallback (RFC 4253 S6.3
 *     rolling IV)
 *   - MAC: hmac-sha2-256 (RFC 6668) - NOT hmac-sha1
 *   - Host key: rsa-sha2-256 / rsa-sha2-512 (RFC 8332), ssh-rsa blobs.
 *     The signature over the exchange hash H is verified against the
 *     host key K_S BEFORE any key is used (RFC 4253 S8); RSA-2048 only.
 *   - Trust anchor: TOFU against /etc/ssh_known_hosts ("<ip:port> <fp>"
 *     per line); a changed host key refuses the connection (possible
 *     MITM) instead of being silently accepted.
 *   - Key derivation: SHA-256 (RFC 4253 S7.2), not HMAC-SHA1
 *   - Interop tested against paramiko in both directions (kernel client
 *     to paramiko server; kernel sshd with paramiko client)
 *
 * Limitations:
 *   - No compression
 *   - No PTY (exec only, no shell)
 *   - Rekey is client-initiated at a packet boundary in the exec loop;
 *     no data packets are mixed into an in-progress exchange - a peer
 *     that sends data mid-exchange fails the rekey and the session is
 *     dropped (documented drop-with-disconnect policy)
 */
#include "net_ssh.h"
#include "crypto_core.h"
#include "crypto_curve25519.h"
#include "crypto_rsa.h"
#include "crypto_sha512.h"
#include "net_core.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"
#include "core_timer.h"
#include "fs_vfs.h"

/* SSH message types (RFC 4254 §4) */
#define SSH_MSG_KEXINIT         20
#define SSH_MSG_NEWKEYS         21
#define SSH_MSG_KEXDH_INIT      30
#define SSH_MSG_KEXDH_REPLY    31
#define SSH_MSG_USERAUTH_REQ    50
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_USERAUTH_FAILURE 53
#define SSH_MSG_DISCONNECT     1
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT  6
#define SSH_MSG_GLOBAL_REQUEST 80
#define SSH_MSG_CHANNEL_OPEN    90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_DATA   94
#define SSH_MSG_CHANNEL_EOF    96
#define SSH_MSG_CHANNEL_CLOSE  97

/* DH group 14 prime now lives in crypto.c (crypto_dh_group14_prime, declared in crypto.h). */
#define SSH_DH_BYTES 256

/* BUG-0220 (A14-34): strict-kex extension tokens (RFC 9144). The client
 * advertises ..._c_..., the server ..._s_...; each side looks for the
 * PEER's token in the received KEXINIT list. */
#define SSH_KEX_STRICT_CLIENT "kex-strict-c-v00@openssh.com"
#define SSH_KEX_STRICT_SERVER "kex-strict-s-v00@openssh.com"

/* BUG-0219 (A14-33): client-side rekey threshold (bytes per direction,
 * counted after every completed NEWKEYS; 64 MiB). */
#define SSH_REKEY_THRESHOLD (64ULL * 1024ULL * 1024ULL)

/* Encrypted packet framing lives further down the file; the strict-kex
 * reset, the SERVICE_REQUEST dance (BUG-0235) and the rekey path (BUG-0219)
 * use it right after NEWKEYS. */
int net_ssh_send_packet_encrypted(net_ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len);
int net_ssh_recv_packet_encrypted(net_ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len);

static net_ssh_ctx_t g_ssh_ctx;

static void net_ssh_debug(const char *msg) {
    screen_console_puts(msg);
    screen_console_puts("\n");
}

static void net_ssh_debug_hex(const char *prefix, const u8 *buf, int n) {
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
    screen_console_puts(out);
}

net_ssh_ctx_t *net_ssh_get_ctx(void) { return &g_ssh_ctx; }

/* BUG-0218 (A14-32): single cleanup path for every handshake failure.
 * The old code had 12+ direct `return -N` exits after net_connect()
 * succeeded; none of them closed the TCP socket, so a retry loop (e.g. a
 * script hammering a wrong password) exhausted the socket table. Every
 * failure exit in net_ssh_connect funnels through here. */
static int net_ssh_connect_fail(net_ssh_ctx_t *ctx, int code) {
    if (ctx->net_tcp_sock >= 0) {
        net_close(ctx->net_tcp_sock);
        ctx->net_tcp_sock = -1;
    }
    ctx->encrypted = 0;
    return code;
}

/* BUG-0221 (A14-35): constant-time all-zero test (RFC 7748 S6.1) - OR all
 * bytes into one accumulator and branch only on the final value, so the
 * result does not depend on how many bytes were zero. */
static int net_ssh_secret_is_zero(const u8 *buf, int len) {
    u8 acc = 0;
    for (int i = 0; i < len; i++) acc |= buf[i];
    return acc == 0;
}

/* BUG-0220 (A14-34): strict-kex (RFC 9144) - when the extension was
 * negotiated, both directions' sequence numbers restart at 0 immediately
 * after the final NEWKEYS of the initial AND every rekey exchange. */
static void net_ssh_reset_seq(net_ssh_ctx_t *ctx) {
    if (!ctx->kex_strict) return;
    ctx->write_seq = 0;
    ctx->read_seq = 0;
    net_ssh_debug("[ssh] strict-kex: sequence numbers reset to 0");
}

/* Send raw (unencrypted) SSH packet.
 * Layout: packet_length(4 BE) || padding_length(1) || payload(N) || random_pad(4-255)
 * packet_length = padding_length(1) + payload(N) + padding_count
 * padding_count = 8 - ((5 + payload_len) % 8) [must be 4..255] */
static int net_ssh_send_packet_unencrypted(net_ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    /* WP-09 fix: pkt_len must include msg_type byte. Original was 1 byte short,
     * causing server to read 0-byte payload and IndexError. */
    int pad_len = 8 - ((6 + payload_len) % 8);  /* 4(length) + 1(pad_len) + 1(msg_type) + payload */
    if (pad_len < 4) pad_len += 8;
    u8 *pkt = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!pkt) return -1;
    int pkt_len = 1 + 1 + payload_len + pad_len;  /* pad_len_byte(1) + msg_type(1) + payload + pad */
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad_len;
    pkt[5] = msg_type;
    if (payload_len > 0) memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_len);
    int rc = net_send(ctx->net_tcp_sock, pkt, 4 + pkt_len);
    mem_pmm_free_frame((u64)(uintptr_t)pkt);
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
static int net_ssh_recv_packet_unencrypted(net_ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    u8 len_buf[4];
    int n = 0;
    u64 start = core_timer_ticks();
    while (n < 4) {
        int got = net_recv(ctx->net_tcp_sock, len_buf + n, 4 - n);
        if (got > 0) { n += got; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 800) return -1;
        }
    }
    int pkt_len = ((int)len_buf[0] << 24) | ((int)len_buf[1] << 16) |
                  ((int)len_buf[2] << 8) | len_buf[3];
    /* P0fix1 BUG-0007 (A14-01): the body is ONE 4096B page frame; the old
     * limit of 35000 let a hostile peer write ~31KB past it (pre-auth).
     * Everything the client legitimately receives (KEXINIT, keys,
     * interactive channel data) fits well below this bound. */
    if (pkt_len < 1 || pkt_len > 3500) return -1;

    u8 *body = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!body) return -1;
    int got = 0;
    while (got < pkt_len) {
        int r = net_recv(ctx->net_tcp_sock, body + got, pkt_len - got);
        if (r > 0) { got += r; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 800) {
                mem_pmm_free_frame((u64)(uintptr_t)body);
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
        char dbg2[80]; strcpy(dbg2, "[ssh]   recv encrypted: type=");
        char nn[10]; u64_to_str((u64)*msg_type, nn); strcat(dbg2, nn);
        strcat(dbg2, " plen="); u64_to_str((u64)plen, nn); strcat(dbg2, nn);
        strcat(dbg2, "\n"); screen_console_puts(dbg2);
    }
    memcpy(payload, body + 2, plen);  /* skip padding_length + msg_type */
    *payload_len = plen;
    mem_pmm_free_frame((u64)(uintptr_t)body);
    /* WP-09 fix: read_seq must advance for every received packet, same
     * rationale as write_seq above (affects verifying peer MACs later). */
    ctx->read_seq++;
    return 0;
}

/* Send SSH version banner */
static int net_ssh_send_version(net_ssh_ctx_t *ctx) {
    const char *banner = "SSH-2.0-OpenCubeOS_WP-09\r\n";
    int len = (int)strlen(banner);
    return net_send(ctx->net_tcp_sock, banner, len);
}

/* Receive SSH version banner */
static int net_ssh_recv_version(net_ssh_ctx_t *ctx) {
    int n = 0;
    u64 start = core_timer_ticks();
    /* P0fix1 BUG-0008 (A14-02): the loop used to allow 256 bytes into
     * server_banner[64] -> 192 bytes of struct overwrite with no CRLF
     * needed (pre-auth). Stop at the buffer size and reject. */
    while (n < (int)sizeof(ctx->server_banner) - 1) {
        int r = net_recv(ctx->net_tcp_sock, ctx->server_banner + n, 1);
        if (r > 0) {
            n++;
            if (n >= 2 && ctx->server_banner[n-2] == '\r' && ctx->server_banner[n-1] == '\n') break;
            start = core_timer_ticks();
        } else {
            net_poll();
            if (core_timer_ticks() - start > 800) return -1;
        }
    }
    if (n < 2 || n >= (int)sizeof(ctx->server_banner) - 1 ||
        ctx->server_banner[n-2] != '\r' || ctx->server_banner[n-1] != '\n') {
        net_ssh_debug("[ssh] server banner too long or unterminated");
        return -1;
    }
    ctx->server_banner[n-2] = 0;  /* strip \r\n */
    return 0;
}

/* Send KEXINIT message (msg 20).
 * Layout: cookie(16) + kex_algorithms(name-list) + server_host_key_algorithms +
 *         encryption_c2s + encryption_s2c + mac_c2s + mac_s2c +
 *         compression_c2s + compression_s2c + languages_c2s + languages_s2c +
 *         first_kex_packet_follows(1) + reserved(4) */
/* Check whether a comma-separated SSH name-list contains `name`
 * (exact member match, RFC 4251 S6 name-list semantics). */
static int net_ssh_name_has(const char *list, int list_len, const char *name) {
    int nlen = (int)strlen(name);
    for (int i = 0; i + nlen <= list_len; i++) {
        int m = 1;
        for (int j = 0; j < nlen; j++) {
            if (list[i + j] != name[j]) { m = 0; break; }
        }
        if (m && (i + nlen == list_len || list[i + nlen] == ',')) return 1;
    }
    return 0;
}

static int net_ssh_send_kexinit(net_ssh_ctx_t *ctx, int encrypted) {
    u8 buf[400];
    int p = 0;
    /* cookie */
    crypto_random(ctx->client_cookie, 16);
    memcpy(buf + p, ctx->client_cookie, 16); p += 16;
    /* name-list helper: write string as 4-byte length + bytes */
    #define WRITE_STR(s) do { \
        int L = (int)strlen(s); \
        buf[p++] = (u8)(L >> 24); buf[p++] = (u8)(L >> 16); \
        buf[p++] = (u8)(L >> 8); buf[p++] = (u8)(L & 0xFF); \
        for (int i = 0; s[i]; i++) buf[p++] = s[i]; \
    } while (0)
    /* Both RFC 8731 spellings: OpenSSH prefers the bare name, paramiko 5
     * only offers the @libssh.org variant. Listing both guarantees the
     * negotiated kex agrees whichever name-list order the server uses.
     * The trailing token advertises strict-kex (RFC 9144, BUG-0220). */
    WRITE_STR("curve25519-sha256,curve25519-sha256@libssh.org,diffie-hellman-group14-sha256,kex-strict-c-v00@openssh.com");
    WRITE_STR("rsa-sha2-256,rsa-sha2-512,ssh-rsa");
    WRITE_STR("aes128-ctr,aes128-cbc");
    WRITE_STR("aes128-ctr,aes128-cbc");
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
    memcpy(ctx->client_kexinit + 1, buf, ctx->client_kexinit_len - 1);

    /* BUG-0219: during a rekey the exchange runs under the currently
     * active keys, so the framing depends on the session phase. */
    if (encrypted)
        return net_ssh_send_packet_encrypted(ctx, SSH_MSG_KEXINIT, buf, p);
    return net_ssh_send_packet_unencrypted(ctx, SSH_MSG_KEXINIT, buf, p);
}

/* Parse server's KEXINIT to extract server's cookie + save bytes for hash. */
static int net_ssh_recv_kexinit(net_ssh_ctx_t *ctx, int encrypted) {
    static u8 payload[4096];  /* paramiko KEXINIT can be 800+ bytes */
    int payload_len = sizeof(payload);
    u8 msg_type;
    int rc = encrypted ? net_ssh_recv_packet_encrypted(ctx, &msg_type, payload, &payload_len)
                       : net_ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len);
    if (rc < 0) return -1;
    if (msg_type != SSH_MSG_KEXINIT) return -2;
    /* WP-09 fix: paramiko's remote_kex_init = cMSG_KEXINIT + m.get_so_far()
     * which INCLUDES the msg_type byte. Both local_kex_init and remote_kex_init
     * include msg_type. We must match this for exchange hash H to be correct. */
    ctx->server_kexinit_len = 1 + payload_len;
    if (ctx->server_kexinit_len > (int)sizeof(ctx->server_kexinit))
        ctx->server_kexinit_len = (int)sizeof(ctx->server_kexinit);
    ctx->server_kexinit[0] = msg_type;
    memcpy(ctx->server_kexinit + 1, payload, ctx->server_kexinit_len - 1);
    /* Extract server's cookie */
    if (payload_len >= 16) {
        memcpy(ctx->server_cookie, payload, 16);
    }
    /* Negotiate algorithms: parse the server's name-lists and pick the
     * first entry we support, in OUR preference order (RFC 4253 §7.1).
     * KEXINIT layout: cookie(16) + kex + hostkey + enc_c2s + enc_s2c +
     * mac_c2s + mac_s2c + comp*2 + lang*2 + fkpf + reserved. */
    {
        ctx->kex_curve25519 = 0;
        ctx->cipher_ctr = 0;
        int q = 16;
        const char *lists[8];
        int lens[8];
        for (int li = 0; li < 8; li++) {
            if (q + 4 > payload_len) { lens[li] = 0; lists[li] = ""; q += 4; continue; }
            int L = ((int)payload[q] << 24) | ((int)payload[q+1] << 16) |
                    ((int)payload[q+2] << 8) | payload[q+3];
            q += 4;
            /* P0fix1 BUG-0009 (A14-03): `q + L` overflows for L near 2^31,
             * so the bound check passed and name_has() walked gigabytes of
             * address space. Subtract instead of add (no overflow). */
            if (L < 0 || L > payload_len - q) { lens[li] = 0; lists[li] = ""; continue; }
            lists[li] = (const char *)(payload + q);
            lens[li] = L;
            q += L;
        }
        /* lists[0] = kex_algorithms; both RFC 8731 spellings.
         * Matches must respect name-list commas: a bare "curve25519-sha256"
         * is NOT matched when it is merely the prefix of the
         * "@libssh.org" variant. */
        if (net_ssh_name_has(lists[0], lens[0], "curve25519-sha256") ||
            net_ssh_name_has(lists[0], lens[0], "curve25519-sha256@libssh.org")) {
            ctx->kex_curve25519 = 1;
        }
        /* lists[2] = encryption c2s, lists[3] = s2c: choose ctr if both support */
        int c2s_ctr = net_ssh_name_has(lists[2], lens[2], "aes128-ctr");
        int s2c_ctr = net_ssh_name_has(lists[3], lens[3], "aes128-ctr");
        ctx->cipher_ctr = (c2s_ctr && s2c_ctr) ? 1 : 0;
        /* BUG-0220 (A14-34): strict-kex (RFC 9144). The SERVER advertises
         * kex-strict-s-v00@openssh.com; when present, both sides reset
         * both sequence numbers right after every NEWKEYS. */
        ctx->kex_strict = net_ssh_name_has(lists[0], lens[0], SSH_KEX_STRICT_SERVER);
        net_ssh_debug(ctx->kex_curve25519 ?
                  "[ssh] negotiated KEX: curve25519-sha256" :
                  "[ssh] negotiated KEX: diffie-hellman-group14-sha256");
        net_ssh_debug(ctx->cipher_ctr ?
                  "[ssh] negotiated cipher: aes128-ctr" :
                  "[ssh] negotiated cipher: aes128-cbc");
    }
    return 0;
}

/* ---- curve25519-sha256 KEX (RFC 8731) ----
 * KEX_ECDH_INIT (30): string e = X25519 public key (32 bytes)
 * KEX_ECDH_REPLY (31): string K_S || string f || string signature
 * K = raw 32-byte X25519 shared secret. */
static int net_ssh_send_kex_ecdh_init(net_ssh_ctx_t *ctx, int encrypted) {
    crypto_random(ctx->client_priv, 32);
    crypto_x25519_public(ctx->client_priv, ctx->client_pub);  /* low 32 bytes used */

    u8 payload[64];
    int p = 0;
    payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 32;
    memcpy(payload + p, ctx->client_pub, 32); p += 32;
    if (encrypted)
        return net_ssh_send_packet_encrypted(ctx, SSH_MSG_KEXDH_INIT, payload, p);
    return net_ssh_send_packet_unencrypted(ctx, SSH_MSG_KEXDH_INIT, payload, p);
}

static int net_ssh_recv_kex_reply(net_ssh_ctx_t *ctx, int encrypted) {
    u8 payload[4096];
    int payload_len = sizeof(payload);
    u8 msg_type;
    int rc = encrypted ? net_ssh_recv_packet_encrypted(ctx, &msg_type, payload, &payload_len)
                       : net_ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len);
    if (rc < 0) return -1;
    if (msg_type != SSH_MSG_KEXDH_REPLY) return -2;
    int off = 0;
    if (off + 4 > payload_len) return -3;
    int ks_len = (payload[off] << 24) | (payload[off+1] << 16) |
                 (payload[off+2] << 8) | payload[off+3];
    off += 4;
    /* P0fix1 BUG-0010 (A14-04): a negative ks_len passed both checks and
     * reached memcpy with a huge size_t length. */
    if (ks_len < 0 || off + ks_len > payload_len) return -3;
    if (ks_len <= (int)sizeof(ctx->server_host_key)) {
        memcpy(ctx->server_host_key, payload + off, ks_len);
        ctx->server_host_key_len = ks_len;
    }
    off += ks_len;
    if (off + 4 > payload_len) return -4;
    int f_len = (payload[off] << 24) | (payload[off+1] << 16) |
                (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (f_len < 0 || off + f_len > payload_len || f_len < 32) return -5;
    /* server x25519 public key: last 32 bytes of the mpint */
    memcpy(ctx->server_pub + SSH_DH_BYTES - 32, payload + off + (f_len - 32), 32);
    off += f_len;
    /* string signature - verified against K_S after H is computed */
    if (off + 4 > payload_len) return -6;
    ctx->server_sig_len = (payload[off] << 24) | (payload[off+1] << 16) |
                          (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (ctx->server_sig_len <= 0 ||
        ctx->server_sig_len > (int)sizeof(ctx->server_sig) ||
        off + ctx->server_sig_len > payload_len) return -6;
    memcpy(ctx->server_sig, payload + off, ctx->server_sig_len);
    return 0;
}

/* SSH mpint encoding (RFC 4251 S5): 4-byte length + minimal big-endian
 * magnitude + sign byte (0x00) iff the MSB of the first byte is set.
 * This matches paramiko's deflate_long() and OpenSSH's buffer_put_bignum2. */
static void net_ssh_write_mpint(u8 *buf, int *p, const u8 *val, int val_len) {
    /* Skip leading zeros */
    int start = 0;
    while (start < val_len - 1 && val[start] == 0) start++;
    int n = val_len - start;
    int need_zero = (val[start] & 0x80) ? 1 : 0;
    int final_len = n + need_zero;
    buf[(*p)++] = (u8)(final_len >> 24);
    buf[(*p)++] = (u8)(final_len >> 16);
    buf[(*p)++] = (u8)(final_len >> 8);
    buf[(*p)++] = (u8)(final_len & 0xFF);
    if (need_zero) buf[(*p)++] = 0;
    memcpy(buf + *p, val + start, n);
    *p += n;
}

/* Send KEXDH_INIT (msg 30): mpint e */
static int net_ssh_send_kexdh_init(net_ssh_ctx_t *ctx, int encrypted) {
    /* Generate client DH private key (256-byte random, masked) */
    crypto_random(ctx->client_priv, SSH_DH_BYTES);
    ctx->client_priv[0] &= 0x7F;  /* ensure < p */
    ctx->client_priv[SSH_DH_BYTES - 1] &= 0xFE;  /* even */

    /* Compute e = g^x mod p */
    u8 g_val[SSH_DH_BYTES];
    memset(g_val, 0, SSH_DH_BYTES);
    g_val[SSH_DH_BYTES - 1] = 2;  /* g = 2 for group 14 */

    net_ssh_debug("[ssh] computing e = g^x mod p (DH modexp 2048-bit, ~60s in QEMU)...");
    u64 t0 = core_timer_ticks();
    crypto_dh_modexp_n(g_val, ctx->client_priv, crypto_dh_group14_prime, ctx->client_pub, 256);
    u64 t1 = core_timer_ticks();
    u64 ms = (t1 - t0) * 1000 / (u64)OC_TIMER_HZ;
    char buf[80];
    strcpy(buf, "[ssh]   DH modexp time: ");
    char num[10];
    u64_to_str(ms, num);
    strcat(buf, num);
    strcat(buf, " ms\n");
    screen_console_puts(buf);

    /* Build msg: mpint e */
    u8 payload[300];
    int p = 0;
    net_ssh_write_mpint(payload, &p, ctx->client_pub, SSH_DH_BYTES);
    if (encrypted)
        return net_ssh_send_packet_encrypted(ctx, SSH_MSG_KEXDH_INIT, payload, p);
    return net_ssh_send_packet_unencrypted(ctx, SSH_MSG_KEXDH_INIT, payload, p);
}

/* Receive KEXDH_REPLY (msg 31): string K_S || mpint f || string sig */
static int net_ssh_recv_kexdh_reply(net_ssh_ctx_t *ctx, int encrypted) {
    u8 payload[4096];
    int payload_len = sizeof(payload);
    u8 msg_type;
    int rc = encrypted ? net_ssh_recv_packet_encrypted(ctx, &msg_type, payload, &payload_len)
                       : net_ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len);
    if (rc < 0) return -1;
    if (msg_type != SSH_MSG_KEXDH_REPLY) return -2;
    /* Parse: string K_S (host key blob) + mpint f + string signature */
    int off = 0;
    if (off + 4 > payload_len) return -3;
    int ks_len = (payload[off] << 24) | (payload[off+1] << 16) | (payload[off+2] << 8) | payload[off+3];
    off += 4;
    /* P0fix1 BUG-0011 (A14-05): reject negative lengths before they are
     * used as (negated) memcpy sizes / buffer offsets. */
    if (ks_len < 0 || off + ks_len > payload_len) return -3;
    /* WP-09 fix: save K_S (server host key blob) for exchange hash H.
     * Without K_S in the hash, all derived keys are wrong. */
    if (ks_len <= (int)sizeof(ctx->server_host_key)) {
        memcpy(ctx->server_host_key, payload + off, ks_len);
        ctx->server_host_key_len = ks_len;
    }
    off += ks_len;  /* skip K_S */
    if (off + 4 > payload_len) return -4;
    int f_len = (payload[off] << 24) | (payload[off+1] << 16) | (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (f_len < 0 || off + f_len > payload_len) return -5;
    /* Copy server's f into server_pub (right-aligned, SSH_DH_BYTES=256 bytes).
     * mpint encoding may have a leading 0x00 for sign extension when MSB
     * is set (which it always is for 2048-bit DH values — top byte 0x80+).
     * Typical f_len is 257 (0x00 + 256 bytes). We handle:
     *   - f_len == 256: copy directly
     *   - f_len == 257 (leading 0x00): strip leading byte, copy 256
     *   - f_len < 256: right-align in 256-byte buffer (rare for DH)
     *   - f_len > 257: take low 256 bytes (drop high padding) */
    memset(ctx->server_pub, 0, SSH_DH_BYTES);
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
    memcpy(ctx->server_pub + (SSH_DH_BYTES - copy_len), payload + data_off, copy_len);
    net_ssh_debug_hex("[ssh]   server f (first 8): ", ctx->server_pub, 8);
    /* string signature - verified against K_S after H is computed */
    off += f_len;
    if (off + 4 > payload_len) return -6;
    ctx->server_sig_len = (payload[off] << 24) | (payload[off+1] << 16) |
                          (payload[off+2] << 8) | payload[off+3];
    off += 4;
    if (ctx->server_sig_len <= 0 ||
        ctx->server_sig_len > (int)sizeof(ctx->server_sig) ||
        off + ctx->server_sig_len > payload_len) return -6;
    memcpy(ctx->server_sig, payload + off, ctx->server_sig_len);
    return 0;
}

/* BUG-0073 FIX (A14-22): TOFU trust anchor — known_hosts.
 *
 * Before this fix the client verified the host-key SIGNATURE but had
 * no anchor: any attacker running a MITM with their own RSA-2048 host
 * key passed verification (they sign H with their own key), so the
 * "verified" handshake gave zero protection. The fingerprint was
 * printed (Trust On First Use display) but never recorded or checked.
 *
 * Now the SHA-256 fingerprint of the host key is persisted per
 * "<ip>:<port>" in /etc/ssh_known_hosts (FAT32, survives reboots on a
 * persistent disk):
 *   - first connection: record the key ("TOFU" — the user is the
 *     anchor, exactly like OpenSSH's known_hosts first use), and
 *   - later connections: a DIFFERENT key is a hard failure — the
 *     connection is refused with both fingerprints displayed, so a
 *     MITM key swap is detected instead of silently accepted. */
#define SSH_KNOWN_HOSTS_PATH "/etc/ssh_known_hosts"

/* BUG-0073: the <ip>:<port> anchor key for the CURRENT connection; set
 * by net_ssh_connect before the handshake, read by the TOFU check. */
static u32 g_ssh_kh_ip;
static u16 g_ssh_kh_port;

static void ssh_kh_fp_hex(const u8 fp[32], char out[65]) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[fp[i] >> 4];
        out[i * 2 + 1] = hexd[fp[i] & 0xF];
    }
    out[64] = 0;
}

static void ssh_kh_hostport(u32 ip, u16 port, char *out, int cap) {
    /* dotted-quad:port — the kernel shell passes a numeric or
     * DNS-resolved address, but the anchor must be stable, so we pin
     * the numeric form */
    char n[6];
    int o = 0;
    u64_to_str((ip >> 24) & 0xFF, n); for (int i = 0; n[i] && o < cap - 8; i++) out[o++] = n[i];
    out[o++] = '.';
    u64_to_str((ip >> 16) & 0xFF, n); for (int i = 0; n[i] && o < cap - 8; i++) out[o++] = n[i];
    out[o++] = '.';
    u64_to_str((ip >> 8) & 0xFF, n); for (int i = 0; n[i] && o < cap - 8; i++) out[o++] = n[i];
    out[o++] = '.';
    u64_to_str(ip & 0xFF, n); for (int i = 0; n[i] && o < cap - 8; i++) out[o++] = n[i];
    out[o++] = ':';
    u64_to_str(port, n);
    for (int i = 0; n[i] && o < cap - 1; i++) out[o++] = n[i];
    out[o] = 0;
}

/* Look up <ip>:<port> in /etc/ssh_known_hosts.
 * Returns 0 = entry found, *out_fp holds the recorded fingerprint.
 *         1 = no entry for this host (first use)
 *        -1 = read error (treated as first use, with a notice) */
static int ssh_kh_lookup(const char *hostport, u8 out_fp[32]) {
    int fd = fs_vfs_open(SSH_KNOWN_HOSTS_PATH, VFS_O_RDONLY);
    if (fd < 0) return 1;   /* no file yet = first use */
    static u8 khbuf[4096];
    int n = fs_vfs_read(fd, khbuf, (int)sizeof(khbuf) - 1);
    fs_vfs_close(fd);
    if (n <= 0) return 1;
    khbuf[n] = 0;
    int hl = (int)strlen(hostport);
    int p = 0;
    while (p < n) {
        int e = p;
        while (e < n && khbuf[e] != '\n') e++;
        /* line = "<hostport> <64 hex fp>" */
        int lp = p;
        int llen = e - p;
        khbuf[e] = 0;
        if (llen > hl + 1 + 64 &&
            memcmp(khbuf + lp, hostport, hl) == 0 && khbuf[lp + hl] == ' ') {
            const char *hex = (const char *)khbuf + lp + hl + 1;
            for (int i = 0; i < 32; i++) {
                int hi = hex[i * 2], lo = hex[i * 2 + 1];
                if (hi < '0' || lo < '0') goto nextline;
                int hv = (hi <= '9') ? hi - '0' : ((hi | 0x20) - 'a' + 10);
                int lv = (lo <= '9') ? lo - '0' : ((lo | 0x20) - 'a' + 10);
                if (hv > 15 || lv > 15) goto nextline;
                out_fp[i] = (u8)((hv << 4) | lv);
            }
            return 0;   /* found */
        }
nextline:
        p = e + 1;
    }
    return 1;
}

static void ssh_kh_record(const char *hostport, const u8 fp[32]) {
    int fd = fs_vfs_open(SSH_KNOWN_HOSTS_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND);
    if (fd < 0) {
        net_ssh_debug("[ssh] TOFU: cannot write /etc/ssh_known_hosts (read-only fs?)");
        return;
    }
    char line[128];
    int hl = (int)strlen(hostport);
    if (hl > 96) { fs_vfs_close(fd); return; }
    memcpy(line, hostport, hl);
    line[hl] = ' ';
    ssh_kh_fp_hex(fp, line + hl + 1);
    line[hl + 1 + 64] = '\n';
    fs_vfs_write(fd, line, hl + 1 + 64 + 1);
    fs_vfs_close(fd);
}

/* Verify the server host key signature over the exchange hash H (RFC 4253
 * S8). Host key blob K_S = string "ssh-rsa" + mpint e + mpint n; signature
 * blob = string "rsa-sha2-256" + string sig. Returns 0 = valid. On success
 * also prints the SHA-256 fingerprint of K_S (TOFU: the user is the trust
 * anchor, as in OpenSSH known_hosts first use). */
static int net_ssh_verify_host_signature(net_ssh_ctx_t *ctx, const u8 hash[32]) {
    if (ctx->server_host_key_len <= 0 || ctx->server_sig_len <= 0) {
        net_ssh_debug("[ssh] host signature missing");
        return -1;
    }
    const u8 *ks = ctx->server_host_key;
    int ksl = ctx->server_host_key_len;
    int off = 0;
    if (off + 4 > ksl) return -1;
    int alen = (ks[off] << 24) | (ks[off+1] << 16) | (ks[off+2] << 8) | ks[off+3];
    off += 4;
    if (alen != 7 || off + 7 > ksl || memcmp(ks + off, "ssh-rsa", 7) != 0) {
        net_ssh_debug("[ssh] host key algo not ssh-rsa");
        return -1;
    }
    off += 7;
    if (off + 4 > ksl) return -1;
    int elen = (ks[off] << 24) | (ks[off+1] << 16) | (ks[off+2] << 8) | ks[off+3];
    off += 4;
    if (elen <= 0 || elen > 8 || off + elen > ksl) return -1;
    const u8 *e = ks + off;
    off += elen;
    if (off + 4 > ksl) return -1;
    int nlen = (ks[off] << 24) | (ks[off+1] << 16) | (ks[off+2] << 8) | ks[off+3];
    off += 4;
    /* strip mpint sign byte */
    if (nlen > 0 && off < ksl && ks[off] == 0) { off++; nlen--; }
    if (nlen <= 0 || off + nlen > ksl || nlen != 256) {
        net_ssh_debug("[ssh] host key modulus not RSA-2048");
        return -1;
    }
    const u8 *n = ks + off;
    /* signature blob: string "rsa-sha2-256" + string sig */
    const u8 *sb = ctx->server_sig;
    int sl = ctx->server_sig_len;
    int sp = 0;
    if (sp + 4 > sl) return -1;
    int san = (sb[sp] << 24) | (sb[sp+1] << 16) | (sb[sp+2] << 8) | sb[sp+3];
    sp += 4;
    /* Servers pick either rsa-sha2-256 or rsa-sha2-512 (RFC 8332); both OK. */
    int sha_alg = 0;
    int hash_len = 0;
    if (san == 12 && sp + 12 <= sl && memcmp(sb + sp, "rsa-sha2-256", 12) == 0) {
        sha_alg = RSA_SHA256;
        hash_len = 32;
    } else if (san == 12 && sp + 12 <= sl && memcmp(sb + sp, "rsa-sha2-512", 12) == 0) {
        sha_alg = RSA_SHA512;
        hash_len = 64;
    } else {
        net_ssh_debug("[ssh] signature algo not rsa-sha2-256/512");
        return -1;
    }
    sp += 12;
    if (sp + 4 > sl) return -1;
    int siglen = (sb[sp] << 24) | (sb[sp+1] << 16) | (sb[sp+2] << 8) | sb[sp+3];
    sp += 4;
    if (siglen != nlen || sp + siglen > sl) {
        net_ssh_debug("[ssh] signature length mismatch");
        return -1;
    }
    /* RFC 4253 §8: the signature is computed over H itself. rsa-sha2-*
     * signs Hash(H) internally (RFC 8332), so the verify digest is
     * SHA-256(H) or SHA-512(H) — NOT H verbatim. */
    u8 digest[64];
    if (sha_alg == RSA_SHA512) sha512(hash, 32, digest);
    else sha256(hash, 32, digest);
    int ok = crypto_rsa_verify_pkcs1(n, nlen, e, elen, sha_alg, digest, hash_len,
                              sb + sp, siglen);
    if (ok != 1) {
        net_ssh_debug("[ssh] HOST SIGNATURE INVALID - disconnecting");
        return -1;
    }
    /* SHA-256 fingerprint of the host key blob (TOFU display) */
    u8 fp[32];
    sha256(ctx->server_host_key, ctx->server_host_key_len, fp);
    char line[120];
    strcpy(line, "[ssh] host key fingerprint (sha256): ");
    for (int i = 0; i < 24; i++) {
        const char hexd[] = "0123456789abcdef";
        char two[3] = { hexd[fp[i] >> 4], hexd[fp[i] & 0xF], 0 };
        strcat(line, two);
        if (i % 8 == 7) strcat(line, " ");
    }
    strcat(line, "\n");
    screen_console_puts(line);

    /* BUG-0073 FIX (A14-22): the signature alone proves nothing without
     * an anchor. Compare the fingerprint against /etc/ssh_known_hosts:
     *   first use  -> record it (TOFU),
     *   match      -> continue,
     *   MISMATCH   -> refuse the connection (MITM detected). */
    {
        char hostport[64];
        u8 recorded[32];
        char recorded_hex[65], presented_hex[65];
        ssh_kh_hostport(g_ssh_kh_ip, g_ssh_kh_port, hostport, (int)sizeof(hostport));
        int kr = ssh_kh_lookup(hostport, recorded);
        if (kr == 0) {
            if (memcmp(recorded, fp, 32) != 0) {
                ssh_kh_fp_hex(recorded, recorded_hex);
                ssh_kh_fp_hex(fp, presented_hex);
                screen_console_puts("[ssh] WARNING: HOST KEY CHANGED! (possible MITM)\n");
                strcpy(line, "[ssh] known_hosts: ");
                strcat(line, recorded_hex); strcat(line, "\n");
                screen_console_puts(line);
                strcpy(line, "[ssh] presented : ");
                strcat(line, presented_hex); strcat(line, "\n");
                screen_console_puts(line);
                screen_console_puts("[ssh] connection refused (delete the entry in /etc/ssh_known_hosts to re-trust)\n");
                return -1;
            }
            net_ssh_debug("[ssh] known_hosts fingerprint match");
        } else {
            ssh_kh_record(hostport, fp);
            strcpy(line, "[ssh] TOFU: first connection, key recorded for ");
            strcat(line, hostport); strcat(line, "\n");
            screen_console_puts(line);
        }
    }
    net_ssh_debug("[ssh] host signature verified");
    return 0;
}

/* Compute exchange hash H = SHA-256(V_C || V_S || I_C || I_S || K_S || e || f || K).
 * V_C/V_S/I_C/I_S/K_S are length-prefixed strings, e/f/K are mpints
 * (e/f as 32-byte strings for curve25519 per RFC 8731).
 * P0fix1 BUG-0012 (A14-06): every append is now bounds-checked against the
 * 4096B page; the old code blindly concatenated (V_C+V_S+I_C+I_S alone can
 * reach ~5.2KB) and wrote past the page. Returns 0 or -1 on overflow. */
static int net_ssh_compute_hash(net_ssh_ctx_t *ctx, u8 hash[32]) {
    /* Build hash input: strings (length-prefixed) + mpints */
    /* Use a page frame (4KB) since this can be large */
    u8 *buf = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!buf) return -1;
    const int cap = 4096;
    int p = 0;
    /* helper: room for `need` more bytes? */
#define OC_HASH_ROOM(need) ((cap - p) >= (need))
    /* V_C: client version banner (without \r\n) */
    int vlen = (int)strlen(ctx->client_banner);
    if (!OC_HASH_ROOM(4 + vlen)) goto overflow;
    buf[p++] = (u8)(vlen >> 24); buf[p++] = (u8)(vlen >> 16);
    buf[p++] = (u8)(vlen >> 8); buf[p++] = (u8)(vlen & 0xFF);
    for (int i = 0; ctx->client_banner[i]; i++) buf[p++] = ctx->client_banner[i];
    /* V_S: server version banner */
    vlen = (int)strlen(ctx->server_banner);
    if (!OC_HASH_ROOM(4 + vlen)) goto overflow;
    buf[p++] = (u8)(vlen >> 24); buf[p++] = (u8)(vlen >> 16);
    buf[p++] = (u8)(vlen >> 8); buf[p++] = (u8)(vlen & 0xFF);
    for (int i = 0; ctx->server_banner[i]; i++) buf[p++] = ctx->server_banner[i];
    /* I_C: client KEXINIT payload (msg_type + payload, excluding padding_length + padding) */
    int ic_len = ctx->client_kexinit_len;
    if (ic_len < 0 || !OC_HASH_ROOM(4 + ic_len)) goto overflow;
    buf[p++] = (u8)(ic_len >> 24); buf[p++] = (u8)(ic_len >> 16);
    buf[p++] = (u8)(ic_len >> 8); buf[p++] = (u8)(ic_len & 0xFF);
    memcpy(buf + p, ctx->client_kexinit, ic_len); p += ic_len;
    /* I_S: server KEXINIT payload */
    int is_len = ctx->server_kexinit_len;
    if (is_len < 0 || !OC_HASH_ROOM(4 + is_len)) goto overflow;
    buf[p++] = (u8)(is_len >> 24); buf[p++] = (u8)(is_len >> 16);
    buf[p++] = (u8)(is_len >> 8); buf[p++] = (u8)(is_len & 0xFF);
    memcpy(buf + p, ctx->server_kexinit, is_len); p += is_len;
    /* K_S: server host key blob — saved from KEXDH_REPLY */
    int ks_total = ctx->server_host_key_len;
    if (ks_total < 0 || !OC_HASH_ROOM(4 + ks_total)) goto overflow;
    buf[p++] = (u8)(ks_total >> 24); buf[p++] = (u8)(ks_total >> 16);
    buf[p++] = (u8)(ks_total >> 8); buf[p++] = (u8)(ks_total & 0xFF);
    if (ks_total > 0) {
        memcpy(buf + p, ctx->server_host_key, ks_total);
        p += ks_total;
    }
    if (!OC_HASH_ROOM(2 * (4 + SSH_DH_BYTES) + 4 + SSH_DH_BYTES + 4)) goto overflow;
    if (ctx->kex_curve25519) {
        /* RFC 8731: e and f are 32-byte STRINGS in the exchange hash
         * (no mpint sign-extension), K is a standard mpint. */
        buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 32;
        memcpy(buf + p, ctx->client_pub, 32); p += 32;
        buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 32;
        memcpy(buf + p, ctx->server_pub + SSH_DH_BYTES - 32, 32); p += 32;
        net_ssh_write_mpint(buf, &p, ctx->shared_secret, SSH_DH_BYTES);
    } else {
        /* e: client DH public value (as mpint: length + bytes) */
        net_ssh_write_mpint(buf, &p, ctx->client_pub, SSH_DH_BYTES);
        /* f: server DH public value */
        net_ssh_write_mpint(buf, &p, ctx->server_pub, SSH_DH_BYTES);
        /* K: shared secret */
        net_ssh_write_mpint(buf, &p, ctx->shared_secret, SSH_DH_BYTES);
    }
#undef OC_HASH_ROOM

    sha256(buf, p, hash);
    mem_pmm_free_frame((u64)(uintptr_t)buf);

    /* WP-09 debug: print hash input components for comparison with server */
    {
        char dbg[160];
        strcpy(dbg, "[ssh] H input: V_C=");
        char n[10];
        int vc_len = (int)strlen(ctx->client_banner);
        u64_to_str((u64)vc_len, n); strcat(dbg, n);
        strcat(dbg, " V_S=");
        int vs_len = (int)strlen(ctx->server_banner);
        u64_to_str((u64)vs_len, n); strcat(dbg, n);
        strcat(dbg, " I_C=");
        u64_to_str((u64)ctx->client_kexinit_len, n); strcat(dbg, n);
        strcat(dbg, " I_S=");
        u64_to_str((u64)ctx->server_kexinit_len, n); strcat(dbg, n);
        strcat(dbg, " K_S=");
        u64_to_str((u64)ctx->server_host_key_len, n); strcat(dbg, n);
        strcat(dbg, "\n"); screen_console_puts(dbg);
    }
    net_ssh_debug_hex("[ssh] I_C[0..15]: ", ctx->client_kexinit, 16);
    net_ssh_debug_hex("[ssh] I_S[0..15]: ", ctx->server_kexinit, 16);
    net_ssh_debug_hex("[ssh] H (first 16): ", hash, 16);

    /* WP-09 debug: print K and H for comparison with server.
     * (Derived keys are printed after net_ssh_derive_keys(), see below.) */
    net_ssh_debug_hex("[ssh] K (first 8): ", ctx->shared_secret, 8);
    return 0;

overflow:
    mem_pmm_free_frame((u64)(uintptr_t)buf);
    screen_console_puts("[ssh] exchange hash input exceeds 4096B page\n");
    return -1;
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
static void net_ssh_derive_keys(net_ssh_ctx_t *ctx) {
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
    memcpy(k_mpint + 4 + need_zero, ctx->shared_secret + start, n);
    int k_mpint_len = 4 + k_total;

    /* For each key, compute K1 = SHA-256(K_mpint || H(32) || X(1) || session_id(32)) */
    u8 msg[512];
    u8 digest[32];

    #define COMPUTE_KEY(X_char, out_buf, out_len) do { \
        int mp = 0; \
        memcpy(msg + mp, k_mpint, k_mpint_len); mp += k_mpint_len; \
        /* BUG-0219: H is the exchange hash of the CURRENT exchange (equal
         * to session_id for the initial exchange, a NEW hash after rekey).
         * session_id itself stays the first H forever (RFC 4253 S8). */ \
        memcpy(msg + mp, ctx->exchange_hash, 32); mp += 32;  /* H */ \
        msg[mp++] = X_char;  /* X */ \
        memcpy(msg + mp, ctx->session_id, 32); mp += 32;  /* session_id */ \
        sha256(msg, mp, digest); \
        memcpy(out_buf, digest, out_len); \
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
static int net_ssh_send_newkeys(net_ssh_ctx_t *ctx, int encrypted) {
    if (encrypted)
        return net_ssh_send_packet_encrypted(ctx, SSH_MSG_NEWKEYS, NULL, 0);
    return net_ssh_send_packet_unencrypted(ctx, SSH_MSG_NEWKEYS, NULL, 0);
}

/* Receive NEWKEYS */
static int net_ssh_recv_newkeys(net_ssh_ctx_t *ctx, int encrypted) {
    u8 payload[16];
    int payload_len = sizeof(payload);
    u8 msg_type;
    int rc = encrypted ? net_ssh_recv_packet_encrypted(ctx, &msg_type, payload, &payload_len)
                       : net_ssh_recv_packet_unencrypted(ctx, &msg_type, payload, &payload_len);
    if (rc < 0) return -1;
    if (msg_type != SSH_MSG_NEWKEYS) return -2;
    return 0;
}

int net_ssh_connect(u32 ip, u16 port, const char *username, const char *password) {
    net_ssh_ctx_t *ctx = &g_ssh_ctx;
    memset(ctx, 0, sizeof(*ctx));
    /* Save username/password for net_ssh_exec's USERAUTH step */
    if (username) strncpy(ctx->username, username, sizeof(ctx->username)-1);
    if (password) strncpy(ctx->password, password, sizeof(ctx->password)-1);
    ctx->kex_curve25519 = 0;
    ctx->cipher_ctr = 0;
    ctx->auth_publickey = (password == 0 || password[0] == 0);
    /* BUG-0073: remember the endpoint for the known_hosts anchor check */
    g_ssh_kh_ip = ip;
    g_ssh_kh_port = port;

    net_ssh_debug("[ssh] connecting...");
    ctx->net_tcp_sock = net_socket(SOCK_TCP);
    if (ctx->net_tcp_sock < 0) {
        net_ssh_debug("[ssh] net_socket failed");
        return net_ssh_connect_fail(ctx, -1);
    }
    if (net_connect(ctx->net_tcp_sock, ip, port) < 0) {
        net_ssh_debug("[ssh] net_connect failed");
        return net_ssh_connect_fail(ctx, -1);
    }
    net_ssh_debug("[ssh] TCP connected");

    /* Version banner exchange */
    strcpy(ctx->client_banner, "SSH-2.0-OpenCubeOS_WP-09");
    if (net_ssh_send_version(ctx) < 0) {
        net_ssh_debug("[ssh] failed to send version banner");
        return net_ssh_connect_fail(ctx, -2);
    }
    if (net_ssh_recv_version(ctx) < 0) {
        net_ssh_debug("[ssh] failed to receive server banner");
        return net_ssh_connect_fail(ctx, -3);
    }
    net_ssh_debug("[ssh] version banner exchange OK");
    net_ssh_debug(ctx->server_banner);

    /* KEXINIT exchange */
    if (net_ssh_send_kexinit(ctx, 0) < 0) {
        net_ssh_debug("[ssh] failed to send KEXINIT");
        return net_ssh_connect_fail(ctx, -4);
    }
    if (net_ssh_recv_kexinit(ctx, 0) < 0) {
        net_ssh_debug("[ssh] failed to receive KEXINIT");
        return net_ssh_connect_fail(ctx, -5);
    }
    net_ssh_debug("[ssh] KEXINIT exchange OK");

    /* Key exchange: curve25519-sha256 (modern default) or group14 */
    if (ctx->kex_curve25519) {
        if (net_ssh_send_kex_ecdh_init(ctx, 0) < 0) {
            net_ssh_debug("[ssh] failed to send KEX_ECDH_INIT");
            return net_ssh_connect_fail(ctx, -6);
        }
        if (net_ssh_recv_kex_reply(ctx, 0) < 0) {
            net_ssh_debug("[ssh] failed to receive KEX_ECDH_REPLY");
            return net_ssh_connect_fail(ctx, -7);
        }
        /* K = X25519(x, f): 32-byte shared secret, right-aligned into
         * shared_secret so the mpint encoder sees a plain big number. */
        memset(ctx->shared_secret, 0, SSH_DH_BYTES);
        if (crypto_x25519_shared(ctx->client_priv, ctx->server_pub + SSH_DH_BYTES - 32,
                          ctx->shared_secret + SSH_DH_BYTES - 32) != 0) {
            net_ssh_debug("[ssh] x25519 shared secret computation failed");
            return net_ssh_connect_fail(ctx, -7);
        }
        if (net_ssh_secret_is_zero(ctx->shared_secret + SSH_DH_BYTES - 32, 32)) {
            /* BUG-0221 (A14-35): RFC 7748 S6.1 - an all-zero output means
             * the peer's public key was invalid (small-order point); the
             * derived session keys would be attacker-derivable. */
            net_ssh_debug("[ssh] x25519 shared secret is all-zero (invalid server key)");
            return net_ssh_connect_fail(ctx, -7);
        }
        net_ssh_debug("[ssh] curve25519 KEX complete (no modexp needed)");
    } else {
        if (net_ssh_send_kexdh_init(ctx, 0) < 0) {
            net_ssh_debug("[ssh] failed to send KEXDH_INIT");
            return net_ssh_connect_fail(ctx, -6);
        }
        if (net_ssh_recv_kexdh_reply(ctx, 0) < 0) {
            net_ssh_debug("[ssh] failed to receive KEXDH_REPLY");
            return net_ssh_connect_fail(ctx, -7);
        }
        /* Compute shared secret K = f^x mod p */
        net_ssh_debug("[ssh] computing K = f^x mod p (DH modexp, ~60s)...");
        u64 t0 = core_timer_ticks();
        crypto_dh_modexp_n(ctx->server_pub, ctx->client_priv, crypto_dh_group14_prime, ctx->shared_secret, 256);
        u64 t1 = core_timer_ticks();
        u64 ms = (t1 - t0) * 1000 / (u64)OC_TIMER_HZ;
        char buf[80];
        strcpy(buf, "[ssh]   DH modexp time: ");
        char num[10];
        u64_to_str(ms, num);
        strcat(buf, num);
        strcat(buf, " ms\n");
        screen_console_puts(buf);
    }

    /* Compute exchange hash H */
    u8 hash[32];
    if (net_ssh_compute_hash(ctx, hash) < 0) {
        /* BUG-0012: input did not fit the page -- abort the exchange. */
        return net_ssh_connect_fail(ctx, -14);
    }
    /* RFC 4253 §8: verify the server signature over H with the host key
     * from K_S BEFORE deriving/using any keys. */
    if (net_ssh_verify_host_signature(ctx, hash) < 0) {
        net_ssh_debug("[ssh] host key verification failed");
        return net_ssh_connect_fail(ctx, -14);
    }
    memcpy(ctx->exchange_hash, hash, 32);  /* H of the current exchange */
    memcpy(ctx->session_id, hash, 32);     /* session_id = FIRST H, set once */
    ctx->session_id_set = 1;
    net_ssh_debug_hex("[ssh]   session_id (first 8): ", ctx->session_id, 8);

    /* Derive keys */
    net_ssh_derive_keys(ctx);
    /* WP-09 debug: now print the real derived key material. */
    net_ssh_debug_hex("[ssh] enc_key_c2s (first 8): ", ctx->enc_key_c2s, 8);
    net_ssh_debug_hex("[ssh] iv_c2s (first 8): ", ctx->initial_iv_c2s, 8);
    /* WP-09 fix: initialize the rolling CBC IVs (first packet uses the
     * initial IV; every subsequent packet uses the last ciphertext block
     * of the previous packet, RFC 4253 §6.3). */
    memcpy(ctx->iv_c2s_next, ctx->initial_iv_c2s, 16);
    memcpy(ctx->iv_s2c_next, ctx->initial_iv_s2c, 16);
    /* RFC 4344: aes128-ctr uses a continuous counter stream per direction;
     * packets are block-aligned so each packet starts on a counter block. */
    memcpy(ctx->ctr_c2s, ctx->initial_iv_c2s, 16);
    memcpy(ctx->ctr_s2c, ctx->initial_iv_s2c, 16);

    /* NEWKEYS exchange */
    if (net_ssh_send_newkeys(ctx, 0) < 0) {
        net_ssh_debug("[ssh] failed to send NEWKEYS");
        return net_ssh_connect_fail(ctx, -8);
    }
    if (net_ssh_recv_newkeys(ctx, 0) < 0) {
        net_ssh_debug("[ssh] failed to receive NEWKEYS");
        return net_ssh_connect_fail(ctx, -9);
    }
    ctx->encrypted = 1;
    /* BUG-0220 (A14-34): strict-kex - both directions restart at 0 after
     * the final NEWKEYS of the initial exchange (and every rekey). */
    net_ssh_reset_seq(ctx);
    net_ssh_debug("[ssh] NEWKEYS exchange OK — encrypted mode active");
    net_ssh_debug("[ssh] SSH transport layer established (KEX + NEWKEYS complete)");

    /* BUG-0235 (A14-49): RFC 4252 S5 - the client MUST request the
     * "ssh-userauth" service (SSH_MSG_SERVICE_REQUEST) and receive
     * SSH_MSG_SERVICE_ACCEPT before its first USERAUTH_REQUEST. The old
     * code jumped straight to USERAUTH_REQUEST; lenient peers (paramiko)
     * accepted it, RFC-strict servers (OpenSSH) reject it. */
    {
        u8 spay[16];
        int sp = 0;
        const char *svcname = "ssh-userauth";
        int sname_len = 12;
        spay[sp++] = (u8)(sname_len >> 24); spay[sp++] = (u8)(sname_len >> 16);
        spay[sp++] = (u8)(sname_len >> 8); spay[sp++] = (u8)(sname_len & 0xFF);
        for (int i = 0; i < sname_len; i++) spay[sp++] = svcname[i];
        if (net_ssh_send_packet_encrypted(ctx, SSH_MSG_SERVICE_REQUEST, spay, sp) < 0) {
            net_ssh_debug("[ssh] failed to send SERVICE_REQUEST");
            return net_ssh_connect_fail(ctx, -15);
        }
        net_ssh_debug("[ssh] sent SERVICE_REQUEST (ssh-userauth)");
        /* Wait for SERVICE_ACCEPT within the existing receive timeouts:
         * a slow or silent server falls through to USERAUTH, which still
         * reports a clear error if the service exchange was mandatory.
         * Anything else that arrives first (e.g. USERAUTH_BANNER) is
         * tolerated and skipped. */
        u64 svc_start = core_timer_ticks();
        for (;;) {
            u8 rtype;
            u8 rbuf[64];
            int rlen = (int)sizeof(rbuf);
            if (net_ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
                net_ssh_debug("[ssh] no SERVICE_ACCEPT received (proceeding)");
                break;
            }
            if (rtype == SSH_MSG_SERVICE_ACCEPT) {
                net_ssh_debug("[ssh] SERVICE_ACCEPT received");
                break;
            }
            if (core_timer_ticks() - svc_start > 2 * 800) break;
        }
    }

    /* WP-09 batch 13: Send USERAUTH_REQUEST (password or publickey) */
    /* password format:
     *   byte 50, string user, string "ssh-connection", string "password",
     *   byte FALSE, string password
     * publickey format (RFC 4252 §7):
     *   byte 50, string user, string "ssh-connection", string "publickey",
     *   boolean TRUE, string algorithm("rsa-sha2-256"), string blob,
     *   string signature( "rsa-sha2-256" + string sig )
     * sig = RSASSA-PKCS1-v1_5-SHA256 over
     *   string session_id || byte 50 || string user || string "ssh-connection" ||
     *   string "publickey" || boolean TRUE || string algo || string blob
     * BUG-0075: the kernel signs with the per-installation client key
     * from /etc/ssh_client_key; the sshd trusts only moduli listed in
     * ITS /etc/ssh_authorized_keys. */
    if (ctx->auth_publickey) {
        u8 payload[1024];
        int p = 0;
        /* pubkey blob: string "ssh-rsa" + mpint e + mpint n */
        u8 blob[512];
        int bp = 0;
        const char *kname = "ssh-rsa";
        int klen2 = 7;
        blob[bp++] = 0; blob[bp++] = 0; blob[bp++] = 0; blob[bp++] = klen2;
        for (int i = 0; i < klen2; i++) blob[bp++] = kname[i];
        /* BUG-0075 FIX (A14-24): the client identity key comes from
         * /etc/ssh_client_key (n[256] || d[256], e fixed 65537). The old
         * code signed with the EMBEDDED "universal" private key that
         * ships in the public source tree — with it, publickey auth was
         * world-access on any sshd that trusted it. No key file => the
         * client cannot do publickey auth and falls back to password. */
        u8 client_n[256], client_d[256];
        int have_client_key = 0;
        {
            int kfd = fs_vfs_open("/etc/ssh_client_key", VFS_O_RDONLY);
            if (kfd >= 0) {
                u8 kb[512];
                int kn = fs_vfs_read(kfd, kb, 512);
                fs_vfs_close(kfd);
                if (kn == 512) {
                    memcpy(client_n, kb, 256);
                    memcpy(client_d, kb + 256, 256);
                    have_client_key = 1;
                }
            }
        }
        if (!have_client_key) {
            net_ssh_debug("[ssh] no /etc/ssh_client_key - publickey auth unavailable, use password");
            return net_ssh_connect_fail(ctx, -14);
        }
        u8 e_m[4];
        e_m[0] = 0; e_m[1] = 0x01; e_m[2] = 0x00; e_m[3] = 0x01;
        net_ssh_write_mpint(blob, &bp, e_m, 4);   /* leading zero stripped -> 0x010001 */
        net_ssh_write_mpint(blob, &bp, client_n, 256);
        /* signed data: string session_id || byte 50 || user ||
         * service || "publickey" || TRUE || algo || blob */
        u8 sdata[2048];
        int sp = 0;
        sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 32;
        memcpy(sdata + sp, ctx->session_id, 32); sp += 32;
        sdata[sp++] = 50;                                       /* USERAUTH_REQUEST */
        int ulen = (int)strlen(ctx->username);
        sdata[sp++] = (u8)(ulen >> 24); sdata[sp++] = (u8)(ulen >> 16);
        sdata[sp++] = (u8)(ulen >> 8); sdata[sp++] = (u8)(ulen & 0xFF);
        for (int i = 0; i < ulen; i++) sdata[sp++] = ctx->username[i];
        const char *svc = "ssh-connection";
        sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 14;
        for (int i = 0; i < 14; i++) sdata[sp++] = svc[i];
        const char *mth = "publickey";
        sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 9;
        for (int i = 0; i < 9; i++) sdata[sp++] = mth[i];
        sdata[sp++] = 1;                                        /* TRUE */
        const char *algn = "rsa-sha2-256";
        sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 0; sdata[sp++] = 12;
        for (int i = 0; i < 12; i++) sdata[sp++] = algn[i];
        sdata[sp++] = (u8)(bp >> 24); sdata[sp++] = (u8)(bp >> 16);
        sdata[sp++] = (u8)(bp >> 8); sdata[sp++] = (u8)(bp & 0xFF);
        memcpy(sdata + sp, blob, bp); sp += bp;
        /* signature: RSASSA-PKCS1-v1_5-SHA256 */
        u8 em[256];
        memset(em, 0xFF, 256);
        em[0] = 0x00; em[1] = 0x01;
        static const u8 dinfo[] = {
            0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,
            0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
        };
        u8 sdigest[32];
        sha256(sdata, sp, sdigest);
        memcpy(em + (256 - 51), dinfo, 19);
        memcpy(em + (256 - 32), sdigest, 32);
        em[256 - 52] = 0x00;
        u8 sig[256];
        crypto_dh_modexp_n(em, client_d, client_n, sig, 256);
        /* assemble USERAUTH_REQUEST payload */
        ulen = (int)strlen(ctx->username);
        payload[p++] = (u8)(ulen >> 24); payload[p++] = (u8)(ulen >> 16);
        payload[p++] = (u8)(ulen >> 8); payload[p++] = (u8)(ulen & 0xFF);
        for (int i = 0; i < ulen; i++) payload[p++] = ctx->username[i];
        const char *svc2 = "ssh-connection";
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 14;
        for (int i = 0; i < 14; i++) payload[p++] = svc2[i];
        const char *mth2 = "publickey";
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 9;
        for (int i = 0; i < 9; i++) payload[p++] = mth2[i];
        payload[p++] = 1;                                       /* TRUE */
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 12;
        for (int i = 0; i < 12; i++) payload[p++] = algn[i];
        payload[p++] = (u8)(bp >> 24); payload[p++] = (u8)(bp >> 16);
        payload[p++] = (u8)(bp >> 8); payload[p++] = (u8)(bp & 0xFF);
        memcpy(payload + p, blob, bp); p += bp;
        /* RFC 4252: the signature field is a STRING wrapping
         * (string algorithm || string sig) — two nesting levels. */
        int sig_blob_len = 4 + 12 + 4 + 256;
        payload[p++] = (u8)(sig_blob_len >> 24); payload[p++] = (u8)(sig_blob_len >> 16);
        payload[p++] = (u8)(sig_blob_len >> 8); payload[p++] = (u8)(sig_blob_len & 0xFF);
        const char *algn2 = "rsa-sha2-256";
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 12;
        for (int i = 0; i < 12; i++) payload[p++] = algn2[i];
        payload[p++] = 0; payload[p++] = 0; payload[p++] = 1; payload[p++] = 0;  /* 256 */
        memcpy(payload + p, sig, 256); p += 256;

        extern int net_ssh_send_packet_encrypted(net_ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len);
        if (net_ssh_send_packet_encrypted(ctx, SSH_MSG_USERAUTH_REQ, payload, p) < 0) {
            net_ssh_debug("[ssh] failed to send USERAUTH_REQUEST");
            return net_ssh_connect_fail(ctx, -10);
        }
        net_ssh_debug("[ssh] sent USERAUTH_REQUEST (publickey rsa-sha2-256)");
        {
            u8 rtype;
            u8 rbuf[256];
            int rlen = sizeof(rbuf);
            extern int net_ssh_recv_packet_encrypted(net_ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len);
            if (net_ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
                net_ssh_debug("[ssh] failed to receive USERAUTH response");
                return net_ssh_connect_fail(ctx, -11);
            }
            if (rtype == SSH_MSG_USERAUTH_SUCCESS) {
                net_ssh_debug("[ssh] USERAUTH_SUCCESS — authenticated (publickey)");
            } else if (rtype == SSH_MSG_USERAUTH_FAILURE) {
                net_ssh_debug("[ssh] USERAUTH_FAILURE — public key rejected");
                return net_ssh_connect_fail(ctx, -12);
            } else {
                char b[60]; strcpy(b, "[ssh] unexpected msg type ");
                char num2[10]; u64_to_str((u64)rtype, num2);
                strcat(b, num2); strcat(b, "\n");
                screen_console_puts(b);
                return net_ssh_connect_fail(ctx, -13);
            }
        }
    } else {
        u8 payload[256];
        int p = 0;
        /* msg_type is added by net_ssh_send_packet_encrypted, NOT in payload */
        /* user name */
        int ulen = (int)strlen(ctx->username);
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
        int plen = (int)strlen(ctx->password);
        payload[p++] = (u8)(plen >> 24); payload[p++] = (u8)(plen >> 16);
        payload[p++] = (u8)(plen >> 8); payload[p++] = (u8)(plen & 0xFF);
        for (int i = 0; i < plen; i++) payload[p++] = ctx->password[i];

        extern int net_ssh_send_packet_encrypted(net_ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len);
        if (net_ssh_send_packet_encrypted(ctx, SSH_MSG_USERAUTH_REQ, payload, p) < 0) {
            net_ssh_debug("[ssh] failed to send USERAUTH_REQUEST");
            return net_ssh_connect_fail(ctx, -10);
        }
        net_ssh_debug("[ssh] sent USERAUTH_REQUEST (password)");

        /* Read response: USERAUTH_SUCCESS (52) or USERAUTH_FAILURE (53) */
        u8 rtype;
        u8 rbuf[256];
        int rlen = sizeof(rbuf);
        extern int net_ssh_recv_packet_encrypted(net_ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len);
        if (net_ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            net_ssh_debug("[ssh] failed to receive USERAUTH response");
            return net_ssh_connect_fail(ctx, -11);
        }
        if (rtype == SSH_MSG_USERAUTH_SUCCESS) {
            net_ssh_debug("[ssh] USERAUTH_SUCCESS — authenticated");
        } else if (rtype == SSH_MSG_USERAUTH_FAILURE) {
            net_ssh_debug("[ssh] USERAUTH_FAILURE — wrong password");
            return net_ssh_connect_fail(ctx, -12);
        } else {
            char b[60]; strcpy(b, "[ssh] unexpected msg type ");
            char num2[10]; u64_to_str((u64)rtype, num2);
            strcat(b, num2); strcat(b, "\n");
            screen_console_puts(b);
            return net_ssh_connect_fail(ctx, -13);
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

int net_ssh_send_packet_encrypted(net_ssh_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
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
    if (payload_len > 0) memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_count);

    /* Compute MAC over: seq(4) || unencrypted (total_unenc bytes) */
    static u8 mac_input[8192];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->write_seq >> 24);
    mac_input[mi++] = (u8)(ctx->write_seq >> 16);
    mac_input[mi++] = (u8)(ctx->write_seq >> 8);
    mac_input[mi++] = (u8)(ctx->write_seq & 0xFF);
    if (total_unenc > (int)sizeof(mac_input) - 4) return -1;
    memcpy(mac_input + mi, pkt, total_unenc); mi += total_unenc;
    u8 mac[32];
    crypto_hmac_sha256(ctx->mac_key_c2s, 32, mac_input, mi, mac);

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
    if (ctx->cipher_ctr) {
        /* aes128-ctr: one continuous big-endian counter stream (RFC 4344) */
        crypto_aes128_ctr_encrypt(ctx->enc_key_c2s, ctx->ctr_c2s, pkt, total_unenc, enc);
        for (int blk = 0; blk < total_unenc / 16; blk++)
            for (int ci = 15; ci >= 0; ci--)
                if (++ctx->ctr_c2s[ci] != 0) break;
    } else {
        crypto_aes128_cbc_encrypt(ctx->enc_key_c2s, ctx->iv_c2s_next, pkt, total_unenc, enc);
        /* WP-09 fix: CBC chaining — the IV for the next outgoing packet is the
         * last ciphertext block of this one. */
        memcpy(ctx->iv_c2s_next, enc + total_unenc - 16, 16);
    }

    /* Reassemble: encrypted (total_unenc bytes) + MAC (32 bytes) */
    static u8 out[16384];
    if (total_send > (int)sizeof(out)) return -1;
    memcpy(out, enc, total_unenc);
    memcpy(out + total_unenc, mac, mac_size);

    int rc = net_send(ctx->net_tcp_sock, out, total_send);
    if (rc < 0) {
        net_ssh_debug("[ssh] net_send failed in ssh_send_packet_encrypted");
    }
    ctx->bytes_sent += (u64)total_send;   /* BUG-0219: rekey accounting */
    ctx->write_seq++;
    return rc;
}

int net_ssh_recv_packet_encrypted(net_ssh_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
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
    u64 start = core_timer_ticks();
    while (n < block_size) {
        int got = net_recv(ctx->net_tcp_sock, first_block + n, block_size - n);
        if (got > 0) { n += got; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 800) return -1;
        }
    }

    /* Decrypt first block to get packet_length */
    u8 dec_first[16];
    if (ctx->cipher_ctr) {
        crypto_aes128_ctr_encrypt(ctx->enc_key_s2c, ctx->ctr_s2c, first_block, block_size, dec_first);
        for (int ci = 15; ci >= 0; ci--)
            if (++ctx->ctr_s2c[ci] != 0) break;
    } else {
        /* WP-09 fix: use the rolling IV (equals the last ciphertext block of the
         * previous packet), NOT the fixed initial IV. */
        crypto_aes128_cbc_decrypt(ctx->enc_key_s2c, ctx->iv_s2c_next, first_block, block_size, dec_first);
    }
    int packet_length = ((int)dec_first[0] << 24) | ((int)dec_first[1] << 16) |
                        ((int)dec_first[2] << 8) | dec_first[3];
    /* P0fix1 BUG-0013 (A14-07): the old limit of 35000 exceeds every buffer
     * used below (rest_buf/dec_rest/body/mac_input are all 16384B). Cap the
     * packet at what the receive page can hold (matching the plaintext
     * path) and validate before any byte is received. */
    if (packet_length < 1 || packet_length > 3500) {
        net_ssh_debug("[ssh] invalid packet_length in encrypted packet");
        return -1;
    }

    /* Read remaining encrypted body + MAC */
    int leftover = block_size - 4;  /* 12 bytes already in dec_first */
    int remaining = packet_length - leftover;  /* bytes to read more */
    if (remaining < 0 || remaining % block_size != 0) {
        net_ssh_debug("[ssh] Invalid packet blocking (from server)");
        return -1;
    }

    static u8 rest_buf[16384];
    /* P0fix1 BUG-0013 (A14-07): bounds-check BEFORE receiving. */
    if (remaining + mac_size > (int)sizeof(rest_buf)) return -1;
    n = 0;
    while (n < remaining + mac_size) {
        int r = net_recv(ctx->net_tcp_sock, rest_buf + n, (remaining + mac_size) - n);
        if (r > 0) { n += r; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 800) return -1;
        }
    }

    /* Decrypt remaining body (encrypted part, not MAC) */
    static u8 dec_rest[16384];
    if (ctx->cipher_ctr) {
        if (remaining > 0) {
            crypto_aes128_ctr_encrypt(ctx->enc_key_s2c, ctx->ctr_s2c, rest_buf, remaining, dec_rest);
            for (int blk = 0; blk < remaining / 16; blk++)
                for (int ci = 15; ci >= 0; ci--)
                    if (++ctx->ctr_s2c[ci] != 0) break;
        }
    } else {
        if (remaining > 0) {
            /* WP-09 fix: the IV for the rest of THIS packet is the first
             * ciphertext block we just read (first_block) — CBC chains block to
             * block within the packet too. */
            crypto_aes128_cbc_decrypt(ctx->enc_key_s2c, first_block, rest_buf, remaining, dec_rest);
        }
        /* WP-09 fix: roll the incoming IV = last ciphertext block of this packet
         * (rest_buf's last block if any, else first_block itself). */
        if (remaining >= 16) {
            memcpy(ctx->iv_s2c_next, rest_buf + remaining - 16, 16);
        } else {
            memcpy(ctx->iv_s2c_next, first_block, 16);
        }
    }

    /* Combine decrypted body: dec_first[4..15] + dec_rest[0..remaining-1] */
    static u8 body[16384];
    int body_len = leftover + remaining;  /* = packet_length */
    if (body_len > (int)sizeof(body)) return -1;
    memcpy(body, dec_first + 4, leftover);  /* first 12 decrypted body bytes */
    if (remaining > 0) memcpy(body + leftover, dec_rest, remaining);

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
    memcpy(mac_input + mi, body, body_len); mi += body_len;
    u8 expected_mac[32];
    crypto_hmac_sha256(ctx->mac_key_s2c, 32, mac_input, mi, expected_mac);
    int mac_ok = 1;
    for (int i = 0; i < mac_size; i++) {
        if (rest_buf[remaining + i] != expected_mac[i]) { mac_ok = 0; break; }
    }
    if (!mac_ok) {
        net_ssh_debug("[ssh] MAC verify FAIL on incoming encrypted packet");
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
        char dbg2[80]; strcpy(dbg2, "[ssh]   recv encrypted: type=");
        char nn[10]; u64_to_str((u64)*msg_type, nn); strcat(dbg2, nn);
        strcat(dbg2, " plen="); u64_to_str((u64)plen, nn); strcat(dbg2, nn);
        strcat(dbg2, "\n"); screen_console_puts(dbg2);
    }
    memcpy(payload, body + 2, plen);  /* skip padding_length + msg_type */
    *payload_len = plen;

    ctx->read_seq++;
    ctx->bytes_received += (u64)(4 + packet_length + mac_size);  /* BUG-0219 */
    return 0;
}

/* BUG-0219 (A14-33): client-side byte-threshold rekey. The full KEX
 * machinery runs again (KEXINIT -> KEX -> NEWKEYS), this time under the
 * CURRENTLY ACTIVE keys; the new keys apply after NEWKEYS (RFC 4253 S9).
 * The host key is re-verified (its signature covers the new H) and
 * session_id stays the FIRST exchange hash. While the exchange is in
 * progress no other packets are accepted: a data packet mixed into the
 * key exchange desynchronizes both cipher streams, so the documented
 * policy is drop-with-disconnect (the caller tears the session down when
 * this returns non-zero). */
static int net_ssh_client_rekey(net_ssh_ctx_t *ctx) {
    net_ssh_debug("[ssh] rekey threshold reached - starting key re-exchange");
    ctx->rekey_in_progress = 1;
    if (net_ssh_send_kexinit(ctx, 1) < 0) goto fail;
    if (net_ssh_recv_kexinit(ctx, 1) < 0) goto fail;
    if (ctx->kex_curve25519) {
        if (net_ssh_send_kex_ecdh_init(ctx, 1) < 0) goto fail;
        if (net_ssh_recv_kex_reply(ctx, 1) < 0) goto fail;
        memset(ctx->shared_secret, 0, SSH_DH_BYTES);
        if (crypto_x25519_shared(ctx->client_priv, ctx->server_pub + SSH_DH_BYTES - 32,
                                 ctx->shared_secret + SSH_DH_BYTES - 32) != 0) goto fail;
        if (net_ssh_secret_is_zero(ctx->shared_secret + SSH_DH_BYTES - 32, 32)) goto fail;
    } else {
        if (net_ssh_send_kexdh_init(ctx, 1) < 0) goto fail;
        if (net_ssh_recv_kexdh_reply(ctx, 1) < 0) goto fail;
        crypto_dh_modexp_n(ctx->server_pub, ctx->client_priv, crypto_dh_group14_prime,
                           ctx->shared_secret, SSH_DH_BYTES);
    }
    u8 hash[32];
    if (net_ssh_compute_hash(ctx, hash) < 0) goto fail;
    if (net_ssh_verify_host_signature(ctx, hash) < 0) goto fail;
    memcpy(ctx->exchange_hash, hash, 32);   /* new H; session_id unchanged */
    /* NEWKEYS under the OLD keys: derive only AFTER both NEWKEYS so the
     * NEWKEYS packets themselves are still framed with the old keys. */
    if (net_ssh_send_newkeys(ctx, 1) < 0) goto fail;
    if (net_ssh_recv_newkeys(ctx, 1) < 0) goto fail;
    net_ssh_derive_keys(ctx);
    memcpy(ctx->iv_c2s_next, ctx->initial_iv_c2s, 16);
    memcpy(ctx->iv_s2c_next, ctx->initial_iv_s2c, 16);
    memcpy(ctx->ctr_c2s, ctx->initial_iv_c2s, 16);
    memcpy(ctx->ctr_s2c, ctx->initial_iv_s2c, 16);
    ctx->encrypted = 1;
    net_ssh_reset_seq(ctx);                 /* BUG-0220: strict-kex per exchange */
    ctx->bytes_sent = 0;
    ctx->bytes_received = 0;
    ctx->rekey_in_progress = 0;
    net_ssh_debug("[ssh] rekey complete - new keys active");
    return 0;
fail:
    ctx->rekey_in_progress = 0;
    return -1;
}

/* BUG-0219: threshold check, called ONLY at packet boundaries (exec loop
 * between packets). Either direction reaching the threshold triggers a
 * full rekey. */
static int net_ssh_maybe_rekey(net_ssh_ctx_t *ctx) {
    if (!ctx->encrypted || ctx->rekey_in_progress) return 0;
    if (ctx->bytes_sent < SSH_REKEY_THRESHOLD &&
        ctx->bytes_received < SSH_REKEY_THRESHOLD) return 0;
    return net_ssh_client_rekey(ctx);
}

/* net_ssh_exec: open session channel, send exec request, read output. */
int net_ssh_exec(const char *command, void *output, int output_len) {
    net_ssh_ctx_t *ctx = &g_ssh_ctx;
    if (!ctx->encrypted) {
        net_ssh_debug("[ssh] ssh_exec: not encrypted (transport not established)");
        return -1;
    }

    /* SSH_MSG_CHANNEL_OPEN (90) — session channel
     * Format: byte(90) + string("session") + u32(sender_channel) + u32(window_size) + u32(max_packet_size) */
    {
        u8 payload[64];
        int p = 0;
        /* msg_type added by net_ssh_send_packet_encrypted */
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

        if (net_ssh_send_packet_encrypted(ctx, SSH_MSG_CHANNEL_OPEN, payload, p) < 0) {
            net_ssh_debug("[ssh] failed to send CHANNEL_OPEN");
            return -2;
        }
        net_ssh_debug("[ssh] sent CHANNEL_OPEN (session)");
    }

    /* Read CHANNEL_OPEN_CONFIRMATION (91) or CHANNEL_OPEN_FAILURE (92) */
    {
        u8 rtype;
        u8 rbuf[256];
        int rlen = sizeof(rbuf);
        if (net_ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            net_ssh_debug("[ssh] failed to receive CHANNEL_OPEN_CONFIRMATION");
            return -3;
        }
        if (rtype != SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            net_ssh_debug("[ssh] CHANNEL_OPEN failed");
            return -4;
        }
        /* Parse: sender_channel(4) + recipient_channel(4) + window(4) + max_packet(4) */
        if (rlen >= 8) {
            /* rbuf starts after msg_type — actually our recv_packet_encrypted
             * strips msg_type, so rbuf[0..3] = sender_channel (server's channel) */
            ctx->server_channel_id = ((u32)rbuf[0] << 24) | ((u32)rbuf[1] << 16) |
                                     ((u32)rbuf[2] << 8) | rbuf[3];
        }
        net_ssh_debug("[ssh] got CHANNEL_OPEN_CONFIRMATION");
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
        int clen = (int)strlen(command);
        payload[p++] = (u8)(clen >> 24); payload[p++] = (u8)(clen >> 16);
        payload[p++] = (u8)(clen >> 8); payload[p++] = (u8)(clen & 0xFF);
        for (int i = 0; i < clen; i++) payload[p++] = command[i];

        if (net_ssh_send_packet_encrypted(ctx, SSH_MSG_CHANNEL_REQUEST, payload, p) < 0) {
            net_ssh_debug("[ssh] failed to send CHANNEL_REQUEST exec");
            return -5;
        }
        net_ssh_debug("[ssh] sent CHANNEL_REQUEST (exec)");
    }

    /* Read CHANNEL_DATA (94) records until EOF/CLOSE */
    int total = 0;
    u8 *out = (u8 *)output;
    while (total < output_len) {
        /* BUG-0219 (A14-33): packet-boundary rekey trigger. */
        if (net_ssh_maybe_rekey(ctx) != 0) {
            net_ssh_debug("[ssh] rekey failed - dropping session");
            break;
        }
        u8 rtype;
        u8 rbuf[4096];
        int rlen = sizeof(rbuf);
        if (net_ssh_recv_packet_encrypted(ctx, &rtype, rbuf, &rlen) < 0) {
            net_ssh_debug("[ssh] timeout reading channel data");
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
                    memcpy(out + total, rbuf + 8, copy);
                    total += copy;
                }
            }
        } else if (rtype == SSH_MSG_CHANNEL_EOF) {
            net_ssh_debug("[ssh] got CHANNEL_EOF");
            break;
        } else if (rtype == SSH_MSG_CHANNEL_CLOSE) {
            net_ssh_debug("[ssh] got CHANNEL_CLOSE");
            break;
        } else {
            /* Ignore other messages (CHANNEL_REQUEST success, WINDOW_ADJUST, etc.) */
            char dbg[64]; strcpy(dbg, "[ssh]   (exec loop) ignored msg type ");
            char nn[10]; u64_to_str((u64)rtype, nn); strcat(dbg, nn); strcat(dbg, "\n");
            screen_console_puts(dbg);
        }
    }
    net_ssh_debug("[ssh] exec complete");
    return total;
}

void net_ssh_close(void) {
    net_ssh_ctx_t *ctx = &g_ssh_ctx;
    if (ctx->net_tcp_sock >= 0) {
        net_close(ctx->net_tcp_sock);
        ctx->net_tcp_sock = -1;
    }
    ctx->encrypted = 0;
}
