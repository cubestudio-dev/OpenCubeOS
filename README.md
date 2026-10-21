<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS

**Version string**: generated at build time from the checked-out tree by
`git describe --tags` (Makefile `OC_RELEASE_VERSION`); the boot banner,
`uname -a` and `update --status` all report it verbatim. There is no
hardcoded release name in this file on purpose. Current source batch:
WP-10-chinese_support-fix2 (fix batch on top of WP-10-chinese_support:
full-glyph TUI rendering, UTF-8-aware vi/nano, Chinese TUI parity with
the English flows, a real-mode S3 resume trampoline - the firmware
delivers the waking vector in 16-bit big real mode - fsck through the
block-device registry, honest ping/dhcp exit codes and a $?
contract in the shell; latest tagged release:
WP-10-chinese_support-fix2).

**官网**: https://cubestudio-dev.github.io/OpenCubeOS
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

## Stats (WP-11-crash_recovery)

- **Source code**: 137,495 lines (kernel + boot + userprogs + fs + net + shell + l1 + drivers + libs + tui + chinese, incl. headers, no docs/tools/tests, no generated files;
  verify: `git ls-files | grep -E '\.(c|h|S)$' | grep -E '^(kernel|boot|userprogs|fs|net|shell|l1|drivers|libs|tui|chinese)/' | grep -vE 'userprobs_data|chinese_font_data|pinyin_data|grub_boot_data' | xargs wc -l`)
- **Work packages**: 23 (WP-01 ~ WP-09, WP-10a, WP-10b, WP-10u, WP-10c,
  WP-10d, WP-10-wp08fix1, WP-10-input_chipset, WP-10-timer_rtc,
  WP-10-wireless_bluetooth, WP-10-camera_misc, WP-10-tui,
  WP-10-chinese_support, WP-10-chinese_support-fix2) — one `## WP-*` section
  each below (WP-10-input_chipset, WP-10-timer_rtc,
  WP-10-wireless_bluetooth, WP-10-chinese_support and
  WP-10-chinese_support-fix2 themselves are
  summarized in the Download section above; they have no separate `## WP-*` sections)
  + auxiliary batches (not counted as WPs): project restructure,
  WP-10d-fix2, WP-09-fix5, and the audit batches WP-AUDIT-01
  (+p0fix/p1fix) and WP-10-AUDIT_P2-fix1 / fix2 / fix2b / fix3 / fix4
  / fix5 (fix5 untagged).  WP-10-input_chipset is the 16th,
  WP-10-timer_rtc the 17th, WP-10-wireless_bluetooth the 18th,
  WP-10-camera_misc the 19th, WP-10-tui the 20th,
  WP-10-chinese_support the 21st,
  WP-10-chinese_support-fix2 the 22nd and WP-11-crash_recovery the
  23rd work package
  (counted above), not
  auxiliary batches.
  + the rule-9 self-hosting batch (WP-10c-selfhost): in-system `abdisk`,
  `install` and `grub-install` - create A/B update disks, install the OS
  to a disk and write the GRUB BIOS boot loader entirely from the oc>
  shell (no host tools; the kernel payload travels with the boot media as
  a multiboot2 module).
- **L1 extension interfaces**: 146 (57 through WP-09 + 8 WP-10a items:
  driver_block_register / driver_block_read / driver_block_write / driver_block_flush (+ driver_block_set_ops),
  driver_block_ahci_init(driver_pci_dev), driver_block_nvme_init(driver_pci_dev), driver_block_ata_dma_init(driver_pci_dev),
  + 13 WP-10b items:
  driver_nic_register / driver_nic_send / driver_nic_recv / driver_nic_link_status / driver_nic_get_mac,
  driver_nic_e1000e_init / driver_nic_igb_init / driver_nic_ixgbe_init / driver_nic_rtl8139_init / rtl8168_init /
  rtl8125_init / rtl810x_init / bcm57xx_init(driver_pci_dev),
  driver_pci_find_class_exact/mask, driver_block_ata_identify_capacity,
  + 7 WP-10u items: ota_ab_update.h (A/B slots, flags, verify, install),
  + 26 WP-10c items: driver_snd_register / driver_snd_play / driver_snd_stop / driver_snd_set_rate /
  driver_snd_set_volume / driver_snd_get_caps (+ driver_snd_probe_all, driver_snd_make_tone and the
  snd.h lookup/ listing helpers),
  driver_snd_hda_init / driver_snd_ac97_init / driver_snd_sb16_init / driver_snd_es1370_init / driver_snd_virtio_init /
  driver_usb_audio_init(driver_pci_dev / isa_dev / driver_usb_dev),
  driver_usb_init / driver_usb_enumerate / driver_usb_control / driver_usb_set_interface /
  driver_usb_iso_out_submit (drivers/usb/driver_usb.h),
  + 11 WP-10d items: driver_usb_register_host / driver_usb_enumerate_host /
  driver_usb_control_transfer / driver_usb_bulk_transfer / driver_usb_interrupt_transfer /
  driver_usb_isochronous_transfer / driver_usb_register_driver (class-driver registry:
  HID keyboard + mouse, MSC storage, CDC-ACM/FTDI serial, UAC audio),
  driver_usb_uhci_init / driver_usb_ohci_init / driver_usb_ehci_init / driver_usb_xhci_init(driver_pci_dev) — four host
  controller backends (drivers/usb/driver_usb.h),
  + 7 WP-10d-fix2 items: core_power_shutdown / core_power_suspend /
  core_power_halt / core_power_reboot + shell_register_command_ex /
  shell_list_commands_a_z / shell_list_commands_by_wp,
  + 9 WP-10-wp08fix1 items: shell_lineedit_init / shell_lineedit_history_add /
  shell_lineedit_history_get / shell_lineedit_cursor_move /
  shell_lineedit_tab_complete / shell_lineedit_ctrlc (shell/shell_lineedit.h)
  + editor_open / editor_save / editor_close (shell/editor.h)
  + 8 WP-11-crash_recovery items: kernel_panic_register_handler /
  kernel_panic / core_dump_save / core_dump_read /
  crashlog_record / crashlog_flush / crash_recovery_set_policy /
  crash_recovery_get_policy (kernel/core/core_crash.h)
  (57 + 8 + 13 + 7 + 26 + 11 + 7 + 9 + 8 = 146; plus 5 unnumbered WP-09-fix5
  config/check-update interfaces and 5 rule-9 self-hosting interfaces
  outside the 1-146 numbering)
- **System calls**: 53 (verify: `grep -c '#define SYS_' kernel/core/core_syscall.h`)
- **Audit bugs fixed**: 47 from the original WP-08 audit (P0=2, P1=8, P2=29, P3=8)
  + 4 additional P0 + 8 P1 + 20 P2 from subsequent independent audits and
  the P2-batch fix-ups (P2-BATCH-1 + P2-BATCH-2), bringing the running
  total to 79.
  + 15 GitHub-AI P3 bugs (WP-08-p3: security + memory + syscall + signal + ELF + pipe)
  + 26 P4 bugs (WP-08-p4: doc fixes + Makefile + ld.so output + shell pipe +
  idle alignment + kill/nice overflow + pmm/pftest + execve argv/envp +
  core_kthread_destroy sync hook + crash log + VFS misc)
  Grand total: 120 bugs fixed (as of WP-08; WP-09 added further SSH/TLS fixes).
- **Tests passing**: 18/18 full QEMU regression (WP-09 canonical suite —
  boot banner + uname + 12 user programs + p3_test + heaptest + l1test +
  crashlog) plus dhtest 5/5, HTTPS E2E and SSH both-direction interop with
  paramiko.

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

- **Complete syscall set** (53 syscalls; `SYS_` numbering 0-109 — WP-10-tui added the ring3 socket API 103-109 — verify:
  `grep -c '#define SYS_' kernel/core/core_syscall.h`): exit, write,
  write_and_exit, open, close, stat, readdir, mkdir, rmdir, unlink, fork,
  execve, wait4, kill, getpid, getppid, exit2, pipe, dup, dup2, mmap,
  munmap, mprotect, brk, signal, sigaction, sigreturn, chdir, getcwd,
  ioctl, read, write2, readline, getch, uptime, meminfo, select, poll,
  map_solib, symlink, readlink, link, chmod, chown, netcmd, ps
  (symlink..ps are the WP-10-wp08fix1 additions, SYS 96-102).
- **Dynamic linking**: ld.so (user-space), 5 relocation types (R_X86_64_64, R_X86_64_RELATIVE, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_X86_64_COPY), dlopen/dlsym/dlclose, ldd.
- **User-space Shell (ush)**: 20 built-in tools, Tab completion, job control (`&`), signals (Ctrl+C → SIGINT), redirect (`>`), pipe (`|`).
- **15 user-mode test programs**: hello, badapp, loop, fork_test, exec_test, pipe_test, mmap_test, signal_test, select_test, dyn_hello, so_test, dyn_test (alias), dlsym_test, pie_test, reloc_test, mmap_multi.
- **Audit fixes**: 47 bugs fixed (P0=2, P1=8, P2=29, P3=8).
- Seven new L1 extension interfaces (items 51-57): shell_run, shell_register_builtin, tool_register, tool_list, job_create, job_list, job_control.

## WP-09 (done) - Security transport: SSH (client + server), TLS 1.3 / TLS 1.2 / HTTPS, crypto core

- **Crypto core** (`kernel/crypto.{c,h}` + bn/ec/rsa/aead/x509 modules):
  AES-128 (CTR/CBC) + AES-128/256-GCM + ChaCha20-Poly1305, SHA-256, streaming
  SHA-256/HMAC, SHA-512/384, HMAC-SHA-512, HKDF, big-integer modexp
  (Montgomery core, RSA-4096 viable, verified against python3 pow() truth
  vectors at 8/16/32/64/128/256 bytes), NIST P-256/P-384 ECDH+ECDSA, X25519
  (RFC 7748 vectors), RSA PKCS#1 v1.5 + PSS verify, RFC 8439/NIST KAT-validated
  AEAD, crypto_random.
- **SSH client** (`kernel/ssh.{c,h}`): KEX curve25519-sha256 (+@libssh.org)
  and diffie-hellman-group14-sha256, aes128-ctr (aes128-cbc fallback),
  hmac-sha2-256, rsa-sha2-256/512 host key, password **and publickey** auth,
  session channel exec, server host-key signature verified over H with TOFU
  SHA-256 fingerprint display. Byte-level K verified against paramiko
  server-side capture.
- **SSH server** (`net/net_sshd.c`): same mainstream suite (curve25519 KEX,
  aes128-ctr), password **and publickey** auth (kernel identity key doubles
  as host key), exec requests executed via kernel shell capture API; verified
  against the paramiko client.
- **TLS 1.3 client** (RFC 8446, preferred): X25519, AES-128/256-GCM and
  ChaCha20-Poly1305, full HKDF key schedule (RFC 8448 vectors), encrypted
  handshake, server CertificateVerify checked.
- **TLS 1.2 client** (RFC 5246, fallback): ECDHE_RSA with AES-GCM and
  ChaCha20-Poly1305; legacy DHE-CBC (0x0067) retained for old servers.
- **Certificate verification** (`kernel/x509.{c,h}`): X.509 chain verified
  against embedded public CA roots (ISRG, Google GTS, DigiCert, GlobalSign,
  Baltimore, Amazon, Microsoft) + SAN dNSName hostname match, validity via
  RTC. Fails closed on verification errors.
- **HTTPS**: `wget https://host:port/path` downloads through TLS into VFS.
  Real-site verified: cubestudio-dev.github.io (10148-byte update.json),
  google.com, cloudflare.com.
- **Network ops commands**: route, arp, firewall (stateful rules, conntrack,
  three chains with policies + REJECT + per-rule hit counters), tcpstats
  (CUBIC cwnd/RTO/SACK/fast-retransmit visibility), dns (A/AAAA/CNAME/MX/TXT/NS/SRV).
- **Shell**: 116 commands (live boot count; `help` lists 116 unique);
  includes the WP-10a storage surface: `ahci`, `nvme`, `ata` (driver status)
  and the eight-test suite `ahci_test`, `nvme_test`, `ata_dma_test`,
  `virtio_blk_test`, `disk_rw_test`, `partition_test`, `fs_mount_test`,
  `real_hw_test`, plus the WP-10b NIC surface: `e1000e`, `igb`, `ixgbe`,
  `rtl8139`, `rtl8168`, `rtl8125`, `rtl810x`, `bcm57xx` (driver status)
  and `e1000e_test`, `igb_test`, `ixgbe_test`, `rtl8139_test`,
  `rtl8168_test`, `rtl8125_test`, `rtl810x_test`, `bcm57xx_test`,
  `other_nic_test`, `nic_rw_test`.
- **Storage (WP-10a)**: AHCI SATA (DMA, multi-port, FLUSH), NVMe (admin +
  2 I/O queue pairs, Identify, Read/Write/Flush), ATA Bus-Master DMA
  (PRDT + PIO fallback preserved), virtio-blk (capacity fixed);
  MBR + GPT partition parsing on every device; FAT32 mounts on all four
  driver types through the unified blk layer.
- **New user test programs**: mprotect_test, p3_test.
- **Verification**: 18/18 QEMU regression + dhtest 5/5 + cryptotest 3/3 +
  nf_test 8/8 + tcpcc_test + dnstest 6/6 (live) + tcptest + HTTPS E2E
  (real sites above) + SSH both-direction interop (external evidence:
  paramiko 5.0). See docs/EXTENSIONS_WP09.md, docs/INTERFACES.md.
- **WP-09-fix5 — system configuration + update check**:
  `/etc/opencube.conf` (first user-editable config, FAT32 /etc volume,
  ramfs fallback), `checkupdate` over HTTP/HTTPS with JSON manifest,
  non-blocking `auto_check` boot check, `config`/`edit` commands and the
  `l1_ext_config_*` / `l1_ext_check_update*` L1 interfaces. See
  docs/CONFIG.md.

## WP-10a (done) - Storage drivers: AHCI SATA / NVMe / ATA Bus-Master DMA / virtio-blk

- **Drivers** (drivers/block/): AHCI SATA (DMA, multi-port, FLUSH),
  NVMe (admin + 2 I/O queue pairs, Identify, Read/Write/Flush),
  ATA Bus-Master DMA (PRDT + PIO fallback preserved), virtio-blk
  (capacity fixed).
- **L1 interfaces** (numbered items 58-65 of 138; declared in
  drivers/block/driver_block_blk.h, drivers/pci/driver_pci.h,
  driver_block_{ahci,nvme,ata_dma}.h, driver_block_ata.h):
  driver_block_register / driver_block_read / driver_block_write /
  driver_block_flush (+ driver_block_set_ops),
  driver_pci_find_class_exact / driver_pci_find_class_mask,
  driver_block_ahci_init / driver_block_nvme_init /
  driver_block_ata_dma_init(driver_pci_dev),
  driver_block_ata_identify_capacity.
- MBR + GPT partition parsing on every device; FAT32 mounts on all four
  driver types through the unified blk layer. Test suite: `ahci`, `nvme`,
  `ata` status + `ahci_test`, `nvme_test`, `ata_dma_test`, `virtio_blk_test`,
  `disk_rw_test`, `partition_test`, `fs_mount_test`, `real_hw_test`.
  See docs/EXTENSIONS_WP10a.md.

## WP-10b (done) - NIC drivers: e1000e / igb / ixgbe / RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx

- **Driver framework** (drivers/nic/driver_nic.h): L1 registry
  (register / send / recv / link_status / get_mac), driver_nic_probe_all,
  per-model init entry points; `other_nics_init` covers the legacy tail.
- **L1 interfaces** (numbered items 66-78 of 138 = 5 + 8; declared in
  drivers/nic/driver_nic.h): driver_nic_register / driver_nic_send /
  driver_nic_recv / driver_nic_link_status / driver_nic_get_mac +
  driver_nic_e1000e_init / driver_nic_igb_init / driver_nic_ixgbe_init /
  driver_nic_rtl8139_init / rtl8168_init / rtl8125_init / rtl810x_init /
  bcm57xx_init(driver_pci_dev).
- **Test surface**: one status command + one test command per model
  (`e1000e`..`bcm57xx`, `e1000e_test`..`bcm57xx_test`) + `other_nic_test`
  + `nic_rw_test`. See docs/EXTENSIONS_WP10b.md.

## WP-10u (done) - In-system update: A/B partitions + tar.gz packages + rollback + offline update

- **A/B partition layout** (boot/flags + slot A + slot B + data), partitions
  registered as their own block devices (hdapN) and mounted at /ab/boot,
  /ab/a, /ab/b and /data.
- **Kernel-side gzip decoder** (RFC 1952/1951: stored/fixed/dynamic blocks,
  resumable across feed boundaries, CRC32+ISIZE verification) and a
  **streaming ustar parser** (header checksums, GNU long names).
- **Two-phase streaming HTTP/HTTPS downloader** (status code + Content-Length
  enforced) with **two SHA256 layers** (the package digest in update.json
  plus the payload digest inside the package manifest).
- **Boot-flag protocol** (next_B/ok_B/bootfail_B) gives GRUB-side automatic
  rollback; a failed slot-B boot keeps bootfail_B so the next boot goes back
  to slot A.
- **Commands**: update, update --local, update --status, rollback, reboot.
- **User guide**: docs/UPDATE-HOWTO.md - step-by-step A/B disk creation,
  package building, test server, update/rollback with expected outputs.
- **Verification**: update_pkg_test 12/12, ab_partition_test 7/7,
  ota_update_check/download/verify/install/rollback/local/status all PASS,
  real_update_test 7/7 end-to-end (download, verify, install into slot B,
  a real reboot into slot B, confirm ok_B, rollback). See
  docs/EXTENSIONS_WP10u.md.

## WP-10c (done) - Sound card drivers: Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio

- **snd framework** (kernel/snd.{c,h}): 8-slot registry,
  driver_snd_register / driver_snd_play / driver_snd_stop / driver_snd_set_rate / driver_snd_set_volume /
  driver_snd_get_caps; every driver does real DMA and raises real device
  interrupts (per-device IRQ counters visible in `sound` and the tests).
- **Intel HDA** (drivers/snd/driver_snd_hda.c): MMIO BARs, controller reset, CORB/RIRB
  command rings, codec address discovery, widget-tree enumeration
  (audio function group, DAC/ADC, pins), stream format programming and
  BDL DMA with IOC interrupts, LPIB flow control.
- **AC'97 82801AA** (drivers/snd/driver_snd_ac97.c): mixer (master/PCM volume + rate) and
  bus-master BDL DMA with IOC interrupts.
- **Sound Blaster 16** (drivers/snd/driver_snd_sb16.c): ISA DSP 4.05 (io 0x220, IRQ 5),
  8/16-bit single-cycle DMA with auto-init block interrupts.
- **ES1370/1371** (drivers/snd/driver_snd_es1370.c): DAC2 frame DMA + PCLKDIV clocking,
  memory-mapped ring with IRQ on every buffer.
- **virtio-snd** (drivers/snd/driver_snd_virtio.c): modern virtio-pci (1AF4:1059),
  control + TX queues, PCM prepare/start/set_volume requests; probed on
  every boot (QEMU 10 has no device model, so no live card in CI).
- **USB Audio Class 1.0** (drivers/usb/driver_usb_audio.c) over the **new UHCI host
  stack** (kernel/usb.{c,h}): UHCI controller driver (piix3/4), blocking
  control transfers, device enumeration (SET_ADDRESS/CONFIGURATION/
  INTERFACE), isochronous OUT scheduled per 1 ms frame.
- **44.1/48 kHz** sample-rate configuration (caps-aware; sb16 rejects
  48 kHz by design and the test verifies the rejection).
- **Commands**: sound, hda, ac97, sb16, es1370, virtiosnd, usbaudio,
  play [device] [rate], volume [device] [0-100]; lspci shows sound
  controllers (class 0x04). 196 commands registered at boot
  (including the rule-9 self-hosting set: abdisk, install,
  grub-install, abcfg, and the WP-10-wp08fix1 nano/vi editors).
- **Verification**: hda_test / ac97_test / sb16_test / es1370_test /
  usb_audio_test live in QEMU (init + caps + DMA bytes + IRQ counters),
  audio_rw_test plays every registered card, sample_rate_test programs
  44.1/48 kHz on every card that supports them, virtio_snd_test reports
  SKIPPED honestly (no QEMU device model), real_hw_test reports NOT RUN
  in a VM. See docs/EXTENSIONS_WP10c.md.

## WP-10d (done) - USB host stack: UHCI / OHCI / EHCI / XHCI + HID/MSC/serial/audio + hub/hot-plug

- **USB core** (kernel/usb.{c,h}): host registry, device table, endpoint
  management, the four transfer types (control / interrupt / bulk /
  isochronous), a class-driver registry (probe + disconnect) and the
  enumeration walker (root ports + external hub cascade + hot-plug).
- **UHCI** (drivers/usb/driver_usb.c, PIIX3): full chain - kbd/mouse/hub cascade
  enumeration, MSC, FAT32 round-trip, hot-plug.
- **OHCI** (drivers/usb/driver_usb_ohci.c): independent verification session ALL
  PASS - enumeration with product strings, usb_core_test 5/5, MSC
  capacity/MBR/write-read-back, FAT32 mkfs + mount + file round-trip,
  real hot-plug; fixes: TD_R buffer rounding on control DATA TDs, the
  status-TD direction kept in u32 (an u8 truncated the bit-19/20
  direction field to SETUP and STALLed every status stage), TD_R on
  interrupt IN TDs.
- **EHCI** (drivers/usb/driver_usb_ehci.c): async ring + qTD engine with
  spec-correct buffer pointers and the IAAD doorbell; kbd/mouse/storage
  enumeration, MSC read/write, FAT32 mount and file round-trip, STALL
  recovery.
- **XHCI** (drivers/usb/driver_usb_xhci.c): command/event rings, DCBAA + scratchpad,
  two-stage AddressDevice, lazy ConfigureEndpoint, spec-layout endpoint
  contexts; Reset Endpoint / Set TR Dequeue endpoint ID now lives in
  control bits 20:16 (spec Table 6-42/6-44) - the old low-bit layout
  made the controller retire the commands with TRB Error and left every
  STALLed endpoint unrecoverable. Enumeration/HID/core tests PASS;
  the qemu-xhci usb-storage CSW handback gap is documented honestly.
- **Class drivers**: HID keyboard/mouse (report descriptors), MSC
  (BOT + SCSI wired into blk), CDC-ACM/FTDI serial, UAC 1.0/2.0.
- **Commands**: usb, usbdev + usb_core_test / usb_kbd_test /
  usb_mouse_test / usb_storage_test / usb_serial_test / usb_hotplug_test
  / usb_hub_test (196 commands at boot; 138 L1 interfaces;
  WP-10d-fix2 added 7: core_power_shutdown/core_power_suspend/core_power_halt/core_power_reboot
  + shell_register_command_ex/shell_list_commands_a_z/shell_list_commands_by_wp).
- **Verification**: UHCI/OHCI/EHCI/XHCI exercised in QEMU with explicit
  controllers (piix3-usb-uhci, pci-ohci, usb-ehci, qemu-xhci); BIOS +
  UEFI boot matrix; usb_core_test 5/5 on all four; the OHCI session and
  the XHCI EPID fix close out the WP-10d controller work. See
  docs/EXTENSIONS_WP10d.md.

## WP-10-wp08fix1 (done) - Shell line editor + nano/vi editors + VFS link/symlink + 7 new syscalls

- **Kernel line editor** (shell/shell_lineedit.h): history
  (shell_lineedit_init / shell_lineedit_history_add /
  shell_lineedit_history_get), cursor movement
  (shell_lineedit_cursor_move), TAB completion
  (shell_lineedit_tab_complete), ^C handling (shell_lineedit_ctrlc) -
  items 130-135.
- **nano / vi full-screen editors** (shell/editor.h): editor_open /
  editor_save / editor_close - items 136-138; the `nano` and `vi`
  commands registered at boot.
- **VFS additions** (fs/fs_vfs.h): symlink/readlink + hard links,
  chmod/chown with permission enforcement.
- **7 new syscalls (SYS 96-102)**: symlink, readlink, link, chmod, chown,
  netcmd, ps; ush gained the matching user-space commands.
- This batch closes the L1 interface numbering at **138** (items 130-138).
  See docs/EXTENSIONS_WP10-wp08fix1.md.

## WP-10-chinese_support (done) - GB2312 font + encoding + CJK rendering + Chinese TUI + pinyin IME

Summarized in the Download section (WP-10-chinese_support is the 21st
work package). See docs/EXTENSIONS_WP10-chinese_support.md.

## Repository layout

```
oc-os/
+-- boot/                       # Assembly boot stubs (3 .S files, WP-01)
|   +-- multiboot2_header.S
|   +-- boot.S
|   +-- long_mode_init.S
+-- kernel/                     # C kernel (142 files: .c + .h + .S)
|   +-- types.h, string.{c,h}, multiboot2.{c,h}    # WP-01 base
|   +-- fb.{c,h}, font.{c,h}, font_data.c          # WP-01 framebuffer + font
|   +-- console.{c,h}, ext.{c,h}, ext_selftest.c   # WP-01 console + extensions
|   +-- log.{c,h}                                   # WP-02: real timestamps
|   +-- idt.{c,h}, arch_idt_stub.S, arch_idt_load.S          # WP-02: IDT/GDT/TSS
|   +-- pic.h, exceptions.{c,h}, irq.{c,h}         # WP-02: PIC + exceptions
|   +-- timer.{c,h}, keyboard.{c,h}                # WP-02: PIT + keyboard
|   +-- screen_serial_in.{c,h}, console_in.{c,h}          # WP-02: COM1 RX + line editor
|   +-- pmm.{c,h}, vmm.{c,h}, heap.{c,h}           # WP-03: memory managers
|   +-- shell.{c,h}                                 # WP-03: shell + cmd registration
|   +-- sched.{c,h}, sync.{c,h}                     # WP-04: scheduler + sync
|   +-- usermode.{c,h}, enter_ring3_fork.S         # WP-04: user mode + fork
|   +-- context_switch.S                            # WP-04: context switch
|   +-- vfs.{c,h}, ramfs.{c,h}                     # WP-05: VFS + ramfs
|   +-- file_cmds.{c,h}, shell_cmds               # WP-05: file commands
|   +-- net.{c,h}                                   # WP-06: TCP/IP stack
|   +-- ata.{c,h}, virtio_blk.{c,h}, nvme.{c,h}    # WP-07: disk drivers
|   +-- blk.{c,h}, driver_block_cache.{c,h}, part.{c,h}     # WP-07: block + partition
|   +-- fat32.{c,h}, exfat.{c,h}, ext4.{c,h}       # WP-07: filesystems
|   +-- disk_cmds.{c,h}                             # WP-07: disk commands
|   +-- syscall.{c,h}                               # WP-08: syscall dispatch
|   +-- ext_wp8a.{c,h}, ext_wp8b.{c,h}, ext_wp8cd.{c,h}  # WP-08 L1 extensions
|   +-- userprogs_data.h, solib_data.h             # WP-08 embedded ELF + .so data
|   +-- crypto.{c,h}, crypto_dh_scale_vectors.h           # WP-09: AES/SHA/HMAC/DH
|   +-- bn.{c,h}, crypto_ec_nist.{c,h}, curve25519.{c,h}  # WP-09 mainstream: bignum + P-256/384 + X25519
|   +-- rsa.{c,h}, aead.{c,h}, sha512.{c,h}        # WP-09 mainstream: RSA verify + AEAD + SHA-512
|   +-- x509.{c,h}                                 # WP-09 mainstream: X.509 chain + hostname verify
|   +-- net_tcp_cc.{c,h}                               # WP-09 mainstream: CUBIC congestion control
|   +-- ssh.{c,h}, sshd.c, net_sshd_rsa_key.h          # WP-09: SSH client + server
|   +-- tls.{c,h}                                   # WP-09: TLS 1.3/1.2 client
|   +-- config.{c,h}, update.{c,h}, ota_ab_update.{c,h}  # WP-09-fix5/WP-10u: config + checkupdate + A/B update
|   +-- ahci.{c,h}, driver_block_ata_dma.{c,h}                   # WP-10a: AHCI SATA + ATA Bus-Master DMA
|   +-- nic.{c,h}, driver_nic_e1000e.c, driver_nic_igb.c,        # WP-10b: NIC framework + nine
|   |   driver_nic_ixgbe.c, driver_nic_rtl8139.c, driver_nic_rtl8169.c, #   driver families + tests
|   |   driver_nic_bcm57xx.c, driver_nic_other.c, driver_nic_test_cmds.c
|   +-- snd.{c,h}, driver_snd_test_cmds.c                   # WP-10c: sound framework + tests
|   +-- hda.{c,h}, ac97.{c,h}, sb16.{c,h},          # WP-10c: six sound driver
|   |   es1370.{c,h}, virtio_snd.{c,h},             #   families + USB audio
|   |   usb.{c,h}, driver_usb_audio.{c,h}
|   +-- kmain.c                                     # Kernel main
+-- userprogs/                  # User-mode programs (23 files: .c + .asm + .ld)
|   +-- hello.asm, badapp.asm, loop.asm            # basic tests
|   +-- fork_test.asm, exec_test.asm               # process tests
|   +-- pipe_test.asm, signal_test.asm, select_test.asm  # IPC tests
|   +-- mmap_test.asm, mmap_multi.asm, mprotect_test.asm  # memory tests
|   +-- p3_test.asm                                 # WP-09: P3 regression
|   +-- main_dyn.c, dyn_hello.c, so_test.c, dlsym_test.c  # dynamic linking tests
|   +-- pie_test.c, reloc_test.c                   # PIE + relocation tests
|   +-- ld_so.c                                    # dynamic linker (ld.so)
|   +-- libfoo.c                                   # shared library
|   +-- ush.c                                      # user-space shell
|   +-- user.ld, ld_so.ld                          # link scripts
+-- docs/                       # Documentation (18 files)
|   +-- BUILD.md, CONFIG.md, COPYRIGHT.md, INTERFACES.md, MANIFEST.txt
|   +-- EXTENSIONS.md (overview)
|   +-- EXTENSIONS_WP02..WP10c.md (per-WP interface docs)
+-- tools/                      # Build + test scripts
|   +-- build_iso.sh, gen_font.py, embed_userprog.py
|   +-- qemu_shot.py, qemu_shot_vnc.py, qemu_runner.py
|   +-- github_release_wp08.sh  # GitHub Release helper
|   +-- net_sshd_test.py, paramiko_sshd.py, https_test_server.py  # WP-09 E2E
|   +-- make_ab_disk.sh, make_update_pkg.sh, ota_update_server.py # WP-10u OTA
+-- archive/                    # Old archived source (3 files, .gitignored subdirs)
+-- .gitignore                  # Excludes build/, *.o, *.elf, *.iso, *.zip, releases/, etc.
+-- LICENSE                     # Apache 2.0 full text (201 lines)
+-- NOTICE                      # Copyright + third-party components
+-- Makefile                    # Top-level build (kernel + iso)
+-- linker.ld                   # Kernel link script
+-- grub.cfg                    # GRUB boot config
+-- MANIFEST.md                 # Project manifest
+-- README.md                   # This file
```

**Note:** Binary releases (ISO + src zip) are hosted on GitHub Releases —
not stored in this repo. Use the website (https://cubestudio-dev.github.io/OpenCubeOS)
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

Once the `oc>` prompt appears, type `help` for the full command list.
Try `run ush` to launch the user-space shell.

**Trying the hardware features in QEMU** (blank disk to format, NIC for
DHCP/ping, sound cards to play a tone, A/B disk for in-system updates):
step-by-step commands with expected outputs are in
**docs/TRY-IT.md**; the update flow has its own guide in
**docs/UPDATE-HOWTO.md**.

## Tests

The canonical regression is the **28/28 full QEMU suite** (boot banner,
uname, 12+ user programs, p3_test, heaptest, l1test, crashlog, crypto,
FS, G6/G7/G8), executed via `tools/run_fix4_regression.py`. The
**WP-11 acceptance gate** is `tools/wp11_crash_e2e.py` - 14 scenarios,
each panic variant in a fresh QEMU boot: exc self-test, badapp,
sigsegv_test (SIGSEGV delivered + caught), stackovf (kill + parent
wake), crashlog listing + flush + read-back, crashtest dump/dumpinfo,
panic/exc/pf/assert/wd/ovf panic variants, the reboot policy
(panic -> reset -> second boot) and the TUI crash containment E2E
(tools/wp11_tui_isolation_e2e.py). In addition:

- `dhtest` — 5/5 DH modexp correctness (Oakley Group 1 + group14 truth vectors
  + scale sweep 8..256 bytes + determinism).
- HTTPS E2E — real-site TLS through the kernel client
  (`https://cubestudio-dev.github.io/OpenCubeOS/update.json` → 10148 bytes;
  google.com, cloudflare.com), plus the local `tools/https_test_server.py`
  (TLS1.2-only, DHE-RSA-AES128-SHA256) covering the legacy fallback:
  handshake + encrypted GET + decrypted response + MAC verification.
- SSH interop both directions with paramiko 5.0 (`tools/net_sshd_test.py` and
  `tools/paramiko_sshd.py`): password + publickey auth, curve25519/group14
  KEX, aes128-ctr/cbc — 4/4 checks + byte-level K agreement + server
  host-key signature verification (TOFU fingerprint).
- WP-10u update tests — update_pkg_test 12/12, ab_partition_test 7/7,
  ota_update_check/download/verify/install/rollback/local/status all PASS and
  real_update_test 7/7 with a real reboot into slot B (`tools/make_ab_disk.sh`,
  `tools/make_update_pkg.sh`, `tools/ota_update_server.py`).  Step-by-step
  user guide: **docs/UPDATE-HOWTO.md**.
- WP-10c sound tests — hda_test / ac97_test / sb16_test / es1370_test /
  usb_audio_test live in QEMU (real DMA + real IRQ counters), audio_rw_test
  on every card, sample_rate_test for 44.1/48 kHz; virtio_snd_test SKIPPED
  (no QEMU device model), real_hw_test NOT RUN in a VM (honest report).

## Download

- **Latest (WP-11-crash_recovery)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-11-crash_recovery) — ISO + SRC zip
  (ISO sha256 `032dd175cc28b67d82bb5edf8623437d9202012e6191f86f6ce3bf4d77cf5890`,
  SRC sha256 `40a32924e4689c7f5e8694fd6a4cc2e0c283cdc572b674191a77de515411d9a4`)
  Batch summary: the crash-recovery layer - unified panic capture
  (banner + 16-GPR register snapshot + call traceback, serial AND the
  red VGA console), core dumps persisted to /var/crash/core.dump (or
  hex-dumped on serial with no filesystem), a 32-entry crashlog ring
  flushed to /var/log/crash.log, the HALT/REBOOT policy (QEMU-verified
  panic -> 8042 reset -> second boot), a soft watchdog, kernel-stack
  guard pages + scheduler bounds checks + KERNEL STACK OVERFLOW
  attribution, ring-3 SIGSEGV delivery with a ping-pong guard, and TUI
  sub-page crash containment (a real ring-0 fault inside a guarded TUI
  page longjmps back to the surviving desktop). New commands crashtest
  + dumpinfo, crashlog completed (226 total). Acceptance: WP-11 E2E
  14/14 on this exact ISO, regression 28/28, dhtest, cryptotest, SSH
  E2E 5/5, HTTPS E2E 7/7, BIOS + UEFI boots. KNOWN_ISSUES 15.13
  states the one boundary (#DF path records no dump/log entry).
  Batch summary: screenshot-driven fix batch on the chinese_support
  release — every QEMU screen was reviewed as an image (screendump ->
  PNG) and three real defects found that the ANSI/serial E2E assertions
  could not see.  (1) The zh system messages (banner, help headers,
  unknown-command lines) rendered as hollow boxes on the framebuffer:
  shell_lang.c / main.c / shell.c printed through screen_console_puts
  (8x16 ASCII font, no CJK glyphs) although the header comment claimed
  the console_cn path; all shell_msg() output sites now go through
  console_cn_puts/putcw (ASCII routes unchanged, CJK renders 16x16)
  and help -w gained a localized tail (SM_HELP_WP_TAIL; the en column
  stays byte-identical).  (2) The TUI PCI page (both the English
  tui-hw and the Chinese tui-cn twins) printed literal "2x:2x,0
  4x:4x class 2x2x" instead of the ids: the page-local formatters did
  not understand the zero-padded %02x/%04x widths; both now parse
  "%0N" and emit real ids (00:00.0 8086:1237 ... 00:03.0 8086:100e on
  the reference VM).  (3) The cross-reboot persistence claim was
  false: the ATA DMA driver registers drives as hda..hdd while
  lib_config_init mounted /etc from the hard-coded "ata0" alias, the
  mount silently fell back to ramfs and lib_config_write returned
  rc=0 on the in-memory copy; lib_config now tries the alias then
  every registered block device, and `lang zh` reports honestly
  ("persisted" on the FAT32 backend, "in-memory only; ramfs config,
  lost on reboot" on the fallback).  Verified end to end on a real
  disk with the snapshot off: boot1 writes zh (Chinese confirmation
  line), reboot, boot2 shows the Chinese banner ("已就绪。输入 'help'
  查看命令。") and "Shell 输出语言：zh", and host-side mtype reads
  shell.lang=zh back from etc.img.  User decision honored: the
  suspend/suspend_test entry points are hidden (TUI power page 4 -> 3
  actions, shell command count 222 -> 220) and the new `lang` command
  brings the count to 221; shell.lang=en|zh is a known config key
  (canonical lowercase, en/zh validated, config set syncs the live
  state) and `tui` opens the Chinese launcher directly when
  shell.lang=zh.  Gates: 0 errors 0 warnings; chinese E2E 21/21 BIOS +
  21/21 UEFI (with the config disk attached); TUI E2E 37/37 BIOS +
  37/37 UEFI; chinesetest 101/101; full regression 28/28 (command
  baseline 221); screenshot walkthrough of every Chinese TUI page
  (main menu, hardware 7 pages, wireless, system-info 8 sub-pages,
  IME, time form) plus the update page (check-update feedback "已是
  最新版本" stays visible after the operation).  133,203 lines of
  tracked .c/.h/.S source excluding the four generated tables
  (chinese_font_data.c, pinyin_data.c, grub_boot_data.c,
  screen_font_data.c).  WP-10-chinese_support-fix3 is the 23rd work
  package.
- **Previous (WP-10-chinese_support-fix2)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-chinese_support-fix2) — ISO + SRC zip
  (ISO sha256 `156b8eabfaab86a4578c3656a6b4eb968cf40bc7df03b1c45582dc6caeb9e301`,
  SRC sha256 `c6e8e7c5cb85879f2b9f246b7fe26f61e2502cfb69654710c7e05f7b4cb7e409`)
  Batch summary: user-reported fix batch on the chinese_support release —
  every CJK glyph now renders fully in the TUI (the WIDE_CONT cell is no
  longer repainted with the background color, killing the right half of
  each hanzi), three TUI session-leak paths closed (tui / tui-cn /  
  chinesetest leave the screen alive), the CSI decoder delivers a lone
  ESC instead of eating it, the hardware page really scrolls, volume /
  clock preferences are consumed, mouse events reach menus, the Chinese
  update page runs the real OTA flow, the Chinese pages match the English
  parity (process priority + scroll, monitor auto-sampling, window
  resize), the S3 resume trampoline is real 16-bit big-real-mode code
  (firmware calls it with farcall16big) and the full resume chain
  re-arms (timer, PCI command registers, legacy e1000, UART RX), fsck
  goes through the blk registry, ping/dhcp return honest exit codes and
  the shell supports $?.  Gates: 0 errors 0 warnings; TUI E2E 37/37
  BIOS + 37/37 UEFI; CN E2E 17/17 x2; render-pixel E2E ALL PASS;
  suspend E2E ALL PASS on two QEMU generations (10.0.13 + 8.1.5);
  system E2E 15/15; regression 28/28; chinesetest 101/101.
  127,616 lines of source.  WP-10-chinese_support-fix2 is the 22nd
  work package.
- **Previous (WP-10-chinese_support)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-chinese_support) — ISO + SRC zip
  (ISO sha256 `4bddd0bd30b38a87469f8385f26944c2e06d091208bbc3a065ea9be16f783501`,
  SRC sha256 `7d01c38ab7f0fb74f9e7bcc5566051aaa7dce3849b63c39eac3830bc150ed42e`)
  Batch summary: Chinese support enters L0 — a new chinese/ module with
  the embedded GB2312 16x16 bitmap font (6763 hanzi zones 16-87 =
  216,416 B compact + 682 symbols zones 1-9 = 21,824 B, generated
  exactly from the GNU Unifont rectangle outlines by
  tools/gen_chinese_font.py; zone/Unicode index tables both
  directions), the GB2312 + UTF-8 codec (strict RFC 3629: overlong /
  surrogate / >U+10FFFF / truncated rejected; bulk conversion any
  direction through the Unicode pivot; heuristic detection ASCII /
  UTF-8 / GB2312 / mixed), a pluggable font registry (font_register /
  font_select / font_get_bitmap / font_get_metrics) with the
  mixed-width renderer (the console cell stays 8x16 - a CJK glyph
  spans two cells - so cols/rows/scroll/ASCII output are unchanged),
  the console cn write path (console_cn_putcw / puts_utf8 with the
  stock putc byte path untouched), UTF-8 aware line editing
  (character-boundary cursor movement, backspace, delete, Ctrl+W and
  display-width redraw; serial bridge now passes 8-bit bytes biased
  +0x100 so pasted CJK reaches the editor), UTF-8 aware `cat`, the
  TUI Chinese layer (tui_cell_t already stores u32 codepoints;
  tui_print decodes UTF-8, wide cells carry TUI_WIDE_CONT) with three
  Chinese applications (hardware configuration / wireless network /
  system information - same live driver data sources as their English
  twins, Chinese labels and descriptions) and a pinyin IME over the
  GB2312 level-1 set (3755 chars, 396 tone-free syllables, digit-key
  candidate selection, generated by tools/gen_pinyin_table.py).  9 new
  commands (chinese / chinesetest / font / font-test / encoding /
  encoding-test / ime / ime-test / tui-cn, 213 -> 222, additions
  only); the numbered L1 set stays closed at 1-138 - the 34 new
  interfaces are driver-API style, single-listed in
  docs/EXTENSIONS_WP10-chinese_support.md.  Gates: 0 errors 0 warnings;
  chinesetest 91/91 in-kernel suite; Chinese E2E 14/14 BIOS + 14/14
  UEFI; TUI E2E 32/32 BIOS + 32/32 UEFI; full regression 28/28
  (command baseline 222); dhtest 5/5; SSH E2E 5/5; HTTPS E2E 7/7;
  host suite 17 PASS / 0 FAIL / 2 SKIP-FIXTURE; pixel-verified CJK
  rendering (4/4 glyph blocks + 572 title pixels on the screendump).
  125,776 lines of source.  WP-10-chinese_support is the 21st work
  package.
- **Previous (WP-10-tui)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-tui) — ISO + SRC zip
  (ISO sha256 `163c38089a0c5e42e3ead78f97acda249f851916b57cbf8791ef5c843d1ba323`,
  SRC sha256 `fd719775788af97108a26770408c1d744301dd453fbdfb80991a775fcc478788`)
  Batch summary: a terminal user interface (TUI) enters L0 — tui/ (12
  files) with two real render backends (framebuffer character console +
  ANSI COM1 serial terminal, 16-color palette + 256-color nearest-entry
  map), window system, text/attributes, input (OC_KEY mapping + a real
  ANSI CSI decoder + SGR mouse + touchpad path), panels (pointer-slot
  z-order), menus, forms; the hardware configuration TUI (11 domains
  through the live driver query APIs, 9 config keys persisted via
  lib_config to /etc/opencube.conf: tui.backend/tui.clock_24h/
  tui.tz_offset/snd.volume), the wireless network TUI (WiFi scan/list/
  connect through the real STA state machine + Bluetooth manager through
  the real HCI stack) and the system information TUI (overview/
  processes/log ring/monitor/power/update/time/help).  5 new commands
  (tui/tui-test/tui-hw/tui-net/tui-sysinfo, 208 -> 213, additions only)
  and the ring3 socket API (SYS 103-109, syscalls 46 -> 53); docs/
  EXTENSIONS_WP10-tui.md carries the 59-interface contract.  The same
  tree dispositions all 12 handover suspicions: the WiFi->driver_nic
  bridge (wl0) + net_poll draining every present framework NIC, the
  resident USB poll thread actually starting, OTA signature verification
  tracked in KNOWN_ISSUES (key custody decision), the fs/_raw cache
  bypass documented, access_ok_str bounded to 16 pages, fork full-copy +
  MAX_TASKS=32 documented, real SB16 mixer/UAC1 volume (virtio honest
  -1), the make test host-suite target, 174 stale File: header paths,
  the WP08a 8-items/21-functions doc note and the P3 ledger.  Gates:
  0 errors 0 warnings; TUI E2E 32/32 BIOS + 32/32 UEFI; full regression
  28/28 (command baseline 213); dhtest 5/5; SSH E2E 5/5; HTTPS E2E 7/7;
  host suite 17 PASS / 0 FAIL / 2 SKIP-FIXTURE.  121,890 lines of
  source.  WP-10-tui is the 20th work package.
- **Previous (WP-10-camera_misc)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-camera_misc) — ISO + SRC zip
  (ISO sha256 `a07183ac4c635cde9484183e50e3e81cd847eba5ffcf43502c5d537ffa6f8e2e`,
  SRC sha256 `4a32b71a9684d411bab5580fd350e8c4d3e9b7c70cdbe6f7079a7c1cb6e0bcd6`)
  Batch summary: five hardware domains as bare-metal drivers — UVC cameras
  (USB Video Class 1.1 descriptor parser over the WP-10d USB stack, Probe/
  Commit control, alternate-setting switching, baseline JPEG decoder with
  YCbCr->RGB, a 20-interface camera core), IEEE 1284 parallel (SPP forward/
  reverse, EPP 1.7/1.9, ECP mode+FIFO, LPT1-LPT3), the game port (RC-timing
  axes + calibration + 4 buttons), infrared (CIR over ACPI + SIR over the
  secondary 16550 loopback-verified, NEC/RC-5/RC-6/SIRC codecs) and SCSI
  (a SAM-style command core with CDB builders, response parsers, per-device
  queues, UNIT ATTENTION retry, plus an LSI 53C895A HBA backend driven by
  mini-SCRIPTS programs including the asynchronous disconnect/reselect
  protocol; direct-access targets land in the blk layer as sda.. and go
  through MBR/GPT like every other backend), 10 new commands (uvc/uvctest/
  parallel/paralleltest/gameport/gameporttest/ir/irtest/scsi/scsitest,
  198 -> 208, additions only), gates 0/0 build, batch E2E 12/12 BIOS +
  12/12 UEFI (LSI + scsi-hd + parallel chardev verified live; scsitest
  17/17 incl. the WRITE/READ round trip with content restore), regression
  28/28, SSH 5/5, HTTPS 7/7, host_camera_test 30/30 (-Os), host_ir_test
  21/21, host_scsi_test 49/49.  WP-10-camera_misc is the 19th work package
  (counted in the Stats section above; like the earlier WP-10 batches it
  has no separate `## WP-*` section — details in
  docs/EXTENSIONS_WP10-camera_misc.md).
- **Previous (WP-10-wireless_bluetooth)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-wireless_bluetooth) — ISO + SRC zip
  (ISO sha256 `1e609eef7517f8e89cc6d6b824f7440548b13d76beeae86b92a2292e6bb5d1a1`,
  SRC sha256 `888b8da80a02c3c928be6dceb875b06c6279df220a7adb4ea81af5a935c12d85`)
  Batch summary: 802.11 core + seven wireless families (Intel iwlwifi,
  Realtek rtl8188/8192/8821, Atheros ath9k/ath10k/ath11k, Broadcom
  b43/brcmfmac with a real SDHCI/SDIO bus) + a full Bluetooth stack
  (HCI, H4 UART on COM2, HCI-USB btusb, L2CAP, RFCOMM with real
  CRC-8 FCS, SDP, SPP, HID), 2 new commands (wifi / bt, 196 -> 198,
  additions only), gates 0/0 build, regression 28/28, dhtest 5/5,
  SSH 5/5, HTTPS 7/7, batch E2E 32/32 BIOS + 32/32 UEFI (Bluetooth
  verified end-to-end in QEMU against a real H4 UART peer;
  datasheet-level verification for the wireless families - QEMU has
  no 802.11 model), host_wifi_test 38/38, host_bt_test 22/22.
  WP-10-wireless_bluetooth is the 18th work package (counted in the
  Stats section above; like WP-10-input_chipset and WP-10-timer_rtc
  it has no separate `## WP-*` section — details in
  docs/EXTENSIONS_WP10-wireless_bluetooth.md).
- **Previous (WP-10-timer_rtc)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-timer_rtc) — ISO + SRC zip
  (ISO sha256 `4377112140a56e07c8e668c7e2a3d530a51aed570b40ceb8dad403c517aba32f`,
  SRC sha256 `04489327a25770fad50ca974bdc7e37ddca42a8bb41c6b004d490bfa6eb21430`)
  Batch summary: four time-source domains (CMOS RTC status + IRQ8
  update-ended/periodic/alarm interrupts; HPET table + MMIO + one-shot/periodic
  comparators on timer 1 via the legacy route; ACPI PM timer with 24-bit-masked
  wrap arithmetic + PIT calibration; TSC with PIT/HPET/PMTimer three-reference
  calibration + TSC-driven delay), 4 new commands (rtc / hpet / pmtimer / tsc,
  192 -> 196, additions only), gates 0/0 build, regression 28/28, dhtest 5/5,
  SSH 5/5, HTTPS 7/7, batch E2E 29/29 BIOS + 29/29 UEFI. WP-10-timer_rtc is
  the 17th work package (counted in the Stats section above; like
  WP-10-input_chipset it has no separate `## WP-*` section — details in
  docs/EXTENSIONS_WP10-timer_rtc.md).
- **Previous (WP-10-timer_rtc, 2026-10-13)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-timer_rtc) — ISO + SRC zip
  (ISO sha256 `4377112140a56e07c8e668c7e2a3d530a51aed570b40ceb8dad403c517aba32f`,
  SRC sha256 `04489327a25770fad50ca974bdc7e37ddca42a8bb41c6b004d490bfa6eb21430`)
  Batch summary: four time-source domains (CMOS RTC status + IRQ8
  update-ended/periodic/alarm interrupts; HPET table + MMIO + one-shot/periodic
  comparators on timer 1 via the legacy route; ACPI PM timer with 24-bit-masked
  wrap arithmetic + PIT calibration; TSC with PIT/HPET/PMTimer three-reference
  calibration + TSC-driven delay), 4 new commands (rtc / hpet / pmtimer / tsc,
  192 -> 196, additions only), gates 0/0 build, regression 28/28, dhtest 5/5,
  SSH 5/5, HTTPS 7/7, batch E2E 29/29 BIOS + 29/29 UEFI. WP-10-timer_rtc is
  the 17th work package (counted in the Stats section above; like
  WP-10-input_chipset it has no separate `## WP-*` section — details in
  docs/EXTENSIONS_WP10-timer_rtc.md).
- **Previous (WP-10-input_chipset)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-input_chipset) — ISO + SRC zip
  (ISO sha256 `844e346d855723072daab7138625fceaaf068e6eb04fd72ee7a00938f378e9af`,
  SRC sha256 `83cae823acc65df935b981ad748c0917033360cd8035f2543e7e0e18db11bbbc`)
- **Previous (WP-10-AUDIT_P2-fix4)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-AUDIT_P2-fix4) — ISO + SRC zip
  (ISO sha256 `3fbb69a57e02441a62996d62ecdefa5ba21ea4ea3442f00e70e7f2c0ca4b852d`,
  SRC sha256 `f696064951e48893eacedb52f4e320b3d90d8b330815917e6d917b0b22d823f8`)
- **Previous (WP-10-AUDIT_P2-fix3, ISO rebuilt AFTER the tag so uname shows the
  release tag)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-AUDIT_P2-fix3)
  (ISO sha256 `f1f3fe85820b4e4f04e82e02f4c4f915735bf58bf9ccbf3af9defd76f0fb4686`,
  SRC sha256 `60fc5de5e3e3d492e468762af72f9f21dc54e52e6bcf1cd28aedd26e09019199`)
- **Archived (WP-08 series)**: [GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)
- Or visit https://cubestudio-dev.github.io/OpenCubeOS for direct downloads

- **WP-10-chinese_support-fix4 (this batch)** closes the two defects
  the user could reproduce on screen and finishes the TUI as a second
  frontend.  Line editor: the interactive loop used to print "oc> "
  itself while the editor session held an EMPTY prompt, so every
  arrow/history redraw restarted at column 0 and overwrote the
  prompt; the session now owns the prompt (synced from $PS1) and
  tracks last_drawn_w so shrinking lines (history nav, Ctrl+U/K)
  erase the stale tail.  Rendering: echo/ls emit user content through
  the cn path (ASCII byte-identical, UTF-8 as real 16x16 glyphs);
  a systematic verification echoed 120 characters chosen for dense
  bottom-row ink and compared the screendump against the font
  bitmaps pixel by pixel - 30720 pixels, 0 mismatches (the fix3-era
  "bottom 1/5 missing" report is a screenshot-crop artifact).
  TUI: tui-time (clock & sources / timezone / real CMOS RTC write),
  tui-sys (host/network/storage/user/security/update/log/TUI-look,
  all through the same lib_config keys the shell exposes),
  tui-drv (12 hardware pages including the new Display page),
  tui-ext (the L1 extension lab).  TUI L1 extension registry with
  the seven documented interface sets (menu/page/widget/command/
  theme/hotkey/event), builtin themes, F5 repaint hotkey, and a
  selftest that reports ALL PASS on screen.  Dvorak keyboard layout
  with runtime switching (kbd.layout) and a quiet log level
  (log.level=1 suppresses non-failure stage lines).  Command count
  221 -> 225 (tui-time/tui-drv/tui-sys/tui-ext; nothing removed,
  help three modes unchanged).

- **WP-10-tui-unified (this batch)** unifies the TUI into one
  character desktop.  Single entry `tui` (the tui-cn launcher is
  removed, 225 -> 224; the tui-* app commands stay); a fully
  dynamic 6-level menu registry (tui_dt_node_add/remove with dup /
  orphan / depth enforcement) drives every entry; five desktop
  applications over the real subsystems - Files (VFS file manager
  with clipboard), Processes (live g_procs + PMM + kill-by-tid as
  the generic L1 API user_process_kill_by_tid), Editor (the real
  nano over the shell command path), Terminal (ring-3 ush,
  syscall-only), System Info - plus Control Panel, Chinese Input,
  Command Bridge (shell_cmd_enum + shell_execute_captured) and the
  Extensions lab.  en/zh labels driven by shell.lang with runtime
  F2 switching, resident status bar with digit quick-open (1-9),
  green/red toasts and loading feedback, and tui_dt_guard crash
  containment.  Exception isolation now covers ring 3 too: a TSS
  I/O-permission-bitmap bug (iopb_offset field sat at 0x64, the
  SDM puts it at 0x66 - the bitmap silently pointed at the all-zero
  TSS header and every port was allowed from ring 3 since WP-02)
  is fixed; `run priv_test` proves `out 0x70` from ring 3 raises
  #GP(0), the process dies, the kernel continues (BIOS + UEFI).
  Gates: tui-test 90/90, regression 28/28, chinese E2E 21/21 x2,
  TUI E2E 37/37 x2, 0 errors / 0 warnings.

## WP-11-crash_recovery (done) - panic capture + core dump + crash log + recovery policy + stack guards + SIGSEGV + TUI containment

  "When the kernel crashes, the system must not be stone dead": every
  fatal path now captures, records, and either stops safely or
  reboots.  kernel/core/core_crash.c/.h implements the layer: a
  unified panic path (banner + 16-GPR register snapshot + call
  traceback on serial AND the VGA console in red; rbp-chain traceback
  with a labelled approximate stack scan for this O2 kernel), the
  kernel_panic_register_handler observer chain (a handler claiming
  recovery resumes the faulting frame), core dumps (oc_dump_hdr_t +
  22-u64 register block + kernel-stack window, kmalloc'd once at
  boot, persisted to /var/crash/core.dump via VFS or hex-dumped on
  the serial line with no filesystem), a 32-entry crashlog ring
  (vector/ring/err/rip/rsp/cr2/task/tid/pid + RTC timestamps,
  crashlog_record from any fault context, crashlog_flush to
  /var/log/crash.log), the post-panic policy (HALT safe loop or the
  production 8042+0xCF9 REBOOT - QEMU-verified panic -> reset ->
  second boot), a soft watchdog (1 s tick, boot-disarmed by documented
  decision, crashtest wd exercises the real tick path) and OC_ASSERT.
  Kernel-stack overflow detection: every kthread's lowest stack page
  is an unmapped guard page, the scheduler bounds-checks the next
  task's saved rsp before switching, and the dispatcher attributes
  guard-page #PF / burned-rsp #DF as KERNEL STACK OVERFLOW with the
  owning task name.  Ring-3 SIGSEGV: the P3-12 delivery primitive
  was extracted into user_signal_deliver() so a faulting user process
  with a registered handler gets a REAL signal (iretq straight into
  it; 3-delivery ping-pong guard then force-kill, reset on exec);
  run sigsegv_test / run stackovf / run badapp prove catch, kill and
  parent-wake.  TUI crash containment: linker.ld exports the TUI code
  window, and a ring-0 fault inside it with an active tui_dt_guard()
  longjmps back to the desktop instead of panicking (Control Panel ->
  Crash test page raises one on demand; the menu survives).  New
  commands: crashtest (panic/exc/pf/assert/wd/ovf/dump/policy) and
  dumpinfo; crashlog gained the flush subcommand + formatted output
  (name unchanged).  Command count 224 -> 226.  Boundary, stated in
  KNOWN_ISSUES 15.13: the #DF path itself prints + attributes + stops
  at dispatcher depth but records no dump/log entry (destroyed-stack
  context re-faulted every deeper continuation during bring-up).
  Gates: WP-11 E2E 14/14 (tools/wp11_crash_e2e.py), regression 28/28,
  dhtest, cryptotest, SSH E2E 5/5, HTTPS E2E 7/7, BIOS + UEFI boots,
  0 errors / 0 warnings.

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
