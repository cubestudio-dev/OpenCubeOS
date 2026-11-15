<!-- SPDX-License-Identifier: Apache-2.0 -->
# End-to-end suites

The deep tests that gate a release. All of them drive a real QEMU
boot through pexpect on the serial console; the matrix below is
what the release evidence quotes (real outputs, skip counts
declared).

## The verification matrix

| # | Gate | Tool | The line it must print |
|---|------|------|------------------------|
| 1 | Host unit tests (`make test`) | `tools/run_host_tests.py` | all harnesses PASS (SKIP-FIXTURE declared) |
| 2 | Full regression 31/31 | `tools/run_fix4_regression.py` | `=== 31/31 checks passed` |
| 3 | SSH E2E | `tools/ssh_g11a_e2e.py` | `[g11a] ALL PASS` (5 checks) |
| 4 | HTTPS E2E | `tools/run_fix4_https_e2e.py` | `=== 8/8 HTTPS E2E checks passed` |
| 5 | Watchdog/logging/health E2E | `tools/wp12_watchdog_logging_e2e.py` | S1-S14 |
| 6 | TUI / Chinese / desktop E2E | `tools/wp10_*.py` | per-suite |
| 7 | Mega-app smoke | the WP-l1apps-merge smoke script | 21/21 (classic dispatch + wizard open/quit + menu op + update steps) |

## The regression gate (31/31) - tools/run_fix4_regression.py

Boots the ISO, runs the command list (uname, help -a, the
selftest programs through `run`), and asserts per-command patterns
scoped to ONE command's output section (BUG #64: the old
whole-log matching was structurally always-true). It also asserts:

- **the live framebuffer** through a real HMP screendump (BUG #66
  - serial-only gates once passed on a dead panel);
- **the registry floor**: `help -a` must list >= 276 programs
  (measured 284 with the 8 merged apps) - the interface-freeze
  rule made executable.

## The SSH E2E (5/5) - tools/ssh_g11a_e2e.py

Client and server directions over the real TCP stack: ssh-auth,
ssh-exec, sshd-listen, sshd-exec, sshd-capture-log. Notes that
save you hours:

- TCG RSA-2048 host-key generation is slow (>500 s) - pre-seed the
  key (n||d, 256 B each, e=65537) into `build/etc.img`
  (`ssh_host_key`) with mtools; the loader takes the
  "key already on disk" fast path.
- Warm the ARP cache with one client-direction packet before the
  server test (the cold-cache SYN-ACK stall).

## The HTTPS E2E (8/8) - tools/run_fix4_https_e2e.py

The real OpenSSL-backed test server + the kernel TLS client:
handshake, TLS1.2 + cipher negotiation, encrypted GET, canonical
body, saved body, close_notify, the truncation guard (hard-close
server detected promptly), and the self-signed-chain refusal.

## The mega-app smoke (21/21)

Phase A - classic dispatch is byte-compatible: every mega-app runs
one of its historical ops (`disk-tool lsblk`, `net-tool ip`,
`sound-tool sound`, `sysdiag health`, `hwinfo uname`, `power sleep
1`, `files df`, `update status`, `disk-tool direct lsblk`, the
unknown-op usage).

Phase B - the wizards: every app's no-arg menu renders and quits
on `q`; `disk-tool` runs lsblk through menu key 1 + the ENTER
pause; `update` walks steps 1-2 for real (network check, A/B
absence handling) and returns to ush cleanly.

## Host unit tests - tools/run_host_tests.py

The harnesses compile L0 components host-side and run them:
multiboot2 parser (ASAN), crypto (NIST AES/SHA/HMAC, RSA, X.509,
AEAD), TLS key schedule, TCP congestion control, block-512 gate,
FAT structures, heap/VMM invariants... SKIP-FIXTURE harnesses run
for real when the openssl fixtures exist - declare the state.

## Watchdog / TUI / Chinese suites

`tools/wp12_watchdog_logging_e2e.py` (S12: Intel TCO real reset +
second boot; S13: ACPI WDAT; klog rotate chains; policy kill;
health JSON), `tools/wp10_tui_e2e.py`, `wp10_chinese_e2e.py`,
`wp10_tuiqa_apps_e2e.py`, `wp10_native_e2e.py` - unchanged
mechanics, run them when you touch their domains.
