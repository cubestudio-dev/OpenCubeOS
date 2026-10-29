/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP04 G11c
 * File: services/tui/tui_exec.c
 * Purpose: implementation of the desktop command bridge (see
 * tui_exec.h).  The exec/spawn machinery mirrors the G11a sshd path
 * (net_sshd_exec_spawn_wait), with the retire wait parked on sti;hlt
 * because the bridge runs inside the ring-3 SVC dispatch frame.
 */
#include "tui_exec.h"
#include "core_usermode.h"
#include "core_timer.h"
#include "core_kcap.h"
#include "lib_string.h"

/* The generated registry (userprogs_data.h is included exactly once by
 * core_usermode.c; the externs here are the public surface). */
extern const struct oc_userprog_decl OC_USERPROGS[];
extern const u64 OC_USERPROGS_COUNT;

/* ---- enumeration ------------------------------------------------------ */

int tui_exec_enum(tui_cmd_info_t *out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    for (u64 i = 0; i < OC_USERPROGS_COUNT && n < cap; i++) {
        const char *nm = OC_USERPROGS[i].name;
        if (!nm) continue;
        tui_cmd_info_t *row = &out[n];
        memset(row, 0, sizeof(*row));
        strncpy(row->name, nm, sizeof(row->name) - 1);
        /* The registry carries names only; the bridge labels every row
         * with the (WP-04) provenance the v2.2.0 table had per command. */
        strncpy(row->help, "ring-3 L1 app (src/L1 registry)",
                sizeof(row->help) - 1);
        strncpy(row->wp, "3.0.0-WP04", sizeof(row->wp) - 1);
        n++;
    }
    return n;
}

/* ---- spawn + retire ---------------------------------------------------- */

static int tui_exec_first_word(const char *cmd, char *name, int cap) {
    if (!cmd || !name || cap <= 0) return -1;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    int n = 0;
    while (cmd[n] && cmd[n] != ' ' && cmd[n] != '\t' && n < cap - 1) {
        name[n] = cmd[n];
        n++;
    }
    name[n] = 0;
    return n > 0 ? 0 : -1;
}

static pid_t tui_exec_spawn(const char *cmd) {
    char name[64];
    if (tui_exec_first_word(cmd, name, sizeof(name)) != 0) return -1;
    u64 elf_size = 0;
    extern const u8 *core_userprog_lookup(const char *name, u64 *size_out);
    const u8 *elf = core_userprog_lookup(name, &elf_size);
    if (!elf || elf_size == 0) return -1;
    return user_process_create(elf, elf_size, name);
}

/* Bounded retire wait, parked on sti;hlt (SVC-frame rule - see
 * tui_exec.h).  60 s of kernel time is far beyond any desktop command
 * (v2.2.0 used the same bounded-wait idea in the sshd bridge); the cap
 * keeps a wedged app from parking the desktop forever. */
static int tui_exec_wait_retire(pid_t pid) {
    u64 deadline = core_timer_now_ms() + 60000;
    for (;;) {
        int alive = 0;
        for (int i = 0; i < MAX_USER_PROCS; i++) {
            if (g_procs[i].alive && g_procs[i].pid == pid) { alive = 1; break; }
        }
        if (!alive) return 0;
        if ((i64)(core_timer_now_ms() - deadline) >= 0) return -2;
        __asm__ volatile("sti; hlt");
    }
}

static int tui_exec_spawn_wait(const char *cmd) {
    pid_t pid = tui_exec_spawn(cmd);
    if (pid < 0) return -1;
    return tui_exec_wait_retire(pid);
}

int tui_exec_captured(const char *cmd, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    out[0] = 0;
    /* The kcap run chains the previous console hook and restores it
     * when done, so the serial mirror survives exactly like the v2.x
     * shell bridge demanded (same contract as sshd's exec capture). */
    int rc = core_kcap_run_fn(tui_exec_spawn_wait, cmd, out, cap - 1);
    if (rc != 0) out[0] = 0;
    out[cap - 1] = 0;
    return rc;
}

int tui_exec_interactive(const char *cmd) {
    pid_t pid = tui_exec_spawn(cmd);
    if (pid < 0) return -1;
    return tui_exec_wait_retire(pid);
}
