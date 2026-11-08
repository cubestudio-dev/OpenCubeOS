/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL shared ELF64 kernel loader (WP-02-ocbl).
 *
 * THE shared parser of both boot firmware paths: the BIOS 64-bit C
 * stage and the UEFI entry both hand this function a raw kernel.elf
 * image in memory and get back the entry point. It validates the
 * ELF64 header, copies every PT_LOAD segment to its physical
 * address (p_paddr), zeroes the tail (memsz > filesz = BSS) and
 * returns e_entry.
 *
 * Pure freestanding C: no platform headers, no toolchain-specific
 * builtins, compiled by BOTH gcc (ELF64) and mingw (PE32+). Memory
 * moves are byte-wise - the images are small and correctness beats
 * speed at boot time.
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), elf_loader (what it does).
 */

#ifndef CORE_BOOT_ELF_LOADER_H
#define CORE_BOOT_ELF_LOADER_H

#include <stdint.h>

/* Parse and load a kernel ELF64 image.
 *
 * image: pointer to the raw ELF file in memory (not yet relocated).
 * Returns the entry point address (e_entry) on success, or 0 when
 * the image does not validate (bad magic, wrong class/machine,
 * unplaceable segment). */
uint64_t core_boot_elf_load(const void *image);

/* Parse the ELF64 header WITHOUT loading anything and report the
 * physical address range the PT_LOAD segments will occupy:
 * [ *first, *last ). Returns 0 on success, non-zero when the image
 * does not validate.
 *
 * UEFI needs this to reserve exactly the right region with
 * AllocatePages BEFORE core_boot_elf_load copies into it (a blind
 * reservation of a fixed size fails on firmware that keeps its own
 * structures in low memory - OVMF puts its DXE firmware volume at
 * 8 MiB). The BIOS path does not need it: its memory is flat and
 * private. */
int core_boot_elf_probe(const void *image, uint64_t *first, uint64_t *last);

/* Sanity bounds for PT_LOAD destinations: NAK kernels live at or
 * above 1 MiB and below 1 GiB (the boot trampoline identity-maps
 * exactly that range). Anything else is rejected. */
#define CORE_BOOT_ELF_MIN_PADDR 0x00100000ULL
#define CORE_BOOT_ELF_MAX_PADDR 0x3F000000ULL

#endif /* CORE_BOOT_ELF_LOADER_H */
