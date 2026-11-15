<!-- SPDX-License-Identifier: Apache-2.0 -->
# 8 个大应用：向导、direct、classic

自 3.0.0 起，用户操作面围绕八个大应用组织。每个应用都打开同样的三扇门；用哪一扇，取决于你是在探索（向导）、写脚本（classic），还是想显式地绕过菜单（direct）。

```
ush> disk-tool                wizard: menus, checks, confirmations
ush> disk-tool direct lsblk   explicit verbatim passthrough
ush> disk-tool lsblk          classic dispatch = the historical command
```

三种方式产生**相同的服务输出**——向导只是在其中增加了检测、安全确认与分组。

## 向导（无参数）

不带参数运行应用会打开一个意图向导：

- 编号菜单：按单个数字键（`1`-`9`）选择，`q` 或 ESC 退出/返回。无需按回车。
- 破坏性路径（mkfs、abdisk、install、rm、电源操作）会询问目标名称，要求你**重新输入一遍**，然后输入单独的 `y`——其他任何输入都会中止。
- 向导读取的是与脚本相同的英文服务输出行，并且可以**跨域**：`update` 向导在检查更新之前，会先用 DHCP 修复缺失的 IP，用 `lsblk` + `abdisk` 修复缺失的 A/B 盘。
- 所有内容都经当前语言渲染：`lang zh` 会切换整个向导，菜单也不例外。

## direct（显式透传）

`<app> direct <op> [args...]` 会从参数行中去掉 `direct` 一词，然后严格按照历史命令的方式运行一个操作。当你希望脚本中的意图不言自明时使用它：

```
ush> update direct           the historical bare `update` (online flow)
ush> sound-tool direct volume 50
ush> sysdiag direct health watch
```

## classic（逐字分发）

`<app> <op> [args...]` 在应用的表里查找该操作，并按历史拼写运行——输出与运行同名单命令应用逐字节一致。单命令应用仍保持注册（接口冻结规则：命令只增不删），因此 `lsblk` 与 `disk-tool lsblk` 是进入同一服务调用的两扇门。

未知的操作名会打印该应用的用法及其完整操作表：

```
ush> disk-tool nope
disk-tool: unknown op: nope
usage: disk-tool            (interactive menu)
       disk-tool direct <op> [args]   (explicit passthrough)
       disk-tool <op> [args]          (classic op dispatch)
ops:
  ahci
  nvme
  ...
```

## 逐应用详解

### update——OTA 向导

`update`（无参数）用五步走完整个更新意图：

1. **网络**——读取 IP 状态；若地址为 0.0.0.0，则提供 DHCP 修复，然后复查。
2. **A/B 盘**——读取 `update status`；当 A/B 盘缺失时列出 `lsblk`，询问设备名（重输 + 确认）并运行 `abdisk`，然后复查直到其存在。
3. **DNS**——解析当前的 `config get update_url` 取值，并对更新主机做域名解析。
4. **检查**——`checkupdate`：已是最新则以 OK 结束向导。
5. **安装**——经确认的在线更新，随后经确认重启进入新槽位。

经典面：`update status`、`update --local <path>`、`update <anything>` = 逐字 OTA 操作。

### disk-tool——磁盘与文件系统

菜单：lsblk / parted / mkfs 子菜单（FAT32、exFAT、ext4——双重确认）/ 挂载管理器（list、mount、umount、fatmount、fatstat）/ fsck / dskstat / A/B 管理器（status、create、abcfg）。

经典表：全部 23 个块设备操作（`ahci` .. `abcfg`）加 6 个文件系统操作（`mount`、`mounts`、`umount`、`fatmount`、`fatstat`、`tree`）。

### net-tool——连通性

菜单：显示 IP / DHCP / 静态 IP（地址、掩码、网关逐字段采集）/ DNS / WiFi 管理器（status、scan、test、sdio）/ 蓝牙（status、usb、uart、test）/ 防火墙 / route + arp / 连通性测试（ping、wget、ssh、dnstest——ping 和 wget 走的是历史应用所用的同一座 NETCMD 桥）。

经典表：14 个网络操作（`arp` .. `sshd`）加 `wifi` 和 `bt`。

### sound-tool——音频

菜单：设备概览 / 音量（仅数字校验）/ 440 Hz 测试音 / 各驱动状态（HDA、AC'97、SB16、ES1370、virtio-snd、USB audio）/ 自检（RW 回环、采样率精度，或依次运行每个驱动各自的自检）。

经典表：全部 17 个声音操作（`sound` .. `sample_rate_test`）。

### files——文件管理器

菜单：ls / tree / 磁盘用量（df + du）/ 复制 / 移动 / 删除（带确认）/ edit（全屏编辑器）/ 信息 + 权限（先 stat，再可选 chmod，带八进制校验）。

这里的分发是对历史程序的 fork + execve（`run` 机制），因为文件工具是真实程序而非服务调用——编辑器是交互式的。经典表：`ls`、`tree`、`df`、`du`、`cp`、`mv`、`rm`、`edit`、`stat`、`chmod`。

### sysdiag——系统诊断

菜单：健康快照 / 内核日志 / 内存画像（mem + frag + heap）/ 调度器 + 同步统计 / 崩溃记录 / 日志工具（logcat、logctl、health export、health watch）/ 自检实验室（memtest、heaptest、fstest、vmtest——可单个运行，也可四个依次全跑）。

经典表：全部 39 个诊断操作，加上 health、dmesg/logcat/logctl 与 watchdog——共 46 项。`crashtest`（故意注入 panic）只能通过 classic/direct 触达，这是有意为之。

### hwinfo——硬件清单

菜单：系统摘要（uname -a + 内存）/ 时钟源（RTC、HPET、PM timer、TSC）/ PCI（lspci、pcidump、pcibars、pcie）/ ACPI / SMBus-I2C / USB / 摄像头与老式输入 / 显示 + 字体 + 编码。

经典表：横跨显示、时间、网络、诊断、USB、摄像头与输入各面的 26 个只读操作。

### power——电源管理

菜单：关机 / 重启 / 停机（每项都需单独输入 y 确认）/ 睡眠 N 秒（校验为纯数字）/ 电源路径自检（每项都有各自的警告 + 确认——这些探测会真实演练电源路径）。

经典表：`shutdown`、`poweroff`、`reboot`、`halt`、`sleep`、`poweroff_test`、`suspend_test`、`halt_test`、`reboot_test`。

## i18n

向导的每一行都经当前 locale 渲染。切换方式：

```
ush> lang zh        (menus, prompts and verdicts become Chinese)
ush> lang en        (back to English)
```

缺失的翻译会回退到英文，再回退到键本身——未翻译的应用绝不会崩溃或打印乱码。
