/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_mb2.h
 * Purpose: Multiboot2 info parser for OCIRE (WP-02).
 *
 * WP-01 discarded the multiboot2 info structure - the minimal shell
 * found everything from hardware.  WP-02 needs it for three things:
 *
 *   1. The framebuffer tag (type 8): the OCIRE header now REQUESTS a
 *      linear framebuffer (800x600x32) so the same video mode GRUB
 *      sets up for OCOS can be handed over again at kexec time (OCOS
 *      hard-fails without an RGB fb tag).  ocire_console uses it for
 *      LFB text rendering; without it OCIRE falls back to VGA text.
 *
 *   2. The command line (type 1): the GRUB dual-boot menu
 *      differentiates the three OCIRE boot roles:
 *        ocire.mode=sup   - boot supervisor (watchdog + hand over to
 *                           OCOS; the default menu entry)
 *        ocire.mode=full  - full recovery shell (manual menu pick)
 *        (absent)         - standalone media (ISO): degrade to full
 *
 *   3. The RAW info block itself: ocire_kexec copies the original
 *      GRUB mbi (memory map, RSDP pointer, framebuffer geometry) to a
 *      safe low-memory address and APPENDS a module tag for the OCIRE
 *      log agent before booting OCOS - so OCOS sees a byte-identical
 *      multiboot2 environment plus the injected agent.
 *
 * The tag structures below are written from the multiboot2 2.0 spec
 * (independent of the OCOS parser; the two kernels share no headers
 * by the WP-01 independence rule).
 */

#ifndef OCIRE_MB2_H
#define OCIRE_MB2_H

#include "ocire_types.h"

#define OCIRE_MB2_MAGIC 0x36d76289u

/* Tag types OCIRE consumes. */
#define OCIRE_MB2_TAG_END     0
#define OCIRE_MB2_TAG_CMDLINE 1
#define OCIRE_MB2_TAG_LOADER  2
#define OCIRE_MB2_TAG_MODULE  3
#define OCIRE_MB2_TAG_MMAP    6
#define OCIRE_MB2_TAG_FB      8
#define OCIRE_MB2_TAG_ACPI_OLD 14
#define OCIRE_MB2_TAG_ACPI_NEW 15

/* Every tag starts with u32 type + u32 total size (8-byte aligned). */
typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) ocire_mb2_tag_t;

/* Framebuffer info tag (type 8). */
typedef struct {
    u32 type;
    u32 size;
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8  bpp;
    u8  fb_type;      /* 1 = RGB (the only kind OCIRE renders) */
    u16 reserved0;
    u8  r_pos, r_sz, g_pos, g_sz, b_pos, b_sz;
} __attribute__((packed)) ocire_mb2_fb_t;

/* NUL-terminated string tag (cmdline / loader name). */
typedef struct {
    u32 type;
    u32 size;
    char str[];
} __attribute__((packed)) ocire_mb2_str_t;

/* Module tag (type 3). */
typedef struct {
    u32 type;
    u32 size;
    u32 mod_start;    /* physical start (inclusive) */
    u32 mod_end;      /* physical end (exclusive) */
    char cmdline[];
} __attribute__((packed)) ocire_mb2_mod_t;

/* Memory map tag (type 6). */
typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} __attribute__((packed)) ocire_mb2_mmap_t;

/* Parsed view of the info block.  Pointers are PHYSICAL addresses;
 * OCIRE runs under a 4 GiB identity map, so they dereference
 * directly. */
typedef struct {
    u64 mbi_phys;               /* address GRUB handed us */
    u32 mbi_total;              /* total_size field of the header */
    const ocire_mb2_fb_t   *fb;
    const char             *cmdline;
    const char             *loader_name;
    const ocire_mb2_mmap_t *mmap;
    u64 rsdp_phys;              /* 0 = tag absent */
    u32 rsdp_version;           /* 0 none, 1 ACPI 1.x, 2 ACPI 2.0+ */
} ocire_mb2_info_t;

/* Parse the block at mbi_phys.  Returns 0 on success (magic + header
 * sane), -1 on failure.  The result stays valid for the whole boot:
 * nothing in OCIRE overwrites the bootloader's low memory. */
int ocire_mb2_parse(u64 magic, u64 mbi_phys);

/* Parsed info (all-zero struct before a successful parse). */
const ocire_mb2_info_t *ocire_mb2_get(void);

/* True when the command line contains "key=value" with value "val"
 * (val compared up to its NUL; a key with no '=' is a flag: pass
 * val="" to test it).  Used for ocire.mode=. */
int ocire_mb2_cmdline_kv(const char *key, const char *val);

#endif /* OCIRE_MB2_H */
