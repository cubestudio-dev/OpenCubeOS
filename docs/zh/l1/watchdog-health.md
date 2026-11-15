<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用视角的看门狗、健康与 klog

## 看门狗（服务 `watchdog`，L1SVC cat 11）

一个注册的槽是一个*策略*看门狗：持有它的任务必须喂它；超时会
击杀该任务（原因写入日志）而内核继续活着。硬件看门狗（Intel
TCO、ACPI WDAT）叠在其下并复位整机 - E2E 套件用真实复位和
第二次启动证明两者。

```
watchdog_register(name, timeout_ms, policy)  -> slot
watchdog_start / feed / stop / set_timeout
```

`ush> watchdog`（或 sysdiag 经典表）报告槽位与策略；饿死测试
（`wd-starve`，3000 ms）是 E2E 证明击杀路径的方式。

## 健康（服务 `health`，L1SVC cat 9）

```
health_get_cpu / mem / tasks / io      (the snapshot sources)
health_export(path)                    (valid JSON)
```

阈值触发告警进内核日志（`CPU-LOCKED` 表示一个被钉死的核心、
...）。应用表面：`health`（快照 - sysdiag 向导的第一个菜单项）、
`health watch`（实时监视，ENTER 退出）、`health export [path]`。

## klog（服务 `klog`，L1SVC cat 10）

`dmesg [N]`（环形日志，最新在末尾）、`logcat`（结构化：-l
level -m module -k key -t seconds -n N -f follow）、`logctl
status|level|...`（运行时控制）。sysdiag 的日志工具子菜单代理
全部三者。

## 大应用视图

`sysdiag` 汇集整个可观测性故事：健康快照、内核日志、内存画像、
调度器统计、崩溃记录、日志工具、自检实验室。看门狗 + 健康 +
klog 仍可单独触达（经典/直通），供脚本和 E2E 套件使用。
