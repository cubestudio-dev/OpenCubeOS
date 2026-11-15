<!-- SPDX-License-Identifier: Apache-2.0 -->
# 崩溃恢复：panic、转储、crashlog、看门狗与 OCIRE 接管

内核失败时，四个机制依次触发——panic 路径捕获当下，crashlog 环
记住历史，软看门狗捕捉静默，引导状态接力把机器交给 OCIRE。文件：
`ocos/services/crash/`（`svc_crash_core.c` 1018 行、
`svc_crash_watchdog.c`）、
`ocos/kernel/arch/x86_64/arch_exceptions.c`（分派器一侧）、
`core_boot_state.c`（接力的内核一半）。分层结构，逐字引自核心
头文件：

```
 *   exception entry (arch_exc_dispatch, ring-0 fault)
 *        └─> kernel_panic_frame(reason, frame)      [this module]
 *              ├─ crashlog_record(...)              ring buffer entry
 *              ├─ panic banner + register snapshot  serial + VGA(red)
 *              ├─ stack traceback                   rbp chain + scan
 *              ├─ core_dump_save(...)               memory + /var/crash
 *              ├─ registered panic handlers         L1 observers
 *              ├─ crashlog_flush()                  /var/log/crash.log
 *              └─ policy: HALT (cli;hlt) or REBOOT (8042 + 0xCF9)
```

## 异常帧（捕获形态）

来自 `arch_idt.h`——22 个 u64，与 `OC_DUMP_REGS_U64 22` 一致：

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

## panic 横幅与回溯

`kernel_panic(reason)` / `kernel_panic_frame(reason, frame)` 是核心
槽位；实现由 crash 服务在引导时经 `arch_set_panic_handlers`
（+ `arch_set_crashlog_record`）安装。安装之前，回退路径打印
`\nKERNEL PANIC (no crash handler installed): <reason>\nSystem halted.` 并 cli;hlt。

真实路径（`svc_crash_core.c`）：

1. **嵌套 panic 守卫**：`*** NESTED PANIC INSIDE PANIC - HALTING ***` -> cli;hlt。
2. **先做 CR3 规范化**：`mem_vmm_switch_as(kernel_as)`，使回溯/
   转储/堆读取即便从用户地址空间发起也安全。
3. `klog_record(KLOG_FATAL, ...)`——只进环，深度处不用 VFS。
4. 横幅（串口为权威，VGA 红色尽力而为）：
   `*** KERNEL PANIC: <reason> [task=<name>] ***`、`kernel: Open Cube OS <version> (build 0x<hex>)`、`source: exception <n> (<name>) err=0x<hex>`，随后是寄存器转储（4x4 网格 r15..rax +
   rip/cs/ss/rflags/rsp/int/err，#PF 另加 cr2 与 cr3）、任务行，
   以及**回溯**——rbp 链遍历外加经 `looks_like_code` 过滤的栈
   扫描。
5. #DF rsp < stack_base 时的栈耗尽注记：
   `[KERNEL STACK OVERFLOW: exhausted, task=<name>]`。
6. 核心转储保存、crashlog 冲刷、已注册的 panic 处理器（最多 4 个，
   最近注册者先运行；返回非零的处理器认领恢复：`*** panic handler claimed recovery - resuming ***`）。
7. **策略**：HALT（默认，cli + 安全 hlt 循环）或 REBOOT（8042 +
   0xCF9）——`crashtest policy [halt|reboot]` 实时切换。

**#DF 特例**有意保持最小：完全在分派器深度处理，只用一个静态
192 字节串口行缓冲经 `arch_exc_ser_puts`——“No record, no dump,
no flush for #DF (stated in KNOWN_ISSUES)”。横幅：`source: #DF Double Fault err=0x.. rip=0x.. [h]handled at dispatcher depth`（该
typo 在源码中逐字如此）。`crash_contained()` 是非致命变体：记录 +
串口提示，返回（TUI 守卫机制）。

`OC_ASSERT(cond)` -> `kernel_panic("assertion failed: " #cond)`。
`crashtest panic|exc|pf|assert|wd|ovf|dump|policy` 按需注入其中
每一条路径（`exc` 执行 `ud2`；`pf` 写 0x700000000000——PML4[224]，
保证 not-present，因为 C 里的 NULL 解引用会被 UB 折叠成 ud2，而且
第 0 页是恒等映射的）。

## 核心转储（OCDUMP01）

```c
#define OC_DUMP_STACK_BYTES   4096    /* kernel-stack window copy     */
#define OC_DUMP_SERIAL_MAX    2048    /* hex-print cap when diskless  */
#define OC_DUMP_REGS_U64      22      /* arch_irq_frame_t, in u64s    */

typedef struct {
    char magic[8];            /* "OCDUMP01"                          */
    u32 hdr_len;              /* sizeof(oc_dump_hdr_t)               */
    u32 image_len;            /* total bytes captured                */
    u64 build;                /* OC_KERNEL_BUILD                     */
    char version[24];         /* OC_RELEASE_VERSION                  */
    char reason[40];          /* short panic reason                  */
    u64 jiffies;
    u64 rtc_epoch;
    u64 cr3;                  /* page-table root at capture time     */
    u64 pte_count;            /* present leaf PTEs walked            */
    u32 tid, pid;
    u32 ring;
    char task[16];
    u64 stack_base, stack_size, stack_rsp;   /* kernel-stack window  */
    u32 stack_copy_len;       /* bytes actually copied               */
    u32 regs_present;         /* 1 = frame captured, 0 = live regs   */
} oc_dump_hdr_t;
```

镜像布局按捕获顺序：头部、寄存器（22 个 u64 或实时快照）、任务
块、栈窗口（至多 4 KiB，裁剪到任务栈内）、表（CR3 + PML4 自扫描
的 PTE 计数）。持久化到 `/var/crash/core.dump`——在 ramfs rootfs
与 FAT32 挂载上都可用；若没有任何文件系统就绪，前 2048 字节以
十六进制转储到串口，数据不至于丢失。缓冲由 `core_dump_alloc()`
在引导时分配——kmalloc，不是 .bss，**一次、预先**：“此后
kmalloc 可能失败、堆可能损坏，但 panic 路径只触碰已预留的缓冲”
（见 [memory.md](memory.md)）。

## crashlog 环

```c
#define OC_CRASHLOG_CAP        32
#define OC_CRASH_SRC_PANIC     32
#define OC_CRASH_SRC_WDT       33
#define OC_CRASH_SRC_ASSERT    34
#define OC_CRASH_SRC_CONTAINED 35
#define OC_CRASH_SRC_STACKOVF  36

typedef struct {
    u64 jiffies;         /* core_timer_ticks() at record time        */
    u64 rtc_epoch;       /* seconds since 1970 (0 = rtc unavailable) */
    u32 vector;          /* 0..31 exception, 32..36 synthetic source */
    u32 ring;            /* 0 = kernel, 3 = user                     */
    u64 error_code;
    u64 rip;
    u64 rsp;
    u64 cr2;             /* #PF faulting address, 0 otherwise        */
    i32 tid;
    i32 pid;
    char task[16];       /* task name (or "user:<pid>")              */
} crashlog_entry_t;
```

32 个条目；落到系统任何位置的**每一个**异常（含用户击杀，经分派器
的 crashlog 记录钩子）都追加一条。`crashlog`（L1SVC diag op 6）
打印每条记录；`crashlog flush` 把环追加到 `/var/log/crash.log`；
空环时：`"crashlog: no exceptions recorded"`。

## 看门狗：软的、槽位化的、硬件的

**软看门狗**（`crash_wdt`）：`OC_WDT_TIMEOUT_MS 15000`——周期性
core_timer 回调检查心跳戳；shell 行编辑器每键踢它一次；布防状态
下静默 15 秒即以来源 `OC_CRASH_SRC_WDT` panic。启动时处于解除
状态（长同步命令独占 shell 任务、无法踢狗）。

**统一槽位引擎**（spec 5.2）：最多 8 个受监视槽位，1 秒节拍检查
每个已布防槽位，四种策略：

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

槽位 0 = "system-heartbeat"（由 `watchdog_init()` 注册，包住 WP-11
的 shell 心跳语义，默认超时 15 秒，策略 PANIC；`watchdog_feed(0)`
是 L1 可见的踢狗）。`watchdog` 命令暴露一切：`status`、`slots`、
`hw start|stop|feed|timeout <2..63>`、按槽位的
`start|stop|feed|timeout|register`，以及 `test kill|panic|log`
（拉起一个自注册 3 秒却从不喂狗的 `wd-starve` kthread——“预期
3 秒内看到策略”）。

**硬件**：引导时探测（先 ACPI WDAT 表，再 LPC 桥上的 Intel TCO
——QEMU q35 上存在、默认 pc 上没有，“探测会如实报告 'none'”）但
**绝不在引导时布防**；每个节拍上软件检查先行，且当软硬件同时布防
时，硬件超时被设为 >= 软件超时的 2 倍。ring 3 面是 `watchdog`
（cat 11，1 个 op 承载整条子命令行）。

**Health 联动**：告警作为额外喂送源喂给槽位 0——告警但仍活着的
系统持续喂狗，死掉的系统停止喂狗，panic 路径随之触发
（见 [../internals/health.md](../internals/health.md)）。

## OCIRE 接管链

当 panic 策略为 REBOOT——或内核挂死到 OCBL 布防的 6300ESB 看门狗
触发时——恢复链如下：

1. 复位。0x98000/0x9B000 孤岛幸存（E820 保留，经实测可在复位中
   存活）。
2. OCBL 的 statemon 引导检查读取它们：阶段卡在 RUNTIME 之下 +
   `>= 8 秒` RTC 静默 -> 判定 BOOT_FAILED；OCBL 写下事后记录
   （`ocst_record_t`：类别、阶段、静默秒数、seq、时间戳、反码
   校验和），显示 4 秒提示，脉冲 8042 复位。
3. 下一次引导，`ocbl_statemon_route_pending()` **一次性**自动选择
   OCIRE 条目（`routed` 位使自动恢复严格单发——连续失败的循环把
   用户送进恢复 shell，而不是复位风暴）。
4. OCIRE 解释自己：`ocire_ocbl_report()` 读取记录并打印原因——
   USER_REQUEST（“手动恢复会话”）或 BOOT_FAILED（附阶段名、静默
   秒数与按阶段的修复建议：RESET/KM_AIN/SCHED -> `syscheck`；
   DRIVERS -> `syscheck` + 复查驱动；SERVICES -> `repair`；
   L1_READY/运行时 -> `logread` + `repair`）。
5. OCIRE shell 的 `syscheck` 运行**十点**完整性审计（磁盘、恢复
   卷、系统卷、邮箱、日志环、代理摘要 vs 邮箱 SHA-256、黄金清单、
   黄金文件、系统文件、内核副本）；`repair quick` 修复元数据 +
   还原有差异的文件；`repair full` 从黄金镜像破坏性地重格式化
   系统卷（需输入 `YES`）；`return` 清空邮箱并正常重启。

状态孤岛的完整字节布局：`include/ocbl_state.h`
（见 [hidden-mechanisms.md](hidden-mechanisms.md)）；监督引导的
邮箱之舞：[system-map.md](system-map.md)；面向用户的恢复：
[../manual/recovery.md](../manual/recovery.md)；转储格式内部：
[../internals/crash-dump.md](../internals/crash-dump.md)、
[../internals/watchdog.md](../internals/watchdog.md)。
