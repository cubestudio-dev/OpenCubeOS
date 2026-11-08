/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL UEFI GOP framebuffer driver (WP-02-ocbl).
 *
 * The Graphics Output Protocol gives OCBL the linear framebuffer
 * address and mode geometry. This driver locates GOP, blanks the
 * framebuffer and draws a marker bar so the UEFI-side screen
 * takeover is visibly proven; it is compiled by the UEFI toolchain
 * (mingw PE32+) only and never included by the BIOS path.
 *
 * Text logging in the UEFI phase goes through the firmware's own
 * Simple Text Output protocol (core_boot_uefi.c) and the shared COM1
 * serial driver - the serial port is the core debug channel of both
 * paths.
 *
 * Naming: [class]_[category]_[role] - drv (driver), video (device
 * class), uefi_gop (the UEFI Graphics Output Protocol).
 */

#ifndef DRV_VIDEO_UEFI_GOP_H
#define DRV_VIDEO_UEFI_GOP_H

#include <stdint.h>
#include "boot/core_boot_uefi.h"

/* Locate the Graphics Output Protocol, blank the framebuffer and
 * draw the OCBL marker bar in the top rows.
 *
 * bs: the boot services table from the UEFI system table.
 * Returns 0 on success, non-zero if GOP is unavailable. */
int drv_video_gop_init(core_boot_efi_boot_services_t *bs);

/* Number of scan lines the marker bar occupies after init. */
#define DRV_VIDEO_GOP_BAR_ROWS 24U

#endif /* DRV_VIDEO_UEFI_GOP_H */
