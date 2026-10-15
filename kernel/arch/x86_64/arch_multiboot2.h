/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/arch/x86_64/arch_multiboot2.h
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
/* WP-10-input_chipset: ACPI RSDP tags. GRUB emits tag 14 (RSDP v1, 20
 * bytes) when the platform is ACPI 1.x only, tag 15 (RSDP v2+, 36 bytes)
 * when ACPI 2.0+ tables are present. Both carry the PHYSICAL address of
 * the RSDP; the driver_acpi parser re-validates the signature there. */
#define OC_MB2_TAG_ACPI_OLD            14
#define OC_MB2_TAG_ACPI_NEW            15

/* WP-10-input_chipset: RSDP tags (type 14 / type 15). Per the
 * multiboot2 spec the tag payload contains a COPY of the RSDP
 * structure itself (not a pointer to it). We remember the physical
 * address of that copy so the ACPI code can validate and use it in
 * place without copying again. */
typedef struct arch_multiboot2_rsdp_tag {
    u32 type;          /* 14 or 15 */
    u32 size;          /* 8 + 20 (old) or 8 + 36 (new) */
    u8  rsdp[];        /* RSDP structure copy */
} __attribute__((packed)) arch_multiboot2_rsdp_tag_t;

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
    /* WP-10-input_chipset: physical address of the RSDP copy handed to
     * us by GRUB (tag 14/15) and the ACPI revision it implies (0 = no
     * ACPI, 1 = ACPI 1.x, 2 = ACPI 2.0+). driver_acpi_init() consumes it. */
    u64 rsdp_phys;
    u32 rsdp_acpi_version;
} arch_multiboot2_info_t;

/* Parse the multiboot2 info at `mbi_phys` and fill `out`. Returns 0 on success. */
int arch_multiboot2_parse(arch_multiboot2_info_t* out, uintptr_t mbi_phys);

/* WP-10d-pre: the kernel ELF attached by GRUB as a module (cmdline
 * "self").  Returns 0 and fills data/size when present, -1 when the
 * bootloader did not attach it (then install/abdisk refuse to run). */
int arch_multiboot2_get_kernel_self(const u8** data, u64* size);

#endif /* OC_MULTIBOOT2_H */
