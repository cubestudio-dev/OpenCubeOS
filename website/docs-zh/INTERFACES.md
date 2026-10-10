<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/INTERFACES.md (website-provided, for
     reading convenience). The English original in the repository is
     authoritative. Signatures and code blocks are kept verbatim.
     Structurally re-aligned with the current English original at
     WP-10-AUDIT_P2-fix3 (fix3 G5, BUG-0270). -->

# Open Cube OS — 接口索引

接口编号 1-138 于 WP-10-wp08fix1 收口；计数于 WP-10-AUDIT_P2-fix3
重新核验（2026-10-09）。

Open Cube OS 向上层（L1）与 shell 暴露的每一个接口的单页索引。
签名逐字复制自头文件——完整文档与示例见各头文件。

工作包总览：docs/EXTENSIONS.md（WP-01）至
docs/EXTENSIONS_WP09.md（WP-09）、docs/EXTENSIONS_WP10a.md（WP-10a，
存储驱动：AHCI / NVMe / ATA DMA / virtio-blk）与
docs/EXTENSIONS_WP10b.md（WP-10b，网卡驱动：e1000e / igb / ixgbe /
RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx / legacy 其他）。

## 1. L0 → L1 扩展 API（l1/*.h + 各域 driver 头文件）

### l1/l1_ext.h — WP-01（framebuffer / 渲染器 / 字体 / 控制台）

```c
const screen_fb_info_t* l1_ext_fb_get_info(void);
screen_renderer_t*      l1_ext_set_renderer(screen_renderer_t* new_renderer);
int   l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);
void  l1_ext_set_console_hook(screen_console_hook_fn hook, void* ctx);
void  l1_ext_console_hook(u8 ch);   /* 由 L0 控制台调用 */
int   l1_ext_self_test(void);
```

### l1/l1_wp8a.h — WP-08a（进程 / 信号 / 系统调用 / fd）

```c
sys_proc_fork / sys_proc_exec / sys_proc_exit / sys_proc_wait
sys_proc_getpid / sys_proc_getppid
signal_register / signal_return / signal_send
sys_mem_brk / sys_mem_mmap / sys_mem_mprotect / sys_mem_munmap
sys_select / sys_poll
sys_chdir / sys_getcwd / sys_ioctl
pipe_create / fd_dup / fd_dup2
```
（21 个函数；精确签名见头文件。）

### l1/l1_wp8b.h — WP-08b（动态链接）

```c
ldso_run
dlopen_impl / dlsym_impl / dlclose_impl
so_load / so_unload
symbol_resolve / reloc_apply
elf_load_dynamic / elf_get_needed
ext_wp8b_selftest
```
（11 个函数。）

### l1/l1_wp8cd.h — WP-08cd（用户 shell / 工具 / 作业）

```c
shell_run / shell_register_builtin
tool_register / tool_list
job_create / job_list / job_control
```
（7 个函数。）

### shell/shell_lineedit.h + shell/editor.h — WP-10-wp08fix1（第 130-138 项）

```c
/* 内核侧 oc> 行编辑器： */
void        shell_lineedit_init(void);                     /* item 130 */
void        shell_lineedit_history_add(const char *cmd);   /* item 131 */
const char *shell_lineedit_history_get(int idx);           /* item 132 */
void        shell_lineedit_cursor_move(int dir);           /* item 133 */
int         shell_lineedit_tab_complete(void);             /* item 134 */
int         shell_lineedit_ctrlc(void);                    /* item 135 */
/* nano 风格全屏编辑器： */
int         editor_open(const char *file);                 /* item 136 */
int         editor_save(void);                             /* item 137 */
int         editor_close(void);                            /* item 138 */
```
（9 个编号项——接口编号 1-138 在此收口。签名逐字取自
docs/EXTENSIONS_WP10-wp08fix1.md；同一批次还加入了
VFS link/symlink + chmod/chown 接口面与 SYS 96-102。）

### l1/l1_wp2.h、l1/l1_wp3.h、l1/l1_wp7.h

WP-02/WP-03/WP-07 子系统结构的类型/宏头文件（无可调用函数）。
WP-02/03/05/06/07 的"扩展点"以直接内核 API 形式暴露（见 §3），
而非 l1_ext_* 函数。

## 2. WP-09 传输 API（net/net_ssh.h、net/net_tls.h）

```c
/* SSH 客户端（curve25519-sha256 / group14-sha256 KEX、aes128-ctr/cbc、
 * hmac-sha2-256、rsa-sha2-256/512 主机密钥在 H 上验证并显示 TOFU 指纹、
 * 密码 + 公钥认证、session exec） */
int  net_ssh_connect(u32 ip, u16 port, const char *username, const char *password);
int  net_ssh_exec(const char *command, void *output, int output_len);
void net_ssh_close(void);

/* SSH 服务端（net/net_sshd.c）：`sshd <port> <user> <password>` shell 命令；
 * 服务一个会话后返回。exec 请求经内核 shell 捕获 API
 * （shell_execute_captured）执行。 */

/* TLS 1.3/1.2 客户端（TLS 1.3：X25519 + AES-GCM/ChaCha20-Poly1305；
 * TLS 1.2 回退：ECDHE_RSA + AES-GCM/ChaCha20-Poly1305，旧式 DHE-CBC 保留。
 * X.509 链 + 主机名验证（内嵌公共 CA 根）——验证失败即握手失败） */
int  net_tls_connect(u32 ip, u16 port, const char *hostname);
int  net_tls_send(net_tls_ctx_t *ctx, const void *data, int len);
int  net_tls_recv(net_tls_ctx_t *ctx, void *buf, int len);
void net_tls_close(net_tls_ctx_t *ctx);
int  net_tls_https_get(u32 ip, u16 port, const char *hostname, const char *path,
                   void *out_buf, int out_len);
```

Crypto 原语：`kernel/crypto/crypto_core.h`（aes128 CTR/CBC、sha256、crypto_hmac_sha256、
hkdf、crypto_dh_modexp、crypto_random）、`kernel/crypto/crypto_bn.h`（大整数 modexp、Montgomery
核心）、`kernel/crypto/crypto_ec_nist.h`（P-256/P-384 ECDH+ECDSA）、
`kernel/crypto/crypto_curve25519.h`（X25519）、`kernel/crypto/crypto_rsa.h`
（PKCS#1 v1.5 + PSS 验证）、`kernel/crypto/crypto_aead.h`
（AES-GCM、ChaCha20-Poly1305）、`kernel/crypto/crypto_sha512.h`（SHA-512/384 + HMAC）、
`kernel/crypto/crypto_x509.h`（证书解析 + 链/主机名验证）。
DH 自测真值向量：kernel/crypto/crypto_dh_scale_vectors.h。

## 3. 内核子系统 API（自各自 WP 起稳定）

| 子系统 | 头文件 | 关键函数 |
|---|---|---|
| Framebuffer | drivers/display/screen_fb.h | screen_fb_* |
| 控制台 | drivers/display/screen_console.h | screen_console_* |
| Shell + 命令注册 | shell/shell.h | shell_register_command、shell_execute_captured（WP-09） |
| PMM / VMM / 堆 | kernel/mem/mem_pmm.h、vmm.h、heap.h | mem_pmm_alloc_frame、mem_vmm_*、kmalloc/kfree/krealloc |
| 调度器 + 同步 | kernel/core/core_sched.h、sync.h | core_sched_*、spinlock/mutex/sem/cond |
| 用户态 + 系统调用 | kernel/core/core_usermode.h、syscall.h | user_process_create、syscall 分发（46 个系统调用，SYS 0-102） |
| VFS + ramfs | fs/fs_vfs.h、ramfs.h | fs_vfs_open/read/write/stat/... |
| 网络（TCP/IP） | net/net_core.h | net_socket/connect/send/recv/accept/close、net_dns_resolve；命令级：dhcp/ping/wget/dns/route/arp/firewall/tcpstats |
| 块 + 文件系统 | drivers/block/driver_block_blk.h、part.h、fat32.h、exfat.h、ext4.h | driver_block_*、vfs 挂载（FAT32 R/W、exFAT R/W、ext4 RO） |
| 存储驱动（WP-10a） | drivers/block/driver_block_ahci.h、nvme.h、driver_block_ata_dma.h、virtio_blk.h | driver_block_ahci_init(driver_pci_dev)、driver_block_nvme_init(driver_pci_dev)、driver_block_ata_dma_init(driver_pci_dev)、driver_block_virtio_blk_init、driver_pci_find_class_exact/mask（pci.h） |
| 网卡驱动（WP-10b） | drivers/nic/driver_nic.h | driver_nic_register(dev, ops)、driver_nic_send/recv(dev, ...)、driver_nic_link_status(dev)、driver_nic_get_mac(dev, mac)、driver_nic_e1000e_init/driver_nic_igb_init/driver_nic_ixgbe_init/driver_nic_rtl8139_init/rtl8168_init/rtl8125_init/rtl810x_init/bcm57xx_init(driver_pci_dev)、other_nics_init、driver_nic_probe_all、driver_nic_active |
| 声卡驱动（WP-10c） | drivers/snd/driver_snd.h、usb.h | driver_snd_register(dev, ops)、driver_snd_play/stop/set_rate/set_volume/get_caps(dev, ...)、driver_snd_hda_init/driver_snd_ac97_init/driver_snd_es1370_init/driver_snd_virtio_init(driver_pci_dev)、driver_snd_sb16_init(isa_dev)、driver_usb_audio_init(driver_usb_dev)、driver_snd_probe_all；USB：driver_usb_init、driver_usb_enumerate、driver_usb_control、driver_usb_set_interface、driver_usb_iso_out_submit（drivers/usb/driver_usb.h） |
| USB 主机栈（WP-10d） | drivers/usb/driver_usb.h | driver_usb_register_host(host, ops)、driver_usb_enumerate_host(host)、driver_usb_control_transfer(d, setup, buf, len)、driver_usb_bulk_transfer(d, ep, buf, len)（含 _timeout）、driver_usb_interrupt_transfer(d, ep, buf, len)、driver_usb_isochronous_transfer(d, ep, buf, len)、driver_usb_register_driver(name, class, probe, disconnect)、driver_usb_uhci_init / driver_usb_ohci_init / driver_usb_ehci_init / driver_usb_xhci_init(driver_pci_dev)；类驱动 hid-kbd/hid-mouse/usb-msc/usb-serial/usb-audio；命令：usb、usbdev |
| 电源管理 + 结构化 help（WP-10d-fix2） | kernel/core/core_power.h、shell/shell.h | core_power_shutdown(void)、core_power_suspend(void)、core_power_halt(void)、core_power_reboot(void)（kernel/core/core_power.h）；shell_register_command_ex(name, fn, help, wp)、shell_list_commands_a_z(void)、shell_list_commands_by_wp(void)（shell/shell.h）；L1 包装 l1_ext_power_* / l1_ext_shell_*（l1/l1_ext.h） |
| TCP socket 状态（WP-10a-fix） | net/net_core.h | net_tcp_established(fd)、net_tcp_established_or_close_wait(fd)（WP-10-AUDIT_P2-fix2：连接处于 ESTABLISHED 或 CLOSE_WAIT（即发送 close_notify 告警仍合法）时返回 1） |
| 配置 + 检查更新（WP-09-fix5，不入编号） | l1/l1_ext.h、kernel/config.h | l1_ext_config_read(key, val_out, outlen)、l1_ext_config_write(key, value)、l1_ext_config_get_all(buf, buflen)（/etc/opencube.conf）、l1_ext_check_update(&info)、l1_ext_check_update_async(void)；见 docs/CONFIG.md |
| 第 ⑨ 条自宿主（WP-10c-selfhost，不入编号） | drivers/block/driver_block_part.h、kernel/ota/ota_ab.h、kernel/arch/x86_64/arch_multiboot2.h | driver_block_part_register_child(...)、driver_block_part_write_mbr_table(...)、driver_block_part_scan_register_all(void)、ota_ab_rescan(void)、arch_multiboot2_get_kernel_self(&data, &size)；命令：abdisk / install / grub-install / abcfg——docs/EXTENSIONS_SELFHOST.md |

## 4. Shell 命令面

boot 时注册 176 条命令（实测计数；`help` 列出 176 个不重复名并打印
`Total: 176 commands`，`help -w` 按工作包 tag 分组）。
本节更新于 WP-10-AUDIT_P2-fix2：fix1 新增三个回归命令
（`irqabitest`、`heapbounds`、`vmkernelpt`，注册于
kernel/main.c 2936-2938，BUG-0136/0140/0141 归因），boot 计数由
173 → 176；更早的 173 表先于这三条，已作废（fix1 轮 finding #3）。
`shell_register_command` 调用点（静态计数，2026-10-08 按文件 grep
重新核验 = 176 个不重复名、无重复；每次发布需与 QEMU 里 live 的
`help -a` 抽取交叉核对）：
kernel/main.c 56（含 update/rollback/reboot、pmmrace 与三条
fix1 回归命令），
net/net_core.c 16，
shell/shell_cmds_file.c 18、shell/shell_cmds_disk.c 7，
drivers/block/driver_block_disk_setup.c 4（第 ⑨ 条自宿主：
abdisk/install/grub-install/abcfg）、shell/shell_cmds_disk_test.c 8
（WP-10a 存储测试套件）、shell/shell_cmds_nic_test.c 18（WP-10b：
10 个 NIC 测试 + 8 个 NIC 状态命令）、shell/shell_cmds_snd_test.c 17
（WP-10c 声卡状态命令 + 测试）、shell/shell_cmds_update_test.c 10
（WP-10u）、shell/shell_cmds_usb_test.c 9（WP-10d：usb/usbdev 状态 +
7 个测试命令）、shell/shell.c 6（含 WP-10-wp08fix1 nano/vi）、
shell/shell_cmds_power_test.c 7（WP-10d-fix2 电源 + help 测试套件）。
56+16+18+7+4+8+18+17+10+9+6+7 = 176。完整列表：在 `oc>`
提示符输入 `help`。

## 5. 这些接口的验证

- L1 自测：`ext_selftest`（WP-01）、l1test（WP-08cd）、ext_wp8cd
  boot 自测——在 18/18 回归中全部 PASS。
- WP-09 传输 E2E：HTTPS 双侧、SSH 双向、K 字节级一致。
