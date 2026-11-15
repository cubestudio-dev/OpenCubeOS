<!-- SPDX-License-Identifier: Apache-2.0 -->
# Scheduler: 32 slots, priorities, and the switch

The scheduler is `ocos/kernel/core_sched.{c,h}` (959 lines) with the
context switch in `ocos/kernel/arch/x86_64/arch_context_switch.S`.
Single-CPU by design (no SMP, no AP startup, no per-CPU areas; LAPIC
exists for MSI delivery and self-IPI only - [interrupts.md](interrupts.md)).

## The model

**Fixed-priority (0 = highest ... 31 = lowest, default 15) with
per-priority FIFO round-robin.** 32 static task slots. Time slice 2
ticks = 20 ms at the PIT's 100 Hz. Tick-driven from IRQ0 plus
cooperative `core_sched_yield()`. A starvation guard fires after 10
ticks (100 ms) of a ready task never running: the lowest-priority
ready task (or idle) is forced in - low-priority background work can
never be starved to death by a saturated high-priority band.

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

Ready queues are `g_ready_queue[32][MAX_TASKS]` plus a 32-bit
occupancy bitmap scanned with `bsf`; all mutations run under
pushfq/cli windows (single-CPU atomicity discipline). The idle task
(tid 0) is never queued - it runs `hlt` when nothing else is ready.

## The task control block (verbatim)

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

The saved-register order is **pinned by static asserts in
`core_sched.c` against `%define`s in `arch_context_switch.S`**:
`OFF_FPU=288`, `OFF_FPU_SAVED=800`, `OFF_RFLAGS=208`, RIP=192,
RSP=216, CR3=248. The FPU image is 64 u64 = 512 bytes (`fxsave` area),
16-byte aligned. The `cr3` field carries the address space (0 = kernel
identity mapping); `rsp0` is what the TSS gets so ring-3 interrupts
land on this task's kernel stack.

## Context switch (`arch_context_switch.S`)

The switch path, `core_sched_switch_to` -> `arch_context_switch(old,new)`:

1. **Stack-bounds check first** (in C): if the saved rsp is outside the
   old task's `[stack_base, stack_base+stack_size)` window, the
   scheduler panics with
   `kernel stack bounds violated on switch (task=<name>, rsp=0x.. window=[..,..))`
   - a corrupted task is never resumed.
2. Save 15 GPRs + RFLAGS + CS/SS/RIP/RSP into `task_t` at the
   hardcoded offsets above.
3. `fxsave` the FPU image into `fpu_state` (CR4.OSFXSR/OSXMMEXCPT are
   enabled in `core_sched_init`; every switch saves/restores the full
   x87/SSE state - no lazy FPU switching).
4. Load `new`'s CR3 (page-table switch; kernel tasks share the
   identity map so this is often a no-op reload).
5. `fxrstor` the new task's image (first switch-in does `fninit` +
   `ldmxcsr 0x1F80` instead), restore RFLAGS via push/popfq **before**
   `ret` (the BUG-0103 fix - restoring IF at the wrong moment could
   nest or lose interrupts), and `ret` into the new task.

Fork's first entry is special: `arch_enter_ring3_fork.S`
(`enter_ring3_fork(frame, cr3)`) builds an iret frame and restores ALL
22 registers (including caller-saved - the WP04 G7 fix) with rax=0, so
the child returns from fork exactly where the parent called it.

## Lifecycle

- **Kernel threads**: `core_kthread_create` - 10-page (40 KiB)
  contiguous stacks (`OC_KTHREAD_STACK_PAGES 10`), **lowest page
  unmapped as a guard page** (split-huge first with fail-loud
  verification - a huge-PDE unmap is otherwise a silent no-op
  [memory.md](memory.md)). The idle task gets 1 page.
- **Ring-3 processes**: created by the ELF loader in
  `core_usermode.c`; the task is the kernel-side scheduling unit, the
  `user_proc_t` is the process identity (fds, cwd, signals, address
  space). At most 16 user processes.
- **Exit**: tasks are marked TASK_EXITED and reaped by
  `core_sched_reap_exited` from both the tick and yield paths, under
  cli (the BUG-0282 double-free fix). The **unified reaper**
  `user_process_kill_by_tid` is the only sanctioned kill path - the
  old `user_process_kill(pid)` was deleted with a tombstone comment
  (three invariant violations; see
  [hidden-mechanisms.md](hidden-mechanisms.md)).
- **Blocking**: semaphores/mutexes/condvars mark the task BLOCKED and
  yield; the timer wakes sleepers. Watchdog `kill` policy uses
  `core_sched_task_exited(tid)` directly.

## Observability

`sched` (L1SVC diag op 4) dumps the statistics counters; `ps` (the
`SYS_PS` syscall) walks the process table and returns
`struct ush_ps_entry` arrays (tid, state, priority, name,
cpu_time_ticks). The `nice` command and L1SVC_DIAG_NICE test priority
changes at runtime. The boot registry line `registry: drv=N svc=N app=N` comes from the neighboring collectors.

## How other subsystems lean on it

- **Timer**: `core_sched_tick` is registered on IRQ0 via
  `arch_irq_register_handler(0, ...)`; it decrements slices, bumps
  starvation counters, runs the reap.
- **Syscalls**: blocking syscalls (pipe reads, wait4, net recv) yield
  the task; the syscall ABI itself never blocks the scheduler
  ([syscall-ring3.md](syscall-ring3.md)).
- **Drivers**: all storage/NIC/USB data paths are **polled** with
  cooperative yields inside wait loops (e.g. `driver_snd_play` polls
  DMA position and yields); USB hot-plug lives on a resident kthread
  (`driver_usb_poll_start`). Real IRQs are few: PS/2 kbd (IRQ1), COM1
  (IRQ4), COM2 H4 (IRQ3), audio INTx (+SB16 IRQ5), HPET/RTC (IRQ8).
- **Crash paths**: the stack-overflow attribution names the task; the
  watchdog KILL policy kills a monitored task without stopping the
  kernel ([crash-recovery.md](crash-recovery.md)).
- **Health**: CPU busy% is computed from idle-counter deltas - the
  scheduler exports `core_sched_idle_counters()` for the health
  sampler ([../internals/health.md](../internals/health.md)).
