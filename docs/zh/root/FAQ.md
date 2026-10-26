<!-- GENERATED mirror of the repository-root FAQ.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# FAQ - Open Cube OS

常见问题，答案全部来自真实代码库。凡引用某个上限，都会点名定义它的头文件。

## 项目

**Open Cube OS 是什么？**
一个从零写起的 x86_64 内核（"L0"），提供服务——内存、调度、VFS、
网络、驱动，以及 WinRE 风格的恢复 Shell（`oc>` 提示符）——外加一个
Ring-3 应用层（"L1"），其默认启动项是 ush 用户 Shell。它从 BIOS+UEFI
混合 ISO 启动，给你 **251 个命令归属**（L0 恢复 Shell 99 条、Ring-3
服务命令 133 条、ush 内建 19 条）来检视和控制机器。

**这个 "L0 / L1" 是什么？**
两个环：

- **L0 = 提供服务。** 本仓库：完整内核、驱动、服务实现，以及 `oc>`
  恢复环境。
- **L1 = 使用服务。** Ring-3 应用经扩展接口面使用 L0 能力：编号的
  L1SVC 操作表（17 个类别 / 158 个操作，`services/l1/l1svc_defs.h`）
  加经典 syscall。`l1apps/` 在树内交付 L1 侧：ush（唯一的
  `OC_L1_APP_DECL` 应用，默认启动项）与嵌入测试程序。

设计原则是"一切皆可扩展"——Shell、TUI、驱动、甚至控制台字体引擎
都是扩展面。

**它是 Linux 吗？**
不是。不是分支、不是发行版。内核的每一行
（core/arch/drivers/services/recovery/tui/chinese/boot/l1apps 共
130,572 行）都是 Apache 2.0 下的原创代码。它不运行 Linux 二进制；
它运行自己的小型 ELF 用户程序（见 `l1apps/` 与 `run` 命令）。

**什么协议？**
Apache 2.0（见 `LICENSE`）。每个源文件都带
`SPDX-License-Identifier: Apache-2.0` 头。

## 启动

**怎么启动？**
```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso
```
或把 ISO 写入 U 盘在真机上启动。ISO 是混合的：传统 BIOS（El
Torito）与 UEFI（EFI 启动映像）都能引导——`make run-bios` /
`make run-uefi` 跑对应的 QEMU 变体。默认启动落在 Ring-3 的 `ush>`
提示符；`exit` 落入 `oc>` 恢复 Shell。

**需要硬盘吗？**
不需要。ISO 自包含启动（内核作为 multiboot2 模块附带）。只有需要
A/B 更新槽位、/etc 持久卷或 `install` 的目标盘时硬盘才有意义。

**为什么启动菜单显示槽 A / 槽 B / 槽 ISO？**
那是 A/B 更新方案（`services/ota/ota_ab.{c,h}`）。GRUB 读取启动
槽位标志并引导活动槽；`abcfg`/`ab_partition_test` 与 OTA 命令在
系统内控制它。

## Shell

**有多少命令？**
251 个命令归属：L0 恢复 Shell 99 条（`oc>` 提示符下 `help -a`）、
经 SYS_L1SVC 路由的 Ring-3 SVC 命令 133 条、ush 内建 19 条（ush 内
`help`）。可在访客内实测，或 `make api-docs` 读
`docs/data/commands.json`。v1.0.0 的 233 条命令基线全量映射——见
`INTERFACES.md` Part 1c 的兼容性对照表。

**完整命令清单在哪？**
`oc>` 下：`help -a`（字母序）、`help -w`（按工作包）、`help`（短
清单）。ush 内：`help`。`docs/manual/commands/` 的参考文档覆盖全部
251 条。

**为什么系统字符串全是英文？**
控制台与内核路径按设计保持 ASCII（见 `CONTRIBUTING.md` 基本规则 1）；
存在独立的中文控制台/TUI 层（`chinese/` 树、`chinese` 命令），但
系统自有字符串为硬件兼容性保持 ASCII。

## 上限与设计取舍

**需要多少内存？**
在 512 MiB 的 QEMU 访客中舒适运行。物理内存管理器解析 multiboot2
内存图并管理引导器报告的一切；PMM 中没有固定上限。

**为什么是 100 Hz 定时器？**
PIT 被编程为 100 Hz（`core/core_timer.h`）——10 ms 的 tick 驱动
调度器的轮转时间片与软定时轮。HPET 与 TSC 作为附加时间源（`hpet`、
`pmtimer`、`tsc` 命令），但 PIT 仍是 tick 源。

**命令能被移除吗？**
不能悄悄移除——接口契约规定命令面只增不减；任何真实删除必须写入
删除申报清单（`INTERFACES.md` Part 2d；v2.0.0 树只删除了五个
init/probe 管道条目，每个都带着替代者申报）。L0 注册表上限是
`SHELL_MAX_COMMANDS`（256，2.2.0 注册 99 条——留有增长余量；上限
定义在 `recovery/shell.h`）。

**我的文件放哪？**
默认 `ramfs`（内存中，易失）；你挂载或格式化的 FAT32 / exFAT / ext4
卷（从 ush 跑 `mkfs.fat32` 等）是持久的；32 MiB 的 `/etc` 卷跨重启
保存配置（`etc.img`、`opencube.conf`）。

## 网络

**网络栈做什么？**
以太网 + ARP + IPv4 + ICMP + UDP + DNS + TCP（含拥塞控制）、用户
程序套接字、HTTPS 客户端（TLS 1.2/1.3，RSA/EC 证书验证）、SSH
客户端与 SSH *服务器*（访客内 `sshd`，密钥在 /etc 卷）。NIC 层支持
e1000/e1000e/igb/ixgbe/rtl8139/rtl8169（rtl8168/8125/810x 家族）
与 BCM57xx 卡；QEMU 默认的 `-device e1000` 开箱即用。

**QEMU 里怎么联网？**
```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```
然后在访客内：`dhcp`、`ping 10.0.2.2`、`wget 10.0.2.2`、
`dns example.com`。

## 调试

**怎么看内核日志？**
`dmesg`（环形缓冲）、`logcat`（实时跟随）、`logctl`（级别控制与
过滤）——日志子系统是 `services/klog/lib_klog.{c,h}`，带轮转
（/etc 卷上的 `kernel.log`、`kernel.log.1`……）。

**我的机器上三重故障 / 反复重启——怎么办？**
用 `-serial file:log.txt` 启动，读最后一条没打 `OK` 的自检行；那就是
故障子系统。崩溃恢复路径（`crashtest`、`dumpinfo`、/etc 卷上的
crashlog）带寄存器转储捕获 panic。

**怎么带调试符号构建？**
默认构建就带 `-g` 链接，再从最终 ELF 剥离调试符号；流水线中
`strip` 之前的构建产物留在 `build/`（`opencube_base.elf`）。

## 构建与可复现

**构建可复现吗？**
是——`make reproducible-check` 两次干净构建并字节对比 ELF；必须打
印 `PASS`。树中没有 `__DATE__`/`__TIME__`/`__TIMESTAMP__` 宏，链接
顺序固定，`strip` 移除携带路径的段。源码 zip 是确定性的 `git
archive`（树序、提交 mtime、索引权限）。OTA 包内嵌构建日（UTC）——
按设计在同一天内可复现。

**对兴趣内核来说 CI 为什么重要？**
CI 门（`.github/workflows/build.yml`）是可移植的真相：runner 上更严
的 GCC、更新的 binutils 或 GRUB 会拒绝本地工具链悄悄接受的东西——
2.0.0 正是这样发布了本地能编译、CI 编不过的源码（端口约束内联汇
编、被淘汰的 GRUB 模块、C23 的 `bool` 关键字；均在 2.1.0 修复）。
自 2.1.0 起该门是硬性的：发布 tag 必须指向 CI 绿的提交。

**Windows？**
`build.ps1` 经 WSL2（推荐）或 MSYS2 工具链驱动同一流水线——见
`BUILDING.md`。

## 扩展

**怎么写 L1 扩展？**
读 `docs/l1/`（树内指南集：概览、接口面地图、写模块、驱动
block/nic/snd/ota/watchdog 指南）。编号的 L1SVC 面（17 类 / 158
操作）定义在 `services/l1/l1svc_defs.h`；仍然公开的 register 类面
（`tui/tui_ext.h`、`services/watchdog/core_watchdog.h`、
`services/l1/l1_ext.h` 一族）与各服务头都在 `INTERFACES.md` 有记载。

**能从用户程序加自己的 Shell 命令吗？**
能——`l1_ext_shell_register_command_ex` 经 L1 面暴露
（`services/l1/l1_ext.h`），且动态加载器支持 L1 共享对象（经
`l1apps/ld_so.c` 的 `.so`，访客内可试 `so_test` 与 `dlsym_test`）。
