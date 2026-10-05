<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Interface Index (as of WP-10d-fix2)

Single-page index of every interface Open Cube OS exposes to upper layers
(L1) and to the shell. Signatures are copied verbatim from the headers —
see each header for full docs and examples.

Work-package overview: docs/EXTENSIONS.md (WP-01) through
docs/EXTENSIONS_WP09.md (WP-09), docs/EXTENSIONS_WP10a.md (WP-10a,
storage drivers: AHCI / NVMe / ATA DMA / virtio-blk) and
docs/EXTENSIONS_WP10b.md (WP-10b, NIC drivers: e1000e / igb / ixgbe /
RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx / legacy others).

## 1. L0 → L1 extension API (kernel/ext*.h)

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

### l1/l1_wp2.h, ext_wp3.h, ext_wp7.h

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
| User mode + syscalls | kernel/core/core_usermode.h, syscall.h | user_process_create, syscall dispatch (37 syscalls) |
| VFS + ramfs | fs/fs_vfs.h, ramfs.h | fs_vfs_open/read/write/stat/... |
| Network (TCP/IP) | net/net_core.h | net_socket/connect/send/recv/accept/close, net_dns_resolve, cmd-level: dhcp/ping/wget/dns/route/arp/firewall/tcpstats |
| Block + FS | drivers/block/driver_block_blk.h, part.h, fat32.h, exfat.h, ext4.h | driver_block_*, vfs mount (FAT32 R/W, exFAT R/W, ext4 RO) |
| Storage drivers (WP-10a) | drivers/block/driver_block_ahci.h, nvme.h, driver_block_ata_dma.h, virtio_blk.h | driver_block_ahci_init(driver_pci_dev), driver_block_nvme_init(driver_pci_dev), driver_block_ata_dma_init(driver_pci_dev), driver_block_virtio_blk_init, driver_pci_find_class_exact/mask (pci.h) |
| NIC drivers (WP-10b) | drivers/nic/driver_nic.h | driver_nic_register(dev, ops), driver_nic_send/recv(dev, ...), driver_nic_link_status(dev), driver_nic_get_mac(dev, mac), driver_nic_e1000e_init/driver_nic_igb_init/driver_nic_ixgbe_init/driver_nic_rtl8139_init/rtl8168_init/rtl8125_init/rtl810x_init/bcm57xx_init(driver_pci_dev), other_nics_init, driver_nic_probe_all, driver_nic_active |
| Sound cards (WP-10c) | drivers/snd/driver_snd.h, usb.h | driver_snd_register(dev, ops), driver_snd_play/stop/set_rate/set_volume/get_caps(dev, ...), driver_snd_hda_init/driver_snd_ac97_init/driver_snd_es1370_init/driver_snd_virtio_init(driver_pci_dev), driver_snd_sb16_init(isa_dev), driver_usb_audio_init(driver_usb_dev), driver_snd_probe_all; USB: driver_usb_init, driver_usb_enumerate, driver_usb_control, driver_usb_set_interface, driver_usb_iso_out_submit (drivers/usb/driver_usb.h) |
| USB host stack (WP-10d) | drivers/usb/driver_usb.h | driver_usb_register_host(host, ops), driver_usb_enumerate_host(host), driver_usb_control_transfer(d, setup, buf, len), driver_usb_bulk_transfer(d, ep, buf, len) (+ _timeout), driver_usb_interrupt_transfer(d, ep, buf, len), driver_usb_isochronous_transfer(d, ep, buf, len), driver_usb_register_driver(name, class, probe, disconnect), driver_usb_uhci_init / driver_usb_ohci_init / driver_usb_ehci_init / driver_usb_xhci_init(driver_pci_dev); class drivers hid-kbd/hid-mouse/usb-msc/usb-serial/usb-audio; shell: usb, usbdev |
| Power mgmt + structured help (WP-10d-fix2) | kernel/core/core_power.h, shell/shell.h | core_power_shutdown(void), core_power_suspend(void), core_power_halt(void), core_power_reboot(void) (kernel/core/core_power.h); shell_register_command_ex(name, fn, help, wp), shell_list_commands_a_z(void), shell_list_commands_by_wp(void) (shell/shell.h); L1 wrappers l1_ext_power_* / l1_ext_shell_* (l1/l1_ext.h) |
| TCP socket state (WP-10a-fix) | net/net_core.h | net_tcp_established(fd) |

## 4. Shell command surface

172 commands registered at boot (live count; `help` lists 172 unique and
prints `Total: 172 commands`, `help -w` groups them by work-package tag).
Section updated at WP-AUDIT-01-p0fix2: the earlier table (168 total, 170
live) used pre-restructure file names and predates WP-10-wp08fix1.
`shell_register_command` call sites (static count, verified 2026-10-05 by
per-file grep = 172 unique names, no duplicates; cross-checked against a
live `help -a` extraction in QEMU — both directions empty diff):
kernel/main.c 52 (incl. update/rollback/reboot), net/net_core.c 16,
shell/shell_cmds_file.c 18, shell/shell_cmds_disk.c 7,
drivers/block/driver_block_disk_setup.c 4 (rule-9 self-hosting:
abdisk/install/grub-install/abcfg), shell/shell_cmds_disk_test.c 8
(WP-10a storage test suite), shell/shell_cmds_nic_test.c 18 (WP-10b:
10 NIC tests + 8 NIC status commands), shell/shell_cmds_snd_test.c 17
(WP-10c sound status commands + tests), shell/shell_cmds_update_test.c 10
(WP-10u), shell/shell_cmds_usb_test.c 9 (WP-10d: usb/usbdev status +
7 test commands), shell/shell.c 6 (incl. WP-10-wp08fix1 nano/vi),
shell/shell_cmds_power_test.c 7 (WP-10d-fix2 power + help test suite).
52+16+18+7+4+8+18+17+10+9+6+7 = 172. Full list: type `help` at the `oc>`
prompt.

## 5. Verification of these interfaces

- L1 self-tests: `ext_selftest` (WP-01), l1test (WP-08cd), ext_wp8cd
  boot self-test — all PASS in the 18/18 regression.
- WP-09 transport E2E: HTTPS both sides, SSH both directions,
  K byte-level match.
