<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: entropy and the crypto core

`kernel/crypto/crypto_core.c` - the RNG and the hash/MAC/AEAD
primitives the protocols build on.

## Seeding

HMAC_DRBG (NIST SP 800-90A) over HMAC-SHA-256, seeded from:

- **RDRAND** (CPU entropy - CPUID-checked),
- **RDTSC**, re-mixed after EVERY generate call, so a partial state
  compromise cannot persist.

(The rdrand wrapper's `setc` flag is the u8/"=q" inline-asm that the
stricter CI assembler forced into correctness - portability note in
[../dev/portability.md](../dev/portability.md).)

## The primitive families

- AES, SHA-2 family, HMAC - NIST-vector validated (`cryptotest`).
- RSA, NIST curves, curve25519-style, Ed25519-style - the TLS/SSH
  key exchanges (`dhtest` times + verifies the group-14 modexp).
- AEAD (GCM), X.509 parsing + chain verification (RFC 6125 policy).

## The audit trail that shaped it

The old LCG was replaced (repetition visible to an observer); the
TCP RX checksum chain bug (double complement) and the TLS
mid-handshake hostname bug are the two famous crypto-adjacent fixes
- both regression-tested now.
