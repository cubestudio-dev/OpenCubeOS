/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/tls.h
 * Purpose: TLS 1.2 client — minimal implementation for HTTPS.
 *   - TLS 1.2 handshake (ClientHello → ... → Finished)
 *   - AES-128-CTR record encryption/decryption
 *   - DH 1024-bit key exchange (Oakley Group 1)
 *   - Certificate verification: skipped (accept any cert)
 */
#ifndef OC_TLS_H
#define OC_TLS_H

#include "types.h"
#include "crypto.h"  /* for DH_BYTES */

/* TLS connection context */
typedef struct {
    int tcp_sock;          /* underlying TCP socket */
    u8 client_random[32];  /* client random (from ClientHello) */
    u8 server_random[32];  /* server random (from ServerHello) */
    u8 premaster[DH_BYTES]; /* premaster secret (full DH shared secret) */
    u8 master_secret[48];  /* master secret (from PRF) */
    u8 write_key[32];     /* client_write_MAC_key (HMAC-SHA256 key, 32 bytes) */
    u8 read_key[32];      /* server_write_MAC_key */
    u8 write_iv[16];      /* client_write_key (AES-128 key, 16 bytes) */
    u8 read_iv[16];       /* server_write_key (AES-128) */
    u8 write_seq[8];     /* write sequence number (big-endian, TLS 1.2 = 64-bit) */
    u8 read_seq[8];      /* read sequence number */
    int encrypted;          /* 1 after ChangeCipherSpec */
} tls_ctx_t;

/* Connect to a TLS server. Returns 0 on success. */
int tls_connect(u32 ip, u16 port, const char *hostname);

/* Send data over established TLS connection. */
int tls_send(tls_ctx_t *ctx, const void *data, int len);

/* Receive data from TLS connection. */
int tls_recv(tls_ctx_t *ctx, void *buf, int len);

/* Close TLS connection. */
void tls_close(tls_ctx_t *ctx);

/* HTTPS: download a file via TLS. Returns bytes downloaded or -1. */
int tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                  void *out_buf, int out_len);

#endif /* OC_TLS_H */
