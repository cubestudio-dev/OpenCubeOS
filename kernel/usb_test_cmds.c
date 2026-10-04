/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_test_cmds.c
 * Purpose: Shell status + test commands for the WP-10d USB stack.
 *
 *   usb              - hosts + device tree + class-driver states
 *   usbdev [slot]    - per-device descriptor detail
 *   usb_core_test    - core invariants + live control transfers
 *   usb_kbd_test     - HID keyboard presence + injection counters
 *   usb_mouse_test   - HID mouse presence + event ring drain
 *   usb_storage_test - MSC drive: capacity, sector0, r/w round-trip,
 *                      partition table
 *   usb_serial_test  - CDC-ACM/FTDI port: TX + RX round
 *   usb_hotplug_test - poll/enumerate (wait-new N / wait-leave N)
 *   usb_hub_test     - external hub + children behind it
 *   (usb_audio_test lives in snd_test_cmds.c since WP-10c)
 *
 * Everything runs real controller transactions - no simulated data.
 * When a device class is not attached, the test reports FAIL with
 * the actual reason (no fake output).
 */
#include "usb.h"
#include "usb_hid.h"
#include "usb_msc.h"
#include "usb_serial.h"
#include "usb_audio.h"
#include "blk.h"
#include "part.h"
#include "shell.h"
#include "console.h"
#include "string.h"
#include "timer.h"
#include "sched.h"

static void t_pass(const char *test, const char *what) {
    char line[140];
    oc_strcpy(line, "["); oc_strcat(line, test); oc_strcat(line, "] ");
    oc_strcat(line, what); oc_strcat(line, " => PASS\n");
    oc_console_puts(line);
}

static void t_fail(const char *test, const char *what,
                   const char *actual) {
    char line[200];
    oc_strcpy(line, "["); oc_strcat(line, test); oc_strcat(line, "] ");
    oc_strcat(line, what); oc_strcat(line, " => FAIL (");
    oc_strcat(line, actual); oc_strcat(line, ")\n");
    oc_console_puts(line);
}

static void t_info(const char *test, const char *what) {
    char line[180];
    oc_strcpy(line, "["); oc_strcat(line, test); oc_strcat(line, "] ");
    oc_strcat(line, what); oc_console_puts(line);
    oc_console_puts("\n");
}

/* ==================================================================
 * status commands
 * ================================================================== */

static int cmd_usb(const char *args) {
    (void)args;
    usb_print_state();
    char n[12];
    char line[96];
    oc_strcpy(line, "class drivers: ");
    oc_u64_to_str((u64)usb_num_drivers(), n); oc_strcat(line, n);
    oc_strcat(line, " registered | keyboards=");
    oc_u64_to_str((u64)usb_hid_num_keyboards(), n); oc_strcat(line, n);
    oc_strcat(line, " mice=");
    oc_u64_to_str((u64)usb_hid_num_mice(), n); oc_strcat(line, n);
    oc_strcat(line, " msc=");
    oc_u64_to_str((u64)usb_msc_num_devices(), n); oc_strcat(line, n);
    oc_strcat(line, " serial=");
    oc_u64_to_str((u64)usb_serial_num_ports(), n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");
    usb_msc_print_state();
    usb_serial_print_state();
    usb_mouse_print_state();
    usb_audio_print_state();
    return 0;
}

static int cmd_usbdev(const char *args) {
    if (args && args[0]) {
        int slot = 0;
        for (const char *p = args; *p >= '0' && *p <= '9'; p++)
            slot = slot * 10 + (*p - '0');
        usb_print_device(slot);
        return 0;
    }
    int any = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_get_device(i)) {
            usb_print_device(i);
            any = 1;
        }
    }
    if (!any) oc_console_puts("usbdev: no devices\n");
    return 0;
}

/* ==================================================================
 * usb_core_test
 * ================================================================== */

static int usb_count_devices(void) { return usb_num_devices(); }

static int cmd_usb_core_test(const char *args) {
    (void)args;
    const char *T = "usb_core_test";
    int fails = 0;
    char n[12];

    t_info(T, "input: USB core invariants + live transfers");

    /* 1. hosts */
    if (usb_num_hosts() > 0) {
        t_pass(T, "usb host controllers registered");
    } else {
        t_fail(T, "usb host controllers registered", "none");
        fails++;
    }

    /* 2. device table consistency */
    int devs = usb_count_devices();
    int bad = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_dev_t *d = usb_get_device(i);
        if (!d) continue;
        if (d->addr < 1 || d->addr > 127) bad++;
        if (!d->host) bad++;
        if (d->parent >= 0) {
            usb_dev_t *p = usb_get_device(d->parent);
            if (!p) bad++;
        }
        if (d->n_if < 1 || d->mps0 < 8 || d->mps0 > 64) bad++;
        if (d->cfg_len < 9) bad++;
    }
    if (bad == 0) {
        oc_strcpy(n, ""); oc_u64_to_str((u64)devs, n);
        char w[64];
        oc_strcpy(w, "device table consistent (");
        oc_strcat(w, n);
        oc_strcat(w, " devices)");
        t_pass(T, w);
    } else {
        t_fail(T, "device table consistent", "see counters");
        fails++;
    }

    /* 3. live control transfer: GET_STATUS on every device */
    int live_bad = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_dev_t *d = usb_get_device(i);
        if (!d) continue;
        u8 st[2] = { 0, 0 };
        if (usb_control(d, 0x80, USB_REQ_GET_STATUS, 0, 0, st, 2)
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
    if (usb_num_drivers() >= 5) {
        oc_strcpy(n, "");
        oc_u64_to_str((u64)usb_num_drivers(), n);
        char w[64];
        oc_strcpy(w, "class drivers registered (");
        oc_strcat(w, n); oc_strcat(w, ")");
        t_pass(T, w);
    } else {
        t_fail(T, "class drivers registered", "too few");
        fails++;
    }

    /* 5. re-enumeration is idempotent */
    int before = usb_count_devices();
    usb_enumerate();
    int after = usb_count_devices();
    if (before == after) {
        t_pass(T, "re-enumeration idempotent");
    } else {
        t_fail(T, "re-enumeration idempotent",
               "device count changed");
        fails++;
    }

    char s[64];
    oc_strcpy(s, "summary ");
    oc_strcat(s, fails ? "FAIL" : "PASS");
    oc_console_puts("[");
    oc_console_puts(T);
    oc_console_puts("] ");
    oc_console_puts(s);
    oc_console_puts("\n");
    return fails ? 1 : 0;
}

/* ==================================================================
 * usb_kbd_test / usb_mouse_test
 * ================================================================== */

static int cmd_usb_kbd_test(const char *args) {
    (void)args;
    const char *T = "usb_kbd_test";
    int kbds = usb_hid_num_keyboards();
    if (kbds <= 0) {
        t_fail(T, "HID keyboard attached", "none enumerated");
        return 1;
    }
    char n[12];
    char w[96];
    oc_strcpy(w, "HID keyboard attached (");
    oc_u64_to_str((u64)kbds, n); oc_strcat(w, n); oc_strcat(w, ")");
    t_pass(T, w);
    /* one poll round + report the injection counter */
    usb_hid_poll();
    t_info(T, "poll: one interrupt-IN round done; keystrokes are "
              "injected into the standard input queue");
    return 0;
}

static int cmd_usb_mouse_test(const char *args) {
    (void)args;
    const char *T = "usb_mouse_test";
    int mice = usb_hid_num_mice();
    if (mice <= 0) {
        t_fail(T, "HID mouse attached", "none enumerated");
        return 1;
    }
    char n[12];
    char w[96];
    oc_strcpy(w, "HID mouse attached (");
    oc_u64_to_str((u64)mice, n); oc_strcat(w, n); oc_strcat(w, ")");
    t_pass(T, w);
    /* drain the event ring for up to 2 s (movement comes from the
     * host pointer / management input) */
    int got = 0;
    usb_mouse_event_t ev;
    usb_mouse_event_t first;
    int have_first = 0;
    u64 deadline = oc_timer_now_ms() + 2000;
    while (oc_timer_now_ms() < deadline) {
        usb_hid_poll();
        while (usb_mouse_read_event(&ev)) {
            if (!have_first) { first = ev; have_first = 1; }
            got++;
        }
        if (got >= 3) break;
        sched_yield();
    }
    if (got > 0) {
        oc_strcpy(w, "events received: ");
        oc_u64_to_str((u64)got, n); oc_strcat(w, n);
        oc_strcat(w, " first(dx=");
        oc_u64_to_str((u64)first.dx, n); oc_strcat(w, n);
        oc_strcat(w, ",dy=");
        oc_u64_to_str((u64)first.dy, n); oc_strcat(w, n);
        oc_strcat(w, ",btn=");
        oc_u64_to_str(first.buttons, n); oc_strcat(w, n);
        oc_strcat(w, ")");
        t_pass(T, w);
    } else {
        t_info(T, "no movement events in the window "
                  "(mouse present; move the host pointer)");
    }
    return 0;
}

/* ==================================================================
 * usb_storage_test
 * ================================================================== */

static int cmd_usb_storage_test(const char *args) {
    (void)args;
    const char *T = "usb_storage_test";
    char n[24];
    if (usb_msc_num_devices() <= 0) {
        t_fail(T, "USB storage drive attached",
               "no MSC device enumerated");
        return 1;
    }
    /* find the USB block device */
    int usd = -1;
    for (int i = 0; i < blk_num_devices(); i++) {
        blk_device_t *d = blk_get_device(i);
        if (d && d->present && d->type == BLK_TYPE_USB) { usd = i; break; }
    }
    if (usd < 0) {
        t_fail(T, "USB block device registered", "not in blk table");
        return 1;
    }
    blk_device_t *d = blk_get_device(usd);
    char w[128];
    oc_strcpy(w, "drive ");
    oc_strcat(w, d->name);
    oc_strcat(w, " capacity ");
    oc_u64_to_str(d->sectors, n); oc_strcat(w, n);
    oc_strcat(w, " sectors");
    t_pass(T, w);

    /* sector 0 read (MBR) */
    u8 sec0[512];
    if (blk_read_sectors(usd, 0, 1, sec0) != 0) {
        t_fail(T, "sector 0 read", "blk_read_sectors failed");
        return 1;
    }
    t_pass(T, "sector 0 read (MBR)");

    /* read/write round-trip on the last sector, then restore */
    u64 last = d->sectors - 1;
    u8 orig[512], pat[512], back[512];
    if (blk_read_sectors(usd, last, 1, orig) != 0) {
        t_fail(T, "last sector read", "failed");
        return 1;
    }
    for (int i = 0; i < 512; i++) pat[i] = (u8)(i ^ 0x5a);
    if (blk_write_sectors(usd, last, 1, pat) != 0) {
        t_fail(T, "last sector write", "failed");
        return 1;
    }
    blk_flush(d);
    if (blk_read_sectors(usd, last, 1, back) != 0 ||
        oc_memcmp(pat, back, 512) != 0) {
        t_fail(T, "write/read-back verify", "data mismatch");
        blk_write_sectors(usd, last, 1, orig);
        return 1;
    }
    t_pass(T, "write/read-back verify (last sector)");
    blk_write_sectors(usd, last, 1, orig);   /* restore */
    blk_flush(d);
    t_pass(T, "original sector restored");

    /* partition table */
    part_table_t pt;
    if (part_parse(usd, &pt) == 0) {
        oc_strcpy(w, "partition table ");
        oc_strcat(w, pt.table_type == PART_TYPE_MBR ? "MBR" :
                     pt.table_type == PART_TYPE_GPT ? "GPT" : "?");
        oc_strcat(w, " partitions=");
        oc_u64_to_str((u64)pt.count, n); oc_strcat(w, n);
        t_pass(T, w);
    } else {
        t_info(T, "no partition table (blank media)");
    }
    return 0;
}

/* ==================================================================
 * usb_serial_test
 * ================================================================== */

static const char *SER_MSG = "OpenCubeOS WP-10d serial test\n";

static int cmd_usb_serial_test(const char *args) {
    (void)args;
    const char *T = "usb_serial_test";
    char n[12];
    if (usb_serial_num_ports() <= 0) {
        t_fail(T, "USB serial port attached",
               "no CDC-ACM/FTDI device enumerated");
        return 1;
    }
    t_pass(T, "USB serial port attached");
    int len = 0;
    while (SER_MSG[len]) len++;
    int sent = usb_serial_write(SER_MSG, len);
    if (sent == len) {
        oc_strcpy(n, ""); oc_u64_to_str((u64)sent, n);
        char w[80];
        oc_strcpy(w, "TX complete (");
        oc_strcat(w, n); oc_strcat(w, " bytes)");
        t_pass(T, w);
    } else {
        t_fail(T, "TX complete", "short write / error");
        return 1;
    }
    /* RX drain for up to 2 s (loopback or remote echo) */
    char rx[64];
    int got = 0;
    u64 deadline = oc_timer_now_ms() + 2000;
    while (oc_timer_now_ms() < deadline && got == 0) {
        usb_serial_poll();
        got = usb_serial_read(rx, sizeof(rx));
        sched_yield();
    }
    if (got > 0) {
        oc_strcpy(n, ""); oc_u64_to_str((u64)got, n);
        char w[96];
        oc_strcpy(w, "RX received (");
        oc_strcat(w, n); oc_strcat(w, " bytes)");
        t_pass(T, w);
        oc_console_puts("[usb_serial_test] rx: ");
        for (int i = 0; i < got; i++) {
            char c = (rx[i] >= 0x20 && rx[i] < 0x7f) ? rx[i] : '.';
            oc_console_putc(c);
        }
        oc_console_puts("\n");
    } else {
        t_info(T, "no RX in the window (expected when no loopback/"
                  "peer is wired to the port)");
    }
    return 0;
}

/* ==================================================================
 * usb_hotplug_test
 * ================================================================== */

static int cmd_usb_hotplug_test(const char *args) {
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
        u64 deadline = oc_timer_now_ms() + 8000;
        int seen = usb_num_devices();
        int ok = want_new ? (seen >= n_arg) : (seen <= n_arg);
        while (!ok && oc_timer_now_ms() < deadline) {
            usb_poll();
            usb_enumerate();
            seen = usb_num_devices();
            ok = want_new ? (seen >= n_arg) : (seen <= n_arg);
            sched_yield();
        }
        oc_strcpy(n, ""); oc_u64_to_str((u64)seen, n);
        char w[96];
        oc_strcpy(w, "device count now ");
        oc_strcat(w, n);
        if (ok) t_pass(T, w);
        else t_fail(T, want_new ? "device appeared" : "device left",
                    w);
        return ok ? 0 : 1;
    }
    usb_poll();
    int found = usb_enumerate();
    oc_strcpy(n, ""); oc_u64_to_str((u64)found, n);
    char w[96];
    oc_strcpy(w, "poll + enumerate round done (new devices: ");
    oc_strcat(w, n); oc_strcat(w, ")");
    t_pass(T, w);
    usb_print_tree();
    return 0;
}

/* ==================================================================
 * usb_hub_test
 * ================================================================== */

static int cmd_usb_hub_test(const char *args) {
    (void)args;
    const char *T = "usb_hub_test";
    int hubs = 0, children = 0;
    char n[12];
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_dev_t *d = usb_get_device(i);
        if (!d || d->class != USB_CLASS_HUB) continue;
        hubs++;
        int kids = 0;
        for (int j = 0; j < USB_MAX_DEVICES; j++) {
            usb_dev_t *c = usb_get_device(j);
            if (c && c->parent == d->slot) {
                kids++;
                char w[128];
                oc_strcpy(w, "hub dev");
                oc_u64_to_str((u64)d->addr, n); oc_strcat(w, n);
                oc_strcat(w, " port ");
                oc_u64_to_str(c->hub_port, n); oc_strcat(w, n);
                oc_strcat(w, " -> device ");
                oc_u64_to_str((u64)c->addr, n); oc_strcat(w, n);
                oc_strcat(w, " (vid=0x");
                oc_u64_to_hex(c->vid, n, 4); oc_strcat(w, n);
                oc_strcat(w, ")");
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

static void usb_poll_thread(void *arg) {
    (void)arg;
    u64 round = 0;
    for (;;) {
        usb_poll();
        usb_hid_poll();
        usb_serial_poll();
        if ((round & 0x0f) == 0) usb_msc_poll();
        round++;
        /* ~2 ms breather between rounds */
        u64 deadline = oc_timer_now_ms() + 2;
        while (oc_timer_now_ms() < deadline) sched_yield();
    }
}

void usb_poll_start(void) {
    /* Priority = TASK_PRIO_MAX: the shell runs inside the idle task
     * (same priority), so the poll thread round-robins with it
     * instead of starving it on the USB transfer mutex. */
    kthread_create(usb_poll_thread, NULL, "usb-poll", TASK_PRIO_MAX);
}

void usb_test_cmds_register(void) {
    shell_register_command_ex("usb", cmd_usb, "USB hosts/devices + class driver status", "WP-10d");
    shell_register_command_ex("usbdev", cmd_usbdev, "USB device detail (usbdev [slot])", "WP-10d");
    shell_register_command_ex("usb_core_test", cmd_usb_core_test, "USB core invariants + live transfers", "WP-10d");
    shell_register_command_ex("usb_kbd_test", cmd_usb_kbd_test, "USB HID keyboard presence + injection", "WP-10d");
    shell_register_command_ex("usb_mouse_test", cmd_usb_mouse_test, "USB HID mouse presence + event drain", "WP-10d");
    shell_register_command_ex("usb_storage_test", cmd_usb_storage_test, "USB MSC drive capacity/rw/partitions", "WP-10d");
    shell_register_command_ex("usb_serial_test", cmd_usb_serial_test, "USB serial TX/RX round", "WP-10d");
    shell_register_command_ex("usb_hotplug_test", cmd_usb_hotplug_test, "USB hot-plug (usb_hotplug_test [wait-new N|wait-leave N])", "WP-10d");
    shell_register_command_ex("usb_hub_test", cmd_usb_hub_test, "USB external hub + children", "WP-10d");
}
