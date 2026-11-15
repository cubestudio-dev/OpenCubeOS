<!-- SPDX-License-Identifier: Apache-2.0 -->
# 用户手册

操作员运行 Open Cube OS 3.0.0 所需的一切。

命令操作面围绕**八个大应用**组织——`update`、`disk-tool`、`net-tool`、`sound-tool`、`files`、`sysdiag`、`hwinfo`、`power`。每个应用既是一个意图向导，*又*是其下每条历史命令的逐字分发入口。284 项的程序注册表让所有经典单命令逐字节原样可用。

| 页面 | 内容概要 |
|------|----------|
| [getting-started](getting-started.md) | 下载、在 QEMU 中启动、头五分钟 |
| [mega-apps](mega-apps.md) | 8 个大应用：向导 / direct / classic 契约 |
| [shell-basics](shell-basics.md) | ush：管道、重定向、环境变量、别名、脚本 |
| [filesystems](filesystems.md) | VFS 类型、格式化、挂载、/etc 卷 |
| [network](network.md) | 地址配置、WiFi/蓝牙、防火墙、TLS + SSH |
| [update-ota](update-ota.md) | 五步更新向导、安装、回滚 |
| [boot-modes](boot-modes.md) | BIOS + UEFI、GRUB A/B 槽位菜单、持久化 |
| [tui-guide](tui-guide.md) | 桌面、应用、键盘模型 |
| [chinese-console](chinese-console.md) | 中文层、输入法、ASCII 契约 |
| [recovery](recovery.md) | WinRE 工作流、崩溃产物、救援工具包 |
| [commands](commands/index.md) | 全部 284 条命令参考页（每个命令归属：L0、SVC、内建） |

## 8 个大应用一览

| 应用 | 意图 | 包装的经典操作 |
|-----|--------|----------------------|
| `update` | 把机器带到最新版本 | OTA 链：status、checkupdate、install、rollback |
| `disk-tool` | 磁盘、分区、文件系统 | lsblk、parted、mkfs.*、mount 系列、fsck、A/B 工具 |
| `net-tool` | 连通性与地址管理 | ip、dhcp、dns、route、arp、firewall、wifi、bt、ssh |
| `sound-tool` | 音频设备与播放 | sound、volume、play、各驱动状态 + 自检 |
| `files` | 日常文件操作 | ls、tree、df、du、cp、mv、rm、edit、stat、chmod |
| `sysdiag` | "感觉哪里不对——查一下" | health、dmesg、内存/调度统计、自检 |
| `hwinfo` | 这台机器里有什么硬件 | uname、时钟、PCI、ACPI、SMBus、USB、摄像头 |
| `power` | 关机 / 重启 / 停机 / 睡眠 | 电源操作及其路径自检 |

每个应用都遵循同一套三扇门契约：

```
ush> <app>              the intent wizard (menus, checks, confirmations)
ush> <app> direct <op>  explicit verbatim passthrough of one op
ush> <app> <op> [args]  classic dispatch - identical to the historical command
```

细节与逐应用详解见 [mega-apps](mega-apps.md)。
