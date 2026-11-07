/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_spp.c
 *
 * Serial Port Profile (SPP): SDP service lookup for the RFCOMM server
 * channel, RFCOMM DLC open on that channel, and a raw byte stream
 * (send/recv + echo helper for the E2E suite).
 */
#include "types.h"
#include "driver_bt.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

static int (*g_app_recv)(u16 dlci, const u8 *data, u16 len);
static u16 g_open_dlci;

static int spp_rfcomm_input(u16 dlci, const u8 *data, u16 len);

int bt_spp_init(void)
{
    g_app_recv = 0;
    g_open_dlci = 0;
    (void)bt_rfcomm_recv_register(spp_rfcomm_input);
    return 0;
}

int bt_spp_recv_register(int (*recv)(u16 dlci, const u8 *data, u16 len))
{
    g_app_recv = recv;
    return 0;
}

static int spp_rfcomm_input(u16 dlci, const u8 *data, u16 len)
{
    g_open_dlci = dlci;
    if (g_app_recv) return g_app_recv(dlci, data, len);
    return 0;
}

int bt_spp_connect(const u8 addr[6], u16 *dlci_out)
{
    u16 channel;
    u16 dlci;

    if (!addr) return -1;
    if (bt_sdp_get_spp_channel(addr, &channel) != 0) return -2;
    bt_rfcomm_set_peer(0);
    {
        u16 h;
        if (bt_hci_handle_of_addr(addr, &h) != 0) return -3;
        bt_rfcomm_set_peer(h);
    }
    if (bt_rfcomm_open(channel, &dlci) != 0) return -4;
    g_open_dlci = dlci;
    if (dlci_out) *dlci_out = dlci;
    return 0;
}

int bt_spp_send(u16 dlci, const u8 *data, u16 len)
{
    return bt_rfcomm_send(dlci, data, len);
}

/* Echo helper: send, pump, compare (used by `bt test` + the E2E).     */
int bt_spp_echo(u16 dlci, const char *msg, char *resp_out, int cap)
{
    u8 rbuf[128];
    u64 dl;
    int got = 0;

    {
        u16 n = 0;
        while (msg[n]) n++;
        if (n == 0 || n > 100) return -1;
        if (bt_spp_send(dlci, (const u8 *)msg, n) != 0) return -2;
        for (int i = 0; i < (int)n; i++) rbuf[i] = 0;
    }
    dl = core_timer_now_ms() + 3000;
    for (;;) {
        (void)bt_hci_poll();
        /* the RFCOMM recv callback above routes to the tester; for the
         * echo helper, scan what the peer sent back through the same
         * DLCI via a small capture hook                              */
        {
            u16 cl = spp_capture_len();
            if (cl) {
                for (u16 i = 0; i < cl && i < (u16)sizeof(rbuf); i++)
                    rbuf[i] = spp_capture_byte(i);
                got = cl;
                spp_capture_clear();
                break;
            }
        }
        if (core_timer_now_ms() >= dl) break;
        core_sched_yield();
    }
    if (!got) return -3;
    if (resp_out && cap > 0) {
        int n = got;
        if (n > cap - 1) n = cap - 1;
        for (int i = 0; i < n; i++) resp_out[i] = (char)rbuf[i];
        resp_out[n] = '\0';
    }
    return 0;
}

/* capture hook (tiny, real bytes only)                                */
static u8 g_cap[128];
static u16 g_cap_len;

static int spp_capture_input(u16 dlci, const u8 *data, u16 len)
{
    (void)dlci;
    for (u16 i = 0; i < len && g_cap_len < (u16)sizeof(g_cap); i++)
        g_cap[g_cap_len++] = data[i];
    return 0;
}

u16 spp_capture_len(void) { return g_cap_len; }
u8  spp_capture_byte(u16 i) { return (i < g_cap_len) ? g_cap[i] : 0; }
void spp_capture_clear(void) { g_cap_len = 0; }

/* stack init hook: route RFCOMM data into the capture when no app     */
void bt_spp_register_capture(void)
{
    (void)bt_spp_recv_register(spp_capture_input);
}
