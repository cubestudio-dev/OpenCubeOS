<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS Architecture

This document describes L0 as it exists in the tree - every component
named below is a real file you can open. Line counts and per-directory
detail live in `docs/data/stats.json` (regenerate: `make api-docs`).

## The big picture

```
                       GRUB 2.12 (hybrid BIOS + UEFI ISO)
                                     |  multiboot2 + kernel-as-module
        +----------------------------+----------------------------+
        |                 boot/boot.S                            |
        |   32-bit entry -> GDT -> 2 MiB identity map of the      |
        |   first 4 GiB -> PAE+LME+PGE -> long mode -> kmain      |
        +----------------------------+----------------------------+
                                     |
   kernel/main.c  (kmain: bring-up order = the boot log you see)
        |
   +----+------------+------------+-------------+------------+
   |                 |            |             |            |
 kernel/arch/x86_64 kernel/core  kernel/mem   kernel/lib   kernel/crypto
   IDT/GDT/TSS      scheduler    PMM/VMM/heap string/tar  RSA/EC/AEAD/
   exceptions       syscalls 53   4 KiB pages  gzip/config  X.509/SHA
   IRQ 8259 remap   ring3+ELF    free-list     klog
   LAPIC/timers     signals      kmalloc
   context switch   soft timers
        |
   +----------------+----------------+---------------+
   |                |                |               |
 drivers/*        fs/              net/            shell/
 16 families      VFS + ramfs      e1000-family    parser/executor
 block NVME AHCI  FAT32 exFAT      ARP IPv4 ICMP   233 commands
 ATA DMA virtio   ext4             UDP DNS TCP     pipes/redirection
 USB xHCI/EHCI/   node cache       sockets TLS     env/aliases
 OHCI UHCI MSC    mount crossing   SSH client      line editor
 HID audio serial congestion       sshd server    script lang
 PCI PCIe ACPI    /etc volume      firewall        editor
 I2C SMBus HPET                    tcp cc          TUI bridge
 RTC TSC PM timer
 wireless bt SD   tui/            chinese/        l1/
 camera UVC IR    desktop,        CN console      162-numbered
 gameport parallel menus, forms,  font 16x16      extension surface
 SCSI LSI 53C8xx  editor, apps    pinyin IME      + solib loader
```

## Boot flow (what the serial log shows, in order)

1. GRUB loads `boot/opencube.elf` (the kernel is also attached as a
   multiboot2 module tagged `self` - the installer pattern, so
   `install`/`abdisk` can copy the *exact* running kernel onto a disk).
2. `boot/boot.S`: 32-bit entry point, own GDT, identity-map the first
   4 GiB with 2 MiB pages, PAE + LME + PGE, switch to 64-bit long
   mode, call `kmain`.
3. `kernel/arch/x86_64/arch_multiboot2.c` parses the multiboot2 info
   (framebuffer, memory map, command line, ACPI RSDP tags) - with
   full malformed-input hardening: per-tag minimums, NUL-termination
   checks, truncated-tag rejection.
4. `kmain` brings up, in boot-log order: framebuffer console and font,
   IDT/GDT/TSS (256 gates, IST stacks for #DF/#MC and debug),
   8259 PIC remap, PIT @ 100 Hz, PS/2 keyboard, COM1 serial RX,
   line editor, exceptions (with a live #DE/#UD/#PF self-test),
   PMM, VMM, heap, then the driver domains, VFS, network, shell.
5. Shell registers its 233 commands (`shell command system (233
   commands)` line), crash recovery arms, and the `oc>` prompt appears.

## Memory management

- **PMM** (`kernel/mem/mem_pmm.{c,h}`): bitmap allocator over 4 KiB
  page frames from the multiboot2 memory map; reserved regions (kernel
  code, framebuffer, mbi) marked used at init; O(1) alloc/free.
- **VMM** (`kernel/mem/mem_vmm.{c,h}`): 4-level page tables; the
  kernel keeps the boot-time identity map of the first 4 GiB; APIs
  create and manipulate additional address spaces (user processes).
  Page protection flags map to rw/nx/user bits.
- **Heap** (`kernel/mem/mem_heap.{c,h}`): kmalloc/kfree/krealloc/
  kzalloc, free-list with first-fit and coalescing, 16-byte aligned
  blocks (a historical 40-vs-32-byte header bug taught us the hard
  way), 16 MiB fail-fast ceiling.
- **#PF growth**: stack-growth and heap-growth page faults are real
  fault handling in `kmain`'s exception path - illegal accesses
  escalate to the crash path.

## Scheduling and time

- **Scheduler** (`kernel/core/core_sched.{c,h}`): preemptive,
  priority + time-slice round-robin over task control blocks; context
  switch in `arch/x86_64/arch_context_switch.S`-family assembly with
  ABI-probe-verified alignment.
- **Timer** (`kernel/core/core_timer.{c,h}`): PIT at 100 Hz drives
  ticks, `g_boot_time_ms` and the soft-timer wheel. HPET, the ACPI
  PM timer and the TSC are supported as *additional* time sources
  (`drivers/time/`).
- **Watchdog** (`kernel/core/core_watchdog.{c,h}`): monitored task
  slots with timeout policy (the `watchdog` command), plus true
  hardware watchdog bring-up where firmware offers it - Intel TCO
  (verified live on QEMU q35: PMBASE discovery, armed status,
  real hard reset + second boot) and ACPI WDAT tables.

## Interrupts and exceptions

`kernel/arch/x86_64/arch_idt.{c,h}` + `arch_irq.c`: 256-entry IDT,
TSS with IST1 (panic stack for #DF/#MC) and IST2 (dedicated
exception-exit stack - never a context switch on IST2), 8259 remap to
vectors 32-47, LAPIC handling, and a full exception frame path that
feeds crash dump generation. A live 3/3 (#DE/#UD/#PF) self-test runs
on every boot.

## Ring 3, syscalls, processes

- **Syscall ABI** (`kernel/core/core_syscall.h`): `rax`=number,
  `rdi/rsi/rdx/r10`=args, `rax`=result. 53 syscall numbers
  (verify: `grep -c '#define SYS_' kernel/core/core_syscall.h`).
- **User mode** (`kernel/core/core_usermode.{c,h}`): ELF loader,
  process table, signals, ring-3 entry/fork in assembly.
- **User programs** (`userprogs/`): hello, fork/exec/pipe/signal/
  select/mmap tests, dynamic linking with a real in-tree loader
  (`libs/ld_so.c`) - shared objects, `dlsym`, PIE and relocation
  tests all run as shell commands.

## Filesystems

- **VFS** (`fs/fs_vfs.{c,h}`): uniform open/read/write/seek/close/
  stat/mkdir/rmdir/readdir/lookup over registered fs types; the node
  tree crosses mount points transparently; per-fs case sensitivity;
  permission bits enforced; i64 seek.
- **Filesystems**: FAT32 (LFN with alias + collision scan + checksum
  validation, dual-FAT mirror, FSINFO), exFAT (active FAT), ext4
  (read/write with extent support), ramfs (sparse holes zeroed).
- **Block layer** (`drivers/block/driver_block_blk.{c,h}`): register/
  read/write/flush with per-instance names, partition scanning,
  AHCI, ATA/ATA-DMA (with PRDT alignment guaranteed by the 32-byte
  heap header), NVMe, virtio-blk.
- **/etc volume**: a 32 MiB FAT32 image mounted at /etc holding
  `opencube.conf`, SSH host keys, kernel logs - so configuration
  survives reboots (`etc.img` target).

## Network stack

`net/net_core.{c,h}` and friends - a self-contained stack: PCI
discovery, e1000-family NIC drivers (e1000/e1000e/igb/ixgbe; rtl8139/
rtl8169-family; BCM57xx), Ethernet + ARP + IPv4 + ICMP + UDP + DNS +
TCP with congestion control (`net_tcp_cc.c`), a socket API for user
programs, stateful firewall, `wget`/`dns`/`ping`/`route` commands.
On top of it: a TLS 1.2/1.3 client with RSA/ECDHE, X.509 chain
verification (RFC 6125 hostname policy) and a real SSH client +
SSH server (`net_sshd.c`, key exchange via curve25519/ed-style
primitives, host keys on /etc). The DRBG seeding is RDRAND +
re-mixed RDTSC (NIST SP 800-90A HMAC_DRBG).

## Drivers

16 families under `drivers/` - each with its own bring-up manual page
under `docs/drivers/`. Highlights: UHCI/OHCI/EHCI/xHCI USB stacks
with MSC (mass storage), HID, serial, audio classes; HDA/AC97/SB16/
ES1370/virtio sound; i2c + SMBus; wireless (ath/brcm/iwlwifi/rtlwifi
families) and bluetooth (HCI/L2CAP/RFCOMM/SDP/SPP + HID) layers;
UVC cameras with MJPEG; SD/SDHCI; SCSI (LSI 53C8xx family); ACPI
with a real AML namespace walker (FADT/DSDT/MCFG, S3 suspend/resume
in `arch_acpi_s3*.S`); chipset time sources (HPET/PM timer/TSC/CMOS
RTC).

## Shell and TUI

- **Shell** (`shell/`): tokeniser, env vars, aliases, pipes,
  redirection, background jobs, wildcards, command chains
  (`;` `&&` `||`), cwd, script language, `help -a/-w` two views,
  233 commands, line editor with history, a text `editor`.
- **TUI** (`tui/`): the windowing layer on top of the framebuffer -
  desktop with taskbar/launcher, menus, forms, panels, text widgets,
  a TUI editor, system monitor apps, and the extension surface
  `tui/tui_ext.h` (37 callable declarations) that L1 modules use.

## Chinese console layer

`chinese/` adds a 16x16 bitmap font, GB-encoded console rendering,
pinyin IME, and CN variants of the TUI hardware/net/sysinfo apps -
selectable at build time (`CONFIG_CHINESE`), ASCII-equivalent fallbacks
when disabled. System strings remain ASCII by contract.

## L1 extension surface

The numbered 162-interface surface (see `INTERFACES.md`) plus the
driver/OTA/watchdog/health/klog headers; a shared-object loader
(`libs/ld_so.c`) lets L1 modules load at runtime. The TUI command
bridge demonstrates the pattern end-to-end.

## OTA and A/B updates

`kernel/ota/`: update manifest verification (SHA256 + signature),
download, staged install onto A/B slots (`ota_ab.{c,h}`), rollback,
boot-slot flags consumed by the GRUB menu (slot ISO / A / B).
The `update` command chain runs the whole flow; `ab_partition_test`
exercises slot management on a scratch disk.

## Crash recovery

`kernel/core/core_crash.c` + `shell/shell_cmds_crash.c`: panic path
with register dump to serial + crashlog on the /etc volume, dump
inspection via `dumpinfo`, an in-guest `crashtest`, and the WinRE-style
recovery posture: the system boots into the `oc>` shell by default,
so "recovery" is the primary interface, not a special mode.
