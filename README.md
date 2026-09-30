<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-09

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

## Stats (WP-09)

- **Source code**: 46014 lines (kernel + boot + userprogs, no docs)
- **Work packages**: 9 (WP-01 ~ WP-09)
- **L1 extension interfaces**: 57 (WP-09 adds transport-level features instead of L1 interfaces)
- **System calls**: 37
- **Audit bugs fixed**: 47 from the original WP-08 audit (P0=2, P1=8, P2=29, P3=8)
  + 4 additional P0 + 8 P1 + 20 P2 from subsequent independent audits and
  the P2-batch fix-ups (P2-BATCH-1 + P2-BATCH-2), bringing the running
  total to 79.
  + 15 GitHub-AI P3 bugs (WP-08-p3: security + memory + syscall + signal + ELF + pipe)
  + 26 P4 bugs (WP-08-p4: doc fixes + Makefile + ld.so output + shell pipe +
  idle alignment + kill/nice overflow + pmm/pftest + execve argv/envp +
  kthread_destroy sync hook + crash log + VFS misc)
  Grand total: 120 bugs fixed (as of WP-08; WP-09 added further SSH/TLS fixes,
  see docs/VERIFICATION_BATCH_B.md).
- **Tests passing**: 18/18 full QEMU regression (WP-09 canonical suite —
  boot banner + uname + 12 user programs + p3_test + heaptest + l1test +
  crashlog) plus dhtest 5/5, HTTPS E2E and SSH both-direction interop with
  paramiko. Full evidence: docs/VERIFICATION_BATCH_B.md + docs/verification/.

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

## WP-09 (done) - Security transport: SSH (client + server), TLS 1.2 / HTTPS, crypto core

- **Crypto core** (`kernel/crypto.{c,h}`): AES-128 (enc/dec), SHA-256, HMAC-SHA256,
  DH modexp (arbitrary length, verified against python3 pow() truth vectors at
  8/16/32/64/128/256 bytes), crypto_random.
- **SSH client** (`kernel/ssh.{c,h}`): KEX group14-sha256 (2048-bit), aes128-cbc,
  hmac-sha2-256, rsa-sha2-256 host key signature, password auth, session channel
  exec. Byte-level K verified against paramiko server-side capture.
- **SSH server** (`kernel/sshd.c`): same algorithm suite, password auth (oc/oc),
  exec requests executed via kernel shell capture API; verified 4/4 against
  paramiko client.
- **TLS 1.2 client** (`kernel/tls.{c,h}`): cipher DHE_RSA_WITH_AES_128_CBC_SHA256
  (0x0067), RFC 3526 1024-bit MODP key exchange, full record layer (encrypt
  outgoing, decrypt + MAC-verify incoming), server Finished accepted-by-design.
- **HTTPS**: `wget https://host:port/path` downloads through TLS into VFS.
- **Network ops commands**: route, arp, firewall, tcpstats, dns.
- **Shell**: 68 commands (boot self-test count).
- **New user test programs**: mprotect_test, p3_test.
- **Verification**: 18/18 QEMU regression + dhtest 5/5 + HTTPS E2E + SSH
  both-direction interop (external evidence: paramiko 5.0). See
  docs/VERIFICATION_BATCH_B.md, docs/EXTENSIONS_WP09.md, docs/INTERFACES.md.

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
|   +-- crypto.{c,h}, dh_scale_vectors.h           # WP-09: AES/SHA/HMAC/DH
|   +-- ssh.{c,h}, sshd.c, sshd_rsa_key.h          # WP-09: SSH client + server
|   +-- tls.{c,h}                                   # WP-09: TLS 1.2 client
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
+-- tools/                      # Build + test scripts
|   +-- build_iso.sh, gen_font.py, embed_userprog.py
|   +-- qemu_shot.py, qemu_shot_vnc.py, qemu_runner.py
|   +-- github_release_wp08.sh  # GitHub Release helper
|   +-- sshd_test.py, paramiko_sshd.py, https_test_server.py  # WP-09 E2E
+-- archive/                    # Old archived source (3 files, .gitignored subdirs)
+-- .gitignore                  # Excludes build/, *.o, *.elf, *.iso, *.zip, releases/, etc.
+-- LICENSE                     # Apache 2.0 full text (201 lines)
+-- NOTICE                      # Copyright + third-party components
+-- Makefile                    # Top-level build (kernel + iso)
+-- linker.ld                   # Kernel link script
+-- grub.cfg                    # GRUB boot config
+-- MANIFEST.md                 # Project manifest
+-- VERIFICATION_REPORT.md       # WP-01..WP-09 verification report
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

The canonical WP-09 regression is the **18/18 full QEMU suite** (boot banner,
uname, 12 user programs, p3_test, heaptest, l1test, crashlog), executed in one
QEMU session via `tools/qemu_runner.py`. In addition:

- `dhtest` — 5/5 DH modexp correctness (Oakley Group 1 + group14 truth vectors
  + scale sweep 8..256 bytes + determinism).
- HTTPS E2E — kernel TLS 1.2 client against `tools/https_test_server.py`
  (TLS1.2-only, DHE-RSA-AES128-SHA256): handshake + encrypted GET + decrypted
  response + MAC verification, both sides logged.
- SSH interop both directions with paramiko 5.0 (`tools/sshd_test.py` and
  `tools/paramiko_sshd.py`): 4/4 checks + byte-level K agreement.

Full evidence with real outputs: docs/VERIFICATION_BATCH_B.md and
raw logs in docs/verification/.

## Download

- **Latest (WP-09)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases) — ISO + SRC zip
- **Archived (WP-08 series)**: [GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)
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
