<!-- SPDX-License-Identifier: Apache-2.0 -->
# Error catalog — every failure message, by subsystem

This is the full-repo failure-message inventory: the strings you
actually see, organized by component and file, each with **trigger →
investigation → fix**. ~1700 distinct error-semantic strings exist in
the tree; repeated templates are merged here with a file list, so this
page is the complete map, not a sample. Numeric codes are in
[error-codes.md](error-codes.md); failures that print *nothing* are in
[silent-failures.md](silent-failures.md).

Investigation commands are spelled for the `ush>` prompt. The general
ladder for anything: `health` → `dmesg 100` → the family's status
command → the family's `_test` stub
([../manual/hidden-commands.md](../manual/hidden-commands.md)).

---

## 1. OCBL — the boot loader (5 classes)

### 1.1 BIOS payload path (`ocbl/common/ocbl_main.c`)

- `[boot] FATAL: payload volume remount failed` — the /boot remount
  before kernel load failed. Investigate: boot the ISO's other entry;
  capture serial (`-serial file:`). Fix: re-run `install <dev>` or
  `grub-install <dev>` from a working boot.
- `[boot] VBE FAILED (kernel will refuse)` — no VBE mode; OCOS requires
  a framebuffer. Fix: enable the VGA/VBE device (QEMU: default VGA;
  real hardware: BIOS video settings).
- `[boot] FATAL: kernel load failed` — ELF load of
  `/boot/opencube.elf` failed (missing/corrupt). Fix: re-install, or
  boot slot B / the ISO and repair the volume.
- `[boot] ACPI S5 shutdown` / `[boot] S5 did not power off; halting` —
  menu shutdown path; the second line means the board ignores S5. Use
  the physical/host power control.

### 1.2 UEFI flavor (`ocbl/efi/efi_entry.c` + helpers)

- `[efi] FATAL: fixed low-memory windows not ...` — the 0x98000/0x9B000
  OCST islands are occupied by firmware. Fix: firmware setup option to
  free low memory, or boot the BIOS flavor.
- `[boot] FATAL: chainload volume remount failed` (payload variant too)
  — target ESP unreadable. Fix: verify the ESP on another machine.
- `[boot] GOP FAILED (kernel will refuse)` — UEFI GOP gave no mode.
  Fix: firmware video settings; OVMF defaults work.
- `[boot] FATAL: kernel load failed` — as §1.1.
- `[efi] FATAL: memory map failed` / `[efi] GetMemoryMap failed: 0x<hex>` — EFI memory services refused. Retry; firmware bug class.
- `[efi] EBS retry (0x<hex>)` / `[boot] FATAL: ExitBootServices refused` — ExitBootServices retried then fatal: a boot driver still
  holds a stale map key. Update firmware / use BIOS flavor.
- Chainload helpers: `[boot] FATAL: chainload buffer alloc failed`,
  `chainload target not readable: <path>`, `[boot] LoadImage failed: 0x<hex>` — the foreign loader is missing/not PE. Expected for the
  non-PE stub test disk; fix the target ESP.
- `[boot] GOP: SetMode failed, keeping current` — benign; the current
  mode is used.

### 1.3 Chainload (`ocbl/bios/ocbl_chain.c`)

- `[chain] VBR read failed` — the chosen volume's boot record is
  unreadable. Fix: the volume is not actually bootable; pick another
  entry.

### 1.4 The boot-failure judge (`ocbl/common/ocbl_statemon.c`)

- Screen: `Open Cube OS failed to boot at stage: <STAGE>` +
  `Rebooting to recovery...` — OCBL judged ≥ 8 s of RTC silence against
  the stage budget (30 s boot / 10 s per phase). The next boot
  auto-routes to OCIRE **one-shot**.
- Serial: `[state] previous boot judged FAILED at stage <STAGE_NAME> (silence <n>s) - BOOT_FAILED recorded`.
- Honest non-failures (informational): `previous boot shut down cleanly (seq <n>)`, `warm reset with recent progress (silence <n>s) - not a failure`, `previous boot armed no payload - no failure`.
- `[state] BOOT_FAILED record pending (stall <n>s) - detection degrades to the next reset` — no hardware watchdog to enforce the budget; the
  judgment lands on the next reset instead.
- `[state] USER_REQUEST recorded - recovery session` — you picked OCIRE
  in the menu.
- `[rtc] clock frozen - skipping the dwell` — RTC not ticking; silence
  cannot be measured. Fix: RTC battery / `rtc` after boot.
- ` (the kernel will halt after the stage)` — the /HANGAT diagnostic is
  armed; see [boot-failures.md](boot-failures.md).

### 1.5 Scan/menu labels (`ocbl/common/ocbl_scan.c`)

Not errors, but the visible outcome of scanning: `Open Cube OS (slot B|A, <size> KiB)` (+ ` rollback` when `next_B`+`bootfail_B` force slot
A), `OpenCubeOS Setup (OCSE)`, `OCIRE recovery`, `EFI loader`,
`<volume> (cd|hd<N> gpt|p<N>)`. Serial trace: `[scan] entries: <N>`. A
missing entry means the marker file was not found — see
[boot-failures.md](boot-failures.md) §scanner.

## 2. OCIRE — the recovery kernel (5 classes)

### 2.1 The supervisor (`ocire_bootsvc.c`)

- `Reset failed on this machine; halting.` — 8042/CF9 reset both
  ineffective. Use host power control.
- `Supervisor: no OCIRE recovery volume found ` — the hd1 0xEE+FAT12
  volume is absent. Fix: re-install with the recovery partition
  (OCSE installer creates it).
- `Supervisor: recovery volume malformed (mailbox/log missing). Entering the full shell.` — the shadow volume exists but its mailbox
  is bad. Run `syscheck`, then `repair quick` from the full shell.
- `Supervisor: mailbox write failed - skipping the reboot to avoid a reset loop; entering the full ...` — the anti-reset-loop guard
  (BUG-47 fix). The mailbox is unreliable; repair it before rebooting.
- `WARNING: log agent digest mismatch` + `The agent will NOT be injected (tamper guard).` — the agent image does not match the
  mailbox digest. Treat as tampering or corruption: `syscheck`,
  `repair`.
- `Supervisor: /BOOT/OPENCUBE.ELF not found on the ...` / `OCOS kernel too large (<n> ...)` — the kexec source is missing/oversized.
  Fix: `syscheck` then `repair` rebuilds the system volume from the
  golden set.
- `Supervisor: FATAL - cannot write the mailbox. ` — the embed channel
  is dead; recovery routing degrades. `repair`.
- `Watchdog arm FAILED - continuing without it ` — 6300ESB absent;
  recovery proceeds without a hardware leash.
- `Supervisor: kexec refused the image (rc <n>)` — the OCOS ELF failed
  validation. `syscheck`.

### 2.2 syscheck / repair (`ocire_recover.c`)

Volume verification verdicts (checks 1-10): `FAIL`, `BAD (magic/state invalid)`, `BAD (magic/cursor invalid)`, `MISMATCH (agent image vs mailbox digest)`, `FAIL (cannot parse)`, `... MISSING/UNREADABLE`,
`... DIGEST MISMATCH`, `I/O ERROR`, `4-10. skipped (volumes incomplete)`, `8-10. skipped (no manifest)` — each names a broken
piece; `repair` is the response to all of them.
- `logread: recovery volume not found (run syscheck).` /
  `logread: cannot read /LOG/BOOT.LOG.` / `logread: BAD ring header (magic/cursor) - use 'repair quick' to reset it.` — the boot-log
  channel; `repair quick` resets the ring.
- Repair flow failures: `REBUILD FAILED`, `RESET FAILED`,
  `FAILED - aborting (volume left as-is).`, `failed (re-run or use another golden set)`, `cannot verify files. Aborting.` — re-run
  repair; if it repeats, the golden set itself is damaged.
- `WARNING: this REFORMATS the OpenCubeOS system volume; all data on it will be lost.  Type YES to continue:` — the deliberate
  destructive confirm; type `YES` exactly.

### 2.3 Stall explanations (`ocire_ocbl.c`)

`The kernel failed to start or stalled in its earliest ...` (3 stage
variants, each ending `retry the boot.`), `The default L1 application (ush) failed to come up. Run syscheck to verify the system image, then retry ...`, `Unknown stall point. Run syscheck ...` — these are the
user-facing mapping of the OCBL judgment; follow the embedded advice.
Boot-record echo lines (`Recovery session requested by the user (seq <n>)`, `No boot failure was judged - this is a manual recovery session.`, `Previous boot FAILED at stage <STAGE>`) state why you are
in recovery.

### 2.4 Exceptions (`ocire_idt.c`)

`<vec #XX name>` + ` error 0x<hex>` + `System halted.` — an unhandled
fault inside OCIRE itself (7 vector names: #DE/#OF/#UD/#TS/#GP/#CP,
fallback `unknown vector`). Capture serial; this is a bug class.

### 2.5 Shell / boot honesty (`ocire_shell.c`, `ocire_main.c`)

- `Unknown command: <cmd>` — the recovery shell is tiny; `help` lists
  it.
- `Power off not supported on this machine; halting.` /
  `Reset failed on this machine; halting.` — platform limits.
- `ACPI: not found - legacy power ports will be used` — honest
  degradation, not an error.
- `=== ocire-agent: klog snapshot refused ===` — the guard-only klog op
  refused a non-guard caller; expected when probing the channel.

## 3. OCSE — the installer environment (6 classes)

### 3.1 Stage failures (`ocse/kernel/main.c`, `ocse_main.c`)

- `[ocse] STAGE <n> FAILED: <what>` — a boot stage of the installer
  kernel failed (serial + VGA + boot log). Read `<what>`; the mirrored
  kernel messages are in §5/§6 below.
- `[ocse] FATAL: no usable disk - halting` — no install target.
- `... B, ocblmbr.bin MISSING` / `[ocse] medium check: ocos.tar MISSING` — the install medium is incomplete (bad burn/download).
  Re-download the ISO; verify size/SHA.
- `direct rendering (alloc failed)` — installer GUI could not allocate
  its framebuffer.

### 3.2 ocsecl — the command line (`ocse_cl.c`)

Navigation/files: `ls: bad path`, `cd: bad path`, `cat: cannot open `,
`write: cannot create `, `touch: cannot create `, `mkdir: cannot create (exists, or parent missing)`, `rmdir: cannot remove  (not found, not a directory, or not empty)`, `rm: cannot remove `, `cp: bad source name`, `cp: destination path too long`, `cp: copy failed (err <n>)`,
`mv: moving a directory across parents is not supported` — standard
path/argument errors; `ls`/`pwd` the target first.
Partitions: `mkpart: bad start` / `bad end`, `rmpart: bad partition number`, `setflag: bad partition number`, `install: bad partition number` — numeric guards. `rmpart: WARNING: deleting a partition DESTROYS ITS DATA!` and `: WARNING: formatting DESTROYS ALL DATA on the target device!` are the destructive confirms.
tar/dd/sha: `tar: cannot open `, `tar: out of memory`, `tar: archive error (<n>)`, `dd: cannot open input: <d>`, `dd: I/O error (<n>)`,
`sha256sum: read error`, `sha256sum: bad sector count` — medium or
memory problems.
Misc: `' - unknown command (try 'help')`, `click: bad coordinate`.

### 3.3 Wizard dictionary (`ocse_i18n.c`)

`disk not found: `, `partition not found`, `invalid range (need start < end <= disk size)`, `format failed`, `... failed (busy or not a mount point)`, `install: FATAL - install medium incomplete (OCOS.TAR / OCBLMBR.BIN)`, `config: invalid key or value`, `A command failed:` —
the GUI wizard's failure texts (EN/zh pairs); each maps to the ocsecl
step named after it.

### 3.4 The installer (`ocse_inst.c`) — the step chain

`ocbl-install: cannot open <f>`, `ocbl-install: stage1 verify FAILED`,
`ocbl-install: core verify FAILED`, `install: recovery partition skipped - `, `recovery kernel unreadable`, `mkpart failed`,
`created partition not found`, `partition device missing`,
`mkfs.fat32 failed`, `mount failed`, `cannot create /boot/ocire.elf on it`, `recovery kernel short read`, `install: FATAL - no such partition: <p>`, `install: FATAL - extraction failed`,
`install: FATAL - /boot/opencube.elf missing after extraction`,
`install: FATAL - ocbl-install failed`, `install: FATAL - could not set the boot flag`, `config: cannot write the configuration` — the chain
is sequential; the FIRST message names the broken step. Almost all
reduce to: bad target disk (run the disk checks first) or bad medium
(§3.1's MISSING class).

### 3.5 Input guards (`ocse_wizard.c`)

`[wizard] refuse: existing ...`, `mount (too long)`, `install (too long)`, `write /mnt/etc/users (record too long)`, `The user record (name + full name) is too long for the ...` — length guards; shorten
the input.

### 3.6 mkfs + the mirrored pool (`ocse_mkfs.c`)

`: device name too long` — mkfs argument guard. The rest of OCSE's
driver/fs/kernel error files mirror the OCOS strings in §5/§6
byte-for-byte (same gzip/tar/config enums, same fs/driver messages) —
one shared pool; dedupe on the string, not the file.

## 4. OCOS kernel (7 classes)

### 4.1 Boot FATALs and stage FAILs (`core_boot_main.c`)

Hard failures (halt):
- `[oc] FATAL: bad multiboot2 magic` — not booted via OCBL/GRUB mb2.
  Fix: boot through the ISO's OCBL menu.
- `[oc] FATAL: multiboot2 parse failed` — mbi tag walk hit an invalid
  tag. Capture serial; loader bug class.
- `[oc] FATAL: fb_init failed` — no GOP/VBE mode passed. Fix the
  bootloader's video handoff.

Stage FAIL verdicts (`[HH:MM:SS.mmm] <stage>: FAIL`):
- `WP-01 extension self-test` — one of the 4 L1-extension canaries
  failed. Kernel surface regression; report.
- `exception self-test` — #UD/#PF/#BP self-test counters wrong. Report.
- `ACPI: RSDP not found (no ACPI power paths)` — no RSDP; power
  features degrade to legacy ports.
- `CMOS RTC: probe inconclusive (VRT not set)` — RTC battery absent
  (VMs always); `date` falls back to ms-since-boot.
- `HPET: no usable ACPI HPET table` / `ACPI PM timer: no usable PM_TMR_BLK` — timer sources degrade; TSC/PIT carry on.
- `kmalloc/kfree self-test` — heap round-trip failed. Report.

### 4.2 L1 launch (`core_boot_l1launch.c`)

- `l1 launcher: no default L1 app declared; entering recovery shell` —
  build damage (registry empty). Report.
- `l1 launcher: failed to start the default L1 app; entering recovery shell` — ush failed to start. Boot the ISO, run `help_a_test`.
- `l1 launcher: default L1 app exited; L0 halting (reboot or use the boot menu)` — ush returned (end of session; normal after
  `shutdown`).

### 4.3 OCST state area (`core_boot_state.c`)

- `absent - stage recording only, no hardware reset ` — no 6300ESB
  watchdog: boot-failure detection degrades to next-reset judgment.
- `[ocst] /HANGAT diagnostic: will halt after stage ` / `[ocst] HANGAT: deliberate halt with interrupts off ` / `[ocst] HANGAT runtime: deliberate halt with ` — the /HANGAT file is present! Delete
  `/HANGAT` from the payload volume (see
  [boot-failures.md](boot-failures.md)).
- `[ocst] L1 ISOLATION FAILURE: a user page covers 0x<hex>` — a user
  PTE maps the supervisor-only OCST mailbox window. Security check
  tripped; report immediately.

### 4.4 ELF loader, static path (`core_usermode.c`)

- `[user] no address space` — PMM/VMM exhausted at process create.
  `mem`, `free`; stop memory-hungry jobs.
- `[user] not an ELF file` / `[user] not ELF64` — the exec target is
  not a valid ELF64. Expected for `badapp`-class fixtures.
- `user: process table full (max <N> processes)` — too many
  concurrent processes. `ps`, `kill <pid>` stragglers.

### 4.5 Dynamic exec (`core_usermode.c`, `dynlink:` prefix)

`PT_INTERP: (offset out of bounds)`, `dynlink: PT_INTERP not present`,
`dynlink: no embedded ld.so for path`, `dynlink: ld.so too small` /
`not an ELF file` / `not ELF64` / `has no PT_LOAD segment`,
`dynlink: main image failed ELF header checks`, `dynlink: main PT_LOAD out of user window`, `dynlink: failed to map main PT_LOAD`, the
`libfoo` twins — the dynamic chain is broken; almost always a build or
registry-integrity issue. Verify with `ldd <program>`; re-run
`l1test`.

### 4.6 Program-header validation (both paths)

`[user] bad phentsize`, `[user] program-header table out of bounds`,
`[user] PT_LOAD file range out of bounds`, `[user] PT_LOAD vaddr+memsz overflow`, `[user] PT_LOAD memsz < filesz`, `[user] PT_LOAD vaddr in kernel space`, `[user] PT_LOAD segments overlap` —
the ELF being loaded is malformed/hostile. If it is a shipped program,
report; if it is your own, fix the link script.

### 4.7 Scheduler + config (`core_sched.c`, `core_lib_config.c`)

- `PANIC: idle task stack allocation failed - halting` — sched-init
  OOM. Increase RAM (512 MiB comfortable).
- klog WARN `kthread %s: guard page unmap FAILED at 0x%lx (stack protection absent)` — 2 MiB huge-page split failed; the task runs
  without stack-overflow protection (bug-list BUG-4). Harmless but
  report-worthy.
- `config: backend bring-up FAILED` — /etc could not be mounted;
  config degrades to ramfs (changes lost on reboot). See
  [storage-issues.md](storage-issues.md).
- `config: invalid auto_check value - using no (safe default)` /
  `config: invalid online_update value - using yes (default)` — the
  config file has junk values; defaults applied. Fix with `config set`.

## 5. OCOS services (21 classes, by family)

### 5.1 audio

- `play: out of memory` / `play: device error` — allocation or device
  failure. `sound` lists devices; `mem` checks memory.
- `: unknown device '<name>` — `play -d`/`volume` target not
  registered. Use a name from `sound`.
- `[audio_rw_test] kmalloc(48000) failed => FAIL` — OOM during the
  loopback test.
- `[audio_rw_test] no sound cards registered => SKIPPED` — honest skip;
  attach an audio device (QEMU `-device intel-hda`).
- `driver rejected the rate` / `(unsupported, correctly rejected) => PASS` — negative fixtures: illegal rates are SUPPOSED to be refused.
- QEMU-10 `hda` model dead-loops on 44.1 kHz — known model limitation;
  real hardware programs it fine.

### 5.2 block — cache + device protection

- `blkcache: alloc FAILED - cache disabled (raw I/O)` — the 64-slot
  LRU write-back cache could not allocate; the system continues
  uncached (slower). `mem`; report if persistent.
- `blk: refusing device '<name>` — registering a device with invalid
  geometry. Driver-level; report.
- `blk: write to protected device <name> REFUSED (ocire recovery disk)` / `blk: cached write to protected device REFUSED (ocire recovery disk)` — the whole-disk OCIRE protection is armed; writes to
  the recovery volume are blocked **by design**. Use OCIRE's own
  `repair` to modify that volume.

### 5.3 block — parted/mkfs/fsck commands

- `parted: device not found` / `: device name too long` — wrong device
  string. `lsblk` first.
- `mkfs.fat32: device not found` / `mkfs.fat32: device too small` /
  the exfat variants (`device too small (need at least 65 sectors, got <N>)`, `device too small for FAT + heap`) / `mkfs.ext4: device not found` — target checks. Pick a real, big enough device from `lsblk`.
- `WARNING: formatting DESTROYS ALL DATA on the target device!` — the
  confirm; bare `y` proceeds.
- fsck (FAT32 only): `fsck: cannot read boot sector`, `fsck: invalid boot signature (not 0x55AA)`, `fsck: not a FAT32 volume (fsck supports FAT32 only; got invalid BPB)`, `fsck: unsupported bytes_per_sector (fsck reads raw 512-byte sectors; got <N>)`,
  `fsck: out of memory`, `fsck: FAIL (<detail>)`, `fsck: FAIL (cluster count mismatch: counted <A> ...` — the volume is not a readable
  FAT32. `fatstat <dev>` shows the parsed BPB; reformat if the data is
  expendable.

### 5.4 block — install/abdisk/grub-install chains

- `grub-install: MBR gap too small for <N>` — the post-MBR gap cannot
  host core.img. Repartition with a ≥ 1 MiB gap (modern layout) or use
  GPT.
- `grub-install: failed to write boot sector` / `failed to write core.img` — write failure; check the medium.
- abdisk chain: `abdisk: device not found`, `device too small (need <N>)`, `WARNING - all data on <dev>`, `failed to write partition table`, `failed to register partition`, `mkfs.fat32 failed on p<N>`,
  `mount p1 failed`, `failed to write grub.cfg`, `mount p2 failed`,
  `failed to write slot A kernel`, `grub-install failed` — sequential;
  the first failure is the real one. Bad disk or too-small target
  covers most cases.
- install chain: `install: device not found`, `device too small (need >= 64 MiB ...)`, `disk too large for MBR (>2TiB) or too small`,
  `failed to write partition table`, `failed to register p1`,
  `mkfs.fat32 failed`, `mount p1 failed`, `failed to write /boot/opencube.elf`, `failed to write grub.cfg`, `grub-install failed` — same reading rule.
- `(build system error)` — an embedded asset is missing from the
  build. Rebuild.

### 5.5 camera (UVC/IR/parallel/gameport/SCSI)

- `nec corrupted rejected`, `mjpeg truncated rejected`, `set format unknown rejected`, `set frame mismatch rejected`, `capture refused before start` — negative fixtures; these PASSing is correct behavior.
- `no parallel port present - skipping live steps`, `no stick (timeout)`, `<N> failures - PASS|FAIL` — honest hardware-absence
  reporting.

### 5.6 crash

- `crashlog: flush FAILED (no filesystem mounted?)` — no VFS mount to
  write /var/log. Mount a volume, re-run `crashlog`.
- Panic-time `crash log: ... FAILED (no filesystem)` — the panic
  itself happened before/without a filesystem; the serial console
  carries the record.

### 5.7 crypto

- Self-test verdicts `SHA-256: FAIL`, `AES-128: FAIL`,
  `HMAC-SHA256: FAIL`, `HMAC-SHA384: FAIL` — crypto primitives broken:
  build/toolchain issue; report.
- `DIFFERENT (memory corruption!)` — heap canary check; report
  immediately.
- `RSA modulus too small`, `pss: bad DB padding at %d: %02x` — RSA-PSS
  padding out of spec (hostile or broken peer).
- X509 verify strings: `bad certificate format`, `unsupported algorithm`, `signature verification failed`, `hostname mismatch`,
  `issuer certificate missing`, `unknown error` — see X509_E_* in
  [error-codes.md](error-codes.md).

### 5.8 diag (the self-test console, 121 strings — the highlights)

- `usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]` — the grammar; see
  [../manual/hidden-commands.md](../manual/hidden-commands.md).
- `crashtest: #UD returned unexpectedly` / `unmapped write returned unexpectedly` / `assert returned unexpectedly` — the injected fault
  SURVIVED, which is itself a failure. Report.
- `crashtest: dump FAILED (no boot-time buffer)` — the boot-time dump
  buffer was not allocated (memory-constrained boot).
- nice guards: `nice: tid too large`, `nice: invalid tid`, `nice: priority out of range (0..31)`, `nice: invalid tid or prio (0..31)`,
  `nice: invalid tid (not in use)` — remember it is a TID (from `ps`).
- VFS regression lines `BUG-0178 legal O_RDWR write: FAIL`,
  `BUG-0179 case-sensitive ramfs: FAIL`, `BUG-0180 hole is zeros: FAIL`, `BUG-0182 mode-0000 refused: FAIL`, `BUG-0184 umount / refused: FAIL` — these mark FIXED bugs; a FAIL here is a regression.
  Report with the fstest output.
- `  FAIL: kernel PT frame was FREED by user-AS destroy (BUG-0136)` —
  same class (regression marker).
- heap wrap guards: `FAIL: wrap size 0xFFFFFFFFFFFFFFF1 was served`,
  `FAIL: 64 GiB size was served`, `FAIL: legitimate 4096 B alloc refused` — allocator hardening regression; report.
- i2c: `i2cget: error (<code>)`, `i2cset: error`,
  `controller: i801/PIIX4 not present FAIL` — controller absent (or
  the i801 status words: NACK/bus error/transaction failed/timeout).

### 5.9 display

- `chinesetest: FAILURES PRESENT` / `encoding-test: FAIL` /
  `ime_test_fail` — the CJK battery failed; report (font blob or codec
  regression).
- touchtest verdict block: `touchtest: FAIL` + per-gesture `descriptor parse FAIL`, `TAP gesture FAIL`, `SCROLL gesture FAIL`, `PINCH gesture FAIL`, `SWIPE gesture FAIL` — descriptor or gesture-engine issue.

### 5.10 font

- `chinese_font gz layout mismatch - regenerate chinese_font_data.c` —
  build-time guard (also a _Static_assert); regenerate the font data.
- The negative fixtures (`zone 95 rejected`, `wei 0 rejected`,
  `surrogate ED A0 80 rejected`, ...) — hostile inputs; each rejection
  is a PASS.

### 5.11 fs

- VFS: `vfs_init: failed to allocate global root`, `vfs_mount: invalid path`, `vfs_mount: unknown fs type`, `vfs_mount: fs mount failed`,
  `vfs: cannot unmount the root filesystem`, `vfs_mount: parent path does not exist`, `vfs_mount: mount point exists and is not a directory` — mount-argument problems; `mounts` shows what exists.
- `ramfs: mount at / failed` — fatal at boot (no rootfs); report.
- L1 face: `tree: bad path`, `mount: failed`, `umount: bad path`,
  `umount: cannot unmount the root filesystem`, `umount: target is busy - close open files first`, `umount: unmount nested mounts first`, `umount: failed (not mounted?)`, `... FAILED - no FAT32 partition on device or drive not present` — close open files,
  unmount children first (`mounts` shows nesting).
- FAT32: `fat32: invalid device string`, `fat32: read boot sector failed rc=<N>`, `fat32: invalid BPB`, `fat32: not a FAT32 volume (FAT12/16 detected)`, `fat32: ext_flags active FAT out of range`,
  `fat32: FAT cache alloc failed`, `fat32: FAT read failed` — the
  volume is not a mountable FAT32; `fatstat` shows the parsed BPB.
- exFAT: `exfat: invalid number_of_fats`, `exfat: warning - volume was not cleanly unmounted` (dirty flag — mount proceeds), the rest as
  FAT32.
- ext4 (read-only): `ext4: not an ext4 volume (bad magic)`,
  `ext4: unsupported INCOMPAT feature bits 0x<hex> - mount refused`,
  `ext4: unsupported block size (log_block_size > 6)`, `ext4: invalid inode size (must be 128..1024, power of two)`, `ext4: read root inode failed` — the volume uses features the RO driver refuses;
  mount it read-only elsewhere to inspect.

### 5.12 health

- `health: export FAILED` / `health: export FAILED (no filesystem mounted?)` — export needs a writable target; mount a volume.
- Alarms (`alarm: memory >90% used`, `alarm: <n> active tasks (>64)`,
  `alarm: CPU >=90% busy for 10 consecutive seconds`) — see
  [silent-failures.md](silent-failures.md) §health-first.

### 5.13 ipc / l1 / loader

- `unknown program: <name>` — `run`/registry lookup failed. `help -a`
  to check spelling.
- `not a dynamic executable (unknown type)` — `ldd` on a static
  program; expected.
- Loader negative fixtures (`so_unload(0) = -1 (NULL handle rejected) - PASS`, `elf_get_needed(truncated 48B) = 0 (rejected) - PASS`, ...) —
  hostile-input rejections; correct behavior.

### 5.14 klog

- `logcat: bad level (debug/info/warn/error/fatal or 0-4)` /
  `logcat: unknown option <o>` — argument guards.
- `logctl: bad level` / `usage: logctl enable|disable debug|info|warn|error|fatal` — same.
- `klog: flush FAILED (no filesystem mounted?)` / rotate / export
  variants — need /var/log; mount a volume first.
- `logctl: bad level ('-' = any)` — filter syntax note.

### 5.15 net — core

- `virtio TX timeout: avail_idx=<N> used_idx=<N> ISR=0x<hex> STATUS=0x<hex> desc_addr=0x<hex>` — the one true TX-timeout template;
the virtio NIC stopped consuming descriptors. QEMU: restart with
`-device virtio-net-pci`; report if persistent.
- `[tcp] send fail: sock=<n>` — TCP transmit error; check link state
  (`ifconfig`).
- DNS family: `DNS resolution failed`, `DNS resolution failed (timeout)`, `timeout`, `MX: no mail exchanger (or timeout)` and the
  AAAA/CNAME/NS/SRV/TXT variants — see [network-issues.md](network-issues.md).
- `DHCP failed (timeout)` — the DISCOVER→OFFER→REQUEST→ACK chain got
  no answer; see [network-issues.md](network-issues.md).
- `  UDP rx dropped (buffer full): <n>` / `  IP frag dropped: <n>` —
  socket-buffer/reassembly drop counters; heavy-load symptom.
- `firewall reset (rules, conntrack, policies)` — informational after
  `firewall reset`.
- `rule not found` — `firewall del <n>` with a stale index;
  `firewall` re-lists.
- wget: `cannot resolve host`, `out of memory`, `socket failed`,
  `connect failed`, `TLS failed (code <n>)`, `no response (timeout)`,
  `wget: cannot create file, printing to console:` — the body prints
  to console when the target file cannot be created.
- `tcptest: alloc failed` — OOM.

### 5.16 net — ssh client

- Transport: `[ssh] net_socket failed`, `net_connect failed`, `failed to send version banner`, `failed to receive server banner`,
  `[ssh] server banner too long or unterminated` — connectivity/peer
  problems.
- kex: `failed to send KEXINIT` / `receive KEXINIT` / `send KEX_ECDH_INIT` / `receive KEX_ECDH_REPLY`, `[ssh] x25519 shared secret computation failed`, `[ssh] x25519 shared secret is all-zero (invalid server key)`, `[ssh] exchange hash input exceeds 4096B page` — broken or hostile peer.
- Host trust: `[ssh] TOFU: cannot write /etc/ssh_known_hosts (read-only fs?)` (expected on ramfs /etc), `[ssh] host signature missing`, `[ssh] signature length mismatch`, `[ssh] HOST SIGNATURE INVALID - disconnecting`, `[ssh] WARNING: HOST KEY CHANGED! (possible MITM)`, `[ssh] connection refused (delete the entry in /etc/ssh_known_hosts to re-trust)` — the TOFU model speaking; delete
  the entry only if you know why the key changed.
- Auth: `[ssh] USERAUTH_FAILURE — public key rejected`, `[ssh] USERAUTH_FAILURE — wrong password`, `[ssh] publickey unavailable (no /etc/ssh_client_key), retrying with password` — credentials.
- Session: `[ssh] failed to send CHANNEL_OPEN`, `CHANNEL_OPEN failed`,
  `[ssh] rekey failed - dropping session`, `[ssh] timeout reading channel data`.
- Integrity: `[ssh] MAC verify FAIL on incoming encrypted packet`,
  `[ssh] invalid packet_length in encrypted packet`, `[ssh] unexpected msg type <n>` — tampering or version skew; capture serial.

### 5.17 net — sshd server

- `host key file corrupt; regenerating` — self-healing first-boot path.
- `ERROR: cannot obtain a per-installation host key - refusing to start sshd (embedded universal key removed)` — no key and keygen
  impossible (no entropy/memory). Pre-seed `etc.img` via
  `tools/host_keygen_etc.py` (see [network-issues.md](network-issues.md)).
- `ERROR: cannot spawn the sshd-hkgen thread` — thread-table full;
  `ps`.
- `recv_unenc: timeout reading 4-byte length`, `recv_unenc: bad pkt_len=<n>`, `recv: bad packet_length=<n>`, `recv: bad blocking, remaining=<n>` — hostile or broken client framing.
- `MAC verify FAIL on incoming packet` — tampering.
- `publickey signature INVALID`, `USERAUTH_FAILURE sent (bad credentials)`, `too many authentication failures` / `auth failure cap reached - disconnecting` — auth path.
- `second connection during active session - refusing it`, `too many connections (one session at a time)` — one session by design.
- `listen socket alloc failed`, `tcp_listen failed` — port in use /
  OOM.
- `curve25519 shared secret is all-zero (invalid client key)`,
  `exchange hash input too large` — hostile kex.

### 5.18 net — TLS (HTTPS)

- `ServerHello recv/parse failed`, `ServerHello legacy version mismatch`, `EncryptedExtensions failed`, `Certificate failed`,
  `CertificateVerify recv failed`, `server Finished verify_data mismatch` — handshake step failures; the step name is the diagnosis.
- `[tls] x509 verify failed: <why>` + ` - connection REFUSED` — the
  trust decision (see X509_E_*). Self-signed chains are REFUSED by
  default.
- `record decrypt failed (AEAD open)`, `transcript overflow (certificate chain too large)`, `tls12 hs_log overflow (chain too large)`, `[tls-dbg] PSS verify failed: transcript_len=<n>` — peer
  crypto broken/hostile.

### 5.19 net — wireless + BT

- `WEP (not supported)`, `WPA2 (not supported)`, `WPA1 (not supported)` — the honest state machine: association with these
  securities is refused.
- `associate failed (honest state machine result)`, `connection failed (honest HCI result)` — hardware/driver reality reported as-is.
- Driver-sim fixtures (`ath9k non-atheros rejected`, `iwl ucode bad magic honest`, ...) — correct rejections.

### 5.20 ocembed (the OCIRE embed channel)

- `mailbox magic mismatch at lba=%u - no signal written` /
  `log ring magic mismatch at lba=%u - append refused` — the target
  block is not a valid mailbox (OCIREMB2/OCIRELOG1).
- `L1SVC ocire op=%u refused: caller is not ocire_guard` +
  `ocire: refused (guard-only channel)` — only the OCIRE log agent may
  call cat-18 ops 4/5; expected for anyone else.
- `no block device covers the mailbox lba=%u` / `SIGNAL_OK write failed` — no recovery volume; embed inactive (normal on plain
  installs).
- `ocire embed: boot signal FAILED - see log` / `ocire: append FAILED (ring io)` — embed channel broken; OCIRE supervision degrades to
  unsupervised.
- `ocire embed: whole-disk write protection ARMED (<range> bytes, protected)` — informational: writes to the recovery disk are closed.
- `i6300esb disarm: device absent` — OCIRE/OCSE boot: no TCO to
  disarm; harmless.
- `ocire embed: agent spawn FAILED - the recovery log channel stays unused` — process table full or image missing.

### 5.21 power

- `System halted.` — normal `halt`.
- `S3 resume failed.` — suspend/resume path broken on this machine.
- `Power off not supported. System halted.` — all ports exhausted
  (OC_POWER_E_UNSUPPORTED); use host power.
- `Suspend not supported (ACPI required).` — no ACPI on this machine.
- `sleep: invalid time` — `sleep` needs a positive number (fractional
  ok).
- `poweroff_test: no power-off port took effect on this machine - power-off NOT observed : FAIL` — the probe's honest verdict in a VM.

### 5.22 selfhost (update + config + autoupdate)

- `update: download failed: <why>` / `download size mismatch` /
  `update: cannot determine target slot (not booted from A/B?)` /
  `update: install failed: <why>` / `update: setting boot failed` /
  `update: check failed: <why>` / `rollback: failed` — the `<why>` is
  the OC_UPDATE_E_* text; see [error-codes.md](error-codes.md) and
  [silent-failures.md](silent-failures.md) §OTA-chain.
- `abupdate: boot slot B (bootfail marker written)` — deliberate
  failover marker; the next boot rolls back to A.
- config: `config: cannot read /etc/opencube.conf`, `: config file missing`, `: key not found`, `: read error`, `config: write rejected (empty, non-ASCII, too long, or invalid value)`, `config: restore failed` — the value guards are strict by design; `config list` for
  valid keys.
- autoupdate: `[autoupdate] skipped: network not ready`,
  `[autoupdate] check failed (code <n>)` — silent by design; see
  [silent-failures.md](silent-failures.md).

### 5.23 sysapi

- klog WARN `kill pid=%d refused: ocire_guard process` — the OCIRE log
  agent is unkillable by design; ring 3 only sees "no such process"
  (open boundary).

### 5.24 time

- `rtc pie: invalid rate/count (rate 3..15, count 1..100000)`,
  `alarm test: internal error`, `counter test: FAIL (non-monotonic)`,
  `tsc cal: FAIL (drift > 10%)`, `tsc delay: FAILED (no calibration available)` — calibration failures; run `tsc`/`hpet`/`pmtimer`
  status.
- The honest family: `hpet: legacy route not available (honest skip)`,
  `oneshot: arm failed (honest FAIL)`, `calibration: FAILED (no samples)` — absent hardware reported honestly, not a bug.

### 5.25 tui

- Desktop: `page error`, `page '%s' failed (rc=%d)`, `tui: registry alloc failed`, `tui: desktop init failed`, `tui: desktop window failed`, `out of memory`, `command failed` — desktop internals;
  report with `dmesg`.
- Editor: `open %s failed (rc=%d)`, `%s: partial load, save refused`,
  `partial buffer: saving refused`, ` [PARTIAL: save refused]` — the
  256-line × 128-byte limits; the refusal protects your file.
- Files: `stat %s failed`, `open %s failed`, `unknown fs type: %s (ramfs/fat32/exfat/ext4)`.
- Sysinfo/update: `check failed`, `manifest has no package fields (download refused)`, `download failed (rc=%d)`, `verify failed (rc=%d) - package discarded`, `install failed (rc=%d)`, `installed but set-boot failed (rc=%d)`, `rollback failed` — the TUI mirrors of
  the OC_UPDATE codes.
- Time page: `value out of range (1980-2099, valid M/D/H/M/S)`,
  `RTC write failed (SET-window readback mismatch)`.
- Security strings `WEP/WPA1/WPA2 (not supported)` — honest.
- Extension self-tests: `menu: FAIL (`, `page: FAIL`, `widget: FAIL`,
  `cmd: FAIL`, `theme: FAIL`, `key: FAIL`, `event: FAIL`, `selftest FAILED (see log)` — run `tui-test`, then `dmesg`.

### 5.26 usb

- `blk_read_sectors failed`, `too few`, `data mismatch`,
  `short write / error` — USB MSC read/write test failures; try
  `usb_storage_test` with the stick replugged.

### 5.27 watchdog

- `watchdog: task create FAILED` — the `test` injector could not
  spawn; process table full.
- `watchdog hw: start FAILED (bad timeout?)` / `stop FAILED` / `feed FAILED (not armed?)` / `set FAILED` — hw API guards; check
  `watchdog hw status` first.
- `watchdog: register FAILED (table full?)` — 8 slots max; `watchdog slots` to inspect.
- `watchdog: bad slot id` — `feed <id>` with a dead id.
- Usage lines echo the exact ranges (`hw timeout <2..63 seconds>`,
  `timeout <id> <ms 100..3600000>`).

## 6. OCOS drivers (6 classes)

### 6.1 audio drivers

- `es1370: block IRQ timeout` / `es1370: IRQ registration failed - device disabled` — the blocking wait missed the IRQ / IRQ line lost.
- `sb16: block IRQ timeout` / `sb16: IRQ 5 registration failed - device disabled` — same template on IRQ 5.
- `hda: verb timeout verb=0x<hex>` / `hda: controller reset failed` /
  `hda: codec enumeration failed` — HDA verb channel broken; QEMU
  `-device intel-hda` or real-hardware BIOS settings.
- `virtio-snd: control timeout`, `required PCI capabilities not found`,
  `not enough virtqueues`, `queue setup failed` — virtio-snd setup;
  check `-device virtio-sound-pci`.

### 6.2 bus (ACPI / AML / SMBus)

- `ACPI: table with bad length skipped` / `ACPI: table checksum mismatch skipped` / `ACPI: no valid RSDP found` / `ACPI: RSDT/XSDT invalid` — firmware tables; the system degrades (no ACPI power
  paths).
- AML interpreter guards (hostile-DSDT protection): `AML: unsupported op 0x<hex>`, `node pool allocation failed`, `op budget exceeded`,
  `while-loop guard tripped`, `table length out of range` — the
  interpreter refuses to hang on a bad DSDT. Firmware issue.
- i801 SMBus: `NACK/device error`, `bus error/collision`,
  `transaction failed`, `timeout`, `10-bit not supported by i801-class SMBus`, `bad address`, `unsupported combined transfer` — status
  decode; mostly device-side.

### 6.3 misc (BT HCI / MJPEG)

- HCI status names `unknown-cmd`, `unknown-conn`, `hw-fail`,
  `timeout` — controller replies.
- MJPEG decoder rejects (25 guards: `missing SOI`, `bad segment length`, `progressive JPEG not supported (baseline only)`,
  `lossless/arithmetic JPEG not supported`, ...) — kernel log only;
  the UVC layer reports capture failure upstream.

### 6.4 net drivers — the merged `init failed` template

Each NIC driver prints `<name>: init failed` when its probe fails:

`e1000e: init failed`, `igb: init failed`, `ixgbe: init failed`,
`rtl8139: init failed`, `rtl8169: init failed` (rtl8168 face),
`bcm57xx: init failed`.

One unified triage (see [network-issues.md](network-issues.md)):
`lspci` (is the card on the bus?) → `pcidump <b:d.f>` (BARs sane?) →
the family status command (`e1000e`, ...) → the family selftest.
Extra: `bcm57xx: DMA memory above 4GiB - refused` — the NIC cannot
reach its DMA buffer; a memory-layout issue on that machine.

### 6.5 storage drivers

- AHCI: `ahci: issue fail (PxIS.TFE) IS=0x<hex>`, `ahci: issue fail (PxTFD.ERR) TFD=0x<hex>`, `ahci: issue TIMEOUT CI=0x<hex>`,
  `<port>: ATAPI (not supported)`, `<port>: failed to stop engine`,
  `<port>: out of memory`, `<port>: IDENTIFY failed`, `ahci: BAR5 invalid`, `<dev>) - controller skipped`.
- ATA: `ata: PIO read DRQ timeout lba=<n>`; `atadma: xfer FAIL rc=<n>`; `ata_dma: init re-entered with live controllers - refusing, ...` (double-init guard).
- NVMe: `nvme: BAR0 invalid`, `nvme: disable timeout, rewriting CC=0 and retrying`, `nvme: ready timeout, resetting and retrying`,
  `nvme: out of memory (admin queue|io queue|bounce)`,
  `nvme: Identify Controller failed`, `nvme: Identify Namespace failed`, `nvme: unsupported formatted LBA size (only 512B supported; got <n>)`, `nvme: Create I/O CQ/SQ failed`.
- virtio-blk: `virtio-blk: no legacy PCI device found (modern-only devices not supported)`, `virtio-blk: BAR0 invalid`, `invalid queue size`, `out of memory`, `WARNING: capacity read failed (legacy config not available?)`, `used-ring poll timeout`, `virtio-blk: write rejected (device is read-only)` — the last one is the QEMU
  `-drive file=...,readonly=on` semantic, not a bug.

### 6.6 USB host + class drivers

- Enumeration: `usb: GET_DESCRIPTOR(8) failed`, `SET_ADDRESS failed`,
  `GET_DESCRIPTOR(18/config9/config) failed`, `SET_CONFIGURATION failed`, `hub GET_DESCRIPTOR failed` — the device did not enumerate;
  replug, check the hub.
- DMA reachability (by design on >4 GiB machines): `usb: UHCI probe failed - driver state above 4GiB, 32-bit DMA`, `UHCI probe failed - no 32-bit-reachable frame-list page` (and schedule/ISO/bounce
  variants), `usb: EHCI probe failed - driver state above 4GiB, 32-bit DMA`, `EHCI probe failed - DMA frame at/above 4GiB`, `usb: OHCI probe failed - pool incomplete or above 4GiB (32-bit DMA HC)`.
- xHCI: `xhci: ENABLE_SLOT failed`, `ADDRESS_DEVICE(BSR) failed`,
  `xhci: port reset fail ccs=0 w=0x<hex>`, `controller reset timeout`,
  `scratchpad page alloc failed`, `probe failed, allocated frames released`.
- EHCI: `ehci: port reset fail ccs=0 w=0x<hex>`.
- OHCI: `ohci: reset timeout`.
- MSC: `msc: CBW bulk out failed`, `msc: data phase failed`, `msc: CSW bulk in failed rc=<n>`, `msc: CSW bad signature or tag`,
  `usb-msc: device not ready`, `usb-msc: inquiry failed`, `usb-msc: read capacity failed`, ` NOT registered (blk layer rejected block size <n>)`.
- Audio class: `usbaudio: SET_INTERFACE failed`, `device rejected SET_CUR(SAM_FREQ)`, `SET_CUR(MUTE) rejected`, `SET_CUR(VOLUME) rejected`.
- Serial: `usb-serial: configure failed`.

## 7. l1apps (5 classes)

### 7.1 The shared i18n templates (250 apps)

- `<app>: service call failed (rc=-1)` (i18n key `svc_call_failed`) —
  the L1SVC bridge returned -1. **Unified triage**: (1) re-run —
  transient? (2) `health` + `dmesg 100` (the service's klog line tells
  the real story); (3) run the family's `_test` stub; (4) if the
  service itself reports healthy, it is an op-argument problem — check
  the command's grammar in
  [../manual/function-dictionary.md](../manual/function-dictionary.md).
- `service call failed` (wizard variant, `wiz_svc_failed`) — the mega
  wizard's copy; same triage.
- `[save failed]` / `[save failed - stay in editor]` — editor save
  failure; usually a read-only filesystem.

### 7.2 Classic per-app errors

- `cat: cannot open ` (same template: head/tail/sort/uniq/wc/awk) —
  `ls` the path.
- `cp: cannot open '<src|dst>'` / `mv: ...` — same.
- `rm: cannot remove '<path>'` / `rmdir: cannot remove '<path>'` —
  file busy or not empty.
- `mkdir: cannot create '<path>'` — exists or no parent.
- `ln: cannot create symlink (target must be an absolute path)` /
  `ln: hard link failed (rc=<n>)` — the symlink rule is absolute-only.
- `chmod: cannot change mode` / `chown: cannot change owner` /
  `stat: cannot stat '<path>'`.
- `write: cannot open ` / `write: write failed on `.
- `awk: missing {print ...}` — the tiny awk only supports
  `{print ...}`.
- `sed: unknown script` — only `s///`, `-n Np`, `Nd` exist.
- `date: ms since boot (no RTC driver)` — fallback notice (VMs).
- `free: SYS_MEMINFO unavailable (kernel too old)` /
  `ps: SYS_PS unavailable (kernel too old)` — version skew.
- `run: fork failed` / `run: unknown program: <name>`.
- `help: unknown option '<o>'` / `(registry listing failed)`.
- ush: `ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart`, `ush: syntax error near '>'` / `near '<'`,
  `ush: alias expansion too long`, `ush: fork failed`, `ush: command not found: <cmd>`.

### 7.3 Mega-app dispatch errors

- `disk-tool: unknown op: <op>` (and the files/net-tool/sound-tool/
  sysdiag/hwinfo/power variants), `files: unknown program: <p>` — the
  verbatim table miss; `help -w` lists what exists.
- update wizard states (i18n keys `net_probe_failed`, `ab_probe_failed`,
  `ab_missing`, `ab_mismatch`, `ab_warn`, `dns_fail`, `check_unknown`,
  `install_skipped`, `install_failed`, `reboot_skipped`) — each maps to
  a wizard step outcome; the printed text names the failing step.
- `WARNING: this destroys all data on that device.` (disk-tool
  `dt_warn`) — the retype-guard path.

### 7.4 ld_so (the ring-3 dynamic loader)

- `': invalid ELF (magic/ELF64/ET_DYN/phdr bounds)`, `ld.so: bad main base 0x<hex>`, `ld.so: bad libfoo.so base 0x<hex>` — load failures.
- `ld.so: symbol reloc with bad index in '<so>'`, `ld.so: COPY reloc with bad index in '<so>'`, `ld.so: unknown reloc type <n>`.
- `ld.so: mprotect W^X failed for '<seg>'` — W^X enforcement failed.
- `ld.so: bad DT_NEEDED name in '<so>'`, `ld.so: cannot load '<lib>'`,
  `ld.so: libfoo.so missing DT_SYMTAB or DT_STRTAB`, `ld.so: failed to relocate libfoo.so` / `main program` — dynamic-chain damage; re-run
  `l1test`, `so_test`, `dlsym_test`.

### 7.5 Test apps

- p2test: `UTF8-FAIL short write`, `READ0-FAIL`, `PIPE-FAIL round=<n>` (+ `pipe()=-1 (slot table exhausted?)`) — live-fix probes;
a FAIL is a real regression of the fix they guard.
- help gates (`help_a_test` etc.): FAIL = the registry listing shape
  changed — build or i18n damage.
- g6_oom / stackovf / pf_test / sigsegv_test / int3_user / badapp —
  deliberate crashers; their "failure" output is the kernel's
  containment notices (§kernel).

---

## 8. Known bugs — bug-list.md, 1 P0 + 35 P1 (3.0.0 export)

### P0

- **BUG-AUD-2a-001** (kernel/syscall security) — ring 3 can reach
  kernel memory through standard read/write syscalls (details withheld
  by security policy). Until fixed: treat every ring-3 program as
  potentially privileged; do not run untrusted binaries.

### P1 (symptom → cause → what to do)

1. **BUG-3** OCIRE — interrupts permanently disabled after one
   `core_sched_task_states` call (flags-restore idiom). Symptom:
   recovery kernel goes dead after a scheduler call. Fixed in tree;
   report if an old build reproduces.
2. **BUG-4** kernel-stack guard page unmap is a no-op on 2 MiB huge
   pages (+ stale global TLB). Symptom: the klog WARN
   `kthread %s: guard page unmap FAILED` and weaker stack-overflow
   protection. Known; tracked.
3. **BUG-5** ush kill race — victim AS destroyed before EXITED mark; a
   timer tick can schedule the victim on a freed CR3. Symptom: rare
   crash right after `kill`. Fixed; `wp01_killrace_test.py` guards it.
4. **BUG-6** exec-failure/OOM paths leak the whole address space
   (`user_process_create`). Symptom: memory slowly vanishes on failed
   execs. `mem`; reboot clears.
5. **BUG-7** `sys_execve` destroys the old AS before the new one is
   built; exec cannot fail gracefully. Symptom: exec failure kills the
   process hard. Known.
6. **BUG-20** ACPI AML OpRegion access has no bounds checks — a
   malicious DSDT gets arbitrary physical R/W. Do not boot untrusted
   firmware tables.
7. **BUG-21** TLS cert-chain verification failure only warned and
   continued. **Now REFUSED by default** (the HTTPS E2E item 0); the
   explicit opt-in is `config set tls.insecure 1`.
8. **BUG-22** DNS transaction ID hardcoded 0x1234, responses
   unvalidated — offline poisoning possible. Symptom: bizarre
   resolutions on hostile LANs. Known.
9. **BUG-24** TCP RTO retransmit in IRQ-off timer context can
   busy-wait in ARP; an expired ARP entry deadlocks the machine.
   Symptom: hard freeze under packet loss. `tcpstats` before/after.
10. **BUG-31** ush command-not-found path unbounded concat into
    `path[64]` (stack smashing). Fixed; type garbage commands only on
    current builds.
11. **BUG-32** Tab completion unbounded append into `line[512]`.
    Fixed (bounded appends).
12. **BUG-33** alias expansion unbounded strcat into `newcmd[512]`.
    Fixed.
13. **BUG-34** editor: open an over-limit file then save = silent
    destruction. **Now save REFUSES** (`[PARTIAL: save refused]`).
    Never force-save a partial buffer.
14. **BUG-35** the `write` app opened RDONLY then wrote; failure
    ignored (silent no-op). Symptom: `write <file>` produced nothing.
    Fixed in 3.0.0.
15. **BUG-36** `cp x x` truncates, `mv x x` deletes (no same-file
    guard). **Still live**: never cp/mv a file onto itself.
16. **BUG-46** OCBL ELF loader bounds checks bypassable via integer
    wraparound. Fixed (overflow-safe math).
17. **BUG-47** OCIRE supervisor mailbox-write failure formed an
    infinite reboot loop. Fixed (the anti-reset-loop guard you see as
    `skipping the reboot to avoid a reset loop`).
18. **BUG-63** release chain: boot gate used a staleable /tmp file +
    `|| true` swallowed QEMU failure. CI-level false green.
19. **BUG-64** deep-regression gate assertion structurally always-true.
    CI-level.
20. **BUG-65** E2E scripts did not propagate failure exit codes.
    CI-level.
21. **BUG-66** screen content never machine-asserted. CI-level.
22. **BUG-90** docs version/status claims stale in README and friends.
23. **BUG-91** website stuck at an old build (site-data.js).
24. **BUG-92** OCIRE banner claimed the wrong version. Fixed
    (`ocire_version_gen.h`).
25. **BUG-99** website ↔ gh-pages out of sync.
26. **BUG-102** README documented the wrong build command.
27. **BUG-104** README release-note SHA256 claim not implemented.
28. **BUG-107** published ISO local copy size/SHA mismatch.
29. **BUG-109** whole-machine silent freeze race — typing 'a'/'u'
    rapidly or running `loop` can freeze the system with NO panic and
    NO watchdog takeover. **The nastiest live P1**: if it reproduces,
    capture `-serial file:` and report; the workaround is simply not
    racing the console input.
30. **BUG-110** L1 app stdout Chinese renders as tofu on screen
    (serial fine) — the console fallback lacked the UTF-8 fix
    (BUG-130 is the root cause).
31. **BUG-111** nano/vi exit does not clear the screen (afterimages
    overlap the prompt). Cosmetic; `clear` after exiting.
32. **BUG-112** busy-wait `loop`: 35 s+ with no preemption, no health
    alarm, no watchdog kill — the declared containment chain fails
    live. Symptom: the machine feels dead while `loop` runs. `kill`
    from another path or wait it out; tracked.
33. **BUG-113** OCSE GUI mouse clicks all ineffective (hover renders,
    clicks don't trigger). Use the CLI (Ctrl+Alt+F2) or `click <x> <y>` command.
34. **BUG-129** OCSE installer↔kernel config dialect split (wizard
    writes dead keys `lang/hostname/boot.slot`; kernel reads
    `shell.lang/sys.hostname`). Symptom: installer settings ignored at
    first boot; the kernel rewrites the config. Set keys again from
    the running system (`config set shell.lang zh`).
35. **BUG-130** ush `sys_write2` console fallback lacked the UTF-8
    fix — all ush output tofu on screen (root cause of BUG-110).
    Fixed with the WP06g1 fix.

(62 P2 + 41 P3 are counted in bug-list.md but not detailed here.)

## 9. KNOWN_ISSUES postmortems (19 fixed) + 3 open boundaries

### 2.0.0 postcheck — all fixed in 2.1.0 (17)

P-01 the tree did not compile on CI (imm8 port constraint ≥ 0x100;
GRUB efi_uga module; C23 `bool` collisions) → port I/O via `%dx`,
module filtering, C23-aware types. P-02/P-03 release-note arithmetic +
231/233 command mapping rows (a regex dropped 2) → ledger regenerated.
P-04/P-05 interface truth tables + deletion register. P-06/P-07 docs
rewritten to v2 reality. P-08 per-app L1 test matrix added. P-09
Makefile comment. P-10..P-16 website data/assets/evidence/count
calipers/governance. P-17 false-positive text defect recorded
honestly.

### 2.1.0 open — all fixed in 2.2.0 (2)

O-01 wp10_tui #DF after a checkupdate round: the deepest call chain
(SYS_L1SVC→ota→https→tls→x509→rsa→bn mont, ~23.6 KB) + IRQ nesting
exceeded the 20 KiB kthread stack → `OC_KTHREAD_STACK_PAGES 6→10`
(40 KiB window); the corrupted #DF banner ("andled") also fixed. If
you ever see a garbled panic banner, suspect stack pressure.
O-02 wait4-vs-fork flaky race — actually deterministic (dead parent's
tid reused, stale zombie exit code collected early) → orphan-detach +
fork corpse paths detach; 20/20 clean boots verified.

### Open boundaries at 3.0.0-WP03 (3, by design)

1. ush `kill` of the ocire_guard process reports generic "no such
   process"; the real reason is only in klog WARN (`kill refused: ocire_guard process`). Use `dmesg` when a kill "mysteriously"
   fails.
2. A bare `sti;hlt` park remains in 4 proven paths (sys_read keyboard
   wait, wp8cd fg wait, sdhci timed wait, OTA settle loop) — a
   hard-freeze risk class if interrupts are lost; documented, not yet
   restructured.
3. The direct-boot GRUB entry carries no oc.* facts — unsupervised by
   definition (boot through OCBL for the full state machine).

## Related pages

- [error-codes.md](error-codes.md) — the 61 numeric codes, the 11
  panic sites, log formats
- [silent-failures.md](silent-failures.md) — failures with no message
- [boot-failures.md](boot-failures.md) /
  [network-issues.md](network-issues.md) /
  [storage-issues.md](storage-issues.md) — per-domain deep dives
- [common-errors.md](common-errors.md) — the short list you reach for
  first
