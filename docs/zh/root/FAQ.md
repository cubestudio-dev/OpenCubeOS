<!-- SPDX-License-Identifier: Apache-2.0 -->
# FAQ - Open Cube OS

常见问题，答案来自真实代码库。
凡引用某个限制之处，都点名了定义它的头文件。

## 项目

**Open Cube OS 是什么？**
一个从零写起、提供服务的 x86_64 内核（"L0"）——内存、
调度、VFS、网络、驱动，以及一个 WinRE 风格的恢复 shell
（2.x 的 `oc>` 提示符）——外加一个 Ring-3 应用层（"L1"），
其默认启动项是 ush 用户 shell。它从混合 BIOS+UEFI ISO 启动，
给你 **251 个命令归属**（99 条在 L0 恢复 shell、133 条
Ring-3 服务命令、19 个 ush 内建）来检视和控制这台机器。

**"L0 / L1" 是什么？**
两个环：

- **L0 = 提供服务。** 本仓库：完整内核、驱动、服务实现，
  以及 `oc>` 恢复环境。
- **L1 = 使用服务。** 经扩展面触达 L0 能力的 Ring-3 应用：
  编号的 L1SVC 操作表（17 个类别 / 158 个操作，
  `services/l1/l1svc_defs.h`）加经典 syscall。`l1apps/`
  在树内交付 L1 侧：ush（唯一的 `OC_L1_APP_DECL` 应用，
  默认启动项）和内嵌的测试程序。

设计原则是"一切皆可扩展"——shell、TUI、驱动、甚至
控制台字体引擎都是扩展面。

**它是 Linux 吗？**
不是。不是分支，不是发行版。内核的每一行
（core/arch/drivers/services/recovery/tui/chinese/boot/l1apps
共 130,572 行）都是 Apache 2.0 下的原创代码。它不运行
Linux 二进制；它运行自己的小型 ELF 用户程序（见
`l1apps/` 和 `run` 命令）。

**什么许可？**
Apache 2.0（见 `LICENSE`）。每个源文件携带
`SPDX-License-Identifier: Apache-2.0` 头。

## 启动

**怎么启动它？**
```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso
```
或把 ISO 写进 U 盘启动真机。ISO 是混合的：legacy BIOS
（El Torito）和 UEFI（EFI 启动镜像）一样能启动——
`make run-bios` / `make run-uefi` 做 QEMU 变体。默认启动
落进 Ring-3 的 `ush>` 提示符；`exit` 掉入 2.x 的 `oc>`
恢复 shell（3.0.0 中已删除）。

**需要硬盘吗？**
不需要。ISO 自包含启动（内核作为 multiboot2 模块挂载）。
磁盘只在你想要 A/B 更新槽位、/etc 持久卷或 `install`
命令的目标盘时才有意义。

**为什么启动菜单显示 slot A / slot B / slot ISO？**
那是 A/B 更新方案（`services/ota/ota_ab.{c,h}`）。GRUB 读
启动槽位标志并启动活动槽位；`abcfg`/`ab_partition_test`
和 OTA 命令在系统内控制它。

## Shell

**有多少条命令？**
251 个命令归属：99 条 L0 恢复 shell 命令（`ush>` 提示符下
的 `help -a`）、L1SVC 服务桥（20 个类别），以及
19 个 ush 内建（ush 内的 `help`）。在客户机里实测验证，或
`make api-docs` 后读 `docs/data/commands.json`。v1.0.0 的
233 条命令基线完整映射——见 `INTERFACES.md` Part 1c 的
兼容性表。

**完整命令清单在哪？**
在 `ush>`：`help -a`（按字母序）、`help -w`（按工作包）、
`help`（短清单）。ush 内：`help`。`docs/manual/commands/`
下的参考文档覆盖全部 251 条。

**为什么所有系统字符串是英文？**
控制台与内核路径按设计是 ASCII（见 `CONTRIBUTING.md`
基本规则 1）；存在单独的中文控制台/TUI 层（`chinese/` 树、
`chinese` 命令），但系统自身的字符串为硬件兼容保持
ASCII。

## 限制与设计选择

**需要多少内存？**
在 512 MiB 的 QEMU 客户机里舒适运行。物理内存管理器解析
multiboot2 内存映射，管理引导加载器报告的一切；PMM 中
没有固定上限。

**为什么是 100 Hz 定时器？**
PIT 编程在 100 Hz（`core/core_timer.h`）——10 毫秒
tick 驱动调度器的轮转片和软定时器
轮。HPET 和 TSC 额外作为时源受支持（`hpet`、`pmtimer`、
`tsc` 命令），但 PIT 仍是 tick 源。

**命令能被移除吗？**
不能悄悄移除——接口契约说命令面只增不减；任何真实删除
必须在删除登记表中声明（`INTERFACES.md` Part 2d；v2.0.0 树
只移除了五条 init/probe 管线条目，每条都随其替代者一同
声明）。L0 注册表上限是 `SHELL_MAX_COMMANDS`（256，2.2.0
时注册了 99 条——留有增长余量；限制住在
`recovery/shell.h`）。

**我的文件去哪儿？**
默认 `ramfs`（内存中、易失）；你挂载或格式化的
FAT32 / exFAT / ext4 卷（来自 ush 的 `mkfs.fat32` 等）是持久的；
32 MiB 的 `/etc` 卷跨重启保存配置（`etc.img`、
`opencube.conf`）。

## 网络

**网络栈做什么？**
以太网 + ARP + IPv4 + ICMP + UDP + DNS + TCP（带拥塞
控制）、用户程序套接字、HTTPS 客户端（TLS 1.2/1.3，
RSA/EC 证书验证）、SSH 客户端，以及 SSH *服务器*
（客户机上的 `sshd`，密钥在 /etc 卷上）。NIC 层支持
e1000/e1000e/igb/ixgbe/rtl8139/rtl8169（rtl8168/8125/810x
家族）和 BCM57xx 网卡；QEMU 默认的 `-device e1000` 开箱
即用。

**QEMU 里怎么联网？**
```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```
然后在客户机里：`dhcp`、`ping 10.0.2.2`、`wget 10.0.2.2`、
`dns example.com`。

## 调试

**怎么看内核日志？**
`dmesg`（环形缓冲）、`logcat`（实时跟随）、`logctl`（级别
控制与过滤）——日志子系统是 `services/klog/lib_klog.{c,h}`，
带轮转（/etc 卷上的 `kernel.log`、`kernel.log.1`、…）。

**它在我的机器上三重故障 / 重启——怎么办？**
带 `-serial file:log.txt` 启动，读最后一条没有打印 `OK`
的自检行；那就是失败的子系统。崩溃恢复路径
（`crashtest`、`dumpinfo`、/etc 卷上的 crashlog）带寄存器
转储捕获 panic。

**怎么带调试符号构建？**
默认构建已经带 `-g` 并链接，然后从最终 ELF 剥离调试
符号；`strip` 之前的构建工件在流水线期间留在 `build/`
里（`opencube_base.elf`）。

## 构建与可复现性

**构建可复现吗？**
是——`make reproducible-check` 从干净状态构建内核两次并逐
字节比较 ELF；它必须打印 `PASS`。树中没有
`__DATE__`/`__TIME__`/`__TIMESTAMP__` 宏，链接顺序固定，
`strip` 移除携带路径的节。源码 zip 是确定性的
`git archive`（树序、提交 mtime、索引权限）。OTA 包内嵌
构建日（UTC）——按设计它一天之内可复现。

**对业余内核来说 CI 为什么重要？**
CI 门禁（`.github/workflows/build.yml`）是可移植的真相：
runner 上更严格的 GCC、更新的 binutils 或更新的 GRUB
会拒绝本地工具链默默接受的东西——2.0.0 发布就是这样
发布了本地能编译、CI 上不能的源码（端口约束汇编、退役的
GRUB 模块、C23 的 `bool` 关键字；全部在 2.1.0 修复）。
自 2.1.0 起门禁是硬的：发布 tag 必须指向 CI 绿的提交。

**Windows？**
`build.ps1` 经 WSL2（推荐）或 MSYS2 工具链驱动同一流水线
——见 `BUILDING.md`。

## 扩展

**怎么写一个 L1 扩展？**
读 `docs/l1/`（树内指南集：overview、surface map、
writing-modules、driver/block/nic/snd/ota/watchdog 指南）。
编号的 L1SVC 面（17 个类别 / 158 个操作）定义在
`services/l1/l1svc_defs.h`；仍公开的 register 类面
（`tui/tui_ext.h`、`services/watchdog/core_watchdog.h`、
`services/l1/l1_ext.h` 及伙伴）和逐服务头记录在
`INTERFACES.md`。

**能从用户程序加自己的 shell 命令吗？**
能——`l1_ext_shell_register_command_ex` 经 L1
面暴露（`services/l1/l1_ext.h`），动态加载器支持
L1 共享对象（`.so`，经 `l1apps/ld_so.c`；在客户机里试试
`so_test` 和 `dlsym_test`）。
