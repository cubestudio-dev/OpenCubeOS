/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_load.c
 * Purpose: ELF64 loader + multiboot2 info builder (see ocbl_boot.h).
 *
 * All disk reads funnel through the 64 KiB bounce window at 0x30000
 * (int 13h DAP targets must sit below 1 MiB), then memcpy to the
 * destination physical address through the 4 GiB identity mapping.
 */
#include "ocbl_boot.h"
#include "ocbl_fs.h"
#include "ocbl_disk.h"
#include "ocbl_mmap.h"
#include "ocbl_fb.h"            /* shared ocbl_fb_info_t (the vbe.h one
                                 * was BIOS-only; the EFI flavor fills
                                 * the same shape from GOP) */
#include "ocbl_serial.h"

#define BOUNCE ((u8*)OCBL_BOUNCE)

/* ---- ELF64 structures (only the fields we consume) ---- */
typedef struct {
    u8  e_ident[16];
    u16 e_type, e_machine;
    u32 e_version;
    u64 e_entry, e_phoff, e_shoff;
    u32 e_flags;
    u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    u32 p_type, p_flags;
    u64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} elf64_phdr_t;

_Static_assert(sizeof(elf64_ehdr_t) == 64, "ehdr layout");
_Static_assert(sizeof(elf64_phdr_t) == 56, "phdr layout");

#define EHDR_ENTRY_OFF  0x18
#define EHDR_PHOFF_OFF  0x20
#define EHDR_PHNUM_OFF  0x38
#define EHDR_PHENT_OFF  0x36

static u16 rd16b(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32b(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}
static u64 rd64b(const u8* p)
{
    return (u64)rd32b(p) | ((u64)rd32b(p + 4) << 32);
}

/* ---- raw file copy to a physical base (bounce-chunked) ---- */
u32 ocbl_load_raw(const ocbl_volume_t* v, const char* path, u64 want_base)
{
    u32 size = 0;
    if (!ocbl_fs_stat(v, path, &size) || size == 0)
        return 0;
    u8* dst = (u8*)(u64)want_base;
    u32 done = 0;
    while (done < size) {
        u32 chunk = size - done;
        if (chunk > OCBL_BOUNCE_SZ)
            chunk = OCBL_BOUNCE_SZ;
        u32 got = ocbl_fs_pread(v, path, done, BOUNCE, chunk);
        if (got < chunk)
            return 0;                   /* read fell short: abort */
        for (u32 i = 0; i < chunk; i++)
            dst[done + i] = BOUNCE[i];
        done += chunk;
    }
    return size;
}

/* ---- ELF64 loader ---- */
u64 ocbl_load_elf(const ocbl_volume_t* v, const char* path, u64 want_base,
                  u32* total_size)
{
    u32 fsize = 0;
    if (!ocbl_fs_stat(v, path, &fsize) || fsize < 64)
        return 0;
    if (ocbl_fs_pread(v, path, 0, BOUNCE, 64) < 64)
        return 0;
    if (BOUNCE[0] != 0x7F || BOUNCE[1] != 'E' || BOUNCE[2] != 'L' ||
        BOUNCE[3] != 'F' || BOUNCE[4] != 2 /* ELFCLASS64 */)
        return 0;
    u16 phentsize = rd16b(BOUNCE + EHDR_PHENT_OFF);
    u16 phnum = rd16b(BOUNCE + EHDR_PHNUM_OFF);
    if (phentsize != sizeof(elf64_phdr_t) || phnum == 0 || phnum > 32)
        return 0;
    u64 entry = rd64b(BOUNCE + EHDR_ENTRY_OFF);
    u64 phoff = rd64b(BOUNCE + EHDR_PHOFF_OFF);

    /* program headers through the bounce window, then stash the
     * compact copy (segment reads overwrite the window) */
    u32 ph_bytes = phnum * sizeof(elf64_phdr_t);
    if (phoff + ph_bytes > (u64)fsize)
        return 0;
    if (ocbl_fs_pread(v, path, (u32)phoff, BOUNCE, ph_bytes) < ph_bytes)
        return 0;
    elf64_phdr_t ph[32];
    for (u32 i = 0; i < phnum; i++) {
        const u8* src = BOUNCE + i * sizeof(elf64_phdr_t);
        ph[i].p_type = rd32b(src + 0);
        ph[i].p_flags = rd32b(src + 4);
        ph[i].p_offset = rd64b(src + 8);
        ph[i].p_vaddr = rd64b(src + 16);
        ph[i].p_paddr = rd64b(src + 24);
        ph[i].p_filesz = rd64b(src + 32);
        ph[i].p_memsz = rd64b(src + 40);
    }

    for (u32 i = 0; i < phnum; i++) {
        if (ph[i].p_type != 1 /* PT_LOAD */)
            continue;
        if (ph[i].p_memsz == 0)
            continue;
        u64 dst = ph[i].p_paddr;
        if (dst < want_base || dst >= 0x40000000ull)
            return 0;                   /* outside the identity window */
        u64 foff = ph[i].p_offset;
        u32 filesz = (u32)ph[i].p_filesz;
        if (foff + filesz > (u64)fsize)
            return 0;
        u8* dmem = (u8*)(u64)dst;
        u32 off = 0;
        while (off < filesz) {
            u32 chunk = filesz - off;
            if (chunk > OCBL_BOUNCE_SZ)
                chunk = OCBL_BOUNCE_SZ;
            if (ocbl_fs_pread(v, path, (u32)foff + off, BOUNCE, chunk) <
                chunk)
                return 0;
            for (u32 k = 0; k < chunk; k++)
                dmem[off + k] = BOUNCE[k];
            off += chunk;
        }
        for (u64 z = ph[i].p_filesz; z < ph[i].p_memsz; z++)
            dmem[z] = 0;                /* .bss tail */
    }
    if (total_size)
        *total_size = fsize;
    return entry;
}

/* ---- multiboot2 info builder (0x50000) ---- */
#define MBI ((u8*)OCBL_MBI_BASE)

static u32 mbi_off;

static void mbi_tag(u32 type, u32 size)
{
    u8* p = MBI + mbi_off;
    p[0] = (u8)type;
    p[1] = (u8)(type >> 8);
    p[2] = (u8)(type >> 16);
    p[3] = (u8)(type >> 24);
    p[4] = (u8)size;
    p[5] = (u8)(size >> 8);
    p[6] = (u8)(size >> 16);
    p[7] = (u8)(size >> 24);
    mbi_off += 8;
}

static void mbi_u32(u32 off, u32 v)
{
    u8* p = MBI + off;
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void mbi_u64(u32 off, u64 v)
{
    mbi_u32(off, (u32)v);
    mbi_u32(off + 4, (u32)(v >> 32));
}

static void mbi_str(u32 off, const char* s)
{
    u8* p = MBI + off;
    while (*s)
        *p++ = (u8)*s++;
    *p = 0;
}

static void mbi_pad(void)
{
    mbi_off = (mbi_off + 7) & ~7u;
}

static u32 lower_kb(void)
{
    /* classic GRUB semantics: KiB below the 640 KiB EBDA hole */
    const ocbl_mmap_t* mm = ocbl_mmap_get();
    for (u32 i = 0; i < mm->count; i++) {
        if (mm->entries[i].type != 1)
            continue;
        if (mm->entries[i].addr == 0 && mm->entries[i].len >= 0x9FC00)
            return 639;
    }
    return 639;
}

static u32 upper_kb(void)
{
    const ocbl_mmap_t* mm = ocbl_mmap_get();
    for (u32 i = 0; i < mm->count; i++) {
        u64 a = mm->entries[i].addr;
        u64 l = mm->entries[i].len;
        if (mm->entries[i].type != 1)
            continue;
        if (a <= 0x100000ull && a + l > 0x100000ull) {
            u64 kib = (a + l - 0x100000ull) >> 10;
            if (kib > 0xFFFF0000ull)
                kib = 0xFFFF0000ull;
            return (u32)kib;
        }
    }
    return 0;
}

u32 ocbl_build_mbi(const char* slot_cmdline, u64 self_start, u64 self_end,
                   const ocbl_fb_info_t* fb)
{
    mbi_off = 0;
    u32 hdr_at = mbi_off;
    mbi_off += 8;                       /* total_size + reserved */

    /* tag 1: command line (oc.slot=... for the OCOS entries) */
    {
        char cl[32];
        u32 o = 0;
        if (slot_cmdline) {
            static const char pre[] = "oc.slot=";
            for (u32 k = 0; pre[k]; k++)
                cl[o++] = pre[k];
            cl[o++] = slot_cmdline[0];
        }
        cl[o] = 0;
        u32 sz = 8 + o + 1;
        mbi_tag(1, sz);
        mbi_str(hdr_at + mbi_off, cl);
        mbi_off += o + 1;
        mbi_pad();
    }

    /* tag 2: boot loader name */
    {
        static const char name[] = "OCBL 3.0.0";
        u32 sz = 8 + sizeof(name);
        mbi_tag(2, sz);
        mbi_str(hdr_at + mbi_off, name);
        mbi_off += sizeof(name);
        mbi_pad();
    }

    /* tag 3: the "self" module (kernel ELF copy at 0x800000) */
    if (self_end > self_start) {
        u32 sz = 8 + 8 + 5;             /* hdr + start/end + "self\0" */
        mbi_tag(3, sz);
        mbi_u32(hdr_at + mbi_off, (u32)self_start);
        mbi_u32(hdr_at + mbi_off + 4, (u32)self_end);
        mbi_str(hdr_at + mbi_off + 8, "self");
        mbi_off += 13;                  /* sz-8: the header advanced */
        mbi_pad();
    }

    /* tag 4: basic meminfo */
    {
        mbi_tag(4, 16);
        mbi_u32(hdr_at + mbi_off, lower_kb());
        mbi_u32(hdr_at + mbi_off + 4, upper_kb());
        mbi_off += 8;                   /* sz-8 */
        mbi_pad();
    }

    /* tag 6: memory map.  GRUB-compatible 24-byte entries:
     * [addr8][len8][type4][reserved4] with entry_size=24 - the shape
     * the kernel's mem_pmm has always consumed from GRUB (its
     * sizeof-check is 24B and the type field sits at entry+16). */
    {
        const ocbl_mmap_t* mm = ocbl_mmap_get();
        u32 n = 0;
        for (u32 i = 0; i < mm->count; i++)
            if (mm->entries[i].len)
                n++;
        u32 sz = 8 + 8 + n * 24;
        mbi_tag(6, sz);
        mbi_u32(hdr_at + mbi_off, 24);       /* entry_size */
        mbi_u32(hdr_at + mbi_off + 4, 0);    /* entry_version */
        u32 w = hdr_at + mbi_off + 8;
        for (u32 i = 0; i < mm->count; i++) {
            if (mm->entries[i].len == 0)
                continue;
            mbi_u64(w, mm->entries[i].addr);
            mbi_u64(w + 8, mm->entries[i].len);
            mbi_u32(w + 16, mm->entries[i].type);
            mbi_u32(w + 20, 0);              /* reserved */
            w += 24;
        }
        mbi_off = w - hdr_at;
        mbi_pad();
    }

    /* tag 8: framebuffer.  40-byte shape matching what GRUB emits and
     * what the kernel's fb_tag_t (with its u16 reserved slot) reads:
     * [addr8][pitch4][w4][h4][bpp1][type1][rsvd2][red_pos1][red_size1]
     * [green_pos1][green_size1][blue_pos1][blue_size1] */
    if (fb) {
        u32 sz = 8 + 32;
        mbi_tag(8, sz);
        mbi_u64(hdr_at + mbi_off, fb->addr);
        mbi_u32(hdr_at + mbi_off + 8, fb->pitch);
        mbi_u32(hdr_at + mbi_off + 12, fb->width);
        mbi_u32(hdr_at + mbi_off + 16, fb->height);
        MBI[hdr_at + mbi_off + 20] = fb->bpp;
        MBI[hdr_at + mbi_off + 21] = 1;      /* RGB */
        MBI[hdr_at + mbi_off + 22] = 0;      /* reserved0 */
        MBI[hdr_at + mbi_off + 23] = 0;
        MBI[hdr_at + mbi_off + 24] = fb->red_pos;
        MBI[hdr_at + mbi_off + 25] = fb->red_size;
        MBI[hdr_at + mbi_off + 26] = fb->green_pos;
        MBI[hdr_at + mbi_off + 27] = fb->green_size;
        MBI[hdr_at + mbi_off + 28] = fb->blue_pos;
        MBI[hdr_at + mbi_off + 29] = fb->blue_size;
        mbi_off += 32;                  /* sz-8 */
        mbi_pad();
    }

    /* tag 15/14: ACPI RSDP copy */
    {
        u64 rsdp = ocbl_rsdp_find();
        if (rsdp) {
            u8* rp = (u8*)(u64)rsdp;
            u8 rev = rp[15];
            if (rev >= 2) {
                mbi_tag(15, 8 + 36);
                for (u32 i = 0; i < 36; i++)
                    MBI[hdr_at + mbi_off + i] = rp[i];
                mbi_off += 36;
                mbi_pad();
            } else {
                mbi_tag(14, 8 + 20);
                for (u32 i = 0; i < 20; i++)
                    MBI[hdr_at + mbi_off + i] = rp[i];
                mbi_off += 20;
                mbi_pad();
            }
        }
    }

    /* tag 0: end */
    mbi_tag(0, 8);
    mbi_u32(hdr_at, mbi_off);           /* total_size */
    return mbi_off;
}
