/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "help".
 *
 * The v2.2.0 ush `help` builtin printed the shell's static command
 * list.  In 3.0.0-WP04 the command surface IS the app registry, so
 * help prints the shell-state builtins (still inside ush - cd and
 * friends must run in the shell process) followed by the live registry
 * listing (L1SVC cat "l1" op 1: every migrated command app, A-Z,
 * generated - no hand-written command table anywhere). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts_("\nOpen Cube OS User-space Shell (ush) - shell built-in commands:\n\n");
    puts_("  cd [dir]        Change directory (shell process state)\n");
    puts_("  exit [code]     Exit shell\n");
    puts_("  export VAR=val  Set environment variable\n");
    puts_("  alias name=val  Set command alias\n");
    puts_("  unalias name    Remove alias\n");
    puts_("  env             Show environment variables\n");
    puts_("  set             Show shell variables\n");
    puts_("  history         Show command history\n");
    puts_("  jobs            List background jobs\n");
    puts_("  fg [job]        Bring job to foreground\n\n");
    puts_("All other commands are ring-3 apps (src/L1/<name>/), served\n");
    puts_("through the OC_USERPROGS registry:\n\n");
    char out[8192];
    long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, 0,
                        (long)out, (long)sizeof(out));
    if (rc == 0) puts_(out);
    else puts_("(registry unavailable)\n");
    return 0;
}
