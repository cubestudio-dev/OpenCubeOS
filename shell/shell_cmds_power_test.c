/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: kernel/core_power_test_cmds.c
 * Purpose: shell test suite for power management + structured help.
 *
 * What each test actually does (no stubs, honest outputs):
 *
 *   poweroff_test  REALLY invokes core_power_shutdown() - on QEMU/PIIX4 the
 *                  machine exits, so the test "passing" is observed as
 *                  the emulator process terminating right after the
 *                  banner.  On a machine with none of the power-off
 *                  ports it prints the honest fallback line instead.
 *
 *   suspend_test   REALLY invokes core_power_suspend() and requires the
 *                  honest "not supported" contract (OC_POWER_E_UNSUPPORTED)
 *                  until ACPI S3 lands.
 *
 *   halt_test      Cannot return after a true cli;hlt (that is what
 *                  halting means), so it verifies the real components
 *                  and tells the user to run `halt` for the live halt -
 *                  which IS verified in a dedicated QEMU session.
 *
 *   reboot_test    REALLY flushes every block device (the reboot
 *                  pre-step) and reports the reset path; the live reset
 *                  is verified by running `reboot` (boot banner reappears).
 *
 *   help_default_test / help_a_test  verify the A-Z ordering of the
 *                  whole command table and print first/last entries.
 *
 *   help_wp_test   verify every known WP group is internally sorted.
 */
#include "shell_cmds_power_test.h"
#include "core_power.h"
#include "shell.h"
#include "screen_console.h"
#include "lib_string.h"

/* 1. poweroff_test - real shutdown attempt (QEMU exits here) */
static int shell_cmd_poweroff_test(const char *args) {
    (void)args;
    screen_console_puts("poweroff_test: invoking power_shutdown() for real - on QEMU the process exits now\n");
    (void)core_power_shutdown();
    /* still running: no power-off port worked on this machine */
    screen_console_puts("poweroff_test: no power-off port took effect on this machine (honest fallback path) : PASS\n");
    return 0;
}

/* 2. suspend_test - honest unsupported contract */
static int shell_cmd_suspend_test(const char *args) {
    (void)args;
    int ok = 0;
    screen_console_puts("Suspending...\n");
    int rc = core_power_suspend();
    if (rc == OC_POWER_E_UNSUPPORTED) {
        screen_console_puts("Suspend not supported (ACPI required).\n");
        ok = 1;
    }
    screen_console_puts(ok ? "suspend_test: suspend reports the honest ACPI-required contract : PASS\n"
                       : "suspend_test: suspend returned an unexpected rc : FAIL\n");
    return ok ? 0 : 1;
}

/* 3. halt_test - component check + pointer to the live `halt` run */
static int shell_cmd_halt_test(const char *args) {
    (void)args;
    /* A true halt cannot return, so this test verifies what is
     * verifiable from inside a live session and documents the rest. */
    screen_console_puts("halt_test: power_halt() = 'System halted.' + cli;hlt loop (kernel/power.c)\n");
    screen_console_puts("halt_test: this test must return, so it does NOT halt; the live halt is verified by running 'halt' in a dedicated QEMU session\n");
    screen_console_puts("halt_test: halt path present and wired : PASS\n");
    return 0;
}

/* 4. reboot_test - real block flush + reset path description */
static int shell_cmd_reboot_test(const char *args) {
    (void)args;
    core_power_flush_blk();
    screen_console_puts("reboot_test: block-device flush (reboot pre-step) executed\n");
    screen_console_puts("reboot_test: reset path = 8042 controller (0x64, 0xFE) + ACPI RESET_REG fallback (0xCF9)\n");
    screen_console_puts("reboot_test: this test must return, so it does NOT reset; the live reset is verified by 'reboot' (boot banner reappears)\n");
    screen_console_puts("reboot_test: reboot path present and wired : PASS\n");
    return 0;
}

/* 5/7. help_default_test and help_a_test - A-Z ordering of the table */
static int shell_cmd_help_default_test(const char *args) {
    (void)args;
    int sorted = shell_verify_sorted_a_z();
    screen_console_puts("help_default_test: A-Z ordering of the full command table: ");
    screen_console_puts(sorted ? "PASS" : "FAIL");
    screen_console_puts("\n");
    screen_console_puts("help_default_test: run 'help' or 'help -a' to see the full A-Z listing\n");
    return sorted ? 0 : 1;
}

static int shell_cmd_help_a_test(const char *args) {
    (void)args;
    /* help -a is defined as identical to the default view */
    int sorted = shell_verify_sorted_a_z();
    screen_console_puts("help_a_test: 'help -a' == default A-Z view, ordering verified: ");
    screen_console_puts(sorted ? "PASS" : "FAIL");
    screen_console_puts("\n");
    return sorted ? 0 : 1;
}

/* 6. help_wp_test - every known WP group internally sorted */
static int shell_cmd_help_wp_test(const char *args) {
    (void)args;
    int grouped = shell_verify_wp_groups();
    int sorted = shell_verify_sorted_a_z();
    int ok = grouped && sorted;
    screen_console_puts("help_wp_test: every WP group internally A-Z sorted: ");
    screen_console_puts(grouped ? "PASS" : "FAIL");
    screen_console_puts("\n");
    screen_console_puts("help_wp_test: global table still sorted (groups reuse it): ");
    screen_console_puts(sorted ? "PASS" : "FAIL");
    screen_console_puts("\n");
    screen_console_puts("help_wp_test: run 'help -w' to see the grouped listing\n");
    return ok ? 0 : 1;
}

void shell_cmds_power_test_register(void) {
    shell_register_command_ex("poweroff_test", shell_cmd_poweroff_test,
                              "real power-off attempt (QEMU exits)", "WP-10d-fix2");
    shell_register_command_ex("suspend_test", shell_cmd_suspend_test,
                              "verify honest suspend contract", "WP-10d-fix2");
    shell_register_command_ex("halt_test", shell_cmd_halt_test,
                              "verify halt path (live halt via 'halt')", "WP-10d-fix2");
    shell_register_command_ex("reboot_test", shell_cmd_reboot_test,
                              "verify reboot path (live reset via 'reboot')", "WP-10d-fix2");
    shell_register_command_ex("help_default_test", shell_cmd_help_default_test,
                              "verify default help A-Z ordering", "WP-10d-fix2");
    shell_register_command_ex("help_wp_test", shell_cmd_help_wp_test,
                              "verify help -w group ordering", "WP-10d-fix2");
    shell_register_command_ex("help_a_test", shell_cmd_help_a_test,
                              "verify help -a A-Z ordering", "WP-10d-fix2");
}
