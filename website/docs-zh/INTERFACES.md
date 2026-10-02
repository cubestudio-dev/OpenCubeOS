<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/INTERFACES.md (website-provided, for
     reading convenience). The English original in the repository is
     authoritative. Signatures and code blocks are kept verbatim. -->

# Open Cube OS — 接口索引（截至 WP-09）

Open Cube OS 向上层（L1）与 shell 暴露的每一个接口的单页索引。
签名逐字复制自头文件——完整文档与示例见各头文件。

工作包总览：docs/EXTENSIONS.md（WP-01）至
docs/EXTENSIONS_WP09.md（WP-09）。

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

## 4. Shell 命令面

68 条命令在 boot 时注册（自检计数；见 `shell_register_command` 调用点：
kmain.c 38、net.c 13、file_cmds.c 17、disk_cmds.c 7、shell.c 7、ext_wp8cd.c 1
——部分在不同阶段注册同名命令）。完整列表：在 `oc>` 提示符输入 `help`。

## 5. 这些接口的验证

- L1 自测：`ext_selftest`（WP-01）、l1test（WP-08cd）、ext_wp8cd
  boot 自测——在 18/18 回归中全部 PASS。
- WP-09 传输 E2E：HTTPS 双侧、SSH 双向、K 字节级一致。
