/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_kexec.h
 * Purpose: kexec hand-over - OCIRE loads OCOS and jumps, giving it a
 *          multiboot2 environment byte-compatible with what GRUB
 *          would have provided (plus the injected log-agent module).
 *
 * Physical layout used by the hand-over (OCIRE itself runs at 16 MiB,
 * so none of these overlap it):
 *
 *   0x80000   the rebuilt multiboot2 info block
 *   0x100000+ the OCOS kernel segments (their natural p_paddr)
 *   0x2100000 scratch: the raw OCOS ELF image while its segments are
 *             being copied out (released after the copy)
 *   0x2000000 the log-agent module bytes (left in place; OCOS copies
 *             them out early in kmain before its PMM can reuse them)
 *
 * The rebuilt info block copies every original GRUB tag (memory map,
 * framebuffer, RSDP, loader name, ...) except the command line and
 * the end tag, then appends:
 *   - a NEW cmdline tag: the original string plus the hand-over
 *     parameters OCOS consumes ("oc.mb=<lba> oc.log=<lba>
 *     oc.logsecs=<n> oc.wdt=ib700 oc.agent=1"),
 *   - a module tag for the agent (mod_start/mod_end at 0x2000000,
 *     cmdline "ocire-agent"),
 *   - the end tag.
 */

#ifndef OCIRE_KEXEC_H
#define OCIRE_KEXEC_H

#include "ocire_types.h"

#define OCIRE_KEXEC_MBI_AT   0x80000u      /* rebuilt info block */
#define OCIRE_KEXEC_AGENT_AT 0x2000000u    /* agent module bytes */
#define OCIRE_KEXEC_ELF_AT   0x2100000u    /* scratch ELF image */

/* Load the OCOS ELF64 image (already in memory at elf_img, len bytes)
 * into its physical segments and jump into it.
 *
 * mbi_src     - the ORIGINAL GRUB info block (physical address)
 * cmdline_extra - extra arguments appended to the OCOS command line
 *                 (may be "" - the separator space is added)
 * agent       - agent module bytes (may be NULL: no module tag)
 * agent_len   - byte count (0 with NULL)
 *
 * Returns only on failure (bad ELF, copy overflow): 0 on success is
 * NEVER returned - the machine belongs to OCOS by then. */
int ocire_kexec_exec(const u8 *elf_img, u64 len, u64 mbi_src,
                     const char *cmdline_extra,
                     const u8 *agent, u64 agent_len);

#endif /* OCIRE_KEXEC_H */
