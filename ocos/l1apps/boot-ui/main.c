/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS WP-vmware-input-fix 4.2 - L1 app "boot-ui".
 *
 * Switches the boot UI the NEXT boot uses and persists the choice in
 * /etc/opencube.conf (boot.ui), so the setting belongs to the user
 * instead of being fixed by the factory image.
 *
 *   boot-ui              print the current mode
 *   boot-ui log          boot printing the classic scrolling log (default)
 *   boot-ui splash       boot showing the logo + phase progress bar
 *
 * One command = one app = one folder.  The write goes through the
 * existing `config` op (L1SVC_OTA_CONFIG), which validates the value and
 * rewrites the file atomically - this app is a friendly front door for
 * one key, not a second configuration mechanism. */
#include "l1_api_syscall.h"

static char g_out[2048];
/* NOTE: this app deliberately prints literal English instead of going
 * through tr_(): the per-app i18n table is bound by the app loader, and a
 * stand-alone wrapper that only forwards one config key does not need a
 * translation layer - the rule is "system strings are English" anyway. */

static int do_config(const char *argline) {
    long rc = sys_l1svc(L1SVC_CAT_OTA, L1SVC_OTA_CONFIG,
                        (long)argline, (long)g_out, (long)sizeof(g_out));
    if (rc != 0) { puts_("boot-ui: config service call failed\n"); return 1; }
    puts_(g_out);
    int n = strlen_(g_out);
    if (n > 0 && g_out[n - 1] != '\n') putc_('\n');
    return 0;
}

int app_main(int argc, char **argv) {
    if (argc < 2)
        return do_config("get boot.ui");
    const char *m = argv[1];
    if (m[0] == 'l' && m[1] == 'o' && m[2] == 'g' && m[3] == 0)
        return do_config("set boot.ui log");
    if (m[0] == 's' && m[1] == 'p' && m[2] == 'l' && m[3] == 'a' &&
        m[4] == 's' && m[5] == 'h' && m[6] == 0)
        return do_config("set boot.ui splash");
    puts_("usage: boot-ui [log|splash]\n");
    puts_("  log     boot with the classic scrolling log (default)\n");
    puts_("  splash  boot with the logo + progress bar\n");
    puts_("  (no argument prints the mode the next boot will use)\n");
    return 1;
}
