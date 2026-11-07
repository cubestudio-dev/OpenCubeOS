<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：看门狗与健康监控

## 看门狗 - `kernel/core/core_watchdog.h`（18 个声明；编号 5）

```
watchdog_register(name, timeout_ms, policy)  -> slot
watchdog_start(slot)
watchdog_feed(slot)
watchdog_stop(slot)
watchdog_set_timeout(slot, ms)
```

已注册的槽位是一个策略看门狗：任务必须喂狗；
超时会杀死任务（原因已记录），而内核继续运行。
硬件看门狗（Intel TCO、ACPI WDAT）位于其下层并
重置整台机器 - 端到端测试套件通过真实重置和二次启动
证明了这一点。

## 健康 - `kernel/core/core_health.h`（8 个声明；编号 5）

```
health_get_cpu(out)
health_get_mem(out)
health_get_tasks(out)
health_get_io(out)
health_export(path)          // valid JSON to /var/log/health.json
```

阈值会向内核日志发出警报（在锁定的核心上触发 `CPU-LOCKED`...）；
`health`、`health watch`、`health export` 是相关命令；
端到端测试验证了警报线路和 JSON 有效性。

## klog - `kernel/lib/lib_klog.h`（29 个声明；编号 6）

```
klog_write(level, fmt, ...)
klog_set_level(lvl)
klog_set_level_enabled(lvl, on)
klog_flush()
klog_rotate()
klog_filter_set(min_level)
```

级别：DEBUG/INFO/WARN/ERROR/FATAL；轮转
（`kernel.log` -> `kernel.log.1`，阈值 4096）和过滤是
真实存在且经过端到端验证的行为。
