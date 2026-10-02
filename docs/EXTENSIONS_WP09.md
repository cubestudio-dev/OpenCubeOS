<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-09 Extensions: Secure Transport (SSH / TLS-HTTPS / Crypto)

WP-09 adds a security-transport layer on top of the WP-06 TCP/IP stack.
Unlike WP-02..WP-08 it does **not** add new `oc_ext_*` L1 interfaces — the
features are exposed through (a) kernel C APIs (ssh.h / tls.h / crypto.h)
and (b) shell commands. All algorithm choices follow the minimal-dependency
philosophy: everything implemented in-tree against kernel/crypto.c.

Verification: real QEMU outputs on both sides.

## 1. Crypto core (kernel/crypto.h)

| Function | Purpose |
|---|---|
| `aes128_cbc_encrypt(iv, in, len, out)` | AES-128-CBC encryption (PKCS-free, TLS-style padding done by caller) |
| `aes128_cbc_decrypt(iv, in, len, out)` | AES-128-CBC decryption |
| `sha256(data, len, out32)` | SHA-256 digest |
| `hmac_sha256(key, key_len, data, data_len, out32)` | HMAC-SHA256 (RFC 2104) |
| `dh_modexp(base, exp, mod, out)` | Arbitrary-length big-integer modular exponentiation (byte arrays, big-endian) |
| `crypto_random(buf, len)` | Entropy from timer jitter + RDTSC mixing |

Correctness evidence (real `dhtest` kernel self-test output):

```
DH modexp self-test (Oakley Group 1, 1024-bit):
  g^0 mod p = 00000001 (expect ...0001)  PASS
  1^x mod p = 00000001 (expect ...0001)  PASS
  g^x mod p = 933b2b3a5df3bc4d3869049c9ed0684a...   Time: 3230 ms
DH group14 (2048-bit) fixed-vector truth tests:
  g^x mod p14 = e  -> PASS  (got 32a09a91..., expect 32A09A91...)
  f^x mod p14 = K2 -> PASS  (got 5962870f..., expect 5962870F...)
  determinism (3 runs): IDENTICAL
DH modexp scale sweep (truth vectors, python3 pow()):
  len=8/16/32/64/128/256: all PASS
  5/5 correctness tests passed
```

Truth vectors: kernel/dh_scale_vectors.h (verified against python3 pow()).

## 2. SSH client (kernel/ssh.h)

```c
int  ssh_connect(u32 ip, u16 port, const char *username, const char *password);
int  ssh_exec(const char *command, void *output, int output_len);
void ssh_close(void);
```

- **KEX**: diffie-hellman-group14-sha256 (2048-bit MODP, RFC 3526 §3),
  host key `rsa-sha2-256`, ciphers `aes128-cbc`, MAC `hmac-sha2-256`.
- **Auth**: password only.
- **Channel**: one session; `ssh_exec` sends
  `SSH_MSG_CHANNEL_REQUEST("exec")` and reads `SSH_MSG_CHANNEL_DATA` until
  EOF/CLOSE into `output`.
- **Shell command**: `ssh <ip> [port] [user] [password]`
  (port defaults to 2222, the Python paramiko test server port).

Byte-level evidence (real output, kernel ssh + paramiko server logs):

```
kernel:  [ssh] K (first 8): 2f4130816e935c6a
server:  [paramiko-sshd] K (server) len=256 K[:8]=2f4130816e935c6a
server:  [paramiko-sshd] EXEC request: b'echo hello-from-OpenCubeOS-kernel-ssh'
```

Timing under QEMU TCG: 26.5 s + 30.5 s per modexp pair.

## 3. SSH server — sshd (kernel/sshd.c)

Shell command: `sshd <port> <user> <password>` — serves **one** connection
then returns to the shell (single-session design for the test bench).

- Same algorithm suite as the client (group14-sha256 / aes128-cbc /
  hmac-sha2-256); host key embedded in kernel/sshd_rsa_key.h.
- Password authentication against the credentials given on the command line.
- `exec` requests are run through `shell_execute_captured()` (kernel shell
  capture API, WP-09 addition in shell.c), output returned as
  `SSH_MSG_CHANNEL_DATA`, followed by `exit-status` and channel close.

Evidence (paramiko 5.0 client → kernel sshd E2E run):
4/4 checks PASS (listening / connection accepted / session finished
cleanly / auth + exec succeeded), 32-byte exec output captured.

## 4. TLS 1.3 / TLS 1.2 client + HTTPS (kernel/tls.h)

```c
int  tls_connect(u32 ip, u16 port, const char *hostname);
int  tls_send(tls_ctx_t *ctx, const void *data, int len);
int  tls_recv(tls_ctx_t *ctx, void *buf, int len);
void tls_close(tls_ctx_t *ctx);
int  tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                   void *out_buf, int out_len);
```

- **TLS 1.3 (RFC 8446, preferred)**: X25519 key share, cipher suites
  TLS_AES_128_GCM_SHA256 (0x1301), TLS_AES_256_GCM_SHA384 (0x1302),
  TLS_CHACHA20_POLY1305_SHA256 (0x1303); full HKDF key schedule (validated
  against RFC 8448 vectors), encrypted handshake, server CertificateVerify
  checked, client Finished accepted.
- **TLS 1.2 (RFC 5246, fallback)**: ECDHE_RSA with AES_128_GCM (0xC02F),
  AES_256_GCM (0xC030) or ChaCha20-Poly1305 (0xCCA8); legacy
  DHE_RSA_AES_128_CBC_SHA256 (0x0067, Oakley Group 1 — the client uses the
  server's p/g from ServerKeyExchange) retained for old servers.
- **Server certificate**: X.509 chain verified against embedded public CA
  roots (ISRG Root X1/X2, Google GTS, DigiCert, GlobalSign, Baltimore,
  Amazon, Microsoft) + hostname match via SAN dNSName (kernel/x509.c);
  validity checked against the RTC clock. Handshake fails closed on
  verification errors.
- **Record layer**: AEAD (AES-GCM / ChaCha20-Poly1305) per negotiated
  suite; TLS 1.3 handshake traffic and application traffic use separate
  keys per the RFC 8446 key schedule.
- **close_notify**: warning alert sent on `tls_close()` (both versions);
  incoming alerts are decrypted and treated as EOF.

**HTTPS shell command** (net.c): `wget https://host[:port]/path` — performs
the handshake, sends `GET path HTTP/1.0`, decrypts the response, splits the
body at `\r\n\r\n` and saves it to `/wget_https.html` (prints
`Saved N bytes to /wget_https.html`). Plain `wget http://...` unchanged.

Real-site verification (mainstreaming batch): HTTPS GET against
`https://cubestudio-dev.github.io/OpenCubeOS/update.json` → 10148 bytes
downloaded and saved; google.com and cloudflare.com handshake + GET also
verified. The bundled `tools/https_test_server.py` (TLS 1.2 DHE-CBC)
continues to cover the legacy fallback path.

## 5. New/changed shell commands

| Command | WP | Purpose |
|---|---|---|
| `ssh <ip> [port] [user] [password]` | WP-09 | SSH client + exec round trip |
| `sshd <port> <user> <password>` | WP-09 | Single-session SSH server |
| `wget https://host[:port]/path` | WP-09 | HTTPS GET via TLS 1.3/1.2 (extended `wget`) |
| `route`, `arp`, `firewall`, `tcpstats`, `dns` | WP-09 | Network ops/visibility |

## 6. ABI stability

All WP-01..WP-08 interfaces unchanged in WP-09 (verified: 18/18 regression
includes the WP-01 self-test, l1test and the WP-08cd boot self-test).
WP-09 adds new symbols only (`ssh_*`, `tls_*`, `shell_execute_captured`,
crypto primitives) and one extended command (`wget` gained the `https://`
prefix form).

## 9. System configuration + update check (WP-09-fix5)

WP-09-fix5 adds the first user-editable system configuration file and an
online update check on top of the WP-06/09 transport stack.

### Interfaces

| Interface | Header | Purpose |
|---|---|---|
| `oc_ext_config_read(key, val_out, outlen)` | kernel/ext.h | strict config read (see docs/CONFIG.md §3 for missing/empty/invalid semantics) |
| `oc_ext_config_write(key, value)` | kernel/ext.h | validated write preserving comments and other keys |
| `oc_ext_config_get_all(buf, buflen)` | kernel/ext.h | whole config file |
| `oc_ext_check_update(out)` | kernel/ext.h | synchronous update check over HTTP/HTTPS (`0` up-to-date, `1` new, `<0` error) |
| `oc_ext_check_update_async()` | kernel/ext.h | spawn the non-blocking boot-check thread |

Implementations live in kernel/config.c (config subsystem +
`oc_config_autocheck_enabled`) and kernel/update.c (URL parsing, HTTP and
HTTPS fetches, scoped JSON field parser, async kernel thread). Each has a
contract comment at the declaration site and a default (non-stub)
implementation in-tree.

### Shell surface added

`checkupdate`, `config` (list/get/set/restore/path), `edit <file>`,
`config_test`, `checkupdate_test` — 83 shell commands total.

### Verification

13-case matrix with real QEMU outputs
(config read/write/delete/restore, URL prefix routing, HTTP and HTTPS
manifest fetches, same/new version handling, auto_check boot behaviour,
all five error paths), plus the unchanged WP-09 regression (18/18,
dhtest 5/5, cryptotest 3/3, SSH both directions, HTTPS E2E).
