/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Host-side regression test for the WP-10-input_chipset touchpad
 * pipeline (drivers/input/driver_input_i2chid.c): HID report-descriptor
 * parse + the decoder + the gesture engine, fed with the same synthetic
 * multitouch fixtures the in-kernel `touchtest` command uses.
 * NOT part of the kernel build.
 *
 * Build (from oc-os/):
 *   gcc -O2 -I. -Ikernel -Ikernel/lib -Idrivers/input -Idrivers/i2c \
 *       -Idrivers/acpi tests/host_i2chid_touch_test.c -o /tmp/i2chid_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "kernel/types.h"

/* ---- kernel symbol stubs (the file under test needs these) ---- */
static u64 g_fake_ms = 1000;
u64 core_timer_now_ms(void) { return g_fake_ms; }
void *kmalloc(u64 n) { return malloc((size_t)n); }
void kfree(void *p) { free(p); }
void lib_log_stage(const char *m, const char *s) { (void)m; (void)s; }


/* ACPI discovery path (never taken on the host): report honestly empty */
int  driver_acpi_aml_loaded(void)        { return 0; }
int  driver_acpi_aml_device_count(void)  { return 0; }

/* I2C core (only used by the transport path, not the decoder tests) */
int  driver_i2c_count(void) { return 0; }
int  driver_i2c_transfer(int id, u16 addr, int addr10,
                         const u8 *wbuf, int wlen, u8 *rbuf, int rlen) {
    (void)id; (void)addr; (void)addr10; (void)wbuf; (void)wlen;
    (void)rbuf; (void)rlen;
    return -1;
}

#include "drivers/input/driver_input_i2chid.c"

static int check(const char *name, int cond) {
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

static const u8 fixture_rdesc[] = {
    0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01,
    0x09, 0x22, 0xA1, 0x00,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x09, 0x44, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x09, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02,
    0x09, 0x31, 0x81, 0x02,
    0xC0,
    0x05, 0x0D,
    0x09, 0x54, 0x25, 0x09, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0xC0,
};

static void drain_events(touchpad_event_t *out, int max, int *n) {
    *n = 0;
    touchpad_event_t e;
    while (*n < max && driver_input_touchpad_get_event(&e) == 0)
        out[(*n)++] = e;
}

int main(void) {
    int fails = 0;
    touchpad_event_t ev[16];
    int ne;
    u8 r[8];

    fails += check("descriptor parse", hid_parse_descriptor(fixture_rdesc, sizeof(fixture_rdesc)) == 0);
    printf("  layouts=%d fields0=%d\n", g_layout_count,
           g_layout_count ? g_layouts[0].field_count : -1);
    for (int i = 0; g_layout_count && i < g_layouts[0].field_count; i++) {
        printf("  field %d: off=%u size=%u kind=%d lmin=%lld lmax=%lld\n",
               i, g_layouts[0].fields[i].bit_off, g_layouts[0].fields[i].size,
               g_layouts[0].fields[i].kind,
               (long long)g_layouts[0].fields[i].lmin,
               (long long)g_layouts[0].fields[i].lmax);
    }

    /* full pipeline self-test (same fixtures as the in-kernel touchtest) */
    u32 m = (u32)driver_input_touchpad_selftest();
    fails += check("descriptor parse (selftest bit0)", (m & 1u) == 0);
    fails += check("TAP gesture",                     (m & (1u << 1)) == 0);
    fails += check("SCROLL gesture",                 (m & (1u << 2)) == 0);
    fails += check("PINCH gesture",                  (m & (1u << 3)) == 0);
    fails += check("SWIPE gesture",                  (m & (1u << 4)) == 0);
    if (m) printf("  selftest mask = 0x%x\n", m);

    printf("\n%s (%d failed checks)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails);
    return fails;
}
