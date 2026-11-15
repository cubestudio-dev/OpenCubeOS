<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：健康监视器

以 1 秒为周期对五个指标维度的采样器、三个去重告警位，以及
告警到看门狗的馈送联动。实现：
`ocos/services/health/interface/svc_health_core.h`
（101 行）、`src/svc_health_core.c`（约 313 行）、
`src/svc_health_l1.c`（181 行）。用法：
[../dev/debug-handbook.md](../dev/debug-handbook.md) 4.1。

## 1. 一次采样（原文照录）

```c
typedef struct {
    u64  ts_ms;             /* core_timer_now_ms() at sample time   */
    u32  cpu_busy_pct;      /* 0..100 over the last window          */
    u64  mem_total_kb;      /* physical RAM in KiB                  */
    u64  mem_used_kb;
    u64  mem_free_kb;
    u64  heap_used_bytes;   /* kmalloc arena                        */
    u64  heap_free_bytes;
    u32  tasks_total;       /* scheduler slots in use               */
    u32  tasks_running;
    u32  tasks_ready;
    u32  tasks_blocked;
    u64  disk_reads;        /* cumulative since boot                */
    u64  disk_writes;
    u64  disk_read_bytes;
    u64  disk_write_bytes;
    u64  net_tx_packets;
    u64  net_rx_packets;
    u64  net_tx_bytes;
    u64  net_rx_bytes;
    u32  alarms;            /* bitmask of live alarms (see below)   */
} health_sample_t;

#define HEALTH_ALARM_MEM    (1u << 0)   /* memory > 90% used          */
#define HEALTH_ALARM_TASKS  (1u << 1)   /* active tasks > 64           */
#define HEALTH_ALARM_CPU    (1u << 2)   /* 100% busy for 10 samples    */
```

## 2. 五个指标维度及其来源

每秒采样（`health_init()` 在 kmain 步骤 13；启动日志行：
`monitor started (1 s samples, alarms: mem>90%% tasks>64 cpu>=90%%x10s)`）：

| 维度 | 来源 | 说明 |
|---|---|---|
| CPU | `core_sched_idle_counters()` 增量 | 窗口内 busy% = 100 - idle% |
| 内存 | `mem_pmm_get_stats()` + `mem_heap_get_stats()` | 物理 + kmalloc 竞技场 |
| 任务 | `core_sched_get_stats()` + 任务状态遍历 | total/running/ready/blocked |
| 磁盘 | `driver_block_get_stats()` 求和 | 扇区 -> 按 512 折算字节 |
| 网络 | `net_get_stats()` | 累计 tx/rx 包数 + 字节数 |

## 3. 判定逻辑（三个告警位）

告警在每次采样时评估一次并**去重** —— 每次越过阈值一条
ERROR klog，指标回落到阈值之下时重新布防（模块
"health"）：

- `mem_used*100/mem_total > 90` -> `HEALTH_ALARM_MEM`；
  klog：`alarm: memory >90%% used (%u/%u KiB)`
- `tasks_total > 64` -> `HEALTH_ALARM_TASKS`；
  klog：`alarm: %u active tasks (>64)`
- `cpu_busy_pct >= 90` 递增 `g_cpu_100_run`；连续 >= 10 次
  采样 -> `HEALTH_ALARM_CPU`；
  klog：`alarm: CPU >=90%% busy for 10 consecutive seconds`
  （头注释写的是 "100%"，但代码在 10 秒内以 >= 90 为门 ——
  实现注记诚实地说明了这一差异）

整个机制的**触发**：1 秒 tick。**处理**：读计数器、算增量、
以去重状态机评估告警。**影响**：每份快照中的 `alarms:`
行（`MEMORY>90% TASKS>64 CPU-LOCKED` 或 `none`）与 klog
ERROR 行 —— 任何静默失败调查中的第一手客观证据。

## 4. 告警到看门狗的馈送联动

告警作为**额外馈送来源**接入软槽位表的 system-heartbeat
槽位（槽位 0，由 `watchdog_init()` 注册，即 WP-11 的
shell 心跳语义，默认 15 秒，策略 PANIC）：

- **告警但活着**的系统继续喂狗（采样器证明了内核仍在
  调度）；
- **死掉的**系统停止喂食，WP-11 panic 路径接管：完整崩溃
  路径（转储 + crashlog + 策略），崩溃来源为
  `OC_CRASH_SRC_WDT`（33）。

这是刻意的分工：告警是*观察*（日志 + 显示），看门狗槽位
是*强制执行*（真正死掉的内核得到 panic 待遇，过载的不会
仅因忙碌而被复位）。见 [watchdog.md](watchdog.md)。

## 5. ring-3 接口面（3 个 op）

| Op | 命令 | 行为 |
|---|---|---|
| `L1SVC_HEALTH_SNAPSHOT` (1) | `health` | v2.2.0 报告文本 —— cpu/内存/任务/磁盘/网络区段，`hc_line()` 风格（标签以空格填充到第 22 列，然后是值 + 单位），外加 `alarms:` 行 |
| `L1SVC_HEALTH_EXPORT` (2) | `health export [path]` | `health_export()` JSON（`{"ts_ms":1234,"cpu_busy_pct":7,...}`）经 fs_vfs 写入 arg（默认 `/var/log/health.json`），随后打印 |
| `L1SVC_HEALTH_WATCH` (3) | `health watch` | 交互式 1 秒刷新循环，控制台直连输出，ENTER 退出（`health watch: refreshing every second (press ENTER to exit)` / `-- watch stopped --`） |

错误：`health: export FAILED`、`health: export FAILED (no filesystem mounted?)`。

## 6. 实战中读取快照

报告区段依次为（cpu / 内存 / 任务 / 磁盘 / 网络），每行
都是 `hc_line()` 形状 —— 标签以空格填充到第 22 列，然后是
值 + 单位 —— 最后是 `alarms:` 行
（`MEMORY>90% TASKS>64 CPU-LOCKED` 或 `none`）：

```
cpu:
  <label>                <value> %
memory:
  <label>                <value> KiB
tasks: / disk: / network:
  ...
alarms: none
```

静默失败阶梯的解读指南
（[../dev/debug-handbook.md](../dev/debug-handbook.md) 4.1）：

| 症状 | 看哪里 | 下一步 |
|---|---|---|
| 机器"变慢" | cpu busy + `CPU-LOCKED` | `sched`、`ps` —— 找到自旋者；用 `nice <tid> <prio>` 把它降级 |
| 分配失败、挂载失败 | memory + `MEMORY>90%` | `mem`、`frag`（碎片 vs 耗尽）、`heap` |
| 新进程拒绝启动 | tasks + `TASKS>64` | `ps` 找未被收割/僵尸形状的行 |
| 磁盘感觉卡死 | 两份快照之间的 disk 增量 | `dskstat`、`sync`、`lsblk` 中的块缓存统计 |
| 网络无声 | net tx/rx 增量 | `netstat`、`tcpstats`、各驱动状态 op |

间隔几秒的两份快照把累计计数器变成速率 —— 这正是
`health watch` 实时展示的内容（1 秒刷新，ENTER 退出）。

## 7. 验证

E2E（`tools/wp12_watchdog_logging_e2e.py`）演练健康分组：
S11 运行 3 个自旋任务并在内核保持应答的同时断言 CPU 告警；
JSON 导出场景断言 `/var/log/health.json` 可解析（标准指标
集，约 386-389 字节）—— 包括重启之后从 /etc 卷导出、可被
任何监视工具解析。告警去重也被断言：ERROR klog 行每次
越限只出现一次而不是每秒一次，并在指标恢复时重新布防。
