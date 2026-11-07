/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_vbe.c
 * Purpose: VBE mode enumeration + LFB set (see ocbl_vbe.h).
 *
 * Scratch layout inside the trampoline page (0x40000-0x4FFFF):
 *   0x40700  512 B VBE controller info
 *   0x40900  256 B per-mode info
 * (0x40600 and below belong to the disk/geometry scratch.)
 *
 * Field offsets are the VBE ModeInfoBlock layout (live-verified
 * against SeaBIOS: pitch@0x10, X@0x12, Y@0x14, planes@0x18, bpp@0x19,
 * memory model@0x1B, mask fields @0x1F.., phys base @0x2C).
 */
#include "ocbl_vbe.h"
#include "ocbl_bios.h"

#ifdef OCBL_FLAVOR_BIOS

#define VBE_CINFO  0x40700u
#define VBE_MINFO  0x40900u

static u16 rd16m(u32 addr)
{
    volatile u8* p = (volatile u8*)(u64)addr;
    return (u16)(p[0] | (p[1] << 8));
}

static u32 rd32m(u32 addr)
{
    volatile u8* p = (volatile u8*)(u64)addr;
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

bool ocbl_vbe_set_800x600x32(ocbl_fb_info_t* fb)
{
    /* controller info */
    ocbl_bios_regs_t r = {0};
    r.eax = 0x4F00;
    r.es = (u16)(VBE_CINFO >> 4);
    r.edi = VBE_CINFO & 0xF;
    if (ocbl_bios_int(0x10, &r) != 0)
        return false;
    if ((r.eax & 0xFFFF) != 0x004F)
        return false;
    volatile u8* ci = (volatile u8*)(u64)VBE_CINFO;
    if (ci[0] != 'V' || ci[1] != 'E' || ci[2] != 'S' || ci[3] != 'A')
        return false;
    /* VideoModePtr: offset @0x0E, segment @0x10 */
    u32 list_phys = ((u32)rd16m(VBE_CINFO + 0x10) << 4) |
                    rd16m(VBE_CINFO + 0x0E);
    if (!list_phys)
        return false;

    u32 seen = 0;
    for (u32 i = 0; i < 128; i++) {
        u16 mode = rd16m(list_phys + i * 2);
        if (mode == 0xFFFF)
            break;
        r.eax = 0x4F01;
        r.ecx = mode;
        r.es = (u16)(VBE_MINFO >> 4);
        r.edi = VBE_MINFO & 0xF;
        if (ocbl_bios_int(0x10, &r) != 0)
            continue;
        if ((r.eax & 0xFFFF) != 0x004F)
            continue;
        seen++;
        u32 w = rd16m(VBE_MINFO + 0x12);
        u32 h = rd16m(VBE_MINFO + 0x14);
        u8 bpp = (u8)rd16m(VBE_MINFO + 0x19) & 0xFF;
        u8 model = (u8)rd16m(VBE_MINFO + 0x1B) & 0xFF;
        u32 attrs = rd16m(VBE_MINFO + 0x00);
        if (w != 800 || h != 600 || bpp != 32 || model != 6)
            continue;
        if (!(attrs & 0x0080))          /* linear framebuffer bit */
            continue;
        /* set it with the LFB flag */
        r.eax = 0x4F02;
        r.ebx = mode | 0x4000;
        if (ocbl_bios_int(0x10, &r) != 0)
            return false;
        if ((r.eax & 0xFFFF) != 0x004F)
            return false;
        fb->addr = rd32m(VBE_MINFO + 0x28);   /* PhysBasePtr */
        fb->pitch = rd16m(VBE_MINFO + 0x10);
        fb->width = w;
        fb->height = h;
        fb->bpp = bpp;
        fb->red_size = (u8)rd16m(VBE_MINFO + 0x1F) & 0xFF;
        fb->red_pos = (u8)rd16m(VBE_MINFO + 0x20) & 0xFF;
        fb->green_size = (u8)rd16m(VBE_MINFO + 0x21) & 0xFF;
        fb->green_pos = (u8)rd16m(VBE_MINFO + 0x22) & 0xFF;
        fb->blue_size = (u8)rd16m(VBE_MINFO + 0x23) & 0xFF;
        fb->blue_pos = (u8)rd16m(VBE_MINFO + 0x24) & 0xFF;
        return true;
    }
    (void)seen;
    return false;
}

#endif /* OCBL_FLAVOR_BIOS */
