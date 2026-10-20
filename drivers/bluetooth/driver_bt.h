/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt.h
 *
 * Bluetooth stack - public interface.
 *
 * Layers (Bluetooth Core Specification v5.x + profile specs):
 *   driver_bt_hci.c   - HCI core: command/event engine, connections,
 *                       inquiry results          (Core 5.x vol 2/4)
 *   driver_bt_h4.c    - H4 UART transport on COM2 (16550, IRQ3)  (vol 4.A)
 *   driver_bt_usb.c   - HCI-USB transport (control=cmd, int=events,
 *                       bulk=ACL)                (vol 4.B)
 *   driver_bt_l2cap.c - L2CAP signaling + channels          (vol 3.A)
 *   driver_bt_rfcomm.c- RFCOMM mux (TS 07.10 frames, MSC/PN)  (RFCOMM spec)
 *   driver_bt_sdp.c   - SDP client (service search/attribute)  (SDP spec)
 *   driver_bt_spp.c   - Serial Port Profile over RFCOMM        (SPP spec)
 *   driver_bt_hid.c   - HID profile (control/interrupt)        (HID spec)
 *
 * All layer boundaries are real: the HCI core never fabricates a
 * response; events come from a transport byte stream (UART IRQ or USB
 * endpoint), and every protocol field is parsed from real packets.
 *
 * L1 note: numbered L1 interfaces stay closed at 1-138; these are
 * kernel-internal driver APIs documented in
 * docs/EXTENSIONS_WP10-wireless_bluetooth.md.
 */
#ifndef DRIVER_BT_H
#define DRIVER_BT_H

#include <types.h>

/* ------------------------------------------------------------------ */
/* Limits + packet types (Core 5.x vol 4)                              */
/* ------------------------------------------------------------------ */
#define BT_MAX_CONNS            8
#define BT_INQUIRY_MAX          8
#define BT_ACL_MTU              1024
#define BT_NAME_MAX             32

/* H4 packet type bytes (Core 5.x vol 4.A) */
#define BT_H4_COMMAND           0x01
#define BT_H4_ACL               0x02
#define BT_H4_SCO               0x03
#define BT_H4_EVENT             0x04

/* OGFs (Core 5.x vol 2) */
#define BT_OGF_LINK_CTL         0x01
#define BT_OGF_LINK_POLICY      0x02
#define BT_OGF_HOST_CTL         0x03
#define BT_OGF_INFO_PARAM       0x04
#define BT_OGF_STATUS_PARAM     0x05

#define BT_OPCODE(ogf, ocf)     (u16)(((ogf) << 10) | (ocf))
#define BT_OGF(op)              (u8)((op) >> 10)
#define BT_OCF(op)              (u16)((op) & 0x03ffu)

/* Link control (OGF 0x01) */
#define BT_OCF_INQUIRY          0x0001
#define BT_OCF_INQUIRY_CANCEL   0x0002
#define BT_OCF_CREATE_CONN      0x0005
#define BT_OCF_DISCONNECT       0x0006
#define BT_OCF_REMOTE_NAME_REQ  0x0019
/* Host control (OGF 0x03) */
#define BT_OCF_SET_EVENT_MASK   0x0001
#define BT_OCF_RESET            0x0003
#define BT_OCF_WRITE_SCAN_ENABLE 0x001a
/* Info (OGF 0x04) */
#define BT_OCF_READ_LOCAL_VERSION 0x0001
#define BT_OCF_READ_LOCAL_CMDS  0x0002
#define BT_OCF_READ_BD_ADDR     0x0009
/* Status (OGF 0x05) */
#define BT_OCF_READ_RSSI        0x0005

/* Events (Core 5.x vol 2 5.4.4) */
#define BT_EVT_INQUIRY_COMPLETE 0x01
#define BT_EVT_INQUIRY_RESULT   0x02
#define BT_EVT_CONN_COMPLETE    0x03
#define BT_EVT_DISCONN_COMPLETE 0x05
#define BT_EVT_REMOTE_NAME      0x08
#define BT_EVT_CMD_COMPLETE     0x0e
#define BT_EVT_CMD_STATUS       0x0f
#define BT_EVT_NUM_COMPLETED    0x13

/* Error codes (Core 5.x vol 1 4.4) */
#define BT_ERR_SUCCESS          0x00
#define BT_ERR_UNKNOWN_CMD      0x01
#define BT_ERR_NO_CONN          0x02
#define BT_ERR_HW_FAIL          0x03
#define BT_ERR_TIMEOUT          0x08
#define BT_ERR_ACL_CONN_EXISTS  0x0b
#define BT_ERR_UNSPEC           0x0e   /* unspecified error              */

/* Connection states */
#define BT_CONN_IDLE            0
#define BT_CONN_CONNECTING      1
#define BT_CONN_OPEN            2

/* ------------------------------------------------------------------ */
/* Device view                                                         */
/* ------------------------------------------------------------------ */
typedef struct bt_conn {
    u16 handle;
    u8  addr[6];                    /* HCI octet order (LAP first)       */
    u8  state;
    u8  used;
} bt_conn_t;

typedef struct bt_inquiry_result {
    u8  addr[6];
    u8  class_of_device[3];
    u16 clock_offset;
    u8  page_scan_rep_mode;
    u8  rssi;                        /* extended result form             */
} bt_inquiry_result_t;

/* ------------------------------------------------------------------ */
/* Transport interface (implemented by H4 UART + HCI-USB)              */
/* ------------------------------------------------------------------ */
typedef struct bt_transport_ops {
    const char *name;
    /* framed send: packet type + payload (type byte prepended by the
     * transport itself; the core passes the type explicitly)           */
    int  (*send)(u8 type, const u8 *data, u16 len);
    /* framed receive: returns 1 with a packet, 0 when none pending,
     * negative on transport error                                      */
    int  (*recv)(u8 *type_out, u8 *buf, u16 buflen, u16 *len_out);
    int  (*poll_status)(char *out, int outlen);
} bt_transport_ops_t;

/* ------------------------------------------------------------------ */
/* HCI core API                                                        */
/* ------------------------------------------------------------------ */
int  bt_hci_init(void);                       /* state init            */
int  bt_hci_attach_transport(const bt_transport_ops_t *ops);
const char *bt_hci_transport_name(void);
int  bt_hci_present(void);                    /* transport + reset ok  */

/* Synchronous command (waits for Command_Complete/Status).            */
int  bt_hci_cmd(u16 opcode, const u8 *params, u8 plen,
                u8 *resp, u16 *resp_len, u32 timeout_ms);

/* Named wrappers (all real commands).                                 */
int  bt_hci_reset(void);
int  bt_hci_set_event_mask(u64 mask);
int  bt_hci_read_bd_addr(u8 addr[6]);
int  bt_hci_read_local_version(u8 *hci_ver, u16 *hci_rev,
                               u8 *lmp_ver, u16 *manufacturer,
                               u16 *lmp_subver);
int  bt_hci_write_scan_enable(u8 scan);
int  bt_hci_inquiry_start(void);              /* 5.12 s GIAC inquiry   */
int  bt_hci_inquiry_results(bt_inquiry_result_t *out, int max);
int  bt_hci_create_connection(const u8 addr[6], u16 *handle_out);
int  bt_hci_disconnect(u16 handle);
int  bt_hci_remote_name(const u8 addr[6], char *name_out, int cap);
int  bt_hci_read_rssi(u16 handle, i8 *rssi_out);

/* ACL path (up-calls from the RX path).                               */
int  bt_hci_send_acl(u16 handle, const u8 *data, u16 len);

/* Event pump: drains the transport and dispatches events + ACL.       */
int  bt_hci_poll(void);

/* Accessors for the shell.                                            */
int  bt_hci_handle_of_addr(const u8 addr[6], u16 *handle_out);
int  bt_hci_conn_count(void);
bt_conn_t *bt_hci_conn_get(int idx);
int  bt_hci_inquiry_count(void);
const bt_inquiry_result_t *bt_hci_inquiry_get(int idx);
void bt_addr_to_str(const u8 addr[6], char out[18]);  /* NN:NN:... form */
const char *bt_err_str(int status);

/* Whole-stack init: HCI + L2CAP + RFCOMM + SDP + SPP + HID + the
 * transport pick (USB dongle first, then the H4 UART).                */
int  bt_stack_init(void);

/* ------------------------------------------------------------------ */
/* H4 UART transport (COM2 0x2f8, IRQ3)                                */
/* ------------------------------------------------------------------ */
int  bt_h4_init(void);                        /* registers the transport */
int  bt_h4_probe(void);                       /* 1 = UART responds      */
int  bt_h4_active(void);                      /* WP-10-tui-qa: H4 owns
                                                COM2 right now (IR SIR
                                                must yield the port)   */
const bt_transport_ops_t *bt_h4_ops(void);

/* ------------------------------------------------------------------ */
/* HCI-USB transport (btusb class driver)                              */
/* ------------------------------------------------------------------ */
int  bt_usb_init(void);                       /* registers the class drv */
int  bt_usb_present(void);                    /* device attached?       */
const bt_transport_ops_t *bt_usb_ops(void);

/* ------------------------------------------------------------------ */
/* L2CAP (vol 3.A)                                                     */
/* ------------------------------------------------------------------ */
#define BT_L2CAP_SIG_CID        0x0001
#define BT_L2CAP_CONNLESS_CID   0x0002
#define BT_L2CAP_PSM_SDP        0x0001
#define BT_L2CAP_PSM_RFCOMM     0x0003
#define BT_L2CAP_PSM_HID_CTRL   0x0011
#define BT_L2CAP_PSM_HID_INTR   0x0013

#define BT_L2CAP_CMD_CONN_REQ   0x02
#define BT_L2CAP_CMD_CONN_RESP  0x03
#define BT_L2CAP_CMD_CFG_REQ    0x04
#define BT_L2CAP_CMD_CFG_RESP   0x05
#define BT_L2CAP_CMD_DISC_REQ   0x06
#define BT_L2CAP_CMD_DISC_RESP  0x07
#define BT_L2CAP_CMD_ECHO_REQ   0x08
#define BT_L2CAP_CMD_ECHO_RESP  0x09

#define BT_L2CAP_RESULT_SUCCESS 0x0000
#define BT_L2CAP_RESULT_PSM_BAD 0x0002
#define BT_L2CAP_RESULT_NO_RES  0x0004

int  bt_l2cap_init(void);
int  bt_l2cap_connect(u16 handle, u16 psm, u16 *local_cid_out);
int  bt_l2cap_disconnect(u16 local_cid);
int  bt_l2cap_echo(u16 handle, const u8 *data, u8 len,
                   u8 *resp, u16 *resp_len);
int  bt_l2cap_send(u16 local_cid, const u8 *data, u16 len);
/* channel data up-call: returns bytes consumed by a registered profile */
int  bt_l2cap_register_profile(u16 psm,
                               int (*recv)(u16 local_cid,
                                           const u8 *data, u16 len));
int  bt_l2cap_channel_count(void);
void bt_l2cap_dump(char *out, int outlen);   /* channel table view     */

/* ------------------------------------------------------------------ */
/* RFCOMM (TS 07.10 frames over L2CAP PSM 3)                           */
/* ------------------------------------------------------------------ */
#define BT_RFCOMM_SABM          0x2f
#define BT_RFCOMM_UA            0x63
#define BT_RFCOMM_DM            0x0f
#define BT_RFCOMM_DISC          0x43
#define BT_RFCOMM_UIH           0xef

#define BT_RFCOMM_MSC_CMD       0xe3
#define BT_RFCOMM_MSC_RESP      0xe1
#define BT_RFCOMM_PN_CMD        0x83
#define BT_RFCOMM_RPN_CMD       0x93

#define BT_RFCOMM_MSC_FC        0x01
#define BT_RFCOMM_MSC_DSR       0x02
#define BT_RFCOMM_MSC_RTR       0x04
#define BT_RFCOMM_MSC_DCD       0x08

int  bt_rfcomm_init(void);
void bt_rfcomm_set_peer(u16 acl_handle);  /* bind the remote ACL handle */
u16  bt_rfcomm_peer(void);
int  bt_rfcomm_open(u16 server_channel, u16 *dlci_out);   /* mux+DLC    */
int  bt_rfcomm_close(u16 dlci);
int  bt_rfcomm_send(u16 dlci, const u8 *data, u16 len);
int  bt_rfcomm_recv_register(int (*recv)(u16 dlci, const u8 *data, u16 len));
u8   bt_rfcomm_fcs(const u8 *data, int len);   /* real CRC-8 FCS       */
int  bt_rfcomm_dlci_count(void);
int  bt_rfcomm_poll(void);                    /* drain HCI events      */
void bt_rfcomm_register_l2cap(void);          /* PSM 3 profile hook    */

/* ------------------------------------------------------------------ */
/* SDP client (PSM 1)                                                  */
/* ------------------------------------------------------------------ */
int  bt_sdp_init(void);
int  bt_sdp_service_search(const u8 addr[6], u16 uuid16,
                           u32 *handles_out, int max, int *found_out);
int  bt_sdp_get_spp_channel(const u8 addr[6], u16 *server_channel_out);
void bt_sdp_register_l2cap(void);          /* PSM 1 pseudo-profile   */

/* ------------------------------------------------------------------ */
/* SPP (Serial Port Profile)                                           */
/* ------------------------------------------------------------------ */
int  bt_spp_init(void);
int  bt_spp_connect(const u8 addr[6], u16 *dlci_out);
int  bt_spp_send(u16 dlci, const u8 *data, u16 len);
int  bt_spp_recv_register(int (*recv)(u16 dlci, const u8 *data, u16 len));
int  bt_spp_echo(u16 dlci, const char *msg, char *resp_out, int cap);
/* capture hook used by `bt test` to check the real echo bytes back    */
u16  spp_capture_len(void);
u8   spp_capture_byte(u16 i);
void spp_capture_clear(void);
void bt_spp_register_capture(void);

/* ------------------------------------------------------------------ */
/* HID profile                                                         */
/* ------------------------------------------------------------------ */
#define BT_HID_BOOT_KEYBOARD    1
#define BT_HID_BOOT_MOUSE       2
#define BT_HID_REPORT_INPUT     1
#define BT_HID_REPORT_OUTPUT    2
#define BT_HID_REPORT_FEATURE   3

int  bt_hid_init(void);
int  bt_hid_connect(const u8 addr[6]);        /* ctrl+interrupt chans  */
int  bt_hid_get_report(u8 report_type, u8 *report_out, u16 *len_out);
int  bt_hid_last_report(u8 *report_out, u16 *len_out);  /* int channel */
int  bt_hid_disconnect(void);

#endif /* DRIVER_BT_H */
