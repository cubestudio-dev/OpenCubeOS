/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "vi".
 * Moved verbatim from the ush `vi` builtin: the same editor engine as
 * `nano` (_shared/editor_core.h) under the vi-compatible command name. */
#include "_shared/editor_core.h"

int app_main(int argc, char **argv) {
    if (argc < 2) {
        puts_("usage: ");
        puts_(argv[0]);
        puts_(" <file>\n");
        return 0;
    }
    ush_nano_run(argv[1]);
    return 0;
}
