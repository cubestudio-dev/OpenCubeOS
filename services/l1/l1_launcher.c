/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/l1 launcher.
 *
 * Ring-3 application identity + boot entry.  Every embedded L1 app
 * declares itself in the file that owns it via OC_L1_APP_DECL
 * (.oc_apps section, collected by core_registry_collect()); this file
 * declares ush - the DEFAULT boot entry of 2.0.0.
 *
 * l1_launcher_boot_default() runs at the end of kmain:
 *   - exactly one app declared is_default != 0 is expected;
 *   - starting it succeeds -> kmain waits for it like `run ush` did
 *     (g_usershell_running spin; when the app exits the kernel shell
 *     resumes - the operator lands back at the oc> recovery prompt);
 *   - starting it fails -> the failure is logged and boot falls back
 *     to the oc> recovery environment directly.
 * This replaces the v1 `run ush` boot path (the run command itself
 * stays for the test programs).
 */
#include "l1_ext.h"
#include "core_registry.h"
#include "core_usermode.h"
#include "screen_console.h"
#include "lib_log.h"
#include "lib_string.h"

/* Embedded ELF bytes live in core/userprogs_data.h (compiled once,
 * inside core_usermode.o) - reference them, never re-include. */
extern const u8 userprog_ush[];
extern const u64 userprog_ush_size;

/* ---- The default L1 entry: the user-space shell (ush) ------------- */
OC_L1_APP_DECL("ush", userprog_ush, &userprog_ush_size, 1,
               "user-space shell (ring-3, default L1 entry)");

/* Boot into the default declared app.  Returns after the app exits
 * (kernel shell takes over) or after a failed start (caller falls
 * back to the recovery shell). */
void l1_launcher_boot_default(void) {
    const oc_l1_app_decl_t *dflt = 0;
    int n = core_registry_apps_count();
    for (int i = 0; i < n; i++) {
        const oc_l1_app_decl_t *a = core_registry_app_at(i);
        if (a->is_default) {
            if (dflt) {
                lib_log_info("l1 launcher: multiple default apps declared; using the first");
            } else {
                dflt = a;
            }
        }
    }
    if (!dflt) {
        lib_log_info("l1 launcher: no default L1 app declared; entering recovery shell");
        return;
    }
    {
        char line[96]; char nb[12];
        strcpy(line, "l1 launcher: booting '");
        strcat(line, dflt->name);
        strcat(line, "' (ring 3, ");
        u64_to_str(*dflt->size_ptr, nb);
        strcat(line, nb);
        strcat(line, " bytes)");
        lib_log_info(line);
    }
    pid_t pid = user_process_create(dflt->elf, *dflt->size_ptr, dflt->name);
    if (pid < 0) {
        lib_log_info("l1 launcher: failed to start the default L1 app; entering recovery shell");
        return;
    }
    /* Same wait discipline as the v1 `run ush` path: the kernel task
     * parks on sti/hlt while the ring-3 app owns the keyboard; when
     * the app exits, sys_exit2 clears the volatile flag and the boot
     * task continues into the recovery shell loop. */
    extern volatile int g_usershell_running;
    g_usershell_running = 1;
    while (g_usershell_running) {
        __asm__ volatile("sti; hlt");
    }
    lib_log_info("l1 launcher: default L1 app exited; recovery shell taking over");
}
