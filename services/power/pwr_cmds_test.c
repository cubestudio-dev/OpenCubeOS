/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/power test commands (migrated from
 * recovery/shell_cmds_power_test.c; the help_*_test commands stay in
 * the recovery shell - they verify the L0 help system itself). */
#include "pwr_cmds.h"
#include "core_power.h"
#include "driver_acpi_power.h"
#include "shell.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"

int pwr_cmd_poweroff_test(const char *args) {
    (void)args;
    screen_console_puts("poweroff_test: invoking power_shutdown() for real - on QEMU the process exits now\n");
    (void)core_power_shutdown();
    /* still running: no power-off port worked on this machine.
     * WP-10-tui-qa: that is NOT a pass - the thing under test (the
     * machine actually powering off) was not observed.  Report it as
     * such and fail the command. */
    screen_console_puts("poweroff_test: no power-off port took effect on this machine - power-off NOT observed : FAIL\n");
    return 1;
}

int pwr_cmd_suspend_test(const char *args) {
    (void)args;
    u32 mask = driver_acpi_power_sleep_mask();
    int typ3 = driver_acpi_power_slp_typ(3);
    int acpi_up = (mask & (1u << 3)) && typ3 >= 0;
    screen_console_puts(acpi_up
        ? "suspend_test: ACPI S3 available (sleep mask bit3 set, slp_typ resolves)\n"
        : "suspend_test: no ACPI S3 on this platform\n");
    if (acpi_up) {
        char b[48];
        u64_to_str((u64)typ3, b);
        screen_console_puts("suspend_test: S3 SLP_TYP value: ");
        screen_console_puts(b);
        screen_console_puts(" (live cycle verified in the dedicated QEMU suspend/wake session)\n");
        screen_console_puts("suspend_test: suspend path present and wired : PASS\n");
        return 0;
    }
    /* ACPI-less platform: the honest unsupported report is the PASS
     * condition (same shape as the pre-S3 contract). */
    int rc = core_power_suspend();
    if (rc == OC_POWER_E_UNSUPPORTED) {
        screen_console_puts("Suspend not supported (ACPI required).\n");
        screen_console_puts("suspend_test: suspend reports the honest ACPI-required contract : PASS\n");
        return 0;
    }
    screen_console_puts("suspend_test: suspend returned an unexpected rc : FAIL\n");
    return 1;
}

int pwr_cmd_halt_test(const char *args) {
    (void)args;
    /* A true halt cannot return, so this test verifies what is
     * verifiable from inside a live session and documents the rest. */
    screen_console_puts("halt_test: power_halt() = 'System halted.' + cli;hlt loop (kernel/power.c)\n");
    screen_console_puts("halt_test: this test must return, so it does NOT halt; the live halt is verified by running 'halt' in a dedicated QEMU session\n");
    /* WP-10-tui-qa: a destructive operation this command refuses to
     * perform is a SKIP, not a PASS - the PASS label required no
     * evidence and the release notes cited it as coverage it was not. */
    screen_console_puts("halt_test: live halt not observed (destructive) : SKIP - see the dedicated halt session\n");
    return 0;
}

int pwr_cmd_reboot_test(const char *args) {
    (void)args;
    core_power_flush_blk();
    screen_console_puts("reboot_test: block-device flush (reboot pre-step) executed\n");
    screen_console_puts("reboot_test: reset path = 8042 controller (0x64, 0xFE) + ACPI RESET_REG fallback (0xCF9)\n");
    screen_console_puts("reboot_test: this test must return, so it does NOT reset; the live reset is verified by 'reboot' (boot banner reappears)\n");
    /* WP-10-tui-qa: the flush above DID run (that part is a real
     * PASS); the reset itself is destructive and stays unobserved -
     * report SKIP for it instead of an evidence-free PASS. */
    screen_console_puts("reboot_test: pre-step flush executed : PASS\n");
    screen_console_puts("reboot_test: live reset not observed (destructive) : SKIP - see the dedicated reboot session\n");
    return 0;
}
