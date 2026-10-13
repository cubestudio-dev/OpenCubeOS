/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_sdp.c
 *
 * SDP client (Bluetooth SDP spec): ServiceSearchRequest for a UUID-16
 * followed by ServiceAttributeRequest for the ProtocolDescriptorList
 * (attribute 0x0004), then a real ProtocolDescriptorList walk to pull
 * the RFCOMM server channel for SPP.  L2CAP PSM 1, PDU header
 * {id u8, tid u16 BE, plen u16 BE}.
 */
#include "types.h"
#include "driver_bt.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

#define SDP_SSR_REQ     0x02
#define SDP_SSR_RESP    0x03
#define SDP_SAR_REQ     0x04
#define SDP_SAR_RESP    0x05

#define SDP_ATTR_PROTO_DESC_LIST 0x0004

/* SDP data element types (SDP spec 3.1.3): size index in bits 0-2     */
#define SDP_DE_UUID16   0x19
#define SDP_DE_U16      0x09
#define SDP_DE_U8       0x08
#define SDP_DE_DES8     0x35
#define SDP_DE_DES16    0x36

#define SDP_UUID_SPP    0x1108
#define SDP_UUID_RFCOMM 0x0003
#define SDP_UUID_L2CAP  0x0100

static u16 g_tid;

int  sdp_response_ready(void);
u16  sdp_response_len(void);
void sdp_response_copy(u8 *dst, u16 n);
void sdp_response_clear(void);

static u16 be16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] << 8) | (u32)p[3];
}
static void put_be16(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)(v & 0xff);
}
static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)((v >> 16) & 0xff);
    p[2] = (u8)((v >> 8) & 0xff);
    p[3] = (u8)(v & 0xff);
}

int bt_sdp_init(void)
{
    g_tid = 0;
    return 0;
}

static int sdp_transact(u16 handle, const u8 *req, u16 reqlen,
                        u8 req_pdu, u8 **resp_body, u16 *resp_len)
{
    static u8 out[256];
    u16 cid;
    u8 r[256];
    u16 rl;
    u64 dl;
    u8 want;

    if (bt_l2cap_connect(handle, BT_L2CAP_PSM_SDP, &cid) != 0) return -1;
    if (bt_l2cap_send(cid, req, reqlen) != 0) return -2;

    want = (u8)(req_pdu + 1);
    dl = core_timer_now_ms() + 3000;
    for (;;) {
        (void)bt_hci_poll();
        /* responses arrive via the L2CAP profile input (registered by
         * the stack init as the SDP pseudo-profile); implement the
         * wait inline: the stack routes SDP PDUs into sdp_input.     */
        if (sdp_response_ready()) {
            u16 n = sdp_response_len();
            if (n > (u16)sizeof(out)) n = (u16)sizeof(out);
            sdp_response_copy(out, n);
            if (out[0] != want) { sdp_response_clear(); return -3; }
            *resp_body = out + 5;      /* id(1) tid(2) plen(2)           */
            *resp_len  = (u16)(be16(out + 3));
            if (*resp_len > (u16)(n - 5)) *resp_len = (u16)(n - 5);
            sdp_response_clear();
            return 0;
        }
        if (core_timer_now_ms() >= dl) return -4;
        (void)r; (void)rl;
        core_sched_yield();
    }
}

/* ---- SDP pseudo-profile glue (PSM 1 responses) ----                  */
static u8 g_resp_buf[256];
static u16 g_resp_len;
static u8 g_resp_ready;

int sdp_response_ready(void) { return g_resp_ready; }
u16 sdp_response_len(void) { return g_resp_len; }
void sdp_response_copy(u8 *dst, u16 n)
{
    if (n > g_resp_len) n = g_resp_len;
    for (u16 i = 0; i < n; i++) dst[i] = g_resp_buf[i];
}
void sdp_response_clear(void) { g_resp_ready = 0; }

static int sdp_l2cap_input(u16 local_cid, const u8 *data, u16 len)
{
    (void)local_cid;
    if (g_resp_ready) return -1;
    if (len > (u16)sizeof(g_resp_buf)) len = (u16)sizeof(g_resp_buf);
    for (u16 i = 0; i < len; i++) g_resp_buf[i] = data[i];
    g_resp_len   = len;
    g_resp_ready = 1;
    return 0;
}

void bt_sdp_register_l2cap(void)
{
    (void)bt_l2cap_register_profile(BT_L2CAP_PSM_SDP, sdp_l2cap_input);
}

/* ------------------------------------------------------------------ */
/* Service search + attribute pull                                     */
/* ------------------------------------------------------------------ */
int bt_sdp_service_search(const u8 addr[6], u16 uuid16,
                          u32 *handles_out, int max, int *found_out)
{
    u8 req[24];
    int o = 0;
    u8 *body;
    u16 blen;
    int r;

    if (!addr) return -1;
    /* SDP PDU: id(1) tid(2 BE) plen(2 BE) params                       */
    req[o++] = SDP_SSR_REQ;
    req[o++] = (u8)(g_tid >> 8);
    req[o++] = (u8)(g_tid & 0xff);
    req[o++] = 0; req[o++] = 0;       /* plen placeholder                */
    /* service search pattern: DES{ UUID16 }                            */
    req[o++] = SDP_DE_DES8;
    req[o++] = 3;
    req[o++] = SDP_DE_UUID16;
    req[o++] = (u8)(uuid16 >> 8);
    req[o++] = (u8)(uuid16 & 0xff);
    put_be16(req + o, 8); o += 2;     /* max record count                */
    req[o++] = 0;                     /* continuation state: none        */
    put_be16(req + 3, (u16)(o - 5));
    g_tid++;

        {
        u16 h;
        if (bt_hci_handle_of_addr(addr, &h) != 0) return -8;
        r = sdp_transact(h, req, (u16)o, SDP_SSR_REQ, &body, &blen);
    }
    if (r != 0) return r;
    if (blen < 4) return -5;
    {
        int total = be16(body);
        int cur   = be16(body + 2);
        int n = (cur < max) ? cur : max;
        if (n > total) n = total;
        for (int i = 0; i < n; i++)
            handles_out[i] = be32(body + 4 + 4 * i);
        *found_out = n;
        return 0;
    }
}

/* ProtocolDescriptorList walker: find the RFCOMM entry's channel.     */
static int proto_list_find_rfcomm_channel(const u8 *p, u16 len,
                                          u16 *channel_out)
{
    u16 i = 0;
    if (len < 2 || p[i] != SDP_DE_DES8) return -1;
    {
        u16 des_len = p[i + 1];
        i = 2;
        while (i + 1 < len && i + 1 < (u16)(2 + des_len)) {
            u8 t = p[i];
            u8 l = p[i + 1];
            if (t != SDP_DE_DES8) return -2;
            /* each protocol entry: DES{ UUID16, [params...] }          */
            if (i + 2 + l > len) return -3;
            {
                const u8 *e = p + i + 2;
                if (l >= 3 && e[0] == SDP_DE_UUID16 &&
                    ((u16)((u16)e[1] << 8) | e[2]) == SDP_UUID_RFCOMM) {
                    /* next element inside this entry is the channel   */
                    if (l >= 5 && e[3] == SDP_DE_U8) {
                        *channel_out = e[4];
                        return 0;
                    }
                    if (l >= 5 && e[3] == SDP_DE_U16) {
                        *channel_out = be16(e + 4);
                        return 0;
                    }
                }
            }
            i = (u16)(i + 2 + l);
        }
    }
    return -4;
}

int bt_sdp_get_spp_channel(const u8 addr[6], u16 *server_channel_out)
{
    u8 req[32];
    int o = 0;
    u32 handles[4];
    int found = 0;
    u8 *body;
    u16 blen;
    int r;

    r = bt_sdp_service_search(addr, SDP_UUID_SPP, handles, 4, &found);
    if (r != 0) return r;
    if (found < 1) return -6;

    /* ServiceAttributeRequest: handle + attribute 0x0004               */
    req[o++] = SDP_SAR_REQ;
    req[o++] = (u8)(g_tid >> 8);
    req[o++] = (u8)(g_tid & 0xff);
    req[o++] = 0; req[o++] = 0;
    put_be32(req + o, handles[0]); o += 4;
    put_be16(req + o, 0xffff); o += 2;   /* max attr bytes                */
    req[o++] = SDP_DE_DES8;              /* attribute id list             */
    req[o++] = 6;
    req[o++] = SDP_DE_U16;
    req[o++] = 0x00; req[o++] = 0x04;    /* 0x0004                        */
    req[o++] = SDP_DE_U16;
    req[o++] = 0xff; req[o++] = 0xff;
    req[o++] = 0;                        /* continuation                  */
    put_be16(req + 3, (u16)(o - 5));
    g_tid++;

        {
        u16 h;
        if (bt_hci_handle_of_addr(addr, &h) != 0) return -8;
        r = sdp_transact(h, req, (u16)o, SDP_SAR_REQ, &body, &blen);
    }
    if (r != 0) return r;
    if (blen < 2) return -7;
    return proto_list_find_rfcomm_channel(body + 2, (u16)(blen - 2),
                                          server_channel_out);
}
