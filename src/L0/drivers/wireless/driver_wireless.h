/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_wireless.h
 *
 * 802.11 core + wireless family framework.
 *
 * This is the shared layer used by every wireless family driver shipped in
 * this work package:
 *   - Intel     iwlwifi      (drivers/wireless/driver_iwlwifi.c)
 *   - Realtek   rtl8188/8192/8821 (drivers/wireless/driver_rtlwifi.c)
 *   - Atheros   ath9k/ath10k/ath11k (drivers/wireless/driver_ath9k.c, ...)
 *   - Broadcom  b43 / brcmfmac (drivers/wireless/driver_b43.c, ...)
 *
 * Everything here is real on-air format handling per IEEE 802.11-2016
 * (frame control layout clause 9.2.4.1.1, management body clause 9.3.3.2,
 * information elements clause 9.4.2, LLC/SNAP encapsulation clause 9.3.3.3)
 * plus the STA-side state machine (scan -> authenticate -> associate) that
 * the family drivers drive.
 *
 * L1 note: the numbered L1 interface set stays closed at 1-138.  These are
 * kernel-internal driver APIs, documented in
 * docs/EXTENSIONS_WP10-wireless_bluetooth.md (chipset / timer_rtc
 * precedent).
 */
#ifndef DRIVER_WIRELESS_H
#define DRIVER_WIRELESS_H

#include <types.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */
#define WL_MAX_FAMILIES        8
#define WL_MAX_DEVICES         8
#define WL_MAX_SCAN_RESULTS    16
#define WL_SSID_MAX            32
#define WL_FRAME_MAX           2400   /* mgmt frames fit; data payload cap */
#define WL_MAX_RATES           16

/* ------------------------------------------------------------------ */
/* 802.11 on-air types (IEEE 802.11-2016 clause 9.2.4.1.1)             */
/* ------------------------------------------------------------------ */
#define WL_TYPE_MGMT           0
#define WL_TYPE_CTRL           1
#define WL_TYPE_DATA           2

/* Management subtypes (clause 9.4.1.3.1 table) */
#define WL_STYPE_PROBE_REQ     4
#define WL_STYPE_PROBE_RESP    5
#define WL_STYPE_BEACON        8
#define WL_STYPE_ATIM          9
#define WL_STYPE_DISASSOC      10
#define WL_STYPE_AUTH          11
#define WL_STYPE_DEAUTH        12
#define WL_STYPE_ACTION        13

/* Data subtypes */
#define WL_STYPE_ASSOC_REQ     0
#define WL_STYPE_ASSOC_RESP    1
#define WL_STYPE_REASSOC_REQ   2
#define WL_STYPE_REASSOC_RESP  3
#define WL_STYPE_DATA          0
#define WL_STYPE_DATA_NULL     4
#define WL_STYPE_QOS_DATA      8

/* Status / reason codes (clause 9.4.1.9 / 9.4.1.7) */
#define WL_STATUS_SUCCESS      0
#define WL_STATUS_CAPS_UNSUPPORTED   10
#define WL_STATUS_REASSOC_DENIED     11
#define WL_STATUS_ASSOC_DENIED       12
#define WL_STATUS_AUTH_ALG_UNSUP     13

#define WL_REASON_LEAVE_DEAUTH 3
#define WL_REASON_ASSOC_LEAVE  8

/* Information element IDs (clause 9.4.2.1 table 9-77) */
#define WL_IE_SSID             0
#define WL_IE_SUPP_RATES       1
#define WL_IE_DS_PARAMS        3
#define WL_IE_TIM              5
#define WL_IE_ERP              42
#define WL_IE_HT_CAPS          45
#define WL_IE_RSN              48
#define WL_IE_EXT_SUPP_RATES   50
#define WL_IE_HT_INFO          61
#define WL_IE_VHT_CAPS         191
#define WL_IE_VHT_OP           192
#define WL_IE_VENDOR           221

/* Capability information bits (clause 9.4.1.4) */
#define WL_CAP_ESS             0x0001
#define WL_CAP_IBSS            0x0002
#define WL_CAP_PRIVACY         0x0010
#define WL_CAP_SHORT_PREAMBLE  0x0020
#define WL_CAP_SHORT_SLOT      0x0400

/* Authentication algorithm numbers (clause 9.4.1.1) */
#define WL_AUTH_OPEN           0
#define WL_AUTH_SHARED_KEY     1

/* ------------------------------------------------------------------ */
/* Device states                                                       */
/* ------------------------------------------------------------------ */
typedef enum wl_state {
    WL_STATE_DOWN = 0,        /* no firmware / not initialized          */
    WL_STATE_READY,           /* firmware loaded, radio idle            */
    WL_STATE_SCANNING,        /* passive scan in progress               */
    WL_STATE_AUTHENTICATING,
    WL_STATE_ASSOCIATING,
    WL_STATE_ASSOCIATED,      /* data frames may flow                   */
} wl_state_t;

/* ------------------------------------------------------------------ */
/* Scan result (assembled from real Beacon / Probe Response frames)    */
/* ------------------------------------------------------------------ */
typedef struct wl_scan_result {
    u8     bssid[6];
    char   ssid[WL_SSID_MAX + 1];
    u8     ssid_len;
    u8     channel;              /* DS params IE / 5 GHz derivation      */
    i8     rssi;                 /* dBm estimate as reported by hw       */
    u16    capability;
    u16    beacon_interval;      /* TUs (1024 us)                        */
    u8     max_rate_mbps;
    u8     has_rsn;              /* WPA2 (RSN IE present)                */
    u8     has_wpa;              /* WPA1 (vendor Microsoft IE present)   */
    u8     used;
} wl_scan_result_t;

/* ------------------------------------------------------------------ */
/* Parsed 802.11 frame (shared header view)                            */
/* ------------------------------------------------------------------ */
typedef struct wl_frame_info {
    u8     proto_ver;
    u8     type;
    u8     subtype;
    u8     to_ds;
    u8     from_ds;
    u8     more_frag;
    u8     retry;
    u8     pwr_mgmt;
    u8     more_data;
    u8     protected_frame;
    u8     order;
    u8     addr1[6];             /* receiver                             */
    u8     addr2[6];             /* transmitter                          */
    u8     addr3[6];             /* filtering address                    */
    u8     addr4[6];             /* only when to_ds && from_ds           */
    u16    seq_num;              /* bits 4..15 of sequence control       */
    u16    frag_num;
    u16    qos_present;          /* 1 = QoS header bytes present         */
    u16    body_off;             /* offset of body inside the frame      */
    u16    body_len;
} wl_frame_info_t;

/* ------------------------------------------------------------------ */
/* Device + family ops                                                 */
/* ------------------------------------------------------------------ */
struct wl_device;

/*
 * Per-family operations.  attach/fw_load/scan_start/tx_frame return 0 on
 * success, negative on failure.  selftest writes an ASCII report into
 * out (NUL terminated) and returns the number of failing checks.
 */
typedef struct wl_family_ops {
    const char *name;                                   /* "iwlwifi" ... */
    int  (*probe_pci)(u16 vendor, u16 device);          /* 1 = claim     */
    int  (*attach)(struct wl_device *dev);              /* hw init       */
    int  (*fw_load)(struct wl_device *dev);             /* firmware      */
    int  (*scan_start)(struct wl_device *dev);          /* passive scan  */
    int  (*tx_frame)(struct wl_device *dev,
                     const u8 *frame, u16 len);         /* on-air frame  */
    int  (*selftest)(struct wl_device *dev,
                     char *out, int outlen);            /* real checks   */
} wl_family_ops_t;

typedef struct wl_device {
    const wl_family_ops_t *ops;
    const char            *bus_name;    /* "PCI"/"USB"/"SDIO"          */
    u16                    vendor_id;
    u16                    device_id;
    u8                     mac[6];
    wl_state_t             state;
    char                   ssid[WL_SSID_MAX + 1];
    u8                     ssid_len;
    u8                     bssid[6];
    u8                     channel;
    u16                    aid;
    u16                    capability;
    u8                     rates[WL_MAX_RATES];
    u8                     n_rates;
    void                  *family_priv;  /* per-family context          */
    int                    family_id;
    /* stats (all real counters, updated on actual traffic)              */
    u32                    tx_frames;
    u32                    rx_frames;
    u32                    rx_mgmt;
    u32                    rx_beacons;
    u32                    rx_bad_fcs;
} wl_device_t;

/* ------------------------------------------------------------------ */
/* Firmware-file provider (3.0.0-WP03 G2)                              */
/* ------------------------------------------------------------------ */
/* The family drivers load optional vendor firmware from /etc/firmware.
 * They used to call the fs root service directly - a drivers->services
 * dependency edge.  The net root service now installs the file access
 * here at bring-up (same inversion pattern as the display console
 * hook); until then every fw-requiring family stays DOWN (honest).
 * wl_fw_read_file returns a kmalloc'd buffer the driver owns (the
 * drivers keep it for the device lifetime, exactly as before). */
typedef int (*wl_fw_read_fn)(const char *path, u8 **buf_out, u32 *len_out);
typedef int (*wl_fw_exists_fn)(const char *path);   /* 1 = present */

void wl_fw_set_reader(wl_fw_read_fn fn);
void wl_fw_set_exists(wl_fw_exists_fn fn);
int  wl_fw_read_file(const char *path, u8 **buf_out, u32 *len_out);
int  wl_fw_exists(const char *path);

/* ------------------------------------------------------------------ */
/* Core API                                                            */
/* ------------------------------------------------------------------ */
/* Bring-up.  Called once from the boot composition root; also collects
 * the self-declared "wireless" families from .oc_drvs (WP-03 G3). */
int  driver_wireless_init(void);

/* 3.0.0-WP03 G3: no wl_register_family() prototype anymore - families
 * self-declare in their own file via OC_DRIVER_DECL("wireless", ...)
 * and the core collects them at init. */
int  wl_family_count(void);
const char *wl_family_name(int id);

/* Device lifecycle (family drivers call these). */
int  wl_register_device(int family_id, const char *bus_name,
                        u16 vendor, u16 device);
wl_device_t *wl_device_get(int id);
int  wl_device_count(void);
/* Register one device slot per registered family and run its real
 * attach (PCI/USB enumeration) + firmware load.  Devices without
 * hardware stay DOWN (honest state).  Returns devices registered. */
int  wl_device_probe_all(void);
int  wl_device_bring_up(int id);       /* attach + fw_load + READY     */
int  wl_device_down(int id);

/* Scan list (filled from real beacons/probe responses). */
int  wl_scan_start(int dev_id);
int  wl_scan_result_count(void);
const wl_scan_result_t *wl_scan_result_get(int idx);
void wl_scan_clear(void);

/* Association (open-system, clause 9.3.3.2). */
int  wl_connect(int dev_id, const u8 *bssid, const char *ssid);
int  wl_disconnect(int dev_id);

/* Frame handling (family RX paths hand raw on-air frames here). */
int  wl_rx_frame(wl_device_t *dev, const u8 *frame, u16 len);

/* TX of an 802.11 frame through the family driver. */
int  wl_tx_frame(wl_device_t *dev, const u8 *frame, u16 len);

/* ------------------------------------------------------------------ */
/* 802.11 format helpers (also used by the family selftests)           */
/* ------------------------------------------------------------------ */
int  wl_parse_frame_ctl(const u8 *frame, u16 len, wl_frame_info_t *out);
int  wl_parse_beacon(const u8 *frame, u16 len, wl_scan_result_t *out);
int  wl_walk_ie(const u8 *body, u16 body_len, int *pos,
                u8 *id_out, const u8 **data_out, u8 *len_out);
int  wl_find_ie(const u8 *body, u16 body_len, u8 ie_id,
                const u8 **data_out, u8 *len_out);

/* Builders: write into buf (WL_FRAME_MAX); return frame length or <0. */
int  wl_build_probe_req(u8 *buf, const char *ssid, u8 channel);
int  wl_build_auth_open(u8 *buf, const u8 *dst, const u8 *src,
                        const u8 *bss, u16 seq);
int  wl_build_assoc_req(u8 *buf, const u8 *dst, const u8 *src,
                        const u8 *bss, const char *ssid,
                        const u8 *rates, u8 n_rates, u16 capability);
int  wl_parse_auth_resp(const u8 *body, u16 len, u16 *seq_out,
                        u16 *status_out);
int  wl_parse_assoc_resp(const u8 *body, u16 len, u16 *capability_out,
                         u16 *status_out, u16 *aid_out);

/* Data path bridging (802.11-2016 clause 9.3.3.3 LLC/SNAP encap). */
int  wl_eth_to_80211_data(u8 *out, const u8 *eth, u16 eth_len,
                          const u8 *bssid, u16 seq, u8 qos);
int  wl_80211_data_to_eth(u8 *out, u16 *out_len,
                          const u8 *frame, u16 len);
int  wl_channel_to_mhz(u8 channel);

/* NIC bridge RX: pull one decapsulated Ethernet frame. Returns 1 with */
/* a frame, 0 when empty, negative on error.                           */
int  wl_netif_recv(u8 *buf, u16 buflen, u16 *len_out);

/* NIC bridge: register the first wireless device with driver_nic so
 * net_poll() drains the WiFi RX ring and TX goes out over 802.11.
 * Returns 0 on success, -1 when no wireless hardware exists. */
int  wl_nic_bridge_register(void);

/* MAC address to "xx:xx:xx:xx:xx:xx" (18 bytes incl. NUL).            */
void wl_mac_to_str(const u8 *mac, char out[18]);

/* Self-test of the core itself (called by the `wifi` shell command). */
int  driver_wireless_selftest(char *out, int outlen);

#endif /* DRIVER_WIRELESS_H */
