/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "files" (the file
 * manager mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   files                -> the menu wizard below
 *   files direct <prog> .. -> explicit verbatim passthrough
 *   files <prog> [args]  -> classic dispatch: fork + execve of the
 *                           historical single-command app (byte-
 *                           identical output; the apps stay
 *                           registered, this is a second door into
 *                           the same binaries)
 *
 * Unlike the other merged apps the file tools are real PROGRAMS (the
 * cp/mv/rm/edit/stat/chmod apps do their own syscalls; the editor is
 * a full-screen interactive program) - so the dispatch here is
 * fork + execve + wait4, the exact `run` mechanism, not an L1SVC
 * wrap.  The wizard layers intent on top: browse, tree, usage, copy,
 * move, delete (with a confirmation), edit, permissions. */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
#include "l1_wizard_api.h"

/* fork/execve wrappers (not in l1_api_syscall.h - most apps reach programs
 * only through ush; this app IS a program launcher, the run/main.c
 * pattern) */
static inline long sys_fork_(void) { return syscall0(SYS_FORK); }
static inline long sys_execve_(long path, long argv, long envp) {
    return syscall3(SYS_EXECVE, path, argv, envp);
}

/* the verbatim program table (historical command spellings) */
static const char *const g_progs[] = {
    "ls", "tree", "df", "du", "cp", "mv", "rm", "edit", "stat", "chmod",
};
#define G_PROGS_N ((int)(sizeof(g_progs) / sizeof(g_progs[0])))

/* execve payload: static storage so nothing lives in a caller-saved
 * register across the fork (the run/main.c defensive pattern). */
static char g_p0[64];
static char g_pa[6][160];
static char *g_pargv[10];

/* g_pargv[0] = g_p0; g_pargv[1..n] = filled g_pa slots; then NULL. */
static void prog_args_reset(const char *prog) {
    strcpy_(g_p0, prog);
    for (int i = 0; i < 10; i++) g_pargv[i] = (char *)0;
    g_pargv[0] = g_p0;
    for (int i = 0; i < 6; i++) g_pa[i][0] = 0;
}

static void prog_args_add(const char *a) {
    for (int i = 0; i < 6; i++) {
        if (g_pa[i][0] == 0 && !g_pargv[i + 1]) {
            int n = 0;
            while (a[n] && n < (int)sizeof(g_pa[i]) - 1) {
                g_pa[i][n] = a[n];
                n++;
            }
            g_pa[i][n] = 0;
            g_pargv[i + 1] = g_pa[i];
            return;
        }
    }
}

static int prog_run(void) {
    long pid = sys_fork_();
    if (pid < 0) {
        wiz_fail(tr_("wiz_svc_failed"));
        return 1;
    }
    if (pid == 0) {
        /* CHILD: execve never returns on success */
        long ret = sys_execve_((long)g_p0, (long)g_pargv, 0);
        (void)ret;
        puts_("files: unknown program: ");
        puts_(g_p0);
        putc_('\n');
        sys_exit2(1);
    }
    /* PARENT: wait for the program to finish (ring-3 wait4) */
    syscall2(SYS_WAIT4, pid, 0);
    return 0;
}

static void usage(void) {
    puts_("usage: files                (interactive menu)\n");
    puts_("       files direct <prog> [args]    (explicit passthrough)\n");
    puts_("       files <prog> [args]           (classic program dispatch)\n");
    puts_("programs:");
    for (int i = 0; i < G_PROGS_N; i++) {
        puts_("  ");
        puts_(g_progs[i]);
        putc_('\n');
    }
}

/* octal-digits check for the chmod mode */
static int all_octal(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '7') return 0;
    return 1;
}

/* ---- the main menu --------------------------------------------------- */
static int files_wizard(void) {
    static const char *const items[] = {
        "fi_m_ls", "fi_m_tree", "fi_m_usage", "fi_m_cp", "fi_m_mv",
        "fi_m_rm", "fi_m_edit", "fi_m_perm"};
    static char a[160], b[160], m[16];
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 8);
        if (c == 0) return 0;
        if (c == 1) {
            wiz_ask(tr_("fi_ask_path_opt"), a, (int)sizeof(a));
            prog_args_reset("ls");
            if (a[0]) prog_args_add(a);
            (void)prog_run();
        } else if (c == 2) {
            wiz_ask(tr_("fi_ask_path_opt"), a, (int)sizeof(a));
            prog_args_reset("tree");
            if (a[0]) prog_args_add(a);
            (void)prog_run();
        } else if (c == 3) {
            prog_args_reset("df");
            (void)prog_run();
            wiz_ask(tr_("fi_ask_path_opt"), b, (int)sizeof(b));
            prog_args_reset("du");
            if (b[0]) prog_args_add(b);
            (void)prog_run();
        } else if (c == 4) {
            wiz_ask(tr_("fi_ask_src"), a, (int)sizeof(a));
            if (!a[0]) continue;
            wiz_ask(tr_("fi_ask_dst"), b, (int)sizeof(b));
            if (!b[0]) continue;
            prog_args_reset("cp");
            prog_args_add(a);
            prog_args_add(b);
            (void)prog_run();
        } else if (c == 5) {
            wiz_ask(tr_("fi_ask_src"), a, (int)sizeof(a));
            if (!a[0]) continue;
            wiz_ask(tr_("fi_ask_dst"), b, (int)sizeof(b));
            if (!b[0]) continue;
            prog_args_reset("mv");
            prog_args_add(a);
            prog_args_add(b);
            (void)prog_run();
        } else if (c == 6) {
            wiz_ask(tr_("fi_ask_path"), a, (int)sizeof(a));
            if (!a[0]) continue;
            if (!wiz_confirm(tr_("fi_rm_confirm"))) continue;
            prog_args_reset("rm");
            prog_args_add(a);
            (void)prog_run();
        } else if (c == 7) {
            wiz_ask(tr_("fi_ask_path"), a, (int)sizeof(a));
            if (!a[0]) continue;
            prog_args_reset("edit");
            prog_args_add(a);
            (void)prog_run();
        } else {
            wiz_ask(tr_("fi_ask_path"), a, (int)sizeof(a));
            if (!a[0]) continue;
            prog_args_reset("stat");
            prog_args_add(a);
            (void)prog_run();
            if (!wiz_confirm(tr_("fi_perm_ask"))) continue;
            wiz_ask(tr_("fi_ask_mode"), m, (int)sizeof(m));
            if (!all_octal(m)) {
                wiz_fail(tr_("fi_bad_mode"));
                continue;
            }
            prog_args_reset("chmod");
            prog_args_add(m);
            prog_args_add(a);
            (void)prog_run();
        }
        wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return files_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    const char *prog = argv[start];
    int known = 0;
    for (int i = 0; i < G_PROGS_N; i++)
        if (streq_(g_progs[i], prog)) { known = 1; break; }
    if (!known) {
        puts_("files: unknown program: ");
        puts_(prog);
        putc_('\n');
        usage();
        return 1;
    }
    prog_args_reset(prog);
    for (int i = start + 1; i < argc; i++)
        prog_args_add(argv[i]);
    return prog_run();
}
