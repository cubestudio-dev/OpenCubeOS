/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/ssh.h
 * Purpose: SSH-2.0 client.
 *
 * Implements:
 *   - SSH-2.0 version banner exchange
 *   - KEXINIT (algorithm negotiation)
 *   - KEX: curve25519-sha256 / curve25519-sha256@libssh.org (preferred,
 *     RFC 8731), diffie-hellman-group14-sha256 fallback (2048-bit, RFC 8268)
 *   - Host key algorithms: rsa-sha2-256 / rsa-sha2-512 (RFC 8332), ssh-rsa
 *   - NEWKEYS transition to encrypted mode
 *   - User authentication: password, or publickey when password is empty
 *   - Channel open + exec command + receive output
 *
 * Capabilities (post WP-09 mainstreaming):
 *   - Cipher: aes128-ctr (preferred), aes128-cbc fallback (RFC 4253 S6.3
 *     rolling IV)
 *   - MAC: hmac-sha2-256
 *   - Host key signature over H verified before keys are used (RFC 4253
 *     S8); SHA-256 fingerprint printed; TOFU trust model (no known_hosts
 *     store)
 *   - Key derivation: SHA-256 (RFC 4253 S7.2), not HMAC-SHA1
 *   - Interop tested against paramiko 5.0 in both directions (kernel
 *     client to paramiko server; kernel sshd with paramiko client)
 *
 * Limitations:
 *   - No compression
 *   - No PTY (exec only, no shell)
 *   - No known_hosts persistence (TOFU: fingerprint shown, not stored)
 */
#ifndef OC_SSH_H
#define OC_SSH_H

#include "types.h"
#include "crypto_core.h"  /* for DH_BYTES */

typedef struct {
    int net_tcp_sock;          /* underlying TCP socket */

    /* Negotiated algorithms (WP-09 mainstream batch) */
    int kex_curve25519;    /* 1 = curve25519-sha256, 0 = group14-sha256 */
    int cipher_ctr;        /* 1 = aes128-ctr, 0 = aes128-cbc */
    int auth_publickey;    /* 1 = publickey auth requested */

    /* Version banners */
    char client_banner[64];
    char server_banner[64];

    /* KEX state */
    u8 client_cookie[16];  /* random cookie for KEXINIT */
    u8 server_cookie[16];
    u8 client_kexinit[1024];
    int client_kexinit_len;
    u8 server_kexinit[4096];  /* paramiko KEXINIT can be > 512 bytes */
    int server_kexinit_len;

    /* DH state (SSH uses group 14 = 2048-bit DH, so 256 bytes) */
    u8 client_priv[256];     /* random private key x */
    u8 client_pub[256];      /* e = g^x mod p */
    u8 server_pub[256];      /* f = g^y mod p (from KEXDH_REPLY) */
    u8 shared_secret[256];   /* K = f^x mod p */

    /* Session ID = first exchange hash H (computed once, reused) */
    u8 session_id[32];
    int session_id_set;

    /* Encryption keys (derived from K + H via SHA-256, RFC 4253 S7.2) */
    u8 enc_key_c2s[16];     /* client→server AES-128 key (CTR or CBC) */
    u8 enc_key_s2c[16];     /* server→client AES-128 key (CTR or CBC) */
    u8 ctr_c2s[16];         /* rolling AES-128-CTR counter block, c2s */
    u8 ctr_s2c[16];         /* rolling AES-128-CTR counter block, s2c */
    u8 initial_iv_c2s[16];  /* client→server initial IV (first packet) */
    u8 initial_iv_s2c[16];  /* server→client initial IV (first packet) */
    /* WP-09 fix: RFC 4253 §6.3 CBC chaining — after the first packet, the IV
     * for each packet is the last ciphertext block of the previous packet.
     * Without rolling these, the SECOND encrypted packet decrypts to garbage
     * on the peer ("Invalid packet blocking"). */
    u8 iv_c2s_next[16];     /* rolling IV for outgoing CBC packets */
    u8 iv_s2c_next[16];     /* rolling IV for incoming CBC packets */
    u8 mac_key_c2s[32];     /* client→server HMAC-SHA-256 key (32 bytes) */
    u8 mac_key_s2c[32];     /* server→client HMAC-SHA-256 key */

    /* Sequence numbers (32-bit, per SSH spec) */
    u32 write_seq;
    u32 read_seq;

    int encrypted;          /* 1 after NEWKEYS */

    /* BUG-0219 (A14-33): rekey state. bytes_sent/bytes_received count
     * encrypted traffic since the last completed key exchange; when either
     * reaches the 64 MiB threshold the client starts a new KEXINIT at a
     * packet boundary (exec loop) reusing the existing KEX machinery. */
    u64 bytes_sent;
    u64 bytes_received;
    int rekey_in_progress;

    /* BUG-0220 (A14-34): strict-kex (RFC 9144). kex_strict = 1 after both
     * KEXINIT lists contained the kex-strict-*-v00@openssh.com tokens;
     * both directions' sequence numbers are then reset to 0 right after
     * the final NEWKEYS of the initial and every rekey exchange. */
    int kex_strict;

    /* BUG-0219: exchange hash H of the MOST RECENT completed exchange.
     * session_id stays the FIRST H forever (RFC 4253 S8) - key derivation
     * needs the current H on rekey, so the two are kept apart. */
    u8 exchange_hash[32];

    /* Username + password (set before net_ssh_connect) */
    char username[32];
    char password[32];

    /* Server host key (K_S from KEXDH_REPLY) — needed for exchange hash H */
    u8 server_host_key[1024];
    int server_host_key_len;
    /* Server signature over H (KEXDH_REPLY / KEX_ECDH_REPLY), verified
     * against the host key inside K_S before keys are accepted. */
    u8 server_sig[512];
    int server_sig_len;
    u32 server_channel_id;
    u32 client_channel_id;  /* our channel id (always 0) */
} net_ssh_ctx_t;

/* Connect to SSH server (banner + KEXINIT + KEX + NEWKEYS + USERAUTH).
 * Returns 0 on success. */
int net_ssh_connect(u32 ip, u16 port, const char *username, const char *password);

/* Execute a command on the server. Returns bytes of output received. */
int net_ssh_exec(const char *command, void *output, int output_len);

/* Close the SSH session. */
void net_ssh_close(void);

/* Get current SSH context (singleton). */
net_ssh_ctx_t *net_ssh_get_ctx(void);

#endif /* OC_SSH_H */
