/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL UEFI GOP framebuffer driver (WP-02-ocbl).
 *
 * Implementation for the interface declared in drv_video_uefi_gop.h.
 * Locates GOP through the boot services the caller passes in, blanks
 * the framebuffer (whatever the firmware left on screen disappears)
 * and paints the OCBL marker bar, proving pixel-level takeover before
 * the kernel is even loaded.
 *
 * The framebuffer is left in the firmware-selected resolution; the
 * pixel format is honoured (direct writes only when the mode exposes
 * a linear framebuffer - BltOnly modes are skipped with the reason
 * logged by the caller).
 */

#include <stdint.h>
#include <stddef.h>

#include "boot/core_boot_uefi.h"
#include "drivers/drv_video_uefi_gop.h"

/* Deep green bar on black - visible against the OVMF logo screen. */
#define DRV_VIDEO_GOP_BAR_COLOR 0xFF004000U
#define DRV_VIDEO_GOP_BG_COLOR  0xFF000000U

static void drv_video_gop_fill(void *fb, core_boot_efi_uintn_t bytes,
                               uint32_t color)
{
    volatile uint32_t *p = (volatile uint32_t *)fb;
    core_boot_efi_uintn_t n = bytes / 4U;

    while (n != 0U) {
        *p = color;
        ++p;
        --n;
    }
}

int drv_video_gop_init(core_boot_efi_boot_services_t *bs)
{
    core_boot_efi_gop_protocol_t *gop = NULL;
    core_boot_efi_status_t status;
    core_boot_efi_gop_mode_t *mode;
    core_boot_efi_gop_mode_info_t *info;

    if (bs == NULL || bs->locate_protocol == NULL) {
        return 1;
    }

    {
        const core_boot_efi_guid_t gop_guid = CORE_BOOT_EFI_GOP_GUID;
        status = bs->locate_protocol(&gop_guid, NULL, (void **)&gop);
    }
    if (CORE_BOOT_EFI_ERROR(status) || gop == NULL || gop->mode == NULL) {
        return 1;
    }

    mode = gop->mode;
    info = mode->info;
    if (info == NULL || mode->frame_buffer_base == 0U ||
        mode->frame_buffer_size == 0U) {
        return 1;
    }

    /* BltOnly means there is no linear framebuffer to write to. */
    if (info->pixel_format == 3U) {
        return 1;
    }

    drv_video_gop_fill((void *)(uintptr_t)mode->frame_buffer_base,
                       mode->frame_buffer_size, DRV_VIDEO_GOP_BG_COLOR);

    /* marker bar: top DRV_VIDEO_GOP_BAR_ROWS scan lines, so the
     * takeover is visible from the first moment */
    drv_video_gop_fill((void *)(uintptr_t)mode->frame_buffer_base,
                       (core_boot_efi_uintn_t)DRV_VIDEO_GOP_BAR_ROWS *
                           (core_boot_efi_uintn_t)info->pixels_per_scan_line * 4U,
                       DRV_VIDEO_GOP_BAR_COLOR);

    return 0;
}
