/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "edit".
 * The v2.2.0 L0 `edit <file>` command as a ring-3 app: the same editor
 * engine as nano/vi (_shared/editor_core.h), third entry point.
 * WP-runtime-hardening i18n: the usage fragments render through
 * tr_() (the editor core keeps its own English UI strings). */
#include "editor_core.h"

int app_main(int argc, char **argv) {
    if (argc < 2) {
        puts_(tr_("usage: "));
        puts_(argv[0]);
        puts_(tr_(" <file>\n"));
        return 0;
    }
    ush_nano_run(argv[1]);
    return 0;
}
