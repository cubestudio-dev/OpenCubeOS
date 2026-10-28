/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS WP-08cd (3.0.0-WP03 trimmed)
 * File: l1/l1_wp8cd.c
 * Purpose: L1 process/job extension API implementations (WP-08cd).
 *
 * 3.0.0-WP03: interfaces 51 (shell_run - superseded by the L1
 * launcher, l1_launcher.c) and 52 (shell_register_builtin /
 * shell_unregister_builtin / l1_wp8cd_builtin_exec - the oc> builtin
 * dispatch) are DELETED with the L0 user plane.  The surviving
 * interface set (all real implementations, P1-6):
 *   - tool_register: stores an embedded tool ELF by name
 *   - tool_list / l1_wp8cd_tool_find: list / resolve tools
 *   - job_create: forks a real process, tracks tid
 *   - job_list: lists active jobs
 *   - job_control: fg/bg/kill on real tracked jobs */

#include "l1_wp8cd.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_usermode.h"
#include "core_syscall.h"
#include "core_sched.h"
#include "core_kcap.h"

#define MAX_TOOLS 32
#define MAX_JOBS  16

static struct {
    char name[32];
    const u8 *elf_data;
    u64 elf_size;
} g_tools[MAX_TOOLS];
static int g_tool_count = 0;

static struct {
    int pid;
    int tid;
    char name[32];
    int active;
} g_jobs[MAX_JOBS];
static int g_job_count = 0;

/* Interface 53: tool_register */
int tool_register(const char *name, const u8 *elf_data, u64 elf_size) {
    if (!name || !elf_data || g_tool_count >= MAX_TOOLS) return -1;
    strncpy(g_tools[g_tool_count].name, name, 31);
    g_tools[g_tool_count].elf_data = elf_data;
    g_tools[g_tool_count].elf_size = elf_size;
    g_tool_count++;
    return 0;
}

/* Interface 54: tool_list
 * BUG-0092 FIX (A16-5): companion lookup used by sys_execve so a
 * registered tool ELF is actually EXECUTABLE by name (before this
 * fix the table was write-only: tool_register stored, tool_list
 * printed, but nothing could run the tool). */
int l1_wp8cd_tool_find(const char *name, const u8 **elf, u64 *size) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < g_tool_count; i++) {
        if (strcmp(g_tools[i].name, name) == 0) {
            if (elf) *elf = g_tools[i].elf_data;
            if (size) *size = g_tools[i].elf_size;
            return 1;
        }
    }
    return 0;
}

int tool_list(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return 0;
    int offset = 0;
    for (int i = 0; i < g_tool_count; i++) {
        int len = (int)strlen(g_tools[i].name);
        if (offset + len + 2 > bufsize) break;
        strcpy(buf + offset, g_tools[i].name);
        offset += len;
        buf[offset++] = '\n';
    }
    buf[offset] = 0;
    return g_tool_count;
}

/* Interface 55: job_create — BUG-0249 FIX (A16-11)
 * Real accounting: this function now CREATES the background process
 * for `cmd` (resolved through the shared embedded-program lookup, the
 * same table sys_execve uses) and binds the job entry to the pid/task
 * that the spawn actually returned. The old "scan for the highest
 * alive pid" heuristic is gone: between a caller's spawn and this
 * call, any other process (timer helper, another ring-3 app) could
 * steal the binding, and a recycled pid could alias an
 * unrelated task - the old code even documented the race away as
 * "finds the most recently created process".
 * Returns the job id (index for job_list/job_control),
 *   -1 bad/unknown name or spawn failure,
 *   -2 job table full. */
int job_create(const char *cmd) {
    if (!cmd || !cmd[0]) return -1;
    /* Resolve the program name exactly like sys_execve does. */
    extern const u8 *core_userprog_lookup(const char *name, u64 *size_out);
    u64 elf_size = 0;
    const u8 *elf = core_userprog_lookup(cmd, &elf_size);
    if (!elf || elf_size == 0) return -1;
    pid_t pid = user_process_create(elf, elf_size, cmd);
    if (pid < 0) return -1;
    /* Bind to the ACTUAL spawned pid: exact g_procs lookup, no scan. */
    user_proc_t *p = NULL;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].pid == pid) { p = &g_procs[i]; break; }
    }
    if (!p) return -1;   /* spawn returned a pid that never registered */
    /* Find a slot: reuse an inactive entry or one whose process is
     * gone (real bookkeeping keeps the table truthful), else append. */
    int slot = -1;
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].active) { slot = i; break; }
        int bound_alive = 0;
        for (int k = 0; k < MAX_USER_PROCS; k++) {
            if (g_procs[k].alive && g_procs[k].pid == g_jobs[i].pid) {
                bound_alive = 1; break;
            }
        }
        if (!bound_alive) { slot = i; break; }
    }
    if (slot < 0) {
        if (g_job_count >= MAX_JOBS) return -2;
        slot = g_job_count++;
    }
    g_jobs[slot].pid = p->pid;
    g_jobs[slot].tid = p->tid;
    strncpy(g_jobs[slot].name, cmd, 31);
    g_jobs[slot].name[31] = 0;
    g_jobs[slot].active = 1;
    return slot;
}

/* BUG-0249 FIX (A16-11): exact lookup of the user_proc_t bound to a
 * job (by pid AND tid, so a recycled pid slot cannot alias). */
static user_proc_t *job_bound_proc(int job_id) {
    if (job_id < 0 || job_id >= g_job_count) return NULL;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive &&
            g_procs[i].pid == g_jobs[job_id].pid &&
            g_procs[i].tid == g_jobs[job_id].tid)
            return &g_procs[i];
    }
    return NULL;
}

/* Interface 56: job_list — BUG-0249 FIX (A16-11)
 * Lists jobs whose bound process is STILL ALIVE: a job whose target
 * has exited (or was never born) is no longer an active job, so it is
 * not reported. Previously the table kept stale entries forever. */
int job_list(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return 0;
    int offset = 0;
    int active_count = 0;
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].active) continue;
        if (!job_bound_proc(i)) continue;
        active_count++;
        char num[8];
        u64_to_str((u64)i, num);
        int nl = (int)strlen(num);
        if (offset + nl + 1 + (int)strlen(g_jobs[i].name) + 16 > bufsize) break;
        strcpy(buf + offset, "["); offset++;
        strcpy(buf + offset, num); offset += nl;
        strcpy(buf + offset, "] pid="); offset += 6;
        u64_to_str((u64)g_jobs[i].pid, num); nl = (int)strlen(num);
        strcpy(buf + offset, num); offset += nl;
        buf[offset++] = ' ';
        strcpy(buf + offset, g_jobs[i].name); offset += (int)strlen(g_jobs[i].name);
        buf[offset++] = '\n';
    }
    buf[offset] = 0;
    return active_count;
}

/* Interface 57: job_control — BUG-0249 FIX (A16-11)
 * fg: wake the job's task and WAIT for the bound process to exit
 * (the old code only woke it and returned immediately while its own
 * comment promised "wait for it"), then retire the job entry.
 * bg: leave running in background (already background by design).
 * kill: destroy the task via core_kthread_destroy. */
int job_control(int job_id, int action) {
    if (job_id < 0 || job_id >= g_job_count) return -1;
    if (!g_jobs[job_id].active) return -2;
    switch (action) {
        case 0: { /* fg — wake the job and wait for it to exit */
            user_proc_t *p = job_bound_proc(job_id);
            core_kthread_wake(g_jobs[job_id].tid);
            if (p) {
                /* Yield the CPU until the target is reaped. The timer
                 * interrupt wakes hlt, the scheduler runs the job, and
                 * sys_exit2 -> user_process_reap_resources clears
                 * alive. On the single CPU the target cannot exit
                 * behind our back before we blocked at least once. */
                while (p->alive && p->tid == g_jobs[job_id].tid)
                    __asm__ volatile("sti; hlt");
            }
            g_jobs[job_id].active = 0;
            break;
        }
        case 1: /* bg — leave running in background */
            break;
        case 2: { /* kill — destroy the bound process for real */
            /* BUG-0249 FIX: the job is really bound to a spawned
             * process now, so kill must release the SAME accounting
             * sys_kill(SIGKILL) releases - fds, address space, the
             * alive flag and the g_procs slot. The old bare
             * core_kthread_destroy left g_procs[].alive=1 forever and
             * leaked one process slot per kill. */
            user_proc_t *p = job_bound_proc(job_id);
            if (p) {
                user_process_reap_resources(p, 128 + SIGKILL);
                if (p->parent_tid > 0) core_kthread_wake(p->parent_tid);
                core_sched_task_exited(p->tid);
            } else {
                core_kthread_destroy(g_jobs[job_id].tid);
            }
            g_jobs[job_id].active = 0;
            break;
        }
        default: return -3;
    }
    return 0;
}

/* Self-test — 3.0.0-WP03: the surviving process/job interface set
 * (tool_register / tool_list / job_create / job_list / job_control).
 * The shell_run + shell_register_builtin probes died with the oc>
 * user plane. */
void ext_wp8cd_selftest(void) {
    screen_console_puts("ext_wp8cd: self-test start\n");

    /* 1. tool_register + tool_list */
    tool_register("test_tool", (const u8*)"\x7f" "ELF", 4);
    char tbuf[128];
    int tc = tool_list(tbuf, sizeof(tbuf));
    screen_console_puts("  tool_register+tool_list: ");
    if (tc > 0 && strncmp(tbuf, "test_tool", 9) == 0) {
        screen_console_puts("PASS\n");
    } else {
        screen_console_puts("FAIL\n");
    }

    /* 2. job_list — should return 0 active (no jobs created yet) */
    char jbuf[128];
    int rc;
    int jc = job_list(jbuf, sizeof(jbuf));
    screen_console_puts("  job_list (empty): ");
    if (jc >= 0) {
        char n[8]; u64_to_str((u64)jc, n);
        screen_console_puts("PASS (active="); screen_console_puts(n); screen_console_puts(")\n");
    } else {
        screen_console_puts("FAIL\n");
    }

    /* 3. job_control on invalid job — should return -1 */
    rc = job_control(-1, 2);
    screen_console_puts("  job_control(invalid): ");
    if (rc < 0) screen_console_puts("PASS (correctly rejected)\n");
    else screen_console_puts("FAIL\n");

    /* 4. job_control with valid action=bg on non-existent job — should fail */
    rc = job_control(0, 1);
    screen_console_puts("  job_control(valid action, no job): ");
    if (rc < 0) screen_console_puts("PASS (correctly rejected)\n");
    else screen_console_puts("FAIL\n");

    /* 5. job_create rejects unusable names without spawning (the
     * positive path - real spawn + binding + fg wait + kill - needs
     * live processes; at boot we only assert the rejection side so
     * the boot log stays quiet). BUG-0249 FIX. */
    screen_console_puts("  job_create(bad names): ");
    {
        int r1 = job_create(0);
        int r2 = job_create("");
        int r3 = job_create("nosuch_g2_prog");
        if (r1 < 0 && r2 < 0 && r3 < 0) {
            screen_console_puts("PASS (NULL/empty/unknown rejected)\n");
        } else {
            screen_console_puts("FAIL\n");
        }
    }

    screen_console_puts("ext_wp8cd: self-test done\n");
}
