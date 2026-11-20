<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**一个从零写起、直接启动进 Ring 3 的 x86_64 内核。**

当前发布：**3.0.0-WP-tui-input-encoding-hardening**——OCBL
引导加载器、纯 L0 服务内核（L0 里没有 shell、没有 TUI、没有
IME）、`ocos/l1apps/` 下的 **300 个 Ring-3 程序**——现在围绕
**8 个合并大应用**组织（`update` / `disk-tool` / `net-tool` /
`sound-tool` / `files` / `sysdiag` / `hwinfo` / `power`），每一个
既是意图向导，*又*是其下每条经典命令的逐字分发。稳定线：
**2.2.0**（2.x 世界的最后一个正式发布，留作参考与真机
稳定性）。见
[Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)。

`ush>` 是提示符。一切皆可扩展。

[官网](https://ocos.cubestudio.cn/) ·
[文档](https://ocos.cubestudio.cn/docs/) ·
[下载](#downloads) ·
[构建](BUILDING.md) ·
[架构](ARCHITECTURE.md) ·
[接口参考](INTERFACES.md)

</div>

---

## 它是什么

Open Cube OS 是 **L0 + L1**：

- **L0 提供服务。** 纯服务内核——启动链、核心机制、驱动、
  根服务和编号接口。3.0.0-WP03 彻底删除了 L0 用户面（2.x
  的 `oc>` 恢复 shell、内核内 TUI、输入法引擎）；L0 在
  `L0 ready, waiting for L1` 完成启动，把机器交给默认的
  Ring-3 入口。
- **L1 使用服务。** `ocos/l1apps/` 下的 300 个 Ring-3 程序
  （一条命令 = 一个应用 = 一个文件夹）：默认启动项是 `ush`
  用户 shell（**311 条命令 = 300 个注册表应用 + 11 个
  内建，L0 里零条命令**）、TUI 桌面、编辑器、中文控制台 +
  拼音 IME——以及给整个命令面装上按意图驱动前门的 8 个
  合并大应用：带自动检测与跨域修复的无参向导（`update`
  向导在真正检查更新之前，先通过 DHCP 修复缺失的 IP、
  通过 `abdisk` 修复缺失的 A/B 磁盘）、`direct` 显式透传，
  以及零功能损失的经典逐字分发。

- **不是 Linux 分支。** Apache 2.0 下横跨 764 个文件的
  164,693 行原创 C/汇编（验证：见下文*数据*）。
- **PC 平台走到哪就启动到哪：** 经 OCBL 的单一混合 BIOS +
  UEFI ISO——树内引导加载器（512 字节实模式 stage 1 /
  EFI 入口、16→32→64 位长模式、动态磁盘扫描）——512 MiB
  内存即可舒适运行，磁盘可选。
- **一个真正的内核：** 抢占式调度器、4 级分页、带 54 个
  系统调用的 Ring-3 用户程序、FAT32/exFAT/ext4/ramfs 之上
  的 VFS、带 TLS 1.2/1.3 和 SSH（客户端*和*服务器）的完整
  TCP/IP 协议栈、从 NVMe 到 xHCI 再到 ACPI AML 的 15 个
  驱动家族、崩溃转储、硬件看门狗（Intel TCO / ACPI
  WDAT）、OTA A/B 更新，以及 WP07a 启动状态监视器：六个
  启动阶段记录进一个固定状态区、逐阶段看门狗预算，外加
  一条重启后由 OCIRE 读取、用于打印停滞点与修复建议的
  BOOT_FAILED 事后记录。
- **设计上诚实：** 若 Ring-3 世界无法启动，L0 会明说并
  停机——指引就是重启或 OCBL 启动菜单（故意不设可供
  回退的 L0 shell）。

两层架构：

```
L0  OCOS (ocos/)          kernel + drivers + root services + the
                          numbered L1SVC ABI - no shell, no TUI
L1  the world (ocos/l1apps/)  300 programs: ush, TUI, editor, IME,
                          one command per app, and the 8 merged
                          mega-apps (311 commands total)
```

## 亮点

| 领域 | 你得到什么 |
|------|--------------|
| 启动 | OCBL——树内引导加载器：混合 BIOS + UEFI ISO、16→32→64 位长模式、动态磁盘扫描、带 10 秒倒计时的启动菜单（3.0.0 链中没有 GRUB） |
| 内存 | 引导内存映射之上的 PMM 位图、4 级 VMM、经 #PF 真实增长的 kmalloc 堆 |
| CPU | 抢占式优先级 + 轮转调度器、带 IST 栈的 256 门 IDT、100 Hz PIT + HPET/PM-timer/TSC 时源、LAPIC |
| Ring 3 | ELF 加载器、54 个系统调用、信号、fork/exec/管道/select/mmap、动态链接（树内 ld.so、PIE、dlsym）；SYS_L1SVC + SYS_NETCMD 服务桥 |
| 存储 | AHCI、ATA/ATA-DMA、NVMe、virtio-blk、USB MSC、SCSI（LSI）；带 LFN 的 FAT32、exFAT、ext4、ramfs；GPT/MBR 分区；A/B 槽位 |
| 网络 | e1000/e1000e/igb/ixgbe/rtl8139/rtl8169 家族/BCM57xx 驱动、带拥塞控制的 ARP/IPv4/ICMP/UDP/DNS/TCP、套接字、防火墙、HTTPS 客户端（TLS 1.2/1.3 + X.509 链）、SSH 客户端 + sshd |
| USB | UHCI、OHCI、EHCI、xHCI；大容量存储、HID、串口、音频类 |
| 媒体 | HDA / AC97 / SB16 / ES1370 / virtio sound；带 MJPEG 的 UVC 摄像头 |
| 平台 | ACPI（FADT/DSDT/MCFG、真实 AML 遍历器、S3 挂起/恢复）、Intel TCO + ACPI WDAT 硬件看门狗、HPET、i2c/SMBus |
| Shell | ush（Ring 3，默认启动项）：300 个注册表应用 + 11 个内建，带管道/重定向/环境变量/别名/作业；L0 里零条命令 |
| 大应用 | 8 个合并应用（`update`、`disk-tool`、`net-tool`、`sound-tool`、`files`、`sysdiag`、`hwinfo`、`power`）：带跨域修复的意图向导 + `direct` 透传 + 经典逐字分发；破坏性路径由重输 + y/N 双重确认防护 |
| TUI | 桌面、任务栏、菜单、表单、编辑器、系统应用——依托 L1SVC tui 服务的 Ring-3 程序，外加中文控制台层（16x16 字体、拼音 IME） |
| 可靠性 | 崩溃转储到串口 + /etc crashlog、带轮转 + 级别 + 过滤的内核日志、健康监视器（CPU/内存/任务/IO + JSON 导出）、带 SHA256 + 签名与回滚的 OTA |
| OCBL | 树内引导加载器（`ocbl/`）：BIOS stage 1（El Torito / MBR）+ UEFI 入口、启动菜单、安装器目标（`ocbl-install`） |
| OCIRE | 独立恢复内核（`ocire/`）：与 OCOS 零代码共享的第二个 L0，从自己的 ISO 独立启动，把镜像存放在 OCOS 无法读写的隔离分区（见 `docs/dev/ocire.md`） |

## 快速开始

```
# build (Linux): needs gcc nasm xorriso mtools grub-common
#               grub-pc-bin grub-efi-amd64-bin zip python3
tools/build.sh iso

# run it
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1

# inside the guest (the default boot entry is the ring-3 ush shell)
ush> hwinfo            (wizard: what hardware is this machine?)
ush> update            (wizard: network -> A/B -> DNS -> check -> install)
ush> help -a           (all 300 registry programs)
ush> dhcp
ush> ping 10.0.2.2
ush> lsblk             (or: disk-tool, and pick it from the menu)
```

用 UEFI 而非 BIOS：`make run-uefi`。Windows 宿主机：
`build.ps1`（WSL2/MSYS2）。完整指南：[BUILDING.md](BUILDING.md)。

## 下载

两个频道出自同一条字节可复现流水线——都在
[GitHub releases 页](https://github.com/cubestudio-dev/OpenCubeOS/releases)
并镜像在[网站下载页](https://ocos.cubestudio.cn/download.html)：

| 频道 | 资产 |
|---------|--------|
| **development preview** | `opencube-<version>.iso`（启动 OCBL → OCOS → ush） |
| **2.2.0**（stable，2.x 线） | `opencube-2.2.0.iso` · `opencube-2.2.0-src.zip`（确定性源码）· `opencube-2.2.0-update.tar.gz`（OTA） |

每份发布说明携带每个资产的 SHA256；网站、
Release 与私有镜像持有逐字节一致的副本。

## 文档

- [BUILDING.md](BUILDING.md)——在 Linux/Windows 上构建、QEMU、
  交叉编译（ARM64/RISC-V shell）、可复现验证
- [ARCHITECTURE.md](ARCHITECTURE.md)——L0/L1 架构，自顶向下
- [INTERFACES.md](INTERFACES.md)——按归属划分的命令面和 L1SVC 服务表
  （从树生成：`make api-docs`）
- [FAQ.md](FAQ.md)——常见问题
- [CONTRIBUTING.md](CONTRIBUTING.md)——基本规则与 PR 门槛
- 完整文档中心（带大应用
  指南的用户手册、开发者手册、驱动上线指南、内部机制）：
  **https://ocos.cubestudio.cn/docs/**

## 数据

下列数字全部在构建时经机器验证
（`make api-docs` 写 `docs/data/*.json`；每项都引用了 README
验证命令）：

- **源代码**：814 个文件 164,693 行——ocos/（内核、
  驱动、服务、接口、l1apps）+ ocbl/ + ocire/，含头文件、
  排除生成的数据块（生成器口径：
  `tools/gen_api_docs.py` 的 `STAT_DIRS`/`STAT_EXCLUDE`）。
  验证：
  `python3 tools/gen_api_docs.py && python3 -c "import json;d=json.load(open('docs/data/stats.json'));print(sum(v['lines'] for v in d['dirs'].values()))"`
- **命令**：共 311 条——300 个 Ring-3 注册表应用（一条命令 =
  一个应用 = `ocos/l1apps/` 下一个文件夹，含 8 个合并
  大应用）+ 12 个 ush 内建 + L0 里 0 条。注册表下限由回归
  门禁强制（>= 276）。
- **L1SVC 服务面**：20 个类别 / 221 个编号操作——精确
  表格见 [INTERFACES.md](INTERFACES.md)。
- **系统调用**：54 个——验证：
  `grep -c '#define SYS_' ocos/services/sysapi/interface/svc_sysapi_syscall.h`
- **CI**：每次推送都绿——构建、ISO、打包、宿主机测试和
  可复现性门禁
  （`.github/workflows/build.yml`）。

## 许可

Apache 2.0——见 [LICENSE](LICENSE)。每个源文件携带
`SPDX-License-Identifier: Apache-2.0`。

Copyright 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;
