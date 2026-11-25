/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-compat-and-security-hardening B5
 * File: kernel/core_boot_splash.h
 * Purpose: the boot splash API (see core_boot_splash.c).
 *
 *   boot_splash_begin(cmdline)  - start capturing (oc.splash=0 skips)
 *   boot_splash_set_stage(n)    - the OCST phase 1..6 drives the bar
 *   boot_splash_finish()        - 100%, clear, hand the screen to L1
 *   boot_splash_abort()         - the panic path: full-screen log back
 *   boot_splash_active()        - 1 while capturing
 */
#ifndef OC_BOOT_SPLASH_H
#define OC_BOOT_SPLASH_H

#include "types.h"

void boot_splash_begin(const char *cmdline);
void boot_splash_set_stage(int stage);
void boot_splash_finish(void);
void boot_splash_abort(void);
int  boot_splash_active(void);

#endif /* OC_BOOT_SPLASH_H */
