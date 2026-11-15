<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：内核日志（klog）

统一内核日志（WP-12）：五个级别、四条通道、1024 项 RAM
环、实时过滤器，以及一个轮转的磁盘文件。机制位于
`ocos/kernel/core_lib_klog.{c,h}`；ring-3 接口面
（`dmesg`/`logcat`/`logctl`）位于 `ocos/services/klog/`；
磁盘侧汇聚位于 `services/klog/src/svc_klog_file.c`。使用侧
指南见 [../dev/debug-handbook.md](../dev/debug-handbook.md)
第 2 节。

## 1. 环（原文照录）

```c
#define KLOG_RING_EARLY 24                 /* static BSS bootstrap ring (4 KiB) */
#define KLOG_RING_MAX   1024               /* after klog_grow() (heap)   */
#define KLOG_MOD_MAX  12
#define KLOG_MSG_MAX  144

typedef struct {
    u64  ts_ms;
    u8   level;
    char module[KLOG_MOD_MAX];
    char msg[KLOG_MSG_MAX];
} klog_entry_t;

static klog_entry_t g_ring_early[KLOG_RING_EARLY];
static klog_entry_t *g_ring = g_ring_early;
static u32 g_ring_cap = KLOG_RING_EARLY;
static volatile u32 g_ring_head = 0;   /* next write slot               */
static volatile u32 g_ring_count = 0;  /* valid entries (<= cap)        */
static volatile u64 g_total = 0;       /* absolute entries ever written */
static volatile u64 g_dropped = 0;     /* slots overwritten since boot  */
```

- **触发**：任何内核代码调用
  `klog_write(level, module, fmt, ...)` —— 每个子系统的错误
  路径、健康告警、看门狗策略 kill、OCIRE 拒绝；
  `klog_init()` 在任何日志之前运行（kmain 步骤 3），
  `klog_grow()` 紧跟 `mem_heap_init` 之后。
- **处理**：24 项 BSS 引导环承载最早的启动行（内核镜像被
  钉在 0x400000 以下，因此大环放在堆上）；`klog_grow()`
  kmalloc 出 1024 项环（168 KiB）并把启动条目拷贝过去 ——
  一条不丢。槽位认领是一段短促的 cli/sti 临界区（IRQ
  安全）。槽位数学：
  `klog_slot_of(seq)` -> `off = g_total - seq (>=1)`；
  `slot = (g_ring_head + g_ring_cap - off) % g_ring_cap`。
- **影响**：即使实时通道被静音或过滤，环也始终记录 ——
  它是事后剖析的事实来源；`klog_dropped()` 统计被覆写的
  槽位（显示在 dmesg 页脚）。

## 2. 级别与通道

- 级别：`KLOG_DEBUG 0 ... KLOG_FATAL 4`；逐级独立开关
  （`g_level_mask = 0x1F` 默认全开）；控制台+串口的实时门
  `g_live_level = KLOG_INFO`（-1 静音实时通道；环仍记录）。
- 四条通道：串口 COM1（轮询 `arch_exc_ser_puts`）、控制台
  VGA、RAM 环（永远）、文件 VFS `/var/log/kernel.log`。
  调用时中断被禁用则跳过 FILE 通道（推迟到下一个任务上下
  文写方/flush/定时器踢）。
- 行格式，环回放、文件与实时通道中完全一致：
  `[   12.345] WARN  net: link up (e1000)` —— 时间戳 =
  自 `klog_init` 以来的秒.毫秒（`core_timer_now_ms`；
  PIT 之前为 0.000）。

## 3. 发射钩子与磁盘通道

`klog_set_emit_hook(fn)` 安装唯一观察者；`klog_on_mount()`
（kmain 在首个文件系统挂载后调用）安装来自
`svc_klog_file.c` 的 `klog_file_hook` 并把启动日志转储到
`/var/log/kernel.log`。状态：`g_fd`（O_APPEND 打开的 fd，
-1 = 关闭）、`g_bytes`、`g_synced`、`g_dirty`、
`g_rotate_bytes`、`g_rotate_keep`。

- `kf_sync()` 经 `klog_entry_render` 追加每条尚未同步的
  条目，随后在阈值处自动轮转。
- 轮转：`KLOG_ROTATE_BYTES` = **1 MiB**（1048576；实时的
  `logctl threshold` 接受 512..16777216）、
  `KLOG_ROTATE_KEEP 3`：
  `kernel.log` -> `.1` -> `.2` -> `.3`；先 flush、后移位；
  丢弃最后一代。`kf_shift()` 先 unlink `.keep`，把
  `.gen-1 -> .gen` 一直改名到 2（防 u32 回绕的守卫），然后
  `kernel.log -> kernel.log.1`。
- `klog_snapshot_text(out, cap)` 把整个环从旧到新渲染 ——
  OCIRE 代理的 KLOG_SNAPSHOT 消费者。
- `klog_flush_panic()` 服务崩溃路径（仅环，该深度没有
  VFS）。

## 4. 活动过滤器

`klog_filter_set`（级别（-1 任意 / 0..4 该级及以上）、
模块精确匹配、关键字子串；同一时刻只有一个过滤器，最后一次
调用生效）。**触发**：`logctl filter ...`。
**处理**：作用于实时通道与回放（`dmesg`/`logcat`）。
**影响**：为安静会话而设的过滤器也会改变之后 `dmesg`
显示的内容 —— 困惑时用 `logctl filter clear`。

## 5. ring-3 接口面（3 个 op）

| Op | 命令 | 行为 |
|---|---|---|
| `L1SVC_KLOG_DMESG` (1) | `dmesg [N]` | N = 行数（默认 30，1..1024）；经活动过滤器回放尾部；页脚 `-- <shown> lines (ring <count>/1024, dropped <n>) --` |
| `L1SVC_KLOG_LOGCAT` (2) | `logcat -l lvl -m mod -k key -t sec -n N [-f]` | 过滤回放；跟随模式是 v2.2.0 在 op *内部*的控制台侧循环：10 Hz 轮询（100 ms hlt 拉伸检查 ENTER）；打印 `-- following (press ENTER to exit) --` / `-- follow stopped --` |
| `L1SVC_KLOG_LOGCTL` (3) | `logctl ...` | 11 个子命令：`status`、`level [-1..4]`、`enable\|disable <level>`、`clear`、`flush`、`rotate`、`threshold <bytes>`、`export [path]`、`test`、`filter clear\|[level] [module] [keyword]` |

`logctl test` 每级输出一行（模块 `klogtest`）—— 过滤器
验证工具。错误锚点：
`klog: flush FAILED (no filesystem mounted?)`、
`klog: flushed N bytes to /var/log/kernel.log`、
`klog: rotated (kernel.log -> kernel.log.1, keep 3)`、
`logctl: level needs -1..4`。

## 6. 谁写、谁读

- 写方：每个子系统的失败分支；`health` 告警越限（模块
  "health"）；看门狗策略 kill；崩溃路径
  （`klog_record(KLOG_FATAL, "kernel", reason)` 最先、仅
  环）；OCIRE 守卫拒绝（模块 "ocire"）；启动引导通告。
- 读方：`dmesg`/`logcat`/`logctl`（环 + 状态）、文件通道
  （挂载后）、OCIRE 代理（快照）、panic 路径（flush）、
  E2E 套件（`wp12_watchdog_logging_e2e.py` 的 S1-S6：
  级别、开关、过滤器、回放、轮转链、持久化）。
- 日志*就是*系统的叙事；`logctl` 是你压低噪声而不丢失
  错误的手段。

## 7. 交互注记

- klog 家族声明：
  `OC_SERVICE_DECL_L1("klog", "klog service (ring + on-disk channel + filters + ring-3 surface)", 0, 0, klog_l1_dispatch);`
  - 没有 init 槽位，机制在 kmain 中启动。
- `log.level` 配置键的安静模式（来自
  `/etc/opencube.conf`）抑制非失败的启动阶段行
  （`core_lib_log` 阶段日志器 —— 一个独立机制，打印
  `[HH:MM:SS.mmm] stage ......... OK` 启动阶梯）。
- `crashlog`（32 槽位崩溃环）是崩溃家族里一个*不同的*环
  —— 见 [crash-dump.md](crash-dump.md)。
