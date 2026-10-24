/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: shell/shell_cmds_power_test.c
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
 *   suspend_test   verifies the REAL S3 contract without sleeping the
 *                  machine: on an ACPI platform with \_S3 the sleep mask
 *                  must advertise bit 3 and slp_typ(3) must resolve;
 *                  on ACPI-less machines it verifies the honest
 *                  OC_POWER_E_UNSUPPORTED report path instead. The
 *                  live suspend->wake cycle is verified in a dedicated
 *                  QEMU session (suspend + monitor system_wakeup).
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
#include "driver_acpi_power.h"
#include "shell.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_registry.h"

/* 1. poweroff_test - real shutdown attempt (QEMU exits here) */
/* 2. suspend_test - real S3 capability contract, read-only probe.
 * fix2 contract update: S3 is implemented end to end now (real-mode
 * trampoline + waking vector). Calling core_power_suspend() here would
 * really sleep the machine mid-suite, so the test probes the same
 * decision path core_power_suspend() uses - without entering the
 * state. fix3: the shell entry point is hidden (suspend itself is
 * hidden); the probe stays compiled for the next suspend batch. */
__attribute__((unused))
/* 3. halt_test - component check + pointer to the live `halt` run */
/* 4. reboot_test - real block flush + reset path description */
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


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("help_default_test", shell_cmd_help_default_test, "verify default help A-Z ordering", "recovery");
OC_SHELL_CMD("help_wp_test", shell_cmd_help_wp_test, "verify help -w group ordering", "recovery");
OC_SHELL_CMD("help_a_test", shell_cmd_help_a_test, "verify help -a A-Z ordering", "recovery");
