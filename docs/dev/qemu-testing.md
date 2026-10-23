<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU testing

How the project actually tests in QEMU - the same recipes the
release gates use.

## Interactive boot

```
make run-bios      # default: 512M, e1000 + user net, serial on stdio
make run-uefi      # q35 + OVMF
make run-bios-gui  # display on (for the TUI)
```

The `run-*` targets build the ISO and a fresh `etc.img` (the 32 MiB
FAT32 /etc volume) so persistence tests start clean.

## Deterministic boot-log testing

The regression suite (`tools/run_fix4_regression.py`) drives QEMU
through pexpect over `-serial mon:stdio`:

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

It waits for the `oc>` prompt, then runs the fixed command battery
(uname, help -a, heaptest, fstest, cryptotest, crashlog, l1test, and
the whole `run *test` user-program set), and asserts 28 checks on the
combined output - boot banner, self-tests, command count 233, each
test's PASS line. Zero failures is the bar.

## Watchdog / logging E2E

```
python3 tools/wp12_watchdog_logging_e2e.py build/opencube.iso
```

14 scenarios: klog write/flush/rotation/filter chains, watchdog
status/slots/policy kill, health groups + JSON export + CPU alarm,
and the two *real hardware reset* scenarios - S12 boots a q35 machine
with `-no-reboot`, discovers the Intel TCO, arms it, starves it, and
asserts the second boot; S13 does the same through ACPI WDAT.

## Network E2E

- **SSH E2E**: starts the in-guest `sshd`, then logs in from the host
  through paramiko - full key exchange, session, command round-trip.
  Requires the /etc volume seeded with host keys
  (`host_keygen_etc`).
- **HTTPS E2E**: `wget https://<host>` against a reference TLS server -
  handshake, certificate verification, body saved, close_notify.

Both suites are release gates; see
[e2e-suites.md](e2e-suites.md) for their full mechanics.

## Screenshots

```
make shot-bios     # boot screenshot to build/shot-bios.ppm
make shot-uefi
```

## QEMU devices that map to drivers

| QEMU flag | Kernel driver |
|-----------|---------------|
| `-device e1000` (default net) | e1000 NIC |
| `-device virtio-blk-pci` | virtio-blk |
| `-device nvme` | NVMe |
| `-device usb-storage` through xHCI | USB MSC |
| `-device AC97` / `-device intel-hda` | sound |
| q35 machine | Intel TCO watchdog |
| `-bios OVMF.fd` | UEFI boot path |

## Speed

- `-display none -serial mon:stdio` for every automated run.
- Boot to the `oc>` prompt takes ~2 s of wall time in the regression
  environment; the full 28-check battery runs in well under a minute.
