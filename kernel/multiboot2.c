/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/multiboot2.c
 */
#include "multiboot2.h"
#include "string.h"

/* WP-10d-pre: remember the parsed info for oc_mb2_get_kernel_self(). */
static const oc_mb2_info_t* g_mb2_info;

int oc_mb2_parse(oc_mb2_info_t* out, uintptr_t mbi_phys) {
    oc_memset(out, 0, sizeof(*out));
    if (!mbi_phys) return -1;
    const oc_mb2_header_t* hdr = (const oc_mb2_header_t*)mbi_phys;
    out->header = hdr;
    g_mb2_info = out;   /* WP-10d-pre: remember for oc_mb2_get_kernel_self */

    /* Walk all tags. First tag begins at offset 8 of the mbi. */
    uintptr_t p = mbi_phys + 8;
    uintptr_t end = mbi_phys + hdr->total_size;
    while (p + 8 <= end) {
        const oc_mb2_tag_t* tag = (const oc_mb2_tag_t*)p;
        if (tag->type == OC_MB2_TAG_END) break;
        u32 sz = tag->size;
        if (sz < 8) break;     /* malformed */
        switch (tag->type) {
        case OC_MB2_TAG_FRAMEBUFFER: {
            const oc_mb2_fb_tag_t* f = (const oc_mb2_fb_tag_t*)tag;
            out->fb = f;
            break;
        }
        case OC_MB2_TAG_MMAP: {
            out->mmap = (const oc_mb2_mmap_tag_t*)tag;
            break;
        }
        case OC_MB2_TAG_CMDLINE: {
            const oc_mb2_str_tag_t* s = (const oc_mb2_str_tag_t*)tag;
            out->cmdline = s->str;
            break;
        }
        case OC_MB2_TAG_BOOT_LOADER_NAME: {
            const oc_mb2_str_tag_t* s = (const oc_mb2_str_tag_t*)tag;
            out->loader_name = s->str;
            break;
        }
        case OC_MB2_TAG_BASIC_MEMINFO: {
            /* type(4) + size(4), then u32 mem_lower, u32 mem_upper (in KiB) */
            const u32* p32 = (const u32*)tag;
            out->mem_lower_kb = p32[2];
            out->mem_upper_kb = p32[3];
            break;
        }
        case OC_MB2_TAG_MODULE: {
            /* WP-10d-pre: the grub.cfg files attach the booting kernel
             * itself as a module with cmdline "self" so install/abdisk
             * can copy it onto a target disk. */
            const oc_mb2_module_tag_t* m = (const oc_mb2_module_tag_t*)tag;
            if (m->cmdline[0] == 's' && m->cmdline[1] == 'e' &&
                m->cmdline[2] == 'l' && m->cmdline[3] == 'f' &&
                (m->cmdline[4] == 0 || m->cmdline[4] == ' '))
                out->kernel_self = m;
            break;
        }
        default:
            break;
        }
        /* Advance: tag size padded to 8 bytes. */
        uintptr_t next = p + sz;
        next = (next + 7) & ~(uintptr_t)7;
        p = next;
    }
    return (out->fb != NULL) ? 0 : -1;
}

/* ---- WP-10d-pre: boot-attached kernel self image ------------------- */

int oc_mb2_get_kernel_self(const u8** data, u64* size) {
    if (!g_mb2_info || !g_mb2_info->kernel_self) return -1;
    const oc_mb2_module_tag_t* m = g_mb2_info->kernel_self;
    if (m->mod_end <= m->mod_start) return -1;
    if (data) *data = (const u8*)(uintptr_t)m->mod_start;
    if (size) *size = (u64)(m->mod_end - m->mod_start);
    return 0;
}
