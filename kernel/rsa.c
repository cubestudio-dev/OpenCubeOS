/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/rsa.c
 * Purpose: RSASSA-PKCS1-v1_5 and RSASSA-PSS verification (RFC 8017).
 * Verification only (public exponent e, typically 65537) — practical in
 * QEMU thanks to the bn.c Montgomery core.
 */
#include "rsa.h"
#include "bn.h"
#include "crypto.h"   /* sha256 */
#include "sha512.h"
#include "string.h"
#ifdef RSA_DEBUG
#include <stdio.h>
#endif
int rsa_public_op_wrapper(const u8 *n, int n_len, const u8 *e, int e_len, const u8 *sig, int sig_len, u8 *m_out);

static int rsa_hash_len(int sha_alg) {
    switch (sha_alg) {
        case RSA_SHA256: return 32;
        case RSA_SHA384: return 48;
        case RSA_SHA512: return 64;
    }
    return -1;
}

/* EMSA-PKCS1-v1_5 DigestInfo prefixes (RFC 8017 §9.2 note 1) */
static const u8 DI_SHA256[19] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,
    0x05,0x00,0x04,0x20
};
static const u8 DI_SHA384[19] = {
    0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,
    0x05,0x00,0x04,0x30
};
static const u8 DI_SHA512[19] = {
    0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,
    0x05,0x00,0x04,0x40
};

static const u8 *digest_info(int sha_alg) {
    switch (sha_alg) {
        case RSA_SHA256: return DI_SHA256;
        case RSA_SHA384: return DI_SHA384;
        case RSA_SHA512: return DI_SHA512;
    }
    return 0;
}

/* m = sig^e mod n (bn_mod_exp uses a static workspace). */
static int rsa_public_op(u8 *m_out, int m_len,
                         const u8 *n, int n_len,
                         const u8 *e, int e_len,
                         const u8 *sig, int sig_len) {
    if (sig_len != n_len || m_len < n_len) return -1;
    /* output exactly n_len bytes so m_out[0..n_len) is the EM */
    return bn_mod_exp(m_out, n_len, sig, sig_len, e, e_len, n, n_len);
}

int rsa_verify_pkcs1(const u8 *n, int n_len, const u8 *e, int e_len,
                     int sha_alg, const u8 *hash, int hash_len,
                     const u8 *sig, int sig_len) {
    int hlen = rsa_hash_len(sha_alg);
    const u8 *di = digest_info(sha_alg);
    if (hlen <= 0 || hlen != hash_len || !di) return -1;
    if (n_len <= 0 || n_len > BN_MAX_BYTES || e_len <= 0 || e_len > 8)
        return -1;

    u8 m[BN_MAX_BYTES];
    if (rsa_public_op(m, (int)sizeof(m), n, n_len, e, e_len, sig, sig_len) != 0)
        return 0;

    /* EM = 00 01 FF..FF 00 || DigestInfo || hash  (k bytes total) */
    int t_len = 19 + hlen;
    if (n_len < t_len + 11) return 0;   /* "RSA modulus too small" */
    int ps_len = n_len - 3 - t_len;
    if (ps_len < 8) return 0;
    if (m[0] != 0x00 || m[1] != 0x01) return 0;
    for (int i = 0; i < ps_len; i++)
        if (m[2 + i] != 0xFF) return 0;
    if (m[2 + ps_len] != 0x00) return 0;
    if (oc_memcmp(m + 3 + ps_len, di, 19) != 0) return 0;
    if (oc_memcmp(m + 3 + ps_len + 19, hash, hlen) != 0) return 0;
    return 1;
}

int rsa_public_op_wrapper(const u8 *n, int n_len, const u8 *e, int e_len,
                          const u8 *sig, int sig_len, u8 *m_out) {
    return rsa_public_op(m_out, n_len, n, n_len, e, e_len, sig, sig_len);
}

/* MGF1 (RFC 8017 B.2.1) with the given hash: mask = Hash(seed || counter) */
static int mgf1(int sha_alg, const u8 *seed, int seed_len,
                u8 *mask, int mask_len) {
    u8 hbuf[64];
    int hlen = rsa_hash_len(sha_alg);
    if (hlen <= 0) return -1;
    u8 cbuf[64 + 4];
    oc_memcpy(cbuf, seed, seed_len);
    int off = 0;
    for (u32 counter = 0; off < mask_len; counter++) {
        cbuf[seed_len]     = (u8)(counter >> 24);
        cbuf[seed_len + 1] = (u8)(counter >> 16);
        cbuf[seed_len + 2] = (u8)(counter >> 8);
        cbuf[seed_len + 3] = (u8)counter;
        if (sha_alg == RSA_SHA256) sha256(cbuf, seed_len + 4, hbuf);
        else if (sha_alg == RSA_SHA384) sha384(cbuf, seed_len + 4, hbuf);
        else sha512(cbuf, seed_len + 4, hbuf);
        int take = mask_len - off < hlen ? mask_len - off : hlen;
        oc_memcpy(mask + off, hbuf, take);
        off += take;
    }
    return 0;
}

int rsa_verify_pss(const u8 *n, int n_len, const u8 *e, int e_len,
                   int sha_alg, const u8 *hash, int hash_len,
                   const u8 *sig, int sig_len) {
    int hlen = rsa_hash_len(sha_alg);
    if (hlen <= 0 || hlen != hash_len) return -1;
    if (n_len <= 0 || n_len > BN_MAX_BYTES || e_len <= 0 || e_len > 8)
        return -1;

    /* emBits = modBits - 1; emLen = ceil(emBits / 8) */
    u8 nbuf[BN_MAX_BYTES];
    oc_memcpy(nbuf, n, n_len);
    /* modBits from the modulus top byte */
    int top = nbuf[0];
    int mod_bits = (n_len - 1) * 8;
    int b = 0;
    if (top == 0) return -1;
    while (!(top & 0x80)) { top <<= 1; b++; }
    mod_bits += 8 - b;
    int em_bits = mod_bits - 1;
    int em_len = (em_bits + 7) / 8;
    int s_len = hlen;   /* salt length = hash length (TLS 1.3 default) */

    if (em_len < hlen + s_len + 2) return 0;

    u8 m[BN_MAX_BYTES];
    if (rsa_public_op(m, (int)sizeof(m), n, n_len, e, e_len, sig, sig_len) != 0)
        return 0;
#ifdef RSA_DEBUG
    fprintf(stderr, "pss: em_len=%d s_len=%d em[0]=%02x em[last]=%02x\n",
            em_len, s_len, m[0], m[em_len - 1]);
#endif

    /* leftmost 8*emLen - emBits bits must be zero */
    int unused = 8 * em_len - em_bits;
    if (unused >= 8) return 0;   /* em_len == ceil handles this; sanity */
    if (unused && (m[0] >> (8 - unused)) != 0) return 0;
    if (m[em_len - 1] != 0xBC) return 0;

    int db_len = em_len - hlen - 1;               /* maskedDB is m[0..db_len) */
    u8 h[64];
    oc_memcpy(h, m + db_len, hlen);               /* H */

    /* DB = maskedDB XOR dbMask */
    u8 db_mask[BN_MAX_BYTES];
    if (mgf1(sha_alg, h, hlen, db_mask, db_len) != 0) return -1;
    u8 dbx[BN_MAX_BYTES];
    for (int i = 0; i < db_len; i++) dbx[i] = (u8)(m[i] ^ db_mask[i]);
    /* set leftmost `unused` bits of DB[0] to zero */
    if (unused) dbx[0] &= (u8)(0xFF >> unused);

    /* DB must be 00..00 01 || salt */
    int salt_off = -1;
    for (int i = 0; i < db_len; i++) {
        if (dbx[i] == 0x01) { salt_off = i + 1; break; }
        if (dbx[i] != 0x00) {
#ifdef RSA_DEBUG
            fprintf(stderr, "pss: bad DB padding at %d: %02x\n", i, dbx[i]);
#endif
            return 0;
        }
    }
    if (salt_off < 0 || db_len - salt_off != s_len) {
#ifdef RSA_DEBUG
        fprintf(stderr, "pss: salt_off=%d db_len=%d\n", salt_off, db_len);
#endif
        return 0;
    }
#ifdef RSA_DEBUG
    fprintf(stderr, "pss: DB ok, salt at %d, H=%02x%02x%02x%02x\n",
            salt_off, h[0], h[1], h[2], h[3]);
#endif

    /* H' = Hash(00*8 || mHash || salt) */
    u8 mprime[8 + 64 + 64];
    oc_memset(mprime, 0, 8);
    oc_memcpy(mprime + 8, hash, hlen);
    oc_memcpy(mprime + 8 + hlen, dbx + salt_off, s_len);
    u8 h2[64];
    if (sha_alg == RSA_SHA256) sha256(mprime, 8 + hlen + s_len, h2);
    else if (sha_alg == RSA_SHA384) sha384(mprime, 8 + hlen + s_len, h2);
    else sha512(mprime, 8 + hlen + s_len, h2);
#ifdef RSA_DEBUG
    fprintf(stderr, "pss: H =%02x%02x%02x%02x H'=%02x%02x%02x%02x\n",
            h[0], h[1], h[2], h[3], h2[0], h2[1], h2[2], h2[3]);
#endif
    if (oc_memcmp(h2, h, hlen) != 0) return 0;
    return 1;
}
