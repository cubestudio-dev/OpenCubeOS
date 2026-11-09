/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_hci.c
 *
 * HCI core: the real command/event engine (Bluetooth Core Spec v5.x
 * volume 2 parts E/F).  Every response below is parsed from a real
 * event packet pulled from the attached transport - H4 UART or HCI-USB.
 * Nothing is synthesized here.
 */
#include "types.h"
#include "driver_bt.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

/* forward: ACL data flows into L2CAP when the layer is present        */
extern int bt_l2cap_acl_input(u16 handle, const u8 *data, u16 len);

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static const bt_transport_ops_t *g_tp;
static bt_conn_t      g_conns[BT_MAX_CONNS];
static bt_inquiry_result_t g_inquiry[BT_INQUIRY_MAX];
static int            g_inquiry_count;
static u8             g_local_bdaddr[6];
static u8             g_bdaddr_valid;
static u8             g_hci_ver, g_lmp_ver;
static u16            g_hci_rev, g_manufacturer, g_lmp_subver;
static u8             g_next_cmd_id;

int bt_hci_init(void)
{
    g_tp = 0;
    memset((void *)g_conns, 0, sizeof(g_conns));
    g_inquiry_count = 0;
    g_bdaddr_valid = 0;
    g_next_cmd_id = 1;
    return 0;
}

int bt_hci_attach_transport(const bt_transport_ops_t *ops)
{
    if (!ops || !ops->send || !ops->recv) return -1;
    if (g_tp) return -2;              /* one transport at a time          */
    g_tp = ops;
    return 0;
}

const char *bt_hci_transport_name(void)
{
    return g_tp ? g_tp->name : "none";
}

int bt_hci_present(void)
{
    return g_tp != 0 && g_bdaddr_valid;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static u16 le16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static void put_le16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

static bt_conn_t *conn_by_handle(u16 handle)
{
    for (int i = 0; i < BT_MAX_CONNS; i++)
        if (g_conns[i].used && g_conns[i].handle == handle)
            return &g_conns[i];
    return 0;
}

void bt_addr_to_str(const u8 addr[6], char out[18])
{
    /* HCI stores BD_ADDR with the least-significant octet first; the
     * printable form is most-significant-first.                        */
    static const char hexd[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 5; i >= 0; i--) {
        out[o++] = hexd[(addr[i] >> 4) & 0xf];
        out[o++] = hexd[addr[i] & 0xf];
        if (i) out[o++] = ':';
    }
    out[o] = '\0';
}

const char *bt_err_str(int status)
{
    switch (status & 0xffu) {
    case BT_ERR_SUCCESS:      return "ok";
    case BT_ERR_UNKNOWN_CMD:  return "unknown-cmd";
    case BT_ERR_NO_CONN:      return "unknown-conn";
    case BT_ERR_HW_FAIL:      return "hw-fail";
    case BT_ERR_TIMEOUT:      return "timeout";
    case BT_ERR_ACL_CONN_EXISTS: return "conn-exists";
    default:                  return "err";
    }
}

static void handle_event(const u8 *e, u16 len);

/* ------------------------------------------------------------------ */
/* Synchronous command path                                            */
/* ------------------------------------------------------------------ */
int bt_hci_cmd(u16 opcode, const u8 *params, u8 plen,
               u8 *resp, u16 *resp_len, u32 timeout_ms)
{
    u8 pkt[BT_ACL_MTU];
    u16 pkt_len;
    u64 deadline;

    if (!g_tp) return -1;              /* no transport: honest           */
    if (plen > 240) return -2;      /* HCI max param size 255       */

    g_next_cmd_id++;
    if (g_next_cmd_id == 0) g_next_cmd_id = 1;

    pkt[0] = BT_H4_COMMAND;
    pkt[1] = (u8)(opcode & 0xff);
    pkt[2] = (u8)(opcode >> 8);
    pkt[3] = plen;
    for (u8 i = 0; i < plen; i++) pkt[4 + i] = params[i];
    pkt_len = (u16)(4 + plen);

    if (g_tp->send(BT_H4_COMMAND, pkt + 1, (u16)(pkt_len - 1)) != 0)
        return -3;

    /* Async commands complete on their own event (Core 5.x vol 2):
     * Inquiry -> Inquiry_Complete, Create_Connection -> Connection_
     * Complete, Disconnect -> Disconnection_Complete, Remote_Name ->
     * Remote_Name_Request_Complete. */
    {
        u8 completing = 0;
        if (BT_OCF(opcode) == BT_OCF_INQUIRY &&
            BT_OGF(opcode) == BT_OGF_LINK_CTL)
            completing = BT_EVT_INQUIRY_COMPLETE;
        else if (BT_OCF(opcode) == BT_OCF_CREATE_CONN)
            completing = BT_EVT_CONN_COMPLETE;
        else if (BT_OCF(opcode) == BT_OCF_DISCONNECT)
            completing = BT_EVT_DISCONN_COMPLETE;
        else if (BT_OCF(opcode) == BT_OCF_REMOTE_NAME_REQ)
            completing = BT_EVT_REMOTE_NAME;

        deadline = core_timer_now_ms() + timeout_ms;
        for (;;) {
            u8 type, buf[BT_ACL_MTU];
            u16 len = 0;
            int r = g_tp->recv(&type, buf, (u16)sizeof(buf), &len);
            if (r < 0) return -4;
            if (r > 0 && type == BT_H4_EVENT && len >= 2) {
                u8 evt = buf[0];
                /* event layout: [code][plen][params...]               */
                if (evt == BT_EVT_CMD_COMPLETE && len >= 6 &&
                    le16(buf + 3) == opcode) {
                    /* params: num_pkts(1) opcode(2) status(1) resp... */
                    if (resp && resp_len) {
                        u16 n = (u16)(len - 6);
                        if (n > *resp_len) n = *resp_len;
                        for (u16 i = 0; i < n; i++) resp[i] = buf[6 + i];
                        *resp_len = n;
                    }
                    return buf[5];     /* status byte                 */
                }
                if (evt == BT_EVT_CMD_STATUS && len >= 6 &&
                    le16(buf + 4) == opcode) {
                    /* params: status(1) num_pkts(1) opcode(2)         */
                    if (buf[2] != BT_ERR_SUCCESS) return buf[2];
                    deadline = core_timer_now_ms() + timeout_ms;
                    continue;
                }
                if (completing && evt == completing) {
                    /* status is params[0] (buf[2]); the remaining
                     * params (handle for Create_Connection, name for
                     * Remote_Name) are the "response" payload.        */
                    u8 st = (len >= 3) ? buf[2] : BT_ERR_UNSPEC;
                    if (resp && resp_len) {
                        u16 n = (u16)(len - 3);
                        const u8 *src = buf + 3;
                        if (evt == BT_EVT_REMOTE_NAME && len >= 9) {
                            src = buf + 9;          /* after bdaddr      */
                            n = (u16)(len - 9);
                            if (n > BT_NAME_MAX) n = BT_NAME_MAX;
                        }
                        if (n > *resp_len) n = *resp_len;
                        for (u16 i = 0; i < n; i++) resp[i] = src[i];
                        *resp_len = n;
                    }
                    handle_event(buf, len);   /* state update           */
                    if (evt == BT_EVT_INQUIRY_COMPLETE) return 0;
                    return st;
                }
                /* everything else: dispatch, never drop               */
                if (evt != BT_EVT_CMD_COMPLETE && evt != BT_EVT_CMD_STATUS)
                    handle_event(buf, len);
            }
            if (core_timer_now_ms() >= deadline) return BT_ERR_TIMEOUT;
        /* v2.0.0: timed wait on the SVC dispatch path - park on sti;hlt
         * (any interrupt advances the timer/IO); core_sched_yield here
         * would switch tasks inside an int 0x80 frame and deadlock (see
         * driver_cmos_rtc.c wait_counts). */
            __asm__ volatile("sti; hlt");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Event pump                                                          */
/* ------------------------------------------------------------------ */
static void handle_event(const u8 *e, u16 len)
{
    switch (e[0]) {
    case BT_EVT_INQUIRY_RESULT:
        /* params: num(1) { bdaddr(6) psrm(1) psp(1) psm(1) cod(3)
         * clock(2) } - one response per event here                     */
        if (len >= 17 && g_inquiry_count < BT_INQUIRY_MAX) {
            bt_inquiry_result_t *r = &g_inquiry[g_inquiry_count];
            for (int i = 0; i < 6; i++) r->addr[i] = e[3 + i];
            r->page_scan_rep_mode = e[9];
            r->class_of_device[0] = e[12];
            r->class_of_device[1] = e[13];
            r->class_of_device[2] = e[14];
            r->clock_offset       = le16(e + 15);
            r->rssi = 0;
            g_inquiry_count++;
        }
        break;
    case BT_EVT_CONN_COMPLETE:
        /* params: status(1) handle(2) bdaddr(6) ...                    */
        if (len >= 12 && e[2] == BT_ERR_SUCCESS) {
            u16 handle = le16(e + 3);
            for (int i = 0; i < BT_MAX_CONNS; i++) {
                if (!g_conns[i].used) {
                    g_conns[i].used   = 1;
                    g_conns[i].handle = handle & 0x0fffu;
                    for (int b = 0; b < 6; b++)
                        g_conns[i].addr[b] = e[5 + b];
                    g_conns[i].state  = BT_CONN_OPEN;
                    break;
                }
            }
        }
        break;
    case BT_EVT_DISCONN_COMPLETE:
        /* params: status(1) handle(2) reason(1)                        */
        if (len >= 5) {
            u16 handle = le16(e + 3);
            bt_conn_t *c = conn_by_handle(handle);
            if (c) { memset((void *)c, 0, sizeof(*c)); }
        }
        break;
    default:
        break;
    }
}

int bt_hci_poll(void)
{
    int n = 0;
    if (!g_tp) return -1;
    for (;;) {
        u8 type, buf[BT_ACL_MTU];
        u16 len = 0;
        int r = g_tp->recv(&type, buf, (u16)sizeof(buf), &len);
        if (r <= 0) break;
        n++;
        if (type == BT_H4_EVENT && len >= 2) handle_event(buf, len);
        else if (type == BT_H4_ACL && len >= 4) {
            u16 hf  = le16(buf);
            u16 hdl = hf & 0x0fffu;
            u16 dlen = le16(buf + 2);
            if (dlen <= (u16)(len - 4))
                (void)bt_l2cap_acl_input(hdl, buf + 4, dlen);
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Named commands                                                      */
/* ------------------------------------------------------------------ */
int bt_hci_reset(void)
{
    u8 r[2];
    u16 rl = sizeof(r);
    int s = bt_hci_cmd(BT_OPCODE(BT_OGF_HOST_CTL, BT_OCF_RESET),
                       0, 0, r, &rl, 2000);
    if (s != 0) return s;
    /* state clean-up after reset (real semantics)                      */
    memset((void *)g_conns, 0, sizeof(g_conns));
    return 0;
}

int bt_hci_set_event_mask(u64 mask)
{
    u8 p[8], r[2];
    u16 rl = sizeof(r);
    for (int i = 0; i < 8; i++) p[i] = (u8)((mask >> (8 * i)) & 0xff);
    return bt_hci_cmd(BT_OPCODE(BT_OGF_HOST_CTL, BT_OCF_SET_EVENT_MASK),
                      p, 8, r, &rl, 2000);
}

int bt_hci_read_bd_addr(u8 addr[6])
{
    u8 r[8], p2[2];
    u16 rl = sizeof(r);
    int s = bt_hci_cmd(BT_OPCODE(BT_OGF_INFO_PARAM, BT_OCF_READ_BD_ADDR),
                       0, 0, r, &rl, 2000);
    if (s != 0) return s;
    if (rl < 6) return -2;
    for (int i = 0; i < 6; i++) {
        addr[i] = r[i];
        g_local_bdaddr[i] = r[i];
    }
    g_bdaddr_valid = 1;
    (void)p2;
    return 0;
}

int bt_hci_read_local_version(u8 *hci_ver, u16 *hci_rev,
                              u8 *lmp_ver, u16 *manufacturer,
                              u16 *lmp_subver)
{
    u8 r[10];
    u16 rl = sizeof(r);
    int s = bt_hci_cmd(BT_OPCODE(BT_OGF_INFO_PARAM,
                                 BT_OCF_READ_LOCAL_VERSION),
                       0, 0, r, &rl, 2000);
    if (s != 0) return s;
    if (rl < 8) return -2;
    g_hci_ver       = r[0];
    g_hci_rev       = le16(r + 1);
    g_lmp_ver       = r[3];
    g_manufacturer  = le16(r + 4);
    g_lmp_subver    = le16(r + 6);
    if (hci_ver)      *hci_ver      = g_hci_ver;
    if (hci_rev)      *hci_rev      = g_hci_rev;
    if (lmp_ver)      *lmp_ver      = g_lmp_ver;
    if (manufacturer) *manufacturer = g_manufacturer;
    if (lmp_subver)   *lmp_subver   = g_lmp_subver;
    return 0;
}

int bt_hci_write_scan_enable(u8 scan)
{
    u8 r[2];
    u16 rl = sizeof(r);
    return bt_hci_cmd(BT_OPCODE(BT_OGF_HOST_CTL,
                                BT_OCF_WRITE_SCAN_ENABLE),
                      &scan, 1, r, &rl, 2000);
}

int bt_hci_inquiry_start(void)
{
    /* GIAC (0x9e8b33) LAP, unlimited responses, 5.12 s window          */
    u8 p[5] = { 0x33, 0x8b, 0x9e, 0x05, 0x00 };
    u8 r[2];
    u16 rl = sizeof(r);
    g_inquiry_count = 0;
    return bt_hci_cmd(BT_OPCODE(BT_OGF_LINK_CTL, BT_OCF_INQUIRY),
                      p, 5, r, &rl, 8000);
}

int bt_hci_inquiry_results(bt_inquiry_result_t *out, int max)
{
    int n = (g_inquiry_count < max) ? g_inquiry_count : max;
    for (int i = 0; i < n; i++) out[i] = g_inquiry[i];
    return n;
}

int bt_hci_create_connection(const u8 addr[6], u16 *handle_out)
{
    u8 p[13];
    u8 r[4];
    u16 rl = sizeof(r);
    for (int i = 0; i < 6; i++) p[i] = addr[i];
    put_le16(p + 6, 0x0008);          /* packet type DM1/DH1 bits        */
    p[8]  = 0x01;                     /* page scan repetition mode       */
    p[9]  = 0x00;                     /* reserved                        */
    put_le16(p + 10, 0x0000);         /* clock offset unknown            */
    p[12] = 0x00;                     /* allow role switch               */
    {
        int s = bt_hci_cmd(BT_OPCODE(BT_OGF_LINK_CTL, BT_OCF_CREATE_CONN),
                           p, 13, r, &rl, 8000);
        if (s != 0) return s;
        if (rl < 2) return -2;
        if (handle_out) *handle_out = le16(r) & 0x0fffu;
        return 0;
    }
}

int bt_hci_disconnect(u16 handle)
{
    u8 p[3], r[2];
    u16 rl = sizeof(r);
    put_le16(p, handle);
    p[2] = 0x13;                      /* remote closed connection        */
    return bt_hci_cmd(BT_OPCODE(BT_OGF_LINK_CTL, BT_OCF_DISCONNECT),
                      p, 3, r, &rl, 5000);
}

int bt_hci_remote_name(const u8 addr[6], char *name_out, int cap)
{
    u8 p[6], r[250];
    u16 rl = (u16)sizeof(r);
    for (int i = 0; i < 6; i++) p[i] = addr[i];
    {
        int s = bt_hci_cmd(BT_OPCODE(BT_OGF_LINK_CTL,
                                     BT_OCF_REMOTE_NAME_REQ),
                           p, 6, r, &rl, 5000);
        if (s != 0) return s;
        if (name_out && cap > 0) {
            int n = rl;
            if (n > cap - 1) n = cap - 1;
            if (n > BT_NAME_MAX) n = BT_NAME_MAX;
            for (int i = 0; i < n; i++)
                name_out[i] = (char)r[i];
            name_out[n] = '\0';
        }
        return 0;
    }
}

int bt_hci_read_rssi(u16 handle, i8 *rssi_out)
{
    u8 p[2], r[3];
    u16 rl = sizeof(r);
    put_le16(p, handle);
    {
        int s = bt_hci_cmd(BT_OPCODE(BT_OGF_STATUS_PARAM, BT_OCF_READ_RSSI),
                           p, 2, r, &rl, 2000);
        if (s != 0) return s;
        if (rl < 1) return -2;
        if (rssi_out) *rssi_out = (i8)r[0];
        return 0;
    }
}

int bt_hci_send_acl(u16 handle, const u8 *data, u16 len)
{
    u8 buf[4 + BT_ACL_MTU];

    if (!g_tp) return -1;
    if (len > BT_ACL_MTU) return -2;
    /* ONE framed send: the transport prepends the H4 type byte, so
     * header and payload must go out in a single call (two calls would
     * emit two type bytes and desync the H4 stream).                  */
    put_le16(buf, handle);            /* flags 0 (first/auto-flush)      */
    put_le16(buf + 2, len);
    for (u16 i = 0; i < len; i++) buf[4 + i] = data[i];
    return g_tp->send(BT_H4_ACL, buf, (u16)(4 + len));
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */
int bt_hci_conn_count(void)
{
    int n = 0;
    for (int i = 0; i < BT_MAX_CONNS; i++) if (g_conns[i].used) n++;
    return n;
}

bt_conn_t *bt_hci_conn_get(int idx)
{
    int n = 0;
    for (int i = 0; i < BT_MAX_CONNS; i++) {
        if (g_conns[i].used) {
            if (n == idx) return &g_conns[i];
            n++;
        }
    }
    return 0;
}

int bt_hci_handle_of_addr(const u8 addr[6], u16 *handle_out)
{
    for (int i = 0; i < BT_MAX_CONNS; i++) {
        if (g_conns[i].used && g_conns[i].state == BT_CONN_OPEN) {
            int same = 1;
            for (int b = 0; b < 6; b++)
                if (g_conns[i].addr[b] != addr[b]) { same = 0; break; }
            if (same) {
                if (handle_out) *handle_out = g_conns[i].handle;
                return 0;
            }
        }
    }
    return -1;
}

int bt_hci_inquiry_count(void) { return g_inquiry_count; }

const bt_inquiry_result_t *bt_hci_inquiry_get(int idx)
{
    return (idx >= 0 && idx < g_inquiry_count) ? &g_inquiry[idx] : 0;
}

/* ------------------------------------------------------------------ */
/* Whole-stack init (called once from kernel/main.c)                   */
/* ------------------------------------------------------------------ */
int bt_stack_init(void)
{
    bt_hci_init();
    bt_l2cap_init();
    bt_rfcomm_init();
    bt_sdp_init();
    bt_spp_init();
    bt_hid_init();
    bt_rfcomm_register_l2cap();
    bt_sdp_register_l2cap();
    bt_spp_register_capture();
    /* 3.0.0-WP03 G3: the btusb class driver self-declares
     * (OC_DRIVER_DECL in driver_bt_usb.c) and was collected by the
     * USB core before host enumeration - no registration here. */
    /* transport choice: a live HCI-USB dongle wins, else the H4 UART  */
    if (bt_usb_present())
        return bt_hci_attach_transport(bt_usb_ops());
    if (bt_h4_init() == 0)
        return bt_hci_attach_transport(bt_h4_ops());
    return -1;                          /* no transport: honest           */
}
