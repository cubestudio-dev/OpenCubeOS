<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：崩溃转储与 panic 路径

panic 机制：异常路由、panic 横幅、OCDUMP01 核心转储镜像、
32 槽位 crashlog 环，以及 panic 后策略。实现：
`ocos/services/crash/src/svc_crash_core.c`（1018 行）+
`svc_crash_watchdog.c`，核心槽位位于
`ocos/kernel/arch/x86_64/arch_exceptions.c`，启动状态继电器
位于 `ocos/kernel/core_boot_state.c` +
`include/ocbl_state.h`。实战用法：
[../dev/debug-handbook.md](../dev/debug-handbook.md) 第 3 节。

## 1. 分层（逐字摘自崩溃核心头文件）

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

**触发**：无人认领的 ring-0 异常、一次 `kernel_panic(reason)`
调用、一次 `OC_ASSERT(cond)` 失败
（`-> kernel_panic("assertion failed: " #cond)`），或看门狗
策略 PANIC。**影响**：证据在机器停止之前被捕获；策略决定
接下来发生什么。

## 2. 异常路由进入 panic 路径

1. 嵌套 #PF 守卫：#PF 处理中再遇 #PF 会打印
   `*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***`
   并永久 cli;hlt。
2. L1 处理链（每向量至多 `OC_EXC_CHAIN_LEN 4` 个，最近
   注册者先运行；返回 1 = 已处理/恢复）。
3. #PF -> `mem_vmm_handle_page_fault`（故障 RSP 之下 64
   KiB 且在 8 MiB 下限之上的栈增长；保护违例绝不重映射）。
4. ring-3 来源（`cs & 3 == 3`）：尝试 SIGSEGV 投递
   （有 handler + 不在 handler 中 + 投递少于 3 次；栈上
   restorer 蹦床），否则记录 crashlog 并打印
   `*** KILLING user process (pid=N) due to exception V, kernel continues ***` + 统一回收器 —— 内核对用户故障
   *不* panic。在 IST2 向量（1/2/3）上采取的 kill 先切换
   到专用 8 KiB 退出栈（BUG-0139），因此 IST2 从不被
   挂起/恢复。
5. ring-0 -> 带栈溢出归因的 panic：#PF 的 CR2 落在任务
   守卫页内 -> `"KERNEL STACK OVERFLOW (guard page hit, task=<name>)"`；#DF 的 rsp 位于/低于栈底或为不可信的
   垃圾（>= 0x40000000000）-> `"KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)"`；否则
   `"ring-0 exception <v> (<name>)"`。

## 3. 寄存器快照（原文照录）

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

22 个 u64 —— 与 `OC_DUMP_REGS_U64 22` 匹配，因此转储器的
寄存器副本*就是*这个结构体。逐异常解读指南（#PF 的 cr2、
错误码位、#DF 的 rsp 对栈窗口）：
[../dev/debug-handbook.md](../dev/debug-handbook.md) 3.4。

## 4. OCDUMP01 镜像（原文照录的头）

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

镜像按捕获顺序的布局：头、寄存器（22 个 u64 或活体快照）、
任务块、栈窗口（至多 4 KiB，裁剪到 `[stack_base, +size)`）、
表（CR3 + PML4 自扫描 PTE 计数）。

- **触发**：panic 路径调用 `core_dump_save(reason)`；或
  `crashtest dump`（不 panic）。
- **处理**：填充启动保留缓冲区。`core_dump_alloc()` 已在
  kmain 步骤 16a 用 kmalloc 运行过一次 —— 此后 kmalloc
  可能失败或堆可能损坏，但 panic 路径只碰已保留的缓冲区。
  其他一切之前先把 CR3 归一化到内核 AS
  （`mem_vmm_switch_as(mem_vmm_kernel_as())`），使得从
  用户地址空间出发的 traceback/转储/堆读取都安全。
- **影响**：持久化到 `/var/crash/core.dump`（ramfs rootfs
  与 FAT32 挂载都可用）；若无文件系统，前
  `OC_DUMP_SERIAL_MAX`（2048）字节以十六进制转储到串口，
  数据不丢失。#DF 场景完全跳过转储
  （"core dump: skipped in #DF mode" —— 见 KNOWN_ISSUES）。

## 5. crashlog 环（原文照录）

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

32 个槽位，启动时分配（`crashlog_boot_alloc()`）；每次异常
kill 与 panic 来源都会追加；`crashlog_flush()` 把所有条目
追加到 `/var/log/crash.log`。回读命令：`crashlog`（每条
`[i] <src-name> ring=N err=0x.. rip=0x.. rsp=0x.. [cr2=0x..] [@<rtc>s | @tick=N] [task=<name>] [pid=N]`；为空时：
`crashlog: no exceptions recorded`）与 `crashlog flush`。

## 6. panic 横幅格式

串口为权威，VGA 红底尽力而为：

```
*** KERNEL PANIC: <reason|"(no reason)"> [task=<name>] ***
kernel: Open Cube OS <version> (build 0x<hex>)
source: exception <n> (<name>) err=0x<hex>
<task line>
<register dump: 4x4 grid r15..rax + rip/cs/ss/rflags/rsp/int/err [+ cr2] + cr3>
<traceback: rbp chain + stack scan filtered by looks_like_code>
<policy line>
```

特殊横幅：`[KERNEL STACK OVERFLOW: exhausted, task=<name>]`；`*** NESTED PANIC INSIDE PANIC - HALTING ***`
（嵌套 panic 守卫 cli;hlt）；`*** panic handler claimed recovery - resuming ***` / `*** CONTAINED: <what> - isolated, kernel continues ***`（某个已注册处理程序认领了恢复 ——
`crash_contained()` 是 TUI 守卫所用的非致命变体）。#DF
横幅是刻意最小化的 —— 完全在分发器深度经
`arch_exc_ser_puts` 以一个静态 192 字节行缓冲处理，一处
已知拼写错误原样保留：
`source: #DF Double Fault err=0x.. rip=0x.. [hhandled at dispatcher depth]`。

## 7. 策略与处理程序

- `CRASH_POLICY_HALT 0`（默认：cli + 安全 hlt 循环）或
  `CRASH_POLICY_REBOOT 1`（8042 + 0xCF9）。
  `crashtest policy [halt|reboot]` 在线切换。
- panic 处理程序：至多 `OC_PANIC_HANDLER_MAX 4` 个，最近
  注册者先运行；该函数刻意不标 `noreturn` —— 返回非零即
  认领恢复。
- 核心槽位由 services/crash 在启动时经
  `arch_set_panic_handlers`（+ `arch_set_crashlog_record`）
  安装；内核侧回退（尚无崩溃服务时）打印
  `KERNEL PANIC (no crash handler installed): <reason>`
  然后 cli;hlt。

## 8. IST 纪律及其重要性

IST1 = 8 KiB panic 栈（#DF/#MC）；IST2 = 8 KiB 调试栈
（#DB/NMI/#BP）。故障处理期间的再故障仍能到达转储器，
因为 #DF 运行在 IST1 上。异常退出路径从不在 IST2 上做
上下文切换（BUG-0139）：在 IST2 向量上采取的 kill 先在
专用退出栈上运行 —— 调度器绝不能保存一个位于 IST2 之内
的 rsp。

## 9. 启动状态继电器（启动失败到达 OCIRE）

挂死（而非 panic）由另一条链捕获：启动状态监视器在喂
6300ESB 看门狗的同时，把阶段/心跳/进度写入固定的低内存
孤岛（状态区 0x98000、保留区 0x9B000 —— 经实证能在
QEMU/SeaBIOS 复位清除中幸存）。挂死停止喂食 ->
6300ESB 复位机器（DRAM 保留）-> OCBL 读状态区，判定
（无 CLEAN 标记且沉默 >= 8 秒；手动热复位实测 < 约 4 秒、
*不是*失败），写入 BOOT_FAILED 记录（停滞阶段、时间戳、
实测沉默、反码校验和）-> 下次启动自动启动 OCIRE ->
OCIRE 读该记录并打印停滞点 + 修复建议。一次干净的
`reboot` 会先给状态区打上 CLEAN 标记。内核侧是**只盲写**
的 —— 它从不回读该区域；0x98000/0x9B000 页位于每个用户
地址空间的 supervisor 专属半区（在 L1_READY 时经 CR3
遍历验证）。结构体：
[../dev/data-structures.md](../dev/data-structures.md)
第 8 节。整条链由 `tools/wp07a_state_e2e.py` 实时驱动。

## 10. 测试

- `crashtest panic|exc|pf|assert|wd|ovf|dump|policy`（diag
  op 7）—— 前六个会真的让内核崩溃；`dump`/`policy` 是
  安全的。`exc` = `.byte 0x0F,0x0B`（#UD）；`pf` 写
  0x700000000000（PML4[224]，保证不存在）。
- `crashlog` + `dumpinfo` 证明重启后的往返。
- 回归电池每次运行都检查 `crashlog`；E2E 的
  `wp11_crash_e2e.py` 覆盖 #DE/#UD/#PF 自检、
  sigsegv/stackovf/badapp kill、crashlog + flush 到
  /var/log、dump + dumpinfo。
