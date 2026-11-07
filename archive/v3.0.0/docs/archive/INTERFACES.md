<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Interface Index

Interface numbering 1-138 closed as of WP-10-wp08fix1; counts re-verified
2026-10-14 at WP-10-camera_misc.  WP-10-wireless_bluetooth, WP-10-camera_misc
(like WP-10-timer_rtc / WP-10-input_chipset) add kernel driver APIs + shell
commands and keep the numbered L1 set closed at 138; the new driver APIs are
listed in docs/EXTENSIONS_WP10-wireless_bluetooth.md and
docs/EXTENSIONS_WP10-camera_misc.md.
Items 58-122 were itemized individually at the 2026-10 handover
continuation: they had been counted in every release note
(57 + 8 + 13 + 7 + 26 + 11 + 7 + 9 = 138) but never listed per-item
here. The per-item tables are in section 1 below, with the evidence
chain and one honest caveat about the WP-10a slot assignment.

Single-page index of every interface Open Cube OS exposes to upper layers
(L1) and to the shell. Signatures are copied verbatim from the headers —
see each header for full docs and examples.

Work-package overview: docs/EXTENSIONS.md (WP-01) through
docs/EXTENSIONS_WP09.md (WP-09), docs/EXTENSIONS_WP10a.md (WP-10a,
storage drivers: AHCI / NVMe / ATA DMA / virtio-blk) and
docs/EXTENSIONS_WP10b.md (WP-10b, NIC drivers: e1000e / igb / ixgbe /
RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx / legacy others).

## 1. L0 → L1 extension API (l1/*.h + the per-domain driver headers)

### l1/l1_ext.h — WP-01 (framebuffer / renderer / font / console)

```c
const screen_fb_info_t* l1_ext_fb_get_info(void);
screen_renderer_t*      l1_ext_set_renderer(screen_renderer_t* new_renderer);
int   l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);
void  l1_ext_set_console_hook(screen_console_hook_fn hook, void* ctx);
void  l1_ext_console_hook(u8 ch);   /* called by L0 console */
int   l1_ext_self_test(void);
```

### l1/l1_wp8a.h — WP-08a (processes / signals / syscalls / fd)

```c
sys_proc_fork / sys_proc_exec / sys_proc_exit / sys_proc_wait
sys_proc_getpid / sys_proc_getppid
signal_register / signal_return / signal_send
sys_mem_brk / sys_mem_mmap / sys_mem_mprotect / sys_mem_munmap
sys_select / sys_poll
sys_chdir / sys_getcwd / sys_ioctl
pipe_create / fd_dup / fd_dup2
```
(21 functions; exact signatures in the header.)

### l1/l1_wp8b.h — WP-08b (dynamic linking)

```c
ldso_run
dlopen_impl / dlsym_impl / dlclose_impl
so_load / so_unload
symbol_resolve / reloc_apply
elf_load_dynamic / elf_get_needed
ext_wp8b_selftest
```
(11 functions.)

### l1/l1_wp8cd.h — WP-08cd (user shell / tools / jobs)

```c
shell_run / shell_register_builtin
tool_register / tool_list
job_create / job_list / job_control
```
(7 functions.)

### Numbered items 58-122 — WP-10a / WP-10b / WP-10u / WP-10c / WP-10d

Backfilled at the 2026-10 handover continuation. Numbering follows the
README / EXTENSIONS enumeration order of each batch. Evidence chain:
README.md L60-90 (per-batch enumerations + the 57+8+13+7+26+11+7+9=138
tally), EXTENSIONS_WP10a.md §2, EXTENSIONS_WP10b.md §1-§11 order,
EXTENSIONS_WP10u.md L117-123, EXTENSIONS_WP10c.md L43-70,
EXTENSIONS_WP10d.md §1-§11, and the header declaration lines cited per
row. No header carries an "item N" comment for 58-122 — the numbering
was only ever a per-batch count.

**Honest caveat (WP-10a, items 58-65):** the batch introduced eleven
functions; the original WP-10a release README already listed ten+ names
under "8 WP-10a items", and the current README lists
`driver_pci_find_class_exact` / `driver_pci_find_class_mask` /
`driver_block_ata_identify_capacity` outside the numbered 8 (download
section) while its WP-10a section still shows them inside the
"items 58-65" header list. No per-slot assignment was ever recorded, so
the table follows the current README download-section order and treats
the three PCI/capacity helpers as unnumbered WP-10a-batch interfaces —
the same convention already used for the 5 unnumbered WP-09-fix5 config
interfaces and the 5 rule-9 self-hosting interfaces.

#### WP-10a — storage drivers (items 58-65)

| Item | Interface | Header (decl line) | Purpose |
|---|---|---|---|
| 58 | `driver_block_register` | drivers/block/driver_block_blk.h:133 | pointer-based block-device registration (filled `driver_block_device_t` + ops; returns slot index) |
| 59 | `driver_block_read` | drivers/block/driver_block_blk.h:134 | cached read through the device pointer |
| 60 | `driver_block_write` | drivers/block/driver_block_blk.h:135 | write-back cached write |
| 61 | `driver_block_flush` | drivers/block/driver_block_blk.h:136 | pushes the OS write-back cache, then calls the driver `.flush` hook |
| 62 | `driver_block_set_ops` | drivers/block/driver_block_blk.h:141 | in-place ops replacement (PIO→BMDMA upgrade path) |
| 63 | `driver_block_ahci_init` | drivers/block/driver_block_ahci.h:39 | AHCI SATA controller probe/init (`pdev=NULL` scans PCI; registers `sda`…) |
| 64 | `driver_block_nvme_init` | drivers/block/driver_block_nvme.h:32 | NVMe init (admin + 2 I/O queue pairs; registers `nvme0`) |
| 65 | `driver_block_ata_dma_init` | drivers/block/driver_block_ata_dma.h:35 | IDE Bus-Master DMA init (PRDT; PIO fallback preserved; `hda`..`hdd`) |

Unnumbered WP-10a-batch helpers (introduced with the batch, kept
outside the 1-138 numbering): `driver_pci_find_class_exact` /
`driver_pci_find_class_mask` (drivers/pci/driver_pci.h:44/51) and
`driver_block_ata_identify_capacity`
(drivers/block/driver_block_ata.h:37).

#### WP-10b — NIC drivers (items 66-78)

| Item | Interface | Header (decl line) | Purpose |
|---|---|---|---|
| 66 | `driver_nic_register` | drivers/nic/driver_nic.h:111 | register a NIC into the 8-slot table (send/recv required in ops) |
| 67 | `driver_nic_send` | drivers/nic/driver_nic.h:114 | hand one Ethernet frame to the driver (returns bytes accepted) |
| 68 | `driver_nic_recv` | drivers/nic/driver_nic.h:115 | poll one frame from the driver (>0 len / 0 none / -1 error) |
| 69 | `driver_nic_link_status` | drivers/nic/driver_nic.h:118 | link state: 1 up / 0 down / -1 unknown |
| 70 | `driver_nic_get_mac` | drivers/nic/driver_nic.h:121 | copy out the 6-byte station address |
| 71 | `driver_nic_e1000e_init` | drivers/nic/driver_nic.h:140 | Intel 82574/82540 family GbE init |
| 72 | `driver_nic_igb_init` | drivers/nic/driver_nic.h:142 | Intel 82575/82576 server GbE |
| 73 | `driver_nic_ixgbe_init` | drivers/nic/driver_nic.h:143 | Intel 82598/82599 10GbE |
| 74 | `driver_nic_rtl8139_init` | drivers/nic/driver_nic.h:144 | Realtek RTL8139 100M |
| 75 | `rtl8168_init` | drivers/nic/driver_nic.h:145 | Realtek RTL8168/8111 GbE |
| 76 | `rtl8125_init` | drivers/nic/driver_nic.h:146 | Realtek RTL8125 2.5G |
| 77 | `rtl810x_init` | drivers/nic/driver_nic.h:147 | Realtek RTL810x FE |
| 78 | `bcm57xx_init` | drivers/nic/driver_nic.h:148 | Broadcom BCM57xx/BCM440x (tg3-style rings + mailboxes) |

(`driver_nic_probe_all` / `driver_nic_active` are framework additions
of the same batch, listed in section 3, not numbered.)

#### WP-10u — in-system A/B updates (items 79-85)

| Item | Interface | Header (decl line) | Purpose |
|---|---|---|---|
| 79 | `l1_ext_update_check_pkg` | l1/l1_ext.h:193 | fetch update.json and fill package info (0 same / 1 newer / negative error) |
| 80 | `l1_ext_update_download` | l1/l1_ext.h:194 | two-phase streaming HTTP/HTTPS download to a VFS path |
| 81 | `l1_ext_update_verify` | l1/l1_ext.h:195 | streaming SHA-256 check (case-insensitive hex compare) |
| 82 | `l1_ext_update_install` | l1/l1_ext.h:196 | gunzip→untar→install into the A/B slot + manifest digest + grub.cfg mirror |
| 83 | `l1_ext_update_rollback` | l1/l1_ext.h:197 | clear boot flags so the next boot returns to slot A |
| 84 | `l1_ext_update_set_boot` | l1/l1_ext.h:198 | set next boot slot (B: next_B + clear ok_B/bootfail_B; A: clear all) |
| 85 | `l1_ext_update_get_status` | l1/l1_ext.h:199 | fill `ota_update_status_t` (versions / current slot / next slot / online switch / A-B present) |

(Kernel-side twins: the `ota_update_*` / `ota_ab_*` functions in
kernel/ota/ota_ab.h:157-183 — that header self-describes as "also the
L1 extension surface".)

#### WP-10c — sound cards + WP-10c UHCI stack (items 86-111)

| Item | Interface | Header (decl line) | Purpose |
|---|---|---|---|
| 86 | `driver_snd_init` | drivers/snd/driver_snd.h:149 | sound framework init (clears the 8-slot registry) |
| 87 | `driver_snd_register` | drivers/snd/driver_snd.h:152 | register a sound card (play/stop required; returns device index) |
| 88 | `driver_snd_play` | drivers/snd/driver_snd.h:155 | blocking PCM feed (S16LE stereo; DMA-position flow control) |
| 89 | `driver_snd_stop` | drivers/snd/driver_snd.h:158 | stop the stream and drain the DMA ring |
| 90 | `driver_snd_set_rate` | drivers/snd/driver_snd.h:161 | configure the sample rate (44100/48000…; -1 if unsupported) |
| 91 | `driver_snd_set_volume` | drivers/snd/driver_snd.h:164 | volume 0..100 |
| 92 | `driver_snd_get_caps` | drivers/snd/driver_snd.h:167 | copy out the capability block (rate bitmask / channels / bits / state) |
| 93 | `driver_snd_num_devices` | drivers/snd/driver_snd.h:170 | number of registered cards |
| 94 | `driver_snd_get_device` | drivers/snd/driver_snd.h:171 | device pointer by index |
| 95 | `driver_snd_find_by_type` | drivers/snd/driver_snd.h:172 | device index by family type |
| 96 | `driver_snd_find_first_present` | drivers/snd/driver_snd.h:173 | first present device index |
| 97 | `driver_snd_list_devices` | drivers/snd/driver_snd.h:174 | list registered cards (`sound` command) |
| 98 | `driver_snd_type_name` | drivers/snd/driver_snd.h:175 | family enum → name string |
| 99 | `driver_snd_probe_all` | drivers/snd/driver_snd.h:181 | probe all 6 families in priority order (0 if ≥1 card came up) |
| 100 | `driver_snd_make_tone` | drivers/snd/driver_snd.h:205 | generate a sine test tone (play / test commands) |
| 101 | `driver_snd_hda_init` | drivers/snd/driver_snd.h:184 | Intel HDA init (CORB/RIRB, widget walk, BDL DMA) |
| 102 | `driver_snd_ac97_init` | drivers/snd/driver_snd.h:185 | Intel 82801AA AC'97 (mixer + BDL DMA) |
| 103 | `driver_snd_sb16_init` | drivers/snd/driver_snd.h:186 | ISA Sound Blaster 16 (DSP 4.05) |
| 104 | `driver_snd_es1370_init` | drivers/snd/driver_snd.h:187 | Ensoniq ES1370/1371 (DAC2 frame DMA) |
| 105 | `driver_snd_virtio_init` | drivers/snd/driver_snd.h:188 | virtio-snd (1AF4:1059, modern virtio-pci) |
| 106 | `driver_usb_audio_init` | drivers/snd/driver_snd.h:189 | USB Audio Class 1.0 (via the WP-10c UHCI stack) |
| 107 | `driver_usb_init` | drivers/usb/driver_usb.h:312 | WP-10c-era UHCI host stack bring-up (PIIX3/4) |
| 108 | `driver_usb_enumerate` | drivers/usb/driver_usb.h:328 | enumerate devices per host (descriptors / SET_ADDRESS…) |
| 109 | `driver_usb_control` | drivers/usb/driver_usb.h:318 | blocking control transfer (SETUP+DATA+STATUS) |
| 110 | `driver_usb_set_interface` | drivers/usb/driver_usb.h:320 | SET_INTERFACE (audio lazy alt-switch) |
| 111 | `driver_usb_iso_out_submit` | drivers/usb/driver_usb.h:321 | isochronous OUT packet (one ISO TD per 1 ms frame) |

#### WP-10d — USB host stack, four backends + class-driver registry (items 112-122)

| Item | Interface | Header (decl line) | Purpose |
|---|---|---|---|
| 112 | `driver_usb_register_host` | drivers/usb/driver_usb.h:326 | register a host controller with the USB core (returns host->index) |
| 113 | `driver_usb_enumerate_host` | drivers/usb/driver_usb.h:327 | enumerate root ports + cascaded hubs of one host |
| 114 | `driver_usb_control_transfer` | drivers/usb/driver_usb.h:331 | unified blocking control transfer (global USB mutex, backend dispatch) |
| 115 | `driver_usb_bulk_transfer` | drivers/usb/driver_usb.h:333 | blocking bulk transfer (`_timeout` variant exists) |
| 116 | `driver_usb_interrupt_transfer` | drivers/usb/driver_usb.h:336 | single blocking interrupt IN/OUT (HID polling) |
| 117 | `driver_usb_isochronous_transfer` | drivers/usb/driver_usb.h:338 | schedule one isochronous packet (UHCI/XHCI) |
| 118 | `driver_usb_register_driver` | drivers/usb/driver_usb.h:343 | register a class driver (probe/disconnect; 0xff = match-all) |
| 119 | `driver_usb_uhci_init` | drivers/usb/driver_usb.h:39 (impl driver_usb.c:1609) | UHCI backend (8086:7020/7112; frame list + QH/TD) |
| 120 | `driver_usb_ohci_init` | drivers/usb/driver_usb.h:40 (impl driver_usb_ohci.c:963) | OHCI backend (class 0x0c0310; HCCA + ED/TD pools) |
| 121 | `driver_usb_ehci_init` | drivers/usb/driver_usb.h:41 (impl driver_usb_ehci.c:969) | EHCI backend (class 0x0c0320; async ring + qTD + IAAD) |
| 122 | `driver_usb_xhci_init` | drivers/usb/driver_usb.h:42 (impl driver_usb_xhci.c:1685) | XHCI backend (class 0x0c0330; command/event rings + DCBAA + two-step AddressDevice) |

### shell/shell_lineedit.h + shell/editor.h — WP-10-wp08fix1 (items 130-138)

```c
/* kernel-side oc> line editor: */
void        shell_lineedit_init(void);                     /* item 130 */
void        shell_lineedit_history_add(const char *cmd);   /* item 131 */
const char *shell_lineedit_history_get(int idx);           /* item 132 */
void        shell_lineedit_cursor_move(int dir);           /* item 133 */
int         shell_lineedit_tab_complete(void);             /* item 134 */
int         shell_lineedit_ctrlc(void);                    /* item 135 */
/* nano-style full-screen editor: */
int         editor_open(const char *file);                 /* item 136 */
int         editor_save(void);                             /* item 137 */
int         editor_close(void);                            /* item 138 */
```
(9 numbered items — the interface numbering 1-138 closes here. Signatures
verbatim from docs/EXTENSIONS_WP10-wp08fix1.md; the same batch added the
VFS link/symlink + chmod/chown surface and SYS 96-102.)

### l1/l1_wp2.h, l1/l1_wp3.h, l1/l1_wp7.h

Type/macro-only headers (no callable functions) for WP-02/WP-03/WP-07
subsystem structures. The WP-02/03/05/06/07 "extension points" are exposed
as direct kernel APIs (see §3) rather than l1_ext_* functions.

## 2. WP-09 transport APIs (net/net_ssh.h, net/net_tls.h)

```c
/* SSH client (curve25519-sha256 / group14-sha256 KEX, aes128-ctr/cbc,
 * hmac-sha2-256, rsa-sha2-256/512 host key verified over H with TOFU
 * fingerprint, password + publickey auth, session exec) */
int  net_ssh_connect(u32 ip, u16 port, const char *username, const char *password);
int  net_ssh_exec(const char *command, void *output, int output_len);
void net_ssh_close(void);

/* SSH server (net/net_sshd.c): `sshd <port> <user> <password>` shell command;
 * serves one session then returns. exec requests are executed through the
 * kernel shell capture API (shell_execute_captured). */

/* TLS 1.3/1.2 client (TLS 1.3: X25519 + AES-GCM/ChaCha20-Poly1305;
 * TLS 1.2 fallback: ECDHE_RSA + AES-GCM/ChaCha20-Poly1305, legacy DHE-CBC
 * retained. X.509 chain + hostname verification against embedded public
 * CA roots — handshake fails closed on verification errors) */
int  net_tls_connect(u32 ip, u16 port, const char *hostname);
int  net_tls_send(net_tls_ctx_t *ctx, const void *data, int len);
int  net_tls_recv(net_tls_ctx_t *ctx, void *buf, int len);
void net_tls_close(net_tls_ctx_t *ctx);
int  net_tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                   void *out_buf, int out_len);
```

Crypto primitives: `kernel/crypto/crypto_core.h` (aes128 CTR/CBC, sha256, crypto_hmac_sha256,
hkdf, crypto_dh_modexp, crypto_random), `kernel/crypto/crypto_bn.h` (bignum modexp, Montgomery
core), `kernel/crypto/crypto_ec_nist.h` (P-256/P-384 ECDH+ECDSA), `kernel/crypto/crypto_curve25519.h`
(X25519), `kernel/crypto/crypto_rsa.h` (PKCS#1 v1.5 + PSS verify), `kernel/crypto/crypto_aead.h`
(AES-GCM, ChaCha20-Poly1305), `kernel/crypto/crypto_sha512.h` (SHA-512/384 + HMAC),
`kernel/crypto/crypto_x509.h` (certificate parse + chain/hostname verification).
DH truth vectors for self-test: kernel/crypto/crypto_dh_scale_vectors.h.

## 3. Kernel subsystem APIs (stable since their WP)

| Subsystem | Header | Key functions |
|---|---|---|
| Framebuffer | drivers/display/screen_fb.h | screen_fb_* |
| Console | drivers/display/screen_console.h | screen_console_* |
| Shell + command registration | shell/shell.h | shell_register_command, shell_execute_captured (WP-09) |
| PMM / VMM / heap | kernel/mem/mem_pmm.h, vmm.h, heap.h | mem_pmm_alloc_frame, mem_vmm_*, kmalloc/kfree/krealloc |
| Scheduler + sync | kernel/core/core_sched.h, sync.h | core_sched_*, spinlock/mutex/sem/cond |
| User mode + syscalls | kernel/core/core_usermode.h, syscall.h | user_process_create, syscall dispatch (53 syscalls, SYS 0-109; WP-10-tui added 103-109 = the ring3 socket API) |
| VFS + ramfs | fs/fs_vfs.h, ramfs.h | fs_vfs_open/read/write/stat/... |
| Network (TCP/IP) | net/net_core.h | net_socket/connect/send/recv/accept/close, net_dns_resolve, cmd-level: dhcp/ping/wget/dns/route/arp/firewall/tcpstats |
| Block + FS | drivers/block/driver_block_blk.h, part.h, fat32.h, exfat.h, ext4.h | driver_block_*, vfs mount (FAT32 R/W, exFAT R/W, ext4 RO) |
| Storage drivers (WP-10a) | drivers/block/driver_block_ahci.h, nvme.h, driver_block_ata_dma.h, virtio_blk.h | driver_block_ahci_init(driver_pci_dev), driver_block_nvme_init(driver_pci_dev), driver_block_ata_dma_init(driver_pci_dev), driver_block_virtio_blk_init, driver_pci_find_class_exact/mask (pci.h) |
| NIC drivers (WP-10b) | drivers/nic/driver_nic.h | driver_nic_register(dev, ops), driver_nic_send/recv(dev, ...), driver_nic_link_status(dev), driver_nic_get_mac(dev, mac), driver_nic_e1000e_init/driver_nic_igb_init/driver_nic_ixgbe_init/driver_nic_rtl8139_init/rtl8168_init/rtl8125_init/rtl810x_init/bcm57xx_init(driver_pci_dev), other_nics_init, driver_nic_probe_all, driver_nic_active |
| Sound cards (WP-10c) | drivers/snd/driver_snd.h, usb.h | driver_snd_register(dev, ops), driver_snd_play/stop/set_rate/set_volume/get_caps(dev, ...), driver_snd_hda_init/driver_snd_ac97_init/driver_snd_es1370_init/driver_snd_virtio_init(driver_pci_dev), driver_snd_sb16_init(isa_dev), driver_usb_audio_init(driver_usb_dev), driver_snd_probe_all; USB: driver_usb_init, driver_usb_enumerate, driver_usb_control, driver_usb_set_interface, driver_usb_iso_out_submit (drivers/usb/driver_usb.h) |
| USB host stack (WP-10d) | drivers/usb/driver_usb.h | driver_usb_register_host(host, ops), driver_usb_enumerate_host(host), driver_usb_control_transfer(d, setup, buf, len), driver_usb_bulk_transfer(d, ep, buf, len) (+ _timeout), driver_usb_interrupt_transfer(d, ep, buf, len), driver_usb_isochronous_transfer(d, ep, buf, len), driver_usb_register_driver(name, class, probe, disconnect), driver_usb_uhci_init / driver_usb_ohci_init / driver_usb_ehci_init / driver_usb_xhci_init(driver_pci_dev); class drivers hid-kbd/hid-mouse/usb-msc/usb-serial/usb-audio; shell: usb, usbdev |
| Power mgmt + structured help (WP-10d-fix2) | kernel/core/core_power.h, shell/shell.h | core_power_shutdown(void), core_power_suspend(void), core_power_halt(void), core_power_reboot(void) (kernel/core/core_power.h); shell_register_command_ex(name, fn, help, wp), shell_list_commands_a_z(void), shell_list_commands_by_wp(void) (shell/shell.h); L1 wrappers l1_ext_power_* / l1_ext_shell_* (l1/l1_ext.h) |
| TCP socket state (WP-10a-fix) | net/net_core.h | net_tcp_established(fd), net_tcp_established_or_close_wait(fd) (WP-10-AUDIT_P2-fix2: 1 when the connection is ESTABLISHED or CLOSE_WAIT, i.e. sending a close_notify alert is still legal) |
| Config + update check (WP-09-fix5, unnumbered) | l1/l1_ext.h, kernel/config.h | l1_ext_config_read(key, val_out, outlen), l1_ext_config_write(key, value), l1_ext_config_get_all(buf, buflen) (/etc/opencube.conf), l1_ext_check_update(&info), l1_ext_check_update_async(void); see docs/CONFIG.md |
| Rule-9 self-hosting (WP-10c-selfhost, unnumbered) | drivers/block/driver_block_part.h, kernel/ota/ota_ab.h, kernel/arch/x86_64/arch_multiboot2.h | driver_block_part_register_child(...), driver_block_part_write_mbr_table(...), driver_block_part_scan_register_all(void), ota_ab_rescan(void), arch_multiboot2_get_kernel_self(&data, &size); shell: abdisk / install / grub-install / abcfg — docs/EXTENSIONS_SELFHOST.md |

## 4. Shell command surface

222 commands registered at boot (live count; `help` lists 222 unique and
prints `Total: 222 commands`, `help -w` groups them by work-package tag).
Section updated at WP-10-chinese_support: the batch registered 9 new
commands (chinese, chinesetest, font, font-test, encoding,
encoding-test, ime, ime-test, tui-cn — chinese/chinese_cmd.c) so the boot
count moved 213 -> 222; nothing was removed (interface freeze =
additions only).
Previous update at WP-10-tui: the batch registered 5 new commands (tui,
tui-test, tui-hw, tui-net, tui-sysinfo — tui/tui_cmd.c) so the boot
count moved 208 -> 213; nothing was removed (interface freeze =
additions only).
Previous update at WP-10-camera_misc: the batch registered 10 new commands
(uvc, uvctest — shell/shell_cmds_camera.c; parallel, paralleltest,
gameport, gameporttest — shell/shell_cmds_pargame.c; ir, irtest —
shell/shell_cmds_ir.c; scsi, scsitest — shell/shell_cmds_scsi.c) so the
boot count moved 198 -> 208; nothing was removed (interface freeze =
additions only).  shell.h's SHELL_MAX_COMMANDS was widened 208 -> 224.
Previous update at WP-10-wireless_bluetooth: the batch registered 2 new
commands (wifi, bt — shell/shell_cmds_wireless.c and shell/shell_cmds_bt.c)
so the boot count moved 196 -> 198.  shell.c's per-command `wp` tag field was
widened 24 -> 32 bytes: "WP-10-wireless_bluetooth" plus NUL needs 25.
Previous update at WP-10-timer_rtc: the batch registered 4 new
time-source commands (rtc, hpet, pmtimer, tsc —
shell/shell_cmds_timer.c) so the boot count moved 192 -> 196.
Previous update at WP-10-input_chipset: the batch registered 16 new
chipset/input commands (acpi, acpidev, acpitest, acpipow, msitest,
pcidump, pcibars, pcie, i2cdetect, i2cget, i2cset, i2ctest, smbus,
smbustest, touchpad, touchtest — shell/shell_cmds_chipset.c) so the
boot count moved 176 -> 192. Previous update at WP-10-AUDIT_P2-fix2: fix1 added
three regression commands (`irqabitest`, `heapbounds`, `vmkernelpt`,
BUG-0136/0140/0141 attribution) moving 173 -> 176.
`shell_register_command` call sites (static count, re-verified for
WP-10-input_chipset by per-file grep = 192 unique names, no duplicates;
cross-checked against a live `help -a` extraction in QEMU each release):
kernel/main.c 56 (incl. update/rollback/reboot, pmmrace and the three
fix1 regression commands),
net/net_core.c 16,
shell/shell_cmds_file.c 18, shell/shell_cmds_disk.c 7,
drivers/block/driver_block_disk_setup.c 4 (rule-9 self-hosting:
abdisk/install/grub-install/abcfg), shell/shell_cmds_disk_test.c 8
(WP-10a storage test suite), shell/shell_cmds_nic_test.c 18 (WP-10b:
10 NIC tests + 8 NIC status commands), shell/shell_cmds_snd_test.c 17
(WP-10c sound status commands + tests), shell/shell_cmds_update_test.c 10
(WP-10u), shell/shell_cmds_usb_test.c 9 (WP-10d: usb/usbdev status +
7 test commands), shell/shell.c 6 (incl. WP-10-wp08fix1 nano/vi),
shell/shell_cmds_power_test.c 7 (WP-10d-fix2 power + help test suite),
shell/shell_cmds_chipset.c 16 (WP-10-input_chipset: 4 ACPI + 4
PCIe/MSI + 4 I2C + 2 SMBus + 2 touchpad),
shell/shell_cmds_timer.c 4 (WP-10-timer_rtc: rtc/hpet/pmtimer/tsc),
shell/shell_cmds_wireless.c 1 (WP-10-wireless_bluetooth: wifi),
shell/shell_cmds_bt.c 1 (WP-10-wireless_bluetooth: bt),
shell/shell_cmds_camera.c 2 (WP-10-camera_misc: uvc/uvctest),
shell/shell_cmds_pargame.c 4 (WP-10-camera_misc: parallel/paralleltest/
gameport/gameporttest), shell/shell_cmds_ir.c 2 (WP-10-camera_misc:
ir/irtest), shell/shell_cmds_scsi.c 2 (WP-10-camera_misc: scsi/scsitest).
tui/tui_cmd.c 5 (WP-10-tui: tui/tui-test/tui-hw/tui-net/tui-sysinfo).
chinese/chinese_cmd.c 9 (WP-10-chinese_support: chinese/chinesetest/
font/font-test/encoding/encoding-test/ime/ime-test/tui-cn).
56+16+18+7+4+8+18+17+10+9+6+7+16+4+1+1+2+4+2+2+5+9 = 222. Full list: type
`help` at the `oc>` prompt.

## 5. Verification of these interfaces

- L1 self-tests: `ext_selftest` (WP-01), l1test (WP-08cd), ext_wp8cd
  boot self-test — all PASS in the 18/18 regression.
- WP-09 transport E2E: HTTPS both sides, SSH both directions,
  K byte-level match.
