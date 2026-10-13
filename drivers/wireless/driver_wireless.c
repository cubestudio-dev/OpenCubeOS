/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_wireless.c
 *
 * 802.11 core: real on-air format handling and the STA-side state machine
 * shared by every wireless family driver in this work package.
 *
 * Format references (IEEE 802.11-2016 unless noted):
 *   - Frame Control field          clause 9.2.4.1.1 (figure 9-5)
 *   - Sequence Control             clause 9.2.4.4
 *   - Addressing (4 cases)         clause 9.3.2 / table 9-1
 *   - Beacon / Probe Resp body     clause 9.3.3.2 (figure 9-11)
 *   - Authentication / (Re)Assoc   clause 9.3.3.6 / 9.3.3.10 / 9.3.3.11
 *   - Information elements         clause 9.4.2 (table 9-77)
 *   - Capability Information       clause 9.4.1.4 (figure 9-7)
 *   - LLC/SNAP data encapsulation  clause 9.3.3.3 (AA-AA-03-00-00-00)
 *   - 2.4 GHz channel center freq  clause 16.3.8.2.2 (ch N = 2412 + 5(N-1),
 *     channel 14 center 2484 MHz, Japan DSSS only)
 *   - 5 GHz center freq            clause 21.3.14 (ch N = 5000 + 5N)
 *
 * No stubs: every function below either operates on real driver state or on
 * real on-air byte formats.  Where no hardware is present (QEMU has no
 * 802.11 models) the functions return honest negative codes instead of
 * pretending success.
 */
#include "types.h"
#include "driver_wireless.h"
#include "lib_string.h"
#include "mem_heap.h"

/* ------------------------------------------------------------------ */
/* Registry state                                                      */
/* ------------------------------------------------------------------ */
static const wl_family_ops_t *g_families[WL_MAX_FAMILIES];
static int                    g_family_count;
static wl_device_t            g_devices[WL_MAX_DEVICES];
static int                    g_device_count;
static wl_scan_result_t       g_scan_list[WL_MAX_SCAN_RESULTS];
static int                    g_scan_count;
static u16                    g_tx_seq_next;   /* per-STA sequence no.  */

/* Decapsulated data frames waiting for the NIC bridge to drain.
 * The ring is heap-allocated at driver_wireless_init() (which runs after
 * the kernel heap is up) so the kernel image stays inside the 0x400000
 * identity window enforced by the linker.ld ASSERT. */
#define WL_RX_RING             16
#define WL_RX_ETH_MAX          1522
static u8  *g_rx_ring;          /* WL_RX_RING * WL_RX_ETH_MAX bytes    */
static u16  g_rx_ring_len[WL_RX_RING];
static volatile u32 g_rx_head, g_rx_tail;

/* ------------------------------------------------------------------ */
/* Small helpers (freestanding; keep to lib_string semantics)          */
/* ------------------------------------------------------------------ */
static void wl_copy_mac(u8 *dst, const u8 *src)
{
    for (int i = 0; i < 6; i++) dst[i] = src[i];
}

static int wl_mac_eq(const u8 *a, const u8 *b)
{
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int wl_mac_is_bcast(const u8 *m)
{
    return (m[0] & m[1] & m[2] & m[3] & m[4] & m[5]) == 0xff;
}

static int wl_mac_is_zero(const u8 *m)
{
    return (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) == 0;
}

static u16 wl_le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static void wl_put_le16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

static void wl_put_be16(u8 *p, u16 v)
{
    p[0] = (u8)((v >> 8) & 0xff);
    p[1] = (u8)(v & 0xff);
}

static u16 wl_be16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* seq/append helper used by the frame builders */
static u16 wl_next_seq(void)
{
    u16 s = g_tx_seq_next;
    g_tx_seq_next = (u16)((g_tx_seq_next + 0x10) & 0xfff0);
    return s;
}

static char wl_hexdig(u8 v)
{
    return (char)((v < 10) ? ('0' + v) : ('a' + v - 10));
}

void wl_mac_to_str(const u8 *mac, char out[18])
{
    int i, o = 0;
    for (i = 0; i < 6; i++) {
        out[o++] = wl_hexdig((u8)(mac[i] >> 4));
        out[o++] = wl_hexdig((u8)(mac[i] & 0xf));
        if (i < 5) out[o++] = ':';
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* Frame Control parsing (clause 9.2.4.1.1)                            */
/* ------------------------------------------------------------------ */
int wl_parse_frame_ctl(const u8 *frame, u16 len, wl_frame_info_t *out)
{
    u16 fc, seq;
    u16 hdr_len;

    if (!frame || !out || len < 2) return -1;
    memset((void *)out, 0, sizeof(*out));

    fc = wl_le16(frame);
    out->proto_ver       = (u8)(fc & 0x3);
    if (out->proto_ver != 0) return -2;          /* only version 0 exists */
    out->type            = (u8)((fc >> 2) & 0x3);
    out->subtype         = (u8)((fc >> 4) & 0xf);
    out->to_ds           = (u8)((fc >> 8) & 1);
    out->from_ds         = (u8)((fc >> 9) & 1);
    out->more_frag       = (u8)((fc >> 10) & 1);
    out->retry           = (u8)((fc >> 11) & 1);
    out->pwr_mgmt        = (u8)((fc >> 12) & 1);
    out->more_data       = (u8)((fc >> 13) & 1);
    out->protected_frame = (u8)((fc >> 14) & 1);
    out->order           = (u8)((fc >> 15) & 1);

    if (len < 24) return -3;                      /* minimum MAC header   */

    wl_copy_mac(out->addr1, frame + 4);
    wl_copy_mac(out->addr2, frame + 10);
    wl_copy_mac(out->addr3, frame + 16);

    seq = wl_le16(frame + 22);
    out->frag_num = (u16)(seq & 0xf);
    out->seq_num  = (u16)(seq >> 4);

    hdr_len = 24;
    if (out->to_ds && out->from_ds) {
        if (len < 30) return -4;
        wl_copy_mac(out->addr4, frame + 24);
        hdr_len = 30;
    }
    /* QoS data carries a 2-byte QoS control after the address fields  */
    if (out->type == WL_TYPE_DATA &&
        (out->subtype & 0x08) && (out->subtype & 0x03) != 0x03) {
        if (len < (u16)(hdr_len + 2)) return -5;
        out->qos_present = 1;
        hdr_len = (u16)(hdr_len + 2);
    }

    out->body_off = hdr_len;
    if (len > hdr_len) out->body_len = (u16)(len - hdr_len);
    else               out->body_len = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* IE walk (clause 9.4.2: each element = id(1) len(1) data(len))       */
/* ------------------------------------------------------------------ */
int wl_walk_ie(const u8 *body, u16 body_len, int *pos,
               u8 *id_out, const u8 **data_out, u8 *len_out)
{
    int p = *pos;
    u8  elen;

    if (p < 0 || (u16)p >= body_len) return 0;    /* done                 */
    if ((u16)(p + 2) > body_len) return -1;       /* truncated header     */
    *id_out = body[p];
    elen    = body[p + 1];
    if ((u16)(p + 2 + elen) > body_len) return -2;/* truncated element    */
    *data_out = body + p + 2;
    *len_out  = elen;
    *pos = p + 2 + elen;
    return 1;
}

int wl_find_ie(const u8 *body, u16 body_len, u8 ie_id,
               const u8 **data_out, u8 *len_out)
{
    int pos = 0, r;
    u8  id, elen;
    const u8 *data;

    while ((r = wl_walk_ie(body, body_len, &pos, &id, &data, &elen)) > 0) {
        if (id == ie_id) {
            if (data_out) *data_out = data;
            if (len_out)  *len_out  = elen;
            return 1;
        }
    }
    return 0;
}

/* Maximum PHY rate out of a (possibly split) rates IE set, Mbps.
 * Rate units are 0.5 Mbps (clause 9.4.2.3); bit 7 = basic rate flag. */
static u8 wl_rates_max_mbps(const u8 *a, u8 alen, const u8 *b, u8 blen)
{
    u8 best = 0;
    u8 i;
    for (i = 0; i < alen; i++) {
        u8 mbps = (u8)((a[i] & 0x7f) / 2);
        if (mbps > best) best = mbps;
    }
    for (i = 0; i < blen; i++) {
        u8 mbps = (u8)((b[i] & 0x7f) / 2);
        if (mbps > best) best = mbps;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Beacon / Probe Response body parsing (clause 9.3.3.2 figure 9-11)   */
/* ------------------------------------------------------------------ */
int wl_parse_beacon(const u8 *frame, u16 len, wl_scan_result_t *out)
{
    wl_frame_info_t fi;
    const u8 *body;
    u16 blen;
    u32 tsf_hi;
    int pos = 0, r;
    u8  id, elen;
    const u8 *data;

    if (wl_parse_frame_ctl(frame, len, &fi) != 0) return -1;
    if (fi.type != WL_TYPE_MGMT) return -2;
    if (fi.subtype != WL_STYPE_BEACON && fi.subtype != WL_STYPE_PROBE_RESP)
        return -3;
    /* fixed fields: timestamp(8) + beacon interval(2) + capability(2) */
    if (fi.body_len < 12) return -4;

    memset((void *)out, 0, sizeof(*out));
    wl_copy_mac(out->bssid, fi.addr3);
    body = frame + fi.body_off;
    blen = fi.body_len;

    tsf_hi = (u32)(body[0] | (body[1] << 8) | (body[2] << 16) |
             ((u32)body[3] << 24));
    (void)tsf_hi;                       /* TSF low 32 kept for ordering  */
    out->beacon_interval = wl_le16(body + 8);
    out->capability      = wl_le16(body + 10);

    pos = 12;
    while ((r = wl_walk_ie(body, blen, &pos, &id, &data, &elen)) > 0) {
        switch (id) {
        case WL_IE_SSID:
            if (elen > WL_SSID_MAX) elen = WL_SSID_MAX;
            for (u8 i = 0; i < elen; i++) out->ssid[i] = (char)data[i];
            out->ssid[elen] = '\0';
            out->ssid_len = elen;
            break;
        case WL_IE_DS_PARAMS:
            if (elen >= 1) out->channel = data[0];
            break;
        case WL_IE_RSN:
            out->has_rsn = 1;
            break;
        case WL_IE_SUPP_RATES:
            out->max_rate_mbps = wl_rates_max_mbps(data, elen, 0, 0);
            break;
        case WL_IE_EXT_SUPP_RATES:
            out->max_rate_mbps =
                wl_rates_max_mbps(data, elen, 0, 0) > out->max_rate_mbps
                ? wl_rates_max_mbps(data, elen, 0, 0) : out->max_rate_mbps;
            break;
        case WL_IE_VENDOR:
            /* Microsoft OUI 00:50:f2 type 1 = WPA (RSN predecessor)   */
            if (elen >= 6 && data[0] == 0x00 && data[1] == 0x50 &&
                data[2] == 0xf2 && data[3] == 0x01)
                out->has_wpa = 1;
            break;
        default:
            break;
        }
    }
    if (r < 0) return -5;                         /* malformed IE run    */
    /* Probe responses may omit DS params: derive from RX channel by the
     * caller (rssi field doubles as the hw rx channel holder before
     * insert).  Beacons in 5 GHz bands likewise. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Frame builders (STA side, clause 9.3.3)                             */
/* ------------------------------------------------------------------ */
static int wl_mgmt_common(u8 *buf, u8 subtype, const u8 *dst,
                          const u8 *src, const u8 *bss, u16 seq)
{
    /* To DS=0 From DS=0 for STA-originated management frames           */
    u16 fc = (u16)(subtype << 4);                 /* type=0 (mgmt)       */
    wl_put_le16(buf, fc);
    wl_put_le16(buf + 2, 0);                      /* duration            */
    wl_copy_mac(buf + 4,  dst);                   /* addr1 = receiver    */
    wl_copy_mac(buf + 10, src);                   /* addr2 = transmitter */
    wl_copy_mac(buf + 16, bss);                   /* addr3 = BSSID       */
    wl_put_le16(buf + 22, (u16)((seq << 4) & 0xfff0));
    return 24;
}

int wl_build_probe_req(u8 *buf, const char *ssid, u8 channel)
{
    int o = 0, slen = 0, i;
    /* probe req: wildcard SSID allowed (zero-length SSID IE)           */
    while (ssid && ssid[slen] && slen < WL_SSID_MAX) slen++;
    o = wl_mgmt_common(buf, WL_STYPE_PROBE_REQ,
                       (const u8 *)"\xff\xff\xff\xff\xff\xff",
                       (const u8 *)"\x02\x00\x00\x00\x00\x00",
                       (const u8 *)"\xff\xff\xff\xff\xff\xff", wl_next_seq());
    if (o < 0) return o;
    /* SSID IE                                                          */
    buf[o++] = WL_IE_SSID;  buf[o++] = (u8)slen;
    for (i = 0; i < slen; i++) buf[o++] = (u8)ssid[i];
    /* Supported rates IE: 1, 2, 5.5, 11 basic + 6, 12, 24 (clause 16)  */
    buf[o++] = WL_IE_SUPP_RATES; buf[o++] = 8;
    buf[o++] = 0x82; buf[o++] = 0x84; buf[o++] = 0x8b; buf[o++] = 0x96;
    buf[o++] = 0x0c; buf[o++] = 0x18; buf[o++] = 0x30; buf[o++] = 0x60;
    /* Extended rates                                                   */
    buf[o++] = WL_IE_EXT_SUPP_RATES; buf[o++] = 4;
    buf[o++] = 0x24; buf[o++] = 0x48; buf[o++] = 0x6c; buf[o++] = 0x90;
    /* Request IE skipped; DS params when channel known (probe on a ch) */
    if (channel) {
        buf[o++] = WL_IE_DS_PARAMS; buf[o++] = 1; buf[o++] = channel;
    }
    return o;
}

int wl_build_auth_open(u8 *buf, const u8 *dst, const u8 *src,
                       const u8 *bss, u16 seq)
{
    int o = wl_mgmt_common(buf, WL_STYPE_AUTH, dst, src, bss, seq);
    if (o < 0) return o;
    wl_put_le16(buf + o, WL_AUTH_OPEN);  o += 2;  /* algorithm           */
    wl_put_le16(buf + o, 1);             o += 2;  /* transaction seq 1   */
    wl_put_le16(buf + o, WL_STATUS_SUCCESS); o += 2;
    return o;
}

int wl_build_assoc_req(u8 *buf, const u8 *dst, const u8 *src,
                       const u8 *bss, const char *ssid,
                       const u8 *rates, u8 n_rates, u16 capability)
{
    int o = 0, slen = 0, i;
    while (ssid && ssid[slen] && slen < WL_SSID_MAX) slen++;
    o = wl_mgmt_common(buf, WL_STYPE_ASSOC_REQ, dst, src, bss, wl_next_seq());
    if (o < 0) return o;
    wl_put_le16(buf + o, capability);    o += 2;  /* capability          */
    wl_put_le16(buf + o, 100);           o += 2;  /* listen interval     */
    buf[o++] = WL_IE_SSID; buf[o++] = (u8)slen;
    for (i = 0; i < slen; i++) buf[o++] = (u8)ssid[i];
    if (n_rates > WL_MAX_RATES) n_rates = WL_MAX_RATES;
    buf[o++] = WL_IE_SUPP_RATES; buf[o++] = n_rates;
    for (i = 0; i < n_rates; i++) buf[o++] = rates[i];
    return o;
}

int wl_parse_auth_resp(const u8 *body, u16 len, u16 *seq_out,
                       u16 *status_out)
{
    if (len < 6) return -1;
    if (seq_out)    *seq_out    = wl_le16(body + 2);
    if (status_out) *status_out = wl_le16(body + 4);
    return 0;
}

int wl_parse_assoc_resp(const u8 *body, u16 len, u16 *capability_out,
                        u16 *status_out, u16 *aid_out)
{
    if (len < 6) return -1;
    if (capability_out) *capability_out = wl_le16(body);
    if (status_out)     *status_out     = wl_le16(body + 2);
    if (aid_out)        *aid_out        = (u16)(wl_le16(body + 4) & 0x3fff);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Data path: Ethernet <-> 802.11 (clause 9.3.3.3, table 9-1 case:     */
/* STA -> DS uses to_ds=1, addr1=BSSID, addr2=SA, addr3=DA)            */
/* ------------------------------------------------------------------ */
int wl_eth_to_80211_data(u8 *out, const u8 *eth, u16 eth_len,
                         const u8 *bssid, u16 seq, u8 qos)
{
    u16 ether_type;
    u16 fc;
    int o = 0;

    if (!out || !eth || eth_len < 14) return -1;
    if (eth_len - 14 + 30 > WL_FRAME_MAX) return -2;

    fc = (u16)(WL_TYPE_DATA << 2);                /* subtype 0, to_ds=1  */
    fc |= 0x0100;                                 /* To DS bit           */
    if (qos) fc |= (u16)(WL_STYPE_QOS_DATA << 4);
    wl_put_le16(out, fc);
    wl_put_le16(out + 2, 0);                      /* duration            */
    wl_copy_mac(out + 4,  bssid);                 /* addr1 BSSID         */
    wl_copy_mac(out + 10, eth + 6);               /* addr2 SA            */
    wl_copy_mac(out + 16, eth);                   /* addr3 DA            */
    wl_put_le16(out + 22, (u16)((seq << 4) & 0xfff0));
    o = 24;
    if (qos) {
        out[o++] = 0;                             /* QoS control: TID 0  */
        out[o++] = 0;
    }
    /* LLC/SNAP header (clause 9.3.3.3): AA AA 03 00 00 00 + ethertype  */
    ether_type = wl_be16(eth + 12);
    out[o++] = 0xaa; out[o++] = 0xaa; out[o++] = 0x03;
    out[o++] = 0x00; out[o++] = 0x00; out[o++] = 0x00;
    wl_put_be16(out + o, ether_type); o += 2;
    for (u16 i = 14; i < eth_len; i++) out[o++] = eth[i];
    return o;
}

int wl_80211_data_to_eth(u8 *out, u16 *out_len,
                         const u8 *frame, u16 len)
{
    wl_frame_info_t fi;
    const u8 *src, *dst, *llc;
    u16 ether_type, payload;
    int o = 0;

    if (wl_parse_frame_ctl(frame, len, &fi) != 0) return -1;
    if (fi.type != WL_TYPE_DATA) return -2;
    if (fi.body_len < 8) return -3;

    llc = frame + fi.body_off;
    if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 0x03) return -4;
    ether_type = wl_be16(llc + 6);

    /* STA receiving from the DS: FromDS=1 ToDS=0 ->
     * addr1 = DA, addr2 = BSSID, addr3 = SA (table 9-1)                */
    if (fi.from_ds && !fi.to_ds) {
        dst = fi.addr1;
        src = fi.addr3;
    } else if (fi.to_ds && !fi.from_ds) {         /* IBSS / DS->STA      */
        dst = fi.addr3;
        src = fi.addr2;
    } else {
        dst = fi.addr1;
        src = fi.addr2;
    }

    payload = (u16)(fi.body_len - 8);
    if (payload > 1500) return -5;
    for (int i = 0; i < 6; i++) out[o++] = dst[i];
    for (int i = 0; i < 6; i++) out[o++] = src[i];
    wl_put_be16(out + o, ether_type); o += 2;
    for (u16 i = 0; i < payload; i++)
        out[o++] = llc[8 + i];
    *out_len = (u16)o;
    return 0;
}

/* Channel number -> center frequency (clause 16.3.8.2.2 / 21.3.14).   */
int wl_channel_to_mhz(u8 channel)
{
    if (channel >= 1 && channel <= 13) return 2412 + 5 * (channel - 1);
    if (channel == 14)                 return 2484;   /* Japan DSSS      */
    if (channel >= 32 && channel <= 177) return 5000 + 5 * channel;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registry: families + devices                                        */
/* ------------------------------------------------------------------ */
int driver_wireless_init(void)
{
    g_family_count = 0;
    g_device_count = 0;
    g_scan_count   = 0;
    g_tx_seq_next  = 0;
    g_rx_head = g_rx_tail = 0;
    for (int i = 0; i < WL_MAX_SCAN_RESULTS; i++)
        g_scan_list[i].used = 0;
    g_rx_ring = (u8 *)kmalloc((u64)WL_RX_RING * WL_RX_ETH_MAX);
    if (!g_rx_ring) return -1;      /* honest: no bridge without the ring */
    return 0;
}

int wl_register_family(const wl_family_ops_t *ops)
{
    if (!ops || !ops->name || !ops->attach || !ops->tx_frame) return -1;
    if (g_family_count >= WL_MAX_FAMILIES) return -2;
    for (int i = 0; i < g_family_count; i++)
        if (strcmp(g_families[i]->name, ops->name) == 0) return -3;
    g_families[g_family_count] = ops;
    return g_family_count++;
}

int wl_family_count(void)            { return g_family_count; }
const char *wl_family_name(int id)   { return (id >= 0 && id < g_family_count) ? g_families[id]->name : 0; }

int wl_register_device(int family_id, const char *bus_name,
                       u16 vendor, u16 device)
{
    wl_device_t *d;
    if (family_id < 0 || family_id >= g_family_count) return -1;
    if (g_device_count >= WL_MAX_DEVICES) return -2;
    d = &g_devices[g_device_count];
    memset((void *)d, 0, sizeof(*d));
    d->ops        = g_families[family_id];
    d->bus_name   = bus_name;
    d->vendor_id  = vendor;
    d->device_id  = device;
    d->family_id  = family_id;
    d->state      = WL_STATE_DOWN;
    return g_device_count++;
}

wl_device_t *wl_device_get(int id)
{
    return (id >= 0 && id < g_device_count) ? &g_devices[id] : 0;
}
int wl_device_count(void) { return g_device_count; }

/* One device slot per family; each runs its real attach + fw load. */
int wl_device_probe_all(void)
{
    int added = 0;
    for (int f = 0; f < g_family_count; f++) {
        int id;
        if (g_device_count >= WL_MAX_DEVICES) break;
        id = wl_register_device(f, "auto", 0, 0);
        if (id < 0) continue;
        (void)wl_device_bring_up(id);   /* DOWN stays honest on failure */
        added++;
    }
    return added;
}

int wl_device_bring_up(int id)
{
    wl_device_t *d = wl_device_get(id);
    if (!d) return -1;
    if (d->ops->attach(d) != 0) return -2;
    if (d->ops->fw_load && d->ops->fw_load(d) != 0) {
        d->state = WL_STATE_DOWN;
        return -3;                 /* honest: firmware is required        */
    }
    d->state = WL_STATE_READY;
    return 0;
}

int wl_device_down(int id)
{
    wl_device_t *d = wl_device_get(id);
    if (!d) return -1;
    d->state = WL_STATE_DOWN;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scan list                                                           */
/* ------------------------------------------------------------------ */
int wl_scan_start(int dev_id)
{
    wl_device_t *d = wl_device_get(dev_id);
    if (!d) return -1;
    if (d->state == WL_STATE_DOWN) return -2;
    if (d->ops->scan_start && d->ops->scan_start(d) != 0) return -3;
    d->state = WL_STATE_SCANNING;
    return 0;
}

int wl_scan_result_count(void) { return g_scan_count; }
const wl_scan_result_t *wl_scan_result_get(int idx)
{
    return (idx >= 0 && idx < g_scan_count) ? &g_scan_list[idx] : 0;
}
void wl_scan_clear(void)
{
    g_scan_count = 0;
    for (int i = 0; i < WL_MAX_SCAN_RESULTS; i++) g_scan_list[i].used = 0;
}

static void wl_scan_insert(const wl_scan_result_t *r)
{
    for (int i = 0; i < g_scan_count; i++) {
        if (wl_mac_eq(g_scan_list[i].bssid, r->bssid)) {
            g_scan_list[i] = *r;           /* refresh in place         */
            return;
        }
    }
    if (g_scan_count >= WL_MAX_SCAN_RESULTS) return;
    g_scan_list[g_scan_count] = *r;
    g_scan_list[g_scan_count].used = 1;
    g_scan_count++;
}

/* ------------------------------------------------------------------ */
/* Association state machine (open system)                             */
/* ------------------------------------------------------------------ */
int wl_connect(int dev_id, const u8 *bssid, const char *ssid)
{
    wl_device_t *d = wl_device_get(dev_id);
    u8 frame[WL_FRAME_MAX];
    int len, slen = 0;

    if (!d) return -1;
    if (d->state == WL_STATE_DOWN) return -2;
    if (!bssid || wl_mac_is_zero(bssid) || wl_mac_is_bcast(bssid)) return -3;
    while (ssid && ssid[slen] && slen < WL_SSID_MAX) slen++;
    if (slen == 0) return -4;

    /* Step 1 of the STA bring-up: open-system authentication.  When the
     * family driver receives the Authentication Response it hands the
     * frame to wl_rx_frame(), which sends the Association Request and
     * finishes the state machine there (clause 9.3.3.6 then 9.3.3.10). */
    d->state = WL_STATE_AUTHENTICATING;
    len = wl_build_auth_open(frame, bssid, d->mac, bssid, wl_next_seq());
    if (len < 0) { d->state = WL_STATE_READY; return -5; }
    if (wl_tx_frame(d, frame, (u16)len) != 0) { d->state = WL_STATE_READY; return -6; }

    for (int i = 0; i < WL_SSID_MAX + 1; i++) {
        d->ssid[i] = (i < slen) ? ssid[i] : 0;
    }
    d->ssid_len = (u8)slen;
    wl_copy_mac(d->bssid, bssid);
    return 0;
}

/* Step 2: after a successful Authentication Response, send the
 * Association Request (clause 9.3.3.10).  Called from wl_rx_frame(). */
static int wl_assoc_continue(wl_device_t *d)
{
    u8 frame[WL_FRAME_MAX];
    int len;

    d->state = WL_STATE_ASSOCIATING;
    len = wl_build_assoc_req(frame, d->bssid, d->mac, d->bssid,
                             d->ssid, d->rates, d->n_rates,
                             (u16)(WL_CAP_ESS | WL_CAP_SHORT_SLOT));
    if (len < 0) { d->state = WL_STATE_READY; return -1; }
    if (wl_tx_frame(d, frame, (u16)len) != 0) { d->state = WL_STATE_READY; return -2; }
    return 0;
}

int wl_disconnect(int dev_id)
{
    wl_device_t *d = wl_device_get(dev_id);
    u8 frame[WL_FRAME_MAX];
    int len;

    if (!d) return -1;
    if (d->state != WL_STATE_ASSOCIATED) return 0;
    /* Deauth to the AP (clause 9.3.3.8)                                */
    len = wl_mgmt_common(frame, WL_STYPE_DEAUTH, d->bssid, d->mac,
                         d->bssid, wl_next_seq());
    if (len > 0) {
        wl_put_le16(frame + len, WL_REASON_ASSOC_LEAVE);
        len += 2;
        (void)wl_tx_frame(d, frame, (u16)len);
    }
    d->state   = WL_STATE_READY;
    d->aid     = 0;
    d->bssid[0] = d->bssid[1] = d->bssid[2] =
    d->bssid[3] = d->bssid[4] = d->bssid[5] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX path                                                             */
/* ------------------------------------------------------------------ */
int wl_rx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    wl_frame_info_t fi;
    wl_scan_result_t sr;
    u16 cap = 0, status = 0, aid = 0;
    u8 eth[WL_RX_ETH_MAX];
    u16 elen;
    u32 slot;

    if (!dev || !frame) return -1;
    if (wl_parse_frame_ctl(frame, len, &fi) != 0) {
        dev->rx_bad_fcs++;
        return -2;
    }
    dev->rx_frames++;

    if (fi.type == WL_TYPE_MGMT) {
        dev->rx_mgmt++;
        if (fi.subtype == WL_STYPE_BEACON ||
            fi.subtype == WL_STYPE_PROBE_RESP) {
            dev->rx_beacons++;
            if (wl_parse_beacon(frame, len, &sr) == 0) {
                /* the family driver reports the rx channel per frame;
                 * when it did not (0), keep the DS-params IE value     */
                if (dev->channel) sr.channel = dev->channel;
                wl_scan_insert(&sr);
            }
        } else if (fi.subtype == WL_STYPE_AUTH &&
                   dev->state == WL_STATE_AUTHENTICATING) {
            if (wl_parse_auth_resp(frame + fi.body_off, fi.body_len,
                                   0, &status) == 0 &&
                status == WL_STATUS_SUCCESS) {
                (void)wl_assoc_continue(dev);   /* send Assoc Request    */
                return 0;
            }
            dev->state = WL_STATE_READY;
            return (int)status;
        } else if (fi.subtype == WL_STYPE_ASSOC_RESP &&
                   dev->state == WL_STATE_ASSOCIATING) {
            if (wl_parse_assoc_resp(frame + fi.body_off, fi.body_len,
                                    &cap, &status, &aid) == 0 &&
                status == WL_STATUS_SUCCESS) {
                dev->capability = cap;
                dev->aid        = aid;
                dev->state      = WL_STATE_ASSOCIATED;
                return 0;
            }
            dev->state = WL_STATE_READY;
            return (int)status;
        }
        return 0;
    }

    if (fi.type == WL_TYPE_DATA && dev->state == WL_STATE_ASSOCIATED) {
        /* decapsulate to Ethernet II and queue for the NIC bridge;    */
        /* the queue is drained by wl_netif_recv() (real consumers).   */
        if (wl_80211_data_to_eth(eth, &elen, frame, len) != 0) {
            dev->rx_bad_fcs++;
            return -3;
        }
        if ((u32)(g_rx_tail - g_rx_head) == WL_RX_RING) return -4; /* full */
        slot = g_rx_tail % WL_RX_RING;
        for (u16 i = 0; i < elen; i++)
            g_rx_ring[slot * WL_RX_ETH_MAX + i] = eth[i];
        g_rx_ring_len[slot] = elen;
        g_rx_tail++;
        return 0;
    }
    return 0;
}

/* NIC bridge RX: pull one decapsulated Ethernet frame (real path used */
/* by the driver_nic ops of an attached wireless device).              */
int wl_netif_recv(u8 *buf, u16 buflen, u16 *len_out)
{
    u32 slot;
    u16 n;
    if (!buf || !len_out) return -1;
    if (g_rx_head == g_rx_tail) return 0;        /* empty: honest        */
    slot = g_rx_head % WL_RX_RING;
    n = g_rx_ring_len[slot];
    if (n > buflen) { g_rx_head++; return -2; }  /* frame too small buf  */
    for (u16 i = 0; i < n; i++)
        buf[i] = g_rx_ring[slot * WL_RX_ETH_MAX + i];
    *len_out = n;
    g_rx_head++;
    return 1;
}

int wl_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    if (!dev || !frame || len == 0) return -1;
    if (dev->state == WL_STATE_DOWN) return -2;
    if (dev->ops->tx_frame(dev, frame, len) != 0) return -3;
    dev->tx_frames++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Core self-test: real vector checks through the real parsers and     */
/* builders (frames are byte-exact per IEEE 802.11-2016; the same      */
/* fixtures also run host-side in tests/host_wifi_test.c).             */
/* Returns the number of failing checks; appends a report to out.      */
/* ------------------------------------------------------------------ */
static int wl_check(char *out, int *o, int outlen, const char *name,
                    int cond)
{
    const char *s = cond ? " PASS" : " FAIL";
    if (*o + 32 < outlen) {
        int i = 0;
        while (name[i] && *o < outlen - 1) out[(*o)++] = name[i++];
        i = 0;
        while (s[i] && *o < outlen - 1) out[(*o)++] = s[i++];
        out[(*o)++] = '\n';
    }
    out[*o] = '\0';
    return cond ? 0 : 1;
}

int driver_wireless_selftest(char *out, int outlen)
{
    int fails = 0, o = 0;
    wl_frame_info_t fi;
    wl_scan_result_t sr;
    u8 buf[WL_FRAME_MAX];
    u8 eth[1522];
    u16 elen;
    int len;

    if (outlen > 0) out[0] = '\0';

    /* --- vector 1: beacon, ch 6, open, ESS (802.11-2016 example layout)
     * FC=0x0080 (mgmt, subtype 8), seq 0x1234
     * addr3 = 02:11:22:33:44:55 (BSSID)                                  */
    {
        u8 bcn[67] = {
            0x80, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
            0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
            0x34, 0x12,
            /* body: TSF 8, interval 100 TU = 0x0064, cap ESS         */
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x64, 0x00, 0x01, 0x00,
            0x00, 0x0a, 'O','C','-','T','E','S','T','-','A','P',
            0x03, 0x01, 0x06,
            0x01, 0x04, 0x82, 0x84, 0x8b, 0x96,
            0x32, 0x08, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48,
            0x60, 0x6c,
        };
        len = (int)sizeof(bcn);
        fails += wl_check(out, &o, outlen, "beacon parse",
                          wl_parse_beacon(bcn, (u16)len, &sr) == 0);
        fails += wl_check(out, &o, outlen, "beacon ssid",
                          sr.ssid_len == 10 &&
                          sr.ssid[0] == 'O' && sr.ssid[9] == 'P');
        fails += wl_check(out, &o, outlen, "beacon bssid",
                          sr.bssid[0] == 0x02 && sr.bssid[5] == 0x55);
        fails += wl_check(out, &o, outlen, "beacon channel", sr.channel == 6);
        fails += wl_check(out, &o, outlen, "beacon cap",
                          (sr.capability & WL_CAP_ESS) != 0 &&
                          (sr.capability & WL_CAP_PRIVACY) == 0);
        fails += wl_check(out, &o, outlen, "beacon interval",
                          sr.beacon_interval == 100);
        fails += wl_check(out, &o, outlen, "beacon rate",
                          sr.max_rate_mbps == 54);
        fails += wl_check(out, &o, outlen, "beacon open",
                          sr.has_rsn == 0 && sr.has_wpa == 0);
    }

    /* --- vector 2: FC parsing of QoS data from-DS frame                */
    {
        u8 qos[30] = {
            0x88, 0x02, 0x00, 0x00,          /* QoS data, from_ds      */
            0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
            0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
            0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
            0x34, 0x12, 0x00, 0x00,          /* seq + QoS ctl          */
            0xaa, 0xaa, 0x03, 0x00,
        };
        fails += wl_check(out, &o, outlen, "fc qos parse",
                          wl_parse_frame_ctl(qos, 28, &fi) == 0);
        fails += wl_check(out, &o, outlen, "fc qos subtype",
                          fi.type == WL_TYPE_DATA &&
                          fi.subtype == WL_STYPE_QOS_DATA);
        fails += wl_check(out, &o, outlen, "fc qos flag",
                          fi.from_ds == 1 && fi.to_ds == 0 &&
                          fi.qos_present == 1);
        fails += wl_check(out, &o, outlen, "fc seq",
                          fi.seq_num == 0x123 && fi.frag_num == 4);
        fails += wl_check(out, &o, outlen, "fc body off", fi.body_off == 26);
    }

    /* --- vector 3: assoc request build + reparse round trip           */
    {
        u8 rates[4] = { 0x82, 0x84, 0x0c, 0x96 };
        const u8 *ie_data;
        u8 ie_len;
        len = wl_build_assoc_req(buf,
                                 (const u8 *)"\x02\x11\x22\x33\x44\x55",
                                 (const u8 *)"\x02\x00\x00\x00\x00\x01",
                                 (const u8 *)"\x02\x11\x22\x33\x44\x55",
                                 "OC-TEST", rates, 4,
                                 (u16)(WL_CAP_ESS | WL_CAP_SHORT_SLOT));
        fails += wl_check(out, &o, outlen, "assoc build",
                          len == 24 + 2 + 2 + (2 + 7) + (2 + 4));
        fails += wl_check(out, &o, outlen, "assoc reparse",
                          wl_parse_frame_ctl(buf, (u16)len, &fi) == 0 &&
                          fi.type == WL_TYPE_MGMT &&
                          fi.subtype == WL_STYPE_ASSOC_REQ);
        fails += wl_check(out, &o, outlen, "assoc ssid ie",
                          wl_find_ie(buf + fi.body_off + 4, fi.body_len - 4,
                                     WL_IE_SSID, &ie_data, &ie_len) == 1 &&
                          ie_len == 7 && ie_data[0] == 'O' &&
                          ie_data[6] == 'T');
        fails += wl_check(out, &o, outlen, "assoc cap",
                          wl_le16(buf + 24) ==
                          (WL_CAP_ESS | WL_CAP_SHORT_SLOT));
    }

    /* --- vector 4: assoc response parse (status ok, AID 0xc005)       */
    {
        u8 resp[8] = { 0x01, 0x04, 0x00, 0x00, 0x05, 0xc0, 0x82, 0x84 };
        u16 cap = 0, st = 0xffff, aid = 0;
        fails += wl_check(out, &o, outlen, "assoc resp parse",
                          wl_parse_assoc_resp(resp, 6, &cap, &st, &aid) == 0);
        fails += wl_check(out, &o, outlen, "assoc resp fields",
                          st == 0 && aid == 0x0005 &&
                          cap == 0x0401);
    }

    /* --- vector 5: eth -> 802.11 -> eth round trip                    */
    {
        u8 bss6[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
        for (int i = 0; i < 6; i++) { eth[i] = 0x50; eth[6 + i] = 0x60; }
        eth[12] = 0x08; eth[13] = 0x00;               /* IPv4            */
        for (int i = 14; i < 60; i++) eth[i] = (u8)i;
        len = wl_eth_to_80211_data(buf, eth, 60, bss6, 0x1234, 0);
        fails += wl_check(out, &o, outlen, "eth encap",
                          len == 24 + 8 + (60 - 14));
        fails += wl_check(out, &o, outlen, "llc snap",
                          buf[24] == 0xaa && buf[25] == 0xaa &&
                          buf[26] == 0x03 && buf[29] == 0x00 &&
                          buf[30] == 0x08 && buf[31] == 0x00);
        fails += wl_check(out, &o, outlen, "encap reparse",
                          wl_parse_frame_ctl(buf, (u16)len, &fi) == 0 &&
                          fi.to_ds == 1 && fi.from_ds == 0);
        fails += wl_check(out, &o, outlen, "decap roundtrip",
                          wl_80211_data_to_eth(eth, &elen,
                                               buf, (u16)len) == 0 &&
                          elen == 60 && eth[12] == 0x08);
    }

    /* --- vector 6: channel table                                      */
    fails += wl_check(out, &o, outlen, "ch1 2412", wl_channel_to_mhz(1) == 2412);
    fails += wl_check(out, &o, outlen, "ch6 2437", wl_channel_to_mhz(6) == 2437);
    fails += wl_check(out, &o, outlen, "ch14 2484", wl_channel_to_mhz(14) == 2484);
    fails += wl_check(out, &o, outlen, "ch36 5180", wl_channel_to_mhz(36) == 5180);

    if (o < outlen - 12) {
        const char *tail = "core checks done\n";
        int i = 0;
        while (tail[i] && o < outlen - 1) out[o++] = tail[i++];
        out[o] = '\0';
    }
    return fails;
}
