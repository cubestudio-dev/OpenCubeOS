<!-- GENERATED mirror of the repository-root README.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**一个从零写起的 x86_64 内核，附带 WinRE 风格的恢复 Shell。**

当前发布：**v2.1.0** —— 架在 2.0.0 Ring-3 重构之上的 17 项问题修复版：
CI 重新全绿（并升格为硬性发布门禁）、文档与接口真相逐条化（v1 233
条命令映射补全、278 条接口声明的逐条对照、如实申报的删除清单）、
命令参考全量重生成、网站数据完整性（见
[Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)）。

`oc>` 是回退提示符，`ush>` 是默认提示符。一切皆可扩展。

[官网](https://cubestudio-dev.github.io/OpenCubeOS/) ·
[文档](https://cubestudio-dev.github.io/OpenCubeOS/docs/) ·
[下载](https://cubestudio-dev.github.io/OpenCubeOS/#downloads) ·
[构建](BUILDING.md) ·
[架构](ARCHITECTURE.md) ·
[接口参考](INTERFACES.md)

</div>

---

## 它是什么

Open Cube OS 是 **L0 + L1**：一个完整的、自包含的 64 位内核，可在
真实硬件和 QEMU 中启动，拉起默认的 Ring-3 应用（ush 用户 Shell，
横跨 L0 恢复 Shell 与 L1 服务面的 **251 条命令**），并在 Ring-3 世界
无法启动时回退到恢复 Shell 提示符（`oc>`）。

- **不是 Linux 分支。** Apache 2.0 下的 130,526 行原创 C/汇编
  （验证：见下文 *Stats*）。
- **PC 平台能启动的地方都能启动：** BIOS + UEFI 混合 ISO，512 MiB
  内存即可舒适运行，磁盘可选。
- **一个真正的内核：** 抢占式调度器、4 级分页、带 54 个系统调用的
  Ring-3 用户程序、FAT32/exFAT/ext4/ramfs 上的 VFS、包含 TLS 1.2/1.3
  和 SSH（客户端 *与* 服务器）的完整 TCP/IP 协议栈、从 NVMe 到 xHCI
  再到 ACPI AML 的 16 个驱动家族、崩溃转储、硬件看门狗
  （Intel TCO / ACPI WDAT）、OTA A/B 更新。
- **设计即恢复环境：** 系统*本身*就是 WinRE 式环境——启动、诊断、
  修复、安装、更新。

两层架构：

```
L0  Open Cube OS        内核 + oc> 恢复 Shell                <- 本仓库
L1  Ring-3 服务        17 个 SYS_L1SVC 类别 / 158 个操作 + ush
```

2.0.0 起 L1 侧就在树内：`l1apps/` 收纳 Ring-3 应用（ush——默认启动
项，加上 ld.so 与动态链接测试程序），每个操作性命令类都移到了
SYS_L1SVC 服务面之后
（[INTERFACES.md](INTERFACES.md)——v1→v2 映射表就是兼容性契约；
2.1.0 将其补全到全部 233 条基线命令，零遗漏）。

## 亮点

| 领域 | 你能得到什么 |
|------|--------------|
| 启动 | GRUB 2.12 混合 ISO（BIOS El Torito + UEFI 映像）、multiboot2 附带 `self` 内核模块、A/B 槽位菜单 |
| 内存 | 基于 multiboot2 内存图的 PMM 位图、4 级 VMM、经 #PF 真实增长的 kmalloc 堆 |
| CPU | 抢占式优先级 + 轮转调度器、带 IST 栈的 256 门 IDT、100 Hz PIT + HPET/PM-timer/TSC 时间源、LAPIC |
| Ring 3 | ELF 加载器、54 个系统调用、信号、fork/exec/pipes/select/mmap、动态链接（树内 ld.so、PIE、dlsym）；SYS_L1SVC + SYS_NETCMD 服务桥 |
| 存储 | AHCI、ATA/ATA-DMA、NVMe、virtio-blk、USB MSC、SCSI（LSI）；支持 LFN 的 FAT32、exFAT、ext4、ramfs；GPT/MBR 分区；A/B 槽位 |
| 网络 | e1000/e1000e/igb/ixgbe/rtl8139/rtl8169 家族/BCM57xx 驱动、ARP/IPv4/ICMP/UDP/DNS/TCP（含拥塞控制）、套接字、防火墙、HTTPS 客户端（TLS 1.2/1.3 + X.509 链）、SSH 客户端 + sshd |
| USB | UHCI、OHCI、EHCI、xHCI；大容量存储、HID、串口、音频类 |
| 媒体 | HDA / AC97 / SB16 / ES1370 / virtio 声音；MJPEG 的 UVC 摄像头 |
| 平台 | ACPI（FADT/DSDT/MCFG、真实 AML 遍历器、S3 挂起/恢复）、Intel TCO + ACPI WDAT 硬件看门狗、HPET、i2c/SMBus |
| Shell | ush（Ring 3，默认启动项）：133 条服务命令 + 内建（管道/重定向/通配符/环境变量/别名/作业）；oc> 恢复 Shell：99 条命令 |
| TUI | 桌面、任务栏、菜单、表单、编辑器、系统应用——外加中文控制台层（16x16 字体、拼音输入法） |
| 可靠性 | 崩溃转储到串口 + /etc crashlog、带轮转/级别/过滤的内核日志、健康监视（CPU/内存/任务/IO + JSON 导出）、SHA256 + 签名验证并带回滚的 OTA |

## 快速上手

```
# 构建（Linux）：需要 gcc nasm xorriso mtools grub-common
#               grub-pc-bin grub-efi-amd64-bin zip python3
./build.sh iso

# 运行
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1

# 访客内部（默认启动项是 Ring-3 的 ush Shell）
ush> help
ush> dhcp
ush> ping 10.0.2.2
ush> wget 10.0.2.2
ush> uname -a
ush> exit          # -> 落入 oc> 恢复 Shell（99 条命令）
```

UEFI 而非 BIOS：`make run-uefi`。Windows 宿主：`build.ps1`
（WSL2/MSYS2）。完整指南：[BUILDING.md](BUILDING.md)。

## 下载

每个发布都从同一字节可复现流水线产出三份资产——当前发布永远在
[GitHub releases 页](https://github.com/cubestudio-dev/OpenCubeOS/releases)
并镜像于[官网下载页](https://cubestudio-dev.github.io/OpenCubeOS/)：

| 资产 | 它是什么 |
|------|----------|
| `opencube-2.1.0.iso` | BIOS+UEFI 混合可启动 ISO——QEMU 启动或写入 U 盘 |
| `opencube-2.1.0-src.zip` | 确定性源码 zip（`git archive`） |
| `opencube-2.1.0-update.tar.gz` | OTA 更新包（SHA256 + 签名验证，A/B 槽位） |

每份发布说明附全部三个 SHA256；官网、Release 与私密镜像持有字节
一致的副本。

## 文档

- [BUILDING.md](BUILDING.md) - Linux/Windows 构建、QEMU、交叉编译
  （ARM64/RISC-V Shell）、可复现验证
- [ARCHITECTURE.md](ARCHITECTURE.md) - L0 架构，自顶向下
- [INTERFACES.md](INTERFACES.md) - 按归属划分的 251 条命令面、
  v1→v2 映射与 L1SVC 服务表（由树生成：`make api-docs`）
- [FAQ.md](FAQ.md) - 常见问题
- [CONTRIBUTING.md](CONTRIBUTING.md) - 基本规则与 PR 门槛
- 完整文档中心（用户手册、开发者手册、驱动上手指南、L1 扩展指南、
  内部机制）：**https://cubestudio-dev.github.io/OpenCubeOS/docs/**

## Stats

以下数字全部在构建时由机器验证
（`make api-docs` 写入 `docs/data/*.json`；每项均附 README 验证命令）：

- **源代码**：379 个文件共 130,526 行——core + arch + drivers +
  services + recovery + tui + chinese + boot + l1apps，含头文件，
  排除 docs/tools/tests 与生成的数据块。验证：
  `git ls-files | grep -E '\.(c|h|S)$' | grep -E '^(core|arch|drivers|services|recovery|tui|chinese|boot|l1apps)/' | grep -v '^core/generated/' | grep -vE 'userprogs_data|chinese_font_data|pinyin_data|grub_boot_data' | xargs wc -l`
- **命令**：共 251 条——L0 恢复 Shell 99 条（`oc>` 提示符下
  `help -a`）+ Ring-3 SYS_L1SVC 服务命令 133 条 + ush 内建 19 条。
  v1.0.0 的每条命令都有映射——见
  [INTERFACES.md](INTERFACES.md) 的 v1→v2 表（233 条基线全映射）。
- **L1SVC 服务面**：17 个类别 / 158 个编号操作——精确表格见
  [INTERFACES.md](INTERFACES.md)。
- **系统调用**：54 个 - 验证：`grep -c '#define SYS_' core/core_syscall.h`
- **CI**：每次 push 全绿——构建、ISO、打包、host 测试与可复现门
  （`.github/workflows/build.yml`）。

## 许可

Apache 2.0（见 [LICENSE](LICENSE)）。版权所有 2026
cubestudio-dev &lt;cubestudio@qq.com&gt;。
