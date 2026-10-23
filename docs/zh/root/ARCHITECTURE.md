<!-- GENERATED mirror of the repository-root ARCHITECTURE.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 架构

本文档描述了树中存在的 L0——下面列出的每个组件都是你可以打开的实
际文件。行数和每目录详情在 `docs/data/stats.json` 中（重新生成：
`make api-docs`）。

## 整体架构图

```
                       GRUB 2.12 (hybrid BIOS + UEFI ISO)
                                     |  multiboot2 + kernel-as-module
        +----------------------------+----------------------------+
        |                 boot/boot.S                            |
        |   32-bit entry -> GDT -> 2 MiB identity map of the      |
        |   first 4 GiB -> PAE+LME+PGE -> long mode -> kmain      |
        +----------------------------+----------------------------+
                                     |
   kernel/main.c  (kmain: bring-up order = the boot log you see)
        |
   +----+------------+------------+-------------+------------+
   |                 |            |             |            |
 kernel/arch/x86_64 kernel/core  kernel/mem   kernel/lib   kernel/crypto
   IDT/GDT/TSS      scheduler    PMM/VMM/heap string/tar  RSA/EC/AEAD/
   exceptions       syscalls 53   4 KiB pages  gzip/config  X.509/SHA
   IRQ 8259 remap   ring3+ELF    free-list     klog
   LAPIC/timers     signals      kmalloc
   context switch   soft timers
        |
   +----------------+----------------+---------------+
   |                |                |               |
 drivers/*        fs/              net/            shell/
 16 families      VFS + ramfs      e1000-family    parser/executor
 block NVME AHCI  FAT32 exFAT      ARP IPv4 ICMP   233 commands
 ATA DMA virtio   ext4             UDP DNS TCP     pipes/redirection
 USB xHCI/EHCI/   node cache       sockets TLS     env/aliases
 OHCI UHCI MSC    mount crossing   SSH client      line editor
 HID audio serial congestion       sshd server    script lang
 PCI PCIe ACPI    /etc volume      firewall        editor
 I2C SMBus HPET                    tcp cc          TUI bridge
 RTC TSC PM timer
 wireless bt SD   tui/            chinese/        l1/
 camera UVC IR    desktop,        CN console      162-numbered
 gameport parallel menus, forms,  font 16x16      extension surface
 SCSI LSI 53C8xx  editor, apps    pinyin IME      + solib loader
```

## 启动流程（串口日志显示的顺序）

1. GRUB 加载 `boot/opencube.elf`（内核也作为标记为 `self` 的 multiboot2 模块附加——
   这是安装程序的模式，因此 `install`/`abdisk` 可以将*正在运行的*确切内核复制到磁盘）。
2. `boot/boot.S`：32 位入口点，独立的 GDT，使用 2 MiB 页面对前 4 GiB 进行
   身份映射，PAE + LME + PGE，切换到 64 位长模式，调用 `kmain`。
3. `kernel/arch/x86_64/arch_multiboot2.c` 解析 multiboot2 信息
   （帧缓冲、内存映射、命令行、ACPI RSDP 标签）——具有完整的畸形输入
   防御：每标签最小值、NUL 终止检查、截断标签拒绝。
4. `kmain` 按启动日志顺序点亮：帧缓冲控制台与字体，
   IDT/GDT/TSS（256 个门，#DF/#MC 和调试的 IST 栈），
   8259 PIC 重映射，100 Hz 的 PIT，PS/2 键盘，COM1 串口 RX，
   行编辑器，异常（带有实时的 #DE/#UD/#PF 自检），
   PMM，VMM，堆，然后是驱动域，VFS，网络，Shell。
5. Shell 注册其 233 个命令（`shell command system (233
   commands)` 行），崩溃恢复启用，`oc>` 提示符出现。

## 内存管理

- **PMM** (`kernel/mem/mem_pmm.{c,h}`)：基于 multiboot2 内存映射的 4 KiB
  页框位图分配器；保留区域（内核
  代码、帧缓冲、mbi）在初始化时标记为已使用；O(1) 分配/释放。
- **VMM** (`kernel/mem/mem_vmm.{c,h}`)：4 级页表；
  内核保持启动时前 4 GiB 的身份映射；API
  创建并操作额外的地址空间（用户进程）。
  页面保护标志映射到 rw/nx/user 位。
- **Heap** (`kernel/mem/mem_heap.{c,h}`)：kmalloc/kfree/krealloc/
  kzalloc，带有首次适应和合并的空闲链表，16 字节
  对齐的块（历史上 40 字节与 32 字节头部的错误教会了我们惨痛的教训），
  16 MiB 快速失败上限。
- **#PF 增长**：栈增长和堆增长的缺页异常是 `kmain` 异常路径中的
  真实故障处理——非法访问升级到崩溃路径。

## 调度与时间

- **调度器** (`kernel/core/core_sched.{c,h}`)：抢占式，
  基于任务控制块进行优先级 + 时间片轮转；
  上下文切换在 `arch/x86_64/arch_context_switch.S` 系列汇编中进行，
  并通过 ABI 探测验证对齐。
- **定时器** (`kernel/core/core_timer.{c,h}`)：100 Hz 的 PIT 驱动
  节拍，`g_boot_time_ms` 和软定时器轮。
  HPET、ACPI PM 定时器和 TSC 作为*额外*时间源受支持
  (`drivers/time/`)。
- **看门狗** (`kernel/core/core_watchdog.{c,h}`)：监控任务
  槽位与超时策略（`watchdog` 命令），加上真正的
  硬件看门狗点亮（如果固件提供）——Intel TCO
 （在 QEMU q35 上实时验证：PMBASE 发现，启用状态，
  真正的硬重启 + 二次启动）和 ACPI WDAT 表。

## 中断与异常

`kernel/arch/x86_64/arch_idt.{c,h}` + `arch_irq.c`：256 项 IDT，
带有 IST1（#DF/#MC 的 panic 栈）和 IST2（专用
 异常退出栈——在 IST2 上从不发生上下文切换）的 TSS，
 8259 重映射到向量 32-47，LAPIC 处理，以及完整的
 异常帧路径，用于生成崩溃转储。每次启动都运行实时的
 3/3（#DE/#UD/#PF）自检。

## Ring 3、系统调用、进程

- **系统调用 ABI** (`kernel/core/core_syscall.h`)：`rax`=编号，
  `rdi/rsi/rdx/r10`=参数，`rax`=结果。53 个系统调用编号
  （验证：`grep -c '#define SYS_' kernel/core/core_syscall.h`）。
- **用户模式** (`kernel/core/core_usermode.{c,h}`)：ELF 加载器，
  进程表，信号，ring-3 入口/fork 汇编。
- **用户程序** (`userprogs/`)：hello，fork/exec/pipe/signal/
  select/mmap 测试，使用真正的树内加载器进行动态链接
  (`libs/ld_so.c`)——共享对象、`dlsym`、PIE 和重定位
  测试都作为 shell 命令运行。

## 文件系统

- **VFS** (`fs/fs_vfs.{c,h}`)：统一的 open/read/write/seek/close/
  stat/mkdir/rmdir/readdir/lookup 接口，适用于已注册的文件系统类型；
  节点树透明地跨越挂载点；每个文件系统的大小写敏感性；
  权限位强制执行；i64 寻址。
- **文件系统**：FAT32（LFN 与别名 + 冲突扫描 + 校验和
  验证，双 FAT 镜像，FSINFO），exFAT（活动 FAT），
  ext4（读/写，支持扩展），ramfs（稀疏洞归零）。
- **块层** (`drivers/block/driver_block_blk.{c,h}`)：注册/
  读/写/刷新，支持每个实例的名称，分区扫描，
  AHCI，ATA/ATA-DMA（32 字节堆头保证 PRDT 对齐），
  NVMe，virtio-blk。
- **/etc 卷**：挂载在 /etc 的 32 MiB FAT32 镜像，
  包含 `opencube.conf`、SSH 主机密钥、内核日志——
  因此配置在重启后仍然保留（`etc.img` 目标）。

## 网络协议栈

`net/net_core.{c,h}` 及相关模块——一个自包含的协议栈：PCI
 发现，e1000 系列网卡驱动（e1000/e1000e/igb/ixgbe；rtl8139/
 rtl8169 系列；BCM57xx），Ethernet + ARP + IPv4 + ICMP + UDP + DNS +
 TCP 及拥塞控制（`net_tcp_cc.c`），用户程序的
 套接字 API，状态防火墙，`wget`/`dns`/`ping`/`route` 命令。
 在其之上：支持 RSA/ECDHE、X.509 链验证（RFC 6125 主机名策略）
 的 TLS 1.2/1.3 客户端，以及真正的 SSH 客户端 + SSH 服务器
  (`net_sshd.c`，通过 curve25519/ed-style 原语进行密钥交换，
  主机密钥在 /etc)。DRBG 播种是 RDRAND +
  重新混合的 RDTSC（NIST SP 800-90A HMAC_DRBG）。

## 驱动

`drivers/` 下的 16 个驱动家族——每个在 `docs/drivers/` 下都有
 自己的点亮手册。亮点：UHCI/OHCI/EHCI/xHCI USB 栈，
 支持 MSC（大容量存储）、HID、串口、音频类；
 HDA/AC97/SB16/ES1370/virtio 声卡；i2c + SMBus；
 无线（ath/brcm/iwlwifi/rtlwifi 系列）和蓝牙（HCI/L2CAP/RFCOMM/SDP/SPP + HID）层；
 支持 MJPEG 的 UVC 摄像头；SD/SDHCI；SCSI（LSI 53C8xx 系列）；
 带有真正 AML 命名空间遍历器的 ACPI（FADT/DSDT/MCFG，
 S3 挂起/恢复在 `arch_acpi_s3*.S` 中）；芯片组时间源
 （HPET/PM 定时器/TSC/CMOS RTC）。

## Shell 与 TUI

- **Shell** (`shell/`)：分词器，环境变量，别名，管道，
  重定向，后台作业，通配符，命令链
  (`;` `&&` `||`)，当前工作目录，脚本语言，
  `help -a/-w` 两种视图，233 个命令，带历史记录的
  行编辑器，文本 `editor`。
- **TUI** (`tui/`)：在帧缓冲之上的窗口层——
  带有任务栏/启动器的桌面，菜单，表单，面板，文本控件，
  TUI 编辑器，系统监控应用，以及扩展接口面
  `tui/tui_ext.h`（37 个可调用声明），L1 模块使用。

## 中文控制台层

`chinese/` 添加了 16x16 位图字体，GB 编码的控制台渲染，
拼音输入法，以及 TUI 硬件/网络/系统信息应用的中文变体——
 可在构建时选择（`CONFIG_CHINESE`），禁用时使用
 ASCII 等价回退。系统字符串按约定保持 ASCII。

## L1 扩展接口面

编号的 162 接口面（参见 `INTERFACES.md`）加上驱动/OTA/看门狗/健康/内核日志头文件；
共享对象加载器（`libs/ld_so.c`）让 L1 模块在运行时加载。
TUI 命令桥端到端地演示了该模式。

## OTA 与 A/B 更新

`kernel/ota/`：更新清单验证（SHA256 + 签名），
下载，分阶段安装到 A/B 槽位（`ota_ab.{c,h}`），
回滚，GRUB 菜单消耗的启动槽位标志（槽位 ISO / A / B）。
`update` 命令链运行整个流程；`ab_partition_test`
 在临时磁盘上练习槽位管理。

## 崩溃恢复

`kernel/core/core_crash.c` + `shell/shell_cmds_crash.c`：panic 路径，
将寄存器转储到串口 + /etc 卷上的崩溃日志，
通过 `dumpinfo` 检查转储，客户机内的 `crashtest`，
以及 WinRE 风格的恢复姿态：系统默认启动到 `oc>` shell，
因此"恢复"是主要接口，而不是特殊模式。
