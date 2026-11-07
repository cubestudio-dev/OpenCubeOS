/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_detect.c
 * Purpose: the four OCSE hardware checks. Each fills a check entry
 * with a PASS/FAIL verdict plus an ASCII detail line (the serial log
 * stays greppable in both languages).
 */
#include "ocse_detect.h"
#include "ocse_atapi.h"
#include "lib_string.h"
#include "screen_fb.h"
#include "mem_pmm.h"
#include "driver_block_blk.h"
#include <stdint.h>

ocse_check_t g_ocse_checks[OCSE_CHECKS];

static void set_result(int idx, int pass, const char* detail) {
    g_ocse_checks[idx].pass = pass;
    u32 i = 0;
    for (; detail && detail[i] && i < sizeof(g_ocse_checks[0].detail) - 1; i++)
        g_ocse_checks[idx].detail[i] = detail[i];
    g_ocse_checks[idx].detail[i] = 0;
}

/* check 1: CPU - CPUID vendor + long-mode capability. */
static void check_cpu(char* out, int cap) {
    u32 a = 0, b = 0, c = 0, d = 0;
    char vendor[13];
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0));
    *(u32*)(vendor + 0) = b;
    *(u32*)(vendor + 4) = d;
    *(u32*)(vendor + 8) = c;
    vendor[12] = 0;
    /* Live-caught (T1): the stock QEMU CPU model does not advertise
     * the 0x80000001 extended leaf, so CPUID-based long-mode detection
     * misreported FAIL while the kernel is RUNNING in long mode. The
     * definitive check is EFER.LMA (bit 10 of MSR 0xC0000080): set by
     * the hardware the moment long mode is active - which OCBL proved
     * before handing control to us. */
    u32 elo = 0, ehi = 0;
    __asm__ volatile("rdmsr" : "=a"(elo), "=d"(ehi) : "c"(0xC0000080u));
    int lm = (int)((elo >> 10) & 1u);
    strcpy(out, "CPUID \"");
    u32 p = strlen(out);
    for (u32 i = 0; vendor[i] && p + 24 < (u32)cap; i++) out[p++] = vendor[i];
    strcpy(out + p, lm ? "\" long-mode=yes" : "\" long-mode=NO");
    set_result(0, lm, out);
}

/* check 2: usable memory from the PMM. */
static void check_mem(char* out, int cap) {
    (void)cap;
    mem_pmm_stats_t ps;
    mem_pmm_get_stats(&ps);
    u64 mib = ((u64)ps.total_pages * 4u) / 1024u;   /* 4 KiB pages -> MiB */
    char n[16];
    strcpy(out, "PMM usable: ");
    u64_to_str(mib, n);
    strcat(out, n);
    strcat(out, " MiB");
    set_result(1, mib >= 32, out);
}

/* check 3: framebuffer handed over by OCBL. */
static void check_disp(char* out, int cap) {
    (void)cap;
    const screen_fb_info_t* fb = screen_fb_get_info();
    char n[12];
    strcpy(out, "framebuffer ");
    u64_to_str(fb->width, n);  strcat(out, n); strcat(out, "x");
    u64_to_str(fb->height, n); strcat(out, n); strcat(out, "x");
    u64_to_str(fb->bpp, n);    strcat(out, n);
    int ok = fb->addr != 0 && fb->width >= 200 && fb->height >= 150;
    set_result(2, ok, out);
}

/* check 4: at least one usable block device. WP-06b: optical drives
 * (the ATAPI install medium, "cdN") are deliberately NOT install
 * targets - the check counts storage the system can be installed TO.
 * The negative acceptance (boot from ISO, no hard disk) must still
 * FAIL and halt even though the install medium itself is present. */
static void check_disk(char* out, int cap) {
    (void)cap;
    int n = driver_block_num_devices();
    int usable = 0;
    for (int i = 0; i < n; i++) {
        driver_block_device_t* dev = driver_block_get_device(i);
        if (dev && dev->present && !ocse_atapi_is_cd_name(dev->name))
            usable++;
    }
    char num[12];
    strcpy(out, "block devices: ");
    u64_to_str((u64)usable, num);
    strcat(out, num);
    strcat(out, " present");
    set_result(3, usable >= 1, out);
}

void ocse_detect_run(void) {
    static char line[96];
    for (int i = 0; i < OCSE_CHECKS; i++) {
        g_ocse_checks[i].pass = 0;
        g_ocse_checks[i].detail[0] = 0;
    }
    check_cpu(line, sizeof(line));
    check_mem(line, sizeof(line));
    check_disp(line, sizeof(line));
    check_disk(line, sizeof(line));
}
