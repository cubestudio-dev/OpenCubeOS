/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_test_cmds.c
 * Purpose: Shell status + test commands for the WP-10d USB stack.
 *
 *   usb              - hosts + device tree + class-driver states
 *   usbdev [slot]    - per-device descriptor detail
 *   driver_usb_core_test    - core invariants + live control transfers
 *   driver_usb_kbd_test     - HID keyboard presence + injection counters
 *   driver_usb_mouse_test   - HID mouse presence + event ring drain
 *   driver_usb_storage_test - MSC drive: capacity, sector0, r/w round-trip,
 *                      partition table
 *   driver_usb_serial_test  - CDC-ACM/FTDI port: TX + RX round
 *   driver_usb_hotplug_test - poll/enumerate (wait-new N / wait-leave N)
 *   driver_usb_hub_test     - external hub + children behind it
 *   (driver_usb_audio_test lives in driver_snd_test_cmds.c since WP-10c)
 *
 * Everything runs real controller transactions - no simulated data.
 * When a device class is not attached, the test reports FAIL with
 * the actual reason (no fake output).
 */
#include "driver_usb.h"
#include "driver_usb_hid.h"
#include "driver_usb_msc.h"
#include "driver_usb_serial.h"
#include "driver_usb_audio.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"
#include "shell.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"
#include "core_sched.h"

static void t_pass(const char *test, const char *what) {
    char line[140];
    strcpy(line, "["); strcat(line, test); strcat(line, "] ");
    strcat(line, what); strcat(line, " => PASS\n");
    screen_console_puts(line);
}

static void t_fail(const char *test, const char *what,
                   const char *actual) {
    char line[200];
    strcpy(line, "["); strcat(line, test); strcat(line, "] ");
    strcat(line, what); strcat(line, " => FAIL (");
    strcat(line, actual); strcat(line, ")\n");
    screen_console_puts(line);
}

static void t_info(const char *test, const char *what) {
    char line[180];
    strcpy(line, "["); strcat(line, test); strcat(line, "] ");
    strcat(line, what); screen_console_puts(line);
    screen_console_puts("\n");
}

/* ==================================================================
 * status commands
 * ================================================================== */

static int shell_cmd_usb(const char *args) {
    (void)args;
    driver_usb_print_state();
    char n[12];
    char line[96];
    strcpy(line, "class drivers: ");
    u64_to_str((u64)driver_usb_num_drivers(), n); strcat(line, n);
    strcat(line, " registered | keyboards=");
    u64_to_str((u64)driver_usb_hid_num_keyboards(), n); strcat(line, n);
    strcat(line, " mice=");
    u64_to_str((u64)driver_usb_hid_num_mice(), n); strcat(line, n);
    strcat(line, " msc=");
    u64_to_str((u64)driver_usb_msc_num_devices(), n); strcat(line, n);
    strcat(line, " serial=");
    u64_to_str((u64)driver_usb_serial_num_ports(), n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
    driver_usb_msc_print_state();
    driver_usb_serial_print_state();
    driver_usb_mouse_print_state();
    driver_usb_audio_print_state();
    return 0;
}

static int shell_cmd_usbdev(const char *args) {
    if (args && args[0]) {
        int slot = 0;
        for (const char *p = args; *p >= '0' && *p <= '9'; p++)
            slot = slot * 10 + (*p - '0');
        driver_usb_print_device(slot);
        return 0;
    }
    int any = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (driver_usb_get_device(i)) {
            driver_usb_print_device(i);
            any = 1;
        }
    }
    if (!any) screen_console_puts("usbdev: no devices\n");
    return 0;
}

/* ==================================================================
 * driver_usb_core_test
 * ================================================================== */

static int driver_usb_count_devices(void) { return driver_usb_num_devices(); }

static int shell_cmd_usb_core_test(const char *args) {
    (void)args;
    const char *T = "usb_core_test";
    int fails = 0;
    char n[12];

    t_info(T, "input: USB core invariants + live transfers");

    /* 1. hosts */
    if (driver_usb_num_hosts() > 0) {
        t_pass(T, "usb host controllers registered");
    } else {
        t_fail(T, "usb host controllers registered", "none");
        fails++;
    }

    /* 2. device table consistency */
    int devs = driver_usb_count_devices();
    int bad = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d) continue;
        if (d->addr < 1 || d->addr > 127) bad++;
        if (!d->host) bad++;
        if (d->parent >= 0) {
            driver_usb_dev_t *p = driver_usb_get_device(d->parent);
            if (!p) bad++;
        }
        if (d->n_if < 1 || d->mps0 < 8 || d->mps0 > 64) bad++;
        if (d->cfg_len < 9) bad++;
    }
    if (bad == 0) {
        strcpy(n, ""); u64_to_str((u64)devs, n);
        char w[64];
        strcpy(w, "device table consistent (");
        strcat(w, n);
        strcat(w, " devices)");
        t_pass(T, w);
    } else {
        t_fail(T, "device table consistent", "see counters");
        fails++;
    }

    /* 3. live control transfer: GET_STATUS on every device */
    int live_bad = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d) continue;
        u8 st[2] = { 0, 0 };
        if (driver_usb_control(d, 0x80, USB_REQ_GET_STATUS, 0, 0, st, 2)
                != 0)
            live_bad++;
    }
    if (devs > 0 && live_bad == 0) {
        t_pass(T, "GET_STATUS live on every device");
    } else if (devs == 0) {
        t_fail(T, "GET_STATUS live on every device",
               "no devices attached");
        fails++;
    } else {
        t_fail(T, "GET_STATUS live on every device",
               "a device did not answer");
        fails++;
    }

    /* 4. class drivers */
    if (driver_usb_num_drivers() >= 5) {
        strcpy(n, "");
        u64_to_str((u64)driver_usb_num_drivers(), n);
        char w[64];
        strcpy(w, "class drivers registered (");
        strcat(w, n); strcat(w, ")");
        t_pass(T, w);
    } else {
        t_fail(T, "class drivers registered", "too few");
        fails++;
    }

    /* 5. re-enumeration is idempotent */
    int before = driver_usb_count_devices();
    driver_usb_enumerate();
    int after = driver_usb_count_devices();
    if (before == after) {
        t_pass(T, "re-enumeration idempotent");
    } else {
        t_fail(T, "re-enumeration idempotent",
               "device count changed");
        fails++;
    }

    char s[64];
    strcpy(s, "summary ");
    strcat(s, fails ? "FAIL" : "PASS");
    screen_console_puts("[");
    screen_console_puts(T);
    screen_console_puts("] ");
    screen_console_puts(s);
    screen_console_puts("\n");
    return fails ? 1 : 0;
}

/* ==================================================================
 * driver_usb_kbd_test / driver_usb_mouse_test
 * ================================================================== */

static int shell_cmd_usb_kbd_test(const char *args) {
    (void)args;
    const char *T = "usb_kbd_test";
    int kbds = driver_usb_hid_num_keyboards();
    if (kbds <= 0) {
        t_fail(T, "HID keyboard attached", "none enumerated");
        return 1;
    }
    char n[12];
    char w[96];
    strcpy(w, "HID keyboard attached (");
    u64_to_str((u64)kbds, n); strcat(w, n); strcat(w, ")");
    t_pass(T, w);
    /* one poll round + report the injection counter */
    driver_usb_hid_poll();
    t_info(T, "poll: one interrupt-IN round done; keystrokes are "
              "injected into the standard input queue");
    return 0;
}

static int shell_cmd_usb_mouse_test(const char *args) {
    (void)args;
    const char *T = "usb_mouse_test";
    int mice = driver_usb_hid_num_mice();
    if (mice <= 0) {
        t_fail(T, "HID mouse attached", "none enumerated");
        return 1;
    }
    char n[12];
    char w[96];
    strcpy(w, "HID mouse attached (");
    u64_to_str((u64)mice, n); strcat(w, n); strcat(w, ")");
    t_pass(T, w);
    /* drain the event ring for up to 2 s (movement comes from the
     * host pointer / management input) */
    int got = 0;
    driver_usb_mouse_event_t ev;
    driver_usb_mouse_event_t first;
    int have_first = 0;
    u64 deadline = core_timer_now_ms() + 2000;
    while (core_timer_now_ms() < deadline) {
        driver_usb_hid_poll();
        while (driver_usb_mouse_read_event(&ev)) {
            if (!have_first) { first = ev; have_first = 1; }
            got++;
        }
        if (got >= 3) break;
        core_sched_yield();
    }
    if (got > 0) {
        strcpy(w, "events received: ");
        u64_to_str((u64)got, n); strcat(w, n);
        strcat(w, " first(dx=");
        u64_to_str((u64)first.dx, n); strcat(w, n);
        strcat(w, ",dy=");
        u64_to_str((u64)first.dy, n); strcat(w, n);
        strcat(w, ",btn=");
        u64_to_str(first.buttons, n); strcat(w, n);
        strcat(w, ")");
        t_pass(T, w);
    } else {
        t_info(T, "no movement events in the window "
                  "(mouse present; move the host pointer)");
    }
    return 0;
}

/* ==================================================================
 * driver_usb_storage_test
 * ================================================================== */

static int shell_cmd_usb_storage_test(const char *args) {
    (void)args;
    const char *T = "usb_storage_test";
    char n[24];
    if (driver_usb_msc_num_devices() <= 0) {
        t_fail(T, "USB storage drive attached",
               "no MSC device enumerated");
        return 1;
    }
    /* find the USB block device */
    int usd = -1;
    for (int i = 0; i < driver_block_num_devices(); i++) {
        driver_block_device_t *d = driver_block_get_device(i);
        if (d && d->present && d->type == BLK_TYPE_USB) { usd = i; break; }
    }
    if (usd < 0) {
        t_fail(T, "USB block device registered", "not in blk table");
        return 1;
    }
    driver_block_device_t *d = driver_block_get_device(usd);
    char w[128];
    strcpy(w, "drive ");
    strcat(w, d->name);
    strcat(w, " capacity ");
    u64_to_str(d->sectors, n); strcat(w, n);
    strcat(w, " sectors");
    t_pass(T, w);

    /* sector 0 read (MBR) */
    u8 sec0[512];
    if (driver_block_read_sectors(usd, 0, 1, sec0) != 0) {
        t_fail(T, "sector 0 read", "blk_read_sectors failed");
        return 1;
    }
    t_pass(T, "sector 0 read (MBR)");

    /* read/write round-trip on the last sector, then restore */
    u64 last = d->sectors - 1;
    u8 orig[512], pat[512], back[512];
    if (driver_block_read_sectors(usd, last, 1, orig) != 0) {
        t_fail(T, "last sector read", "failed");
        return 1;
    }
    for (int i = 0; i < 512; i++) pat[i] = (u8)(i ^ 0x5a);
    if (driver_block_write_sectors(usd, last, 1, pat) != 0) {
        t_fail(T, "last sector write", "failed");
        return 1;
    }
    driver_block_flush(d);
    if (driver_block_read_sectors(usd, last, 1, back) != 0 ||
        memcmp(pat, back, 512) != 0) {
        t_fail(T, "write/read-back verify", "data mismatch");
        driver_block_write_sectors(usd, last, 1, orig);
        return 1;
    }
    t_pass(T, "write/read-back verify (last sector)");
    driver_block_write_sectors(usd, last, 1, orig);   /* restore */
    driver_block_flush(d);
    t_pass(T, "original sector restored");

    /* partition table */
    driver_block_part_table_t pt;
    if (driver_block_part_parse(usd, &pt) == 0) {
        strcpy(w, "partition table ");
        strcat(w, pt.table_type == PART_TYPE_MBR ? "MBR" :
                     pt.table_type == PART_TYPE_GPT ? "GPT" : "?");
        strcat(w, " partitions=");
        u64_to_str((u64)pt.count, n); strcat(w, n);
        t_pass(T, w);
    } else {
        t_info(T, "no partition table (blank media)");
    }
    return 0;
}

/* ==================================================================
 * driver_usb_serial_test
 * ================================================================== */

static const char *SER_MSG = "OpenCubeOS WP-10d serial test\n";

static int shell_cmd_usb_serial_test(const char *args) {
    (void)args;
    const char *T = "usb_serial_test";
    char n[12];
    if (driver_usb_serial_num_ports() <= 0) {
        t_fail(T, "USB serial port attached",
               "no CDC-ACM/FTDI device enumerated");
        return 1;
    }
    t_pass(T, "USB serial port attached");
    int len = 0;
    while (SER_MSG[len]) len++;
    int sent = driver_usb_serial_write(SER_MSG, len);
    if (sent == len) {
        strcpy(n, ""); u64_to_str((u64)sent, n);
        char w[80];
        strcpy(w, "TX complete (");
        strcat(w, n); strcat(w, " bytes)");
        t_pass(T, w);
    } else {
        t_fail(T, "TX complete", "short write / error");
        return 1;
    }
    /* RX drain for up to 2 s (loopback or remote echo) */
    char rx[64];
    int got = 0;
    u64 deadline = core_timer_now_ms() + 2000;
    while (core_timer_now_ms() < deadline && got == 0) {
        driver_usb_serial_poll();
        got = driver_usb_serial_read(rx, sizeof(rx));
        core_sched_yield();
    }
    if (got > 0) {
        strcpy(n, ""); u64_to_str((u64)got, n);
        char w[96];
        strcpy(w, "RX received (");
        strcat(w, n); strcat(w, " bytes)");
        t_pass(T, w);
        screen_console_puts("[usb_serial_test] rx: ");
        for (int i = 0; i < got; i++) {
            char c = (rx[i] >= 0x20 && rx[i] < 0x7f) ? rx[i] : '.';
            screen_console_putc(c);
        }
        screen_console_puts("\n");
    } else {
        t_info(T, "no RX in the window (expected when no loopback/"
                  "peer is wired to the port)");
    }
    return 0;
}

/* ==================================================================
 * driver_usb_hotplug_test
 * ================================================================== */

static int shell_cmd_usb_hotplug_test(const char *args) {
    const char *T = "usb_hotplug_test";
    char n[12];
    if (args && args[0]) {
        /* "wait-new N" / "wait-leave N": poll + enumerate until the
         * device count crosses N (or 8 s deadline) */
        int want_new = (args[0] == 'w' && args[5] == 'n');
        int n_arg = 0;
        const char *p = args;
        while (*p && (*p < '0' || *p > '9')) p++;
        while (*p >= '0' && *p <= '9') { n_arg = n_arg * 10 + (*p - '0'); p++; }
        u64 deadline = core_timer_now_ms() + 8000;
        int seen = driver_usb_num_devices();
        int ok = want_new ? (seen >= n_arg) : (seen <= n_arg);
        while (!ok && core_timer_now_ms() < deadline) {
            driver_usb_poll();
            driver_usb_enumerate();
            seen = driver_usb_num_devices();
            ok = want_new ? (seen >= n_arg) : (seen <= n_arg);
            core_sched_yield();
        }
        strcpy(n, ""); u64_to_str((u64)seen, n);
        char w[96];
        strcpy(w, "device count now ");
        strcat(w, n);
        if (ok) t_pass(T, w);
        else t_fail(T, want_new ? "device appeared" : "device left",
                    w);
        return ok ? 0 : 1;
    }
    driver_usb_poll();
    int found = driver_usb_enumerate();
    strcpy(n, ""); u64_to_str((u64)found, n);
    char w[96];
    strcpy(w, "poll + enumerate round done (new devices: ");
    strcat(w, n); strcat(w, ")");
    t_pass(T, w);
    driver_usb_print_tree();
    return 0;
}

/* ==================================================================
 * driver_usb_hub_test
 * ================================================================== */

static int shell_cmd_usb_hub_test(const char *args) {
    (void)args;
    const char *T = "usb_hub_test";
    int hubs = 0, children = 0;
    char n[12];
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d || d->class != USB_CLASS_HUB) continue;
        hubs++;
        int kids = 0;
        for (int j = 0; j < USB_MAX_DEVICES; j++) {
            driver_usb_dev_t *c = driver_usb_get_device(j);
            if (c && c->parent == d->slot) {
                kids++;
                char w[128];
                strcpy(w, "hub dev");
                u64_to_str((u64)d->addr, n); strcat(w, n);
                strcat(w, " port ");
                u64_to_str(c->driver_usb_hub_port, n); strcat(w, n);
                strcat(w, " -> device ");
                u64_to_str((u64)c->addr, n); strcat(w, n);
                strcat(w, " (vid=0x");
                u64_to_hex(c->vid, n, 4); strcat(w, n);
                strcat(w, ")");
                t_pass(T, w);
            }
        }
        children += kids;
    }
    if (hubs == 0) {
        t_fail(T, "external hub attached", "no hub device");
        return 1;
    }
    if (children > 0) {
        t_pass(T, "hub children enumerated through the hub driver");
        return 0;
    }
    t_fail(T, "hub children enumerated", "hub has no devices");
    return 1;
}

/* ==================================================================
 * poll thread + registration
 * ================================================================== */

static void driver_usb_poll_thread(void *arg) {
    (void)arg;
    u64 round = 0;
    for (;;) {
        driver_usb_poll();
        driver_usb_hid_poll();
        driver_usb_serial_poll();
        if ((round & 0x0f) == 0) driver_usb_msc_poll();
        round++;
        /* ~2 ms breather between rounds */
        u64 deadline = core_timer_now_ms() + 2;
        while (core_timer_now_ms() < deadline) core_sched_yield();
    }
}

void driver_usb_poll_start(void) {
    /* Priority = TASK_PRIO_MAX: the shell runs inside the idle task
     * (same priority), so the poll thread round-robins with it
     * instead of starving it on the USB transfer mutex. */
    core_kthread_create(driver_usb_poll_thread, NULL, "usb-poll", TASK_PRIO_MAX);
}

void shell_cmds_usb_test_register(void) {
    shell_register_command_ex("usb", shell_cmd_usb, "USB hosts/devices + class driver status", "WP-10d");
    shell_register_command_ex("usbdev", shell_cmd_usbdev, "USB device detail (usbdev [slot])", "WP-10d");
    shell_register_command_ex("usb_core_test", shell_cmd_usb_core_test, "USB core invariants + live transfers", "WP-10d");
    shell_register_command_ex("usb_kbd_test", shell_cmd_usb_kbd_test, "USB HID keyboard presence + injection", "WP-10d");
    shell_register_command_ex("usb_mouse_test", shell_cmd_usb_mouse_test, "USB HID mouse presence + event drain", "WP-10d");
    shell_register_command_ex("usb_storage_test", shell_cmd_usb_storage_test, "USB MSC drive capacity/rw/partitions", "WP-10d");
    shell_register_command_ex("usb_serial_test", shell_cmd_usb_serial_test, "USB serial TX/RX round", "WP-10d");
    shell_register_command_ex("usb_hotplug_test", shell_cmd_usb_hotplug_test, "USB hot-plug (usb_hotplug_test [wait-new N|wait-leave N])", "WP-10d");
    shell_register_command_ex("usb_hub_test", shell_cmd_usb_hub_test, "USB external hub + children", "WP-10d");
}
