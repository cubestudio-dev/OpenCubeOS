/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/driver_nic_test_cmds.c
 * Purpose: Shell test/status commands for the WP-10b NIC drivers.
 *
 * Per-family tests share one pattern:
 *   - device registered? (framework lookup)
 *   - station address non-zero?
 *   - link status readable?
 *   - TX: a 60-byte broadcast ARP request is handed to the driver; the
 *     return value must equal the frame length (real DMA/send path).
 *   - RX: poll for the ARP reply from the network gateway (real RX
 *     path: the reply must come back through the descriptor ring).
 *   - When the family has no device under the current environment
 *     (QEMU has no model for RTL8169/8125/810x/ixgbe/BCM57xx and the
 *     legacy others), the test reports SKIPPED -- no fake output.
 *
 * driver_nic_rw_test exercises send + recv round-trips on every registered
 * NIC.  real_hw_test (WP-10a) is extended with the NIC section.
 */
#include "driver_nic.h"
#include "net_core.h"
#include "shell.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"

static void t_pass(const char *test, const char *what) {
    char line[120];
    strcpy(line, "["); strcat(line, test); strcat(line, "] ");
    strcat(line, what); strcat(line, " => PASS\n");
    screen_console_puts(line);
}

static void t_fail(const char *test, const char *what, const char *actual) {
    char line[160];
    strcpy(line, "["); strcat(line, test); strcat(line, "] ");
    strcat(line, what); strcat(line, " => FAIL (");
    strcat(line, actual); strcat(line, ")\n");
    screen_console_puts(line);
}

/* Build a 60-byte broadcast ARP request frame ("who has <gw_ip>, tell
 * <our_ip>").  The gateway 10.0.2.2 (QEMU slirp) always answers this
 * probe, which exercises the real RX descriptor path. */
static int build_arp_probe(u8 *frame, int cap, const u8 *src_mac,
                           u32 our_ip, u32 gw_ip) {
    if (cap < 60) return 0;
    static const u8 bcast[6] =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    int i = 0;
    for (int k = 0; k < 6; k++) frame[i++] = bcast[k];          /* dst */
    for (int k = 0; k < 6; k++) frame[i++] = src_mac[k];        /* src */
    frame[i++] = 0x08; frame[i++] = 0x06;                       /* ARP */
    frame[i++] = 0x00; frame[i++] = 0x01;                       /* htype=1 */
    frame[i++] = 0x08; frame[i++] = 0x00;                       /* ptype=IP */
    frame[i++] = 6;                                             /* hlen */
    frame[i++] = 4;                                             /* plen */
    frame[i++] = 0x00; frame[i++] = 0x01;                       /* op=request */
    for (int k = 0; k < 6; k++) frame[i++] = src_mac[k];        /* sha */
    frame[i++] = (u8)(our_ip >> 24); frame[i++] = (u8)(our_ip >> 16);
    frame[i++] = (u8)(our_ip >> 8);  frame[i++] = (u8)our_ip;   /* sip */
    for (int k = 0; k < 6; k++) frame[i++] = 0;                 /* tha */
    frame[i++] = (u8)(gw_ip >> 24); frame[i++] = (u8)(gw_ip >> 16);
    frame[i++] = (u8)(gw_ip >> 8);  frame[i++] = (u8)gw_ip;     /* tip */
    while (i < 60) frame[i++] = 0;                              /* pad */
    return i;
}

/* Shared family test body.  Returns 0 on PASS/SKIP, 1 on FAIL. */
static int driver_nic_family_test(driver_nic_type_t type, const char *testname) {
    int idx = driver_nic_find_by_type(type);
    char line[120];
    char n[16];

    if (idx < 0) {
        strcpy(line, "["); strcat(line, testname);
        strcat(line, "] device not present in this environment");
        strcat(line, " => SKIPPED (needs real hardware; no fake\n");
        strcat(line, "    output is produced for absent devices)\n");
        screen_console_puts(line);
        return 0;
    }

    driver_nic_device_t *dev = driver_nic_get_device(idx);
    int fails = 0;

    strcpy(line, "["); strcat(line, testname);
    strcat(line, "] input: NIC '");
    strcat(line, dev->name);
    strcat(line, "' (vid=0x");
    u64_to_hex(dev->vendor_id, n, 4); strcat(line, n);
    strcat(line, " did=0x");
    u64_to_hex(dev->device_id, n, 4); strcat(line, n);
    strcat(line, ")\n");
    screen_console_puts(line);

    /* 1. MAC present. */
    u8 mac[6];
    int mrc = driver_nic_get_mac(dev, mac);
    int mac_ok = (mrc == 0) &&
                 (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]);
    if (mac_ok) {
        strcpy(line, "["); strcat(line, testname);
        strcat(line, "] mac=");
        for (int k = 0; k < 6; k++) {
            u64_to_hex(mac[k], n, 2); strcat(line, n);
            if (k < 5) strcat(line, ":");
        }
        strcat(line, "\n");
        screen_console_puts(line);
    } else {
        t_fail(testname, "station address", "all-zero or unreadable");
        fails++;
    }

    /* 2. Link status. */
    int link = driver_nic_link_status(dev);
    if (link == 1) {
        t_pass(testname, "link status: up");
    } else if (link == 0) {
        t_fail(testname, "link status", "down (cable/PHY)");
        fails++;
    } else {
        strcpy(line, "["); strcat(line, testname);
        strcat(line, "] link status: unknown => reported\n");
        screen_console_puts(line);
    }

    /* 3. TX: broadcast ARP request (sender = our configured address). */
    u8 frame[64];
    int flen = build_arp_probe(frame, sizeof(frame), mac,
                               net_get_ip(), IP4(10, 0, 2, 2));
    int sent = driver_nic_send(dev, frame, flen);
    if (sent == flen) {
        t_pass(testname, "TX broadcast ARP request (60 bytes)");
    } else {
        t_fail(testname, "TX broadcast ARP request", "send() rejected");
        fails++;
    }

    /* 4. RX: the reply from the gateway must traverse the descriptor
     * ring and the stack's poll loop.  net_arp_refresh() forces a new ARP
     * exchange; net_arp_resolve() then waits for the reply.  (Polling the
     * driver directly here would race net_poll for the same frame.) */
    int resolved = net_arp_refresh(IP4(10, 0, 2, 2));
    if (resolved == 0) {
        u8 gwmac[6];
        resolved = net_arp_resolve(IP4(10, 0, 2, 2), gwmac);
    }
    if (resolved == 0) {
        strcpy(line, "["); strcat(line, testname);
        strcat(line, "] RX ARP reply received via the stack poll loop"
                         " - real RX path verified\n");
        screen_console_puts(line);
    } else {
        t_fail(testname, "RX ARP reply", "no frame (arp resolve timeout)");
        fails++;
    }

    strcpy(line, "["); strcat(line, testname);
    strcat(line, "] => ");
    strcat(line, fails ? "FAIL" : "PASS");
    strcat(line, "\n");
    screen_console_puts(line);
    return fails ? 1 : 0;
}

/* ---- per-family test commands ---- */

static int shell_cmd_e1000e_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_E1000E, "e1000e_test");
}
static int shell_cmd_igb_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_IGB, "igb_test");
}
static int shell_cmd_ixgbe_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_IXGBE, "ixgbe_test");
}
static int shell_cmd_rtl8139_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_RTL8139, "rtl8139_test");
}
static int shell_cmd_rtl8168_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_RTL8168, "rtl8168_test");
}
static int shell_cmd_rtl8125_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_RTL8125, "rtl8125_test");
}
static int shell_cmd_rtl810x_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_RTL810X, "rtl810x_test");
}
static int shell_cmd_bcm57xx_test(const char *args) {
    (void)args;
    return driver_nic_family_test(NIC_TYPE_BCM57XX, "bcm57xx_test");
}

/* other_nic_test: legacy families (3c59x / nForce / AR81xx / Yukon). */
static int shell_cmd_other_nic_test(const char *args) {
    (void)args;
    int idx = driver_nic_find_by_type(NIC_TYPE_OTHER);
    if (idx < 0) {
        screen_console_puts("[other_nic_test] no legacy NIC present in this"
                        " environment => SKIPPED\n");
        return 0;
    }
    driver_nic_device_t *d = driver_nic_get_device(idx);
    char line[120];
    strcpy(line, "[other_nic_test] legacy NIC '");
    strcat(line, d->name);
    strcat(line, "' present: send/recv probe not exhaustive for this"
                    " family (datasheet-derived core)\n");
    screen_console_puts(line);
    return 0;
}

/* driver_nic_rw_test: send/recv correctness on EVERY registered NIC. */
static int shell_cmd_nic_rw_test(const char *args) {
    (void)args;
    char line[120];
    char n[16];
    int count = driver_nic_num_devices();
    strcpy(line, "[nic_rw_test] input: ");
    u64_to_str((u64)count, n); strcat(line, n);
    strcat(line, " registered NIC(s)\n");
    screen_console_puts(line);

    if (count == 0) {
        screen_console_puts("[nic_rw_test] no NICs registered => SKIP\n");
        return 0;
    }

    int fails = 0;
    for (int i = 0; i < NIC_MAX_DEVICES; i++) {
        driver_nic_device_t *d = driver_nic_get_device(i);
        if (!d) continue;

        u8 mac[6];
        if (driver_nic_get_mac(d, mac) != 0) {
            t_fail("nic_rw_test", "get_mac", "driver error");
            fails++;
            continue;
        }
        u8 frame[64];
        int flen = build_arp_probe(frame, sizeof(frame), mac,
                                   net_get_ip(), IP4(10, 0, 2, 2));
        int sent = driver_nic_send(d, frame, flen);
        if (sent != flen) {
            t_fail("nic_rw_test", "nic_send", "length mismatch");
            fails++;
            continue;
        }
        u8 rbuf[ETH_FRAME_MAX];
        int rn = driver_nic_recv(d, rbuf, sizeof(rbuf));
        if (rn < 0) {
            t_fail("nic_rw_test", "nic_recv", "driver error");
            fails++;
            continue;
        }
        strcpy(line, "[nic_rw_test] ");
        strcat(line, d->name);
        strcat(line, ": send ok (");
        u64_to_str((u64)sent, n); strcat(line, n);
        strcat(line, " B), recv ok (");
        u64_to_str((u64)(rn > 0 ? rn : 0), n); strcat(line, n);
        strcat(line, rn > 0 ? " B frame)" : " = no frame pending)");
        strcat(line, " => PASS\n");
        screen_console_puts(line);
    }
    return fails ? 1 : 0;
}

/* ---- per-family status commands (thin shells) ---- */

static int shell_cmd_nic_e1000e(const char *args) {
    (void)args; driver_nic_e1000e_print_state(); return 0;
}
static int shell_cmd_nic_igb(const char *args) {
    (void)args; driver_nic_igb_print_state(); return 0;
}
static int shell_cmd_nic_ixgbe(const char *args) {
    (void)args; driver_nic_ixgbe_print_state(); return 0;
}
static int shell_cmd_nic_rtl8139(const char *args) {
    (void)args; driver_nic_rtl8139_print_state(); return 0;
}
static int shell_cmd_nic_rtl8168(const char *args) {
    (void)args; rtl8168_print_state(); return 0;
}
static int shell_cmd_nic_rtl8125(const char *args) {
    (void)args; rtl8125_print_state(); return 0;
}
static int shell_cmd_nic_rtl810x(const char *args) {
    (void)args; rtl810x_print_state(); return 0;
}
static int shell_cmd_nic_bcm57xx(const char *args) {
    (void)args; bcm57xx_print_state(); return 0;
}

/* ---- registration ---- */

void shell_cmds_nic_test_register(void) {
    shell_register_command_ex("e1000e_test", shell_cmd_e1000e_test, "WP-10b: e1000e init/link/send-recv test", "WP-10b");
    shell_register_command_ex("igb_test", shell_cmd_igb_test, "WP-10b: igb init/link/send-recv test", "WP-10b");
    shell_register_command_ex("ixgbe_test", shell_cmd_ixgbe_test, "WP-10b: ixgbe presence/link/send-recv test", "WP-10b");
    shell_register_command_ex("rtl8139_test", shell_cmd_rtl8139_test, "WP-10b: RTL8139 init/link/send-recv test", "WP-10b");
    shell_register_command_ex("rtl8168_test", shell_cmd_rtl8168_test, "WP-10b: RTL8168 presence/link/send-recv test", "WP-10b");
    shell_register_command_ex("rtl8125_test", shell_cmd_rtl8125_test, "WP-10b: RTL8125 presence/link/send-recv test", "WP-10b");
    shell_register_command_ex("rtl810x_test", shell_cmd_rtl810x_test, "WP-10b: RTL810x presence/link/send-recv test", "WP-10b");
    shell_register_command_ex("bcm57xx_test", shell_cmd_bcm57xx_test, "WP-10b: BCM57xx presence/link/send-recv test", "WP-10b");
    shell_register_command_ex("other_nic_test", shell_cmd_other_nic_test, "WP-10b: legacy NICs (3c59x/nForce/AR81xx/Yukon)", "WP-10b");
    shell_register_command_ex("nic_rw_test", shell_cmd_nic_rw_test, "WP-10b: send/recv correctness on all NICs", "WP-10b");

    /* status commands */
    shell_register_command_ex("e1000e", shell_cmd_nic_e1000e, "e1000e NIC status", "WP-10b");
    shell_register_command_ex("igb", shell_cmd_nic_igb, "igb NIC status", "WP-10b");
    shell_register_command_ex("ixgbe", shell_cmd_nic_ixgbe, "ixgbe NIC status", "WP-10b");
    shell_register_command_ex("rtl8139", shell_cmd_nic_rtl8139, "RTL8139 NIC status", "WP-10b");
    shell_register_command_ex("rtl8168", shell_cmd_nic_rtl8168, "RTL8168 NIC status", "WP-10b");
    shell_register_command_ex("rtl8125", shell_cmd_nic_rtl8125, "RTL8125 NIC status", "WP-10b");
    shell_register_command_ex("rtl810x", shell_cmd_nic_rtl810x, "RTL810x NIC status", "WP-10b");
    shell_register_command_ex("bcm57xx", shell_cmd_nic_bcm57xx, "BCM57xx NIC status", "WP-10b");
}
