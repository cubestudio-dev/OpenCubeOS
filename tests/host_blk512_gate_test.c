/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 *
 * Host-side regression test for the BUG-0040+BUG-0054 (P0fix3) block
 * layer 512-byte sector gate.  NOT part of the kernel build.
 *
 * The block + cache layers are hardwired to 512-byte sectors
 * (BLK_SECTOR_SIZE, cache slot data[512]).  Both registration entry
 * points must now REFUSE any device whose sector_size != 512
 * fail-closed (same policy the NVMe driver applies to 4Kn LBADS) -
 * on the old code a 4Kn "nvme0" or a 2048-byte USB medium was
 * registered anyway and every cached read/write overran its 512-byte
 * caller buffers by sector_size-512 bytes per sector.
 *
 * 13 two-sided assertions: every case checks the register return code
 * AND a side effect (slot really present / absent, honest log line),
 * through BOTH entry paths:
 *   - blk_dev_add()  (index-based, used by ATA/AHCI/
 *     NVMe/virtio/g8ram)
 *   - blk_dev_add_ptr()         (pointer-based, used by USB MSC,
 *     partitions, L1 extensions)
 * Values fed to the gate: 512 (accept) vs 256/4096/2048/0/negative/
 * off-by-one/garbage (reject).
 *
 * Build (from the repo root):
 *   gcc -O2 -Wall -Wextra -Idrivers/block -Ikernel -Ikernel/lib \
 *       -Ikernel/mem -Idrivers/display \
 *       tests/host_blk512_gate_test.c drivers/block/driver_block_blk.c \
 *       -o /tmp/blk512_gate_test
 * (kernel sources compiled with host shims for the console/log/cache
 * dependencies - same pattern as tests/host_a401_entry_test.c and
 * tests/host_fix4_tar_test.c; do NOT include <string.h>, the kernel
 * lib_string.h declarations are used instead)
 */
#include <stdio.h>
#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "blk_drv_internal.h"
#include "driver_block_cache.h"

/* ---- host shims for the kernel dependencies of driver_block_blk.c ---- */

/* Console output is captured so the test can assert the honest log line. */
static char g_log[8192];
static usize g_log_len;
void screen_console_puts(const char *s) {
    while (*s && g_log_len + 1 < sizeof(g_log)) g_log[g_log_len++] = *s++;
    g_log[g_log_len] = 0;
}
static void log_reset(void) { g_log_len = 0; g_log[0] = 0; }
static int log_contains(const char *needle) {
    for (usize i = 0; g_log[i]; i++) {
        usize j = 0;
        while (needle[j] && g_log[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

/* Decimal formatter with the kernel lib_string.h signature (base 10,
 * no leading zeros, NUL-terminated, returns digit count). */
usize u64_to_str(u64 v, char *out) {
    char tmp[24];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return 1; }
    while (v && i < 24) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int n = 0;
    while (i) out[n++] = tmp[--i];
    out[n] = 0;
    return (usize)n;
}

/* The register paths never touch the cache; these stubs only satisfy
 * the linker (driver_block_read/write_sectors reference them). */
int driver_block_cache_read(int dev_idx, u64 lba, void *buf) {
    (void)dev_idx; (void)lba; (void)buf; return -1;
}
int driver_block_cache_write(int dev_idx, u64 lba, const void *buf) {
    (void)dev_idx; (void)lba; (void)buf; return 0;
}
int driver_block_cache_insert(int dev_idx, u64 lba, const void *buf) {
    (void)dev_idx; (void)lba; (void)buf; return 0;
}
int driver_block_cache_flush_dev(int dev_idx) { (void)dev_idx; return 0; }

/* Stub driver ops - never invoked, registration only checks presence. */
static int stub_read(driver_block_device_t *d, u64 lba, u32 n, void *buf) {
    (void)d; (void)lba; (void)n; (void)buf; return 0;
}
static int stub_write(driver_block_device_t *d, u64 lba, u32 n, const void *buf) {
    (void)d; (void)lba; (void)n; (void)buf; return 0;
}
static const driver_block_ops_t stub_ops = { stub_read, stub_write, 0 };

/* ---- harness ---- */

static int g_pass = 0, g_fail = 0;
static int check(const char *name, int cond) {
    printf("%-64s %s\n", name, cond ? "PASS" : "FAIL");
    if (cond) g_pass++; else g_fail++;
    return cond ? 0 : 1;
}

/* Path A: blk_dev_add(name, type, sectors, size, ops, priv) */
static int reg_dev(const char *name, u32 size) {
    return blk_dev_add(name, BLK_TYPE_ATA, 100, size,
                                        &stub_ops, 0);
}

/* Path B: blk_dev_add_ptr(dev, ops) with a filled-in device */
static int reg_ptr(const char *name, u32 size, int *reported_index) {
    driver_block_device_t dev;
    memset(&dev, 0, sizeof(dev));
    strncpy(dev.name, name, BLK_DEV_NAME_LEN - 1);
    dev.type = BLK_TYPE_USB;
    dev.sectors = 100;
    dev.sector_size = size;
    dev.present = 1;
    dev.index = -1;
    int idx = blk_dev_add_ptr(&dev, &stub_ops);
    if (reported_index) *reported_index = dev.index;
    return idx;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    block_svc_init();
    log_reset();

    /* ---------- Path A: index-based registration ---------- */

    /* 1. positive side: 512B is accepted and lands in the registry */
    int idx = reg_dev("ok512", 512);
    check("A1  reg_device: 512B accepted (idx>=0, slot live, size 512)",
          idx >= 0 &&
          driver_block_num_devices() == 1 &&
          driver_block_find_device("ok512") == idx &&
          driver_block_get_device(idx)->sector_size == 512);

    /* 2..7: negative side - every non-512 size must be refused with an
     * honest log line, and must not leave a phantom slot behind */
    int rc = reg_dev("bad4k", 4096);
    check("A2  reg_device: 4096B (4Kn NVMe LBADS) rejected + honest log",
          rc < 0 && driver_block_num_devices() == 1 &&
          log_contains("bad4k") && log_contains("4096") &&
          log_contains("NOT registered"));

    rc = reg_dev("bad2048", 2048);
    check("A3  reg_device: 2048B (USB CD media) rejected, no phantom slot",
          rc < 0 && driver_block_num_devices() == 1 &&
          driver_block_find_device("bad2048") < 0);

    rc = reg_dev("bad256", 256);
    check("A4  reg_device: 256B rejected, no phantom slot",
          rc < 0 && driver_block_num_devices() == 1);

    rc = reg_dev("badzero", 0);
    check("A5  reg_device: 0 (no size reported) rejected, no silent 512 default",
          rc < 0 && driver_block_num_devices() == 1);

    rc = reg_dev("badneg", (u32)-512);   /* 0xFFFFFE00 */
    check("A6  reg_device: 0xFFFFFE00 (negative -512 as u32) rejected",
          rc < 0 && driver_block_num_devices() == 1);

    rc = reg_dev("bad513", 513);
    check("A7  reg_device: 513B (off-by-one) rejected",
          rc < 0 && driver_block_num_devices() == 1);

    /* ---------- Path B: pointer-based registration ---------- */

    /* 8. positive side: 512B accepted, slot number reported back */
    int reported = -1;
    idx = reg_ptr("ok512b", 512, &reported);
    check("B1  reg ptr-API: 512B accepted, index reported back to caller",
          idx >= 0 && reported == idx &&
          driver_block_num_devices() == 2 &&
          driver_block_find_device("ok512b") == idx);

    log_reset();
    rc = reg_ptr("bad4kb", 4096, &reported);
    check("B2  reg ptr-API: 4096B rejected + honest log",
          rc < 0 && reported < 0 && driver_block_num_devices() == 2 &&
          log_contains("bad4kb") && log_contains("4096") &&
          log_contains("NOT registered"));

    rc = reg_ptr("bad2048b", 2048, &reported);
    check("B3  reg ptr-API: 2048B rejected, no phantom slot",
          rc < 0 && driver_block_num_devices() == 2 &&
          driver_block_find_device("bad2048b") < 0);

    rc = reg_ptr("bad256b", 256, &reported);
    check("B4  reg ptr-API: 256B rejected",
          rc < 0 && driver_block_num_devices() == 2);

    rc = reg_ptr("badzerob", 0, &reported);
    check("B5  reg ptr-API: 0 rejected, no silent 512 default",
          rc < 0 && driver_block_num_devices() == 2);

    rc = reg_ptr("badgarbage", 0xFFFFFFFFu, &reported);
    check("B6  reg ptr-API: 0xFFFFFFFF (garbage size) rejected",
          rc < 0 && driver_block_num_devices() == 2);

    /* ---------- summary ---------- */
    printf("\nblk512 gate test: %d/13 PASS%s\n", g_pass,
           g_fail ? "" : " (all sizes gated honestly)");
    if (g_fail) {
        printf("blk512 gate test: %d FAILURES\n", g_fail);
        return 1;
    }
    return 0;
}
