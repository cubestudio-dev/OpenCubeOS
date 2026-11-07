/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "pwd".
 * Moved verbatim from the ush `pwd` builtin: prints the current
 * working directory (SYS_GETCWD) - a forked app inherits the shell
 * cwd, so the output matches the old builtin. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[256];
    sys_getcwd(buf, 256);
    puts_(buf);
    putc_('\n');
    return 0;
}
