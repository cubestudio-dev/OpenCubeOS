/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: shell/shell_cmds_timer.h
 * Purpose: shell commands for the time-source work package
 *          (CMOS RTC / HPET / ACPI PM timer / TSC).
 */
#ifndef OC_SHELL_CMDS_TIMER_H
#define OC_SHELL_CMDS_TIMER_H

#include "types.h"

/* rtc [uie [sec] | pie [rate] [count] | alarm [+sec]]
 *   read   - wall clock + status registers + interrupt counters
 *   uie    - update-ended interrupt test (default 3 seconds)
 *   pie    - periodic interrupt test (default rate 10 = 64 Hz, 32 fires)
 *   alarm  - alarm interrupt test (default 3 seconds ahead) */
int shell_cmd_rtc(const char *args);

/* hpet [counter [n] | oneshot <ms> | periodic <ms> <count> | test] */
int shell_cmd_hpet(const char *args);

/* pmtimer [read [n] | cal [sec] | wrap | test] */
int shell_cmd_pmtimer(const char *args);

/* tsc [cal [sec] | cal hpet | cal pmtimer | delay <ms> | test] */
int shell_cmd_tsc(const char *args);

/* Register all WP-10-timer_rtc commands (called from kmain). */

#endif /* OC_SHELL_CMDS_TIMER_H */
