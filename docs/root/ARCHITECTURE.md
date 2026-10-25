<!-- GENERATED mirror of the repository-root ARCHITECTURE.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS Architecture

This document describes the tree as it exists in 2.1.0 - every
component named below is a real file you can open. Line counts and
per-directory detail live in `docs/data/stats.json` (regenerate:
`make api-docs`).

## The two rings

Open Cube OS is a two-ring system:

- **L0 - provides services.** The kernel plus drivers, service
  implementations and the WinRE-style recovery environment (the
  `oc>` shell). L0 owns the hardware and every service body.
- **L1 - uses services.** Ring-3 user applications that reach L0
  capability through the extension surface (the `SYS_L1SVC` op
  table and the classic syscall ABI). The default boot entry is the
  ring-3 `ush` shell, declared in `services/l1/l1_launcher.c`
  (`OC_L1_APP_DECL("ush", ..., dflt=1)`); `oc>` is the fallback
  after ush exits or fails to start.

```
                  GRUB 2.12 (hybrid BIOS + UEFI ISO)
                                  |  multiboot2 + kernel-as-module
        +-------------------------+-------------------------+
        |                   boot/boot.S                    |
        |  32-bit entry -> GDT -> 2 MiB identity map of    |
        |  the first 4 GiB -> PAE+LME+PGE -> long mode     |
        +---------------------------------------------------+
                                  |
   core/main.c (kmain: bring-up order = the boot log you see)
                                  |
   +-----------+-----------+------------+------------+
   |           |           |            |            |
 arch/x86_64  core/mem    core/lib    core/       services/l1
 IDT/GDT/TSS  PMM/VMM/    string/tar  scheduler   launcher,
 exceptions   heap        gzip/config syscalls 54  SYS_L1SVC(110)
 8259 LAPIC   4 KiB       klog        ring3+ELF    + selftests
 IRQ timers   pages       config      signals
                                  |
   +----------------+---------------+-----------------+
   |                |               |                 |
 drivers/*       services/*      recovery/        tui/ chinese/
 16 families     17 folders      the oc> shell    the v1 TUI/CJK
 block NVME      folder == L1SVC 99 commands     command trees,
 AHCI ATA DMA    category:       (self-tests,     still compiled
 virtio-blk      fs net crypto   watchdog, crash  INTO the kernel,
 USB UHCI/OHCI/  ota klog health policy, logs,    reachable from
 EHCI/xHCI       watchdog power  chipset diag,    oc>; CN console
 MSC HID audio   time block nic   file commands,  + pinyin IME
 serial, PCI     snd display     ssh/sshd)       (CONFIG_CHINESE)
 PCIe ACPI      input camera
 I2C SMBus      usb + services/l1
 HPET/PMtimer/  itself
 TSC/RTC
 wireless bt SD camera UVC IR gameport parallel SCSI LSI
                                  |
                            l1apps/  (ring 3)
   ush - the boot shell (the only OC_L1_APP_DECL application):
   133 unique SVC rows -> SYS_L1SVC, 19 built-ins, 4 NETCMD
   bridges; plus the embedded test programs (36 files: hello,
   fork/exec/pipe/signal/select/mmap, dlsym/PIE dynamic
   linking, g6/g7 chains) runnable through `run`.
```

## Boot flow (what the serial log shows, in order)

1. GRUB loads `boot/opencube.elf` (the kernel is also attached as a
   multiboot2 module tagged `self` - the installer pattern, so
   `install`/`abdisk` can copy the *exact* running kernel onto a
   disk).
2. `boot/boot.S`: 32-bit entry point, own GDT, identity-map the
   first 4 GiB with 2 MiB pages, PAE + LME + PGE, switch to 64-bit
   long mode, call `kmain`.
3. `arch/x86_64/arch_multiboot2.c` parses the multiboot2 info
   (framebuffer, memory map, command line, module list, ACPI RSDP
   tags) - with full malformed-input hardening: per-tag minimums,
   NUL-termination checks, truncated-tag rejection.
4. `kmain` brings up, in boot-log order: framebuffer console and
   font, IDT/GDT/TSS (256 gates, IST stacks for #DF/#MC and debug),
   8259 PIC remap, PIT @ 100 Hz, PS/2 keyboard, COM1 serial RX,
   line editor, exceptions (with a live #DE/#UD/#PF self-test),
   PMM, VMM, heap, then the collected driver/service domains,
   VFS, network, crash recovery.
5. Services and drivers declare themselves in-file
   (`OC_SERVICE_DECL` / `OC_DRIVER_DECL` collected from linker
   sections - `core/core_registry.c`), init runs, and the launcher
   starts the default ring-3 application: the `ush>` prompt. (The
   kernel links in two passes; `tools/embed_grub.py` embeds the
   GRUB boot+core images as `core/generated/grub_boot_data.c` so
   `grub-install`/`install` can write the exact boot images.)

## Memory management

- **PMM** (`core/mem/mem_pmm.{c,h}`): bitmap allocator over 4 KiB
  page frames from the multiboot2 memory map; reserved regions
  (kernel code, framebuffer, mbi) marked used at init; O(1)
  alloc/free.
- **VMM** (`core/mem/mem_vmm.{c,h}`): 4-level page tables; the
  kernel keeps the boot-time identity map of the first 4 GiB; APIs
  create and manipulate additional address spaces (user
  processes). Page protection flags map to rw/nx/user bits.
- **Heap** (`core/mem/mem_heap.{c,h}`): kmalloc/kfree/krealloc/
  kzalloc, free-list with first-fit and coalescing, 16-byte
  aligned blocks, 16 MiB fail-fast ceiling.
- **#PF growth**: stack-growth and heap-growth page faults are
  real fault handling in the exception path - illegal accesses
  escalate to the crash path.

## Scheduling and time

- **Scheduler** (`core/core_sched.{c,h}`): preemptive, priority +
  time-slice round-robin over task control blocks; context switch
  in `arch/x86_64/arch_context_switch.S`-family assembly with
  ABI-probe-verified alignment.
- **Timer** (`core/core_timer.{c,h}`): PIT at 100 Hz drives ticks,
  `g_boot_time_ms` and the soft-timer wheel. HPET, the ACPI PM
  timer and the TSC are supported as *additional* time sources
  (`drivers/time/`) - and as user-facing commands from both shells.
- **Watchdog** (`services/watchdog/core_watchdog.{c,h}`): monitored
  task slots with timeout policy (the `watchdog` command), plus
  true hardware watchdog bring-up where firmware offers it - Intel
  TCO (verified live on QEMU q35) and ACPI WDAT tables.

## Interrupts and exceptions

`arch/x86_64/arch_idt.{c,h}` + `arch_irq.c`: 256-entry IDT, TSS
with IST1 (panic stack for #DF/#MC) and IST2 (dedicated
exception-exit stack - never a context switch on IST2), 8259 remap
to vectors 32-47, LAPIC handling, and a full exception frame path
that feeds crash dump generation. A live 3/3 (#DE/#UD/#PF)
self-test runs on every boot.

## Ring 3, syscalls, the L1 surface

- **Syscall ABI** (`core/core_syscall.h`): `rax`=number,
  `rdi/rsi/rdx/r10`=args (r8 = 5th arg for SYS_L1SVC), `rax`=result.
  54 syscall numbers (verify: `grep -c '#define SYS_'
  core/core_syscall.h`).
- **The service surface** (`services/l1/l1svc_defs.h`): the
  numbered L1SVC op table - **17 categories / 158 ops**. Category
  number selects the `services/<category>/` folder, op selects the
  command; ush routes `SVC_CMDS` rows through `SYS_L1SVC(110)` to
  the owning L0 service body. Four network commands
  (ifconfig/netstat/ping/wget) reach their L0 bodies through the
  `SYS_NETCMD(101)` bridge instead.
- **Counting calibers** (2.1.0, stated exactly): command homes are
  **251 unique names = 99 L0 (`oc>`) + 133 ring-3 SVC + 19 ush
  built-ins**. The ush `SVC_CMDS` table itself carries 142 rows -
  the 9 dual-homed names (rtc/hpet/pmtimer/tsc/shutdown/poweroff/
  reboot/halt/sleep) are registered on both sides and stay callable
  from both shells. The v1.0.0 baseline is 233 commands, every one
  mapped in `INTERFACES.md` Part 1c.
- **Interface history** (2.1.0, per-entry in `INTERFACES.md` Part
  2c/2d): of the v1 surface headers' 278 callable declarations,
  267 survive public, 6 register-class hooks moved into per-family
  `services/*/drv_internal.h` ('internal since 2.0.0'), and 5
  init/probe entries were deleted - each rebuilt inside the
  collected-declaration service model (`*_svc_init` /
  `*_svc_probe_all`).
- **User programs** (`l1apps/`): 36 files - the ush shell itself,
  hello/fork/exec/pipe/signal/select/mmap tests, dynamic linking
  with a real in-tree loader (`l1apps/ld_so.c`, `dlsym`, PIE and
  relocation tests) - embedded into the ISO and runnable via `run`.

## Filesystems

- **VFS** (`services/fs/fs_vfs.{c,h}`): uniform open/read/write/
  seek/close/stat/mkdir/rmdir/readdir/lookup over registered fs
  types; the node tree crosses mount points transparently; per-fs
  case sensitivity; permission bits enforced; i64 seek.
- **Filesystems** (`services/fs/`): FAT32 (LFN with alias +
  collision scan + checksum validation, dual-FAT mirror, FSINFO),
  exFAT (active FAT), ext4 (read/write with extent support), ramfs
  (sparse holes zeroed).
- **Block service** (`services/block/` + `drivers/block/`):
  register/read/write/flush with per-instance names, partition
  scanning, AHCI, ATA/ATA-DMA, NVMe, virtio-blk. Driver families
  self-register (`OC_DRIVER_DECL`); the service half exposes the
  ring-3 surface (mkfs.*, fsck, abdisk...).
- **/etc volume**: a 32 MiB FAT32 image mounted at /etc holding
  `opencube.conf`, SSH host keys, kernel logs - so configuration
  survives reboots (`etc.img` target).

## Network stack

`services/net/net_core.{c,h}` and friends - a self-contained stack:
PCI discovery, e1000-family NIC drivers (e1000/e1000e/igb/ixgbe;
rtl8139/rtl8169-family; BCM57xx), Ethernet + ARP + IPv4 + ICMP +
UDP + DNS + TCP with congestion control (`net_tcp_cc.c`), a socket
API for user programs, stateful firewall, `wget`/`dns`/`ping`/
`route` commands. On top of it: a TLS 1.2/1.3 client with
RSA/ECDHE, X.509 chain verification (RFC 6125 hostname policy) and
a real SSH client + SSH server (`net_sshd.c`, key exchange via
curve25519/ed-style primitives, host keys on /etc). The DRBG
seeding is RDRAND + re-mixed RDTSC (NIST SP 800-90A HMAC_DRBG).

## Drivers

16 families under `drivers/` - each with its own bring-up manual
page under `docs/drivers/`. Highlights: UHCI/OHCI/EHCI/xHCI USB
stacks with MSC (mass storage), HID, serial, audio classes;
HDA/AC97/SB16/ES1370/virtio sound; i2c + SMBus; wireless
(ath/brcm/iwlwifi/rtlwifi families) and bluetooth
(HCI/L2CAP/RFCOMM/SDP/SPP + HID) layers; UVC cameras with MJPEG;
SD/SDHCI; SCSI (LSI 53C8xx family); ACPI with a real AML namespace
walker (FADT/DSDT/MCFG, S3 suspend/resume in `arch_acpi_s3*.S`);
chipset time sources (HPET/PM timer/TSC/CMOS RTC).

## The oc> recovery environment

`recovery/` is the WinRE-style L0 shell: tokeniser, env vars,
aliases, pipes, redirection, background jobs, wildcards, command
chains, cwd, script language, `help -a/-w` views, line editor with
history, a text `editor` - **99 commands**, all registered via
file-local `OC_SHELL_CMD` declarations. What deliberately lives
here: the kernel self-tests, watchdog, crash policy controls, the
kernel log readers (dmesg/logcat/logctl/crashlog/dumpinfo/
crashtest), chipset diagnostics, recovery file commands, and the
interactive session commands (ssh/sshd).

## TUI and the Chinese console (L0-resident command trees)

`tui/` (the windowing layer over the framebuffer: desktop with
taskbar/launcher, menus, forms, panels, text widgets, a TUI editor,
system monitor apps, plus the `tui/tui_ext.h` extension surface)
and `chinese/` (16x16 bitmap font, GB console rendering, pinyin
IME, CN TUI variants) are still compiled INTO the kernel
(`KERNEL_DIRS` in the Makefile; `CONFIG_TUI`/`CONFIG_CHINESE` can
build without them) - reachable as commands from `oc>` and via the
L1SVC surface from ush. System strings remain ASCII by contract.

## OTA and A/B updates

`services/ota/`: update manifest verification (SHA256 +
signature), download, staged install onto A/B slots
(`ota_ab.{c,h}`), rollback, boot-slot flags consumed by the GRUB
menu (slot ISO / A / B). The `update` command chain runs the whole
flow; `ab_partition_test` exercises slot management on a scratch
disk.

## Crash recovery

`core/core_crash.c` + `recovery/shell_cmds_crash.c`: panic path
with register dump to serial + crashlog on the /etc volume, dump
inspection via `dumpinfo`, an in-guest `crashtest`, and the
WinRE-style recovery posture: if the ring-3 launcher fails, the
system falls back to the `oc>` shell - recovery is always one
`exit` away from the default ring-3 session.
