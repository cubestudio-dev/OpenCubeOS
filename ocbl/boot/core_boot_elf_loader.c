/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL shared ELF64 kernel loader (WP-02-ocbl).
 *
 * Implementation for the interface declared in core_boot_elf_loader.h.
 * See the header for the contract; this file only adds the ELF64
 * structure definitions (exact layout, ELF specification) and the
 * byte-wise memory helpers.
 */

#include <stdint.h>
#include <stddef.h>

#include "boot/core_boot_elf_loader.h"

/* ---- ELF64 structures (exact layout per the ELF specification) ----- */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} core_boot_elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} core_boot_elf64_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} core_boot_elf64_shdr_t;

#define CORE_BOOT_ELF_PT_LOAD 1U

/* e_ident indices */
#define CORE_BOOT_ELF_IDX_MAG0 0U
#define CORE_BOOT_ELF_IDX_CLASS 4U
#define CORE_BOOT_ELF_IDX_DATA 5U
#define CORE_BOOT_ELF_IDX_VERSION 6U

#define CORE_BOOT_ELF_CLASS64 2U
#define CORE_BOOT_ELF_DATA2LSB 1U
#define CORE_BOOT_ELF_CURRENT 1U
#define CORE_BOOT_ELF_EM_X86_64 62U
#define CORE_BOOT_ELF_EXEC 2U

static void core_boot_elf_copy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n != 0U) {
        *d = *s;
        ++d;
        ++s;
        --n;
    }
}

static void core_boot_elf_zero(void *dst, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n != 0U) {
        *d = 0U;
        ++d;
        --n;
    }
}

/* tiny strcmp for section names (freestanding: no libc) */
static int core_boot_elf_streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return (*a == *b);
}

/* Validate the ELF64 header; on success return NULL, on failure a
 * short reason string. Shared by probe and load. */
static const core_boot_elf64_ehdr_t *core_boot_elf_validate(const void *image)
{
    const uint8_t *base = (const uint8_t *)image;
    const core_boot_elf64_ehdr_t *ehdr = (const core_boot_elf64_ehdr_t *)base;

    if (ehdr->e_ident[CORE_BOOT_ELF_IDX_MAG0] != 0x7FU) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_ident[CORE_BOOT_ELF_IDX_CLASS] != CORE_BOOT_ELF_CLASS64) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_ident[CORE_BOOT_ELF_IDX_DATA] != CORE_BOOT_ELF_DATA2LSB) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_ident[CORE_BOOT_ELF_IDX_VERSION] != CORE_BOOT_ELF_CURRENT) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_machine != CORE_BOOT_ELF_EM_X86_64) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_type != CORE_BOOT_ELF_EXEC) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_version != 1U) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    if (ehdr->e_phnum == 0U ||
        ehdr->e_phentsize < sizeof(core_boot_elf64_phdr_t)) {
        return (const core_boot_elf64_ehdr_t *)0;
    }
    return ehdr;
}

int core_boot_elf_probe(const void *image, uint64_t *first, uint64_t *last)
{
    const uint8_t *base = (const uint8_t *)image;
    const core_boot_elf64_ehdr_t *ehdr = core_boot_elf_validate(image);
    const core_boot_elf64_phdr_t *phdr;
    uint16_t i;
    uint64_t lo = 0U;
    uint64_t hi = 0U;
    int seen = 0;

    if (ehdr == (const core_boot_elf64_ehdr_t *)0) {
        return 1;
    }
    for (i = 0U; i < ehdr->e_phnum; ++i) {
        phdr = (const core_boot_elf64_phdr_t *)
               (base + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (phdr->p_type != CORE_BOOT_ELF_PT_LOAD) {
            continue;
        }
        if (phdr->p_paddr < CORE_BOOT_ELF_MIN_PADDR ||
            phdr->p_paddr >= CORE_BOOT_ELF_MAX_PADDR ||
            phdr->p_memsz > CORE_BOOT_ELF_MAX_PADDR - phdr->p_paddr ||
            phdr->p_filesz > phdr->p_memsz) {
            return 1;
        }
        if (seen == 0 || phdr->p_paddr < lo) {
            lo = phdr->p_paddr;
        }
        if (seen == 0 || phdr->p_paddr + phdr->p_memsz > hi) {
            hi = phdr->p_paddr + phdr->p_memsz;
        }
        seen = 1;
    }
    if (seen == 0) {
        return 1;
    }
    *first = lo;
    *last = hi;
    return 0;
}

uint64_t core_boot_elf_load(const void *image)
{
    const uint8_t *base = (const uint8_t *)image;
    const core_boot_elf64_ehdr_t *ehdr = core_boot_elf_validate(image);
    const core_boot_elf64_phdr_t *phdr;
    uint16_t i;
    uint64_t entry;

    if (ehdr == (const core_boot_elf64_ehdr_t *)0) {
        return 0U;
    }

    /* --- map every PT_LOAD segment to p_paddr ------------------------ */
    for (i = 0U; i < ehdr->e_phnum; ++i) {
        phdr = (const core_boot_elf64_phdr_t *)
               (base + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

        if (phdr->p_type != CORE_BOOT_ELF_PT_LOAD) {
            continue;
        }
        if (phdr->p_paddr < CORE_BOOT_ELF_MIN_PADDR ||
            phdr->p_paddr >= CORE_BOOT_ELF_MAX_PADDR ||
            phdr->p_memsz > CORE_BOOT_ELF_MAX_PADDR - phdr->p_paddr) {
            return 0U;
        }
        if (phdr->p_filesz > phdr->p_memsz) {
            return 0U;
        }

        core_boot_elf_copy((void *)(uintptr_t)phdr->p_paddr,
                           base + phdr->p_offset, phdr->p_filesz);
        if (phdr->p_memsz > phdr->p_filesz) {
            core_boot_elf_zero((void *)(uintptr_t)(phdr->p_paddr + phdr->p_filesz),
                               phdr->p_memsz - phdr->p_filesz);
        }
    }

    /* --- pick the entry point ----------------------------------------- */
    /* e_entry is the 32-bit Multiboot2 trampoline (_start), meant for
     * a firmware that hands over in protected mode. OCBL hands over
     * in long mode, so the kernel publishes the absolute address of
     * its 64-bit entry (_start64) in the .ocbl_boot64 section; prefer
     * it whenever present, fall back to e_entry otherwise. */
    entry = ehdr->e_entry;
    if (ehdr->e_shoff != 0U && ehdr->e_shnum != 0U &&
        ehdr->e_shentsize >= sizeof(core_boot_elf64_shdr_t) &&
        ehdr->e_shstrndx < ehdr->e_shnum) {
        const core_boot_elf64_shdr_t *shdrs =
            (const core_boot_elf64_shdr_t *)(base + ehdr->e_shoff);
        const char *shstrtab =
            (const char *)(base + shdrs[ehdr->e_shstrndx].sh_offset);

        for (i = 0U; i < ehdr->e_shnum; ++i) {
            const char *name = shstrtab + shdrs[i].sh_name;

            if (core_boot_elf_streq(name, ".ocbl_boot64") &&
                shdrs[i].sh_size >= 8U) {
                const uint64_t *ptr64 =
                    (const uint64_t *)(const void *)(base + shdrs[i].sh_offset);

                if (*ptr64 != 0U) {
                    entry = *ptr64;
                }
                break;
            }
        }
    }

    return entry;
}
