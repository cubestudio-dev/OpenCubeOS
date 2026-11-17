<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU-coupling audit evidence

Live transcripts proving the audited A/B-class entries actually fire
in the running system (the WP-docs-cleanup audit requirement: at
least two A-class and two B-class entries live-triggered).

| Proof | Entry class | Command | Verbatim console line |
|---|---|---|---|
| A1 | real_hw_test CPUID hypervisor branch | `real_hw_test` | `[real_hw_test] actual: hypervisor bit set (QEMU/sandbox)` |
| A5 | virtio-blk 1AF4:1004 PCI-ID match | `virtio_blk_test` | `[virtio_blk_test] capacity nonzero (legacy cfg @0x14) => PASS` |
| B2 | e1000 legacy register-init path | `ifconfig` (boot with `-device e1000`) | `e1000: HWaddr 52:54:00:12:34:56 ... link: UP` |
| B2 | e1000 S3 resume replay (existing rings, same MAC) | `power suspend` + monitor `system_wakeup` + `ifconfig` | `[ocst] SLEEP_ARMED set ... Resumed from S3 ... e1000: HWaddr 52:54:00:12:34:56 ... link: UP` |

- 01-liveproof-devices.log: the boot with virtio-blk + e1000 +
  i6300esb attached; the four proof commands in sequence.
- 02-liveproof-s3-replay.log: the suspend -> system_wakeup -> replay
  session (the same NIC with the same MAC after S3).
