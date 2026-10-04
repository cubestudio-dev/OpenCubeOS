/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/x509.h
 * Purpose: X.509 v3 certificate chain validation for TLS.
 *   - DER parsing of Certificate / TBSCertificate
 *   - RSA (PKCS#1 v1.5) and ECDSA (P-256/P-384) signature verification
 *   - Validity window via the CMOS RTC
 *   - Basic-constraints CA flag on issuers
 *   - SAN dNSName hostname matching (single-label wildcard)
 *   - Embedded root CA store (major public roots)
 */
#ifndef OC_X509_H
#define OC_X509_H

#include "types.h"

#define X509_MAX_NAME 128
#define X509_MAX_DNS  8

typedef struct {
    const u8 *der;
    int crypto_der_len;
    /* parsed fields */
    const u8 *tbs;             /* start of tbsCertificate */
    int tbs_len;
    int sig_alg;               /* X509_SIG_* */
    u64 not_before;            /* unix seconds */
    u64 not_after;
    int is_ca;
    char subject[X509_MAX_NAME];
    char issuer[X509_MAX_NAME];
    char net_dns_names[X509_MAX_DNS][X509_MAX_NAME];
    int net_dns_count;
    /* subject public key */
    const u8 *spki;            /* SubjectPublicKeyInfo DER start */
    int spki_len;
    /* RSA key (extracted) */
    const u8 *rsa_n; int rsa_n_len;
    const u8 *rsa_e; int rsa_e_len;
    /* EC key (extracted) */
    u8 ec_pub[97]; int ec_pub_len; int ec_curve;   /* EC_P256/EC_P384 */
    /* signature bits */
    const u8 *sig; int sig_len;
    /* raw tbs for signature input */
    const u8 *sig_input; int sig_input_len;
} crypto_x509_cert_t;

/* Error codes (negative) */
#define X509_OK            0
#define X509_E_FORMAT     -1
#define X509_E_UNSUPPORTED -2
#define X509_E_EXPIRED    -3
#define X509_E_NOT_YET    -4
#define X509_E_NO_ROOT    -5
#define X509_E_BADSIG     -6
#define X509_E_HOSTNAME   -7
#define X509_E_NO_ISSUER  -8

/* Parse one DER certificate into the structure. */
int crypto_x509_parse(crypto_x509_cert_t *c, const u8 *der, int crypto_der_len);

/* Verify a chain (leaf first, then intermediates) against the root store
 * for the given DNS hostname. hostname may be NULL to skip name checks. */
int crypto_x509_verify_chain(const crypto_x509_cert_t *chain, int chain_len,
                      const char *hostname);

/* Human-readable error for reports (ASCII). */
const char *crypto_x509_errstr(int code);

#endif /* OC_X509_H */
