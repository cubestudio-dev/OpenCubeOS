/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/l1 ring-3 surface (rebuilt).
 *
 * The category string "l1" (reserved as category 13 since v2.0.0) gets
 * its first ops: this folder is the face; the mechanisms it consumes
 * live where G5 put them (services/loader for the dynamic-link self
 * test, services/ipc for the job + WP-08a wrapper surfaces) plus the
 * generated OC_USERPROGS registry (the execve name surface - the
 * v2.2.0 hardcoded program tables are gone). */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "core_usermode.h"
#include "core_sched.h"
#include "lib_string.h"
#include "l1_wp8a.h"
#include "l1_wp8b.h"
#include "l1_wp8cd.h"

#include <stdint.h>

static char *s_out;
static int   s_left;

static void put(const char *s) {
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
}
static void put_u64(u64 v) {
    char b[24];
    u64_to_str(v, b);
    put(b);
}

/* ---- op 1: the registry listing -----------------------------------------
 * One name per line, A-Z (gen_userprogs.py emits the registry sorted;
 * the help_*_test apps assert this invariant), with a count footer. */
static int l1_apps_core(void) {
    for (u64 i = 0; i < OC_USERPROGS_COUNT; i++) {
        put(OC_USERPROGS[i].name);
        put("\n");
    }
    put("-- ");
    put_u64(OC_USERPROGS_COUNT);
    put(" programs registered --\n");
    return 0;
}

/* ---- op 2: the v2.2.0 l1test body ---------------------------------------
 * Steps 1-6: the job interface over a real background spawn (loop) -
 * job_create binding, job_list, bg, fg-waits-for-exit, kill through the
 * unified reaper.  Step 7: the WP-08b dynamic-link self-test.  Step 8:
 * the WP-08a wrappers through the real int-0x80 gate from CPL0 (each
 * must fail cleanly with no current user process).  The v2.2.0 steps
 * 9/10 (g7test/g8test) are dropped: those suites were removed with the
 * L0 shell in WP-03 - declared in l1svc_defs.h. */
extern int g_next_pid;   /* core_syscall.c pid counter (v2.2.0 l1test parity) */

static int l1_l1test_core(void) {
    char buf[160]; char num[20];
    int fails = 0;

    put("L1 job interface test:\n");

    /* Steps 1+2: job_create spawns the background process itself and
     * binds the job to the pid the spawn actually returned. */
    put("  step 1: job_create(\"loop\") (real background spawn)\n");
    int job_id = job_create("loop");
    if (job_id < 0) {
        put("  job_create returned ");
        put_u64((u64)(i64)job_id);
        put(" - FAIL\n");
        return 1;
    }
    int loop_pid = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && strcmp(g_procs[i].name, "loop") == 0) {
            loop_pid = g_procs[i].pid; break;
        }
    }
    if (loop_pid <= 0) {
        put("  no alive loop process after job_create - FAIL (spawn)\n");
        return 1;
    }
    put("  job_create -> job_id=");
    put_u64((u64)job_id);
    put(" spawned loop pid=");
    put_u64((u64)loop_pid);
    put(" - PASS\n");

    /* Step 3: job_list shows the job as active. */
    put("  step 3: job_list()\n");
    char jbuf[256];
    int jc = job_list(jbuf, sizeof(jbuf));
    if (jc != 1) {
        put("  job_list returned ");
        put_u64((u64)(i64)jc);
        put(" active jobs - FAIL (want 1)\n");
        fails++;
    } else {
        put("  job_list: 1 active job:\n");
        put(jbuf);
    }

    /* Step 4: bg on the loop job. */
    put("  step 4: job_control(job, bg=1)\n");
    int rc = job_control(job_id, 1);
    if (rc == 0) put("  job_control(bg) = 0 - PASS\n");
    else {
        put("  job_control(bg) = ");
        put_u64((u64)(i64)rc);
        put(" - FAIL\n");
        fails++;
    }

    /* Step 5 (BUG-0249 FIX): fg must WAIT for the target to exit. */
    put("  step 5: job_create(\"hello\") + job_control(fg) waits for exit\n");
    int job2 = job_create("hello");
    if (job2 < 0) {
        put("  job_create(hello) = ");
        put_u64((u64)(i64)job2);
        put(" - FAIL\n");
        fails++;
    } else {
        int hp = -1;
        for (int i = 0; i < MAX_USER_PROCS; i++) {
            if (g_procs[i].alive && strcmp(g_procs[i].name, "hello") == 0) {
                hp = g_procs[i].pid; break;
            }
        }
        rc = job_control(job2, 0);
        int hello_alive = 0;
        for (int i = 0; i < MAX_USER_PROCS; i++) {
            if (g_procs[i].alive && g_procs[i].pid == hp) { hello_alive = 1; break; }
        }
        if (rc == 0 && !hello_alive) {
            put("  fg returned after target exit (pid=");
            put_u64((u64)(i64)hp);
            put(" alive=0, job retired) - PASS\n");
        } else {
            put("  fg rc=");
            put_u64((u64)(i64)rc);
            put(" target alive=");
            put_u64((u64)hello_alive);
            put(" - FAIL\n");
            fails++;
        }
        jc = job_list(jbuf, sizeof(jbuf));
        if (jc == 1) put("  job_list after fg: 1 active (loop) - PASS\n");
        else {
            put("  job_list after fg: ");
            put_u64((u64)(i64)jc);
            put(" active - FAIL\n");
            fails++;
        }
    }

    /* Step 6: kill the loop job through the unified reaper. */
    put("  step 6: job_control(job, kill=2)\n");
    rc = job_control(job_id, 2);
    int loop_gone = 1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && strcmp(g_procs[i].name, "loop") == 0) {
            loop_gone = 0; break;
        }
    }
    jc = job_list(jbuf, sizeof(jbuf));
    if (rc == 0 && jc == 0 && loop_gone)
        put("  kill (process reaped) + job_list=0 active - PASS\n");
    else {
        put("  kill rc=");
        put_u64((u64)(i64)rc);
        put(" job_list=");
        put_u64((u64)(i64)jc);
        put(" loop_gone=");
        put_u64((u64)loop_gone);
        put(" - FAIL\n");
        fails++;
    }

    /* Step 7 (BUG-0248 FIX): WP-08b extension self-test. */
    put("  step 7: ext_wp8b_selftest()\n");
    int st = ext_wp8b_selftest();
    if (st == 14) put("  ext_wp8b selftest 14/14 - PASS\n");
    else {
        put("  ext_wp8b selftest = ");
        put_u64((u64)(i64)st);
        put("/14 - FAIL\n");
        fails++;
    }

    /* Step 8 (v2.2.0 BUG-0254 FIX) - REMOVED in WP-04, declared: it
     * asserted the WP-08a wrappers fail cleanly "with no current user
     * process" - true when the L0 shell task called them, impossible
     * now: l1test arrives through SYS_L1SVC, so the current user
     * process is always the calling shell (getpid() is non-zero,
     * fork() would really fork).  The wrappers themselves are still
     * exercised by every user program every day (the whole app
     * surface rides on them); the stale-context smoke is dropped
     * together with the L0 shell that gave it meaning. */

    if (fails == 0 && st == 14) {
        put("  L1 job interface test: PASS\n");
        return 0;
    }
    put("  L1 job interface test: FAIL\n");
    return 1;
    (void)buf; (void)num;
}

/* ---- op 3: the v2.2.0 ldd body over the registry -------------------------
 * PT_INTERP + DT_NEEDED introspection of a registered program; static
 * (ET_EXEC) programs report "not a dynamic executable".  The v2.2.0
 * 20-name hardcoded chain is replaced by the registry lookup. */
static int l1_ldd_core(const char *args) {
    if (!args || !args[0]) {
        put("usage: ldd <program>\n");
        return 1;
    }
    u64 size = 0;
    const u8 *elf = core_userprog_lookup(args, &size);
    if (!elf) {
        put("unknown program: ");
        put(args);
        put("\n");
        return 1;
    }
    if (size < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        put("not an ELF file\n");
        return 1;
    }
    if (elf[4] != 2) {
        put("not ELF64\n");
        return 1;
    }
    u16 e_type = elf[16] | (elf[17] << 8);
    if (e_type == 2) {
        put("not a dynamic executable\n");
        return 0;
    }
    if (e_type != 3) {
        put("not a dynamic executable (unknown type)\n");
        return 1;
    }
    /* Program headers: find PT_INTERP and PT_DYNAMIC. */
    u64 phoff; u16 phentsize, phnum;
    {
        u64 o = 32;                       /* e_phoff */
        phoff = 0;
        for (int i = 0; i < 8; i++) phoff |= (u64)elf[o + i] << (8 * i);
        phentsize = elf[54] | (elf[55] << 8);
        phnum     = elf[56] | (elf[57] << 8);
    }
    u64 interp_off = 0, interp_sz = 0;
    u64 dyn_off = 0, dyn_sz = 0;
    for (int i = 0; i < phnum; i++) {
        const u8 *ph = elf + phoff + (u64)i * phentsize;
        u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | ((u32)ph[3] << 24);
        u64 p_offset = 0, p_filesz = 0;
        for (int k = 0; k < 8; k++) {
            p_offset |= (u64)ph[8 + k]  << (8 * k);
            p_filesz |= (u64)ph[32 + k] << (8 * k);
        }
        if (p_type == 3) { interp_off = p_offset; interp_sz = p_filesz; }
        if (p_type == 2) { dyn_off = p_offset;   dyn_sz = p_filesz; }
    }
    if (interp_off && interp_sz > 1) {
        put("interpreter: ");
        for (u64 i = 0; i + 1 < interp_sz; i++) {
            char c[2] = { (char)elf[interp_off + i], 0 };
            put(c);
        }
        put("\n");
    }
    if (!dyn_off) {
        put("no .dynamic section\n");
        return 0;
    }
    /* .dynamic entries: tag (u64) + val (u64).  DT_NEEDED(1)'s val is
     * an offset into the string table whose vaddr comes from
     * DT_STRTAB(5); both vaddrs translate to file offsets through the
     * PT_LOAD that covers them (a raw elf+val only works while
     * vaddr == offset, which is not guaranteed for PIE dyn apps). */
    u64 strtab_vaddr = 0;
    for (u64 o = 0; o + 16 <= dyn_sz; o += 16) {
        u64 tag = 0, val = 0;
        for (int k = 0; k < 8; k++) {
            tag |= (u64)elf[dyn_off + o + k]     << (8 * k);
            val |= (u64)elf[dyn_off + o + 8 + k] << (8 * k);
        }
        if (tag == 0) break;
        if (tag == 5) { strtab_vaddr = val; break; }
    }
    int needed = 0;
    for (u64 o = 0; o + 16 <= dyn_sz; o += 16) {
        u64 tag = 0, val = 0;
        for (int k = 0; k < 8; k++) {
            tag |= (u64)elf[dyn_off + o + k]     << (8 * k);
            val |= (u64)elf[dyn_off + o + 8 + k] << (8 * k);
        }
        if (tag == 0) break;
        if (tag == 1) {
            u64 str_vaddr = strtab_vaddr + val;   /* name inside DT_STRTAB */
            u64 str_off = 0;
            for (int i = 0; i < phnum; i++) {
                const u8 *ph = elf + phoff + (u64)i * phentsize;
                u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | ((u32)ph[3] << 24);
                if (p_type != 1) continue;
                u64 p_offset = 0, p_vaddr = 0, p_filesz = 0;
                for (int k = 0; k < 8; k++) {
                    p_offset |= (u64)ph[8 + k]  << (8 * k);
                    p_vaddr  |= (u64)ph[16 + k] << (8 * k);
                    p_filesz |= (u64)ph[32 + k] << (8 * k);
                }
                if (str_vaddr >= p_vaddr && str_vaddr < p_vaddr + p_filesz) {
                    str_off = p_offset + (str_vaddr - p_vaddr);
                    break;
                }
            }
            put("depends on: ");
            if (str_off && str_off < size) {
                for (const u8 *s = elf + str_off; s < elf + size && *s; s++) {
                    char c[2] = { (char)*s, 0 };
                    put(c);
                }
            }
            put("\n");
            needed++;
        }
    }
    if (!needed) put("no DT_NEEDED entries\n");
    return 0;
}

static int l1_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
    case L1SVC_L1_APPS:   return l1_apps_core();
    case L1SVC_L1_L1TEST: return l1_l1test_core();
    case L1SVC_L1_LDD:    return l1_ldd_core(a);
    default: return -1;
    }
}

/* Category string equals the services/l1 folder name (rebuilt in
 * WP-04 as the category's face; the loader/IPC mechanisms live in
 * their G5 homes and are only consumed here). */
OC_SERVICE_DECL_L1("l1", "l1 service (program registry + job/IPC face + dynamic-link self-test + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   l1_l1_dispatch);
