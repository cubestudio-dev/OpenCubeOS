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

typedef struct arch_multiboot2_header {
    u32 total_size;
    u32 reserved;
} __attribute__((packed)) arch_multiboot2_header_t;

/* Generic tag header (8 bytes). */
typedef struct arch_multiboot2_tag {
    u32 type;
    u32 size;          /* total size of this tag, including this 8-byte header */
} __attribute__((packed)) arch_multiboot2_tag_t;

/* Framebuffer info tag (type 8). Matches struct multiboot_tag_framebuffer. */
typedef struct arch_multiboot2_fb_tag {
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
} __attribute__((packed)) arch_multiboot2_fb_tag_t;

/* Memory map tag (type 6). */
typedef struct arch_multiboot2_mmap_tag {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} arch_multiboot2_mmap_tag_t;

typedef struct arch_multiboot2_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;
    u32 reserved;
} __attribute__((packed)) arch_multiboot2_mmap_entry_t;

/* String tag (cmdline / loader name). */
typedef struct arch_multiboot2_str_tag {
    u32 type;
    u32 size;
    char str[];
} __attribute__((packed)) arch_multiboot2_str_tag_t;

/* Module tag (type 3). GRUB's `module2 <file> <cmdline>` creates one.
 * The ISO / disk grub.cfg files attach the kernel ELF itself as a
 * module with cmdline "self", so the in-system `install` / `abdisk`
 * commands can copy the exact booting kernel onto a target disk
 * (mainstream installer pattern: take the payload from the boot media). */
typedef struct arch_multiboot2_module_tag {
    u32 type;          /* 3 */
    u32 size;
    u32 mod_start;     /* physical start of the module image */
    u32 mod_end;       /* physical end (exclusive) */
    char cmdline[];    /* NUL-terminated, then padding */
} __attribute__((packed)) arch_multiboot2_module_tag_t;

typedef struct arch_multiboot2_info {
    const arch_multiboot2_header_t*   header;
    const arch_multiboot2_fb_tag_t*  fb;
    const arch_multiboot2_mmap_tag_t* mmap;
    const char* cmdline;
    const char* loader_name;
    u64 mem_lower_kb;
    u64 mem_upper_kb;
    const arch_multiboot2_module_tag_t* arch_kernel_self;   /* module with cmdline "self" */
} arch_multiboot2_info_t;

/* Parse the multiboot2 info at `mbi_phys` and fill `out`. Returns 0 on success. */
int arch_multiboot2_parse(arch_multiboot2_info_t* out, uintptr_t mbi_phys);

/* WP-10d-pre: the kernel ELF attached by GRUB as a module (cmdline
 * "self").  Returns 0 and fills data/size when present, -1 when the
 * bootloader did not attach it (then install/abdisk refuse to run). */
int arch_multiboot2_get_kernel_self(const u8** data, u64* size);

#endif /* OC_MULTIBOOT2_H */
