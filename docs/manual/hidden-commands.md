<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hidden commands and test stubs — the debug surface

About a third of the 284 registered programs are **test stubs**: bodies
migrated from the v2.2.0 shell, now served through the L1SVC bridge or
linked as flat ring-3 probes. They are all ordinary registry commands —
type the name at the `ush>` prompt and press ENTER; no special runner,
no flags needed unless noted. Nothing in `help` marks them as tests:
they sit in the A-Z listing exactly like `ls` or `ping`.

The full-repo scan counts **~93 test-flavored programs**: 69
`*test*`-named directory apps plus 24 flat single-file probes (the
boundary is fuzzy — `hello`, `loop`, `badapp`, `p0leak` are flat utility
probes the job tests use as targets). This page lists every one, grouped
by what it exercises, with how to run it and what a healthy run prints.

> **Safety**: everything here is read-only or self-contained **except**
> the marked items: `fs_mount_test` (formats the device), the four
> power-path probes (they can end the session), `crashtest` (it panics
> the kernel on purpose — that is the test), `stackovf`/`badapp`/
> `sigsegv_test` (they kill themselves on purpose).

## How these are wired

- **Directory stubs** (`ocos/l1apps/<name>/main.c`) are the standard
  `l1svc_main()` wrapper: argv is joined into one 256-byte line and sent
  to the owning service op; the service runs the actual test body and
  the printed text comes back through the output capture.
- **Flat probes** (`ocos/l1apps/<name>.c|.asm`, tag `l1`) are real ring-3
  programs that make raw syscalls themselves — they exercise the
  kernel surface directly and print their own verdicts.
- Verdict lines are consistently `PASS` / `FAIL` / `SKIPPED` (or
  `(honest skip)` / `(honest FAIL)` when hardware is absent — that is a
  *correct* outcome, not a failure).

---

## Class A — directory test stubs, by family

### OTA / update pipeline (12)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `ab_partition_test` | A/B flag-file protocol and slot detection | `ab_partition_test` | per-check OK lines + final PASS |
| `checkupdate_test` | checkupdate negative fixtures (`ftp://` prefix rejected, broken JSON rejected, oversized changes rejected by the 128-byte buffer, missing URLs) | `checkupdate_test` | every hostile fixture `(correctly rejected) => PASS` |
| `config_test` | config backend (read/write/restore, CRC32-corrupt, truncated, ustar bad-checksum rejections) | `config_test` | PASS block |
| `update_check_test` | the check step unit test | `update_check_test` | PASS |
| `update_download_test` | the download step unit test | `update_download_test` | PASS |
| `update_install_test` | the install step unit test | `update_install_test` | PASS |
| `update_local_test` | offline package install (`--local` path) | `update_local_test` | PASS |
| `update_pkg_test` | package parsing (gzip/tar/sha256: corrupted CRC32, truncated stream, bad ustar checksum, wrong sha256 all rejected) | `update_pkg_test` | rejections PASS |
| `update_rollback_test` | rollback flag clearing | `update_rollback_test` | PASS; `set_boot(X) rejected` |
| `update_status_test` | the status op | `update_status_test` | PASS |
| `update_verify_test` | SHA256 verification | `update_verify_test` | PASS |
| `real_update_test` | the full real flow (check→download→verify→install) against the live feed | `real_update_test` | PASS, or honest up-to-date |

Backing tooling: `tools/make_update_pkg.sh` (package builder) and
`tools/fix1_checkupdate_e2e.py` (E2E).

### Block / storage (8)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `ahci_test` | AHCI port IDENTIFY + I/O | `ahci_test` | per-port PASS; no controller → honest skip |
| `nvme_test` | NVMe admin/io queues | `nvme_test` | PASS |
| `ata_dma_test` | ATA DMA transfer | `ata_dma_test` | PASS |
| `virtio_blk_test` | virtio-blk ring I/O | `virtio_blk_test` | PASS |
| `disk_rw_test` | raw sector read/write round-trip | `disk_rw_test <device>` | data compare PASS |
| `partition_test` | MBR/GPT scan | `partition_test <device>` | table dump + PASS |
| `fs_mount_test` | mount after format — **destructive** | `fs_mount_test <device>` | formats the device, then mounts it: PASS |
| `real_hw_test` | real hardware disk + NIC section | `real_hw_test` | per-device PASS |

### NIC drivers (10)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `e1000e_test`, `igb_test`, `ixgbe_test`, `bcm57xx_test`, `rtl810x_test`, `rtl8125_test`, `rtl8139_test`, `rtl8168_test` (8) | per-driver register probe + loopback | `<name>_test` | PASS, or honest `init failed` when the card is absent |
| `nic_rw_test` | NIC frame loopback through the block-neutral API | `nic_rw_test` | PASS; `no NICs registered => SKIP` when none |
| `other_nic_test` | the best-effort families (3c59x / nForce / Atheros AR81xx / Marvell) | `other_nic_test` | PASS or honest skip |

### Audio (8)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `hda_test` / `ac97_test` / `sb16_test` / `es1370_test` / `virtio_snd_test` / `usb_audio_test` (6) | per-driver playback selftest | bare | PASS; absent driver → honest skip |
| `audio_rw_test` | PCM read/write loopback | `audio_rw_test` | PASS; `no sound cards registered => SKIPPED` |
| `sample_rate_test` | sample-rate re-program accuracy | `sample_rate_test` | PASS; `(unsupported, correctly rejected) => PASS` for illegal rates; QEMU's hda model dead-loops on 44.1 kHz — reported as a known model limitation, real hardware programs it fine |

### USB (7)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `usb_core_test` | host controller core (enumeration steps) | bare | PASS |
| `usb_hotplug_test` | attach/detach path | bare | PASS |
| `usb_hub_test` | hub enumeration | bare | PASS |
| `usb_kbd_test` | HID keyboard | bare | PASS |
| `usb_mouse_test` | HID mouse | bare | PASS |
| `usb_serial_test` | CDC-serial | bare | PASS |
| `usb_storage_test` | MSC read/write (`blk_read_sectors`, data compare) | bare | PASS |

### Legacy ports / camera (6)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `uvctest` | UVC capture (MJPEG) | bare | PASS |
| `scsitest` | SCSI/LSI 53C895A scan | bare | PASS |
| `gameporttest` | game port sticks | bare | PASS or `no stick (timeout)` honest skip |
| `paralleltest` | parallel port SPP bytes | bare | PASS |
| `irtest` | IR decode (NEC/RC5/RC6/SIRC, corrupted-frame rejection) | bare | PASS |
| `touchtest` | touchpad descriptor parse + TAP/SCROLL/PINCH/SWIPE gestures | bare | per-gesture PASS |

### Power paths (4) — can end the session

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `poweroff_test` | the power-off port chain | bare (wizard warns + confirms first) | `power-off NOT observed : FAIL` if the machine is still up; on real hardware the session simply ends |
| `suspend_test` | S3 suspend/resume | bare | `S3 resume failed.` on failure; resume = success |
| `halt_test` | the halt path | bare | live halt not observed → FAIL/SKIP verdicts |
| `reboot_test` | the reboot path | bare | the machine reboots (session ends) |

### Display / CJK (5)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `chinesetest` | CJK font + encoding battery | bare | PASS (no `FAILURES PRESENT`) |
| `font-test` | font renderer + negative fixtures (zone/wei/slot/lead/truncated/surrogate/overlong/C1 rejections) | bare | every hostile input `(rejected) => PASS` |
| `encoding-test` | GB2312↔UTF-8 codec | bare | PASS |
| `ime-test` | IME engine + session (phrase, paging, digit-pick protocol assertions) | bare | PASS |
| `tui-test` | TUI render/window/input/IME + crash-guard (`panic -> -1000` containment) | bare | verdict block PASS |

### Kernel core / diag / net / crypto (17 + 12)

| Command | What it tests | Run | Healthy output |
|---------|---------------|-----|----------------|
| `memtest` | memory service battery | bare | PASS |
| `heaptest` | heap stress + wrap guards (`wrap size 0xFF...F1 was served` must FAIL to be served honestly) | bare | PASS |
| `heapbounds` | heap boundary conditions | bare | PASS |
| `fstest` | VFS suite incl. BUG-numbered regression lines (BUG-0178/0179/0180/0182/0184/0061) | bare | every BUG line PASS |
| `vmtest` | VMM unit test | bare | PASS |
| `vmkernelpt` | kernel page-table invariants | bare | PASS |
| `vmmap` | address-space map walk | bare | dump + PASS |
| `cr3test` | CR3 audit | bare | PASS |
| `pftest` | page-fault handler counters | bare | PASS |
| `pmmrace` | PMM allocation race detector | bare | PASS (no `FAIL (race)`) |
| `irqabitest` | IRQ affinity | bare | PASS |
| `msitest` | MSI interrupts — **live injection**, see below | bare | `msitest: PASS` |
| `timer` | timer ticks | bare | PASS |
| `synctest` | sync primitives | bare | PASS |
| `multi` | multi-core wake (3 interleaved tasks) | bare | PASS |
| `spawn` | background spawn | bare | PASS |
| `exc` | exception report status (#UD/#PF/#BP counters) | bare | counters match expectations |
| `crashtest` | crash-path injection — 8 modes | `crashtest <mode>` | see the table below |
| `dumpinfo` | core dump buffer status | bare | size + usage |
| `cryptotest` | SHA-256 / AES-128 / HMAC-SHA256/384 + heap canary | bare | all PASS, `DIFFERENT (memory corruption!)` must not appear |
| `dhtest` | Diffie-Hellman | bare | PASS |
| `l1test` | L1 job/IPC/dynamic-link | bare | PASS |
| `nf_test` | netfilter rules (input drop, hits, removal restores ping) | bare | PASS |
| `tcpcc_test` | TCP congestion control (CUBIC) | bare | PASS |
| `tcptest` | TCP loopback | bare | PASS |
| `dnstest` | DNS resolver battery + built-in vectors | bare | PASS |
| `acpitest` | ACPI parser (fixture table, `\_S5` eval, live DSDT) | bare | PASS |
| `i2ctest` | I2C controllers (i801/PIIX4, DesignWare) | bare | PASS or honest `not present` |
| `smbustest` | SMBus + NACK-is-error check | bare | PASS |
| `touchtest` | (see legacy ports above) | | |

### The self-verifying help gates (3)

These parse the **live registry text itself** — they double as
registry regression gates (if a command is accidentally dropped from
the build, these fail at runtime, not just in CI):

| Command | Asserts |
|---------|---------|
| `help_default_test` | the default listing's count and shape |
| `help_a_test` | `help -a` output is strictly A-Z ordered |
| `help_wp_test` | `help -w` groups every entry under a work-package header |

### The live-fix probe (1)

| Command | What it does | Run |
|---------|--------------|-----|
| `p2test` | one of three targeted probes, driven over the QEMU serial console by `tools/wp_p2_batch_a_test.py` | `p2test utf8` pushes 174 hanzi (522 bytes, one syscall) through both write paths; `p2test read0` checks POSIX `read(0,buf,0)==0`; `p2test pipe` hammers pipe-slot reuse 100 rounds. Failure strings: `UTF8-FAIL short write`, `READ0-FAIL`, `PIPE-FAIL round=<n>` |

---

## msitest — the MSI live injection (op 21, diag service)

`msitest` is the one stub that fires a **real interrupt on your real
hardware**. Three steps, in order (`svc_diag_l1.c`):

1. **LAPIC presence + self-IPI** — registers a handler on vector 0x50,
   sends a self-IPI through the exact message-write path, spins briefly,
   and compares the vector counter: `self-IPI vector 0x50: fired N time(s) PASS`.
2. **MSI-capable device census** — walks the PCIe device list counting
   `cap_msi || cap_msix`: `MSI-capable devices: N of M`.
3. **The live fire** — finds the first e1000, arms MSI on vector 0x51,
   enables the device, then writes `0xFFFFFFFF` to the Interrupt Cause
   Set register (BAR0 + 0x00C8) to force **all** interrupt causes at
   once. A genuine MSI message write must land: `e1000 MSI fire (ICS->MSI): fired N time(s) PASS`. Then it disarms MSI and
   unregisters the vector. No e1000 or no MSI cap/BAR0 →
   `SKIP (reported honestly)`.

Final verdict: `msitest: PASS` / `msitest: FAIL`. If step 3 fails while
steps 1-2 pass, MSI routing (not the LAPIC) is broken — check
`pcibars` for the BAR0 mapping and `pcie` for link state first.

---

## Class B — flat ring-3 probes (24)

Single-file programs under `ocos/l1apps/` that exercise the kernel
surface directly. All are registered; all run bare. (The other `l1`
-tagged programs — `ush`, `ld_so`, `ldd`, `l1test` — are the shell,
the dynamic loader itself, its lister, and the job/IPC self-test,
not probes; `l1test` is listed with the diag family above.)

### Kernel-surface probes (17)

| Command | What it probes | Expected |
|---------|----------------|----------|
| `fork_test` | fork/wait4 round-trip | child exit collected, PASS |
| `exec_test` | execve replaces the image | exec'd program's output appears |
| `fdref_test` | fd refcounting across dup/fork | close semantics correct |
| `pipe_test` | pipe write/read | bytes round-trip |
| `select_test` | select on ready fds | correct readiness |
| `select_zero_test` | select with zero fds | immediate return, no hang |
| `signal_test` | signal delivery + handler | handler runs, process continues |
| `sigsegv_test` | SIGSEGV containment | `*** SIGSEGV delivered to pid=N ... kernel continues ***`, process killed or handler runs |
| `sse_test` | SSE context across switches | FP values preserved |
| `stackovf` | deliberate user stack overflow | process killed, kernel continues |
| `int3_user` | ring-3 #BP | contained, kernel continues |
| `priv_test` | privileged instruction in ring 3 | #GP delivered to the process, kernel continues |
| `pf_test` | ring-3 page fault | contained |
| `p3_test` | priority scheduling | ordering matches priorities |
| `mmap_test` | mmap basic | mapped, readable, unmapped |
| `mmap_multi` | several mappings at once | all independent |
| `mprotect_test` | mprotect W^X enforcement | write to RO page faults |

### Process-group / dynamic-link probes (7 + fixtures)

| Command | What it probes | Expected |
|---------|----------------|----------|
| `g6_test` | the G6 milestone chain (fork/exec/kill/oom), async tally | `failed tests = 0` |
| `g6_exec_a` / `g6_exec_b` | the two exec halves g6_test drives | halves' outputs |
| `g6_oom` | OOM path (address-space cleanup) | process dies cleanly, kernel lives |
| `g7mmap` | G7 mmap milestone (BUG-0281) | PASS |
| `pie_test` | PIE program load | `loaded at 0x...` |
| `reloc_test` | relocations applied | `foo_add=30 foo_global=42 fp=300` |
| `so_test` / `dlsym_test` / `dyn_hello` / `main_dyn` | solib call, dlsym, dynamic hello, dynamic main | `foo_add(2,3)=5`, `foo_add(7,8)=15`, `hello from dynamic program` |
| `hello` / `loop` / `badapp` / `p0leak` | fixtures: hello world; idle loop (job target); deliberate misbehaver; user-pointer fence (`all kernel-address probes fenced`) | as stated |

---

## crashtest — the 8 deliberate failure modes

`crashtest <mode>` injects a specific crash so you can watch the full
WP-11 recovery path (dump + crashlog + klog flush + policy). Reachable
as the bare command or through `sysdiag` classic/direct dispatch —
**deliberately not a wizard button** ("a wizard button for deliberate
panic injection would be a footgun").

| Mode | What it does | What you should see |
|------|--------------|---------------------|
| `panic` | calls `kernel_panic()` directly | `*** KERNEL PANIC: manual crashtest: kernel_panic() ***` + registers + traceback + policy line |
| `exc` | executes a ring-0 #UD | exception banner, then the panic path; `crashtest: #UD returned unexpectedly` means the fault was NOT taken — itself a failure |
| `pf` | writes to an unmapped address in ring 0 | `ring-0 exception 14 (#PF Page Fault)` panic |
| `assert` | `OC_ASSERT(0)` | panic reason `assertion failed: 0` |
| `wd` | forces a software watchdog timeout (armed + heartbeat rewound) | `watchdog timeout: ...` panic |
| `ovf` | spawns a kthread that grows 4 KiB frames until the guard page | `KERNEL STACK OVERFLOW (guard page hit, task=...)` panic |
| `dump` | `core_dump_save("manual crashtest dump")` — no panic | dump written; `crashtest: dump FAILED (no boot-time buffer)` if the boot-time buffer is absent |
| `policy halt\|reboot` | sets the post-panic action | `crashtest: policy set - after the next panic the kernel will HALT` |

Unknown variants print `crashtest: unknown variant '<v>' (try: crashtest help)`. After any panic mode, verify the record landed: `crashlog`
(32-slot ring), `dumpinfo` (core dump buffer), `dmesg` (klog FATAL
line), and on a filesystem, `/var/log/crash.log` + `/var/crash/core.dump`.

## Running the stubs from scripts

Every stub is a normal command, so the usual shell features apply:

```
ush> memtest > /tmp/mem.txt
ush> run dnstest
ush> sysdiag direct memtest        # the mega-app verbatim door
ush> logcat -k msitest -f          # follow only msitest's klog lines
```

The E2E suites do exactly this over the serial console (see
[../dev/e2e-suites.md](../dev/e2e-suites.md)); the per-app record
(`tools/run_l1_per_app.py`) boots the ISO once per program and runs it
via `run <name>`.

## Related pages

- [function-dictionary.md](function-dictionary.md) — all 296 commands
- [../troubleshooting/error-codes.md](../troubleshooting/error-codes.md) —
  what the error codes in stub output mean
- [../troubleshooting/silent-failures.md](../troubleshooting/silent-failures.md) —
  why a stub can "pass" while the feature is broken (honest-skip vs FAIL)
