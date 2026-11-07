<!-- SPDX-License-Identifier: Apache-2.0 -->
# Host-side unit tests

`make test` runs `tools/run_host_tests.py`: it compiles and executes
the harnesses in `tests/host_*.c` *on the host*, against the real
kernel source files they include.

## What runs

The runner extracts the documented gcc line from each harness header,
remaps the pre-restructure paths (`kernel/aead.c` ->
`kernel/crypto/crypto_aead.c`, ...), compiles with the host toolchain
(ASAN where the harness asks for it) and executes it. Harnesses that
need external fixtures (openssl-generated key files) compile but
report SKIP-FIXTURE rather than failing.

Coverage highlights (one harness per family):

- crypto: NIST AES/SHA/HMAC vectors, RSA, X.509 parsing, AEAD
- tar / gzip / string: the lib_* primitives
- FAT structures and LFN encoding
- multiboot2 parsing (with malformed-mbi cases, ASAN)
- heap and VMM invariants

## When to run it

Always. It is the fastest signal in the loop - no boot required, and
it catches pure-logic regressions (checksum chains, header
alignment, bounds) in seconds.

## Adding a harness

1. Create `tests/host_<name>.c` with the standard header comment
   including the exact compile line (flags + include paths).
2. Count your checks and exit with the number of failures.
3. `make test` picks it up automatically.

The rule from [CONTRIBUTING.md](../root/CONTRIBUTING.md): never weaken
a test to make it pass. If a test is wrong, fix the test *and say so
in the commit*.
