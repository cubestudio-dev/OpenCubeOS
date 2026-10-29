/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "run".
 *
 * The v2.2.0 `run <program>` command: start a user program and report
 * the pid.  v2.2.0 dispatched a hardcoded 30-name table through
 * user_process_create; the WP-04 surface is the generated OC_USERPROGS
 * registry reached through fork + execve - the same embedded programs
 * plus every migrated command app, by name.  Usage prints the registry
 * listing (L1SVC cat "l1" op 1).
 *
 * Written defensively: the argv data is re-read from memory after fork
 * (no live value is kept in a caller-saved register across the
 * syscall), every branch is flat, and the child path ends in an
 * explicit exit. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

/* fork/execve/wait4 wrappers (not in l1_lib.h - most apps reach
 * programs only through ush; this app IS the run interface) */
static inline long sys_fork(void) { return syscall0(SYS_FORK); }
static inline long sys_execve(long path, long argv, long envp) {
    return syscall3(SYS_EXECVE, path, argv, envp);
}

/* volatile-pinned copy of the program word: after fork the child
 * re-reads THIS memory rather than trusting any register. */
static volatile long g_target_pid;

int app_main(int argc, char **argv) {
    static char prog[64];
    if (argc < 2 || !argv[1][0]) {
        char out[4096];
        long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, 0,
                            (long)out, (long)sizeof(out));
        puts_("usage: run <program>   (registered programs below)\n");
        if (rc == 0) puts_(out);
        return 0;
    }
    /* copy the program name into static memory (survives fork) */
    int n = 0;
    int background = 0;
    const char *src = argv[1];
    while (src[n] && n < (int)sizeof(prog) - 1) { prog[n] = src[n]; n++; }
    prog[n] = 0;
    if (n > 0 && prog[n - 1] == '&') {   /* v2.2.0 '&' suffix */
        background = 1;
        prog[n - 1] = 0;
    }

    long pid = sys_fork();
    if (pid < 0) { puts_("run: fork failed\n"); return 1; }

    if (pid == 0) {
        /* CHILD: build argv on its own stack, then execve - never
         * returns on success.  prog[] is static memory, valid in the
         * forked address space. */
        char *cargv[3];
        cargv[0] = prog;
        cargv[1] = prog;      /* program name as argv[0] and target  */
        cargv[2] = 0;
        long ret = sys_execve((long)prog, (long)cargv, 0);
        /* only reached when execve failed */
        (void)ret;
        puts_("run: unknown program: ");
        puts_(prog);
        putc_('\n');
        sys_exit2(1);
    }

    /* PARENT: report and wait (v2.2.0 printed "started pid=N" then
     * waited for the program; wait4 is the ring-3 spelling). */
    puts_("started pid=");
    putu_((unsigned long)pid);
    putc_('\n');
    if (background) {
        puts_("started in background\n");
        return 0;
    }
    g_target_pid = pid;
    syscall2(SYS_WAIT4, g_target_pid, 0);
    return 0;
}
