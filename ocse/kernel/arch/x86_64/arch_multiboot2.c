/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/arch/x86_64/arch_multiboot2.c
 */
#include "arch_multiboot2.h"
#include "lib_string.h"

/* WP-10d-pre: remember the parsed info for arch_multiboot2_get_kernel_self(). */
static const arch_multiboot2_info_t* g_mb2_info;

int arch_multiboot2_parse(arch_multiboot2_info_t* out, uintptr_t mbi_phys) {
    memset(out, 0, sizeof(*out));
    if (!mbi_phys) return -1;
    const arch_multiboot2_header_t* hdr = (const arch_multiboot2_header_t*)mbi_phys;
    /* BUG-0142 FIX (A1-9a): the mbi comes from the bootloader (GRUB is only
     * half-trusted). Validate the wrapper before dereferencing anything:
     * total_size must at least cover the header + END tag and must stay
     * within a sane bound (1 MiB - the mbi lives in low memory). */
    if (hdr->total_size < 16 || hdr->total_size > 0x100000u) return -1;
    out->header = hdr;
    g_mb2_info = out;   /* WP-10d-pre: remember for arch_multiboot2_get_kernel_self */

    /* Walk all tags. First tag begins at offset 8 of the mbi. */
    uintptr_t p = mbi_phys + 8;
    uintptr_t end = mbi_phys + hdr->total_size;
    while (p + 8 <= end) {
        const arch_multiboot2_tag_t* tag = (const arch_multiboot2_tag_t*)p;
        if (tag->type == OC_MB2_TAG_END) break;
        u32 sz = tag->size;
        if (sz < 8) break;     /* malformed */
        /* BUG-0142 FIX (A1-9a): a truncated tag (or one whose declared size
         * runs past the mbi end) must not be dereferenced - the old loop
         * only rejected sz < 8 and then read fb/meminfo/module fields
         * straight out of out-of-bounds memory. */
        if (p + sz > end) break;
        switch (tag->type) {
        case OC_MB2_TAG_FRAMEBUFFER: {
            if (sz < sizeof(arch_multiboot2_fb_tag_t)) break;  /* too small: skip */
            const arch_multiboot2_fb_tag_t* f = (const arch_multiboot2_fb_tag_t*)tag;
            out->fb = f;
            break;
        }
        case OC_MB2_TAG_MMAP: {
            if (sz < sizeof(arch_multiboot2_mmap_tag_t)) break;
            out->mmap = (const arch_multiboot2_mmap_tag_t*)tag;
            break;
        }
        case OC_MB2_TAG_CMDLINE: {
            if (sz < sizeof(arch_multiboot2_str_tag_t) + 1) break;
            const arch_multiboot2_str_tag_t* s = (const arch_multiboot2_str_tag_t*)tag;
            /* BUG-0142 FIX (A1-9a): the string must be NUL-terminated
             * within the tag; otherwise every consumer's strcmp runs past
             * the tag into unrelated mbi bytes. */
            int terminated = 0;
            for (u32 i = 0; i + 8 < sz; i++) {
                if (s->str[i] == 0) { terminated = 1; break; }
            }
            if (!terminated) break;
            out->cmdline = s->str;
            break;
        }
        case OC_MB2_TAG_BOOT_LOADER_NAME: {
            if (sz < sizeof(arch_multiboot2_str_tag_t) + 1) break;
            const arch_multiboot2_str_tag_t* s = (const arch_multiboot2_str_tag_t*)tag;
            int terminated = 0;
            for (u32 i = 0; i + 8 < sz; i++) {
                if (s->str[i] == 0) { terminated = 1; break; }
            }
            if (!terminated) break;
            out->loader_name = s->str;
            break;
        }
        case OC_MB2_TAG_BASIC_MEMINFO: {
            /* type(4) + size(4), then u32 mem_lower, u32 mem_upper (in KiB).
             * Read through the packed tag type: a bare u32* cast from the
             * alignment-1 tag pointer trips -Waddress-of-packed-member. */
            if (sz < sizeof(arch_multiboot2_meminfo_tag_t)) break;
            const arch_multiboot2_meminfo_tag_t* mi = (const arch_multiboot2_meminfo_tag_t*)tag;
            out->mem_lower_kb = mi->mem_lower;
            out->mem_upper_kb = mi->mem_upper;
            break;
        }
        case OC_MB2_TAG_MODULE: {
            /* WP-10d-pre: the grub.cfg files attach the booting kernel
             * itself as a module with cmdline "self" so install/abdisk
             * can copy it onto a target disk. */
            if (sz < sizeof(arch_multiboot2_module_tag_t) + 5) break;
            const arch_multiboot2_module_tag_t* m = (const arch_multiboot2_module_tag_t*)tag;
            if (m->cmdline[0] == 's' && m->cmdline[1] == 'e' &&
                m->cmdline[2] == 'l' && m->cmdline[3] == 'f' &&
                (m->cmdline[4] == 0 || m->cmdline[4] == ' '))
                out->arch_kernel_self = m;
            break;
        }
        case OC_MB2_TAG_ACPI_OLD: {
            /* WP-10-input_chipset: ACPI 1.x RSDP copy (20 bytes after the
             * 8-byte tag header). Validate the "RSD PTR " signature inside
             * the copy before trusting it. */
            if (sz < sizeof(arch_multiboot2_rsdp_tag_t) + 20) break;
            out->rsdp_phys = (u64)((uintptr_t)tag + 8);
            out->rsdp_acpi_version = 1;
            break;
        }
        case OC_MB2_TAG_ACPI_NEW: {
            /* WP-10-input_chipset: ACPI 2.0+ RSDP copy (36 bytes). The
             * presence of the NEW tag beats an OLD one (GRUB emits both
             * on ACPI 2.0+ firmware). */
            if (sz < sizeof(arch_multiboot2_rsdp_tag_t) + 36) break;
            out->rsdp_phys = (u64)((uintptr_t)tag + 8);
            out->rsdp_acpi_version = 2;
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

int arch_multiboot2_get_kernel_self(const u8** data, u64* size) {
    if (!g_mb2_info || !g_mb2_info->arch_kernel_self) return -1;
    const arch_multiboot2_module_tag_t* m = g_mb2_info->arch_kernel_self;
    if (m->mod_end <= m->mod_start) return -1;
    if (data) *data = (const u8*)(uintptr_t)m->mod_start;
    if (size) *size = (u64)(m->mod_end - m->mod_start);
    return 0;
}

/* ---- WP-10-chinese_support-fix2: memory-map accessor ---------------- */

const arch_multiboot2_mmap_tag_t* arch_multiboot2_mmap(void) {
    return g_mb2_info ? g_mb2_info->mmap : NULL;
}
