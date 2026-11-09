/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_gop_efi.c
 * Purpose: the UEFI flavor of the framebuffer backend: locate GOP,
 *          pick 800x600x32 (the exact mode the BIOS flavor sets
 *          through VBE - one kernel expectation, two firmware paths),
 *          and fill the shared ocbl_fb_info_t for the mb2 tag 8.
 *
 *          Pixel formats (GOP spec 12.9):
 *            0 PixelRedGreenBlueReserved8BitPerColor: R@16 G@8 B@0
 *            1 PixelBlueGreenRedReserved8BitPerColor: B@16 G@8 R@0
 *            2 bitmask (masks from ModeInfo->PixelInformation)
 *            3 blt-only: NO linear framebuffer - refused honestly.
 */
#include "efi.h"
#include "../common/ocbl_fb.h"
#include "../common/ocbl_serial.h"

/* mask -> (pos, size): lowest set bit is the position, popcount the
 * width - the classic fbcon formula, used for the bitmask format. */
static void mask_bits(u32 mask, u8* pos, u8* size)
{
    if (!mask) {
        *pos = 0;
        *size = 0;
        return;
    }
    u32 p = 0;
    while (!(mask & 1u)) {
        mask >>= 1;
        p++;
    }
    u32 s = 0;
    while (mask & 1u) {
        mask >>= 1;
        s++;
    }
    *pos = (u8)p;
    *size = (u8)s;
}

/* Fill `fb` from a GOP mode info.  Returns false for blt-only.
 *
 * B5-5 live-caught (+4): every field this reads sits 4 bytes into
 * the mode-info buffer OVMF hands out - reading the struct straight
 * at the pointer shows HorizontalResolution=0 with the real numbers
 * shifted one field up (mode 1 shows as v=640 fmt=480 = the REAL
 * 640x480 one slot late).  The OVMF buffer begins with a 4-byte
 * private head; the spec fields live at +4 onward.  Harmless for
 * reads, honored everywhere here. */
static u32 gmi_h(const efi_gop_mode_info* mi)
{
    return ((const u32*)mi)[1];         /* HorizontalResolution @ +4 */
}

static u32 gmi_v(const efi_gop_mode_info* mi)
{
    return ((const u32*)mi)[2];         /* VerticalResolution   @ +8 */
}

static u32 gmi_fmt(const efi_gop_mode_info* mi)
{
    return ((const u32*)mi)[3];         /* PixelFormat          @+12 */
}

static u32 gmi_ppsl(const efi_gop_mode_info* mi)
{
    return ((const u32*)mi)[8];         /* PixelsPerScanLine    @+32
                                         * (live-caught: [9] reads 4
                                         * bytes PAST the 32-byte mode
                                         * info - garbage 0xBEEB____)
                                         */
}

static u32 gmi_mask(const efi_gop_mode_info* mi, int k)
{
    return ((const u32*)mi)[4 + k];     /* PixelInformation     @+16 */
}

static bool gop_fill(const efi_gop_mode_info* mi, u64 fb_base, u64 fb_size,
                     ocbl_fb_info_t* fb)
{
    u32 fmt = gmi_fmt(mi);
    if (fmt == 3)
        return false;                       /* blt-only: no lfb */
    fb->addr = fb_base;
    fb->pitch = gmi_ppsl(mi) * 4;
    fb->width = gmi_h(mi);
    fb->height = gmi_v(mi);
    fb->bpp = 32;
    if (fmt == 0) {
        fb->red_pos = 16;   fb->red_size = 8;
        fb->green_pos = 8;  fb->green_size = 8;
        fb->blue_pos = 0;   fb->blue_size = 8;
    } else if (fmt == 1) {
        fb->red_pos = 0;    fb->red_size = 8;
        fb->green_pos = 8;  fb->green_size = 8;
        fb->blue_pos = 16;  fb->blue_size = 8;
    } else {
        mask_bits(gmi_mask(mi, 0), &fb->red_pos, &fb->red_size);
        mask_bits(gmi_mask(mi, 1), &fb->green_pos, &fb->green_size);
        mask_bits(gmi_mask(mi, 2), &fb->blue_pos, &fb->blue_size);
    }
    (void)fb_size;
    return true;
}

/* Set 800x600x32 through GOP and fill `fb`.  Returns true on success.
 * When the firmware does not offer the mode, the CURRENT GOP mode is
 * used as long as it exposes a linear 32-bpp-class framebuffer (the
 * kernel takes any bpp<=32 RGB frame through the tag). */
bool ocbl_gop_set_fb(ocbl_fb_info_t* fb)
{
    efi_graphics_output* gop = 0;
    if (ocbl_efi_bs->locate_protocol(&ocbl_efi_gop_guid, 0,
                                     (void**)&gop) != EFI_SUCCESS ||
        !gop || !gop->mode) {
        ocbl_serial_puts("[boot] GOP: not available\r\n");
        return false;
    }
    u32 chosen = gop->mode->mode;            /* default: keep current */
    bool chosen_800 = false;
    for (u32 i = 0; i < gop->mode->max_mode; i++) {
        u64 isz = 0;
        efi_gop_mode_info* mi = 0;
        if (gop->query_mode(gop, i, &isz, &mi) != EFI_SUCCESS || !mi)
            continue;
        if (gmi_h(mi) == 800 && gmi_v(mi) == 600 &&
            gmi_fmt(mi) <= 1) {
            chosen = i;
            chosen_800 = true;
            break;
        }
    }
    if (chosen != gop->mode->mode &&
        gop->set_mode(gop, chosen) != EFI_SUCCESS) {
        ocbl_serial_puts("[boot] GOP: SetMode failed, keeping current\r\n");
        chosen = gop->mode->mode;
        chosen_800 = false;
    }
    if (!gop_fill(gop->mode->info, gop->mode->frame_buffer_base,
                  gop->mode->frame_buffer_size, fb)) {
        ocbl_serial_puts("[boot] GOP: no linear framebuffer\r\n");
        return false;
    }
    ocbl_serial_puts("[boot] GOP ");
    ocbl_serial_dec(fb->width);
    ocbl_serial_puts("x");
    ocbl_serial_dec(fb->height);
    ocbl_serial_puts(fb->bpp == 32 ? "x32" : "x");
    if (fb->bpp != 32)
        ocbl_serial_dec(fb->bpp);
    ocbl_serial_puts(chosen_800 ? " ok (800x600 picked)\r\n"
                                : " ok (current mode)\r\n");
    return true;
}
