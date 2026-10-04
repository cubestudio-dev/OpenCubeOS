<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/INTERFACES.md (website-provided, for
     reading convenience). The English original in the repository is
     authoritative. Signatures and code blocks are kept verbatim. -->

# Open Cube OS — 接口索引（截至 WP-10d）

Open Cube OS 向上层（L1）与 shell 暴露的每一个接口的单页索引。
签名逐字复制自头文件——完整文档与示例见各头文件。

工作包总览：docs/EXTENSIONS.md（WP-01）至
docs/EXTENSIONS_WP09.md（WP-09）、docs/EXTENSIONS_WP10a.md（WP-10a，
存储驱动：AHCI / NVMe / ATA DMA / virtio-blk）与
docs/EXTENSIONS_WP10u.md（WP-10u，系统内自动更新：update_check_pkg / update_download /
 *   update_verify / update_install / update_rollback / update_set_boot /
 *   update_get_status 七项接口）、docs/EXTENSIONS_WP10b.md（WP-10b，网卡驱动：e1000e / igb / ixgbe /
RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx / legacy 其他）。

## 1. L0 → L1 扩展 API（kernel/ext*.h）

### kernel/ext.h — WP-01（framebuffer / 渲染器 / 字体 / 控制台）

```c
const oc_fb_info_t* oc_ext_fb_get_info(void);
oc_renderer_t*      oc_ext_set_renderer(oc_renderer_t* new_renderer);
int   oc_ext_register_font_engine(oc_font_engine_t* engine);
oc_font_engine_t* oc_ext_font_engine_by_name(const char* name);
oc_font_engine_t* oc_ext_default_font_engine(void);
void  oc_ext_set_console_hook(oc_console_hook_fn hook, void* ctx);
void  oc_ext_console_hook(u8 ch);   /* 由 L0 控制台调用 */
int   oc_ext_self_test(void);
```

### kernel/ext_wp8a.h — WP-08a（进程 / 信号 / 系统调用 / fd）

```c
proc_fork / proc_exec / proc_exit / proc_wait
proc_getpid / proc_getppid
signal_register / signal_return / signal_send
sys_brk / sys_mmap / sys_mprotect / sys_munmap
sys_select / sys_poll
sys_chdir / sys_getcwd / sys_ioctl
pipe_create / fd_dup / fd_dup2
```
（21 个函数；精确签名见头文件。）

### kernel/ext_wp8b.h — WP-08b（动态链接）

```c
ldso_run
dlopen_impl / dlsym_impl / dlclose_impl
so_load / so_unload
symbol_resolve / reloc_apply
elf_load_dynamic / elf_get_needed
ext_wp8b_selftest
```
（11 个函数。）

### kernel/ext_wp8cd.h — WP-08cd（用户 shell / 工具 / 作业）

```c
shell_run / shell_register_builtin
tool_register / tool_list
job_create / job_list / job_control
```
（7 个函数。）

### kernel/ext_wp2.h、ext_wp3.h、ext_wp7.h

WP-02/WP-03/WP-07 子系统结构的类型/宏头文件（无可调用函数）。
WP-02/03/05/06/07 的"扩展点"以直接内核 API 形式暴露（见 §3），
而非 oc_ext_* 函数。

## 2. WP-09 传输 API（kernel/ssh.h、kernel/tls.h）

```c
/* SSH 客户端（curve25519-sha256 / group14-sha256 KEX、aes128-ctr/cbc、
 * hmac-sha2-256、rsa-sha2-256/512 主机密钥在 H 上验证并显示 TOFU 指纹、
 * 密码 + 公钥认证、session exec） */
int  ssh_connect(u32 ip, u16 port, const char *username, const char *password);
int  ssh_exec(const char *command, void *output, int output_len);
void ssh_close(void);

/* SSH 服务端（kernel/sshd.c）：`sshd <port> <user> <password>` shell 命令；
 * 服务一个会话后返回。exec 请求经内核 shell 捕获 API
 * （shell_execute_captured）执行。 */

/* TLS 1.3/1.2 客户端（TLS 1.3：X25519 + AES-GCM/ChaCha20-Poly1305；
 * TLS 1.2 回退：ECDHE_RSA + AES-GCM/ChaCha20-Poly1305，旧式 DHE-CBC 保留。
 * X.509 链 + 主机名验证（内嵌公共 CA 根）——验证失败即握手失败） */
int  tls_connect(u32 ip, u16 port, const char *hostname);
int  tls_send(tls_ctx_t *ctx, const void *data, int len);
int  tls_recv(tls_ctx_t *ctx, void *buf, int len);
void tls_close(tls_ctx_t *ctx);
int  tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                   void *out_buf, int out_len);
```

Crypto 原语：`kernel/crypto.h`（aes128 CTR/CBC、sha256、hmac_sha256、
hkdf、dh_modexp、crypto_random）、`kernel/bn.h`（大整数 modexp，Montgomery 核心）、
`kernel/ec_nist.h`（P-256/P-384 ECDH+ECDSA）、`kernel/curve25519.h`（X25519）、
`kernel/rsa.h`（PKCS#1 v1.5 + PSS 验证）、`kernel/aead.h`（AES-GCM、ChaCha20-Poly1305）、
`kernel/sha512.h`（SHA-512/384 + HMAC）、`kernel/x509.h`（证书解析 + 链/主机名验证）。
DH 自测真值向量：kernel/dh_scale_vectors.h。

## 3. 内核子系统 API（自各自 WP 起稳定）

| 子系统 | 头文件 | 关键函数 |
|---|---|---|
| Framebuffer | kernel/fb.h | oc_fb_* |
| 控制台 | kernel/console.h | oc_console_* |
| Shell + 命令注册 | kernel/shell.h | shell_register_command、shell_execute_captured（WP-09） |
| PMM / VMM / 堆 | kernel/pmm.h、vmm.h、heap.h | pmm_alloc_frame、vmm_*、kmalloc/kfree/krealloc |
| 调度器 + 同步 | kernel/sched.h、sync.h | sched_*、spinlock/mutex/sem/cond |
| 用户态 + 系统调用 | kernel/usermode.h、syscall.h | user_process_create、syscall 分发（37 个系统调用） |
| VFS + ramfs | kernel/vfs.h、ramfs.h | vfs_open/read/write/stat/... |
| 网络（TCP/IP） | kernel/net.h | net_socket/connect/send/recv/accept/close、dns_resolve；命令级：dhcp/ping/wget/dns/route/arp/firewall/tcpstats |
| 块 + 文件系统 | kernel/blk.h、part.h、fat32.h、exfat.h、ext4.h | blk_*、vfs 挂载（FAT32 R/W、exFAT R/W、ext4 RO） |
| 存储驱动（WP-10a） | kernel/ahci.h、nvme.h、ata_dma.h、virtio_blk.h | ahci_init(pci_dev)、nvme_init(pci_dev)、ata_dma_init(pci_dev)、virtio_blk_init、pci_find_class_exact/mask（pci.h） |
| 系统内更新（WP-10u） | kernel/ab_update.h | oc_ext_update_check_pkg(out)、oc_ext_update_download(url, path)、oc_ext_update_verify(path, sha256)、oc_ext_update_install(pkg, slot)、oc_ext_update_rollback()、oc_ext_update_set_boot(slot)、oc_ext_update_get_status(out) |
| 网卡驱动（WP-10b） | kernel/nic.h | nic_register(dev, ops)、nic_send/recv(dev, ...)、nic_link_status(dev)、nic_get_mac(dev, mac)、e1000e_init/igb_init/ixgbe_init/rtl8139_init/rtl8168_init/rtl8125_init/rtl810x_init/bcm57xx_init(pci_dev)、other_nics_init、nic_probe_all、nic_active |
| 声卡驱动（WP-10c） | kernel/snd.h、usb.h | snd_register(dev, ops)、snd_play/stop/set_rate/set_volume/get_caps(dev, ...)、hda_init/ac97_init/es1370_init/virtio_snd_init(pci_dev)、sb16_init(isa_dev)、usb_audio_init(usb_dev)、snd_probe_all；USB：usb_init、usb_enumerate、usb_control、usb_set_interface、usb_iso_out_submit（kernel/usb.h） |
| USB 主机栈（WP-10d） | kernel/usb.h | usb_register_host(host, ops)、usb_enumerate_host(host)、usb_control_transfer(d, setup, buf, len)、usb_bulk_transfer(d, ep, buf, len)（含 _timeout）、usb_interrupt_transfer(d, ep, buf, len)、usb_isochronous_transfer(d, ep, buf, len)、usb_register_driver(name, class, probe, disconnect)、uhci_init / ohci_init / ehci_init / xhci_init(pci_dev)；类驱动 hid-kbd/hid-mouse/usb-msc/usb-serial/usb-audio；命令：usb、usbdev |
| TCP socket 状态（WP-10a-fix） | kernel/net.h | net_tcp_established(fd) |

## 4. Shell 命令面

159 条命令在 boot 时注册（实测计数；`help` 列出 159 个不重复名）。
`shell_register_command` 调用点（静态计数）：kmain.c 48（含 update/rollback/reboot）、
net.c 17、file_cmds.c 18、disk_cmds.c 7、
disk_setup.c 4（第 ⑨ 条自宿主：abdisk/install/grub-install/abcfg）、
disk_test_cmds.c 8（WP-10a 存储测试套件）、
nic_test_cmds.c 18（WP-10b：10 个 NIC 测试 + 8 个 NIC 状态命令）、
snd_test_cmds.c 17（WP-10c：声卡状态命令 + 声卡测试）、
update_test_cmds.c 10（WP-10u：update_pkg/ab_partition/update_check/update_download/update_verify/update_install/update_rollback/update_local/update_status/real_update）、
usb_test_cmds.c 9（WP-10d：usb/usbdev 状态命令 + 7 个测试命令）、
shell.c 5、ext_wp8cd.c 1——部分在不同阶段注册同名命令，以 boot 实测计数为准。
完整列表：在 `oc>` 提示符输入 `help`。

## 5. 这些接口的验证

- L1 自测：`ext_selftest`（WP-01）、l1test（WP-08cd）、ext_wp8cd
  boot 自测——在 18/18 回归中全部 PASS。
- WP-09 传输 E2E：HTTPS 双侧、SSH 双向、K 字节级一致。
