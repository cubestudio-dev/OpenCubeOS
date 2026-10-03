/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/tls.h
 * Purpose: TLS client — mainstream-grade implementation.
 *   TLS 1.3 (RFC 8446):
 *     - X25519 key share, AES-128-GCM / AES-256-GCM / ChaCha20-Poly1305
 *     - full key schedule (HKDF), encrypted handshake, CertificateVerify
 *   TLS 1.2 (RFC 5246):
 *     - ECDHE_RSA with AES-GCM (0xC02F/0xC030), ChaCha20-Poly1305 (0xCCA8)
 *     - legacy DHE AES-CBC (0x0067) retained as fallback
 *   Security:
 *     - X.509 chain verification against embedded public roots (x509.c)
 *     - hostname verification via SAN dNSName
 */
#ifndef OC_TLS_H
#define OC_TLS_H

#include "types.h"
#include "x509.h"

#define TLS13 0x0304
#define TLS12 0x0303

/* Cipher suite ids */
#define CS_TLS13_AES128GCM_SHA256      0x1301
#define CS_TLS13_AES256GCM_SHA384      0x1302
#define CS_TLS13_CHACHA20POLY1305_SHA256 0x1303
#define CS_TLS12_ECDHE_RSA_AES128GCM   0xC02F
#define CS_TLS12_ECDHE_RSA_AES256GCM   0xC030
#define CS_TLS12_ECDHE_RSA_CHACHA20    0xCCA8
#define CS_TLS12_DHE_RSA_AES128_CBC_SHA256 0x0067

typedef struct {
    int tcp_sock;
    int version;               /* TLS13 / TLS12 */
    int cipher;                /* negotiated suite */
    u8 client_random[32];
    u8 server_random[32];
    u8 session_id[32];
    int session_id_len;

    /* TLS 1.3 state */
    u8 hs_secret[32];
    u8 c_hs[32], s_hs[32];     /* handshake traffic secrets */
    u8 c_ap[32], s_ap[32];     /* application traffic secrets */
    u8 wkey[32], wiv[12];
    u8 rkey[32], riv[12];
    u64 wseq, rseq;
    int hs_keys_active;        /* 1 after ServerHello processed */
    int app_keys_active;       /* 1 after client Finished sent */

    /* TLS 1.2 state */
    u8 master_secret[48];
    u8 mac_key_w[32], mac_key_r[32];
    u8 enc_key_w[32], enc_key_r[32];
    u8 iv_fixed_w[4], iv_fixed_r[4];
    u64 seq12_w, seq12_r;
    int tls12_encrypted;

    /* transcript (TLS 1.3: all handshake messages) */
    u8 transcript[9216];
    int transcript_len;
    /* TLS 1.2 handshake log (for Finished verify_data) */
    u8 hs_log[9216];
    int hs_log_len;

    /* certificate chain workspace */
    u8 chain_buf[14336];
    int chain_len;
    x509_cert_t certs[6];
    int ncerts;
    int verified;

    char hostname[128];
    int err;
} tls_ctx_t;

/* Connect + handshake. Returns 0 on success (certificate chain verified). */
int tls_connect(u32 ip, u16 port, const char *hostname);

/* Send / receive application data. */
int tls_send(tls_ctx_t *ctx, const void *data, int len);
void tls_recv_reset(void);
int tls_recv(tls_ctx_t *ctx, void *buf, int len);

/* Close. */
void tls_close(tls_ctx_t *ctx);

/* HTTPS GET: download a full response body. Returns bytes or -1. */
int tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                  void *out_buf, int out_len);

/* Singleton context (shell commands inspect negotiated parameters). */
tls_ctx_t *tls_get_ctx(void);

#endif /* OC_TLS_H */
