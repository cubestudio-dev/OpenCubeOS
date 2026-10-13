/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_l2cap.c
 *
 * L2CAP layer (Bluetooth Core Spec v5.x vol 3.A): signaling channel
 * (CID 1) with Connection/Config/Disconnection/Echo commands and
 * dynamic channels for the profiles.  PDUs are single-segment (the
 * E2E HCI MTU keeps them under the ACL buffer size).
 *
 * Signaling responses land in a response box filled by the input
 * dispatcher; synchronous callers pump bt_hci_poll() until their
 * signaling id answers.  No response is ever synthesized.
 */
#include "types.h"
#include "driver_bt.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

#define L2CAP_MAX_CHANNELS 8
#define L2CAP_CID_DYNAMIC_BASE 0x0040

typedef enum l2_state {
    L2_CLOSED = 0, L2_WAIT_CONN_RESP, L2_WAIT_CFG, L2_OPEN,
} l2_state_t;

typedef struct l2_chan {
    u8  used;
    u16 local_cid;
    u16 remote_cid;
    u16 handle;
    u16 psm;
    l2_state_t state;
} l2_chan_t;

static l2_chan_t g_chans[L2CAP_MAX_CHANNELS];
static u16 g_next_cid = L2CAP_CID_DYNAMIC_BASE;
static u8  g_sig_id;

/* signaling response box                                              */
static struct {
    u8  id;
    u8  code;
    u8  data[64];
    u16 len;
    u8  ready;
} g_resp;

typedef struct l2_profile {
    u16 psm;
    int (*recv)(u16 local_cid, const u8 *data, u16 len);
} l2_profile_t;
static l2_profile_t g_profiles[4];
static int g_profile_count;

int bt_l2cap_init(void)
{
    memset((void *)g_chans, 0, sizeof(g_chans));
    g_profile_count = 0;
    g_next_cid = L2CAP_CID_DYNAMIC_BASE;
    g_sig_id   = 0;
    g_resp.ready = 0;
    return 0;
}

int bt_l2cap_register_profile(u16 psm,
                              int (*recv)(u16 local_cid,
                                          const u8 *data, u16 len))
{
    if (!recv || g_profile_count >= (int)(sizeof(g_profiles) /
                                          sizeof(g_profiles[0])))
        return -1;
    g_profiles[g_profile_count].psm  = psm;
    g_profiles[g_profile_count].recv = recv;
    return g_profile_count++;
}

static l2_chan_t *chan_by_local_cid(u16 cid)
{
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (g_chans[i].used && g_chans[i].local_cid == cid)
            return &g_chans[i];
    return 0;
}

static l2_chan_t *chan_by_handle_psm(u16 handle, u16 psm)
{
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (g_chans[i].used && g_chans[i].handle == handle &&
            g_chans[i].psm == psm)
            return &g_chans[i];
    return 0;
}

/* Debug view for the `bt` command: dump every channel's cid/psm/state */
void bt_l2cap_dump(char *out, int outlen)
{
    static const char *st[] = { "closed", "wait-conn", "wait-cfg", "open" };
    int o = 0;
    if (outlen > 0) out[0] = '\0';
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (!g_chans[i].used) continue;
        {
            char n[8];
            const char *pre = "  cid ";
            int k = 0;
            while (pre[k] && o < outlen - 1) out[o++] = pre[k++];
            u64_to_hex(g_chans[i].local_cid, n, 4);
            k = 0;
            while (n[k] && o < outlen - 1) out[o++] = n[k++];
            const char *m = " psm ";
            k = 0;
            while (m[k] && o < outlen - 1) out[o++] = m[k++];
            u64_to_hex(g_chans[i].psm, n, 4);
            k = 0;
            while (n[k] && o < outlen - 1) out[o++] = n[k++];
            const char *sp = " ";
            k = 0;
            while (sp[k] && o < outlen - 1) out[o++] = sp[k++];
            k = 0;
            while (st[g_chans[i].state][k] && o < outlen - 1)
                out[o++] = st[g_chans[i].state][k++];
            if (o < outlen - 2) out[o++] = '\n';
        }
    }
    out[o] = '\0';
}

int bt_l2cap_channel_count(void)
{
    int n = 0;
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) if (g_chans[i].used) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Wire helpers                                                        */
/* ------------------------------------------------------------------ */
static u16 le16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static void put_le16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

static u8 next_sig_id(void)
{
    g_sig_id++;
    if (g_sig_id == 0) g_sig_id = 1;
    return g_sig_id;
}

static int sig_send(u16 handle, u8 code, u8 id, const u8 *data, u16 len)
{
    u8 pkt[8 + 64];
    if (len > 64) return -2;
    put_le16(pkt, (u16)(4 + len));
    put_le16(pkt + 2, BT_L2CAP_SIG_CID);
    pkt[4] = code;
    pkt[5] = id;
    put_le16(pkt + 6, len);
    for (u16 i = 0; i < len; i++) pkt[8 + i] = data[i];
    return bt_hci_send_acl(handle, pkt, (u16)(8 + len));
}

static int data_send(u16 handle, u16 cid, const u8 *data, u16 len)
{
    /* ONE ACL packet: L2CAP header + payload in a single framed send
     * (two sends would emit two independent ACL packets and the peer
     * would read the L2CAP header as the whole PDU).                  */
    u8 buf[4 + BT_ACL_MTU];
    if (len > BT_ACL_MTU) return -2;
    put_le16(buf, len);
    put_le16(buf + 2, cid);
    for (u16 i = 0; i < len; i++) buf[4 + i] = data[i];
    return bt_hci_send_acl(handle, buf, (u16)(4 + len));
}

/* ------------------------------------------------------------------ */
/* Signaling dispatcher                                                */
/* ------------------------------------------------------------------ */
static void sig_input(u16 handle, const u8 *p, u16 len)
{
    u8 code, id;
    u16 clen;

    if (len < 4) return;
    code = p[0];
    id   = p[1];
    clen = le16(p + 2);
    if ((u16)(len - 4) < clen) return;
    p += 4;

    switch (code) {
    /* ---- responses: fill the box ----                                */
    case BT_L2CAP_CMD_CONN_RESP:
    case BT_L2CAP_CMD_CFG_RESP:
    case BT_L2CAP_CMD_DISC_RESP:
    case BT_L2CAP_CMD_ECHO_RESP:
        if (!g_resp.ready) {
            g_resp.id   = id;
            g_resp.code = code;
            g_resp.len  = (clen > 64) ? 64 : clen;
            for (u16 i = 0; i < g_resp.len; i++) g_resp.data[i] = p[i];
            g_resp.ready = 1;
        }
        break;

    /* ---- peer-initiated requests ----                                */
    case BT_L2CAP_CMD_CFG_REQ: {
        u8 d[6];
        if (clen < 4) return;
        put_le16(d, le16(p));          /* destination cid (ours)         */
        put_le16(d + 2, 0);
        put_le16(d + 4, BT_L2CAP_RESULT_SUCCESS);
        (void)sig_send(handle, BT_L2CAP_CMD_CFG_RESP, id, d, 6);
        break;
    }
    case BT_L2CAP_CMD_DISC_REQ: {
        u8 d[4];
        l2_chan_t *c;
        if (clen < 4) return;
        c = chan_by_local_cid(le16(p + 2));
        put_le16(d, le16(p + 2));
        put_le16(d + 2, le16(p));
        (void)sig_send(handle, BT_L2CAP_CMD_DISC_RESP, id, d, 4);
        if (c) memset((void *)c, 0, sizeof(*c));
        break;
    }
    case BT_L2CAP_CMD_ECHO_REQ:
        (void)sig_send(handle, BT_L2CAP_CMD_ECHO_RESP, id, p, clen);
        break;
    default:
        break;
    }
}

/* ACL entry point (called by the HCI core for every ACL packet).      */
int bt_l2cap_acl_input(u16 handle, const u8 *data, u16 len)
{
    u16 pdu_len, cid;
    l2_chan_t *c;

    if (len < 4) return -1;
    pdu_len = le16(data);
    cid     = le16(data + 2);
    if (pdu_len > (u16)(len - 4)) return -2;

    if (cid == BT_L2CAP_SIG_CID) {
        sig_input(handle, data + 4, pdu_len);
        return 0;
    }
    c = chan_by_local_cid(cid);
    if (!c) return -3;
    if (c->state != L2_OPEN) return -4;
    for (int i = 0; i < g_profile_count; i++) {
        if (g_profiles[i].psm == c->psm)
            return g_profiles[i].recv(c->local_cid, data + 4, pdu_len);
    }
    return -5;
}

/* wait for the signaling response box to answer a given id            */
static int resp_wait(u8 id, u8 *code_out, u8 *data_out, u16 *len_out,
                     u32 timeout_ms)
{
    u64 dl = core_timer_now_ms() + timeout_ms;
    for (;;) {
        (void)bt_hci_poll();
        if (g_resp.ready && g_resp.id == id) {
            if (code_out) *code_out = g_resp.code;
            if (data_out && len_out) {
                u16 n = g_resp.len;
                if (n > *len_out) n = *len_out;
                for (u16 i = 0; i < n; i++) data_out[i] = g_resp.data[i];
                *len_out = n;
            }
            g_resp.ready = 0;
            return 0;
        }
        if (core_timer_now_ms() >= dl) return -1;
        core_sched_yield();
    }
}

/* ------------------------------------------------------------------ */
/* Outgoing channel setup                                              */
/* ------------------------------------------------------------------ */
int bt_l2cap_connect(u16 handle, u16 psm, u16 *local_cid_out)
{
    l2_chan_t *c = 0;
    u8 d[8], r[16], code = 0;
    u16 rl;
    u8 id;

    /* Idempotent client API: an already-open channel to the same
     * handle+PSM is reused (SDP runs two transactions on one channel). */
    c = chan_by_handle_psm(handle, psm);
    if (c) {
        if (c->state != L2_OPEN) return -2;
        if (local_cid_out) *local_cid_out = c->local_cid;
        return 0;
    }
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (!g_chans[i].used) { c = &g_chans[i]; break; }
    if (!c) return -3;

    memset((void *)c, 0, sizeof(*c));
    c->used      = 1;
    c->handle    = handle;
    c->psm       = psm;
    c->local_cid = g_next_cid++;
    c->state     = L2_WAIT_CONN_RESP;

    put_le16(d, psm);
    put_le16(d + 2, c->local_cid);
    id = next_sig_id();
    if (sig_send(handle, BT_L2CAP_CMD_CONN_REQ, id, d, 4) != 0) {
        memset((void *)c, 0, sizeof(*c));
        return -4;
    }
    rl = sizeof(r);
    if (resp_wait(id, &code, r, &rl, 5000) != 0 ||
        code != BT_L2CAP_CMD_CONN_RESP) {
        memset((void *)c, 0, sizeof(*c));
        return -5;
    }
    if (rl < 8) { memset((void *)c, 0, sizeof(*c)); return -6; }
    if (le16(r + 4) != BT_L2CAP_RESULT_SUCCESS) {
        memset((void *)c, 0, sizeof(*c));
        return (int)(-100 - le16(r + 4));   /* refusal code, honest        */
    }
    c->remote_cid = le16(r);
    c->state      = L2_WAIT_CFG;

    /* Configuration Request (no options) then wait for the response    */
    put_le16(d, c->remote_cid);
    put_le16(d + 2, 0);
    put_le16(d + 4, 0);
    put_le16(d + 6, 0);
    id = next_sig_id();
    if (sig_send(handle, BT_L2CAP_CMD_CFG_REQ, id, d, 8) != 0) {
        memset((void *)c, 0, sizeof(*c));
        return -7;
    }
    rl = sizeof(r);
    if (resp_wait(id, &code, r, &rl, 5000) != 0 ||
        code != BT_L2CAP_CMD_CFG_RESP) {
        memset((void *)c, 0, sizeof(*c));
        return -8;
    }
    if (rl >= 6 && le16(r + 4) != BT_L2CAP_RESULT_SUCCESS) {
        memset((void *)c, 0, sizeof(*c));
        return -9;
    }
    c->state = L2_OPEN;
    if (local_cid_out) *local_cid_out = c->local_cid;
    return 0;
}

int bt_l2cap_disconnect(u16 local_cid)
{
    l2_chan_t *c = chan_by_local_cid(local_cid);
    u8 d[4], r[8], code = 0;
    u16 rl;
    u8 id;

    if (!c || c->state != L2_OPEN) return -1;
    put_le16(d, c->remote_cid);
    put_le16(d + 2, c->local_cid);
    id = next_sig_id();
    (void)sig_send(c->handle, BT_L2CAP_CMD_DISC_REQ, id, d, 4);
    rl = sizeof(r);
    if (resp_wait(id, &code, r, &rl, 3000) == 0 &&
        code == BT_L2CAP_CMD_DISC_RESP) {
        memset((void *)c, 0, sizeof(*c));
        return 0;
    }
    memset((void *)c, 0, sizeof(*c));   /* force-closed on timeout        */
    return -2;
}

int bt_l2cap_echo(u16 handle, const u8 *data, u8 len,
                  u8 *resp, u16 *resp_len)
{
    u8 r[64], code = 0;
    u16 rl;
    u8 id = next_sig_id();

    if (len > 64) return -2;
    if (sig_send(handle, BT_L2CAP_CMD_ECHO_REQ, id, data, len) != 0)
        return -3;
    rl = (u16)sizeof(r);
    if (resp_wait(id, &code, r, &rl, 3000) != 0) return -4;
    if (code != BT_L2CAP_CMD_ECHO_RESP) return -5;
    if (resp && resp_len) {
        u16 n = rl;
        if (n > *resp_len) n = *resp_len;
        for (u16 i = 0; i < n; i++) resp[i] = r[i];
        *resp_len = n;
    }
    return 0;
}

int bt_l2cap_send(u16 local_cid, const u8 *data, u16 len)
{
    l2_chan_t *c = chan_by_local_cid(local_cid);
    if (!c || c->state != L2_OPEN) return -1;
    return data_send(c->handle, c->remote_cid, data, len);
}
