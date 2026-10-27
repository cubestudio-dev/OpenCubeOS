<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCIRE - OpenCubeOS Independent Recovery Environment

Status: **3.0.0-WP-02-ocire_recovery** (this work package ships the
recovery features: the boot supervisor with watchdog-based
boot-failure detection, and the full recovery shell with syscheck /
repair).

## What OCIRE is

OCIRE is a second L0 kernel, fully independent of the Open Cube OS
(OCOS) kernel.  It lives in its own source tree (`ocire/`), shares no
headers with OCOS, links with its own linker script
(`ocire/linker_ocire.ld`) and produces its own boot image
(`build/ocire/ocire.iso`).  The two-kernel architecture:

- **L0 = provides services** — OCOS stays the full-service kernel.
  OCIRE is an L0 *peer*: a recovery kernel that boots when OCOS
  cannot.
- **Independent** — OCIRE must be able to boot, run and power-cycle
  the machine without any OCOS code, data or partition being
  reachable or required.

## What WP-01 ships

| Area | WP-01 | WP-02 (this) | WP-03 |
|---|---|---|---|
| Boot + minimal shell | **done** | — | — |
| help / shutdown / reboot | **done** | — | — |
| Recovery features, FAT12, self-check, repair | — | **here** | — |
| Boot-failure detection, 1-minute timeout | — | **here** | — |
| OCOS <-> OCIRE interface, process protection | — | — | here |

## What WP-02 ships

1. **The boot supervisor** (`ocire.mode=sup`, the dual-boot menu
   default): scans the disks, runs the mailbox state machine, arms
   the i6300esb watchdog for a 60 s boot window, stages and
   digest-verifies the ring-3 log agent, then kexecs the OCOS kernel
   with the mailbox / log / wdt facts on the command line
   (`oc.log= oc.logsecs= oc.mb= oc.wdt= oc.agent=1`).
2. **Boot-failure detection**: a watchdog reset that lands the
   supervisor on a still-`WAIT_BOOT_SIGNAL` mailbox means OCOS never
   came up - the supervisor prints the crash notice, bumps
   `fail_count`, sets `ENTER_FULL` and reboots into the full recovery
   shell.  Proven end to end in QEMU with the real i6300esb device
   (`-device i6300esb`): arm -> kexec -> 60 s expiry -> reset ->
   crash notice -> recovery shell.
3. **The full recovery shell** (menu entry 2, or `ocire.mode=full`):
   `logread` (dump the boot-log ring), `syscheck` (ten-point
   integrity self-check), `repair quick` (fix MBRs / mailbox / log
   header + restore any system file whose digest differs from the
   golden manifest), `repair full` (DESTRUCTIVE reformat of the
   system volume + full golden rewrite, typed-YES confirmation),
   `return` (normal-boot mailbox + reboot), plus the WP-01
   `help/shutdown/reboot`.
4. **FAT12 read / FAT32 read+write**: the streaming helpers
   (`ocire_fat_read_range`, `ocire_fat_hash_file`,
   `ocire_fat_copy_stream`) let the recovery shell hash and restore
   multi-megabyte files without buffering them; allocation carries a
   reservation marker so two consecutive allocations can never hand
   out the same cluster, and names that do not fit the 8.3 form are
   rejected rather than silently truncated.

### Honest boundary (WP-03 scope, by design)

The OCOS side of the closed loop is **not** in this work package:
OCOS does not yet parse the `oc.*` command-line facts, does not write
the `SIGNAL_OK` mailbox state, does not disarm/ping the i6300esb, and
does not spawn the injected agent (the `L1SVC_CAT_OCIRE` channel the
agent calls does not exist yet).  Consequences, measured and declared:

- On real hardware with an i6300esb present, a supervised boot that
  reaches OCOS still hard-resets 60 s after hand-over (nobody
  disarms the window) - entry 3 "Open Cube OS (direct boot)" is the
  interim path for normal use.
- Without the watchdog (the default QEMU setup), the mailbox-only
  signalling means every second consecutive supervised boot degrades
  into the recovery shell (un-signalled = presumed crash).  The
  recovery shell's `return` resets the mailbox to NONE.

## Source layout (ocire/)

```
ocire/
  ocire_mb2_header.S    multiboot2 header (80x25 text mode request)
  ocire_boot.S          32-bit GRUB entry -> GDT + 4 GiB identity map
                        -> long mode -> ocire_main; exception stubs
  ocire_main.c          kernel entry: console, IDT, ACPI, shell
  ocire_console.c/.h    dual console: COM1 115200 + VGA text 0xB8000
  ocire_input.c/.h      serial + PS/2 polled input (shift/caps/backsp)
  ocire_idt.c/.h        minimal IDT: 32 exception vectors, faults
                        print a diagnostic instead of triple-faulting
  ocire_acpi.c/.h       RSDP/XSDT/FADT (checksums verified), tiny \_S5
                        package reader, PM1a S5 + RESET_REG + 8042
  ocire_mb2.c/.h        multiboot2 info parsing (framebuffer/cmdline/
                        modules - WP-02 group 1)
  ocire_font.c/.h       embedded 8x16 font for the LFB console
  ocire_ata.c/.h        ATA PIO driver (IDENTIFY, LBA28 read/write)
  ocire_sha256.c/.h     SHA-256 (golden/agent/kernel digests)
  ocire_fat.c/.h        FAT12 read + FAT32 read/write (mount, lookup,
                        streaming read/hash/copy, create/mkdir/format)
  ocire_wdt.c/.h        Intel 6300ESB watchdog (PCI 8086:25ab probe,
                        arm/disarm/ping, 1 kHz two-stage window)
  ocire_bootsvc.c/.h    the boot supervisor (mailbox state machine,
                        agent staging, kexec hand-over)
  ocire_kexec.c/.h      OCOS ELF staging + mb2 info rebuild + jump
  ocire_kexec_jump.S    64-bit kexec trampoline
  ocire_disk.c/.h       recovery-disk discovery, mailbox, golden image
  ocire_recover.c/.h    the full recovery shell commands (logread/
                        syscheck/repair/return)
  ocire_shell.c/.h      the command loop (8 commands)
  ocire_util.c/.h       freestanding mem/string helpers
  ocire_io.h            port I/O inline helpers
  ocire_types.h         base scalar types
  agent/ocire_agent.c   the ring-3 log agent (OCOS L1 ABI, injected
                        at kexec; its L1SVC channel is WP-03)
  ocire.mk              build targets (included from the top Makefile)
  linker_ocire.ld       own link layout (1 MiB load address)
  grub-ocire.cfg        standalone ISO menu (single entry)
  grub-dual.cfg.in      dual-boot disk menu TEMPLATE (3 entries:
                        supervisor default, full shell, direct OCOS)
```

Build targets: `make ocire-kernel`, `make ocire-iso`, `make
ocire-disk`, `make ocire-test`, `make ocire-dual-test`, `make
ocire-shell-test` (run targets `ocire-run-bios/uefi[-gui]` are also
available).  Compilation uses the same `-Wall -Wextra -Werror`
zero-warning gate as the OCOS kernel; CI runs the OCIRE build on
every push.  The OCIRE object files carry their own `-include
$(OCIRE_OBJ_C:.o=.d)` header-dependency tracking in ocire.mk (the
top-level Makefile only includes the OCOS kernel's .d files - a
header change in ocire/ used to silently link stale objects; found
and fixed live by the WP-02 battery).

## How the two OSes coexist (WP-01 layout)

```
hd0 (ocire-boot.img, built by tools/make_ocire_disk.sh)
  MBR + GRUB (BIOS: boot.img + core.img in the post-MBR gap;
              UEFI: /EFI/BOOT/BOOTX64.EFI on the FAT32 partition)
  p1 FAT32: /boot/opencube.elf (OCOS kernel)
            /boot/ocire.elf    (copy of the OCIRE kernel)
            /boot/grub/grub.cfg (the dual menu)
  -> GRUB menu entry 1 boots OCOS (default)
  -> GRUB menu entry 2 boots OCIRE

hd1 (ocire-hd.img) - the OCIRE disk
  MBR with ONE entry, type 0xEE ("GPT protective shadow"), no GPT
  header behind it; the raw OCIRE kernel image is stored at the
  partition start (LBA 2048).
  -> the GRUB menu loads it with a whole-disk blocklist:
     multiboot2 (hd1)2048+128
  -> OCOS refuses the entire partition table (0xEE makes its parser
     switch to GPT mode, which fails without a GPT header), so the
     OCIRE partition has no device node and no filesystem path in
     OCOS - see "Partition isolation" below.
```

Two lessons from bring-up, encoded here for the record:

1. GRUB 2.12's msdos partmap **skips** 0xEE entries — a partition path
   like `(hd1,msdos1)` does not exist for GRUB even though the same
   entry is perfectly valid on disk.  The menu therefore uses a
   whole-disk blocklist.
2. GRUB menus have no digit hotkeys; automation selects entries with
   the ANSI cursor-down sequence over the serial menu terminal.

## Partition isolation (the WP-01 acceptance test)

Measured in `evidence/3_0_0_wp01_ocire/isolation.log` from inside a
running OCOS with both disks attached:

- `partscan: 1 partition device(s) registered` — the OCIRE disk
  contributes **zero** child devices (only the dual-boot disk's p1
  registers).
- `parted hda` (OCIRE disk) → `no partition table found`.
- `mkfs.fat32 hdap1 confirm` → `device not found` — there is no node
  to write through.
- `disk_rw_test hdap1` → `device lookup FAIL` — there is no node to
  read through.
- `df` — only OCOS volumes are mounted; nothing from the OCIRE disk.

Honest boundary (also measured, `isolation_rawdecl.log`): whole-disk
device nodes still exist — `disk_rw_test hda` and `mkfs.fat32 hda
confirm` both work on a throwaway copy, exactly like Linux root with
`/dev/sdb`.  Blocking administrator-level whole-disk operations
requires an OCOS block-layer policy — an OCOS kernel change this
work package explicitly forbids — and belongs to WP-03 (process
protection).

## Tests

- `tools/run_ocire_tests.py` (`make ocire-test`) — 14 cases, BIOS +
  UEFI: boot to the shell prompt, `help`, unknown-command hint,
  backspace editing, `shutdown` (ACPI S5; QEMU really exits) and
  `reboot` (8042/RESET_REG; with `-no-reboot` QEMU really exits).
- `tools/run_ocire_dual_tests.py` (`make ocire-dual-test`) — 11
  cases: the four dual-boot combinations (BIOS/UEFI x menu default
  supervisor->kexec->OCOS / menu-pick OCIRE full shell) on throwaway
  disk copies (the supervisor is stateful - see the WP-03 boundary),
  the five isolation probes and the two whole-disk declaration
  probes.
- `tools/run_ocire_shell_tests.py` (`make ocire-shell-test`) — the
  WP-02 recovery battery, 4 cases on throwaway copies:
  `shell_healthy` (syscheck 10/10 + idempotent quick repair on a
  pristine disk), `shell_quick_repair` (kernel-ELF byte flipped ->
  syscheck catches the digest mismatch -> repair quick restores +
  verifies -> healthy again), `shell_full_repair` (p1 BPB nuked ->
  boot from the STANDALONE ISO, hd0's own GRUB cannot read its
  config any more -> repair full rewrites the volume from the golden
  image -> healthy) and `shell_wdt_timeout` (the real i6300esb
  attached: supervisor arms the 60 s window -> kexec -> OCOS boots
  but never signals -> watchdog reset -> crash notice -> the full
  recovery shell).
- Full OCOS regression matrix on a tree that contains OCIRE but zero
  OCOS edits: `evidence/3_0_0_wp01_ocire/regression/` (host tests,
  reproducible-check, wp12, wp10_tui, chinese, tuiqa x2, native x2,
  l1_per_app, wait4 fresh-boot battery 20/20, boot gates) — all green
  and identical to the 2.2.0 release baseline.
