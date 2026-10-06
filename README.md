<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-AUDIT-01-p1fix2

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

## Stats (WP-AUDIT-01-p1fix2)

- **Source code**: 93,923 lines (kernel + boot + userprogs + fs + net + shell + l1 + drivers + libs, incl. headers, no docs;
  verify: `find kernel boot userprogs fs net shell l1 drivers libs \( -name '*.c' -o -name '*.h' -o -name '*.S' \) | xargs wc -l`)
- **Work packages**: 16 (WP-01 ~ WP-09, WP-10a, WP-10b, WP-10u, WP-10c, WP-10d, project restructure, WP-10-wp08fix1, WP-AUDIT-01 + p0fix1)
  + the rule-9 self-hosting batch (WP-10c-selfhost): in-system `abdisk`,
  `install` and `grub-install` - create A/B update disks, install the OS
  to a disk and write the GRUB BIOS boot loader entirely from the oc>
  shell (no host tools; the kernel payload travels with the boot media as
  a multiboot2 module).
- **L1 extension interfaces**: 129 (57 through WP-09 + 8 WP-10a items:
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
  controller backends (drivers/usb/driver_usb.h)
- **System calls**: 44
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

- **Complete syscall set** (37 syscalls): fork, exec, wait, kill, signal, mmap, munmap, mprotect, brk, pipe, dup, dup2, sigaction, sigreturn, select, poll, chdir, getcwd, ioctl, getpid, getppid, exit, write, write_and_exit, open, close, stat, readdir, mkdir, rmdir, unlink, exit2, map_solib, read, write2, readline, getc.
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
  controllers (class 0x04). 173 commands registered at boot
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
  / usb_hub_test (173 commands at boot; 138 L1 interfaces;
  WP-10d-fix2 added 7: core_power_shutdown/core_power_suspend/core_power_halt/core_power_reboot
  + shell_register_command_ex/shell_list_commands_a_z/shell_list_commands_by_wp).
- **Verification**: UHCI/OHCI/EHCI/XHCI exercised in QEMU with explicit
  controllers (piix3-usb-uhci, pci-ohci, usb-ehci, qemu-xhci); BIOS +
  UEFI boot matrix; usb_core_test 5/5 on all four; the OHCI session and
  the XHCI EPID fix close out the WP-10d controller work. See
  docs/EXTENSIONS_WP10d.md.

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

The canonical WP-09 regression is the **18/18 full QEMU suite** (boot banner,
uname, 12 user programs, p3_test, heaptest, l1test, crashlog), executed in one
QEMU session via `tools/qemu_runner.py`. In addition:

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

- **Latest (WP-AUDIT-01-p1fix2)**: [GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/wp-audit-01-p1fix2) — ISO + SRC zip
  (ISO sha256 `960af5549d7ae7408ccd0f5240d878435c96c14d60c68faec68120fe14ef1ff6`)
- **Archived (WP-08 series)**: [GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)
- Or visit https://cubestudio-dev.github.io/OpenCubeOS for direct downloads

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
