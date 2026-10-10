<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of README.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, paths, links and identifiers are kept verbatim.
     Structurally re-aligned with the current English original at
     WP-10-AUDIT_P2-fix3 (fix3 G5, BUG-0270). -->

# Open Cube OS

**版本串**：构建时由 `git describe --tags` 根据当前检出的源码树生成
（Makefile `OC_RELEASE_VERSION`）；boot 横幅、`uname -a` 与
`update --status` 均逐字报告该串。本文件刻意不写死任何发布名。当前
源码批次：WP-10-AUDIT_P2-fix3（进行中；最近的已打 tag 发布：
WP-10-AUDIT_P2-fix2b）。

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

## 统计（WP-10-AUDIT_P2-fix3）

- **源码**：102,687 行（kernel + boot + userprogs + fs + net + shell + l1 + drivers + libs，
  含头文件，不含文档；
  验证：`find kernel boot userprogs fs net shell l1 drivers libs \( -name '*.c' -o -name '*.h' -o -name '*.S' \) | xargs wc -l`）
- **工作包**：15 个（WP-01 ~ WP-09、WP-10a、WP-10b、WP-10u、WP-10c、
  WP-10d、WP-10-wp08fix1）——下方每包一个 `## WP-*` 小节
  + 辅助批次（不计入工作包）：项目结构重构、WP-10d-fix2、WP-09-fix5，
  以及审计批次 WP-AUDIT-01（+p0fix/p1fix）与
  WP-10-AUDIT_P2-fix1 / fix2 / fix2b / fix3。
  + 第 ⑨ 条自宿主批次（WP-10c-selfhost）：系统内 `abdisk`、
  `install`、`grub-install`——全部在 oc> shell 内完成 A/B 更新盘创建、
  系统安装到硬盘与 GRUB BIOS 引导器写入（无需宿主机工具；内核载荷经
  multiboot2 module 随启动介质自带）。
- **L1 扩展接口**：138 个（WP-09 及以前 57 个 + WP-10a 新增 8 项：
  driver_block_register / driver_block_read / driver_block_write / driver_block_flush（+ driver_block_set_ops）、
  driver_block_ahci_init(driver_pci_dev)、driver_block_nvme_init(driver_pci_dev)、driver_block_ata_dma_init(driver_pci_dev)，
  + WP-10b 新增 13 项：
  driver_nic_register / driver_nic_send / driver_nic_recv / driver_nic_link_status / driver_nic_get_mac、
  driver_nic_e1000e_init / driver_nic_igb_init / driver_nic_ixgbe_init / driver_nic_rtl8139_init / rtl8168_init /
  rtl8125_init / rtl810x_init / bcm57xx_init(driver_pci_dev)、
  driver_pci_find_class_exact/mask、driver_block_ata_identify_capacity，
  + WP-10u 新增 7 项：ota_ab_update.h（A/B 槽位、标志、校验、安装），
  + WP-10c 新增 26 项：driver_snd_register / driver_snd_play / driver_snd_stop / driver_snd_set_rate /
  driver_snd_set_volume / driver_snd_get_caps（+ driver_snd_probe_all、driver_snd_make_tone 与
  snd.h 查找/列举辅助函数）、
  driver_snd_hda_init / driver_snd_ac97_init / driver_snd_sb16_init / driver_snd_es1370_init / driver_snd_virtio_init /
  driver_usb_audio_init(pci_dev / isa_dev / driver_usb_dev)、
  driver_usb_init / driver_usb_enumerate / driver_usb_control / driver_usb_set_interface /
  driver_usb_iso_out_submit（drivers/usb/driver_usb.h），
  + WP-10d 新增 11 项：driver_usb_register_host / driver_usb_enumerate_host /
  driver_usb_control_transfer / driver_usb_bulk_transfer / driver_usb_interrupt_transfer /
  driver_usb_isochronous_transfer / driver_usb_register_driver（类驱动注册表：
  HID 键盘 + 鼠标、MSC 存储、CDC-ACM/FTDI 串口、UAC 音频）、
  driver_usb_uhci_init / driver_usb_ohci_init / driver_usb_ehci_init / driver_usb_xhci_init(driver_pci_dev)
  ——四类主机控制器后端（drivers/usb/driver_usb.h），
  + WP-10d-fix2 新增 7 项：core_power_shutdown / core_power_suspend /
  core_power_halt / core_power_reboot + shell_register_command_ex /
  shell_list_commands_a_z / shell_list_commands_by_wp，
  + WP-10-wp08fix1 新增 9 项：shell_lineedit_init / shell_lineedit_history_add /
  shell_lineedit_history_get / shell_lineedit_cursor_move /
  shell_lineedit_tab_complete / shell_lineedit_ctrlc（shell/shell_lineedit.h）
  + editor_open / editor_save / editor_close（shell/editor.h）
  （57 + 8 + 13 + 7 + 26 + 11 + 7 + 9 = 138；另有 5 个不入编号的
  WP-09-fix5 config/check-update 接口与 5 个第 ⑨ 条自宿主接口，
  在 1-138 编号之外）
- **系统调用**：46 个（验证：`grep -c '#define SYS_' kernel/core/core_syscall.h`）
- **审计 bug 修复**：原始 WP-08 审计修复 47 个（P0=2、P1=8、P2=29、P3=8）
  + 后续独立审计与 P2 批量收尾（P2-BATCH-1 + P2-BATCH-2）追加修复
  4 个 P0 + 8 个 P1 + 20 个 P2，累计达 79。
  + 15 个 GitHub-AI P3 bug（WP-08-p3：安全 + 内存 + 系统调用 + 信号 + ELF + 管道）
  + 26 个 P4 bug（WP-08-p4：文档修复 + Makefile + ld.so 输出 + shell 管道 +
  idle 对齐 + kill/nice 溢出 + pmm/pftest + execve argv/envp +
  core_kthread_destroy 同步钩子 + 崩溃日志 + VFS 杂项）
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

- **完整系统调用集**（46 个；`SYS_` 编号 0-102，验证：
  `grep -c '#define SYS_' kernel/core/core_syscall.h`）：exit、write、
  write_and_exit、open、close、stat、readdir、mkdir、rmdir、unlink、fork、
  execve、wait4、kill、getpid、getppid、exit2、pipe、dup、dup2、mmap、
  munmap、mprotect、brk、signal、sigaction、sigreturn、chdir、getcwd、
  ioctl、read、write2、readline、getch、uptime、meminfo、select、poll、
  map_solib、symlink、readlink、link、chmod、chown、netcmd、ps
  （symlink..ps 为 WP-10-wp08fix1 新增，SYS 96-102）。
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
- **SSH 服务端**（`net/net_sshd.c`）：同一主流套件（curve25519 KEX、aes128-ctr），
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
  `l1_ext_config_*` / `l1_ext_check_update*` L1 接口。见 docs/CONFIG.md。

## WP-10a（完成）- 存储驱动：AHCI SATA / NVMe / ATA Bus-Master DMA / virtio-blk

- **驱动**（drivers/block/）：AHCI SATA（DMA、多端口、FLUSH）、NVMe（admin +
  2 个 I/O 队列对、Identify、Read/Write/Flush）、ATA Bus-Master DMA
  （PRDT + 保留 PIO 回退）、virtio-blk（容量修复）。
- **L1 接口**（138 项中的第 58-65 项；声明于
  drivers/block/driver_block_blk.h、drivers/pci/driver_pci.h、
  driver_block_{ahci,nvme,ata_dma}.h、driver_block_ata.h）：
  driver_block_register / driver_block_read / driver_block_write /
  driver_block_flush（+ driver_block_set_ops）、
  driver_pci_find_class_exact / driver_pci_find_class_mask、
  driver_block_ahci_init / driver_block_nvme_init /
  driver_block_ata_dma_init(driver_pci_dev)、
  driver_block_ata_identify_capacity。
- 每类设备均支持 MBR + GPT 分区解析；FAT32 经统一 blk 层挂载到全部四类驱动。
  测试套件：`ahci`、`nvme`、`ata` 状态 + `ahci_test`、`nvme_test`、`ata_dma_test`、
  `virtio_blk_test`、`disk_rw_test`、`partition_test`、`fs_mount_test`、`real_hw_test`。
  见 docs/EXTENSIONS_WP10a.md。

## WP-10b（完成）- 网卡驱动：e1000e / igb / ixgbe / RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx

- **驱动框架**（drivers/nic/driver_nic.h）：L1 注册表
  （register / send / recv / link_status / get_mac）、driver_nic_probe_all、
  按型号 init 入口；`other_nics_init` 覆盖遗留尾部型号。
- **L1 接口**（138 项中的第 66-78 项 = 5 + 8；声明于
  drivers/nic/driver_nic.h）：driver_nic_register / driver_nic_send /
  driver_nic_recv / driver_nic_link_status / driver_nic_get_mac +
  driver_nic_e1000e_init / driver_nic_igb_init / driver_nic_ixgbe_init /
  driver_nic_rtl8139_init / rtl8168_init / rtl8125_init / rtl810x_init /
  bcm57xx_init(driver_pci_dev)。
- **测试面**：每型号一个状态命令 + 一个测试命令
  （`e1000e`..`bcm57xx`、`e1000e_test`..`bcm57xx_test`）+ `other_nic_test`
  + `nic_rw_test`。见 docs/EXTENSIONS_WP10b.md。

## WP-10u（完成）- 系统内自动更新：A/B 分区 + tar.gz 更新包 + 回滚 + 离线更新

- **A/B 分区布局**（boot/flags + slot A + slot B + data），分区注册为独立块设备（hdapN）并挂载 /ab/boot、/ab/a、/ab/b、/data。
- **内核自带 gzip 解码器**（RFC 1952/1951：stored/fixed/dynamic 三种块、可跨 feed 恢复、CRC32+ISIZE 校验）与**流式 ustar 解析**（头部校验和、GNU 长名）。
- **两阶段流式 HTTP/HTTPS 下载器**（状态码 + Content-Length 校验）与 **SHA256 双层校验**（update.json 包哈希 + 包内 manifest 载荷哈希）。
- **boot 标志协议**（next_B/ok_B/bootfail_B）实现 GRUB 侧自动回滚；slot B 启动失败保留 bootfail_B，下次启动回 slot A。
- **命令**：update、update --local、update --status、rollback、reboot。
- **用户指南**：docs/UPDATE-HOWTO.md——A/B 盘创建、更新包构建、测试服务器、
  更新/回滚的分步操作与预期输出。
- **验证**：update_pkg_test 12/12、ab_partition_test 7/7、update_check/download/verify/install/rollback/local/status 全 PASS、real_update_test 7/7 端到端（下载-校验-安装 slot B-真实重启-确认 ok_B-回滚）。见 docs/EXTENSIONS_WP10u.md。

## WP-10c（完成）- 声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频

- **snd 框架**（kernel/snd.{c,h}）：8 槽注册表，
  driver_snd_register / driver_snd_play / driver_snd_stop / driver_snd_set_rate / driver_snd_set_volume /
  driver_snd_get_caps；每个驱动都是真 DMA + 真设备中断（每设备 IRQ 计数在 `sound` 与测试中可见）。
- **Intel HDA**（drivers/snd/driver_snd_hda.c）：MMIO BAR、控制器复位、CORB/RIRB
  命令环、codec 地址发现、widget 树枚举（音频功能组、DAC/ADC、引脚）、
  流格式编程与 BDL DMA + IOC 中断、LPIB 流控。
- **AC'97 82801AA**（drivers/snd/driver_snd_ac97.c）：mixer（master/PCM 音量 + 采样率）与总线主控 BDL DMA + IOC 中断。
- **Sound Blaster 16**（drivers/snd/driver_snd_sb16.c）：ISA DSP 4.05（io 0x220、IRQ 5），8/16 位单周期 DMA + 自动初始化块中断。
- **ES1370/1371**（drivers/snd/driver_snd_es1370.c）：DAC2 帧 DMA + PCLKDIV 时钟，内存映射环 + 每缓冲中断。
- **virtio-snd**（drivers/snd/driver_snd_virtio.c）：modern virtio-pci（1AF4:1059），
  控制队列 + TX 队列，PCM prepare/start/set_volume 请求；每次开机探测（QEMU 10 无设备模型，CI 中无实体卡）。
- **USB Audio Class 1.0**（drivers/usb/driver_usb_audio.c）+ **新 UHCI 主机栈**
  （kernel/usb.{c,h}）：UHCI 控制器驱动（piix3/4）、阻塞控制传输、
  设备枚举（SET_ADDRESS/CONFIGURATION/INTERFACE）、同步 OUT 按 1ms 帧调度。
- **44.1/48 kHz** 采样率配置（能力感知；sb16 按设计拒绝 48kHz，测试验证该拒绝）。
- **命令**：sound、hda、ac97、sb16、es1370、virtiosnd、usbaudio、play [device] [rate]、volume [device] [0-100]；lspci 显示声卡控制器（class 0x04）。boot 实测 176 条命令
  （含第 ⑨ 条自宿主命令集：abdisk、install、grub-install、abcfg，
  以及 WP-10-wp08fix1 的 nano/vi 编辑器）。
- **验证**：hda_test / ac97_test / sb16_test / es1370_test / usb_audio_test 在 QEMU 实测（初始化 + 能力 + DMA 字节数 + IRQ 计数），audio_rw_test 对每张卡播放，sample_rate_test 配置 44.1/48kHz，virtio_snd_test 如实 SKIPPED（无 QEMU 设备模型），real_hw_test 在 VM 中如实 NOT RUN。见 docs/EXTENSIONS_WP10c.md。

## WP-10d（完成）- USB 主机栈：UHCI / OHCI / EHCI / XHCI + HID/MSC/串口/音频 + Hub/热插拔

- **USB 核心**（kernel/usb.{c,h}）：主机注册表、设备表、端点管理、四种传输（控制/中断/批量/等时）、类驱动注册表（probe + disconnect）与枚举器（根端口 + 外部 Hub 级联 + 热插拔）。
- **UHCI**（drivers/usb/driver_usb.c，PIIX3）：全链路——kbd/mouse/Hub 级联枚举、MSC、FAT32 往返、热插拔。
- **OHCI**（drivers/usb/driver_usb_ohci.c）：独立验证会话全 PASS——含产品字符串的枚举、usb_core_test 5/5、MSC 容量/MBR/写读回、FAT32 mkfs + 挂载 + 文件往返、真热插拔；修复：控制 DATA TD 补 TD_R（缓冲取整）、状态 TD 方向位改用 u32（u8 截断把 bit-19/20 方向位变成 SETUP，导致每个状态阶段被 STALL）、中断 IN TD 补 TD_R。
- **EHCI**（drivers/usb/driver_usb_ehci.c）：异步环 + qTD 引擎（缓冲指针按 spec、IAAD 门铃）；kbd/mouse/存储枚举、MSC 读写、FAT32 挂载与文件往返、STALL 恢复。
- **XHCI**（drivers/usb/driver_usb_xhci.c）：命令/事件环、DCBAA + scratchpad、两段式 AddressDevice、惰性 ConfigureEndpoint、按 spec 布局的端点上下文；Reset Endpoint / Set TR Dequeue 的端点 ID 移至 control bits 20:16（spec Table 6-42/6-44）——旧低位布局使控制器以 TRB Error 结束命令、STALL 端点永久无法恢复。枚举/HID/核心测试 PASS；qemu-xhci 的 usb-storage CSW 缺口如实标注。
- **类驱动**：HID 键盘/鼠标（报告描述符）、MSC（BOT + SCSI 对接 blk）、CDC-ACM/FTDI 串口、UAC 1.0/2.0。
- **命令**：usb、usbdev + usb_core_test / usb_kbd_test / usb_mouse_test / usb_storage_test / usb_serial_test / usb_hotplug_test / usb_hub_test（boot 实测 176 条命令；138 个 L1 接口；
  WP-10d-fix2 新增 7 项：core_power_shutdown/core_power_suspend/core_power_halt/core_power_reboot
  + shell_register_command_ex/shell_list_commands_a_z/shell_list_commands_by_wp）。
- **验证**：UHCI/OHCI/EHCI/XHCI 在 QEMU 显式控制器下实测（piix3-usb-uhci、pci-ohci、usb-ehci、qemu-xhci）；BIOS + UEFI 启动矩阵；四控制器 usb_core_test 全 5/5；OHCI 独立会话与 XHCI EPID 修复收官。见 docs/EXTENSIONS_WP10d.md。

## WP-10-wp08fix1（完成）- Shell 行编辑器 + nano/vi 编辑器 + VFS link/symlink + 7 个新系统调用

- **内核侧行编辑器**（shell/shell_lineedit.h）：历史
  （shell_lineedit_init / shell_lineedit_history_add /
  shell_lineedit_history_get）、光标移动
  （shell_lineedit_cursor_move）、TAB 补全
  （shell_lineedit_tab_complete）、^C 处理（shell_lineedit_ctrlc）
  ——第 130-135 项。
- **nano / vi 全屏编辑器**（shell/editor.h）：editor_open /
  editor_save / editor_close——第 136-138 项；`nano` 与 `vi`
  命令在 boot 时注册。
- **VFS 新增**（fs/fs_vfs.h）：symlink/readlink + 硬链接，
  chmod/chown 含权限强制执行。
- **7 个新系统调用（SYS 96-102）**：symlink、readlink、link、chmod、chown、
  netcmd、ps；ush 补齐对应的用户态命令。
- 本批次将 L1 接口编号收口于 **138**（第 130-138 项）。
  见 docs/EXTENSIONS_WP10-wp08fix1.md。

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
|   +-- idt.{c,h}, arch_idt_stub.S, arch_idt_load.S          # WP-02：IDT/GDT/TSS
|   +-- pic.h, exceptions.{c,h}, irq.{c,h}         # WP-02：PIC + 异常
|   +-- timer.{c,h}, keyboard.{c,h}                # WP-02：PIT + 键盘
|   +-- screen_serial_in.{c,h}, console_in.{c,h}          # WP-02：COM1 RX + 行编辑器
|   +-- pmm.{c,h}, vmm.{c,h}, heap.{c,h}           # WP-03：内存管理
|   +-- shell.{c,h}                                 # WP-03：shell + 命令注册
|   +-- sched.{c,h}, sync.{c,h}                     # WP-04：调度器 + 同步
|   +-- usermode.{c,h}, enter_ring3_fork.S         # WP-04：用户态 + fork
|   +-- context_switch.S                            # WP-04：上下文切换
|   +-- vfs.{c,h}, ramfs.{c,h}                     # WP-05：VFS + ramfs
|   +-- file_cmds.{c,h}, shell_cmds               # WP-05：文件命令
|   +-- net.{c,h}                                   # WP-06：TCP/IP 协议栈
|   +-- ata.{c,h}, virtio_blk.{c,h}, nvme.{c,h}    # WP-07：磁盘驱动
|   +-- blk.{c,h}, driver_block_cache.{c,h}, part.{c,h}     # WP-07：块层 + 分区
|   +-- fat32.{c,h}, exfat.{c,h}, ext4.{c,h}       # WP-07：文件系统
|   +-- disk_cmds.{c,h}                             # WP-07：磁盘命令
|   +-- syscall.{c,h}                               # WP-08：syscall 分发
|   +-- ext_wp8a.{c,h}, ext_wp8b.{c,h}, ext_wp8cd.{c,h}  # WP-08 L1 扩展
|   +-- userprogs_data.h, solib_data.h             # WP-08 内嵌 ELF + .so 数据
|   +-- crypto.{c,h}, crypto_dh_scale_vectors.h           # WP-09：AES/SHA/HMAC/DH
|   +-- bn.{c,h}, crypto_ec_nist.{c,h}, curve25519.{c,h}  # WP-09 主流化：大整数 + P-256/384 + X25519
|   +-- rsa.{c,h}, aead.{c,h}, sha512.{c,h}        # WP-09 主流化：RSA 验证 + AEAD + SHA-512
|   +-- x509.{c,h}                                 # WP-09 主流化：X.509 链 + 主机名验证
|   +-- net_tcp_cc.{c,h}                               # WP-09 主流化：CUBIC 拥塞控制
|   +-- ssh.{c,h}, sshd.c, net_sshd_rsa_key.h          # WP-09：SSH 客户端 + 服务端
|   +-- tls.{c,h}                                   # WP-09：TLS 1.3/1.2 客户端
|   +-- config.{c,h}, update.{c,h}, ota_ab_update.{c,h}  # WP-09-fix5/WP-10u：配置 + 检查更新 + A/B 更新
|   +-- ahci.{c,h}, driver_block_ata_dma.{c,h}                   # WP-10a：AHCI SATA + ATA Bus-Master DMA
|   +-- nic.{c,h}, driver_nic_e1000e.c, driver_nic_igb.c,        # WP-10b：网卡框架 + 九族
|   |   driver_nic_ixgbe.c, driver_nic_rtl8139.c, driver_nic_rtl8169.c, #   驱动 + 测试
|   |   driver_nic_bcm57xx.c, driver_nic_other.c, driver_nic_test_cmds.c
|   +-- snd.{c,h}, driver_snd_test_cmds.c                   # WP-10c：声卡框架 + 测试
|   +-- hda.{c,h}, ac97.{c,h}, sb16.{c,h},          # WP-10c：六族声卡驱动
|   |   es1370.{c,h}, virtio_snd.{c,h},             #   + USB 音频
|   |   usb.{c,h}, driver_usb_audio.{c,h}
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
|   +-- net_sshd_test.py, paramiko_sshd.py, https_test_server.py  # WP-09 E2E
|   +-- make_ab_disk.sh, make_update_pkg.sh, ota_update_server.py # WP-10u OTA
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

**在 QEMU 里体验硬件特性**（空盘格式化、DHCP/ping 用的网卡、播放音调的
声卡、系统内更新用的 A/B 盘）：分步命令与预期输出见
**docs/TRY-IT.md**；更新流程有专门的指南
**docs/UPDATE-HOWTO.md**。

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
  握手 + 加密 GET + 解密响应 + MAC 校验。
- SSH 与 paramiko 5.0 双向互操作（`tools/net_sshd_test.py` 与
  `tools/paramiko_sshd.py`）：密码 + 公钥认证、curve25519/group14 KEX、
  aes128-ctr/cbc — 4/4 检查 + K 字节级一致 + 服务器主机密钥签名验证（TOFU 指纹）。
- WP-10u 更新测试 — update_pkg_test 12/12、ab_partition_test 7/7、
  update_check/download/verify/install/rollback/local/status 全 PASS、
  real_update_test 7/7 真实重启进入 slot B（`tools/make_ab_disk.sh`、
  `tools/make_update_pkg.sh`、`tools/ota_update_server.py`）。
  分步用户指南：**docs/UPDATE-HOWTO.md**。
- WP-10c 声卡测试 — hda_test / ac97_test / sb16_test / es1370_test /
  usb_audio_test 在 QEMU 实测（真 DMA + 真 IRQ 计数）、audio_rw_test 对每张卡、
  sample_rate_test 44.1/48kHz；virtio_snd_test SKIPPED（无 QEMU 设备模型）、
  real_hw_test 在 VM 中 NOT RUN（如实申报）。

## 下载

- **最新（WP-10-AUDIT_P2-fix2b）**：[GitHub Release](https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10-AUDIT_P2-fix2b) — ISO + SRC zip
  （ISO sha256 `6f5d7c2c63394dc261d5a2125d01aaccdfbf23b1f726869ac7001dc815d8a656`，
  SRC sha256 `bc2f00700fac9579e10b2188d3aed07d84ad30e4c969f574e9ccbe6bb54b9344`）
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
