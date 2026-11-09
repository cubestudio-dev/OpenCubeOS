/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/input: touchpad surface op bodies.
 *
 * Rescued verbatim out of recovery/shell_cmds_chipset.c (the L0-only
 * chipset diagnostics commands died with the oc> shell; these two
 * bodies serve the L1SVC "input" ops through inp_l1.c).  The rest of
 * that file (acpi / acpitest / msitest / pcidump / pcibars / pcie /
 * i2c / smbus diagnostics commands) is user-plane WP-03 deletion -
 * to be re-exposed from ring 3 in WP-04.
 */
#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "driver_input_i2chid.h"

int inp_cmd_touchpad(const char *args)   /* v2.0.0: input L1 surface */ {
    (void)args;
    char line[100]; char n[16];
    if (driver_input_i2chid_present()) {
        strcpy(line, "touchpad: i2c-hid bound, bus ");
        u64_to_str((u64)driver_input_i2chid_input_bus(), n); strcat(line, n);
        strcat(line, " addr 0x");
        u64_to_hex(driver_input_i2chid_input_addr(), n, 2); strcat(line, n);
        strcat(line, " vid 0x");
        u64_to_hex(driver_input_i2chid_vendor_id(), n, 4); strcat(line, n);
        strcat(line, " pid 0x");
        u64_to_hex(driver_input_i2chid_product_id(), n, 4); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("touchpad: no i2c-hid device bound\n");
        screen_console_puts("  (QEMU ships no I2C-HID model; pipeline covered by touchtest)\n");
    }
    strcpy(line, "reports decoded: ");
    u64_to_str(driver_input_i2chid_report_count(), n); strcat(line, n);
    strcat(line, "  events queued total: ");
    u64_to_str(driver_input_i2chid_event_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    driver_input_i2chid_poll();
    int shown = 0;
    touchpad_event_t e;
    while (shown < 8 && driver_input_touchpad_get_event(&e) == 0) {
        strcpy(line, "  event: ");
        switch (e.type) {
        case TPE_TAP:    strcat(line, "TAP"); break;
        case TPE_SCROLL: strcat(line, "SCROLL"); break;
        case TPE_PINCH:  strcat(line, "PINCH"); break;
        case TPE_SWIPE:  strcat(line, "SWIPE"); break;
        case TPE_MOVE:   strcat(line, "MOVE"); break;
        case TPE_DOWN:   strcat(line, "DOWN"); break;
        case TPE_UP:     strcat(line, "UP"); break;
        default:         strcat(line, "?"); break;
        }
        char num[16];
        strcpy(line + strlen(line), " dx=");
        u64_to_str((u64)(e.dx < 0 ? -e.dx : e.dx), num);
        if (e.dx < 0) strcat(line, "-");
        strcat(line, num);
        strcat(line, " dy=");
        u64_to_str((u64)(e.dy < 0 ? -e.dy : e.dy), num);
        if (e.dy < 0) strcat(line, "-");
        strcat(line, num);
        strcat(line, " fingers=");
        u64_to_str((u64)e.fingers, num); strcat(line, num);
        strcat(line, "\n");
        screen_console_puts(line);
        shown++;
    }
    return 0;
}

int inp_cmd_touchtest(const char *args)   /* v2.0.0: input L1 surface */ {
    (void)args;
    int mask = driver_input_touchpad_selftest();
    if (mask == 0) {
        screen_console_puts("touchtest: descriptor parse + TAP/SCROLL/PINCH/SWIPE  PASS\n");
        return 0;
    }
    /* name the failing item(s) - bit0 parse / bit1 tap / bit2 scroll /
     * bit3 pinch / bit4 swipe - so the verdict is actionable. */
    screen_console_puts("touchtest: FAIL\n");
    if (mask & (1 << 0)) screen_console_puts("  descriptor parse      FAIL\n");
    if (mask & (1 << 1)) screen_console_puts("  TAP gesture           FAIL\n");
    else                 screen_console_puts("  TAP gesture           PASS\n");
    if (mask & (1 << 2)) screen_console_puts("  SCROLL gesture        FAIL\n");
    else                 screen_console_puts("  SCROLL gesture        PASS\n");
    if (mask & (1 << 3)) screen_console_puts("  PINCH gesture         FAIL\n");
    else                 screen_console_puts("  PINCH gesture         PASS\n");
    if (mask & (1 << 4)) screen_console_puts("  SWIPE gesture         FAIL\n");
    else                 screen_console_puts("  SWIPE gesture         PASS\n");
    return 1;
}
