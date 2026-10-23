<!-- GENERATED mirror of the repository-root README.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**一个从零写起的 x86_64 内核，附带 WinRE 风格的恢复 Shell。**

`oc>` 是提示符。一切皆可扩展。

[官网](https://cubestudio-dev.github.io/OpenCubeOS/) ·
[文档](https://cubestudio-dev.github.io/OpenCubeOS/docs/) ·
[下载](#downloads) ·
[构建](BUILDING.md) ·
[架构](ARCHITECTURE.md) ·
[接口参考](INTERFACES.md)

</div>

---

## 是什么

Open Cube OS 是 **L0**：一个完整的、自包含的 64 位内核，可在真实硬件和 QEMU 中启动，并为你提供一个包含 **233 条命令**的恢复 Shell 提示符（`oc>`），用于检查和控制机器。

- **不是 Linux 的分支。** 140,672 行原创 C/汇编代码，遵循 Apache 2.0 许可（验证：见下方的 *统计*）。
- **能在 PC 平台的所有地方启动：** 混合 BIOS + UEFI ISO，512 MiB RAM 即可流畅运行，磁盘为可选。
- **一个真正的内核：** 抢占式调度器、4 级分页、ring-3 用户程序与 53 个系统调用、FAT32/exFAT/ext4/ramfs 上的 VFS、包含 TLS 1.2/1.3 和 SSH（客户端 *和* 服务器）的完整 TCP/IP 协议栈、从 NVMe 到 xHCI 再到 ACPI AML 的 16 个驱动家族、崩溃转储、硬件看门狗（Intel TCO / ACPI WDAT）、OTA A/B 更新。
- **按设计实现的恢复 Shell：** 系统 *就是* WinRE 风格的环境——启动、诊断、修复、安装、更新。

双层架构：

```
L0  Open Cube OS        kernel + oc> recovery shell     <- this repo
L1  upper extensions    built on the 162-interface surface
```

L1 不随 L0 一起发布；扩展接口面
([INTERFACES.md](INTERFACES.md)) 是契约，而共享对象加载器使 L1 模块可在运行时加载。

## 主要亮点

| 领域 | 提供的功能 |
|------|------------|
| 启动 | GRUB 2.12 混合 ISO（BIOS El Torito + UEFI 镜像），multiboot2，内核作为 `self` 模块附加，A/B 槽位菜单 |
| 内存 | 基于 multiboot2 映射的 PMM 位图，4 级 VMM，支持通过 #PF 实际增长的 kmalloc 堆 |
| CPU | 抢占式优先级 + 轮转调度器，带 IST 栈的 256 门 IDT，100 Hz PIT + HPET/PM-timer/TSC 源，LAPIC |
| Ring 3 | ELF 加载器，53 个系统调用，信号，fork/exec/pipes/select/mmap，动态链接（内置 ld.so，PIE，dlsym） |
| 存储 | AHCI，ATA/ATA-DMA，NVMe，virtio-blk，USB MSC，SCSI（LSI）；支持 LFN 的 FAT32，exFAT，ext4，ramfs；GPT/MBR 分区；A/B 槽位 |
| 网络 | e1000/e1000e/igb/ixgbe/rtl8139/rtl8169-family/BCM57xx 驱动，ARP/IPv4/ICMP/UDP/DNS/TCP 与拥塞控制，套接字，防火墙，HTTPS 客户端（TLS 1.2/1.3 + X.509 链），SSH 客户端 + sshd |
| USB | UHCI，OHCI，EHCI，xHCI；海量存储、HID、串行、音频类设备 |
| 媒体 | HDA / AC97 / SB16 / ES1370 / virtio 声卡；支持 MJPEG 的 UVC 摄像头 |
| 平台 | ACPI（FADT/DSDT/MCFG，真实 AML 解析器，S3 挂起/恢复），Intel TCO + ACPI WDAT 硬件看门狗，HPET，i2c/SMBus |
| Shell | 233 条命令，管道/重定向/通配符/环境变量/别名/脚本，两种视图的帮助（`-a`，`-w`） |
| TUI | 桌面、任务栏、菜单、表单、编辑器、系统应用——外加一个中文控制台层（16x16 字体，拼音输入法） |
| 可靠性 | 串口 + /etc crashlog 的崩溃转储，带轮转/级别/过滤器的内核日志，健康监控（CPU/内存/任务/IO + JSON 导出），带 SHA256 + 签名和回滚的 OTA |

## 快速上手

```
# build (Linux): needs gcc nasm xorriso mtools grub-common
#               grub-pc-bin grub-efi-amd64-bin zip python3
./build.sh iso

# run it
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1

# inside the guest
oc> help -a
oc> dhcp
oc> ping 10.0.2.2
oc> wget 10.0.2.2
oc> uname -a
```

使用 UEFI 而非 BIOS：`make run-uefi`。Windows 主机：`build.ps1`
（WSL2/MSYS2）。完整指南：[BUILDING.md](BUILDING.md)。

## 下载

每个发布版都从相同的字节可复现管道中生成三个资产——当前发布版始终在
[GitHub 发布页面](https://github.com/cubestudio-dev/OpenCubeOS/releases)
和
[官网下载页面](https://cubestudio-dev.github.io/OpenCubeOS/)
上镜像：

| 资产 | 说明 |
|-------|------|
| `opencube-<version>.iso` | 混合 BIOS+UEFI 可启动 ISO - 在 QEMU 中启动或写入 USB |
| `opencube-<version>-src.zip` | 确定性源码包（`git archive`） |
| `opencube-<version>-update.tar.gz` | OTA 更新包（SHA256 + 签名验证，A/B 槽位） |

每个发布说明都包含这三个文件的 SHA256；官网、发布版和私有镜像持有完全相同的字节副本。

## 文档

- [BUILDING.md](BUILDING.md) - 在 Linux/Windows 上构建，QEMU，
  交叉编译（ARM64/RISC-V Shell），可复现性验证
- [ARCHITECTURE.md](ARCHITECTURE.md) - L0 架构，从头到尾
- [INTERFACES.md](INTERFACES.md) - 233 条命令和 L1 接口面
 （从源树生成：`make api-docs`）
- [FAQ.md](FAQ.md) - 常见问题
- [CONTRIBUTING.md](CONTRIBUTING.md) - 基本规则和 PR 合入门槛
- 完整文档中心（用户手册、开发者手册、
  驱动点亮手册、L1 扩展指南、内部机制）：
  **https://cubestudio-dev.github.io/OpenCubeOS/docs/**

## 统计

以下所有数字都在构建时通过机器验证
（`make api-docs` 写入 `docs/data/*.json`；README 验证命令按项目引用）：

- **源代码**：140,672 行 - 内核 + 启动 + 用户程序 + 文件系统 +
  网络 + Shell + l1 + 驱动 + 库 + TUI + 中文 + 架构，包含头文件，
  排除 docs/tools/tests 和生成的数据块。
  验证：
  `git ls-files | grep -E '\.(c|h|S)$' | grep -E '^(kernel|boot|userprogs|fs|net|shell|l1|drivers|libs|tui|chinese|arch)/' | grep -vE 'userprobs_data|chinese_font_data|pinyin_data|grub_boot_data' | xargs wc -l`
- **Shell 命令**：233 - 在访客系统中通过 `help -a` 实时验证。
- **L1 扩展接口**：162 个编号（57 WP-09 + 8 + 13 + 7 +
  26 + 11 + 7 + 9 + 8 + 16），外加文档化的未编号组 -
  完整接口面见 [INTERFACES.md](INTERFACES.md)。
- **系统调用**：53 - 验证：
  `grep -c '#define SYS_' kernel/core/core_syscall.h`
- **CI**：每次推送都通过 - 构建、ISO、包、主机测试和
  可复现性门禁
  (`.github/workflows/build.yml`)。

## 许可证

Apache 2.0 - 见 [LICENSE](LICENSE.md)。每个源文件都带有
`SPDX-License-Identifier: Apache-2.0`。

版权所有 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;
