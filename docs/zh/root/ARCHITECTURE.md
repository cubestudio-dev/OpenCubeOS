<!-- GENERATED mirror of the repository-root ARCHITECTURE.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 架构

本文档描述 2.1.0 树中的真实形态——下面列出的每个组件都是你可以打开
的实际文件。行数和每目录详情在 `docs/data/stats.json` 中（重新生成：
`make api-docs`）。

## 两个环

Open Cube OS 是一个双环系统：

- **L0——提供服务。** 内核加驱动、服务实现，以及 WinRE 式恢复环境
  （`oc>` 壳）。L0 拥有硬件和全部服务实体。
- **L1——使用服务。** Ring-3 用户态应用，通过扩展接口面（`SYS_L1SVC`
  op 表与经典 syscall ABI）使用 L0 能力。默认启动项是 ring-3 的
  `ush` 壳，由 `services/l1/l1_launcher.c` 声明
  （`OC_L1_APP_DECL("ush", ..., dflt=1)`）；ush 退出或启动失败后，
  回退到 `oc>`。

```
                  GRUB 2.12（BIOS + UEFI 混合 ISO）
                                  |  multiboot2 + 内核即模块
        +-------------------------+-------------------------+
        |                   boot/boot.S                     |
        |  32 位入口 -> GDT -> 2 MiB 大页恒等映射前 4 GiB   |
        |  -> PAE+LME+PGE -> long mode                      |
        +---------------------------------------------------+
                                  |
   core/main.c（kmain：启动顺序即串口日志顺序）
                                  |
   +-----------+-----------+------------+------------+
   |           |           |            |            |
 arch/x86_64  core/mem    core/lib    core/       services/l1
 IDT/GDT/TSS  PMM/VMM/    string/tar  调度器      启动器、
 异常处理     堆          gzip/config syscall 54   SYS_L1SVC(110)
 8259 LAPIC   4 KiB 页    klog        ring3+ELF   + 自检
 IRQ 定时器               config      信号
                                  |
   +----------------+---------------+-----------------+
   |                |               |                 |
 drivers/*       services/*      recovery/        tui/ chinese/
 16 个家族       17 个文件夹      oc> 壳           v1 时代的 TUI/CJK
 block NVME      文件夹 == L1SVC  99 条命令        命令树，仍编入
 AHCI ATA DMA    类别：fs net     （自检、看门狗、 内核，可从 oc>
 virtio-blk      crypto ota klog  崩溃策略、日志、 调用；中文控制台
 USB UHCI/OHCI/  health watchdog  芯片组诊断、     + 拼音输入法
 EHCI/xHCI       power time block 文件命令、       （CONFIG_CHINESE）
 MSC HID 音频    nic snd display  ssh/sshd）
 串口、PCI       input camera
 PCIe ACPI      usb + services/l1
 I2C SMBus      自身
 HPET/PMtimer/
 TSC/RTC
 wireless 蓝牙 SD 摄像头 UVC 红外 游戏口 并口 SCSI LSI
                                  |
                            l1apps/（ring 3）
   ush——启动壳（唯一的 OC_L1_APP_DECL 应用）：133 个唯一 SVC 行
   -> SYS_L1SVC、19 个内建、4 个 NETCMD 桥；以及 36 个嵌入测试
   程序（hello、fork/exec/pipe/signal/select/mmap、dlsym/PIE
   动态链接、g6/g7 链），经 `run` 运行。
```

## 启动流（串口日志顺序）

1. GRUB 加载 `boot/opencube.elf`（内核同时作为标记 `self` 的
   multiboot2 模块附加——安装器模式，`install`/`abdisk` 因此能把
   *正在运行的*内核原样拷到盘上）。
2. `boot/boot.S`：32 位入口、自有 GDT、2 MiB 大页恒等映射前
   4 GiB、PAE + LME + PGE、切入 64 位 long mode、调用 `kmain`。
3. `arch/x86_64/arch_multiboot2.c` 解析 multiboot2 信息（帧缓冲、
   内存图、命令行、模块表、ACPI RSDP 标签）——对畸形输入有完整
   防护：每标签最小尺寸、NUL 终止检查、截断标签拒绝。
4. `kmain` 按日志顺序拉起：帧缓冲控制台与字体、IDT/GDT/TSS
   （256 门，#DF/#MC 与调试用 IST 栈）、8259 PIC 重映射、
   100 Hz PIT、PS/2 键盘、COM1 串口 RX、行编辑器、异常处理
   （带 #DE/#UD/#PF 实测自检）、PMM、VMM、堆，然后是收集式的
   驱动/服务域、VFS、网络、崩溃恢复。
5. 服务与驱动在各自文件内自我声明（`OC_SERVICE_DECL` /
   `OC_DRIVER_DECL`，由链接器段收集——`core/core_registry.c`），
   初始化运行，启动器启动默认 ring-3 应用：`ush>` 提示符出现。
   （内核两遍链接；`tools/embed_grub.py` 把 GRUB boot+core 映像
   嵌为 `core/generated/grub_boot_data.c`，`grub-install`/`install`
   因此能写出精确的引导映像。）

## 内存管理

- **PMM**（`core/mem/mem_pmm.{c,h}`）：基于 multiboot2 内存图的
  4 KiB 页框位图分配器；保留区（内核代码、帧缓冲、mbi）初始化时
  标记已用；O(1) 分配/释放。
- **VMM**（`core/mem/mem_vmm.{c,h}`）：四级页表；内核保留启动时的
  前 4 GiB 恒等映射；API 创建并操作额外地址空间（用户进程）。
  页保护位映射到 rw/nx/user。
- **堆**（`core/mem/mem_heap.{c,h}`）：kmalloc/kfree/krealloc/
  kzalloc，first-fit + 合并的自由链表，16 字节对齐块，16 MiB
  快速失败上限。
- **#PF 增长**：栈增长与堆增长的缺页是异常路径里的真实处理——
  非法访问升级为崩溃路径。

## 调度与时间

- **调度器**（`core/core_sched.{c,h}`）：抢占式，任务控制块上的
  优先级 + 时间片轮转；上下文切换在
  `arch/x86_64/arch_context_switch.S` 系列汇编中，对齐经 ABI 探针
  验证。
- **定时器**（`core/core_timer.{c,h}`）：100 Hz PIT 驱动 tick、
  `g_boot_time_ms` 与软定时轮。HPET、ACPI PM timer 与 TSC 作为
  *附加*时间源（`drivers/time/`）——也是两个壳都能用的命令。
- **看门狗**（`services/watchdog/core_watchdog.{c,h}`）：受监控
  任务槽位与超时策略（`watchdog` 命令），以及固件提供时的真硬件
  看门狗——Intel TCO（QEMU q35 上实测）与 ACPI WDAT 表。

## 中断与异常

`arch/x86_64/arch_idt.{c,h}` + `arch_irq.c`：256 项 IDT、TSS
IST1（#DF/#MC panic 栈）与 IST2（专用异常退出栈——绝不在 IST2 上
发生上下文切换）、8259 重映射到向量 32-47、LAPIC 处理、完整的
异常帧路径（喂给崩溃转储生成）。每次启动实测 3/3（#DE/#UD/#PF）。

## Ring 3、syscall、L1 接口面

- **Syscall ABI**（`core/core_syscall.h`）：`rax`=编号，
  `rdi/rsi/rdx/r10`=参数（SYS_L1SVC 用 r8 作第 5 参），`rax`=返回
  值。54 个 syscall 编号（验证：`grep -c '#define SYS_'
  core/core_syscall.h`）。
- **服务接口面**（`services/l1/l1svc_defs.h`）：编号的 L1SVC op
  表——**17 类 / 158 op**。类别编号选 `services/<类别>/` 文件夹，
  op 选命令；ush 把 `SVC_CMDS` 行经 `SYS_L1SVC(110)` 路由到所属
  L0 服务实体。四个网络命令（ifconfig/netstat/ping/wget）改经
  `SYS_NETCMD(101)` 桥到达 L0 实体。
- **计数口径**（2.1.0，精确表述）：命令归属为 **251 个唯一名 =
  99 L0（`oc>`）+ 133 ring-3 SVC + 19 ush 内建**。ush 的
  `SVC_CMDS` 表本身有 142 行——9 个双归属名
  （rtc/hpet/pmtimer/tsc/shutdown/poweroff/reboot/halt/sleep）
  两侧都注册，两个壳都能调用。v1.0.0 基线为 233 条命令，逐条
  映射见 `INTERFACES.md` Part 1c。
- **接口沿革**（2.1.0，逐条见 `INTERFACES.md` Part 2c/2d）：v1
  接口头文件的全部 278 个可调用声明中，267 个公开存活，6 个
  register 类挂钩移入各家族的 `services/*/drv_internal.h`
  （"internal since 2.0.0"），5 个 init/probe 条目被删除——
  每个都在收集式声明服务模型里重建（`*_svc_init` /
  `*_svc_probe_all`）。
- **用户程序**（`l1apps/`）：36 个文件——ush 壳本体、
  hello/fork/exec/pipe/signal/select/mmap 测试、带真实 in-tree
  加载器（`l1apps/ld_so.c`，`dlsym`、PIE 与重定位测试）的动态
  链接——嵌入 ISO，经 `run` 运行。

## 文件系统

- **VFS**（`services/fs/fs_vfs.{c,h}`）：注册文件系统类型之上的
  统一 open/read/write/seek/close/stat/mkdir/rmdir/readdir/lookup；
  节点树透明跨挂载点；按文件系统区分大小写；权限位强制；i64
  seek。
- **文件系统**（`services/fs/`）：FAT32（LFN 别名 + 冲突扫描 +
  校验和验证、双 FAT 镜像、FSINFO）、exFAT（活动 FAT）、ext4
  （读写 + extent）、ramfs（稀疏空洞清零）。
- **块服务**（`services/block/` + `drivers/block/`）：带实例名的
  register/read/write/flush、分区扫描、AHCI、ATA/ATA-DMA、NVMe、
  virtio-blk。驱动家族自我注册（`OC_DRIVER_DECL`）；服务半边
  暴露 ring-3 面（mkfs.*、fsck、abdisk……）。
- **/etc 卷**：挂载于 /etc 的 32 MiB FAT32 映像，保存
  `opencube.conf`、SSH 主机密钥、内核日志——配置因此跨重启
  存活（`etc.img` 目标）。

## 网络栈

`services/net/net_core.{c,h}` 一族——自包含协议栈：PCI 发现、
e1000 家族网卡驱动（e1000/e1000e/igb/ixgbe；rtl8139/rtl8169 家族；
BCM57xx）、以太网 + ARP + IPv4 + ICMP + UDP + DNS + TCP（带拥塞
控制 `net_tcp_cc.c`）、用户程序 socket API、有状态防火墙、
`wget`/`dns`/`ping`/`route` 命令。其上：TLS 1.2/1.3 客户端
（RSA/ECDHE、X.509 链验证 RFC 6125 主机名策略）与真实 SSH 客户端
+ SSH 服务端（`net_sshd.c`，curve25519/ed 风格原语密钥交换，主机
密钥在 /etc）。DRBG 播种为 RDRAND + 重混 RDTSC（NIST SP 800-90A
HMAC_DRBG）。

## 驱动

`drivers/` 下 16 个家族——每个在 `docs/drivers/` 有自己的手册页。
要点：UHCI/OHCI/EHCI/xHCI USB 栈（MSC 大容量存储、HID、串口、
音频类）；HDA/AC97/SB16/ES1370/virtio 声音；i2c + SMBus；无线
（ath/brcm/iwlwifi/rtlwifi 家族）与蓝牙
（HCI/L2CAP/RFCOMM/SDP/SPP + HID）层；UVC 摄像头 MJPEG；SD/SDHCI；
SCSI（LSI 53C8xx 家族）；ACPI 带真实 AML 命名空间遍历
（FADT/DSDT/MCFG，S3 挂起/恢复于 `arch_acpi_s3*.S`）；芯片组时间源
（HPET/PM timer/TSC/CMOS RTC）。

## oc> 恢复环境

`recovery/` 是 WinRE 式 L0 壳：分词器、环境变量、别名、管道、
重定向、后台任务、通配符、命令链、cwd、脚本语言、`help -a/-w`
视图、带历史的行编辑器、文本 `editor`——**99 条命令**，全部经
文件内 `OC_SHELL_CMD` 声明注册。刻意留在这里的：内核自检、看门狗、
崩溃策略控制、内核日志读取（dmesg/logcat/logctl/crashlog/
dumpinfo/crashtest）、芯片组诊断、恢复文件命令、交互会话命令
（ssh/sshd）。

## TUI 与中文控制台（L0 常驻命令树）

`tui/`（帧缓冲之上的窗口层：带任务栏/启动器的桌面、菜单、表单、
面板、文本部件、TUI 编辑器、系统监视应用，以及 `tui/tui_ext.h`
扩展面）与 `chinese/`（16x16 位图字体、GB 控制台渲染、拼音输入法、
CN TUI 变体）**仍编入内核**（Makefile `KERNEL_DIRS`；
`CONFIG_TUI`/`CONFIG_CHINESE` 可关掉编译）——作为命令从 `oc>`
可达，并经 L1SVC 面从 ush 可达。系统字符串按契约保持 ASCII。

## OTA 与 A/B 更新

`services/ota/`：更新清单验证（SHA256 + 签名）、下载、A/B 槽位
分阶段安装（`ota_ab.{c,h}`）、回滚、GRUB 菜单消费的启动槽位标志
（槽位 ISO / A / B）。`update` 命令链跑完整流程；`ab_partition_test`
在暂存盘上演练槽位管理。

## 崩溃恢复

`core/core_crash.c` + `recovery/shell_cmds_crash.c`：panic 路径
（寄存器转储到串口 + /etc 卷上的 crashlog）、`dumpinfo` 转储检查、
访客内 `crashtest`，以及 WinRE 式恢复姿态：ring-3 启动器失败时
回退到 `oc>` 壳——恢复永远距默认 ring-3 会话只有一个 `exit`。
