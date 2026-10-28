/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_hid.c
 *
 * HID profile (Bluetooth HID spec): control channel on L2CAP PSM 0x11,
 * interrupt channel on PSM 0x13, GET_REPORT handshakes on the control
 * channel and DATA reports from the interrupt channel (boot protocol:
 * keyboard 8 bytes mods+keys, mouse buttons+dx+dy).
 */
#include "types.h"
#include "driver_bt.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

/* HID control-channel transaction types (HID spec 5.3)                */
#define HID_HDR_GET_REPORT      0x40    /* transaction << 4              */
#define HID_HDR_DATA            0xa0
#define HID_REPORT_TYPE_INPUT   0x01
#define HID_REPORT_TYPE_OUTPUT  0x02
#define HID_REPORT_TYPE_FEATURE 0x03

static u16 g_ctrl_cid, g_intr_cid;
static u8 g_last_report[64];
static u16 g_last_report_len;
static u8 g_connected;

static int hid_ctrl_input(u16 local_cid, const u8 *data, u16 len);
static int hid_intr_input(u16 local_cid, const u8 *data, u16 len);

int bt_hid_init(void)
{
    g_ctrl_cid = g_intr_cid = 0;
    g_last_report_len = 0;
    g_connected = 0;
    (void)bt_l2cap_register_profile(BT_L2CAP_PSM_HID_CTRL, hid_ctrl_input);
    (void)bt_l2cap_register_profile(BT_L2CAP_PSM_HID_INTR, hid_intr_input);
    return 0;
}

static int hid_ctrl_input(u16 local_cid, const u8 *data, u16 len)
{
    (void)local_cid;
    /* DATA response to GET_REPORT: hdr + report                       */
    if (len >= 2 && (data[0] & 0xf0u) == HID_HDR_DATA) {
        u16 n = (u16)(len - 1);
        if (n > (u16)sizeof(g_last_report)) n = (u16)sizeof(g_last_report);
        for (u16 i = 0; i < n; i++) g_last_report[i] = data[1 + i];
        g_last_report_len = n;
        return 0;
    }
    return -1;                          /* unsupported control msg       */
}

static int hid_intr_input(u16 local_cid, const u8 *data, u16 len)
{
    (void)local_cid;
    /* DATA report on the interrupt channel                             */
    if (len >= 1 && (data[0] & 0xf0u) == HID_HDR_DATA) {
        u16 n = (u16)(len - 1);
        if (n > (u16)sizeof(g_last_report)) n = (u16)sizeof(g_last_report);
        for (u16 i = 0; i < n; i++) g_last_report[i] = data[1 + i];
        g_last_report_len = n;
        return 0;
    }
    return -1;
}

int bt_hid_connect(const u8 addr[6])
{
    u16 handle;
    u16 cid;

    if (!addr) return -1;
    if (bt_hci_handle_of_addr(addr, &handle) != 0) return -2;
    if (bt_l2cap_connect(handle, BT_L2CAP_PSM_HID_CTRL, &cid) != 0)
        return -3;
    g_ctrl_cid = cid;
    if (bt_l2cap_connect(handle, BT_L2CAP_PSM_HID_INTR, &cid) != 0) {
        (void)bt_l2cap_disconnect(g_ctrl_cid);
        g_ctrl_cid = 0;
        return -4;
    }
    g_intr_cid  = cid;
    g_connected = 1;
    return 0;
}

int bt_hid_get_report(u8 report_type, u8 *report_out, u16 *len_out)
{
    u8 msg[2];
    u64 dl;

    if (!g_connected) return -1;
    msg[0] = (u8)(HID_HDR_GET_REPORT | (report_type & 0x3u));
    msg[1] = 0;                        /* no report id (boot protocol)  */
    if (bt_l2cap_send(g_ctrl_cid, msg, 2) != 0) return -2;
    dl = core_timer_now_ms() + 3000;
    for (;;) {
        (void)bt_hci_poll();
        if (g_last_report_len) {
            u16 n = g_last_report_len;
            if (n > *len_out) n = *len_out;
            for (u16 i = 0; i < n; i++) report_out[i] = g_last_report[i];
            *len_out = n;
            g_last_report_len = 0;
            return 0;
        }
        if (core_timer_now_ms() >= dl) return -3;
        /* v2.0.0: timed wait on the SVC dispatch path - park on sti;hlt
         * (any interrupt advances the timer/IO); core_sched_yield here
         * would switch tasks inside an int 0x80 frame and deadlock (see
         * driver_cmos_rtc.c wait_counts). */
        __asm__ volatile("sti; hlt");
    }
}

int bt_hid_last_report(u8 *report_out, u16 *len_out)
{
    if (!g_last_report_len) return -1;
    {
        u16 n = g_last_report_len;
        if (n > *len_out) n = *len_out;
        for (u16 i = 0; i < n; i++) report_out[i] = g_last_report[i];
        *len_out = n;
        return 0;
    }
}

int bt_hid_disconnect(void)
{
    if (!g_connected) return 0;
    (void)bt_l2cap_disconnect(g_ctrl_cid);
    (void)bt_l2cap_disconnect(g_intr_cid);
    g_ctrl_cid = g_intr_cid = 0;
    g_connected = 0;
    return 0;
}
