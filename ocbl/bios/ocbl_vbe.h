/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_vbe.h
 * Purpose: VBE linear-framebuffer setup for the multiboot2 framebuffer
 *          tag.  The kernel's kmain() hard-fails without a usable fb
 *          tag, so OCBL must set 800x600x32 through the VBE BIOS
 *          interface (the same contract GRUB's gfxpayload provided).
 */
#ifndef OCBL_VBE_H
#define OCBL_VBE_H

#include "ocbl_types.h"
#include "ocbl_fb.h"             /* shared ocbl_fb_info_t (B5-5: the
                                   UEFI flavor fills the same shape from
                                   GOP instead of VBE) */

/* Set 800x600x32 through VBE.  Returns true and fills `fb` on
 * success.  BIOS flavor only (the UEFI flavor hands out the GOP
 * framebuffer in the EFI batch). */
bool ocbl_vbe_set_800x600x32(ocbl_fb_info_t* fb);

#endif /* OCBL_VBE_H */
