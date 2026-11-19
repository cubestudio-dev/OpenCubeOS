<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Cryptography (2)

### cryptotest

| Field | Value |
|---|---|
| Name | cryptotest |
| Category | Cryptography |
| Function | crypto primitives test |
| Usage | `cryptotest` |
| Source | `ocos/services/crypto/src/svc_crypto_l1.c` |

**Executed example**:

```
cryptotest
Crypto self-test (NIST vectors):
  SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    SHA-256: PASS
  AES-128(plain) = 69c4e0d86a7b0430d8cdb78070b4c55a
    AES-128: PASS
  HMAC-SHA256 = b0344c61d8db3853...
    HMAC-SHA256: PASS
  HMAC-SHA384 = afd03944d84895626b0825f4ab46907f...
    HMAC-SHA384: PASS
  4/4 tests passed
ush> 
```

### dhtest

| Field | Value |
|---|---|
| Name | dhtest |
| Category | Cryptography |
| Function | Diffie-Hellman test |
| Usage | `dhtest` |
| Source | `ocos/services/crypto/src/svc_crypto_l1.c` |

**Executed example**:

```
dhtest
DH modexp self-test (Oakley Group 1, 1024-bit):
  g^0 mod p = 00000001 (expect ...0001)  PASS
  1^x mod p = 00000001 (expect ...0001)  PASS
  Computing g^x mod p (timing)...
  g^x mod p = 33c998e6ba1359412f1bf42dd72cda3c...
  Time: 4090 ms
  premaster (first 16): b432232b859ee92a720843b9c8d88289
  TLS KEX (2 modexp) time: 9100 ms (premaster non-zero)
DH group14 (2048-bit) fixed-vector truth tests:
  g^x mod p14 = e  -> PASS  (got 32a09a91..., expect 32A09A91...)
  determinism (3 runs): IDENTICAL
  f^x mod p14 = K2 -> PASS  (got 5962870f..., expect 5962870F...)
  2048-bit modexp time: 13690 / 0 ms
DH modexp scale sweep (truth vectors, python3 pow()):
  len=8: PASS  got 1f8c25fe... expect 1f8c25fe...
  len=16: PASS  got 5ab910ce... expect 5ab910ce...
  len=32: PASS  got 4a2f13c1... expect 4a2f13c1...
  len=64: PASS  got 10ad3f1b... expect 10ad3f1b...
  len=128: PASS  got 3ca32c36... expect 3ca32c36...
  len=256: PASS  got a7f3a565... expect a7f3a565...
  5/5 correctness tests passed
ush> 
```
