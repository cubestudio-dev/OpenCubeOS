<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：看门狗子系统

`kernel/core/core_watchdog.{c,h}` - 两个看门狗世界，一个头文件。

## 策略看门狗（受监控的槽位）

任务注册一个带超时的槽位：

```
slot = watchdog_register("mydaemon", 5000, policy);
watchdog_start(slot);
... watchdog_feed(slot);   /* at least every 5 s */
```

超时触发 -> 任务被终止，原因记录到内核日志中（E2E 中的 `test log ERROR`），内核继续运行。这是"我的后台任务可能挂起"场景的正确工具。

## 硬件看门狗

- **Intel TCO**（LPC）：PMBASE 从芯片组发现，TCOBASE 派生，定时器启动；过期会硬复位机器。在 QEMU q35 上可用 - E2E S12 打印发现行，启动，饥饿，并断言第二次真实启动。
- **ACPI WDAT**：表的条目编程硬件；相同的通过重启验证周期（E2E S13）。

`watchdog` 展示了两个世界：硬件支持 + 槽位表 + 策略测试。

## 门禁此功能的 E2E 场景

S7（状态/槽位）、S8（策略终止 + 日志）、S12（TCO 真实复位 + 第二次启动）、S13（WDAT 真实复位 + 第二次启动）。`tools/wp12_watchdog_logging_e2e.py` 中共有十四个场景 - 发布时全部必须通过。
