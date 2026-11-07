<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-04 Extension API

WP-04 adds **three** new L0->L1 extension points (on top of WP-01's 4, WP-02's 6, and WP-03's 4). All previous interfaces remain unchanged. The total L1 surface is now 17 stable entry points.

WP-04 also re-uses the WP-03 `shell_register_command()` API to register six new built-in Shell commands (`ps`, `kill`, `nice`, `sched`, `syncstat`, `run`). That API is documented in `EXTENSIONS_WP03.md` section 14 and is not repeated here.

---

## 15. Task Management (Preemptive Scheduler)

```c
#include "core_sched.h"

tid_t core_kthread_create(void (*fn)(void *arg), void *arg, const char *name, int prio);
int   core_kthread_destroy(tid_t tid);
int   core_kthread_block(void);
int   core_kthread_wake(tid_t tid);
void  core_kthread_list(void);
```

The scheduler is a priority-based, round-robin, preemptive scheduler driven by the PIT at 100 Hz (one tick = 10 ms). Each task gets a time slice of 2 ticks (20 ms). When the slice expires, the timer IRQ calls `core_sched_tick()` which selects the next READY task of the highest priority and performs a context switch via `arch_context_switch()` (in `context_switch.S`).

`tid_t` is a small non-negative integer. `0` is reserved for the kernel / idle task. Valid tids returned by `core_kthread_create()` are in the range `1..MAX_TASKS-1` (currently `MAX_TASKS = 32`).

### Parameters

| Function | Description |
|---|---|
| `core_kthread_create(fn, arg, name, prio)` | Spawn a kernel thread. `fn` is the entry function (returns `void`), `arg` is passed unchanged, `name` is a short label (max 31 chars, copied), `prio` is `0..31` where `0` is highest. Use `TASK_PRIO_DEFAULT` (15) if unsure. Returns `tid >= 1` on success, `-1` on failure (table full / bad prio). |
| `core_kthread_destroy(tid)` | Mark a task as exited and free its TCB + stack. The current task may destroy itself (the scheduler will switch away on the next tick). Returns `0` on success, `-1` on bad tid. |
| `core_kthread_block(void)` | Block the **current** task (state -> `TASK_BLOCKED`). The scheduler immediately switches to another task. Returns when the task is later woken. Returns `0` on success, `-1` if called from the idle task. |
| `core_kthread_wake(tid)` | Wake a blocked task (state -> `TASK_READY`). If the woken task has a higher priority than the current task, a preemption is triggered on the next tick. Returns `0` on success, `-1` on bad tid / not blocked. |
| `core_kthread_list(void)` | Prints a table of all tasks (tid, name, state, priority, cpu_time_ticks, switch_count) to the console. Also exposed to the user as the `ps` Shell command. |

### Task states

```
TASK_READY    0   /* runnable, waiting for its slice */
TASK_RUNNING  1   /* currently on the CPU (only one at a time) */
TASK_BLOCKED  2   /* sleeping, waiting for core_kthread_wake() */
TASK_EXITED   3   /* finished, slot is being reclaimed */
```

### Usage

```c
static void worker(void *arg) {
    int n = (int)(u64)arg;
    for (int i = 0; i < n; i++) {
        /* do useful work */
        core_sched_yield();          /* give up the CPU voluntarily */
    }
    /* fall off the end -> task auto-exits */
}

void spawn_workers(void) {
    tid_t t1 = core_kthread_create(worker, (void*)10, "worker-A", 15);
    tid_t t2 = core_kthread_create(worker, (void*)20, "worker-B", 10);  /* higher prio */
    if (t1 < 0 || t2 < 0) {
        screen_console_puts("spawn failed\n");
        return;
    }
    /* ... later, inspect or terminate ... */
    core_kthread_list();
    core_kthread_destroy(t1);
}
```

### Blocking on a condition (typical pattern)

```c
static tid_t g_waiter;

static void waiter_task(void *arg) {
    screen_console_puts("waiter: blocking\n");
    core_kthread_block();
    screen_console_puts("waiter: woken up\n");
}

void wake_the_waiter(void) {
    g_waiter = core_kthread_create(waiter_task, NULL, "waiter", 15);
    /* ... some time later ... */
    core_kthread_wake(g_waiter);
}
```

### Auxiliary helpers

```c
task_t *core_kthread_current(void);      /* NULL if scheduler not yet initialized */
tid_t   core_kthread_current_tid(void);  /* 0 for the idle/kernel task */
void    core_sched_yield(void);          /* voluntary preemption */
void    core_sched_get_stats(core_sched_stats_t *out);
```

`core_sched_stats_t` contains: `total_switches`, `total_preemptions`, `current_tid`, `active_tasks`.

### Caveats

- **No FPU/SSE save**: the context switch saves/restores GP registers, RIP, RFLAGS, RSP, CR3. It does NOT save XMM/YMM. Kernel threads that use floating-point must save/restore it themselves. This will be fixed in WP-05.
- **Stack size**: each new kernel thread gets a fixed `TASK_STACK_SIZE` (8 KiB) stack allocated from the kernel heap. Stack overflow is not detected.
- **No per-task address spaces by default**: `task_t.cr3` is initialized to `0` (= kernel identity mapping). To give a task its own address space, set `tcb->cr3 = mem_vmm_create_address_space()` before the first context switch.

---

## 16. Synchronization Primitives

```c
#include "core_sync.h"

/* Spinlock */
void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int  spin_trylock(spinlock_t *lock);

/* Counting semaphore */
void sem_init(sem_t *sem, int initial);
void sem_wait(sem_t *sem);
void sem_post(sem_t *sem);

/* Mutex (with NO priority inheritance (single-CPU, CLI-protected)) */
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* Condition variable */
void cond_init(cond_t *c);
void cond_wait(cond_t *c, mutex_t *m);
void cond_signal(cond_t *c);
void cond_broadcast(cond_t *c);
```

All four primitives are safe to use from kernel threads. They are NOT safe to use from raw IRQ context (use `spin_lock` only inside IRQ handlers - the other three can sleep).

### Spinlock

Busy-wait lock using `xchg` for test-and-set. Use only for very short critical sections (a few instructions) or inside IRQ context where sleeping is not allowed.

```c
static spinlock_t g_list_lock;

void init_list(void) {
    spin_init(&g_list_lock);
}

void append(node_t *n) {
    spin_lock(&g_list_lock);
    /* ... critical section: insert into list ... */
    spin_unlock(&g_list_lock);
}

int try_append(node_t *n) {
    if (spin_trylock(&g_list_lock) == 0) {
        /* ... insert ... */
        spin_unlock(&g_list_lock);
        return 1;  /* succeeded */
    }
    return 0;  /* locked by someone else, try again later */
}
```

`spin_trylock()` returns `0` on success (lock acquired), non-zero if the lock was already held.

### Semaphore

Counting semaphore. `sem_wait()` decrements the count; if the count goes negative, the calling task blocks. `sem_post()` increments the count and wakes one waiter (FIFO order). Safe for producer/consumer queues.

```c
static sem_t g_items;     /* how many items are in the queue */
static sem_t g_slots;     /* how many free slots in the queue */

void queue_init(int capacity) {
    sem_init(&g_items, 0);          /* nothing to consume yet */
    sem_init(&g_slots, capacity);   /* all slots free */
}

void producer(void *arg) {
    for (;;) {
        sem_wait(&g_slots);         /* wait for a free slot */
        /* ... enqueue one item ... */
        sem_post(&g_items);         /* signal: one more item available */
    }
}

void consumer(void *arg) {
    for (;;) {
        sem_wait(&g_items);         /* wait for an item */
        /* ... dequeue one item ... */
        sem_post(&g_slots);         /* signal: one more slot free */
    }
}
```

### Mutex

Mutual exclusion lock with **NO priority inheritance (single-CPU, CLI-protected)**: if a low-priority task holds the mutex and a higher-priority task tries to acquire it, the holder's effective NO priority bumping (documented limitation) to the waiter's priority to avoid unbounded priority inversion. `mutex_lock()` may block. `mutex_unlock()` releases the lock and restores the holder's original priority.

```c
static mutex_t g_buf_lock;

void init_buf(void) { mutex_init(&g_buf_lock); }

void safe_append(const char *s) {
    mutex_lock(&g_buf_lock);
    /* ... append to shared buffer ... */
    mutex_unlock(&g_buf_lock);
}
```

**Do not** call `mutex_unlock()` from a task that did not acquire the mutex. The kernel detects this case and panics (the alternative - silently allowing it - would corrupt the priority-inheritance bookkeeping).

### Condition variable

Condition variables let a task wait for a state change guarded by a mutex. `cond_wait()` atomically releases `m` and blocks; when woken, it re-acquires `m` before returning. Always re-check the predicate after `cond_wait()` returns (spurious wakeups are possible).

```c
static mutex_t g_m;
static cond_t  g_c;
static int     g_ready;

void waiter_init(void) {
    mutex_init(&g_m);
    cond_init(&g_c);
    g_ready = 0;
}

void wait_until_ready(void) {
    mutex_lock(&g_m);
    while (!g_ready) {                 /* MUST re-check in a loop */
        cond_wait(&g_c, &g_m);         /* atomically: unlock g_m, sleep, re-lock g_m */
    }
    /* g_ready is true and we hold g_m */
    mutex_unlock(&g_m);
}

void signal_ready(void) {
    mutex_lock(&g_m);
    g_ready = 1;
    cond_broadcast(&g_c);              /* wake ALL waiters */
    mutex_unlock(&g_m);
}
```

Use `cond_signal()` to wake exactly one waiter (cheaper). Use `cond_broadcast()` when you cannot prove that one waiter is enough (e.g. the predicate changed in a way that benefits multiple waiters).

### Sync statistics

```c
void sync_get_stats(sync_stats_t *out);
```

`sync_stats_t` contains cumulative counters: `spin_locks`, `spin_unlocks`, `sem_waits`, `sem_posts`, `mutex_locks`, `mutex_unlocks`, `cond_waits`, `cond_signals`. Exposed to the user as the `syncstat` Shell command.

### Caveats

- Spinlocks disable nothing by default - they rely on the scheduler not preempting the holder mid-section. If you need to use a spinlock from an IRQ handler, you must explicitly `cli` before `spin_lock` and `sti` after `spin_unlock`.
- `sem_wait`, `mutex_lock`, and `cond_wait` may sleep. Calling them from IRQ context will hang the system.
- The mutex priority-inheritance implementation is single-hop: it bumps the direct holder. Transitive PI through a chain of held mutexes is not yet supported.

---

## 17. User Mode (ring-3 processes, syscalls, ELF loader)

```c
#include "user.h"

int  user_process_create(const char *path, const char *args);
int  user_process_kill(int pid);
void user_process_list(void);
int  core_syscall_register(int num, void (*handler)(u64 a0, u64 a1, u64 a2, u64 a3, u64 a5));
```

WP-04 introduces ring-3 user processes. Each user process runs in its own address space (CR3) created via `mem_vmm_create_address_space()` (see `EXTENSIONS_WP03.md` section 12). The kernel sets up a user-mode stack, an IRET frame with `RPL=3` / `CS=0x1B` / `SS=0x23`, and jumps to the ELF entry point.

### Parameters

| Function | Description |
|---|---|
| `user_process_create(elf_data, size, name)` | Load an ELF executable from `path`, create a new user address space, spawn a task with priority 15, and switch to user mode via IRET. `path` is currently resolved against a small in-memory file table (a real filesystem comes in WP-05). `args` is a single string passed in `rdi` to the entry point. Returns `pid >= 1` on success, `-1` on failure (file not found / bad ELF / OOM). |
| `user_process_kill(pid)` | Terminate a user process. Frees its address space, closes any kernel handles, and marks the task as exited. Returns `0` on success, `-1` on bad pid. |
| `user_process_list(void)` | Prints a table of all user processes (pid, path, state, cpu_time, address space root) to the console. Exposed to the user as part of the `ps` Shell command. |
| `core_syscall_register(num, handler)` | Register a kernel-side handler for syscall number `num` (0..255). The handler is called with the user's `rdi/rsi/rdx/rcx/r8/r9` (six-argument ABI). Return value in `rax`. Returns `0` on success, `-1` on bad num, `-2` if slot already taken. |

### Syscall ABI

User-mode code triggers a syscall via the int 0x80 interrupt gate. The kernel reads the user's `rax` as the syscall number and dispatches to the registered handler (or returns `-ENOSYS` in `rax` if none registered).

The wrapper macros for user-mode programs are:

```c
/* user-side helper, in a separate user library (provided in WP-05 SDK) */
static inline long oc_syscall6(int num, long a0, long a1, long a2, long a3, long a5) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"((long)num), "D"(a0), "S"(a1), "d"(a2), "c"(a3), "r"((long)a5)
        : "rcx", "r11", "memory"
    );
    return ret;
}
```

### Usage - kernel-side syscall registration

```c
/* Syscall #42: write a string to the console.
 * User calls: oc_syscall6(42, (long)msg, len, 0, 0, 0); */
static void sys_write(u64 a0, u64 a1, u64 a2, u64 a3, u64 a5) {
    const char *buf = (const char*)a0;
    u64         len = a1;
    /* Copy from user space and print safely. The kernel validates
     * that [buf, buf+len) is mapped in the current address space
     * via mem_vmm_is_mapped() before dereferencing. */
    for (u64 i = 0; i < len; i++) {
        screen_console_putc(buf[i]);
    }
}

void register_syscalls(void) {
    core_syscall_register(42, sys_write);
    core_syscall_register(43, sys_exit);   /* user-mode exit */
    /* ... */
}
```

### Usage - spawning a user process

```c
void spawn_init(void) {
    int pid = user_process_create("/bin/init", "--hello");
    if (pid < 0) {
        screen_console_puts("init: spawn failed\n");
        return;
    }
    /* The new process is now runnable. The scheduler will switch to
     * it on the next tick (priority 15, same as default kernel threads). */
}
```

### ELF loader notes

- Supports **static ELF64** executables (ET_EXEC) for x86-64.
- Does NOT support dynamic linking / shared libraries yet (`PT_INTERP` is rejected).
- Does NOT support PIE / `ET_DYN` yet.
- Loads `PT_LOAD` segments at their virtual addresses, applies the segment protection bits via `mem_vmm_protect_page()` (R / R+X / R+W).
- Entry point is `e_entry` from the ELF header.
- The first 4 MiB of the user address space are reserved (NULL-pointer guard).
- Kernel region (above `0xffff800000000000`) is shared across all address spaces - user code cannot write to it because the pages lack the USER flag.

### Exposed Shell commands (registered via the WP-03 API)

| Command | Description |
|---|---|
| `ps` | List all tasks (kernel + user) with state, priority, CPU time. |
| `kill <tid>` | Terminate a user process. |
| `nice <tid> <prio>` | Change a task's priority (0..31). |
| `sched` | Show scheduler statistics (switches, preemptions, active tasks). |
| `syncstat` | Show sync primitive statistics (locks/waits/posts/signals). |
| `run <path> [args]` | Spawn a user process from an ELF file. |

These are registered via the existing `shell_register_command(name, handler, help)` API (see `EXTENSIONS_WP03.md` section 14). The API itself is unchanged.

### Caveats

- **No filesystem yet**: `user_process_create(path, ...)` currently resolves `path` against a small in-memory file table populated at boot. A real filesystem is scheduled for WP-05.
- **No fork()/exec()**: only `spawn-from-ELF` is supported in WP-04. The traditional UNIX `fork()` + `exec()` pair will arrive in WP-06.
- **No signals**: user processes cannot yet receive signals (SIGINT, SIGSEGV, ...). A `#PF` in ring-3 currently kills the process with a diagnostic message; there is no `SIGSEGV` handler.
- **Single-CPU only**: the scheduler runs on BSP. SMP support is far future.
- **Syscall table is global**: all user processes share the same syscall handlers. Per-process syscall filtering is not yet supported.

---

## ABI Stability

All WP-04 functions, structs, and their typedefs are frozen:

- `core_kthread_create`, `core_kthread_destroy`, `core_kthread_block`, `core_kthread_wake`, `core_kthread_list` (and the auxiliary `core_kthread_current`, `core_kthread_current_tid`, `core_sched_yield`, `core_sched_get_stats`)
- `spin_init/lock/unlock/trylock`, `sem_init/wait/post`, `mutex_init/lock/unlock`, `cond_init/wait/signal/broadcast`, `sync_get_stats`
- `user_process_create`, `user_process_kill`, `user_process_list`, `core_syscall_register`

The `task_t`, `spinlock_t`, `sem_t`, `mutex_t`, `cond_t`, `core_sched_stats_t`, and `sync_stats_t` struct layouts are frozen with respect to the fields documented above. New fields may be appended at the end in future WPs (callers must zero-initialize the struct before passing it in to allow this).

The `tid_t` and `pid_t` types remain `int`. The syscall ABI (six-arg int 0x80 interrupt gate, return in `rax`) is frozen.

Future WPs may add new functions but will not change existing ones incompatibly.

## Loading model

WP-04 still does not have a dynamic module loader - L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged.
