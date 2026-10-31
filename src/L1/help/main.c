/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04-fix1 - L1 app "help".
 *
 * The standalone help command mirrors the ush `help` builtin exactly
 * (three modes, registry-driven - bugs 5+6):
 *   help        -> shell built-in summary + the registry count
 *   help -a     -> built-ins + every registry entry, A-Z, tagged
 *   help -w     -> built-ins + registry grouped by work package
 * All registry text is rendered by the kernel "l1" service from the
 * OC_USERPROGS[] registry - nothing here lists commands by hand. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv) {
    const char *mode = "";
    if (argc > 1) {
        if (strcmp_(argv[1], "-a") == 0 || strcmp_(argv[1], "--all") == 0)
            mode = "-a";
        else if (strcmp_(argv[1], "-w") == 0 || strcmp_(argv[1], "--wp") == 0)
            mode = "-w";
        else {
            puts_("help: unknown option '");
            puts_(argv[1]);
            puts_("' (use: help, help -a, help -w)\n");
            return 1;
        }
    }

    static const char *builtin_rows[] = {
        "cd [dir]        change directory (shell state)",
        "help [-a|-w]    this text; -a = full A-Z, -w = by work package",
        "export VAR=val  set environment variable",
        "alias name=val  set command alias",
        "unalias name    remove alias",
        "env             show environment variables",
        "set             show a variable",
        "history         show command history",
        "jobs            list background jobs",
        "fg [job] / bg   foreground / background a job",
        "exit            not available (use shutdown / reboot)",
        (const char *)0
    };
    puts_("\nOpen Cube OS User-space Shell (ush) - built-in commands:\n\n");
    for (int i = 0; builtin_rows[i]; i++) {
        puts_(builtin_rows[i]);
        putc_('\n');
    }
    puts_("\nAll other commands are ring-3 apps served from the registry.\n");

    static char out[8192];
    if (mode[0] == 0) {
        long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, 0,
                            (long)out, (long)sizeof(out));
        if (rc == 0) {
            int n = 0;
            for (const char *q = out; *q; ) {
                const char *e = q;
                while (*e && *e != '\n') e++;
                if (!(q[0] == '-' && q[1] == '-')) n++;
                if (!*e) break;
                q = e + 1;
            }
            puts_("Registry: ");
            putu_(n);
            puts_(" programs - 'help -a' lists them all, 'help -w' groups by work package.\n");
        } else {
            puts_("(registry unavailable)\n");
        }
    } else {
        long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, (long)mode,
                            (long)out, (long)sizeof(out));
        if (rc == 0) puts_(out);
        else puts_("(registry listing failed)\n");
    }
    putc_('\n');
    return 0;
}
