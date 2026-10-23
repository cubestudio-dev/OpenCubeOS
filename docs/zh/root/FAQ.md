<!-- GENERATED mirror of the repository-root FAQ.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# 常见问题（FAQ）

常见问题，基于真实代码库回答。
当引用限制时，定义该限制的头文件名称会一并给出。

## 项目

**Open Cube OS 是什么？**
一个从零编写的 x86_64 内核（"L0"）加上一个 WinRE 风格的恢复 Shell
（`oc>` 提示符）。它从混合 BIOS+UEFI ISO 启动，点亮自己的
内存管理、调度器、VFS、网络协议栈和驱动程序，并为您提供
233 条 Shell 命令来检查和控制机器。

**这个 "L0 / L1" 是什么？**
两层架构：

- **L0** = 本仓库：完整的内核加上恢复 Shell。
- **L1** = 上层扩展。L0 暴露了 162 个编号的扩展
  接口（以及一组文档化的驱动/OTA/看门狗/健康
  头文件）；L1 模块不随 L0 一起发布。

设计原则是"一切都是可扩展的"——Shell、TUI、
驱动程序甚至控制台字体引擎都是扩展接口面。

**它是 Linux 吗？**
不是。不是 fork，也不是发行版。内核的每一行（跨越
kernel/boot/drivers/fs/net/shell/l1/tui/chinese/arch/userprogs/libs 的
140,672 行）都是 Apache 2.0 下的原创代码。它不运行 Linux 二进制文件；
它运行自己的小型 ELF 用户程序（参见 `userprogs/` 和
`run` 命令）。

**使用什么许可证？**
Apache 2.0（参见 `LICENSE`）。每个源文件都带有
`SPDX-License-Identifier: Apache-2.0` 头文件。

## 启动

**如何启动它？**
```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso
```
或者将 ISO 写入 U 盘并在真实硬件上启动。ISO 是混合的：
它通过传统 BIOS（El Torito）和 UEFI（一个 EFI 引导镜像）
都能启动——`make run-bios` / `make run-uefi` 执行的是 QEMU 版本。

**它需要硬盘吗？**
不需要。ISO 是自包含启动的（内核作为 multiboot2 模块附加）。
只有在您想要 A/B 更新槽位、/etc 持久卷或
`install` 命令的目标磁盘时，磁盘才有意义。

**为什么启动菜单显示槽位 A / 槽位 B / 槽位 ISO？**
这就是 A/B 更新方案（`kernel/ota/ota_ab.{c,h}`）。GRUB 读取
引导槽位标志并启动活动槽位；`abcfg`/`ab_partition_test`
和 OTA 命令在系统内部控制它。

## Shell

**有多少条命令？**
233 条（实时验证：在访客内部运行 `help -a`，或
运行 `make api-docs` 并阅读 `docs/data/commands.json`）。

**完整命令列表在哪里？**
`help -a`（按字母顺序）、`help -w`（按工作包）、
`help`（简短列表）。`docs/manual/` 下的参考文档
涵盖了每个领域。

**为什么所有系统字符串都是英文的？**
控制台和内核路径在设计上就是 ASCII（参见
`CONTRIBUTING.md` 基本规则 1）；存在一个单独的中文控制台/TUI 层
（`chinese/` 树、`chinese` 命令），但系统自身的
字符串保持 ASCII 以确保硬件兼容性。

## 限制与设计选择

**它需要多少 RAM？**
它在 512 MiB 的 QEMU 访客中运行流畅。物理内存管理器
解析 multiboot2 内存映射并管理引导程序报告的任何内容；
PMM 中没有固定的上限。

**为什么是 100 Hz 定时器？**
PIT 被编程为 100 Hz（`kernel/core/core_timer.h`）——10 ms
节拍驱动调度器的轮询时间片和软定时器轮。
HPET 和 TSC 也作为时间源支持（`hpet`、`pmtimer`、`tsc` 命令）
但 PIT 仍然是节拍源。

**可以移除命令吗？**
不可以——接口契约规定命令接口只增长
（`SHELL_MAX_COMMANDS` 为 256，已注册 233 个，还有 23 个余量；
该限制位于 `shell/shell.h`）。

**我的文件存放在哪里？**
默认是 `ramfs`（内存中、易失的）；您挂载或格式化的
FAT32 / exFAT / ext4 卷是持久的；32 MiB 的
`/etc` 卷保存跨重启的配置（`etc.img`、`opencube.conf`）。

## 网络

**网络协议栈做什么？**
Ethernet + ARP + IPv4 + ICMP + UDP + DNS + TCP（带拥塞
控制），用户程序的套接字，HTTPS 客户端（TLS 1.2/1.3 带
RSA/EC 证书验证），SSH 客户端，以及一个 SSH *服务器*
（访客上的 `sshd`，密钥在 /etc 卷上）。NIC 层支持
e1000/e1000e/igb/ixgbe/rtl8139/rtl8169(rtl8168/8125/810x 系列) 和
BCM57xx 卡；QEMU 默认的 `-device e1000` 开箱即用。

**如何在 QEMU 中获得网络？**
```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```
然后在访客内部：`dhcp`、`ping 10.0.2.2`、`wget 10.0.2.2`、
`dns example.com`。

## 调试

**如何查看内核日志？**
`dmesg`（环形缓冲区）、`logcat`（实时跟踪）、`logctl`（级别控制
和过滤器）——日志子系统是 `kernel/lib/lib_klog.{c,h}`
带轮转（`/etc` 卷上的 `kernel.log`、`kernel.log.1`、...）。

**在我的机器上三重故障/重启了——现在该怎么办？**
使用 `-serial file:log.txt` 启动并读取最后一条
未打印 `OK` 的自检行；那就是失败的子系统。
崩溃恢复路径（`crashtest`、`dumpinfo`、/etc 卷上的 crashlog）
捕获带寄存器转储的 panic。`tools/run_fix4_regression.py` 显示
完整的预期启动序列。

**如何构建带调试符号的版本？**
默认构建已经携带 `-g` 并链接，然后从最终 ELF 中剥离
调试符号；在流水线期间，`strip` 之前的构建产物
保留在 `build/` 中（`opencube_base.elf`）。

## 构建与可复现性

**构建是可复现的吗？**
是的——`make reproducible-check` 从干净状态构建内核两次
并字节比较 ELF；它必须打印 `PASS`。树中不存在
`__DATE__`/`__TIME__`/`__TIMESTAMP__` 宏，链接顺序是
固定的，`strip` 会移除带路径的节。源 zip 是一个确定性的
`git archive`（树顺序、提交 mtimes、索引权限）。
OTA 包嵌入构建日期（UTC）——按设计，它在一天内是
可复现的。

**为什么 CI 对一个爱好内核很重要？**
CI 门禁（`.github/workflows/build.yml`）是可移植的事实：
运行器上更严格的 GCC 或更新的 binutils 曾经捕获了三个
本地工具链静默接受的真正可移植缺陷。门禁运行
与您本地运行完全相同的目标。

**Windows？**
`build.ps1` 通过 WSL2（推荐）或 MSYS2 工具链驱动相同的
流水线——参见 `BUILDING.md`。

## 扩展

**如何编写 L1 扩展？**
包含接口面头文件（`l1/l1_ext.h`、`l1/l1_wp8a.h`、...、
`tui/tui_ext.h`、驱动/OTA/看门狗/健康头文件），实现
您的模块，并注册它。162 个编号接口及其分组在
`INTERFACES.md` 中；扩展指南位于 `docs/l1/` 下。

**我可以在用户程序中添加自己的 Shell 命令吗？**
可以——`shell_register_command_ex` 通过 L1 接口面暴露
（`l1_ext_shell_register_command_ex`），动态加载器支持
L1 共享对象（通过 `libs/ld_so.c` 的 `.so`，在访客中尝试
`so_test` 和 `dlsym_test`）。
