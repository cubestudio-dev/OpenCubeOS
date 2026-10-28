/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_rfcomm.c
 *
 * RFCOMM multiplexer (Bluetooth RFCOMM TS 07.10): SABM/UA/DISC/DM and
 * UIH frames on the L2CAP PSM 3 channel, DLCI allocation (channel<<1|1
 * for the initiator side), MCC subcommand frames (PN negotiation, MSC
 * modem status) on DLCI 0, and the real FCS polynomial (reflected
 * CRC-8, x^8+x^2+x+1, init 0xff, covering addr+ctrl for UIH and
 * addr+ctrl+len otherwise - the interop-proven Linux rfcomm scheme).
 */
#include "types.h"
#include "driver_bt.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

#define RFCOMM_MAX_DLCI 8
#define RFCOMM_MTU      512
#define RF_COMM_FRAME_MAX (RFCOMM_MTU + 8)

typedef enum rf_state {
    RF_IDLE = 0, RF_SABM_SENT, RF_OPEN,
} rf_state_t;

typedef struct rf_dlci {
    u8  used;
    u16 dlci;
    rf_state_t state;
} rf_dlci_t;

static rf_dlci_t g_dlci[RFCOMM_MAX_DLCI];
static u16 g_mux_cid;                    /* L2CAP channel of the mux    */
static u8  g_mux_open;
static u8  g_ctrl_open;                  /* DLCI 0 SABM answered        */
static u16 g_peer_handle;                /* ACL handle of the peer      */
static int (*g_recv_cb)(u16 dlci, const u8 *data, u16 len);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static void put_le16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

int bt_rfcomm_init(void)
{
    memset((void *)g_dlci, 0, sizeof(g_dlci));
    g_mux_cid = 0;
    g_mux_open = 0;
    g_recv_cb = 0;
    return 0;
}

int bt_rfcomm_recv_register(int (*recv)(u16 dlci, const u8 *data, u16 len))
{
    g_recv_cb = recv;
    return 0;
}

/* The ACL handle of the remote device (set before bt_rfcomm_open).    */
void bt_rfcomm_set_peer(u16 acl_handle)
{
    g_peer_handle = acl_handle;
}

u16 bt_rfcomm_peer(void) { return g_peer_handle; }

int bt_rfcomm_dlci_count(void)
{
    int n = 0;
    for (int i = 0; i < RFCOMM_MAX_DLCI; i++)
        if (g_dlci[i].used && g_dlci[i].state == RF_OPEN) n++;
    return n;
}

/* Real RFCOMM FCS (reflected CRC-8 over the covered octets).           */
u8 bt_rfcomm_fcs(const u8 *data, int len)
{
    u8 crc = 0xff;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (u8)((crc >> 1) ^ 0xe0) : (u8)(crc >> 1);
    }
    return crc;
}

/* length field: EA scheme (1 byte < 128, else 2 bytes)                 */
static int put_len(u8 *out, u16 len)
{
    if (len < 128) {
        out[0] = (u8)(0x80u | len);    /* EA=1, 7-bit length             */
        return 1;
    }
    out[0] = (u8)(len & 0x7f);         /* EA=0                           */
    out[1] = (u8)(len >> 7);
    return 2;
}

static int rf_frame_send(u8 ctrl, u16 dlci, const u8 *body, u16 blen)
{
    u8 f[RF_COMM_FRAME_MAX];
    u8 addr;
    int o = 0, lbytes;
    u8 fcs;

    if (blen > RFCOMM_MTU) return -2;
    if (!g_mux_open) return -1;
    addr = (u8)(0x03u | ((dlci & 0x3fu) << 2));   /* EA=1, CR=1, DLCI     */
    f[o++] = addr;
    f[o++] = ctrl;
    lbytes = put_len(f + o, blen);
    o += lbytes;
    for (u16 i = 0; i < blen; i++) f[o++] = body[i];
    /* UIH FCS covers addr+ctrl; other frame types addr+ctrl+len        */
    fcs = bt_rfcomm_fcs(f, (ctrl & 0xefu) == BT_RFCOMM_UIH ? 2
                          : 2 + lbytes);
    f[o++] = fcs;
    return bt_l2cap_send(g_mux_cid, f, (u16)o);
}

/* MCC command over DLCI 0 (type octet with EA|CR bits)                 */
static int rf_mcc_send(u8 mcc_type, const u8 *body, u16 blen)
{
    u8 f[RF_COMM_FRAME_MAX];
    int o = 0;
    u8 fcs;

    f[o++] = 0x03;                     /* addr: EA|CR|DLCI 0             */
    f[o++] = (u8)(BT_RFCOMM_UIH | 0x10);  /* UIH with P/F set            */
    f[o++] = (u8)(0x80u | (blen + 1)); /* EA len: mcc type + body        */
    f[o++] = mcc_type;
    for (u16 i = 0; i < blen; i++) f[o++] = body[i];
    fcs = bt_rfcomm_fcs(f, 2);         /* UIH: addr+ctrl                 */
    f[o++] = fcs;
    return bt_l2cap_send(g_mux_cid, f, (u16)o);
}

/* ------------------------------------------------------------------ */
/* mux + DLC open                                                      */
/* ------------------------------------------------------------------ */
int bt_rfcomm_open(u16 server_channel, u16 *dlci_out)
{
    u16 dlci = (u16)((server_channel & 0x1fu) << 1) | 1u;
    rf_dlci_t *d = 0;
    u64 dl;
    u8 f[8];
    u8 fcs;

    if (!server_channel || server_channel > 30) return -1;
    for (int i = 0; i < RFCOMM_MAX_DLCI; i++)
        if (!g_dlci[i].used) { d = &g_dlci[i]; break; }
    if (!d) return -2;

    /* mux channel first (PSM 3) when not open                          */
    if (!g_mux_open) {
        u16 cid;
        if (!g_peer_handle) return -8;   /* no peer bound: honest          */
        if (bt_l2cap_connect(g_peer_handle, BT_L2CAP_PSM_RFCOMM,
                             &cid) != 0)
            return -3;
        g_mux_cid  = cid;
        g_mux_open = 1;
        /* SABM on DLCI 0 (control channel)                             */
        f[0] = 0x03;
        f[1] = BT_RFCOMM_SABM;
        f[2] = 0x81;                   /* EA=1 len=0                     */
        fcs = bt_rfcomm_fcs(f, 3);
        f[3] = fcs;
        if (bt_l2cap_send(g_mux_cid, f, 4) != 0) return -4;
        /* UA for DLCI 0 arrives through the input path; pump           */
        dl = core_timer_now_ms() + 3000;
        for (;;) {
            (void)bt_rfcomm_poll();
            if (g_ctrl_open) break;
            if (core_timer_now_ms() >= dl) return -5;
        /* v2.0.0: timed wait on the SVC dispatch path - park on sti;hlt
         * (any interrupt advances the timer/IO); core_sched_yield here
         * would switch tasks inside an int 0x80 frame and deadlock (see
         * driver_cmos_rtc.c wait_counts). */
            __asm__ volatile("sti; hlt");
        }
    }

    /* SABM on the data DLCI                                            */
    d->used  = 1;
    d->dlci  = dlci;
    d->state = RF_SABM_SENT;
    f[0] = (u8)(0x03u | ((dlci & 0x3fu) << 2));
    f[1] = BT_RFCOMM_SABM;
    f[2] = 0x81;
    fcs = bt_rfcomm_fcs(f, 3);
    f[3] = fcs;
    if (bt_l2cap_send(g_mux_cid, f, 4) != 0) return -6;
    dl = core_timer_now_ms() + 3000;
    for (;;) {
        (void)bt_rfcomm_poll();
        if (d->state == RF_OPEN) {
            /* PN + MSC negotiation (real MCC flow)                     */
            u8 pn[8] = {
                (u8)(0x03u | ((dlci & 0x3fu) << 2)),
                0xe0,                  /* flow control mechanism          */
                0x07,                  /* priority                        */
                0x00,                  /* ack timer T1                    */
                0x00, 0x00,            /* max frame size (LE, set below)  */
                0x07,                  /* N200                            */
                0x01,                  /* window size                     */
            };
            put_le16(pn + 4, RFCOMM_MTU);
            (void)rf_mcc_send(BT_RFCOMM_PN_CMD, pn, 8);
            {
                u8 msc[2];
                msc[0] = (u8)(0x03u | ((dlci & 0x3fu) << 2));
                msc[1] = (u8)(BT_RFCOMM_MSC_DSR | BT_RFCOMM_MSC_RTR |
                              BT_RFCOMM_MSC_DCD);
                (void)rf_mcc_send(BT_RFCOMM_MSC_CMD, msc, 2);
            }
            if (dlci_out) *dlci_out = dlci;
            return 0;
        }
        if (core_timer_now_ms() >= dl) return -7;
        core_sched_yield();
    }
}

int bt_rfcomm_close(u16 dlci)
{
    rf_dlci_t *d = 0;
    u8 f[4];
    u8 fcs;

    for (int i = 0; i < RFCOMM_MAX_DLCI; i++)
        if (g_dlci[i].used && g_dlci[i].dlci == dlci) { d = &g_dlci[i]; break; }
    if (!d || d->state != RF_OPEN) return -1;
    f[0] = (u8)(0x03u | ((dlci & 0x3fu) << 2));
    f[1] = BT_RFCOMM_DISC;
    f[2] = 0x81;
    fcs = bt_rfcomm_fcs(f, 3);
    f[3] = fcs;
    (void)bt_l2cap_send(g_mux_cid, f, 4);
    memset((void *)d, 0, sizeof(*d));
    return 0;
}

int bt_rfcomm_send(u16 dlci, const u8 *data, u16 len)
{
    rf_dlci_t *d = 0;
    for (int i = 0; i < RFCOMM_MAX_DLCI; i++)
        if (g_dlci[i].used && g_dlci[i].dlci == dlci &&
            g_dlci[i].state == RF_OPEN) { d = &g_dlci[i]; break; }
    if (!d) return -1;
    return rf_frame_send(BT_RFCOMM_UIH, dlci, data, len);
}

/* ------------------------------------------------------------------ */
/* input path (L2CAP profile callback on PSM 3)                        */
/* ------------------------------------------------------------------ */
static int rf_l2cap_input(u16 local_cid, const u8 *data, u16 len)
{
    u8 addr, ctrl;
    u16 dlci, flen;
    int lbytes;

    (void)local_cid;
    if (len < 3) return -1;
    addr = data[0];
    ctrl = data[1];
    /* The C/R bit distinguishes command/response between the two
     * stations; UIH data delivery is role-independent, so frames are
     * processed regardless of it. */
    dlci = (u16)((addr >> 2) & 0x3fu);
    if (data[2] & 0x80u) { flen = data[2] & 0x7fu; lbytes = 1; }
    else {
        if (len < 4) return -3;
        flen = (u16)(data[2] | ((u16)data[3] << 7));
        lbytes = 2;
    }
    if ((u16)(3 + lbytes + flen) > len) return -4;

    switch (ctrl & 0xefu) {
    case BT_RFCOMM_UA: {
        for (int i = 0; i < RFCOMM_MAX_DLCI; i++) {
            if (g_dlci[i].used && g_dlci[i].dlci == dlci &&
                g_dlci[i].state == RF_SABM_SENT)
                g_dlci[i].state = RF_OPEN;
        }
        if (dlci == 0) g_ctrl_open = 1;
        break;
    }
    case BT_RFCOMM_DM:
        for (int i = 0; i < RFCOMM_MAX_DLCI; i++)
            if (g_dlci[i].used && g_dlci[i].dlci == dlci)
                g_dlci[i].state = RF_IDLE;
        break;
    case BT_RFCOMM_UIH: {
        /* frame = addr(1) ctrl(1) len(lbytes) data(flen) fcs(1):
         * the information field starts right after the length octets  */
        const u8 *body = data + 2 + lbytes;
        if (dlci == 0 && (ctrl & 0x10u) && flen >= 1) {
            /* MCC subcommand on the control channel                    */
            u8 mt = body[0];
            /* respond MSC command with MSC response (real flow)        */
            if (mt == BT_RFCOMM_MSC_CMD && flen >= 3) {
                u8 resp_body[2] = { body[1], body[2] };
                (void)rf_mcc_send(BT_RFCOMM_MSC_RESP, resp_body, 2);
            }
        } else if (dlci != 0 && flen && g_recv_cb) {
            return g_recv_cb(dlci, body, flen);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

/* pump: drain the HCI path until nothing is pending                   */
int bt_rfcomm_poll(void)
{
    return bt_hci_poll();
}

/* L2CAP registration helper (called once from bt_stack_init)          */
void bt_rfcomm_register_l2cap(void)
{
    (void)bt_l2cap_register_profile(BT_L2CAP_PSM_RFCOMM, rf_l2cap_input);
}
