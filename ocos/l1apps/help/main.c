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
 * OC_USERPROGS[] registry - nothing here lists commands by hand.
 *
 * WP-runtime-hardening i18n: the built-in table renders through the
 * SAME keys as the ush builtin (help_title / help_row_* /
 * help_registry_* - one spelling, both surfaces); the command-word
 * column stays English (the zh_CN column-skeleton convention, see
 * apply_tool_i18n.py).  The unknown-option line stays a literal:
 * "unknown option" is an E2E anchor (tools/fix1_after_ush*.py, via
 * the mirrored ush builtin) and ush keeps its copy literal too. */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"

int app_main(int argc, char **argv) {
    const char *mode = "";
    if (argc > 1) {
        if (strcmp_(argv[1], "-a") == 0 || strcmp_(argv[1], "--all") == 0)
            mode = "-a";
        else if (strcmp_(argv[1], "-w") == 0 || strcmp_(argv[1], "--wp") == 0)
            mode = "-w";
        else {
            /* machine contract: "unknown option" (fix1_after_ush E2E) */
            puts_("help: unknown option '");
            puts_(argv[1]);
            puts_("' (use: help, help -a, help -w)\n");
            return 1;
        }
    }

    static const char *builtin_rows[] = {
        "cd [dir]        ",
        "help [-a|-w]    ",
        "export VAR=val  ",
        "alias name=val  ",
        "unalias name    ",
        "env             ",
        "set             ",
        "history         ",
        "jobs            ",
        "fg [job]        ",
        "bg              ",
        "exit            ",
        (const char *)0
    };
    static const char *builtin_keys[] = {
        "help_row_cd", "help_row_help", "help_row_export",
        "help_row_alias", "help_row_unalias", "help_row_env",
        "help_row_set", "help_row_history", "help_row_jobs",
        "help_row_fg", "help_row_bg", "help_row_exit",
        (const char *)0
    };
    puts_(tr_("help_title"));
    for (int i = 0; builtin_rows[i]; i++) {
        puts_(builtin_rows[i]);
        puts_(tr_(builtin_keys[i]));
        putc_('\n');
    }
    puts_(tr_("help_registry_note"));

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
            puts_(tr_("help_registry_prefix"));
            putu_(n);
            puts_(tr_("help_registry_suffix"));
        } else {
            puts_(tr_("help_registry_unavailable"));
        }
    } else {
        long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, (long)mode,
                            (long)out, (long)sizeof(out));
        if (rc == 0) puts_(out);
        else puts_(tr_("help_registry_listing_failed"));
    }
    putc_('\n');
    return 0;
}
