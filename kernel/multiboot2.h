/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/multiboot2.h
 * Purpose: Multiboot2 info structure parser.
 *
 * GRUB hands us a contiguous structure in low memory:
 *
 *     u32 total_size;
 *     u32 reserved;
 *     tag[];
 *
 * Each tag has a u32 type and u32 size (the size is the *total* size of
 * the tag, including the 8-byte type+size header). Tags are padded to
 * 8-byte alignment.
 *
 * Layout reference: GNU multiboot2 spec + grub-core/lib/multiboot_elfxx.c
 * and grub-core/loader/multiboot_mbi2.c (which is what actually emits
 * the tags we receive).
 */
#ifndef OC_MULTIBOOT2_H
#define OC_MULTIBOOT2_H

#include "types.h"

#define OC_MB2_MAGIC 0x36d76289u

/* Tag types we care about. */
#define OC_MB2_TAG_END                 0
#define OC_MB2_TAG_CMDLINE             1
#define OC_MB2_TAG_BOOT_LOADER_NAME    2
#define OC_MB2_TAG_MODULE              3
#define OC_MB2_TAG_BASIC_MEMINFO       4
#define OC_MB2_TAG_BOOTDEV             5
#define OC_MB2_TAG_MMAP                6
#define OC_MB2_TAG_FRAMEBUFFER         8

typedef struct oc_mb2_header {
    u32 total_size;
    u32 reserved;
} __attribute__((packed)) oc_mb2_header_t;

/* Generic tag header (8 bytes). */
typedef struct oc_mb2_tag {
    u32 type;
    u32 size;          /* total size of this tag, including this 8-byte header */
} __attribute__((packed)) oc_mb2_tag_t;

/* Framebuffer info tag (type 8). Matches struct multiboot_tag_framebuffer. */
typedef struct oc_mb2_fb_tag {
    u32 type;          /* 8 */
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;       /* 0=indexed, 1=RGB, 2=text */
    u16 reserved0;              /* WP-04 fix: was u8+u16 (3 bytes), should be u16 (2 bytes) */
    /* RGB case (framebuffer_type == 1): */
    u8  red_field_position;
    u8  red_mask_size;
    u8  green_field_position;
    u8  green_mask_size;
    u8  blue_field_position;
    u8  blue_mask_size;
} __attribute__((packed)) oc_mb2_fb_tag_t;

/* Memory map tag (type 6). */
typedef struct oc_mb2_mmap_tag {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} oc_mb2_mmap_tag_t;

typedef struct oc_mb2_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;
    u32 reserved;
} __attribute__((packed)) oc_mb2_mmap_entry_t;

/* String tag (cmdline / loader name). */
typedef struct oc_mb2_str_tag {
    u32 type;
    u32 size;
    char str[];
} __attribute__((packed)) oc_mb2_str_tag_t;

typedef struct oc_mb2_info {
    const oc_mb2_header_t*   header;
    const oc_mb2_fb_tag_t*  fb;
    const oc_mb2_mmap_tag_t* mmap;
    const char* cmdline;
    const char* loader_name;
    u64 mem_lower_kb;
    u64 mem_upper_kb;
} oc_mb2_info_t;

/* Parse the multiboot2 info at `mbi_phys` and fill `out`. Returns 0 on success. */
int oc_mb2_parse(oc_mb2_info_t* out, uintptr_t mbi_phys);

#endif /* OC_MULTIBOOT2_H */
