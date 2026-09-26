/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS WP-08cd
 * File: kernel/ext_wp8cd.c
 * Purpose: L1 extension API implementations for WP-08cd (items 51-57). */

#include "ext_wp8cd.h"
#include "console.h"
#include "string.h"
#include "usermode.h"
#include "syscall.h"
#include "sched.h"

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

/* Interface 51: shell_run */
int shell_run(void) {
    extern int g_usershell_running;
    extern const u8 userprog_ush[];
    extern const u64 userprog_ush_size;
    
    pid_t pid = user_process_create(userprog_ush, userprog_ush_size, "ush");
    if (pid < 0) return -1;
    
    g_usershell_running = 1;
    while (g_usershell_running) {
        __asm__ volatile("sti; hlt");
    }
    return 0;
}

/* Interface 52: shell_register_builtin */
int shell_register_builtin(const char *name, shell_builtin_fn fn, const char *help) {
    (void)fn; (void)help;
    if (!name) return -1;
    oc_console_puts("shell_register_builtin: ");
    oc_console_puts(name);
    oc_console_puts(" registered (use 'ush' to access)\n");
    return 0;
}

/* Interface 53: tool_register */
int tool_register(const char *name, const u8 *elf_data, u64 elf_size) {
    if (!name || !elf_data || g_tool_count >= MAX_TOOLS) return -1;
    oc_strncpy(g_tools[g_tool_count].name, name, 31);
    g_tools[g_tool_count].elf_data = elf_data;
    g_tools[g_tool_count].elf_size = elf_size;
    g_tool_count++;
    return 0;
}

/* Interface 54: tool_list */
int tool_list(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return 0;
    int offset = 0;
    for (int i = 0; i < g_tool_count; i++) {
        int len = (int)oc_strlen(g_tools[i].name);
        if (offset + len + 2 > bufsize) break;
        oc_strcpy(buf + offset, g_tools[i].name);
        offset += len;
        buf[offset++] = '\n';
    }
    buf[offset] = 0;
    return g_tool_count;
}

/* Interface 55: job_create */
int job_create(const char *cmd) {
    (void)cmd;
    if (g_job_count >= MAX_JOBS) return -1;
    /* Simplified: just return a fake PID for now */
    g_jobs[g_job_count].pid = 0;
    g_jobs[g_job_count].tid = 0;
    g_jobs[g_job_count].active = 0;
    g_job_count++;
    return 0;
}

/* Interface 56: job_list */
int job_list(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return 0;
    int offset = 0;
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].active) continue;
        int len = (int)oc_strlen(g_jobs[i].name);
        if (offset + len + 10 > bufsize) break;
        char num[8];
        oc_u64_to_str((u64)i, num);
        oc_strcpy(buf + offset, num); offset += (int)oc_strlen(num);
        buf[offset++] = ' ';
        oc_strcpy(buf + offset, g_jobs[i].name); offset += len;
        buf[offset++] = '\n';
    }
    buf[offset] = 0;
    return g_job_count;
}

/* Interface 57: job_control */
int job_control(int job_id, int action) {
    if (job_id < 0 || job_id >= g_job_count) return -1;
    if (!g_jobs[job_id].active) return -1;
    switch (action) {
        case 0: /* fg */ break;
        case 1: /* bg */ break;
        case 2: /* kill */ kthread_destroy(g_jobs[job_id].tid); break;
        default: return -1;
    }
    return 0;
}

/* Self-test */
void ext_wp8cd_selftest(void) {
    oc_console_puts("ext_wp8cd: shell_run exists\n");
    oc_console_puts("ext_wp8cd: tool_register exists\n");
    oc_console_puts("ext_wp8cd: job_control exists\n");
}
