<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of README.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, paths, links and identifiers are kept verbatim. -->

# Open Cube OS - WP-09

**官网**：https://cubestudio-dev.github.io/OpenCubeOS
**GitHub**：https://github.com/cubestudio-dev/OpenCubeOS
**Releases**：https://github.com/cubestudio-dev/OpenCubeOS/releases

Copyright 2026 cubestudio-dev <cubestudio@qq.com>
基于 Apache License 2.0 许可。

**Open Cube OS** 是一个开源操作系统内核。它的定位：

- **它不是"一个能日常使用的系统"。** 它是"一个能被扩展成任何东西的内核"。
- 它的价值不在自带什么，而在向上层暴露的接口。
- 架构分两层：
  - **L0** = Open Cube OS = 完整内核。
  - **L1** = 上层扩展，构建在 L0 的扩展接口之上。L0 不内置任何 L1。
- L0 采用 Apache 2.0 许可。
- 设计原则："一切皆可扩展"。

## 统计（WP-09）

- **源码**：46058 行（kernel + boot + userprogs，不含文档）
- **工作包**：9 个（WP-01 ~ WP-09）
- **L1 扩展接口**：57 个（WP-09 新增的是传输层能力，而非 L1 接口）
- **系统调用**：37 个
- **审计 bug 修复**：原始 WP-08 审计修复 47 个（P0=2、P1=8、P2=29、P3=8）
  + 后续独立审计与 P2 批量收尾（P2-BATCH-1 + P2-BATCH-2）追加修复
  4 个 P0 + 8 个 P1 + 20 个 P2，累计达 79。
  + 15 个 GitHub-AI P3 bug（WP-08-p3：安全 + 内存 + 系统调用 + 信号 + ELF + 管道）
  + 26 个 P4 bug（WP-08-p4：文档修复 + Makefile + ld.so 输出 + shell 管道 +
  idle 对齐 + kill/nice 溢出 + pmm/pftest + execve argv/envp +
  kthread_destroy 同步钩子 + 崩溃日志 + VFS 杂项）
  总计：修复 120 个 bug（截至 WP-08；WP-09 又完成 SSH/TLS 专项修复，
  见 docs/VERIFICATION_BATCH_B.md）。
- **测试通过**：18/18 QEMU 全量回归（WP-09 基准套件——boot 横幅 + uname +
  12 个用户程序 + p3_test + heaptest + l1test + crashlog），另有 dhtest 5/5、
  HTTPS E2E 与 SSH 与 paramiko 双向互操作。完整证据：docs/VERIFICATION_BATCH_B.md
  + docs/verification/。

## WP-01（完成）- 引导 + framebuffer + 文本渲染

- 经 GRUB multiboot2 实现 BIOS + UEFI 双引导。
- 64-bit long mode，4 GiB 恒等映射。
- 800x600x32 RGB 帧缓冲。
- 8x16 位图字体、字符网格控制台，带光标 + 滚动。
- 首批 4 个 L0→L1 扩展接口（fb 访问、渲染器替换、字体引擎、控制台钩子）。

## WP-02（完成）- 中断 + 定时器 + 键盘

- IDT + GDT + TSS：256 项 IDT，#DF/#MC 使用 IST 栈。
- 8259 PIC remap：IRQ0-15 → 向量 32-47。
- CPU 异常处理：#DE/#UD/#PF/#GP/#DF，带诊断转储 + L1 处理链。
- PIT @ 100 Hz：真实系统滴答计数、真实毫秒时间戳。
- PS/2 键盘 + COM1 串口输入 → 统一键盘队列。
- 控制台输入行编辑器 + 交互式 `oc>` 提示符。
- 新增 4 个 L0→L1 扩展接口（IRQ、定时器、键盘、异常）。

## WP-03（完成）- 物理内存 + 虚拟内存 + 内核堆

- **PMM**：4KB 页帧位图分配器，解析 multiboot2 mmap，保留内核/帧缓冲/mbi 区域，支持紧急回调。
- **VMM**：4 级页表，创建/销毁地址空间，映射/解除映射/保护页，真实页错误处理（栈增长、堆增长、非法访问检测）。
- **堆**：free-list 分配器，first-fit + 合并 + 双重释放检测，kmalloc/kzalloc/kfree/krealloc。
- **Shell**：shell 命令注册 API。
- 新增 4 个 L0→L1 扩展接口：PMM、VMM、堆、shell 命令注册。

## WP-04（完成）- 调度器 + 同步原语 + 用户态

- 抢占式调度器：优先级 + 轮转，32 个优先级，每任务时间片。
- 同步原语：spinlock（含 CLI/STI）、semaphore、mutex（无优先级继承）、condvar。
- Ring 3 用户态：TSS RSP0 切换、用户态页错误处理、ELF 装载器（静态、PIE）。
- 新增 3 个 L0→L1 扩展接口：调度器、同步、用户态启动器。

## WP-05（完成）- Shell 增强 + 文件系统

- Shell：环境变量、别名、cwd、引号处理（无命令历史）。
- VFS：虚拟文件系统，mount/unmount、open/close/read/write/stat/readdir/mkdir/rmdir/unlink。
- RamFS：内存文件系统，挂载为根。
- Shell 文件命令：ls、cd、pwd、cat、mkdir、rm、mv、cp、touch、stat。
- 新增 4 个 L0→L1 扩展接口：环境变量、别名、VFS 挂载、文件操作。

## WP-06（完成）- 网络协议栈

- e1000 网卡驱动（PCI bus master、RX/TX 描述符）。
- ARP + IPv4 + ICMP + TCP + UDP。
- Socket API：socket、bind、listen、accept、connect、send、recv、close。
- Shell 命令：ifconfig、dhcp、ping、wget、dns、netstat。
- 新增 5 个 L0→L1 扩展接口：网卡、ARP、IP、TCP、socket。

## WP-07（完成）- 磁盘子系统

- ATA/IDE PIO 驱动（LBA28）。
- virtio-blk 驱动（modern PCI）。
- NVMe 驱动（admin 队列 + IO 队列）。
- 块缓存：64 槽 LRU 回写。
- 分区解析：MBR。
- FAT32（读写）、exFAT（读写）、ext4（只读）驱动。
- Shell 磁盘命令：lsblk、mount、umount、mkfs、fsck、fatmount。
- 新增 6 个 L0→L1 扩展接口：块设备、分区、文件系统、挂载、缓存、文件系统驱动。

## WP-08（完成）- 完整 syscall + 动态链接 + 用户 shell + 工具 + 审计修复

WP-08 统一了此前分开的 WP-08a / WP-08b / WP-08cd 子包：

- **完整系统调用集**（37 个）：fork、exec、wait、kill、signal、mmap、munmap、mprotect、brk、pipe、dup、dup2、sigaction、sigreturn、select、poll、chdir、getcwd、ioctl、getpid、getppid、exit、write、write_and_exit、open、close、stat、readdir、mkdir、rmdir、unlink、exit2、map_solib、read、write2、readline、getc。
- **动态链接**：ld.so（用户态），5 种重定位类型（R_X86_64_64、R_X86_64_RELATIVE、R_X86_64_GLOB_DAT、R_X86_64_JUMP_SLOT、R_X86_64_COPY），dlopen/dlsym/dlclose、ldd。
- **用户态 Shell（ush）**：20 个内建工具、Tab 补全、作业控制（`&`）、信号（Ctrl+C → SIGINT）、重定向（`>`）、管道（`|`）。
- **15 个用户态测试程序**：hello、badapp、loop、fork_test、exec_test、pipe_test、mmap_test、signal_test、select_test、dyn_hello、so_test、dyn_test（别名）、dlsym_test、pie_test、reloc_test、mmap_multi。
- **审计修复**：修复 47 个 bug（P0=2、P1=8、P2=29、P3=8）。
- 新增 7 个 L1 扩展接口（第 51-57 项）：shell_run、shell_register_builtin、tool_register、tool_list、job_create、job_list、job_control。

## WP-09（完成）- 安全传输：SSH（客户端 + 服务端）、TLS 1.2 / HTTPS、crypto 核心

- **Crypto 核心**（`kernel/crypto.{c,h}`）：AES-128（加/解密）、SHA-256、HMAC-SHA256、
  任意长度 DH modexp（以 python3 pow() 真值向量在 8/16/32/64/128/256 字节尺度验证）、crypto_random。
- **SSH 客户端**（`kernel/ssh.{c,h}`）：KEX group14-sha256（2048 位）、aes128-cbc、
  hmac-sha2-256、rsa-sha2-256 主机密钥签名、密码认证、session channel exec。
  K 与 paramiko 服务端抓包字节级一致。
- **SSH 服务端**（`kernel/sshd.c`）：同一算法套件，密码认证（oc/oc），
  exec 请求经内核 shell 捕获 API 执行；与 paramiko 客户端互操作验证 4/4。
- **TLS 1.2 客户端**（`kernel/tls.{c,h}`）：cipher DHE_RSA_WITH_AES_128_CBC_SHA256
  （0x0067），RFC 3526 1024-bit MODP 密钥交换，完整记录层（出站加密、入站解密 + MAC 校验），
  服务器 Finished 按设计接受。
- **HTTPS**：`wget https://host:port/path` 经 TLS 下载，直接落入 VFS。
- **网络运维命令**：route、arp、firewall、tcpstats、dns。
- **Shell**：68 条命令（boot 自检计数）。
- **新用户测试程序**：mprotect_test、p3_test。
- **验证**：18/18 QEMU 回归 + dhtest 5/5 + HTTPS E2E + SSH 双向互操作
  （外部证据：paramiko 5.0）。见 docs/VERIFICATION_BATCH_B.md、docs/EXTENSIONS_WP09.md、docs/INTERFACES.md。

## 仓库结构

```
oc-os/
+-- boot/                       # 汇编引导桩（3 个 .S 文件，WP-01）
|   +-- multiboot2_header.S
|   +-- boot.S
|   +-- long_mode_init.S
+-- kernel/                     # C 内核（94 个文件：.c + .h + .S）
|   +-- types.h, string.{c,h}, multiboot2.{c,h}    # WP-01 基础
|   +-- fb.{c,h}, font.{c,h}, font_data.c          # WP-01 framebuffer + 字体
|   +-- console.{c,h}, ext.{c,h}, ext_selftest.c   # WP-01 控制台 + 扩展
|   +-- log.{c,h}                                   # WP-02：真实时间戳
|   +-- idt.{c,h}, idt_stub.S, idt_load.S          # WP-02：IDT/GDT/TSS
|   +-- pic.h, exceptions.{c,h}, irq.{c,h}         # WP-02：PIC + 异常
|   +-- timer.{c,h}, keyboard.{c,h}                # WP-02：PIT + 键盘
|   +-- serial_in.{c,h}, console_in.{c,h}          # WP-02：COM1 RX + 行编辑器
|   +-- pmm.{c,h}, vmm.{c,h}, heap.{c,h}           # WP-03：内存管理
|   +-- shell.{c,h}                                 # WP-03：shell + 命令注册
|   +-- sched.{c,h}, sync.{c,h}                     # WP-04：调度器 + 同步
|   +-- usermode.{c,h}, enter_ring3_fork.S         # WP-04：用户态 + fork
|   +-- context_switch.S                            # WP-04：上下文切换
|   +-- vfs.{c,h}, ramfs.{c,h}                     # WP-05：VFS + ramfs
|   +-- file_cmds.{c,h}, shell_cmds               # WP-05：文件命令
|   +-- net.{c,h}                                   # WP-06：TCP/IP 协议栈
|   +-- ata.{c,h}, virtio_blk.{c,h}, nvme.{c,h}    # WP-07：磁盘驱动
|   +-- blk.{c,h}, blk_cache.{c,h}, part.{c,h}     # WP-07：块层 + 分区
|   +-- fat32.{c,h}, exfat.{c,h}, ext4.{c,h}       # WP-07：文件系统
|   +-- disk_cmds.{c,h}                             # WP-07：磁盘命令
|   +-- syscall.{c,h}                               # WP-08：syscall 分发
|   +-- ext_wp8a.{c,h}, ext_wp8b.{c,h}, ext_wp8cd.{c,h}  # WP-08 L1 扩展
|   +-- userprogs_data.h, solib_data.h             # WP-08 内嵌 ELF + .so 数据
|   +-- crypto.{c,h}, dh_scale_vectors.h           # WP-09：AES/SHA/HMAC/DH
|   +-- ssh.{c,h}, sshd.c, sshd_rsa_key.h          # WP-09：SSH 客户端 + 服务端
|   +-- tls.{c,h}                                   # WP-09：TLS 1.2 客户端
|   +-- kmain.c                                     # 内核主入口
+-- userprogs/                  # 用户态程序（21 个文件：.c + .asm + .ld）
|   +-- hello.asm, badapp.asm, loop.asm            # 基础测试
|   +-- fork_test.asm, exec_test.asm               # 进程测试
|   +-- pipe_test.asm, signal_test.asm, select_test.asm  # IPC 测试
|   +-- mmap_test.asm, mmap_multi.asm              # 内存测试
|   +-- dyn_hello.c, so_test.c, dlsym_test.c       # 动态链接测试
|   +-- pie_test.c, reloc_test.c                   # PIE + 重定位测试
|   +-- ld_so.c                                    # 动态链接器（ld.so）
|   +-- libfoo.c                                   # 共享库
|   +-- ush.c                                      # 用户态 shell
|   +-- user.ld, ld_so.ld                          # 链接脚本
+-- docs/                       # 文档（15 个文件）
|   +-- BUILD.md, STATUS.md, COPYRIGHT.md, MANIFEST.txt, FEATURE_REQUESTS.md
|   +-- EXTENSIONS.md（总览）
|   +-- EXTENSIONS_WP02..WP08cd.md（按 WP 接口文档）
+-- tools/                      # 构建 + 测试脚本
|   +-- build_iso.sh, gen_font.py, embed_userprog.py
|   +-- qemu_shot.py, qemu_shot_vnc.py, qemu_runner.py
|   +-- github_release_wp08.sh  # GitHub Release 辅助脚本
|   +-- sshd_test.py, paramiko_sshd.py, https_test_server.py  # WP-09 E2E
+-- archive/                    # 旧归档源码（3 个文件，.gitignore 子目录）
+-- .gitignore                  # 排除 build/、*.o、*.elf、*.iso、*.zip、releases/ 等
+-- LICENSE                     # Apache 2.0 全文（201 行）
+-- NOTICE                      # 版权 + 第三方组件
+-- Makefile                    # 顶层构建（kernel + iso）
+-- linker.ld                   # 内核链接脚本
+-- grub.cfg                    # GRUB 引导配置
+-- MANIFEST.md                 # 项目清单
+-- VERIFICATION_REPORT.md       # WP-01..WP-09 验证报告
+-- README.md                   # 本文件
```

**注**：二进制发布物（ISO + 源码 zip）托管在 GitHub Releases——
不存放在本仓库。请通过官网（https://cubestudio-dev.github.io/OpenCubeOS）
或 GitHub Releases 页面下载。

## 快速开始

```sh
# 构建（需要 PATH 中有 gcc/nasm/xorriso/grub-mkimage）
make iso            # -> build/opencube.iso（BIOS + UEFI 双引导）

# 用 QEMU 运行
make run-bios       # SeaBIOS -> GRUB -> 内核
# 或
make run-uefi       # OVMF -> GRUB EFI -> 内核
```

出现 `oc>` 提示符后，输入 `help` 查看 68 条内建命令。
试试 `run ush` 启动用户态 shell。

## 测试

WP-09 基准回归是 **18/18 QEMU 全量套件**（boot 横幅、uname、12 个用户程序、
p3_test、heaptest、l1test、crashlog），经 `tools/qemu_runner.py` 在单次 QEMU
会话中执行。另有：

- `dhtest` — 5/5 DH modexp 正确性（Oakley Group 1 + group14 真值向量
  + 8..256 字节尺度扫描 + 确定性验证）。
- HTTPS E2E — 内核 TLS 1.2 客户端对 `tools/https_test_server.py`
  （仅 TLS1.2，DHE-RSA-AES128-SHA256）：握手 + 加密 GET + 解密响应 + MAC 校验，
  双侧留日志。
- SSH 与 paramiko 5.0 双向互操作（`tools/sshd_test.py` 与
  `tools/paramiko_sshd.py`）：4/4 检查 + K 字节级一致。

带真实输出的完整证据：docs/VERIFICATION_BATCH_B.md，
原始日志在 docs/verification/。

## 下载

- **最新（WP-09）**：[GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases) — ISO + SRC zip
- **归档（WP-08 系列）**：[GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)
- 或访问 https://cubestudio-dev.github.io/OpenCubeOS 直接下载

## 许可证

Apache 2.0。见 `LICENSE`。

## AI 披露

本项目由 cubestudio-dev 在 AI 工具协助下开发。全部设计决策、架构、
规格、项目管理、代码评审、质量保证与验收测试由 cubestudio-dev 完成。
AI 工具仅作为实现辅助。

## 版权

Copyright 2026 cubestudio-dev <cubestudio@qq.com>.
