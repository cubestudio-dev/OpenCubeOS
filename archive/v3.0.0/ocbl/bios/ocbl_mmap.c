/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_mmap.c
 * Purpose: firmware memory map via int 15h AX=E820 (through the BIOS
 *          trampoline).  The result lives in the fixed low-memory
 *          buffer (0x60000) and is already multiboot2-shaped (u64 addr,
 *          u64 len, u32 type, u32 pad) so the mb2 tag builder can emit
 *          it verbatim in the handoff batch.
 */
#include "ocbl_mmap.h"
#include "ocbl_bios.h"
#include "ocbl_serial.h"

static ocbl_mmap_t* g_map(void)
{
    return (ocbl_mmap_t*)OCBL_MMAP_BUF;
}

u32 ocbl_mmap_query(void)
{
    ocbl_mmap_t* m = g_map();
    m->count = 0;
    for (int i = 0; i < OCBL_MMAP_MAX; i++)
        m->entries[i].addr = m->entries[i].len = m->entries[i].type =
        m->entries[i].reserved = 0;

    /* E820 loop: every call writes one 24-byte entry into the separate
     * scratch slot (0x61000); we copy it into the array each round.
     * The scratch must NOT live inside the map buffer: the entries
     * array begins at BUF+8 (u64 alignment), exactly where an in-buffer
     * scratch would sit - each call then silently corrupts entry[0]
     * (live-caught in B2a). */
    u32 got = 0;
    u32 id = 0;                                 /* E820 continuation id */
    for (u32 i = 0; i < OCBL_MMAP_MAX; i++) {
        ocbl_bios_regs_t r;
        r.eax = 0xE820;
        r.ebx = (u16)id;                         /* continuation id in/out */
        r.ecx = 24;                              /* buffer size */
        r.edx = 0x534D4150;                      /* 'SMAP' */
        r.es = OCBL_MMAP_SCRATCH >> 4;
        r.edi = 0;                              /* separate scratch slot */
        r.ds = 0;
        r.esi = 0;
        if (ocbl_bios_int(0x15, &r) != 0)
            break;                              /* CF = end of list */
        if ((r.eax & 0xFFFFFFFFu) != 0x534D4150u)
            break;                              /* full 'SMAP' echo check */
        volatile u32* src = (volatile u32*)OCBL_MMAP_SCRATCH;
        u64 eaddr = (u64)src[0] | ((u64)src[1] << 32);
        u64 elen = (u64)src[2] | ((u64)src[3] << 32);
        if (elen == 0)
            break;                          /* zero-length = terminator */
        m->entries[got].addr = eaddr;
        m->entries[got].len = elen;
        m->entries[got].type = src[4];
        m->entries[got].reserved = src[5];
        got++;
        id = r.ebx;
        if (id == 0)
            break;                              /* end of the map */
    }
    m->count = got;
    return got;
}

const ocbl_mmap_t* ocbl_mmap_get(void)
{
    return g_map();
}
