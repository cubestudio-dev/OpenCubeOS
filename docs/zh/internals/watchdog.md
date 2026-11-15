<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：看门狗子系统

一个子系统里的两个看门狗世界：**软件槽位引擎**（8 个受
监视槽位、逐槽位策略、1 秒 tick）与**硬件看门狗**（ACPI
WDAT 表、LPC 桥上的 Intel TCO）—— 外加 OCBL 在内核存在
之前布防的 **6300ESB 启动状态继电器**。实现：
`ocos/services/crash/src/svc_crash_watchdog.c` +
`interface/svc_crash_watchdog.h`（统一 API），软看门狗在
`svc_crash_core.c`，L1 接口面在
`ocos/services/watchdog/src/svc_watchdog_l1.c`（302 行），
启动状态继电器在 `ocos/kernel/core_boot_state.c`。

## 1. 软看门狗（crash_wdt）

- `OC_WDT_TIMEOUT_MS 15000` —— 一个 **15 秒**的软件看门狗。
- **触发**：一个周期性 core_timer 回调检查心跳戳；shell
  行编辑器每次按键都踢它。
- **处理**：若内核在*狗已布防*的情况下 15 秒无人伺服输入，
  panic 路径以崩溃来源 `OC_CRASH_SRC_WDT`（33）触发 ——
  完整的 WP-11 崩溃待遇（转储 + crashlog + 策略）。
- **影响**：卡死的控制台变成铁证而不是无声挂死。软狗
  **以解除布防状态启动**（文档化决策）：长的同步命令独占
  shell 任务、无法踢狗。
- API：`crash_wdt_init/kick/blackout/armed/ms_since_kick/ force_timeout`。

## 2. 软件槽位引擎（规格 5.2）

至多 8 个受监视槽位（`WD_MAX_SLOTS 8`），布防时每 1 秒
tick 检查：

```c
#define WD_POLICY_PANIC  0   /* kernel_panic("watchdog timeout: ...") - full WP-11
                               crash path (dump + log + policy) */
#define WD_POLICY_REBOOT 1   /* klog ERROR + core_power_reboot() directly */
#define WD_POLICY_KILL   2   /* klog ERROR + core_sched_task_exited(tid) - the
                               monitored task is killed, kernel lives */
#define WD_POLICY_LOG    3   /* klog ERROR once per timeout episode (no action;
                               re-arms on the next feed) */

#define WD_MAX_SLOTS      8
#define WD_HW_TIMEOUT_MIN 2      /* TCO/WDAT seconds floor           */
#define WD_HW_TIMEOUT_MAX 63     /* 6-bit TCO timer field            */
```

槽位 0 = **"system-heartbeat"**：由 `watchdog_init()`
注册，包裹 WP-11 的 shell 心跳语义（默认超时
`OC_WDT_TIMEOUT_MS` 15 秒，策略 PANIC）；`watchdog_feed(0)`
是 L1 可见的踢狗，健康告警作为额外来源馈送它
（告警但活着的系统继续喂食 —— 见
[health.md](health.md) 第 4 节）。

自省结构体（原文照录）：

```c
typedef struct {
    char name[16];
    u32  timeout_ms;
    int  policy;
    int  running;                  /* armed                            */
    u64  last_feed_ms;
    u64  timeouts;                 /* episodes seen so far            */
    int  tid;                      /* creator tid (KILL policy)       */
} watchdog_info_t;
```

注册返回槽位 id >= 0，表满为 -1，坏参数（NULL/空名、
超时为 0、坏策略）为 -2。

## 3. 硬件看门狗

启动时的探测顺序（探测但**不布防** —— 文档化决策；布防
须经 `watchdog hw start <sec>` 显式进行）：

1. **ACPI WDAT 表**（`driver_acpi_find_table("WDAT")`）——
   条目的 GAS 寄存器驱动每一个动作。QEMU 下，
   `tools/make_wdat_table.py` 为 `-acpitable` 注入构造一个
   合成但合法的 WDAT，驱动真实的 q35 TCO 寄存器
   （WDAT 发现的 WP-12 E2E 门禁）。
2. **Intel TCO** 位于 LPC 桥（PCI 00:1f.0）：PMBASE（配置
   空间 0x40）+ 0x60 -> TCO_RLD/CNT/TMR。在 QEMU q35
   （ICH9-LPC）上存在；默认 `pc` 机型（PIIX3）上缺席 ——
   "探测诚实地报告 'none'"。

TCO 范围 2..63 秒（`WD_HW_TIMEOUT_MIN/MAX` —— 6 位 TCO
定时器字段）。软硬协同：软件检查在每次 tick 上*先*运行
（WP-11 panic 路径）；硬件 TCO 始终是最后手段的硬复位 ——
**"框架同时布防两者时，硬件超时被设置为软件超时的 >= 2
倍"**（双布防规则：硬件 >= 2 x 软件，这样软件 panic 路径
总能在硅片复位之前得到产出转储的机会）。

硬件 API：`watchdog_hw_present/name/start(seconds)/feed/stop/ set_timeout/timeout/base/armed/gcs_noreboot`（GCS bit5 的
活体回读即 "no-reboot" 状态）。

## 4. 6300ESB 启动状态继电器

第三只看门狗，归启动状态监视器而非看门狗服务所有：OCBL
在交接给内核之前布防 **Intel 6300ESB** 看门狗（OCIRE
监督循环所用的同一 PCI 设备）；`core_boot_state.c` 以
原始 PCI/IO 周期接管（配置空间 0x60 在 16 位写入时锁存，
锁寄存器 0x68 仅在 8 位写入时，RELOAD 需要 0x80/0x86 的
解锁舞步；探测 = PCI 8086:25AB，BAR0 解析为 I/O 或
MMIO）。布防期间的阶段预算：30 秒 boot start -> 阶段 1，
中间每阶段 10 秒，30 秒 L1_LAUNCHED -> L1_READY，
**15 秒运行时心跳**由 1 秒 tick 喂养。挂死停止喂食 ->
6300ESB 复位机器（热复位，DRAM 保留）-> OCBL 判定 ->
记录路由到 OCIRE。`/HANGAT`（`oc.hang=<stage>`）刻意演练
真实继电器链；`oc.stmon=off` 关闭整个监视器。在 OCIRE
监督（kexec）启动上，嵌入服务在监督者的 60 秒窗口内解除
i6300esb 布防（见 [ocembed-guard.md](ocembed-guard.md)）。

## 5. L1 接口面（1 个 op 承载全部语法）

```c
#define L1SVC_WDT_CMD 1     /* watchdog [subcommand ...]     */
```

子命令：`status`（默认；hw 名称/布防/超时/基址 + 槽位表）、
`slots`、`hw status|start [sec=30]|stop|feed|timeout <2..63>`、`start|stop|feed <id>`、`timeout <id> <ms 100..3600000>`、`register <name> <ms> <0=panic 1=reboot 2=kill 3=log>`、`test kill|panic|log`（经
`core_kthread_create` 孵化 `wd-starve` kthread，优先级 15，
把自己注册为受监视 3 秒且从不喂食 —— "expect the policy
in 3 s"）。

值得注意的字符串：`watchdog hw: ARMED - machine RESETS if nobody feeds`、`watchdog hw: none (no WDAT table, no Intel TCO LPC)`、`watchdog hw: no hardware watchdog present`、
`watchdog: register FAILED (table full?)`、
`watchdog: bad slot id`。

## 6. 从内核代码使用一个槽位

```
slot = watchdog_register("mydaemon", 5000, WD_POLICY_KILL);
watchdog_start(slot);
... watchdog_feed(slot);   /* at least every 5 s */
```

超时触发 -> 策略运行：任务被杀且理由进入内核日志
（KILL）、内核 panic（PANIC）、机器重启（REBOOT），或每
episode 一条 ERROR（LOG —— 下次喂食时重新布防）。这是
"我的后台工作者可能挂死"的正确工具；KILL/LOG 之下内核
继续活着。

## 7. 为此把关的 E2E 场景

S7（status/slots）、S8（策略 kill + log）、**S12：Intel
TCO 真实复位 + 第二次启动**（q35、`-no-reboot`：发现、
布防、饿死、断言第二次启动）、**S13：ACPI WDAT 真实复位 +
第二次启动**。`tools/wp12_watchdog_logging_e2e.py` 共十四
个场景 —— 发布时十四个必须全部 PASS。
