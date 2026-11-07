/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_kexec.c
 * Purpose: kexec implementation (see ocire_kexec.h).
 *
 * ELF loading follows the same rule GRUB's multiboot2 loader uses for
 * ELF64 images: every PT_LOAD segment is copied to p_paddr, zero-
 * padded to p_memsz; the entry point is e_entry.  OCOS's own boot.S
 * then re-does GDT/paging from scratch, exactly as it would after
 * GRUB - the machine state we hand over matches the multiboot2 spec
 * (32-bit protected mode, paging off, interrupts off, flat segments,
 * eax = magic, ebx = info pointer).
 */

#include "ocire_kexec.h"
#include "ocire_mb2.h"
#include "ocire_util.h"
#include "ocire_io.h"

extern void ocire_kexec_jump32(u64 target, u64 mbi_addr);

/* ---- ELF64 header views (independent, spec-shaped) ---- */

#pragma pack(push, 1)
typedef struct {
    u8  ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_hdr_t;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} elf64_phdr_t;
#pragma pack(pop)

#define PT_LOAD 1

/* Bounds: OCOS loads at 1 MiB and stays well under 8 MiB; anything
 * outside that window is refused (a corrupt or hostile "kernel"
 * must not make the recovery kernel scribble over itself at 16 MiB
 * or over the low-memory trampoline area). */
#define KEXEC_DST_MIN 0x00100000ull
#define KEXEC_DST_MAX 0x00800000ull

int ocire_kexec_exec(const u8 *elf_img, u64 len, u64 mbi_src,
                     const char *cmdline_extra,
                     const u8 *agent, u64 agent_len) {
    if (len < sizeof(elf64_hdr_t)) return -1;
    const elf64_hdr_t *eh = (const elf64_hdr_t *)elf_img;
    if (eh->ident[0] != 0x7F || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F') return -2;
    if (eh->ident[4] != 2) return -3;            /* not ELF64 */
    if (eh->e_phentsize != sizeof(elf64_phdr_t)) return -4;

    /* ---- 1. load the PT_LOAD segments ---- */
    for (int i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph =
            (const elf64_phdr_t *)(elf_img + eh->e_phoff +
                                   (u64)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_paddr < KEXEC_DST_MIN ||
            ph->p_paddr + ph->p_memsz > KEXEC_DST_MAX) return -5;
        if (ph->p_offset + ph->p_filesz > len) return -6;
        u8 *dst = (u8 *)(uintptr)ph->p_paddr;
        oc_memset(dst, 0, ph->p_memsz);
        oc_memcpy(dst, elf_img + ph->p_offset, ph->p_filesz);
    }

    /* ---- 2. the agent module bytes ---- */
    if (agent && agent_len) {
        oc_memcpy((void *)(uintptr)OCIRE_KEXEC_AGENT_AT, agent, agent_len);
    }

    /* ---- 3. rebuild the multiboot2 info at 0x80000 ---- */
    u8 *out = (u8 *)(uintptr)OCIRE_KEXEC_MBI_AT;
    oc_memset(out, 0, 4096);
    u64 opos = 8;                                /* past total_size+res */

    const ocire_mb2_info_t *mb = ocire_mb2_get();
    u64 total = mb->mbi_total;
    u64 pos = mbi_src + 8;
    u64 end = mbi_src + total;
    int cmdline_done = 0;


    while (pos + 8 <= end) {
        const ocire_mb2_tag_t *t = (const ocire_mb2_tag_t *)(uintptr)pos;
        if (t->type == OCIRE_MB2_TAG_END) break;
        if (t->size < 8) break;

        if (t->type == OCIRE_MB2_TAG_CMDLINE) {
            /* Merge the original command line with the extra args. */
            const char *orig = ((const ocire_mb2_str_t *)t)->str;
            char merged[512];
            u64 n = 0;
            for (; orig[n] && n < 300; n++) merged[n] = orig[n];
            merged[n] = 0;
            u64 extra_len = oc_strlen(cmdline_extra);
            if (extra_len && n + 1 + extra_len < sizeof(merged)) {
                merged[n++] = ' ';
                for (u64 k = 0; k < extra_len; k++) merged[n++] = cmdline_extra[k];
                merged[n] = 0;
            }
            /* Tag: header + string + NUL, padded to 8. */
            u64 tag_size = 8 + n + 1;
            u64 padded = (tag_size + 7) & ~(u64)7;
            ocire_mb2_tag_t *nt = (ocire_mb2_tag_t *)(out + opos);
            nt->type = OCIRE_MB2_TAG_CMDLINE;
            nt->size = (u32)tag_size;
            oc_memcpy(out + opos + 8, merged, n + 1);
            for (u64 k = tag_size; k < padded; k++) out[opos + k] = 0;
            opos += padded;
            cmdline_done = 1;
        } else {
            /* Byte-identical copy of the original tag. */
            u64 padded = (t->size + 7) & ~(u64)7;
            if (opos + padded > 4096 - 64) break;   /* out of room */
            oc_memcpy(out + opos, (const void *)(uintptr)pos, t->size);
            for (u64 k = t->size; k < padded; k++) out[opos + k] = 0;
            opos += padded;
        }
        pos += (t->size + 7) & ~(u64)7;
    }

    if (!cmdline_done && cmdline_extra[0]) {
        /* No original cmdline: emit a fresh tag with just the extras. */
        u64 extra_len = oc_strlen(cmdline_extra);
        u64 tag_size = 8 + extra_len + 1;
        u64 padded = (tag_size + 7) & ~(u64)7;
        ocire_mb2_tag_t *nt = (ocire_mb2_tag_t *)(out + opos);
        nt->type = OCIRE_MB2_TAG_CMDLINE;
        nt->size = (u32)tag_size;
        oc_memcpy(out + opos + 8, cmdline_extra, extra_len + 1);
        for (u64 k = tag_size; k < padded; k++) out[opos + k] = 0;
        opos += padded;
    }

    /* The agent module tag. */
    if (agent && agent_len) {
        u64 tag_size = 8 + 8 + 4 + oc_strlen("ocire-agent") + 1;
        u64 padded = (tag_size + 7) & ~(u64)7;
        ocire_mb2_mod_t *mt = (ocire_mb2_mod_t *)(out + opos);
        mt->type = OCIRE_MB2_TAG_MODULE;
        mt->size = (u32)tag_size;
        mt->mod_start = (u32)OCIRE_KEXEC_AGENT_AT;
        mt->mod_end = (u32)(OCIRE_KEXEC_AGENT_AT + agent_len);
        oc_memcpy(out + opos + 20, "ocire-agent", 12);
        for (u64 k = tag_size; k < padded; k++) out[opos + k] = 0;
        opos += padded;
    }

    /* End tag + total size. */
    {
        ocire_mb2_tag_t *nt = (ocire_mb2_tag_t *)(out + opos);
        nt->type = OCIRE_MB2_TAG_END;
        nt->size = 8;
        opos += 8;
        *(u32 *)(void *)out = (u32)opos;         /* total_size */
        *(u32 *)(void *)(out + 4) = 0;           /* reserved */
    }


    /* ---- 4. the jump: never returns ---- */
    ocire_kexec_jump32((u64)eh->e_entry, OCIRE_KEXEC_MBI_AT);
    return 0;                                    /* unreachable */
}
