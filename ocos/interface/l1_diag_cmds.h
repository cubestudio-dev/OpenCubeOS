/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - L1 diag command shared layer (the
 * l1_diag_drv_core / l1_diag_dev_core of the WP naming, one header
 * because every ring-3 app links alone and shares code through
 * ocos/interface/ headers - the wizard_lib.h precedent).
 *
 * ONE implementation serves BOTH surfaces of each family:
 *   - the umbrella command  `drv <sub> [args...]`  (l1apps/drv)
 *   - the standalone twins  `drv_list`, `drv_info`, ... (l1apps/drv_*)
 * The twins call l1_diag_drv_sub(), which builds exactly the argv
 * the umbrella form would receive and enters the SAME dispatch -
 * byte-identical output by construction, never two logics.
 *
 * The kernel side owns every render (services/diag/src/
 * svc_diag_drvfw.c); this layer only maps subcommand -> L1SVC op,
 * forwards the argument string and prints the result. */
#ifndef OC_L1_DIAG_CMDS_H
#define OC_L1_DIAG_CMDS_H

#include "l1_lib.h"
#include "l1svc_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define L1_DIAG_OUT_CAP 32768

typedef struct {
    const char *sub;     /* subcommand word                       */
    long op;             /* L1SVC_DIAG_* op (0 = L1-side help)    */
    int  takes_arg;      /* 0 = none, 1 = required, 2 = optional  */
    const char *args;    /* argument synopsis for the usage text  */
    const char *help;    /* one-line description                  */
} l1_diag_subcmd_t;

static const l1_diag_subcmd_t L1_DRV_SUBS[] = {
    { "list",   L1SVC_DIAG_DRV_LIST,   0, "",           "list registered drivers (name, version, state, devices)" },
    { "info",   L1SVC_DIAG_DRV_INFO,   1, "<name>",     "driver details (deps, d-states, probe result)" },
    { "failed", L1SVC_DIAG_DRV_FAILED, 0, "",           "probe/init failures and their reasons" },
    { "bind",   L1SVC_DIAG_DRV_BIND,   1, "<drv> <dev>","debug: manually bind a driver to a node" },
    { "unbind", L1SVC_DIAG_DRV_UNBIND, 1, "<drv> <dev>","debug: manually unbind a driver" },
    { "reload", L1SVC_DIAG_DRV_RELOAD, 1, "<drv>",      "debug: re-init and re-probe a driver" },
    { "debug",  L1SVC_DIAG_DRV_DEBUG,  1, "<on|off>",   "debug: framework log switch" },
    { "order",  L1SVC_DIAG_DRV_ORDER,  0, "",           "init order (topological) + cycle report" },
    { "help",   0,                     0, "",           "this usage text" },
};
#define L1_DRV_SUBS_N ((int)(sizeof(L1_DRV_SUBS) / sizeof(L1_DRV_SUBS[0])))

static const l1_diag_subcmd_t L1_DEV_SUBS[] = {
    { "tree",   L1SVC_DIAG_DEV_TREE,   0, "",          "print the device tree" },
    { "list",   L1SVC_DIAG_DEV_LIST,   2, "[type]",    "list device nodes (optionally by type)" },
    { "info",   L1SVC_DIAG_DEV_INFO,   1, "<id|name>", "device node details" },
    { "debug",  L1SVC_DIAG_DEV_DEBUG,  1, "<on|off>",  "debug: framework log switch" },
    { "help",   0,                     0, "",          "this usage text" },
};
#define L1_DEV_SUBS_N ((int)(sizeof(L1_DEV_SUBS) / sizeof(L1_DEV_SUBS[0])))

/* ---- shared printer over the L1SVC result --------------------------- */
static char l1_diag_out[L1_DIAG_OUT_CAP];

static inline int l1_diag_run(const char *cmdname, long op, const char *arg)
{
    long rc = sys_l1svc(L1SVC_CAT_DIAG, op, arg && arg[0] ? (long)arg : 0,
                        (long)l1_diag_out, (long)sizeof(l1_diag_out));
    if (rc != 0 && !l1_diag_out[0]) {
        puts_(cmdname);
        puts_(": service call failed (rc ");
        /* small decimal render without libc */
        {
            char b[16];
            int i = 0;
            long v = rc;
            if (v < 0) { putc_('-'); v = -v; }
            if (v == 0) b[i++] = '0';
            while (v && i < 15) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
            while (i--) putc_(b[i]);
        }
        puts_(")\n");
        return 1;
    }
    puts_(l1_diag_out);
    if (l1_diag_out[0] && l1_diag_out[strlen_(l1_diag_out) - 1] != '\n')
        putc_('\n');
    return (rc != 0) ? 1 : 0;
}

/* ---- usage texts ------------------------------------------------------ */
static inline void l1_diag_usage(const char *cmd, const l1_diag_subcmd_t *subs, int n)
{
    puts_("usage: ");
    puts_(cmd);
    puts_(" <subcommand> [args]\nsubcommands:\n");
    for (int i = 0; i < n; i++) {
        puts_("  ");
        puts_(cmd);
        putc_(' ');
        puts_(subs[i].sub);
        if (subs[i].args[0]) {
            putc_(' ');
            puts_(subs[i].args);
        }
        putc_('\n');
    }
    puts_("run `");
    puts_(cmd);
    puts_(" help` for details\n");
}

static inline void l1_diag_help(const char *cmd, const l1_diag_subcmd_t *subs, int n)
{
    puts_(cmd);
    puts_(": driver framework diagnostics\n\nusage: ");
    puts_(cmd);
    puts_(" <subcommand> [args]\n\nsubcommands:\n");
    for (int i = 0; i < n; i++) {
        puts_("  ");
        puts_(cmd);
        putc_(' ');
        puts_(subs[i].sub);
        if (subs[i].args[0]) {
            putc_(' ');
            puts_(subs[i].args);
        }
        puts_(" - ");
        puts_(subs[i].help);
        putc_('\n');
    }
}

/* ---- the ONE dispatch per family --------------------------------------
 * argv[0] = the command word (drv/dev or the standalone twin name),
 * argv[1] = subcommand, argv[2..] = its arguments (joined by spaces
 * into the L1SVC argument string, exactly like the umbrella form). */
static inline int l1_diag_family_cmd(int argc, char **argv,
                              const char *cmd,
                              const l1_diag_subcmd_t *subs, int nsubs)
{
    if (argc < 2) {
        l1_diag_usage(cmd, subs, nsubs);
        return 1;
    }
    const char *sub = argv[1];
    const l1_diag_subcmd_t *hit = 0;
    for (int i = 0; i < nsubs; i++)
        if (streq_(subs[i].sub, sub)) { hit = &subs[i]; break; }
    if (!hit) {
        puts_(cmd);
        puts_(": unknown subcommand: ");
        puts_(sub);
        puts_("\navailable subcommands:");
        for (int i = 0; i < nsubs; i++) {
            putc_(' ');
            puts_(subs[i].sub);
        }
        putc_('\n');
        l1_diag_usage(cmd, subs, nsubs);
        return 1;
    }
    if (hit->op == 0) {                       /* L1-side help */
        l1_diag_help(cmd, subs, nsubs);
        return 0;
    }
    if (hit->takes_arg == 1 && argc < 3) {
        puts_(cmd);
        puts_(": missing argument for '");
        puts_(hit->sub);
        puts_("'\nusage: ");
        puts_(cmd);
        putc_(' ');
        puts_(hit->sub);
        putc_(' ');
        puts_(hit->args);
        putc_('\n');
        return 1;
    }
    /* join argv[2..] into the argument string */
    char arg[128];
    int len = 0;
    arg[0] = 0;
    for (int j = 2; j < argc && len < (int)sizeof(arg) - 1; j++) {
        if (j > 2) arg[len++] = ' ';
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    return l1_diag_run(cmd, hit->op, arg);
}

/* Standalone twin entry: prepend `sub` as the subcommand and run the
 * SAME dispatch - `drv_list foo` and `drv list foo` are one code
 * path, one render, byte-identical output. */
static inline int l1_diag_family_sub(int argc, char **argv, const char *sub,
                              const char *cmd,
                              const l1_diag_subcmd_t *subs, int nsubs)
{
    char *nargv[16];
    char subword[16];
    if (argc > 15) argc = 15;
    strcpy_(subword, sub);
    nargv[0] = (char *)cmd;
    nargv[1] = subword;
    for (int i = 1; i < argc; i++)
        nargv[i + 1] = argv[i];
    return l1_diag_family_cmd(argc + 1, nargv, cmd, subs, nsubs);
}

/* ---- public entries --------------------------------------------------- */
static inline int l1_diag_drv_cmd(int argc, char **argv)
{
    return l1_diag_family_cmd(argc, argv, "drv", L1_DRV_SUBS, L1_DRV_SUBS_N);
}

static inline int l1_diag_dev_cmd(int argc, char **argv)
{
    return l1_diag_family_cmd(argc, argv, "dev", L1_DEV_SUBS, L1_DEV_SUBS_N);
}

static inline int l1_diag_drv_sub(int argc, char **argv, const char *sub)
{
    return l1_diag_family_sub(argc, argv, sub, "drv", L1_DRV_SUBS, L1_DRV_SUBS_N);
}

static inline int l1_diag_dev_sub(int argc, char **argv, const char *sub)
{
    return l1_diag_family_sub(argc, argv, sub, "dev", L1_DEV_SUBS, L1_DEV_SUBS_N);
}

#ifdef __cplusplus
}
#endif

#endif /* OC_L1_DIAG_CMDS_H */
