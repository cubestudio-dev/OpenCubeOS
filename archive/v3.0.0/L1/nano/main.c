/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "nano".
 * Moved verbatim from the ush `nano` builtin: thin shell over the
 * shared editor engine (_shared/editor_core.h, the WP-10-wp08fix1
 * nano-style editor, ^O save / ^X exit). */
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
