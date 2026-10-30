/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_boot.h
 * Purpose: payload loading + multiboot2 handoff (B4).
 *
 * Fixed layout of the handoff (BIOS flavor; the UEFI flavor keeps the
 * same MBI contract but exits boot services instead of downgrading):
 *   0x30000-0x3FFFF  64 KiB read bounce window (int13 DAP <1 MiB)
 *   0x50000          multiboot2 info (64 KiB)
 *   0x400000+        kernel PT_LOAD segments (kernel link base)
 *   0x800000+        the mb2 "self" module copy of the kernel ELF
 *   0x1000000+       OCIRE payload (menu entry OCBL_ENT_OCIRE)
 *
 * Handoff contract (identical to GRUB): 32-bit protected mode,
 * paging OFF, GDT may be ours, EAX = 0x36D76289, EBX = MBI physical,
 * jump to the kernel's 32-bit entry (boot.S _start).
 */
#ifndef OCBL_BOOT_H
#define OCBL_BOOT_H

#include "ocbl_types.h"

struct ocbl_volume;
struct ocbl_fb_info;

#define OCBL_BOUNCE        0x30000u
#define OCBL_BOUNCE_SZ     0x10000u
#define OCBL_MB2_MAGIC     0x36D76289u
#define OCBL_SELF_BASE     0x800000ull
#define OCBL_OCIRE_BASE    0x1000000ull

/* ELF64 loader: read `path` from `v`, place every PT_LOAD at its
 * p_paddr (chunked through the bounce window), zero the tail, return
 * the entry point (0 on failure).  `total_size` gets the file size
 * when non-NULL (the "self" module needs the whole file elsewhere). */
u64 ocbl_load_elf(const struct ocbl_volume* v, const char* path,
                  u64 want_base, u32* total_size);

/* Copy the raw file to `want_base` (the "self" module copy). */
u32 ocbl_load_raw(const struct ocbl_volume* v, const char* path,
                  u64 want_base);

/* Build the multiboot2 info at 0x50000.  slot_cmdline: "A"/"B"/"ISO"
 * (0 = no oc.slot prefix, used for OCIRE).  Returns the info size. */
u32 ocbl_build_mbi(const char* slot_cmdline, u64 self_start, u64 self_end,
                   const struct ocbl_fb_info* fb);

/* RSDP discovery (ocbl_acpi.c): validated physical address or 0. */
u64 ocbl_rsdp_find(void);

/* Downgrade 64->32 and jump to the kernel.  Does not return. */
void ocbl_handoff(u64 entry, u64 mbi_phys);

#endif /* OCBL_BOOT_H */
