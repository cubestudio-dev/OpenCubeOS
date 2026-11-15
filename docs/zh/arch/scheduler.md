<!-- SPDX-License-Identifier: Apache-2.0 -->
# 调度器：32 个槽位、优先级与切换

调度器是 `ocos/kernel/core_sched.{c,h}`（959 行），上下文切换位于
`ocos/kernel/arch/x86_64/arch_context_switch.S`。设计上单 CPU（无
SMP、无 AP 启动、无 per-CPU 区；LAPIC 仅用于 MSI 投递与自 IPI——
见 [interrupts.md](interrupts.md)）。

## 模型

**固定优先级（0 = 最高……31 = 最低，默认 15）加按优先级的 FIFO
轮转。**32 个静态任务槽位。时间片 2 节拍 = PIT 100 Hz 下的 20
毫秒。由 IRQ0 节拍驱动，外加协作式 `core_sched_yield()`。饥饿守卫
在就绪任务连续 10 节拍（100 毫秒）未运行时触发：强制切入最低
优先级的就绪任务（或 idle）——低优先级后台工作绝不会被饱和的高
优先级带饿死。

```c
#define TASK_READY    0
#define TASK_RUNNING  1
#define TASK_BLOCKED  2
#define TASK_EXITED   3
#define TASK_PRIO_MIN  0
#define TASK_PRIO_MAX  31
#define TASK_PRIO_DEFAULT 15
#define MAX_TASKS 32
#define TIME_SLICE_TICKS 2  /* 20ms per slice */
#define SCHED_STARVE_LIMIT 10
```

就绪队列是 `g_ready_queue[32][MAX_TASKS]` 外加一张用 `bsf` 扫描的
32 位占用位图；一切变更都运行在 pushfq/cli 窗口内（单 CPU 原子性
纪律）。idle 任务（tid 0）从不入队——当没有其他就绪任务时它执行
`hlt`。

## 任务控制块（逐字）

```c
typedef int tid_t;
typedef struct task {
    tid_t  tid;
    char   name[32];
    int    state;
    int    priority;
    u64    cpu_time_ticks;   /* total CPU time consumed */
    u64    switch_count;     /* how many times this task was switched to */
    u64    ticks_remaining;  /* time slice remaining */

    /* Saved register context for context switch.
     * The order MUST match context_switch.S. */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;

    /* Stack for kernel threads. */
    u64 *stack_base;     /* allocated stack base */
    u64  stack_size;

    /* Address space (CR3) for this task. 0 = kernel identity mapping. */
    u64 cr3;

    /* RSP0 for TSS (kernel stack top for ring-3 IRQ entry). */
    u64 rsp0;

    /* Function entry point and argument for new tasks. */
    void (*entry)(void *arg);
    void *arg;

    int in_use;

    u64 fpu_state[64] __attribute__((aligned(16)));
    int fpu_saved;
} task_t;
```

保存寄存器的顺序**由 `core_sched.c` 中的 static assert 对照
`arch_context_switch.S` 中的 `%define` 钉死**：`OFF_FPU=288`、
`OFF_FPU_SAVED=800`、`OFF_RFLAGS=208`、RIP=192、RSP=216、CR3=248。
FPU 映像为 64 个 u64 = 512 字节（`fxsave` 区），16 字节对齐。
`cr3` 字段承载地址空间（0 = 内核恒等映射）；`rsp0` 是交给 TSS 的
值，使 ring 3 中断落在本任务的内核栈上。

## 上下文切换（`arch_context_switch.S`）

切换路径为 `core_sched_switch_to` ->
`arch_context_switch(old,new)`：

1. **先做栈边界检查**（C 中）：若保存的 rsp 落在旧任务的
   `[stack_base, stack_base+stack_size)` 窗口之外，调度器以
   `kernel stack bounds violated on switch (task=<name>, rsp=0x.. window=[..,..))`
   panic——被损坏的任务绝不被恢复。
2. 把 15 个 GPR + RFLAGS + CS/SS/RIP/RSP 按上述硬编码偏移保存进
   `task_t`。
3. `fxsave` 把 FPU 映像存入 `fpu_state`（CR4.OSFXSR/OSXMMEXCPT
   在 `core_sched_init` 中启用；每次切换都保存/恢复完整 x87/SSE
   状态——无惰性 FPU 切换）。
4. 加载 `new` 的 CR3（页表切换；内核任务共享恒等映射，因此常是
   空操作式的重载）。
5. `fxrstor` 新任务的映像（首次切入改做 `fninit` +
   `ldmxcsr 0x1F80`），在 `ret` **之前**经 push/popfq 恢复 RFLAGS
   （BUG-0103 修复——在错误时刻恢复 IF 可能嵌套或丢失中断），然后
   `ret` 进入新任务。

fork 的首次进入是特例：`arch_enter_ring3_fork.S`
（`enter_ring3_fork(frame, cr3)`）构建 iret 帧并以 rax=0 恢复全部
22 个寄存器（含调用者保存寄存器——WP04 G7 修复），因此子进程恰好
在父进程调用 fork 之处返回。

## 生命周期

- **内核线程**：`core_kthread_create`——10 页（40 KiB）连续栈
  （`OC_KTHREAD_STACK_PAGES 10`），**最低一页不映射作为守卫页**
  （先拆大页再做高声报警的验证——大页 PDE 的 unmap 否则是无声的
  空操作，见 [memory.md](memory.md)）。idle 任务得 1 页。
- **Ring 3 进程**：由 `core_usermode.c` 中的 ELF 加载器创建；
  task 是内核侧的调度单元，`user_proc_t` 是进程身份（fds、cwd、
  信号、地址空间）。最多 16 个用户进程。
- **退出**：任务被标记 TASK_EXITED，并由 `core_sched_reap_exited`
  在节拍与 yield 两条路径上于 cli 之下收割（BUG-0282 双重释放
  修复）。**统一收割器** `user_process_kill_by_tid` 是唯一被认可的
  击杀路径——旧的 `user_process_kill(pid)` 已带墓碑注释删除（三处
  不变量违反；见 [hidden-mechanisms.md](hidden-mechanisms.md)）。
- **阻塞**：信号量/互斥体/条件变量把任务标记为 BLOCKED 并 yield；
  定时器唤醒睡眠者。看门狗 `kill` 策略直接使用
  `core_sched_task_exited(tid)`。

## 可观测性

`sched`（L1SVC diag op 4）转储统计计数器；`ps`（`SYS_PS` 系统调用）
遍历进程表并返回 `struct ush_ps_entry` 数组（tid、state、priority、
name、cpu_time_ticks）。`nice` 命令与 L1SVC_DIAG_NICE 在运行时测试
优先级变更。引导注册表行 `registry: drv=N svc=N app=N` 来自相邻的
收集器。

## 其他子系统如何倚仗它

- **定时器**：`core_sched_tick` 经 `arch_irq_register_handler(0, ...)`
  注册在 IRQ0 上；它递减时间片、累加饥饿计数器、执行收割。
- **系统调用**：阻塞式系统调用（管道读、wait4、网络 recv）让任务
  yield；系统调用 ABI 本身从不阻塞调度器
  （见 [syscall-ring3.md](syscall-ring3.md)）。
- **驱动**：一切存储/NIC/USB 数据路径都是**轮询**式的，等待循环
  内做协作 yield（例如 `driver_snd_play` 轮询 DMA 位置并 yield）；
  USB 热插拔活在常驻 kthread 上（`driver_usb_poll_start`）。真正的
  IRQ 很少：PS/2 键盘（IRQ1）、COM1（IRQ4）、COM2 H4（IRQ3）、
  音频 INTx（+SB16 IRQ5）、HPET/RTC（IRQ8）。
- **崩溃路径**：栈溢出归因会指名任务；看门狗 KILL 策略击杀受监视
  任务而不停内核（见 [crash-recovery.md](crash-recovery.md)）。
- **Health**：CPU 繁忙百分比由空闲计数器增量计算——调度器为
  health 采样器导出 `core_sched_idle_counters()`
  （见 [../internals/health.md](../internals/health.md)）。
