<!-- SPDX-License-Identifier: Apache-2.0 -->
# 恢复工作流（WinRE 风格）

Open Cube OS *就是*恢复环境。Windows 将 WinRE 隐藏在启动故障之后，而这里的 `oc>` Shell 是主要接口——并且它同时也是运行所在机器的救援工具包。

## 设计理念

- 即使**完全没有磁盘**，系统也能启动到 Shell（ISO 是自包含的）。
- 任何诊断都只需一条 Shell 命令——无需构建单独的救援介质，无需进入特殊模式。
- 崩溃产物会持久化到 /etc 卷，因此经历 panic 后重启的机器仍携带自己的证据。

## 崩溃产物

当内核发生 panic（受保护自测之外未处理的异常）时：

1. 异常帧（寄存器、故障地址）转储到串口——你可以通过 `-serial file:log.txt` 查看。
2. 一条崩溃日志条目被写入 /etc 卷。
3. 机器停止运行（或根据看门狗策略重启）。

重启后：

```
oc> dumpinfo            (inspect the saved crash dump)
oc> dmesg               (kernel ring buffer from the current boot)
oc> logcat              (follow the persistent kernel log live)
oc> ls /etc             (crashlog files live here)
```

## 强制崩溃（以测试该流程）

```
oc> crashtest           (deliberate kernel panic - safe on a scratch
                          system, everything not on /etc is lost)
```

`crashtest` 完整验证了转储路径：panic → 串口转储 → /etc 崩溃日志 → `dumpinfo` 读取回来。

## 救援工具包

典型救援场景及其对应命令：

| 场景 | 操作 |
|------|------|
| 系统启动，然后行为异常 | `dmesg`、`logcat`、`health`、`health export` 获取 JSON 快照 |
| 用户程序挂起 | 通过进程命令列出任务并终止；`watchdog` 策略会终止饿死进程 |
| 磁盘可疑 | `dskstat`、`fsck <dev>`、以只读方式挂载检查 |
| 启动标志指向错误槽位 | GRUB 菜单 → 强制使用另一个槽位；然后执行 `update rollback` |
| 网络配置丢失 | `config restore`（恢复默认）然后重新运行 `dhcp` |
| 日志溢出 | `logctl`（级别 + 过滤器），轮转是自动的（`kernel.log` → `kernel.log.1`） |

## 看门狗（硬件层面）

`kernel/core/core_watchdog.c` 会启用真实硬件看门狗（如果平台有的话）：

- **Intel TCO**（LPC 总线）：在 QEMU q35 上动态发现——端到端测试套件显示 `Intel TCO found: PMBASE=0x600 TCOBASE=0x660`，启用它、使其饥饿，机器确实硬重启并再次启动。
- **ACPI WDAT**：表项被编程；启用和重置以相同方式验证。

```
oc> watchdog            (status: hardware support + monitored slots)
oc> watchdog feed       (feed the active watchdog)
oc> watchdog start/stop/timeout ...   (see the command page)
```

被监控的槽位额外实现了*策略*看门狗：必须定期为其槽位"喂食"的任务会被终止（原因记录在日志中），而不会导致内核崩溃——内核存活，违规进程不存活。

## 健康监控

```
oc> health              (CPU / memory / tasks / IO at a glance)
oc> health watch        (live monitor, 1 s refresh)
oc> health export       (JSON snapshot to /var/log/health.json)
```

阈值会向内核日志发出警报（当某个循环程序锁定核心时显示 `CPU-LOCKED`、内存压力、IO 阻塞）——这些警报行正是端到端测试套件断言的内容。

（本段含 4 个 ⟪OC-…⟫ 占位标记，必须全部原样保留在原位置。）
