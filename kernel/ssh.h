/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/ssh.h
 * Purpose: SSH-2.0 client (minimal implementation).
 *
 * Implements:
 *   - SSH-2.0 version banner exchange
 *   - KEXINIT (algorithm negotiation)
 *   - DH group 1 (1024-bit Oakley) + SHA-1 KEX
 *   - NEWKEYS transition to encrypted mode
 *   - User authentication (password)
 *   - Channel open + exec command + receive output
 *
 * Limitations:
 *   - Only diffie-hellman-group1-sha256 KEX (1024-bit)
 *   - Only aes128-cbc cipher + hmac-sha1 MAC
 *   - Host key verification: skipped (accept any)
 *   - No compression
 *   - No PTY (exec only, no shell)
 */
#ifndef OC_SSH_H
#define OC_SSH_H

#include "types.h"
#include "crypto.h"  /* for DH_BYTES */

typedef struct {
    int tcp_sock;          /* underlying TCP socket */

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

    /* Encryption keys (derived from K + H via HMAC-SHA1) */
    u8 enc_key_c2s[16];     /* client→server AES-128-CBC key */
    u8 enc_key_s2c[16];     /* server→client AES-128-CBC key */
    u8 initial_iv_c2s[16];  /* client→server initial IV */
    u8 initial_iv_s2c[16];  /* server→client initial IV */
    u8 mac_key_c2s[32];     /* client→server HMAC-SHA-256 key (32 bytes) */
    u8 mac_key_s2c[32];     /* server→client HMAC-SHA-256 key */

    /* Sequence numbers (32-bit, per SSH spec) */
    u32 write_seq;
    u32 read_seq;

    int encrypted;          /* 1 after NEWKEYS */

    /* Username + password (set before ssh_connect) */
    char username[32];
    char password[32];

    /* Server host key (K_S from KEXDH_REPLY) — needed for exchange hash H */
    u8 server_host_key[1024];
    int server_host_key_len;
    u32 server_channel_id;
    u32 client_channel_id;  /* our channel id (always 0) */
} ssh_ctx_t;

/* Connect to SSH server (banner + KEXINIT + KEX + NEWKEYS + USERAUTH).
 * Returns 0 on success. */
int ssh_connect(u32 ip, u16 port, const char *username, const char *password);

/* Execute a command on the server. Returns bytes of output received. */
int ssh_exec(const char *command, void *output, int output_len);

/* Close the SSH session. */
void ssh_close(void);

/* Get current SSH context (singleton). */
ssh_ctx_t *ssh_get_ctx(void);

#endif /* OC_SSH_H */
