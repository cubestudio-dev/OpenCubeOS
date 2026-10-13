/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi.c
 * Purpose: ACPI core implementation - RSDP discovery, RSDT/XSDT walk,
 *          table cache, FADT/FACS/MCFG/HPET parsing, GAS accessors.
 *
 * Design notes:
 *  - The boot identity map covers the first 4 GiB, so ACPI structures
 *    are reached through their physical addresses directly. Nothing
 *    here maps or unmaps pages.
 *  - Every table is checksum-validated before being cached; a bad
 *    table is skipped with a boot log line (never trusted).
 *  - All state lives in static storage; no kmalloc at init time so
 *    the ACPI core can start before the heap if ever needed.
 */
#include "driver_acpi.h"
#include "lib_string.h"
#include "lib_log.h"
#include "driver_pci.h"

#include <stdint.h>

/* ---- port I/O (self-contained) ---- */
static inline void acpi_outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   acpi_inb(u16 p)         { u8 v;  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void acpi_outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16  acpi_inw(u16 p)         { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void acpi_outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u32  acpi_inl(u16 p)         { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* 16-bit physical read via a register (the 0x40E EBDA pointer lives at
 * a constant low address; a register-indirect load keeps GCC from
 * flagging the deref as out-of-bounds). */
static inline u16 acpi_phys_read16(uintptr_t a) {
    u16 v;
    __asm__ volatile("movw (%1), %w0" : "=r"(v) : "r"(a));
    return v;
}

/* ---- cached state ---- */
#define ACPI_MAX_TABLES 48

typedef struct {
    u64  phys;
    u32  length;
    u8   revision;
    char signature[5];   /* NUL-padded */
} acpi_table_entry_t;

static acpi_table_entry_t g_tables[ACPI_MAX_TABLES];
static int                g_table_count;
static const acpi_rsdp_t *g_rsdp;
static u64                g_rsdp_phys;
static const acpi_fadt_t *g_fadt;
static u64                g_dsdt_phys;
static u64                g_ssdt_phys[8];
static int                g_ssdt_count;
static const acpi_facs_t *g_facs;
static u64                g_facs_phys;
static u64                g_mmcfg_base;
static u8                 g_mmcfg_bus_start, g_mmcfg_bus_end;
static u16                g_mmcfg_segment;
static int                g_mmcfg_present;
static const acpi_hpet_t *g_hpet;
static int                g_available;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

int driver_acpi_checksum_ok(const u8 *table, u32 length) {
    if (!table || length < 1) return 0;
    u8 sum = 0;
    for (u32 i = 0; i < length; i++) sum = (u8)(sum + table[i]);
    return sum == 0;
}

/* 4-char signature compare. */
static int sig_eq(const char *a, const char *b) {
    for (int i = 0; i < 4; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Cache one table after validating its header + checksum. */
static void acpi_cache_table(u64 phys) {
    if (g_table_count >= ACPI_MAX_TABLES) return;
    const acpi_sdt_hdr_t *h = (const acpi_sdt_hdr_t*)(uintptr_t)phys;
    if (!h) return;
    if (h->length < sizeof(acpi_sdt_hdr_t) || h->length > (1u << 20)) {
        OC_LOG_FAIL("ACPI: table with bad length skipped");
        return;
    }
    if (!driver_acpi_checksum_ok((const u8*)h, h->length)) {
        OC_LOG_FAIL("ACPI: table checksum mismatch skipped");
        return;
    }
    acpi_table_entry_t *e = &g_tables[g_table_count];
    e->phys     = phys;
    e->length   = h->length;
    e->revision = h->revision;
    for (int i = 0; i < 4; i++) e->signature[i] = h->signature[i];
    e->signature[4] = 0;
    g_table_count++;
}

/* ------------------------------------------------------------------ */
/* RSDP discovery                                                      */
/* ------------------------------------------------------------------ */

static int rsdp_valid(const acpi_rsdp_t *r) {
    if (!r) return 0;
    const char sig[9] = { 'R','S','D',' ','P','T','R',' ', 0 };
    for (int i = 0; i < 8; i++) if (r->signature[i] != sig[i]) return 0;
    if (!driver_acpi_checksum_ok((const u8*)r, 20)) return 0;
    if (r->revision >= 2) {
        if (r->length < 36) return 0;
        if (!driver_acpi_checksum_ok((const u8*)r, r->length)) return 0;
    }
    return 1;
}

static u64 acpi_scan_rsdp(u64 hint) {
    if (hint && rsdp_valid((const acpi_rsdp_t*)(uintptr_t)hint)) return hint;
    /* EBDA: word at 0x40E is the segment; scan the first KiB. */
    u16 ebda_seg = acpi_phys_read16(0x40E);
    u64 ebda = (u64)ebda_seg << 4;
    if (ebda && ebda < 0x100000) {
        for (u64 p = ebda; p + sizeof(acpi_rsdp_t) <= ebda + 1024; p += 16) {
            if (rsdp_valid((const acpi_rsdp_t*)(uintptr_t)p)) return p;
        }
    }
    /* BIOS ROM area. */
    for (u64 p = 0xE0000; p + sizeof(acpi_rsdp_t) <= 0x100000; p += 16) {
        if (rsdp_valid((const acpi_rsdp_t*)(uintptr_t)p)) return p;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

int driver_acpi_init(u64 rsdp_phys_hint) {
    g_table_count = 0;
    g_available   = 0;
    g_mmcfg_present = 0;

    u64 rp = acpi_scan_rsdp(rsdp_phys_hint);
    if (!rp) {
        OC_LOG_FAIL("ACPI: no valid RSDP found");
        return -1;
    }
    g_rsdp_phys = rp;
    g_rsdp      = (const acpi_rsdp_t*)(uintptr_t)rp;

    /* Walk the root table: XSDT (ACPI 2.0+) or RSDT. */
    u64 root_phys = 0;
    int use_xsdt  = 0;
    if (g_rsdp->revision >= 2 && g_rsdp->xsdt_phys) {
        const acpi_sdt_hdr_t *x = (const acpi_sdt_hdr_t*)(uintptr_t)g_rsdp->xsdt_phys;
        if (x && sig_eq(x->signature, "XSDT") &&
            driver_acpi_checksum_ok((const u8*)x, x->length)) {
            root_phys = g_rsdp->xsdt_phys;
            use_xsdt  = 1;
        }
    }
    if (!root_phys && g_rsdp->rsdt_phys) {
        const acpi_sdt_hdr_t *r = (const acpi_sdt_hdr_t*)(uintptr_t)g_rsdp->rsdt_phys;
        if (r && sig_eq(r->signature, "RSDT") &&
            driver_acpi_checksum_ok((const u8*)r, r->length)) {
            root_phys = g_rsdp->rsdt_phys;
            use_xsdt  = 0;
        }
    }
    if (!root_phys) {
        OC_LOG_FAIL("ACPI: RSDT/XSDT invalid");
        return -1;
    }

    /* The root SDT payload is an array of table pointers
     * (u32 for RSDT, u64 for XSDT) after the 36-byte header. */
    const acpi_sdt_hdr_t *root = (const acpi_sdt_hdr_t*)(uintptr_t)root_phys;
    u32 payload  = root->length - sizeof(acpi_sdt_hdr_t);
    u32 stride   = use_xsdt ? 8 : 4;
    u32 entries  = payload / stride;
    /* The RSDT/XSDT itself is entry 0 conceptually (the "root"); cache
     * it too so `acpi` shows it, then walk the payload. */
    acpi_cache_table(root_phys);
    for (u32 i = 0; i < entries && g_table_count < ACPI_MAX_TABLES; i++) {
        u64 tp;
        if (use_xsdt) tp = *(const u64*)(uintptr_t)(root_phys + sizeof(acpi_sdt_hdr_t) + i * 8);
        else          tp = (u64)(*(const u32*)(uintptr_t)(root_phys + sizeof(acpi_sdt_hdr_t) + i * 4));
        if (tp) acpi_cache_table(tp);
    }

    /* Pick the important tables out of the cache. */
    for (int i = 0; i < g_table_count; i++) {
        if (sig_eq(g_tables[i].signature, "FACP") && !g_fadt) {
            g_fadt = (const acpi_fadt_t*)(uintptr_t)g_tables[i].phys;
        } else if (sig_eq(g_tables[i].signature, "DSDT") && !g_dsdt_phys) {
            g_dsdt_phys = g_tables[i].phys;
        } else if (sig_eq(g_tables[i].signature, "SSDT")) {
            if (g_ssdt_count < 8) g_ssdt_phys[g_ssdt_count++] = g_tables[i].phys;
        } else if (sig_eq(g_tables[i].signature, "FACS") && !g_facs) {
            g_facs      = (const acpi_facs_t*)(uintptr_t)g_tables[i].phys;
            g_facs_phys = g_tables[i].phys;
        } else if (sig_eq(g_tables[i].signature, "MCFG") && !g_mmcfg_present) {
            /* header: 36-byte SDT header + 8 reserved bytes; then entries. */
            u32 len = g_tables[i].length;
            if (len >= 44 + (u32)sizeof(acpi_mcfg_entry_t)) {
                const acpi_mcfg_entry_t *ent =
                    (const acpi_mcfg_entry_t*)(uintptr_t)(g_tables[i].phys + 44);
                int n = (int)((len - 44) / sizeof(acpi_mcfg_entry_t));
                /* Use the first segment-0 entry (or the first entry). */
                for (int k = 0; k < n; k++) {
                    if (ent[k].pci_segment == 0 || k == 0) {
                        g_mmcfg_present  = 1;
                        g_mmcfg_base     = ent[k].base_addr;
                        g_mmcfg_segment  = ent[k].pci_segment;
                        g_mmcfg_bus_start= ent[k].bus_start;
                        g_mmcfg_bus_end  = ent[k].bus_end;
                        if (ent[k].pci_segment == 0) break;
                    }
                }
            }
        } else if (sig_eq(g_tables[i].signature, "HPET") && !g_hpet) {
            g_hpet = (const acpi_hpet_t*)(uintptr_t)g_tables[i].phys;
        }
    }

    /* When the FADT names the DSDT/FACS, prefer those addresses. The
     * X_ fields only exist on ACPI 2.0+ (rev >= 2, length >= 244);
     * on a rev-1 FADT they are past the table end (garbage). */
    if (g_fadt) {
        u64 xdsdt = driver_acpi_fadt_is_extended() && g_fadt->x_dsdt
                    ? g_fadt->x_dsdt : (u64)g_fadt->dsdt;
        if (xdsdt && !g_dsdt_phys) g_dsdt_phys = xdsdt;
        u64 xfacs = driver_acpi_fadt_is_extended() && g_fadt->x_firmware_ctrl
                    ? g_fadt->x_firmware_ctrl : (u64)g_fadt->firmware_ctrl;
        if (xfacs && !g_facs) {
            g_facs      = (const acpi_facs_t*)(uintptr_t)xfacs;
            g_facs_phys = xfacs;
        }
    }

    g_available = 1;
    {
        char line[96]; char n[16];
        strcpy(line, "ACPI: RSDP rev ");
        u64_to_str((u64)g_rsdp->revision, n); strcat(line, n);
        strcat(line, use_xsdt ? " XSDT " : " RSDT ");
        u64_to_hex(root_phys, n, 8);
        strcat(line, n);
        strcat(line, " tables ");
        u64_to_str((u64)g_table_count, n); strcat(line, n);
        OC_LOG_OK(line);
    }
    if (g_fadt)     OC_LOG_OK("ACPI: FADT parsed (PM1a_CNT / GPE0 / SCI ready)");
    if (g_dsdt_phys) OC_LOG_OK("ACPI: DSDT located (AML interpreter can load)");
    if (g_mmcfg_present) OC_LOG_OK("ACPI: MCFG parsed (PCIe MMCONFIG available)");
    return 0;
}

/* ------------------------------------------------------------------ */
/* getters                                                             */
/* ------------------------------------------------------------------ */

int driver_acpi_available(void)        { return g_available; }
u64 driver_acpi_rsdp_phys(void)        { return g_rsdp_phys; }
int driver_acpi_rsdp_revision(void)    { return g_rsdp ? g_rsdp->revision : 0; }
const char* driver_acpi_oem_id(void)   { return g_rsdp ? g_rsdp->oem_id : ""; }

int driver_acpi_table_count(void)      { return g_table_count; }

int driver_acpi_table_info(int index, char sig_out[5],
                           u32 *length_out, u8 *revision_out,
                           u64 *phys_out) {
    if (index < 0 || index >= g_table_count) return -1;
    const acpi_table_entry_t *e = &g_tables[index];
    for (int i = 0; i < 5; i++) sig_out[i] = e->signature[i];
    sig_out[4] = 0;
    if (length_out)   *length_out   = e->length;
    if (revision_out) *revision_out = e->revision;
    if (phys_out)     *phys_out     = e->phys;
    return 0;
}

u64 driver_acpi_find_table(const char *signature, int nth) {
    int seen = 0;
    for (int i = 0; i < g_table_count; i++) {
        if (sig_eq(g_tables[i].signature, signature)) {
            if (seen == nth) return g_tables[i].phys;
            seen++;
        }
    }
    return 0;
}

const acpi_fadt_t* driver_acpi_fadt(void) { return g_fadt; }

int driver_acpi_fadt_is_extended(void) {
    return g_fadt && g_fadt->hdr.revision >= 2 && g_fadt->hdr.length >= 244;
}

u64 driver_acpi_dsdt_phys(void) { return g_dsdt_phys; }

u64 driver_acpi_ssdt_phys(int nth) {
    if (nth < 0 || nth >= g_ssdt_count) return 0;
    return g_ssdt_phys[nth];
}
int driver_acpi_ssdt_count(void) { return g_ssdt_count; }

u64 driver_acpi_facs_phys(void) { return g_facs_phys; }
const acpi_facs_t* driver_acpi_facs(void) { return g_facs; }

int driver_acpi_mcfg_present(void) { return g_mmcfg_present; }

u64 driver_acpi_mmconfig_base(u16 segment) {
    if (!g_mmcfg_present || g_mmcfg_segment != segment) return 0;
    return g_mmcfg_base;
}

int driver_acpi_mmconfig_bus_range(u16 segment, u8 *start, u8 *end) {
    if (!g_mmcfg_present || g_mmcfg_segment != segment) return -1;
    if (start) *start = g_mmcfg_bus_start;
    if (end)   *end   = g_mmcfg_bus_end;
    return 0;
}

const acpi_hpet_t* driver_acpi_hpet(void) { return g_hpet; }

/* ------------------------------------------------------------------ */
/* GAS accessors                                                       */
/* ------------------------------------------------------------------ */

int driver_acpi_gas_usable(const acpi_gas_t *gas) {
    if (!gas || gas->address == 0) return 0;
    switch (gas->address_space) {
    case 0:  /* SystemMemory: identity map covers the first 4 GiB */
        return gas->address < 0x100000000ull;
    case 1:  /* SystemIO */
        return gas->address < 0x10000ull;
    case 2:  /* PCI_Config: legacy CF8/CFC (bus<256, dev<32, fn<8) */
        return 1;
    default:
        return 0;
    }
}

u64 driver_acpi_gas_read(const acpi_gas_t *gas, int width) {
    if (!driver_acpi_gas_usable(gas)) return 0;
    switch (gas->address_space) {
    case 0: {
        volatile u64 *p = (volatile u64*)(uintptr_t)(gas->address + (gas->bit_offset >> 3));
        switch (width) {
        case 1: return *(volatile u8 *)(uintptr_t)p;
        case 2: return *(volatile u16*)(uintptr_t)p;
        case 4: return *(volatile u32*)(uintptr_t)p;
        case 8: return *(volatile u64*)(uintptr_t)p;
        default: return 0;
        }
    }
    case 1: {
        u16 port = (u16)(gas->address + (gas->bit_offset >> 3));
        switch (width) {
        case 1: return acpi_inb(port);
        case 2: return acpi_inw(port);
        case 4: return acpi_inl(port);
        default: return 0;
        }
    }
    case 2: {
        /* PCI_Config GAS: address = (bus << 20) | (dev << 15) |
         * (fn << 12) | offset (per ACPI spec PCI fg encoding). */
        u32 a = (u32)gas->address;
        u8  bus = (u8)((a >> 20) & 0xFF);
        u8  dev = (u8)((a >> 15) & 0x1F);
        u8  fn  = (u8)((a >> 12) & 0x07);
        u8  off = (u8)(a & 0xFF);
        return driver_pci_read_config(bus, dev, fn, off);
    }
    default:
        return 0;
    }
}

void driver_acpi_gas_write(const acpi_gas_t *gas, int width, u64 value) {
    if (!driver_acpi_gas_usable(gas)) return;
    switch (gas->address_space) {
    case 0: {
        volatile u64 *p = (volatile u64*)(uintptr_t)(gas->address + (gas->bit_offset >> 3));
        switch (width) {
        case 1: *(volatile u8 *)(uintptr_t)p = (u8)value;  break;
        case 2: *(volatile u16*)(uintptr_t)p = (u16)value; break;
        case 4: *(volatile u32*)(uintptr_t)p = (u32)value; break;
        case 8: *(volatile u64*)(uintptr_t)p = value;      break;
        default: break;
        }
        break;
    }
    case 1: {
        u16 port = (u16)(gas->address + (gas->bit_offset >> 3));
        switch (width) {
        case 1: acpi_outb(port, (u8)value);  break;
        case 2: acpi_outw(port, (u16)value); break;
        case 4: acpi_outl(port, (u32)value); break;
        default: break;
        }
        break;
    }
    case 2: {
        u32 a = (u32)gas->address;
        u8  bus = (u8)((a >> 20) & 0xFF);
        u8  dev = (u8)((a >> 15) & 0x1F);
        u8  fn  = (u8)((a >> 12) & 0x07);
        u8  off = (u8)(a & 0xFF);
        driver_pci_write_config(bus, dev, fn, off, (u32)value);
        break;
    }
    default:
        break;
    }
}
