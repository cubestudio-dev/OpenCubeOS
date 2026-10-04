<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Interface Index (as of WP-10c)

Single-page index of every interface Open Cube OS exposes to upper layers
(L1) and to the shell. Signatures are copied verbatim from the headers —
see each header for full docs and examples.

Work-package overview: docs/EXTENSIONS.md (WP-01) through
docs/EXTENSIONS_WP09.md (WP-09), docs/EXTENSIONS_WP10a.md (WP-10a,
storage drivers: AHCI / NVMe / ATA DMA / virtio-blk) and
docs/EXTENSIONS_WP10b.md (WP-10b, NIC drivers: e1000e / igb / ixgbe /
RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx / legacy others).

## 1. L0 → L1 extension API (kernel/ext*.h)

### kernel/ext.h — WP-01 (framebuffer / renderer / font / console)

```c
const oc_fb_info_t* oc_ext_fb_get_info(void);
oc_renderer_t*      oc_ext_set_renderer(oc_renderer_t* new_renderer);
int   oc_ext_register_font_engine(oc_font_engine_t* engine);
oc_font_engine_t* oc_ext_font_engine_by_name(const char* name);
oc_font_engine_t* oc_ext_default_font_engine(void);
void  oc_ext_set_console_hook(oc_console_hook_fn hook, void* ctx);
void  oc_ext_console_hook(u8 ch);   /* called by L0 console */
int   oc_ext_self_test(void);
```

### kernel/ext_wp8a.h — WP-08a (processes / signals / syscalls / fd)

```c
proc_fork / proc_exec / proc_exit / proc_wait
proc_getpid / proc_getppid
signal_register / signal_return / signal_send
sys_brk / sys_mmap / sys_mprotect / sys_munmap
sys_select / sys_poll
sys_chdir / sys_getcwd / sys_ioctl
pipe_create / fd_dup / fd_dup2
```
(21 functions; exact signatures in the header.)

### kernel/ext_wp8b.h — WP-08b (dynamic linking)

```c
ldso_run
dlopen_impl / dlsym_impl / dlclose_impl
so_load / so_unload
symbol_resolve / reloc_apply
elf_load_dynamic / elf_get_needed
ext_wp8b_selftest
```
(11 functions.)

### kernel/ext_wp8cd.h — WP-08cd (user shell / tools / jobs)

```c
shell_run / shell_register_builtin
tool_register / tool_list
job_create / job_list / job_control
```
(7 functions.)

### kernel/ext_wp2.h, ext_wp3.h, ext_wp7.h

Type/macro-only headers (no callable functions) for WP-02/WP-03/WP-07
subsystem structures. The WP-02/03/05/06/07 "extension points" are exposed
as direct kernel APIs (see §3) rather than oc_ext_* functions.

## 2. WP-09 transport APIs (kernel/ssh.h, kernel/tls.h)

```c
/* SSH client (curve25519-sha256 / group14-sha256 KEX, aes128-ctr/cbc,
 * hmac-sha2-256, rsa-sha2-256/512 host key verified over H with TOFU
 * fingerprint, password + publickey auth, session exec) */
int  ssh_connect(u32 ip, u16 port, const char *username, const char *password);
int  ssh_exec(const char *command, void *output, int output_len);
void ssh_close(void);

/* SSH server (kernel/sshd.c): `sshd <port> <user> <password>` shell command;
 * serves one session then returns. exec requests are executed through the
 * kernel shell capture API (shell_execute_captured). */

/* TLS 1.3/1.2 client (TLS 1.3: X25519 + AES-GCM/ChaCha20-Poly1305;
 * TLS 1.2 fallback: ECDHE_RSA + AES-GCM/ChaCha20-Poly1305, legacy DHE-CBC
 * retained. X.509 chain + hostname verification against embedded public
 * CA roots — handshake fails closed on verification errors) */
int  tls_connect(u32 ip, u16 port, const char *hostname);
int  tls_send(tls_ctx_t *ctx, const void *data, int len);
int  tls_recv(tls_ctx_t *ctx, void *buf, int len);
void tls_close(tls_ctx_t *ctx);
int  tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                   void *out_buf, int out_len);
```

Crypto primitives: `kernel/crypto.h` (aes128 CTR/CBC, sha256, hmac_sha256,
hkdf, dh_modexp, crypto_random), `kernel/bn.h` (bignum modexp, Montgomery
core), `kernel/ec_nist.h` (P-256/P-384 ECDH+ECDSA), `kernel/curve25519.h`
(X25519), `kernel/rsa.h` (PKCS#1 v1.5 + PSS verify), `kernel/aead.h`
(AES-GCM, ChaCha20-Poly1305), `kernel/sha512.h` (SHA-512/384 + HMAC),
`kernel/x509.h` (certificate parse + chain/hostname verification).
DH truth vectors for self-test: kernel/dh_scale_vectors.h.

## 3. Kernel subsystem APIs (stable since their WP)

| Subsystem | Header | Key functions |
|---|---|---|
| Framebuffer | kernel/fb.h | oc_fb_* |
| Console | kernel/console.h | oc_console_* |
| Shell + command registration | kernel/shell.h | shell_register_command, shell_execute_captured (WP-09) |
| PMM / VMM / heap | kernel/pmm.h, vmm.h, heap.h | pmm_alloc_frame, vmm_*, kmalloc/kfree/krealloc |
| Scheduler + sync | kernel/sched.h, sync.h | sched_*, spinlock/mutex/sem/cond |
| User mode + syscalls | kernel/usermode.h, syscall.h | user_process_create, syscall dispatch (37 syscalls) |
| VFS + ramfs | kernel/vfs.h, ramfs.h | vfs_open/read/write/stat/... |
| Network (TCP/IP) | kernel/net.h | net_socket/connect/send/recv/accept/close, dns_resolve, cmd-level: dhcp/ping/wget/dns/route/arp/firewall/tcpstats |
| Block + FS | kernel/blk.h, part.h, fat32.h, exfat.h, ext4.h | blk_*, vfs mount (FAT32 R/W, exFAT R/W, ext4 RO) |
| Storage drivers (WP-10a) | kernel/ahci.h, nvme.h, ata_dma.h, virtio_blk.h | ahci_init(pci_dev), nvme_init(pci_dev), ata_dma_init(pci_dev), virtio_blk_init, pci_find_class_exact/mask (pci.h) |
| NIC drivers (WP-10b) | kernel/nic.h | nic_register(dev, ops), nic_send/recv(dev, ...), nic_link_status(dev), nic_get_mac(dev, mac), e1000e_init/igb_init/ixgbe_init/rtl8139_init/rtl8168_init/rtl8125_init/rtl810x_init/bcm57xx_init(pci_dev), other_nics_init, nic_probe_all, nic_active |
| Sound cards (WP-10c) | kernel/snd.h, usb.h | snd_register(dev, ops), snd_play/stop/set_rate/set_volume/get_caps(dev, ...), hda_init/ac97_init/es1370_init/virtio_snd_init(pci_dev), sb16_init(isa_dev), usb_audio_init(usb_dev), snd_probe_all; USB: usb_init, usb_enumerate, usb_control, usb_set_interface, usb_iso_out_submit (kernel/usb.h) |
| USB host stack (WP-10d) | kernel/usb.h | usb_register_host(host, ops), usb_enumerate_host(host), usb_control_transfer(d, setup, buf, len), usb_bulk_transfer(d, ep, buf, len) (+ _timeout), usb_interrupt_transfer(d, ep, buf, len), usb_isochronous_transfer(d, ep, buf, len), usb_register_driver(name, class, probe, disconnect), uhci_init / ohci_init / ehci_init / xhci_init(pci_dev); class drivers hid-kbd/hid-mouse/usb-msc/usb-serial/usb-audio; shell: usb, usbdev |
| TCP socket state (WP-10a-fix) | kernel/net.h | net_tcp_established(fd) |

## 4. Shell command surface

159 commands registered at boot (live count; `help` lists 159 unique).
`shell_register_command` call sites (static count): kmain.c 48 (incl.
update/rollback/reboot), net.c 17, file_cmds.c 18, disk_cmds.c 7,
disk_setup.c 4 (rule-9 self-hosting: abdisk/install/grub-install/abcfg),
disk_test_cmds.c 8 (WP-10a storage test suite), nic_test_cmds.c 18
(WP-10b: 10 NIC tests + 8 NIC status commands), snd_test_cmds.c 17
(WP-10c sound status commands + tests), update_test_cmds.c 10 (WP-10u),
usb_test_cmds.c 9 (WP-10d: usb/usbdev status + 7 test commands),
shell.c 5, ext_wp8cd.c 1 — some register the same name at different
stages, so the live boot count is authoritative. Full list: type `help`
at the `oc>` prompt.

## 5. Verification of these interfaces

- L1 self-tests: `ext_selftest` (WP-01), l1test (WP-08cd), ext_wp8cd
  boot self-test — all PASS in the 18/18 regression.
- WP-09 transport E2E: HTTPS both sides, SSH both directions,
  K byte-level match.
