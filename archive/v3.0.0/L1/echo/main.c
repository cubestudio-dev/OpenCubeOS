/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "echo".
 * Moved verbatim from the ush `echo` builtin: prints argv[1..] joined
 * by single spaces plus a trailing newline. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putc_(' ');
        puts_(argv[i]);
    }
    putc_('\n');
    return 0;
}
