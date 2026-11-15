<!-- SPDX-License-Identifier: Apache-2.0 -->
# 服务表面图

每个 L1SVC 类别、它代理什么、包装它的应用。编号契约是
`ocos/interface/l1svc_ops.h`（从
`ocos/services/sysapi/interface/svc_sysapi_l1svc.h` 镜像而来；
上游只允许追加式重编号，然后重新生成）。

## 类别

| Cat | 名称 | 代理 | 大应用归属 |
|-----|------|--------|---------------|
| 1 | time | RTC / HPET / PM 定时器 / TSC 状态 | hwinfo |
| 2 | block | 磁盘：状态、分区、mkfs、fsck、A/B 工具、测试 | disk-tool |
| 3 | nic | 网卡家族 + 无线（wifi/bt） | net-tool |
| 4 | snd | 音频设备、音量、play、按驱动测试 | sound-tool |
| 5 | power | shutdown/reboot/halt/sleep + 路径测试 | power |
| 6 | ota（selfhost） | 更新链、checkupdate、配置存储、A/B | update |
| 7 | fs | mount/umount/fatmount/fatstat/tree | disk-tool、files |
| 8 | net | ip/dhcp/dns/arp/route/firewall/ssh/sshd | net-tool |
| 9 | health | 快照 / 导出 / 监视 | sysdiag |
| 10 | klog | dmesg / logcat / logctl | sysdiag |
| 11 | watchdog | 看门狗命令家族 | sysdiag |
| 12 | crypto | cryptotest / dhtest | （仅经典） |
| 13 | l1 | 注册表列表本身（help -a/-w）、l1test、ldd | - |
| 14 | display | uname/stats/lang/font/encoding/IME 操作 | hwinfo |
| 15 | input | touchpad / touchtest | hwinfo |
| 16 | camera | uvc/scsi/gameport/parallel/ir + 测试 | hwinfo |
| 17 | usb | usb status/dev + usb 测试家族 | hwinfo |
| 18 | ocire | 启动卷日志环（仅守卫操作） | - |
| 19 | diag | 内存/调度器/vm 测试、PCI/ACPI/I2C/SMBus、crash | sysdiag |
| 20 | tui | 桌面 + TUI 命令家族 | （tui 应用） |

## L1SVC 之外：直接系统调用

| 系统调用 | 使用者 |
|---------|---------|
| `SYS_OPEN/READ/WRITE/CLOSE/STAT/READDIR` | 文件工具（cat、cp、ls、...） |
| `SYS_FORK/EXECVE/WAIT4` | ush、run、files - 程序启动器模式 |
| `SYS_PIPE/DUP/DUP2` | ush 管道与重定向 |
| `SYS_READLINE`（73） | 向导行输入（回显 + 退格，阻塞） |
| `SYS_GETCH`（74） | 非阻塞单键（菜单、编辑器） |
| `SYS_NETCMD`（101） | ping / wget / ifconfig / netstat 桥 |
| `SYS_PS`（102） | ps / top 进程表 |

完整的 x86_64 系统调用表注册在
`ocos/services/sysapi/src/svc_sysapi_syscall.c`。

## 输出契约

服务操作通过调用者的缓冲返回**英文 ASCII 文本** - 这些行是
面向脚本的契约，也是向导探针：

```
"IP: 10.0.2.15"            (net ip)
"DHCP success: IP=..."     (net dhcp)
"A/B disk: present"        (ota update status)
"New version available."   (ota checkupdate)
"Current version is up to date."
"reboot required"          (ota update, after install)
```

重命名其中任何一行都是破坏性变更：向导（`wizard_lib.h` 的
`wiz_find`）、E2E 套件和用户文档都在读它们。

## help -a / help -w

注册表列表（L1SVC cat "l1" op 1）为每个程序渲染名字 + wp 标签
+ 双语提示。`help -w` 按标签分组；`mega` 组（8 个合并应用）排在
视图最前，其后是 18 个历史服务组。
