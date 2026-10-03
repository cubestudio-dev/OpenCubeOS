<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of README.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, paths, links and identifiers are kept verbatim. -->

# Open Cube OS - WP-10c

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

## 统计（WP-10c）

- **源码**：69,910 行（kernel + boot + userprogs，含头文件 + 链接脚本，不含文档；
  验证：`find kernel boot userprogs \( -name '*.c' -o -name '*.h' -o -name '*.S' \) | xargs wc -l`）
- **工作包**：13 个（WP-01 ~ WP-09、WP-10a、WP-10b、WP-10u、WP-10c）
- **L1 扩展接口**：111 个（WP-09 及以前 57 个 + WP-10a 新增 8 项：
  blk_register / blk_read / blk_write / blk_flush（+ blk_set_ops）、
  ahci_init(pci_dev)、nvme_init(pci_dev)、ata_dma_init(pci_dev)，
  + WP-10b 新增 13 项：
  nic_register / nic_send / nic_recv / nic_link_status / nic_get_mac、
  e1000e_init / igb_init / ixgbe_init / rtl8139_init / rtl8168_init /
  rtl8125_init / rtl810x_init / bcm57xx_init(pci_dev)、
  pci_find_class_exact/mask、ata_identify_capacity，
  + WP-10u 新增 7 项：ab_update.h（A/B 槽位、标志、校验、安装），
  + WP-10c 新增 26 项：snd_register / snd_play / snd_stop / snd_set_rate /
  snd_set_volume / snd_get_caps（+ snd_probe_all、snd_make_tone 与 snd.h
  查找/列举辅助函数）、
  hda_init / ac97_init / sb16_init / es1370_init / virtio_snd_init /
  usb_audio_init(pci_dev / isa_dev / usb_dev)、
  usb_init / usb_enumerate / usb_control / usb_set_interface /
  usb_iso_out_submit（kernel/usb.h））
- **系统调用**：37 个
- **审计 bug 修复**：原始 WP-08 审计修复 47 个（P0=2、P1=8、P2=29、P3=8）
  + 后续独立审计与 P2 批量收尾（P2-BATCH-1 + P2-BATCH-2）追加修复
  4 个 P0 + 8 个 P1 + 20 个 P2，累计达 79。
  + 15 个 GitHub-AI P3 bug（WP-08-p3：安全 + 内存 + 系统调用 + 信号 + ELF + 管道）
  + 26 个 P4 bug（WP-08-p4：文档修复 + Makefile + ld.so 输出 + shell 管道 +
  idle 对齐 + kill/nice 溢出 + pmm/pftest + execve argv/envp +
  kthread_destroy 同步钩子 + 崩溃日志 + VFS 杂项）
  总计：修复 120 个 bug（截至 WP-08；WP-09 又完成 SSH/TLS 专项修复）。
- **测试通过**：18/18 QEMU 全量回归（WP-09 基准套件——boot 横幅 + uname +
  12 个用户程序 + p3_test + heaptest + l1test + crashlog），另有 dhtest 5/5、
  HTTPS E2E 与 SSH 和 paramiko 双向互操作。

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

## WP-09（完成）- 安全传输：SSH（客户端 + 服务端）、TLS 1.3 / TLS 1.2 / HTTPS、crypto 核心

- **Crypto 核心**（`kernel/crypto.{c,h}` + bn/ec/rsa/aead/x509 模块）：
  AES-128（CTR/CBC）+ AES-128/256-GCM + ChaCha20-Poly1305、SHA-256、流式 SHA-256/HMAC、
  SHA-512/384、HMAC-SHA-512、HKDF、大整数 modexp（Montgomery 核心，可跑 RSA-4096，
  以 python3 pow() 真值向量在 8/16/32/64/128/256 字节尺度验证）、NIST P-256/P-384 ECDH+ECDSA、
  X25519（RFC 7748 向量）、RSA PKCS#1 v1.5 + PSS 验证、RFC 8439/NIST KAT 验证的 AEAD、crypto_random。
- **SSH 客户端**（`kernel/ssh.{c,h}`）：KEX curve25519-sha256（+@libssh.org）与
  diffie-hellman-group14-sha256、aes128-ctr（aes128-cbc 回退）、hmac-sha2-256、
  rsa-sha2-256/512 主机密钥、密码**与公钥**认证、session channel exec、
  服务器主机密钥签名在 H 上验证并显示 TOFU SHA-256 指纹。
  K 与 paramiko 服务端抓包字节级一致。
- **SSH 服务端**（`kernel/sshd.c`）：同一主流套件（curve25519 KEX、aes128-ctr），
  密码**与公钥**认证（内核身份密钥兼作主机密钥），
  exec 请求经内核 shell 捕获 API 执行；与 paramiko 客户端互操作验证。
- **TLS 1.3 客户端**（RFC 8446，优先）：X25519、AES-128/256-GCM 与
  ChaCha20-Poly1305，完整 HKDF 密钥日程（RFC 8448 向量），加密握手，服务器 CertificateVerify 校验。
- **TLS 1.2 客户端**（RFC 5246，回退）：ECDHE_RSA + AES-GCM 与
  ChaCha20-Poly1305；旧式 DHE-CBC（0x0067）保留给旧服务器。
- **证书验证**（`kernel/x509.{c,h}`）：X.509 链验证（内嵌公共 CA 根：ISRG、Google GTS、
  DigiCert、GlobalSign、Baltimore、Amazon、Microsoft）+ SAN dNSName 主机名匹配，
  RTC 校验有效期。验证失败即握手失败。
- **HTTPS**：`wget https://host:port/path` 经 TLS 下载，直接落入 VFS。
  真实站点验证：cubestudio-dev.github.io（10148 字节 update.json）、
  google.com、cloudflare.com。
- **网络运维命令**：route、arp、firewall（状态规则、conntrack、三链默认策略 +
  REJECT + 逐规则命中计数器）、tcpstats（CUBIC cwnd/RTO/SACK/快速重传可见性）、
  dns（A/AAAA/CNAME/MX/TXT/NS/SRV）。
- **Shell**：116 条（boot 实测计数；`help` 列出 116 个不重复名）；含 WP-10a 存储命令面：
  `ahci`、`nvme`、`ata`（驱动状态）与八项测试套件 `ahci_test`、`nvme_test`、
  `ata_dma_test`、`virtio_blk_test`、`disk_rw_test`、`partition_test`、`fs_mount_test`、
  `real_hw_test`，以及 WP-10b 网卡命令面：`e1000e`、`igb`、`ixgbe`、
  `rtl8139`、`rtl8168`、`rtl8125`、`rtl810x`、`bcm57xx`（驱动状态）
  与 `e1000e_test`、`igb_test`、`ixgbe_test`、`rtl8139_test`、
  `rtl8168_test`、`rtl8125_test`、`rtl810x_test`、`bcm57xx_test`、
  `other_nic_test`、`nic_rw_test`。
- **存储（WP-10a）**：AHCI SATA（DMA、多端口、FLUSH）、NVMe（admin +
  2 个 I/O 队列对、Identify、Read/Write/Flush）、ATA Bus-Master DMA
  （PRDT + 保留 PIO 回退）、virtio-blk（容量修复）；
  每类设备均支持 MBR + GPT 分区解析；FAT32 经统一 blk 层挂载到全部四类驱动。
- **新用户测试程序**：mprotect_test、p3_test。
- **验证**：18/18 QEMU 回归 + dhtest 5/5 + cryptotest 3/3 + nf_test 8/8 +
  tcpcc_test + dnstest 6/6（live）+ tcptest + HTTPS E2E（上述真实站点）+
  SSH 双向互操作（外部证据：paramiko 5.0）。见 docs/EXTENSIONS_WP09.md、docs/INTERFACES.md。
- **WP-09-fix5 —— 系统配置 + 检查更新**：
  `/etc/opencube.conf`（首个用户可编辑配置，FAT32 /etc 卷、
  ramfs 回退）、`checkupdate` 经 HTTP/HTTPS 拉取 JSON manifest、
  非阻塞 `auto_check` 开机检查、`config`/`edit` 命令与
  `oc_ext_config_*` / `oc_ext_check_update*` L1 接口。见 docs/CONFIG.md。

## WP-10u（完成）- 系统内自动更新：A/B 分区 + tar.gz 更新包 + 回滚 + 离线更新

- **A/B 分区布局**（boot/flags + slot A + slot B + data），分区注册为独立块设备（hdapN）并挂载 /ab/boot、/ab/a、/ab/b、/data。
- **内核自带 gzip 解码器**（RFC 1952/1951：stored/fixed/dynamic 三种块、可跨 feed 恢复、CRC32+ISIZE 校验）与**流式 ustar 解析**（头部校验和、GNU 长名）。
- **两阶段流式 HTTP/HTTPS 下载器**（状态码 + Content-Length 校验）与 **SHA256 双层校验**（update.json 包哈希 + 包内 manifest 载荷哈希）。
- **boot 标志协议**（next_B/ok_B/bootfail_B）实现 GRUB 侧自动回滚；slot B 启动失败保留 bootfail_B，下次启动回 slot A。
- **命令**：update、update --local、update --status、rollback、reboot。
- **验证**：update_pkg_test 12/12、ab_partition_test 7/7、update_check/download/verify/install/rollback/local/status 全 PASS、real_update_test 7/7 端到端（下载-校验-安装 slot B-真实重启-确认 ok_B-回滚）。见 docs/EXTENSIONS_WP10u.md。

## WP-10c（完成）- 声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频

- **snd 框架**（kernel/snd.{c,h}）：8 槽注册表，snd_register / snd_play / snd_stop / snd_set_rate / snd_set_volume / snd_get_caps；每个驱动都是真 DMA + 真设备中断（每设备 IRQ 计数在 `sound` 与测试中可见）。
- **Intel HDA**（kernel/hda.c）：MMIO BAR、控制器复位、CORB/RIRB 命令环、codec 地址发现、widget 树枚举（音频功能组、DAC/ADC、引脚）、流格式编程与 BDL DMA + IOC 中断、LPIB 流控。
- **AC'97 82801AA**（kernel/ac97.c）：mixer（master/PCM 音量 + 采样率）与总线主控 BDL DMA + IOC 中断。
- **Sound Blaster 16**（kernel/sb16.c）：ISA DSP 4.05（io 0x220、IRQ 5），8/16 位单周期 DMA + 自动初始化块中断。
- **ES1370/1371**（kernel/es1370.c）：DAC2 帧 DMA + PCLKDIV 时钟，内存映射环 + 每缓冲中断。
- **virtio-snd**（kernel/virtio_snd.c）：modern virtio-pci（1AF4:1059），控制队列 + TX 队列，PCM prepare/start/set_volume 请求；每次开机探测（QEMU 10 无设备模型，CI 中无实体卡）。
- **USB Audio Class 1.0**（kernel/usb_audio.c）+ **新 UHCI 主机栈**（kernel/usb.{c,h}）：UHCI 控制器驱动（piix3/4）、阻塞控制传输、设备枚举（SET_ADDRESS/CONFIGURATION/INTERFACE）、同步 OUT 按 1ms 帧调度。
- **44.1/48 kHz** 采样率配置（能力感知；sb16 按设计拒绝 48kHz，测试验证该拒绝）。
- **命令**：sound、hda、ac97、sb16、es1370、virtiosnd、usbaudio、play [device] [rate]、volume [device] [0-100]；lspci 显示声卡控制器（class 0x04）。boot 实测 150 条命令。
- **验证**：hda_test / ac97_test / sb16_test / es1370_test / usb_audio_test 在 QEMU 实测（初始化 + 能力 + DMA 字节数 + IRQ 计数），audio_rw_test 对每张卡播放，sample_rate_test 配置 44.1/48kHz，virtio_snd_test 如实 SKIPPED（无 QEMU 设备模型），real_hw_test 在 VM 中如实 NOT RUN。见 docs/EXTENSIONS_WP10c.md。

## 仓库结构

```
oc-os/
+-- boot/                       # 汇编引导桩（3 个 .S 文件，WP-01）
|   +-- multiboot2_header.S
|   +-- boot.S
|   +-- long_mode_init.S
+-- kernel/                     # C 内核（142 个文件：.c + .h + .S）
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
|   +-- bn.{c,h}, ec_nist.{c,h}, curve25519.{c,h}  # WP-09 主流化：大整数 + P-256/384 + X25519
|   +-- rsa.{c,h}, aead.{c,h}, sha512.{c,h}        # WP-09 主流化：RSA 验证 + AEAD + SHA-512
|   +-- x509.{c,h}                                 # WP-09 主流化：X.509 链 + 主机名验证
|   +-- tcp_cc.{c,h}                               # WP-09 主流化：CUBIC 拥塞控制
|   +-- ssh.{c,h}, sshd.c, sshd_rsa_key.h          # WP-09：SSH 客户端 + 服务端
|   +-- tls.{c,h}                                   # WP-09：TLS 1.3/1.2 客户端
|   +-- config.{c,h}, update.{c,h}, ab_update.{c,h}  # WP-09-fix5/WP-10u：配置 + 检查更新 + A/B 更新
|   +-- ahci.{c,h}, ata_dma.{c,h}                   # WP-10a：AHCI SATA + ATA Bus-Master DMA
|   +-- nic.{c,h}, nic_e1000e.c, nic_igb.c,        # WP-10b：网卡框架 + 九族
|   |   nic_ixgbe.c, nic_rtl8139.c, nic_rtl8169.c, #   驱动 + 测试
|   |   nic_bcm57xx.c, nic_other.c, nic_test_cmds.c
|   +-- snd.{c,h}, snd_test_cmds.c                   # WP-10c：声卡框架 + 测试
|   +-- hda.{c,h}, ac97.{c,h}, sb16.{c,h},          # WP-10c：六族声卡驱动
|   |   es1370.{c,h}, virtio_snd.{c,h},             #   + USB 音频
|   |   usb.{c,h}, usb_audio.{c,h}
|   +-- kmain.c                                     # 内核主入口
+-- userprogs/                  # 用户态程序（23 个文件：.c + .asm + .ld）
|   +-- hello.asm, badapp.asm, loop.asm            # 基础测试
|   +-- fork_test.asm, exec_test.asm               # 进程测试
|   +-- pipe_test.asm, signal_test.asm, select_test.asm  # IPC 测试
|   +-- mmap_test.asm, mmap_multi.asm, mprotect_test.asm  # 内存测试
|   +-- p3_test.asm                                 # WP-09：P3 回归
|   +-- main_dyn.c, dyn_hello.c, so_test.c, dlsym_test.c  # 动态链接测试
|   +-- pie_test.c, reloc_test.c                   # PIE + 重定位测试
|   +-- ld_so.c                                    # 动态链接器（ld.so）
|   +-- libfoo.c                                   # 共享库
|   +-- ush.c                                      # 用户态 shell
|   +-- user.ld, ld_so.ld                          # 链接脚本
+-- docs/                       # 文档（18 个文件）
|   +-- BUILD.md, CONFIG.md, COPYRIGHT.md, INTERFACES.md, MANIFEST.txt
|   +-- EXTENSIONS.md（总览）
|   +-- EXTENSIONS_WP02..WP10c.md（按 WP 接口文档）
+-- tools/                      # 构建 + 测试脚本
|   +-- build_iso.sh, gen_font.py, embed_userprog.py
|   +-- qemu_shot.py, qemu_shot_vnc.py, qemu_runner.py
|   +-- github_release_wp08.sh  # GitHub Release 辅助脚本
|   +-- sshd_test.py, paramiko_sshd.py, https_test_server.py  # WP-09 E2E
|   +-- make_ab_disk.sh, make_update_pkg.sh, update_server.py # WP-10u OTA
+-- archive/                    # 旧归档源码（3 个文件，.gitignore 子目录）
+-- .gitignore                  # 排除 build/、*.o、*.elf、*.iso、*.zip、releases/ 等
+-- LICENSE                     # Apache 2.0 全文（201 行）
+-- NOTICE                      # 版权 + 第三方组件
+-- Makefile                    # 顶层构建（kernel + iso）
+-- linker.ld                   # 内核链接脚本
+-- grub.cfg                    # GRUB 引导配置
+-- MANIFEST.md                 # 项目清单
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

出现 `oc>` 提示符后，输入 `help` 查看完整命令列表。
试试 `run ush` 启动用户态 shell。

## 测试

WP-09 基准回归是 **18/18 QEMU 全量套件**（boot 横幅、uname、12 个用户程序、
p3_test、heaptest、l1test、crashlog），经 `tools/qemu_runner.py` 在单次 QEMU
会话中执行。另有：

- `dhtest` — 5/5 DH modexp 正确性（Oakley Group 1 + group14 真值向量
  + 8..256 字节尺度扫描 + 确定性验证）。
- HTTPS E2E — 真实站点 TLS（内核客户端）：
  `https://cubestudio-dev.github.io/OpenCubeOS/update.json` → 10148 字节；
  google.com、cloudflare.com；另有本地 `tools/https_test_server.py`
  （仅 TLS1.2，DHE-RSA-AES128-SHA256）覆盖旧式回退路径：
  握手 + 加密 GET + 解密响应 + MAC 校验，
  双侧留日志。
- SSH 与 paramiko 5.0 双向互操作（`tools/sshd_test.py` 与
  `tools/paramiko_sshd.py`）：密码 + 公钥认证、curve25519/group14 KEX、
  aes128-ctr/cbc — 4/4 检查 + K 字节级一致 + 服务器主机密钥签名验证（TOFU 指纹）。
- WP-10u 更新测试 — update_pkg_test 12/12、ab_partition_test 7/7、
  update_check/download/verify/install/rollback/local/status 全 PASS、
  real_update_test 7/7 真实重启进入 slot B（`tools/make_ab_disk.sh`、
  `tools/make_update_pkg.sh`、`tools/update_server.py`）。
- WP-10c 声卡测试 — hda_test / ac97_test / sb16_test / es1370_test /
  usb_audio_test 在 QEMU 实测（真 DMA + 真 IRQ 计数）、audio_rw_test 对每张卡、
  sample_rate_test 44.1/48kHz；virtio_snd_test SKIPPED（无 QEMU 设备模型）、
  real_hw_test 在 VM 中 NOT RUN（如实申报）。

## 下载

- **最新（WP-10c）**：[GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases) — ISO + SRC zip + 系统内更新包
- **WP-10u 更新**：系统内自动更新——A/B 双分区（boot/flags + slot A + slot B + data，分区注册为 hdapN 块设备并挂载 /ab/boot、/ab/a、/ab/b、/data）；内核自带 gzip 解压（RFC 1952/1951：stored/fixed/dynamic 块、可跨 feed 恢复、CRC32+ISIZE 校验）与流式 ustar 解析（头部校验和、GNU 长名、自动剥离顶层包裹目录）；两阶段流式 HTTP/HTTPS 下载（状态码 + Content-Length 校验）；SHA256 双层校验（update.json 包哈希 + 包内 manifest 载荷哈希）；boot 标志协议（next_B/ok_B/bootfail_B）实现 GRUB 侧自动回滚；配置扩至 4 项（package_url、online_update）；命令新增 update / update --local / update --status / rollback / reboot；L1 扩展接口新增 oc_ext_update_check_pkg / download / verify / install / rollback / set_boot / get_status 七项（共 85）；测试新增 update_pkg_test（12/12：gzip 三种块型、头选项、坏 CRC、截断、单字节流式恢复、ustar 解析与坏校验和）、ab_partition_test（7/7）、update_check/download/verify/install/rollback/local/status、real_update_test（7/7 端到端：下载-校验-安装至 slot B-真实重启进入 test1 内核-确认-回滚回 A）
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
