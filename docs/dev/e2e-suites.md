<!-- SPDX-License-Identifier: Apache-2.0 -->
# End-to-end suites

The deep tests that gate a release. All of them drive a real QEMU
boot through pexpect on the serial console; the 2.1.0 matrix below
is what the release evidence quotes (real outputs, skip counts
declared).

## The 2.1.0 verification matrix

| # | Gate | Tool |
|---|------|------|
| 1 | Host unit tests (`make test`) | `tools/run_host_tests.py` |
| 2 | Watchdog/logging/health E2E | `tools/wp12_watchdog_logging_e2e.py` |
| 3 | TUI command surface E2E | `tools/wp10_tui_e2e.py` |
| 4 | Chinese console E2E | `tools/wp10_chinese_e2e.py` |
| 5 | TUI desktop apps E2E | `tools/wp10_tuiqa_apps_e2e.py` |
| 6 | Native desktop E2E | `tools/wp10_native_e2e.py` |
| 7 | L1 per-app record (2.1.0) | every `l1apps/` program executed via `run` from ush, one record row per app |

## Host unit tests - tools/run_host_tests.py

19 harnesses compile L0 components host-side and run them:
multiboot2 parser, crypto (BN/EC/RSA/X.509/AEAD/X25519), TLS 1.3
key schedule, TCP congestion control, block-512 gate, SCSI, IR,
camera, wifi, bluetooth... **17 PASS + 2 SKIP-FIXTURE** without the
openssl-generated fixtures (rsa/x509; with fixtures present they run
for real - declare whichever state applied).

## Watchdog/logging/health E2E - tools/wp12_watchdog_logging_e2e.py

14 scenarios (S1-S14). The two that matter most:

- **S12 (Intel TCO)**: q35 + `-no-reboot`; asserts the TCO discovery
  line (`Intel TCO found: PMBASE=... TCOBASE=...`), arms the
  watchdog, starves it, asserts the REAL reset and the second boot.
- **S13 (ACPI WDAT)**: same shape through the WDAT table entries.

Plus klog write/flush/rotate/filter chains (threshold 4096 ->
`kernel.log` + `kernel.log.1` real generation), watchdog policy kill
(a starver task is killed with a log record while the kernel
survives), and health (JSON export validity, 3-spinner CPU alarm
`CPU-LOCKED` with the kernel alive).

## TUI / Chinese / desktop-app / native suites

`tools/wp10_tui_e2e.py` drives the L0 TUI command surface from
`oc>`; `tools/wp10_chinese_e2e.py` the CJK console; the
`tuiqa`/`native` suites drive the desktop apps (files/editor/
panel/terminal + the native editor/file-manager upgrades) starting
from the ring-3 `ush>` prompt - the 2.x boot-flow adaptation.

## L1 per-app record (2.1.0 addition)

The formal L1 application set is small and honest: **ush** (the
only `OC_L1_APP_DECL` application) plus the 36 embedded test
programs in `l1apps/`. The per-app record boots the release ISO,
starts each program via `run <name>` from ush, and notes the
observed output. Evidence lives in `evidence/` with the suite logs.

## Regression battery (v1-era, reference) - tools/run_fix4_regression.py

28 checks from the 1.0.0 era: boot banner, boot-complete,
exception self-test 3/3, command count 233 (the v1 single-prompt
world), uname tag forms, heaptest, fstest, cryptotest, crashlog,
l1test, and every `run <userprog>`. Its command-count and
prompt-flow assertions predate the v2 ring-3 split - it is kept for
reference and its `run <userprog>` section is covered by the L1
per-app record instead.

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

## SSH / HTTPS suites (available, not standard gates)

`tools/ssh_mainstream_test.py` + `tools/sshd_test.py` (paramiko
client against the in-guest sshd, host keys seeded on /etc via
`host_keygen_etc`) and `tools/run_fix4_https_e2e.py` against
`tools/https_test_server.py`. They need a seeded `etc.img`
(`make etc.img`) and network fixtures; run them when touching the
net stack.

## The /etc volume fixture

SSH and HTTPS suites assume a seeded `etc.img`. Build it once per
test cycle:

```
make etc.img          # 32 MiB FAT32 + opencube.conf
# then inside a first boot: host_keygen_etc (writes SSH keys)
```

The QEMU `run-*` make targets attach it automatically.
