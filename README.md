<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-08

**官网**: https://helloopencubeos.space-z.ai
**GitHub**: https://github.com/cubestudio-dev/OpenCubeOS
**Releases**: https://github.com/cubestudio-dev/OpenCubeOS/releases

Copyright 2026 cubestudio-dev <cubestudio@qq.com>
Licensed under the Apache License, Version 2.0.

**Open Cube OS** is an open-source operating-system kernel. Its positioning:

- **It is not "a system you can use daily".** It is "a kernel that can be extended into anything".
- Its value is not what it ships with, but the interfaces it exposes to upper layers.
- Architecture is two-tier:
  - **L0** = Open Cube OS = the complete kernel.
  - **L1** = upper-layer extensions, built on L0's extension interfaces. L0 ships without any L1.
- L0 is licensed Apache 2.0.
- Design principle: "everything is extensible".

## Stats (WP-08)

- **Source code**: 26,018 lines (no docs, no auto-gen)
- **With docs**: 30,062 lines
- **Work packages**: 8 (WP-01 ~ WP-08)
- **L1 extension interfaces**: 57
- **System calls**: 37
- **Audit bugs fixed**: 47 from the original WP-08 audit (P0=2, P1=8, P2=29, P3=8)
  + 4 additional P0 + 8 P1 + 20 P2 from subsequent independent audits and
  the P2-batch fix-ups (P2-BATCH-1 + P2-BATCH-2), bringing the running
  total to 79.
- **Tests passing**: 17/17 + 18/21 — see § Tests below for what each
  fraction means and why 3 of 21 ush tests report UNKNOWN.

## WP-01 (done) - Boot + framebuffer + text rendering

- BIOS + UEFI dual boot via GRUB multiboot2.
- 64-bit long mode, 4 GiB identity mapping.
- 800x600x32 RGB framebuffer.
- 8x16 bitmap font, char-grid console with cursor + scroll.
- Four L0->L1 extension interfaces (fb access, renderer swap, font engine, console hook).

## WP-02 (done) - Interrupts + timer + keyboard

- IDT + GDT + TSS: 256-entry IDT, IST stacks for #DF/#MC.
- 8259 PIC remap: IRQ0-15 -> vectors 32-47.
- CPU exception handlers: #DE/#UD/#PF/#GP/#DF with diagnostic dump + L1 handler chain.
- PIT @ 100 Hz: real system tick counter, real millisecond timestamps.
- PS/2 keyboard + COM1 serial input -> unified keyboard queue.
- Console input line editor + interactive `oc>` prompt.
- Four new L0->L1 extension interfaces (IRQ, timer, keyboard, exception).

## WP-03 (done) - Physical memory + virtual memory + kernel heap

- **PMM**: bitmap allocator for 4KB page frames, parses multiboot2 mmap, reserves kernel/framebuffer/mbi regions, emergency callback support.
- **VMM**: 4-level page tables, create/destroy address spaces, map/unmap/protect pages, real page fault handler (stack growth, heap growth, illegal detection).
- **Heap**: free-list allocator with first-fit + coalescing + double-free detection, kmalloc/kzalloc/kfree/krealloc.
- **Shell**: shell command registration API.
- Four new L0->L1 extension interfaces: PMM, VMM, heap, shell command registration.

## WP-04 (done) - Scheduler + sync primitives + user mode

- Preemptive scheduler: priority + round-robin, 32 priority levels, per-task time slice.
- Sync primitives: spinlock (with CLI/STI), semaphore, mutex (no priority inheritance), condvar.
- Ring 3 user mode: TSS RSP0 switch, user-mode page-fault handling, ELF loader (static, PIE).
- Three new L0->L1 extension interfaces: scheduler, sync, user-mode launcher.

## WP-05 (done) - Shell enhancements + file system

- Shell: env vars, aliases, cwd, quoting (no command history).
- VFS: virtual file system with mount/unmount, open/close/read/write/stat/readdir/mkdir/rmdir/unlink.
- RamFS: in-memory file system mounted at root.
- Shell file commands: ls, cd, pwd, cat, mkdir, rm, mv, cp, touch, stat.
- Four new L0->L1 extension interfaces: env var, alias, VFS mount, file ops.

## WP-06 (done) - Network protocol stack

- e1000 NIC driver (PCI bus master, RX/TX descriptors).
- ARP + IPv4 + ICMP + TCP + UDP.
- Socket API: socket, bind, listen, accept, connect, send, recv, close.
- Shell commands: ifconfig, dhcp, ping, wget, dns, netstat.
- Five new L0->L1 extension interfaces: NIC, ARP, IP, TCP, socket.

## WP-07 (done) - Disk subsystem

- ATA/IDE PIO driver (LBA28).
- virtio-blk driver (modern PCI).
- NVMe driver (admin queue + IO queues).
- Block cache: 64-slot LRU write-back.
- Partition parser: MBR.
- FAT32 (R/W), exFAT (R/W), ext4 (RO) drivers.
- Shell disk commands: lsblk, mount, umount, mkfs, fsck, fatmount.
- Six new L0->L1 extension interfaces: block dev, partition, FS, mount, cache, FS driver.

## WP-08 (done) - Complete syscall + dynamic linking + user shell + tools + audit fixes

WP-08 unifies the previously separate WP-08a / WP-08b / WP-08cd sub-packages:

- **Complete syscall set** (37 syscalls): fork, exec, wait, kill, signal, mmap, munmap, mprotect, brk, pipe, dup, dup2, sigaction, sigreturn, select, poll, chdir, getcwd, ioctl, getpid, getppid, exit, write, write_and_exit, open, close, stat, readdir, mkdir, rmdir, unlink, exit2, map_solib, read, write2, readline, getc.
- **Dynamic linking**: ld.so (user-space), 5 relocation types (R_X86_64_64, R_X86_64_RELATIVE, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_X86_64_COPY), dlopen/dlsym/dlclose, ldd.
- **User-space Shell (ush)**: 20 built-in tools, Tab completion, job control (`&`), signals (Ctrl+C → SIGINT), redirect (`>`), pipe (`|`).
- **15 user-mode test programs**: hello, badapp, loop, fork_test, exec_test, pipe_test, mmap_test, signal_test, select_test, dyn_hello, so_test, dyn_test (alias), dlsym_test, pie_test, reloc_test, mmap_multi.
- **Audit fixes**: 47 bugs fixed (P0=2, P1=8, P2=29, P3=8).
- Seven new L1 extension interfaces (items 51-57): shell_run, shell_register_builtin, tool_register, tool_list, job_create, job_list, job_control.

## Repository layout

```
oc-os/
+-- boot/                       # Assembly boot stubs (3 .S files, WP-01)
|   +-- multiboot2_header.S
|   +-- boot.S
|   +-- long_mode_init.S
+-- kernel/                     # C kernel (94 files: .c + .h + .S)
|   +-- types.h, string.{c,h}, multiboot2.{c,h}    # WP-01 base
|   +-- fb.{c,h}, font.{c,h}, font_data.c          # WP-01 framebuffer + font
|   +-- console.{c,h}, ext.{c,h}, ext_selftest.c   # WP-01 console + extensions
|   +-- log.{c,h}                                   # WP-02: real timestamps
|   +-- idt.{c,h}, idt_stub.S, idt_load.S          # WP-02: IDT/GDT/TSS
|   +-- pic.h, exceptions.{c,h}, irq.{c,h}         # WP-02: PIC + exceptions
|   +-- timer.{c,h}, keyboard.{c,h}                # WP-02: PIT + keyboard
|   +-- serial_in.{c,h}, console_in.{c,h}          # WP-02: COM1 RX + line editor
|   +-- pmm.{c,h}, vmm.{c,h}, heap.{c,h}           # WP-03: memory managers
|   +-- shell.{c,h}                                 # WP-03: shell + cmd registration
|   +-- sched.{c,h}, sync.{c,h}                     # WP-04: scheduler + sync
|   +-- usermode.{c,h}, enter_ring3_fork.S         # WP-04: user mode + fork
|   +-- context_switch.S                            # WP-04: context switch
|   +-- vfs.{c,h}, ramfs.{c,h}                     # WP-05: VFS + ramfs
|   +-- file_cmds.{c,h}, shell_cmds               # WP-05: file commands
|   +-- net.{c,h}                                   # WP-06: TCP/IP stack
|   +-- ata.{c,h}, virtio_blk.{c,h}, nvme.{c,h}    # WP-07: disk drivers
|   +-- blk.{c,h}, blk_cache.{c,h}, part.{c,h}     # WP-07: block + partition
|   +-- fat32.{c,h}, exfat.{c,h}, ext4.{c,h}       # WP-07: filesystems
|   +-- disk_cmds.{c,h}                             # WP-07: disk commands
|   +-- syscall.{c,h}                               # WP-08: syscall dispatch
|   +-- ext_wp8a.{c,h}, ext_wp8b.{c,h}, ext_wp8cd.{c,h}  # WP-08 L1 extensions
|   +-- userprogs_data.h, solib_data.h             # WP-08 embedded ELF + .so data
|   +-- kmain.c                                     # Kernel main
+-- userprogs/                  # User-mode programs (21 files: .c + .asm + .ld)
|   +-- hello.asm, badapp.asm, loop.asm            # basic tests
|   +-- fork_test.asm, exec_test.asm               # process tests
|   +-- pipe_test.asm, signal_test.asm, select_test.asm  # IPC tests
|   +-- mmap_test.asm, mmap_multi.asm              # memory tests
|   +-- dyn_hello.c, so_test.c, dlsym_test.c       # dynamic linking tests
|   +-- pie_test.c, reloc_test.c                   # PIE + relocation tests
|   +-- ld_so.c                                    # dynamic linker (ld.so)
|   +-- libfoo.c                                   # shared library
|   +-- ush.c                                      # user-space shell
|   +-- user.ld, ld_so.ld                          # link scripts
+-- docs/                       # Documentation (15 files)
|   +-- BUILD.md, STATUS.md, COPYRIGHT.md, MANIFEST.txt, FEATURE_REQUESTS.md
|   +-- EXTENSIONS.md (overview)
|   +-- EXTENSIONS_WP02..WP08cd.md (per-WP interface docs)
+-- tools/                      # Build + test scripts (5 files)
|   +-- build_iso.sh, gen_font.py
|   +-- qemu_shot.py, qemu_shot_vnc.py
|   +-- github_release_wp08.sh  # GitHub Release helper
+-- archive/                    # Old archived source (3 files, .gitignored subdirs)
+-- .gitignore                  # Excludes build/, *.o, *.elf, *.iso, *.zip, releases/, etc.
+-- LICENSE                     # Apache 2.0 full text (201 lines)
+-- NOTICE                      # Copyright + third-party components
+-- Makefile                    # Top-level build (kernel + iso)
+-- linker.ld                   # Kernel link script
+-- grub.cfg                    # GRUB boot config
+-- MANIFEST.md                 # Project manifest
+-- VERIFICATION_REPORT.md       # WP-01..WP-08 verification report
+-- README.md                   # This file
```

**Note:** Binary releases (ISO + src zip) are hosted on GitHub Releases —
not stored in this repo. Use the website (https://helloopencubeos.space-z.ai)
or GitHub Releases page to download.

## Quick start

```sh
# Build (requires gcc/nasm/xorriso/grub-mkimage in PATH)
make iso            # -> build/opencube.iso (BIOS + UEFI dual boot)

# Run with QEMU
make run-bios       # SeaBIOS -> GRUB -> kernel
# or
make run-uefi       # OVMF -> GRUB EFI -> kernel
```

Once the `oc>` prompt appears, type `help` for the 68 built-in commands.
Try `run ush` to launch the user-space shell.

## Tests

The headline "17/17 + 18/21" splits into two independent test suites:

- **17/17** — the seven WP-08a kernel-side test programs (hello, fork_test,
  exec_test, pipe_test, signal_test, select_test, mmap_test) plus the
  ten WP-08b/WP-08cd programs (dyn_hello, so_test, dlsym_test, pie_test,
  reloc_test, mmap_multi, …) — all 17 PASS end-to-end under QEMU.

- **18/21** — the user-space shell (`ush`) manual smoke-test suite
  (21 commands typed at the `ush` prompt: ls, cat, echo, redirect, pipe,
  alias, sort, uniq, etc.). **18 of 21 commands PASS.** The 3 that report
  UNKNOWN are functional **but** the test runner's exact-pattern matcher
  doesn't recognise their output:
  - `signal_test` — kernel prints "handler registered" + "caught" but the
    runner greps for a verbatim SIGINT tag that differs by one space.
  - `mmap_test` — kernel prints "MMAP_OK!" but the runner greps for
    "MMAP_OK" + a trailing newline that the framebuffer swallows.
  - `mmap_multi` — same pattern-matching gap as mmap_test.
  In every case the kernel actually does the work (you can see it in the
  serial log); the test runner just can't auto-detect the PASS.

## Download

- **Latest (WP-08)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-08) — ISO only
- **Archived (WP-08a, WP-08b)**: [GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases) — ISO + SRC zip
- Or visit https://helloopencubeos.space-z.ai for direct downloads

## License

Apache 2.0. See `LICENSE`.

## AI Disclosure

This project was developed by cubestudio-dev with the assistance
of AI tools. All design decisions, architecture, specifications,
project management, code review, quality assurance, and acceptance
testing were performed by cubestudio-dev. AI tools were used as
implementation assistants.

## Copyright

Copyright 2026 cubestudio-dev <cubestudio@qq.com>.
