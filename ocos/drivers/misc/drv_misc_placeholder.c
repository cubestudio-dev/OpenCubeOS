/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - drivers/misc/drv_misc_placeholder: the
 * OC_DRIVER_DECL self-registration demo / live test.
 *
 * WP-refactor-to-nak-architecture acceptance item: adding a driver
 * must require NOTHING but one .c file in ocos/drivers/ + one
 * OC_DRIVER_DECL line - no global table edit, no registration call
 * site, no core file change.  This file proves it: it declares the
 * "placeholder" family with a NULL-free ops table that no family
 * manager owns, so the declaration is collected at boot (the
 * drivers count in the boot registry line includes it) and nothing
 * else changes - no probe runs, no device appears.
 */
#include "drv_framework.h"

/* Minimal ops table: a marker constant.  Family managers match on
 * the family string ("block"/"nic"/"snd"/"usb"/"wireless"/"sdio");
 * "placeholder" owns no manager, so this descriptor is collected and
 * reported but never probed. */
static const int drv_placeholder_ops_marker = 0x504C4143; /* "PLAC" */

OC_DRIVER_DECL("placeholder", "placeholder",
               &drv_placeholder_ops_marker,
               "NAK registration-mechanism test driver (collected, never probed)");
