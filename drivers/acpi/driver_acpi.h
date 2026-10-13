/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi.h
 * Purpose: ACPI core - table discovery + parsing (RSDP, RSDT, XSDT,
 *          FADT, DSDT, SSDT, FACS, MCFG, HPET, BGRT) and the small
 *          generic-address helpers the rest of the chipset code uses.
 *
 * Sources of the RSDP (first one wins):
 *   1. Multiboot2 tag 15 (ACPI 2.0+ RSDP copy)  - preferred
 *   2. Multiboot2 tag 14 (ACPI 1.x RSDP copy)
 *   3. EBDA scan (0x40E<<4, 1 KiB window)
 *   4. BIOS ROM scan (0xE0000..0xFFFFF, 16-byte aligned)
 *
 * The kernel runs with the first 4 GiB identity mapped, so every ACPI
 * table below 4 GiB (all of them, in practice) is dereferenceable
 * directly through its physical address. The parser validates every
 * table header (signature, length, checksum) before caching it.
 *
 * All names/strings are ASCII by project rule.
 */
#ifndef OC_DRIVER_ACPI_H
#define OC_DRIVER_ACPI_H

#include "types.h"

/* ------------------------------------------------------------------ */
/* ACPI structures (ACPI 6.x layout; fields we consume are stable)     */
/* ------------------------------------------------------------------ */

/* Root System Description Pointer. */
typedef struct acpi_rsdp {
    char     signature[8];   /* "RSD PTR " */
    u8       checksum;       /* sum of the first 20 bytes == 0 */
    char     oem_id[6];
    u8       revision;       /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    u32      rsdt_phys;      /* 32-bit RSDT address */
    u32      length;         /* 36 when revision >= 2 */
    u64      xsdt_phys;      /* 64-bit XSDT address (revision >= 2) */
    u8       ext_checksum;   /* sum of all 36 bytes == 0 */
    u8       reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

/* Common table header (all SDTs). */
typedef struct acpi_sdt_hdr {
    char     signature[4];
    u32      length;
    u8       revision;
    u8       checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    u32      oem_revision;
    u32      creator_id;
    u32      creator_revision;
} __attribute__((packed)) acpi_sdt_hdr_t;

/* Generic Address Structure. */
typedef struct acpi_gas {
    u8       address_space;  /* 0=SystemMemory 1=SystemIO 2=PCI_Config */
    u8       bit_width;
    u8       bit_offset;
    u8       access_size;    /* 0=undefined,1=byte,2=word,3=dword,4=qword */
    u64      address;
} __attribute__((packed)) acpi_gas_t;

/* Fixed ACPI Description Table (the fields we consume; mid padding is
 * spelled out so offsets match the spec exactly). */
typedef struct acpi_fadt {
    acpi_sdt_hdr_t hdr;
    u32     firmware_ctrl;      /* phys FACS */
    u32     dsdt;               /* phys DSDT */
    u8      reserved0;
    u8      preferred_pm_profile;
    u16     sci_int;
    u32     smi_cmd;
    u8      acpi_enable;
    u8      acpi_disable;
    u8      s4bios_req;
    u8      pstate_cnt;
    u32     pm1a_evt_blk;
    u32     pm1b_evt_blk;
    u32     pm1a_cnt_blk;
    u32     pm1b_cnt_blk;
    u32     pm2_cnt_blk;
    u32     pm_tmr_blk;
    u32     gpe0_blk;
    u32     gpe1_blk;
    u8      pm1_evt_len;
    u8      pm1_cnt_len;
    u8      pm2_cnt_len;
    u8      pm_tmr_len;
    u8      gpe0_blk_len;
    u8      gpe1_blk_len;
    u8      gpe1_base;
    u8      cst_cnt;
    u16     p_lvl2_lat;
    u16     p_lvl3_lat;
    u16     flush_size;
    u16     flush_stride;
    u8      duty_offset;
    u8      duty_width;
    u8      day_alrm;
    u8      mon_alrm;
    u8      century;
    u16     iapc_boot_arch;
    u8      reserved1;
    u32     flags;
    acpi_gas_t reset_reg;
    u8      reset_value;
    u8      reserved2[3];
    u64     x_firmware_ctrl;
    u64     x_dsdt;
    acpi_gas_t x_pm1a_evt;
    acpi_gas_t x_pm1b_evt;
    acpi_gas_t x_pm1a_cnt;
    acpi_gas_t x_pm1b_cnt;
    acpi_gas_t x_pm2_cnt;
    acpi_gas_t x_pm_tmr;
    acpi_gas_t x_gpe0_blk;
    acpi_gas_t x_gpe1_blk;
    acpi_gas_t sleep_control;   /* FADT rev >= 5 */
    acpi_gas_t sleep_status;    /* FADT rev >= 5 */
    u64     hypervisor_vendor_id;
} __attribute__((packed)) acpi_fadt_t;

/* Fixed Feature ACPI Control Structure. */
typedef struct acpi_facs {
    char     signature[4];   /* "FACS" */
    u32      length;
    u32      hardware_signature;
    u32      firmware_waking_vector;   /* 32-bit waking vector (phys) */
    u32      global_lock;
    u32      flags;
    u64      x_firmware_waking_vector; /* 64-bit waking vector (phys), rev 1+ */
    u8       version;                  /* rev 1+ */
    u8       reserved[33];
    u32      ospm_flags;
} __attribute__((packed)) acpi_facs_t;

/* MCFG entry (after the 44-byte header: reserved[8] then entries). */
typedef struct acpi_mcfg_entry {
    u64      base_addr;      /* MMCONFIG ECAM base */
    u16      pci_segment;
    u8       bus_start;
    u8       bus_end;
    u32      reserved;
} __attribute__((packed)) acpi_mcfg_entry_t;

/* HPET table (after header). */
typedef struct acpi_hpet {
    u8      hardware_rev_id;
    u8      comparator_count;
    u8      counter_size;
    u8      reserved0;
    u8      legacy_replacement;
    u8      pci_vendor_id[2];
    acpi_gas_t base_address;
    u8      hpet_number;
    u16     minimum_tick;
    u8      page_protection;
} __attribute__((packed)) acpi_hpet_t;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* Initialize ACPI from a RSDP physical address (0 = scan EBDA/BIOS).
 * Walks RSDT/XSDT, caches every table, parses FADT/FACS/MCFG/HPET.
 * Returns 0 when ACPI is usable, -1 when no valid RSDP was found. */
int driver_acpi_init(u64 rsdp_phys_hint);

/* 1 when ACPI tables are parsed and usable. */
int driver_acpi_available(void);

/* RSDP info. */
u64   driver_acpi_rsdp_phys(void);
int   driver_acpi_rsdp_revision(void);
const char* driver_acpi_oem_id(void);

/* Find the n-th (0-based) table with the given 4-char signature in the
 * RSDT/XSDT. Returns the physical address of the table (validated
 * header + checksum) or 0. */
u64   driver_acpi_find_table(const char *signature, int nth);

/* Number of cached table entries (RSDT/XSDT payload size). */
int   driver_acpi_table_count(void);

/* Table cache access: fills the four outputs of entry `index`.
 * Returns 0 on success, -1 when out of range. */
int   driver_acpi_table_info(int index, char sig_out[5],
                             u32 *length_out, u8 *revision_out,
                             u64 *phys_out);

/* FADT access (NULL when no FADT). */
const acpi_fadt_t* driver_acpi_fadt(void);

/* 1 when the FADT carries the 64-bit X_ fields (revision >= 2 and a
 * length that covers them). On ACPI 1.0 firmware (QEMU SeaBIOS legacy
 * mode: RSDP rev 0, FADT rev 1 length 128) the X_ area is PAST the
 * table - reading it hands out garbage addresses, so every X_ use is
 * gated on this predicate and falls back to the 32-bit fields. */
int driver_acpi_fadt_is_extended(void);

/* DSDT / SSDT physical addresses (0 = absent). */
u64   driver_acpi_dsdt_phys(void);
u64   driver_acpi_ssdt_phys(int nth);
int   driver_acpi_ssdt_count(void);

/* FACS physical address (0 = absent). */
u64   driver_acpi_facs_phys(void);
const acpi_facs_t* driver_acpi_facs(void);

/* MCFG: 1 when an MCFG table exists. MMCONFIG base for segment 0
 * (0 when MCFG is absent). */
int   driver_acpi_mcfg_present(void);
u64   driver_acpi_mmconfig_base(u16 segment);
int   driver_acpi_mmconfig_bus_range(u16 segment, u8 *start, u8 *end);

/* HPET table (NULL when absent). */
const acpi_hpet_t* driver_acpi_hpet(void);

/* ---- Generic address (GAS) read/write ------------------------------
 * Supports SystemMemory (direct deref, identity map) and SystemIO
 * (in/out). PCI_Config routes through the legacy CF8/CFC mechanism
 * (bus/dev/func packed in the GAS address per the PCI fg spec).
 * width selects the access width in bytes (1/2/4/8).
 * Returns the value read; writes return nothing. */
u64   driver_acpi_gas_read(const acpi_gas_t *gas, int width);
void  driver_acpi_gas_write(const acpi_gas_t *gas, int width, u64 value);

/* True when the GAS is readable/usable by our accessors. */
int   driver_acpi_gas_usable(const acpi_gas_t *gas);

/* Table header checksum validation helper (sum of `length` bytes == 0). */
int   driver_acpi_checksum_ok(const u8 *table, u32 length);

#endif /* OC_DRIVER_ACPI_H */
