/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_mb2.c
 * Purpose: multiboot2 info walk (see ocire_mb2.h).
 */

#include "ocire_mb2.h"
#include "ocire_util.h"

static ocire_mb2_info_t g_mb2;

int ocire_mb2_parse(u64 magic, u64 mbi_phys) {
    oc_memset(&g_mb2, 0, sizeof(g_mb2));
    if (magic != OCIRE_MB2_MAGIC || mbi_phys == 0) return -1;

    /* Header: u32 total_size, u32 reserved, then the tag chain. */
    u32 total = *(const u32 *)(uintptr)mbi_phys;
    if (total < 8 || total > 0x4000) return -1;   /* sanity bound */

    g_mb2.mbi_phys = mbi_phys;
    g_mb2.mbi_total = total;

    /* Relocate the info block to a safe low-memory page FIRST: GRUB
     * places the mbi right after the loaded kernel image (here: just
     * above OCIRE at 16 MiB), which lands inside OCIRE's static
     * buffers once the supervisor stages the OCOS ELF - the kexec
     * hand-over would read a corrupted tag chain.  0x70000 is free
     * conventional memory (above the BIOS data area, below every
     * kernel and below the 0x80000 kexec mbi area). */
    if (mbi_phys != 0x70000) {
        oc_memcpy((void *)(uintptr)0x70000, (const void *)(uintptr)mbi_phys, total);
        mbi_phys = 0x70000;
        g_mb2.mbi_phys = mbi_phys;   /* consumers must see the copy */
    }

    u64 pos = mbi_phys + 8;
    u64 end = mbi_phys + total;
    while (pos + 8 <= end) {
        const ocire_mb2_tag_t *t = (const ocire_mb2_tag_t *)(uintptr)pos;
        if (t->type == OCIRE_MB2_TAG_END) break;
        if (t->size < 8) break;                    /* malformed */
        switch (t->type) {
        case OCIRE_MB2_TAG_CMDLINE:
            g_mb2.cmdline = ((const ocire_mb2_str_t *)t)->str;
            break;
        case OCIRE_MB2_TAG_LOADER:
            g_mb2.loader_name = ((const ocire_mb2_str_t *)t)->str;
            break;
        case OCIRE_MB2_TAG_MMAP:
            g_mb2.mmap = (const ocire_mb2_mmap_t *)t;
            break;
        case OCIRE_MB2_TAG_FB:
            g_mb2.fb = (const ocire_mb2_fb_t *)t;
            break;
        case OCIRE_MB2_TAG_ACPI_OLD:
            g_mb2.rsdp_phys = pos + 8;
            g_mb2.rsdp_version = 1;
            break;
        case OCIRE_MB2_TAG_ACPI_NEW:
            g_mb2.rsdp_phys = pos + 8;
            g_mb2.rsdp_version = 2;
            break;
        default:
            break;                                 /* not consumed */
        }
        pos += (t->size + 7) & ~(u64)7;            /* 8-byte align */
    }
    return 0;
}

const ocire_mb2_info_t *ocire_mb2_get(void) {
    return (const ocire_mb2_info_t *)&g_mb2;
}

int ocire_mb2_cmdline_kv(const char *key, const char *val) {
    if (!g_mb2.cmdline || !key) return 0;
    const char *s = g_mb2.cmdline;
    while (*s) {
        /* Advance to the start of the next word. */
        while (*s == ' ') s++;
        const char *w = s;
        const char *k = key;
        while (*w && *k && *w == *k) { w++; k++; }
        if (*k == 0 && *w == '=') {
            w++;
            const char *v = val;
            while (*w && *v && *w == *v) { w++; v++; }
            if (*v == 0 && (*w == 0 || *w == ' ')) return 1;
        }
        while (*s && *s != ' ') s++;
    }
    return 0;
}
