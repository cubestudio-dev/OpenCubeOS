/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_acpi.c
 * Purpose: Minimal ACPI support for OCIRE.
 *
 * This is an independent implementation (OCIRE shares no code with the
 * OCOS ACPI driver); it covers exactly what a recovery kernel needs:
 *   RSDP (EBDA + legacy BIOS area, checksum-verified)
 *   -> XSDT (checksum-verified) -> FADT -> DSDT (checksum-verified)
 *   -> \_S5 package SLP_TYPa extraction (tiny AML package reader)
 *   -> PM1a_CNT S5 entry, FADT RESET_REG reset.
 */

#include "ocire_acpi.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_console.h"

/* ---- ACPI table layouts (packed by hand, offsets per the spec) ------ */

typedef struct {
    char     signature[8];   /* "RSD PTR " */
    u8       checksum;
    char     oem_id[6];
    u8       revision;
    u32      rsdt_addr;      /* ACPI 1.0 */
    u32      length;         /* ACPI 2.0+ */
    u64      xsdt_addr;      /* ACPI 2.0+ */
    u8       ext_checksum;
    u8       reserved[3];
} rsdp_t;

typedef struct {
    char     signature[4];
    u32      length;
    u8       revision;
    u8       checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    u32      oem_revision;
    char     creator_id[4];
    u32      creator_revision;
} acpi_hdr_t;

/* FADT fields we use (offsets from table start, ACPI 1.0+ layout). */
#define FADT_OFF_DSDT       40      /* u32 physical DSDT (rev 1)   */
#define FADT_OFF_PM1A_CNT  64      /* u32 PM1a control block port */
#define FADT_OFF_RESET_REG 148      /* 12-byte generic address struct */
#define FADT_OFF_RESET_VAL 156      /* u8 value for the reset reg  */

typedef struct {
    u8  address_space;   /* 0 = system memory, 1 = system I/O */
    u8  bit_width;
    u8  bit_offset;
    u8  reserved;
    u64 address;
} gas_t;

/* ---- state ------------------------------------------------------------ */

static acpi_hdr_t *g_fadt;
static u32        g_pm1a_cnt;
static int        g_slp_typ = -1;
static gas_t      g_reset_reg;
static u8         g_reset_val;

/* ---- helpers ---------------------------------------------------------- */

static int acpi_table_ok(const void *table, u32 len_limit) {
    const acpi_hdr_t *h = (const acpi_hdr_t *)table;
    if (!table || h->length < sizeof(acpi_hdr_t)) return 0;
    if (h->length > len_limit && len_limit) return 0;
    /* The table checksum covers the whole table and must sum to 0. */
    const u8 *p = (const u8 *)table;
    u32 len = h->length;
    if (len < 36 || len > 0x40000) return 0;
    u32 sum = 0;
    for (u32 i = 0; i < len; i++) sum += p[i];
    return (sum & 0xFF) == 0;
}

/* ---- RSDP / XSDT / FADT ----------------------------------------------- */

/* Absolute physical addresses (0x40E BDA word, EBDA, ROM area) are read
 * through asm so the compiler's array-bounds analysis cannot complain
 * about literal-address pointers. */
static u16 read_phys16(u64 addr) {
    u32 v;
    __asm__ volatile("movzwl (%1), %0" : "=r"(v) : "r"(addr) : "memory");
    return (u16)v;
}

static rsdp_t *find_rsdp(void) {
    /* 1) EBDA: the word at 0x40E points at the extended BIOS data area. */
    u32 ebda = ((u32)read_phys16(0x40E)) << 4;
    if (ebda >= 0x400 && ebda < 0x9FC00) {
        for (u32 p = ebda & ~0xFu; p + sizeof(rsdp_t) < 0x9FC00; p += 16) {
            if (oc_memcmp((const void *)(uintptr)p, "RSD PTR ", 8) == 0)
                return (rsdp_t *)(uintptr)p;
        }
    }
    /* 2) Legacy BIOS ROM area 0xE0000..0xFFFFF. */
    for (u32 p = 0xE0000; p + sizeof(rsdp_t) <= 0xFFFFF; p += 16) {
        if (oc_memcmp((const void *)(uintptr)p, "RSD PTR ", 8) == 0)
            return (rsdp_t *)(uintptr)p;
    }
    return NULL;
}

static int rsdp_ok(const rsdp_t *r) {
    const u8 *p = (const u8 *)r;
    u32 len = (r->revision >= 2) ? r->length : 20;
    if (len < 20) return 0;
    u32 sum = 0;
    for (u32 i = 0; i < len; i++) sum += p[i];
    return (sum & 0xFF) == 0;
}

/* ---- DSDT \_S5 package parsing ----------------------------------------
 *
 * The DSDT contains (QEMU/OVMF and mainstream firmwares alike):
 *     Name (_S5, Package (2) { <SLP_TYPa>, <SLP_TYPb> })
 * encoded as:
 *     08 5F 53 35 5F 12 <pkglen> <num> <element0> <element1>
 *     ^NameOp  "_S5_"   ^PackageOp         ^ZeroOp/BytePrefix..
 * The tiny reader below skips the package length (variable encoding)
 * and decodes element 0 (ZeroOp 0x00, BytePrefix 0x0A or WordPrefix 0x0B).
 */

static int parse_s5_package(const u8 *p) {
    /* p points at the 4 bytes "_S5_"; the caller guarantees the
     * NameOp prefix is within the searched region. */
    if (p[-1] != 0x08) return -1;        /* must be a NameOp */
    u64 off = 4;                          /* skip "_S5_" */
    if (p[off] != 0x12) return -1;       /* PackageOp */
    off++;
    /* Package length: PkgLeadByte < 64 -> one byte total.
     * Otherwise the low 2 bits say how many extra bytes follow. */
    u8 lead = p[off++];
    if (lead & 0xC0) {
        u8 extra = (lead >> 6) & 0x3;
        off += extra;
    }
    /* Byte list count follows, then element 0. */
    off++;                                /* NumElements */
    /* Decode element 0 = SLP_TYPa. */
    u8 tag = p[off++];
    if (tag == 0x00) return 0;           /* ZeroOp */
    if (tag == 0x0A) return (int)(p[off] & 0x7F);   /* BytePrefix */
    if (tag == 0x0B) return (int)((u16)p[off] |
                                  ((u16)p[off + 1] << 8)); /* WordPrefix */
    return -1;
}

static void resolve_s5(void) {
    if (!g_fadt) return;
    /* ACPI 2.0+ carries the 64-bit X_DSDT at offset 140; every real
     * FADT also mirrors it into the 32-bit DSDT slot at offset 40.
     * Prefer the extended field when the table is long enough to
     * have it, fall back to the legacy slot otherwise. */
    u64 dsdt = 0;
    if (g_fadt->revision >= 3 && g_fadt->length >= 148)
        dsdt = *(const u64 *)((const u8 *)g_fadt + 140);
    if (!dsdt)
        dsdt = *(const u32 *)((const u8 *)g_fadt + FADT_OFF_DSDT);
    if (dsdt < 0x1000 || dsdt >= 0xFFFFFFFFull) return;
    acpi_hdr_t *d = (acpi_hdr_t *)dsdt;
    if (oc_memcmp(d->signature, "DSDT", 4) != 0) return;
    if (!acpi_table_ok(d, 0)) return;
    u64 hit = oc_memfind((const u8 *)d + 36, d->length - 36,
                         "_S5_", 4);
    if (hit == (u64)-1 || hit == 0) return;   /* hit 0: no NameOp prefix */
    g_slp_typ = parse_s5_package((const u8 *)d + 36 + hit);
}

/* ---- public API -------------------------------------------------------- */

int ocire_acpi_init(void) {
    g_fadt = NULL;
    g_slp_typ = -1;

    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) return -1;
    if (!rsdp_ok(rsdp)) return -1;

    /* ACPI 2.0+ -> XSDT; ACPI 1.0 -> RSDT (32-bit entries). */
    acpi_hdr_t *xsdt = NULL;
    u32 rsdt = rsdp->rsdt_addr;
    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        xsdt = (acpi_hdr_t *)(uintptr)rsdp->xsdt_addr;
        if (!acpi_table_ok(xsdt, 0)) xsdt = NULL;
    }

    if (xsdt) {
        u64 entries = (xsdt->length - sizeof(acpi_hdr_t)) / 8;
        for (u64 i = 0; i < entries && i < 256; i++) {
            u64 sub = *(const u64 *)((const u8 *)xsdt +
                                     sizeof(acpi_hdr_t) + i * 8);
            if (!sub) continue;
            acpi_hdr_t *t = (acpi_hdr_t *)(uintptr)sub;
            if (oc_memcmp(t->signature, "FACP", 4) == 0) {
                if (acpi_table_ok(t, 0)) g_fadt = t;
                break;
            }
        }
    } else if (rsdt) {
        acpi_hdr_t *rt = (acpi_hdr_t *)(uintptr)rsdt;
        if (acpi_table_ok(rt, 0)) {
            u32 entries = (rt->length - sizeof(acpi_hdr_t)) / 4;
            for (u32 i = 0; i < entries && i < 256; i++) {
                u32 sub = *(const u32 *)((const u8 *)rt +
                                         sizeof(acpi_hdr_t) + i * 4);
                if (!sub) continue;
                acpi_hdr_t *t = (acpi_hdr_t *)(uintptr)sub;
                if (oc_memcmp(t->signature, "FACP", 4) == 0) {
                    if (acpi_table_ok(t, 0)) g_fadt = t;
                    break;
                }
            }
        }
    }

    if (!g_fadt) return -1;

    g_pm1a_cnt = *(const u32 *)((const u8 *)g_fadt + FADT_OFF_PM1A_CNT);
    oc_memset(&g_reset_reg, 0, sizeof(g_reset_reg));
    g_reset_val = 0;
    /* The RESET_REG / RESET_VALUE fields only exist in FADT revision 3+
     * (table length covers offsets up to 158).  Guard the read so a
     * short ACPI 1.0 FADT cannot make us read past the table. */
    if (g_fadt->revision >= 3 && g_fadt->length >= (FADT_OFF_RESET_VAL + 1)) {
        oc_memcpy(&g_reset_reg,
                  (const u8 *)g_fadt + FADT_OFF_RESET_REG, sizeof(gas_t));
        g_reset_val = *(const u8 *)((const u8 *)g_fadt + FADT_OFF_RESET_VAL);
    }

    resolve_s5();
    return 0;
}

int ocire_acpi_available(void) { return g_fadt ? 0 : -1; }

int ocire_acpi_slp_typ_s5(void) { return g_slp_typ; }

int ocire_acpi_enter_s5(void) {
    if (!g_fadt) return -1;
    if (!g_pm1a_cnt || g_slp_typ < 0) return -1;
    /* PM1a_CNT: SLP_TYP in bits 10..12, SLP_EN is bit 13. */
    u16 val = (u16)(((u16)g_slp_typ << 10) | (1u << 13));
    oc_outw((u16)g_pm1a_cnt, val);
    /* Real hardware also mirrors to PM1b when present; QEMU does not
     * expose one.  Spin briefly so the caller can fall back if the
     * port write did not take. */
    for (volatile int i = 0; i < 1000000; i++) { }
    return -1;
}

int ocire_acpi_reset(void) {
    if (!g_fadt) return -1;
    if (g_reset_reg.address_space != 1) return -1;   /* I/O space only */
    if (g_reset_reg.address == 0 || g_reset_reg.address > 0xFFFF) return -1;
    /* PCI reset register convention: write the value the FADT names. */
    oc_outb((u16)g_reset_reg.address, g_reset_val);
    for (volatile int i = 0; i < 1000000; i++) { }
    return -1;
}
