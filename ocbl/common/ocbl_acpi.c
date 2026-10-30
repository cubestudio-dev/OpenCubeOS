/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_acpi.c
 * Purpose: ACPI table walking + the S5 power-off path (B4b wires the
 *          actual SLP_TYP write).  Firmware-agnostic since B5-2: the
 *          RSDP POINTER discovery moved to the flavors (BIOS scans
 *          the EBDA/0xE0000 in bios/ocbl_rsdp_bios.c, UEFI reads the
 *          EFI configuration table); this unit only consumes it.
 */
#include "ocbl_boot.h"
#include "ocbl_types.h"

static u64 rd64_tbl(const volatile u8* p)
{
    u64 v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

/* ---- ACPI tables (FACP walk + DSDT _S5) ------------------------------ */

typedef struct {
    u8  sig[4];
    u32 length;
    u8  rev;
    u8  checksum;
    u8  oem_id[6];
    u8  oem_table_id[8];
    u32 oem_rev;
    u32 creator_id;
    u32 creator_rev;
} acpi_hdr_t;

#define FADT_OFF_DSDT       40
#define FADT_OFF_PM1A_CNT   64

static int table_ok(const volatile u8* t)
{
    u32 len = (u32)t[4] | ((u32)t[5] << 8) |
              ((u32)t[6] << 16) | ((u32)t[7] << 24);
    if (len < 36 || len > 0x40000)
        return 0;
    u32 sum = 0;
    for (u32 i = 0; i < len; i++)
        sum += t[i];
    return (sum & 0xFF) == 0;
}

static int sig_eq(const volatile u8* t, const char* s)
{
    for (int i = 0; i < 4; i++)
        if (t[i] != (u8)s[i])
            return 0;
    return 1;
}

/* \_S5 package: 08 5F 53 35 5F 12 <pkglen> <num> <elem0> ... */
static int parse_s5(const volatile u8* p)
{
    if (p[-1] != 0x08)
        return -1;
    u32 off = 4;
    if (p[off] != 0x12)
        return -1;
    off++;
    u8 lead = p[off++];
    if (lead & 0xC0)
        off += (lead >> 6) & 0x3;
    off++;                                   /* NumElements */
    u8 tag = p[off++];
    if (tag == 0x00)
        return 0;                            /* ZeroOp */
    if (tag == 0x0A)
        return (int)(p[off] & 0x7F);
    if (tag == 0x0B)
        return (int)((u16)p[off] | ((u16)p[off + 1] << 8));
    return -1;
}

static u32 g_pm1a_cnt;
static int g_slp_typ = -1;

static void acpi_resolve_s5(void)
{
    u64 rsdp = ocbl_rsdp_find();
    if (!rsdp)
        return;
    volatile u8* rp = (volatile u8*)(u64)rsdp;
    u8 rev = rp[15];
    u64 fadt = 0;
    if (rev >= 2) {
        u64 xsdt = rd64_tbl(rp + 24);
        if (xsdt >= 0x1000) {
            volatile u8* x = (volatile u8*)(u64)xsdt;
            if (table_ok(x)) {
                u32 len = (u32)x[4] | ((u32)x[5] << 8) |
                          ((u32)x[6] << 16) | ((u32)x[7] << 24);
                u32 n = (len - 36) / 8;
                for (u32 i = 0; i < n && i < 256; i++) {
                    u64 sub = (u64)x[36 + i * 8] |
                              ((u64)x[36 + i * 8 + 1] << 8) |
                              ((u64)x[36 + i * 8 + 2] << 16) |
                              ((u64)x[36 + i * 8 + 3] << 24) |
                              ((u64)x[36 + i * 8 + 4] << 32) |
                              ((u64)x[36 + i * 8 + 5] << 40) |
                              ((u64)x[36 + i * 8 + 6] << 48) |
                              ((u64)x[36 + i * 8 + 7] << 56);
                    if (!sub)
                        continue;
                    volatile u8* t = (volatile u8*)(u64)sub;
                    if (sig_eq(t, "FACP") && table_ok(t)) {
                        fadt = sub;
                        break;
                    }
                }
            }
        }
    } else {
        u32 rsdt = (u32)rp[16] | ((u32)rp[17] << 8) |
                   ((u32)rp[18] << 16) | ((u32)rp[19] << 24);
        if (rsdt >= 0x1000) {
            volatile u8* rt = (volatile u8*)(u64)rsdt;
            if (table_ok(rt)) {
                u32 len = (u32)rt[4] | ((u32)rt[5] << 8) |
                          ((u32)rt[6] << 16) | ((u32)rt[7] << 24);
                u32 n = (len - 36) / 4;
                for (u32 i = 0; i < n && i < 256; i++) {
                    u32 sub = (u32)rt[36 + i * 4] |
                              ((u32)rt[36 + i * 4 + 1] << 8) |
                              ((u32)rt[36 + i * 4 + 2] << 16) |
                              ((u32)rt[36 + i * 4 + 3] << 24);
                    if (!sub)
                        continue;
                    volatile u8* t = (volatile u8*)(u64)sub;
                    if (sig_eq(t, "FACP") && table_ok(t)) {
                        fadt = sub;
                        break;
                    }
                }
            }
        }
    }
    if (!fadt)
        return;
    volatile u8* f = (volatile u8*)(u64)fadt;
    u32 fadt_len = (u32)f[4] | ((u32)f[5] << 8) |
                   ((u32)f[6] << 16) | ((u32)f[7] << 24);
    u8 fadt_rev = f[9];
    /* PM1a_CNT_BLK @64 */
    g_pm1a_cnt = (u32)f[64] | ((u32)f[65] << 8) |
                 ((u32)f[66] << 16) | ((u32)f[67] << 24);
    /* DSDT: X_DSDT @140 (rev 3+) else legacy @40 */
    u64 dsdt = 0;
    if (fadt_rev >= 3 && fadt_len >= 148)
        dsdt = (u64)f[140] | ((u64)f[141] << 8) |
               ((u64)f[142] << 16) | ((u64)f[143] << 24) |
               ((u64)f[144] << 32) | ((u64)f[145] << 40) |
               ((u64)f[146] << 48) | ((u64)f[147] << 56);
    if (!dsdt)
        dsdt = (u32)f[40] | ((u32)f[41] << 8) |
               ((u32)f[42] << 16) | ((u32)f[43] << 24);
    if (dsdt < 0x1000)
        return;
    volatile u8* d = (volatile u8*)(u64)dsdt;
    if (!sig_eq(d, "DSDT") || !table_ok(d))
        return;
    u32 dlen = (u32)d[4] | ((u32)d[5] << 8) |
               ((u32)d[6] << 16) | ((u32)d[7] << 24);
    /* find \_S5_: the 4-byte name preceded by the NameOp byte */
    static const u8 name[4] = { '_', 'S', '5', '_' };
    for (u32 i = 36; i + 9 < dlen; i++) {
        if (d[i] == name[0] && d[i + 1] == name[1] &&
            d[i + 2] == name[2] && d[i + 3] == name[3]) {
            if (i == 0 || d[i - 1] != 0x08)
                continue;
            g_slp_typ = parse_s5(&d[i]);
            if (g_slp_typ >= 0)
                return;
        }
    }
}

int ocbl_acpi_s5_ready(void)
{
    return (g_pm1a_cnt && g_slp_typ >= 0) ? 0 : -1;
}

/* Enter S5: PM1a_CNT = SLP_TYP<<10 | SLP_EN.  Returns -1 so callers
 * can fall back if the write does not power the machine down. */
int ocbl_acpi_enter_s5(void)
{
    acpi_resolve_s5();
    if (!g_pm1a_cnt || g_slp_typ < 0)
        return -1;
    u16 val = (u16)(((u16)g_slp_typ << 10) | (1u << 13));
    __asm__ volatile("out %0, %1" : : "a"(val), "dN"((u16)g_pm1a_cnt));
    for (volatile int i = 0; i < 1000000; i++) { }
    return -1;
}

