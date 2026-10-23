<!-- SPDX-License-Identifier: Apache-2.0 -->
# End-to-end suites

The deep tests that gate a release. All of them drive a real QEMU
boot through pexpect on the serial console.

## Regression battery - tools/run_fix4_regression.py

28 checks: boot banner, boot-complete, exception self-test 3/3,
command count 233, uname tag forms, heaptest, fstest, cryptotest,
crashlog, l1test, and every `run <userprog>` (hello, fork, exec,
pipe, signal, select, mmap variants, dynamic linking: so_test,
dlsym_test, pie_test, reloc_test, p3, g6, g7...).

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

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

## SSH E2E

The in-guest `sshd` needs host/client keys on the /etc volume - the
suite (or you) seed them with `host_keygen_etc`. Then a paramiko
client from the host performs the full login + session + command
round-trip: 5/5 ALL PASS is the gate.

## HTTPS E2E

`wget https://<reference-host>/` from inside the guest against a
reference TLS server: handshake OK, GET received, body saved,
close_notify clean. 7 checks (handshake, cert chain, body bytes,
close semantics, timings).

## The /etc volume fixture

SSH and HTTPS suites assume a seeded `etc.img`. Build it once per
test cycle:

```
make etc.img          # 32 MiB FAT32 + opencube.conf
# then inside a first boot: host_keygen_etc (writes SSH keys)
```

The QEMU `run-*` make targets attach it automatically.
