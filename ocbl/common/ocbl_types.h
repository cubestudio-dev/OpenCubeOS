/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_types.h
 * Purpose: freestanding base types for the OCBL boot loader (compiled
 *          both for the BIOS image, -m64 at 0x8200, and for the UEFI
 *          PE image, -m64 at an EFI-loaded base).
 *
 * OCBL is a boot loader, not a kernel: no heap, no scheduler, no
 * syscalls - statics and explicit fixed low-memory blocks only.
 */
#ifndef OCBL_TYPES_H
#define OCBL_TYPES_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;
typedef signed long long s64;

typedef u8  bool;
#define true  1
#define false 0

#define NULL ((void*)0)

typedef u32 ocbl_phys_t;   /* usable below-4G addressing for handoff data */

#define OCBL_PACKED __attribute__((packed))
#define OCBL_INLINE static inline

/* Fixed low-memory map of the BIOS flavor (see also core_head.S):
 *   0x00500-0x0051F  A20 test area
 *   0x00520          {u8 boot_drive, u8 flavor} written by stage1
 *   0x00600          int13h DAP scratch (stage1)
 *   0x40000-0x4FFFF  16<->64 real-mode trampoline + BIOS reg block
 *   0x50000-0x57FFF  multiboot2 info build buffer (64 KiB)
 *   0x60000-0x9EFFF  scan scratch (E820 copy, path buffers, ISO/FAT meta)
 *   0x8F000-0x94FFF  OCBL identity page tables
 *   0x9F000          core stack top (grows down)
 * The kernel payloads land at their own link bases: OCOS 0x400000+,
 * the mb2 "self" module copy 0x800000+, OCIRE 0x1000000+.
 */
#define OCBL_LO_BOOTDRIVE   0x520
#define OCBL_LO_FLAVOR      0x521
#define OCBL_TRAMP_BASE     0x40000
#define OCBL_MBI_BASE       0x50000
#define OCBL_SCRATCH_BASE   0x60000
#define OCBL_STACK_TOP      0x9F000

/* A "boot entry" the scanner produces; the menu shows one line each. */
#define OCBL_MAX_ENTRIES    64
#define OCBL_LABEL_MAX      48

typedef enum {
    OCBL_ENT_NONE = 0,
    OCBL_ENT_OCOS,        /* multiboot2 kernel: src/L0 opencube.elf      */
    OCBL_ENT_OCIRE,       /* multiboot2 kernel: ocire.elf                */
    OCBL_ENT_OCSE,        /* install-environment kernel (WP-06 marker)   */
    OCBL_ENT_WINDOWS,     /* chainload VBR / bootmgfw.efi                 */
    OCBL_ENT_LINUX,       /* chainload the volume's own loader           */
    OCBL_ENT_GENERIC,     /* chainload VBR / BOOTX64.EFI                  */
    OCBL_ENT_SHUTDOWN,    /* always last: ACPI S5                         */
} ocbl_ent_type_t;

#endif /* OCBL_TYPES_H */

/* Fixed-address access helpers (GCC -Warray-bounds false-positives
 * literal pointer casts at -O2, so low-memory facts go through asm). */
OCBL_INLINE u8 ocbl_peek8(u64 addr)
{
    u8 v;
    __asm__ volatile("movb (%1), %0" : "=q"(v) : "r"(addr) : "memory");
    return v;
}

OCBL_INLINE u16 ocbl_peek16(u64 addr)
{
    u16 v;
    __asm__ volatile("movw (%1), %0" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

OCBL_INLINE u32 ocbl_peek32(u64 addr)
{
    u32 v;
    __asm__ volatile("movl (%1), %0" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

OCBL_INLINE void ocbl_poke8(u64 addr, u8 v)
{
    __asm__ volatile("movb %0, (%1)" : : "q"(v), "r"(addr) : "memory");
}
