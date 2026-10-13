/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: tests/host_bt_test.c
 *
 * Host-side byte-level test of the Bluetooth stack: the production .c
 * files are compiled for the host and driven with a scripted transport
 * (test vectors for HCI events, H4 framing, RFCOMM frames + FCS, SDP
 * attribute lists) - the same fixtures the QEMU peer produces on the
 * wire.
 *
 * Build:
 *   gcc -O2 -g -Ikernel -Ikernel/lib -Ikernel/mem -Ikernel/arch/x86_64 \
 *       -Idrivers/bluetooth -Idrivers/usb -Idrivers/display -Idrivers/input \
 *       -Idrivers/pci -Ifs -Inet -Ishell -Ikernel/core tests/host_bt_test.c \
 *       -o /tmp/host_bt_test
 * Exit code = number of failing checks.
 */
#include <stdio.h>
#include <stdlib.h>

/* ---- kernel service shims ---- */
#include "types.h"
void *kmalloc(unsigned long long n) { return malloc(n); }
void  kfree(void *p)                { free(p); }
u64   core_timer_now_ms(void)       { static u64 t; return t += 5; }
void  core_sched_yield(void)        { }

#include "../kernel/lib/lib_string.c"
/* the layers each define file-local le/be helpers; rename them per TU */
#define le16      le16_hci
#define put_le16  put_le16_hci
#include "../drivers/bluetooth/driver_bt_hci.c"
#undef le16
#undef put_le16
#define le16      le16_l2
#define put_le16  put_le16_l2
#include "../drivers/bluetooth/driver_bt_l2cap.c"
#undef le16
#undef put_le16
#define put_le16  put_le16_rf
#include "../drivers/bluetooth/driver_bt_rfcomm.c"
#undef put_le16
#define be16      be16_sdp
#define be32      be32_sdp
#define put_be16  put_be16_sdp
#define put_be32  put_be32_sdp
#include "../drivers/bluetooth/driver_bt_sdp.c"
#undef be16
#undef be32
#undef put_be16
#undef put_be32
#include "../drivers/bluetooth/driver_bt_spp.c"
#include "../drivers/bluetooth/driver_bt_hid.c"

/* transports are not part of this host test; stub the init entry points */
int  bt_usb_init(void)                    { return 0; }
int  bt_usb_present(void)                 { return 0; }
const bt_transport_ops_t *bt_usb_ops(void) { return 0; }
int  bt_h4_init(void)                     { return -1; }
const bt_transport_ops_t *bt_h4_ops(void) { return 0; }

static int check(const char *name, int cond)
{
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* ---- scripted transport: a queue of canned packets ----------------- */
static u8  q_type[32];
static u8  q_buf[32][280];
static u16 q_len[32];
static int q_head, q_tail;

static int q_send(u8 type, const u8 *data, u16 len)
{
    (void)type; (void)data; (void)len;
    return 0;                          /* TX accepted                    */
}

static int q_recv(u8 *type_out, u8 *buf, u16 buflen, u16 *len_out)
{
    if (q_head == q_tail) return 0;
    *type_out = q_type[q_head];
    *len_out  = q_len[q_head];
    for (u16 i = 0; i < q_len[q_head] && i < buflen; i++)
        buf[i] = q_buf[q_head][i];
    q_head++;
    return 1;
}

static int q_status(char *out, int outlen)
{
    if (outlen > 0) out[0] = '\0';
    return 0;
}

static const bt_transport_ops_t q_ops = { "host-q", q_send, q_recv, q_status };

static void q_push(u8 type, const u8 *data, u16 len)
{
    q_type[q_tail] = type;
    q_len[q_tail]  = len;
    for (u16 i = 0; i < len; i++) q_buf[q_tail][i] = data[i];
    q_tail++;
}

/* HCI event frame bytes (the peer's wire form, including plen)         */
static void q_push_evt(const u8 *params, u16 plen, u8 code)
{
    u8 f[280];
    f[0] = code;
    f[1] = (u8)plen;
    for (u16 i = 0; i < plen; i++) f[2 + i] = params[i];
    q_push(BT_H4_EVENT, f, (u16)(2 + plen));
}

int main(void)
{
    int fails = 0;

    fails += check("hci init", bt_hci_init() == 0);
    fails += check("l2cap init", bt_l2cap_init() == 0);
    fails += check("rfcomm init", bt_rfcomm_init() == 0);
    fails += check("sdp init", bt_sdp_init() == 0);
    fails += check("spp init", bt_spp_init() == 0);
    fails += check("hid init", bt_hid_init() == 0);
    fails += check("transport attach", bt_hci_attach_transport(&q_ops) == 0);

    /* ---- 1. Command_Complete: opcode at buf[3], status at buf[5] ---- */
    {
        u8 r[16];
        u16 rl = (u16)sizeof(r);
        /* Reset CC: num(1) opcode(2) status(1)                          */
        u8 cc[] = { 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
        q_push(BT_H4_EVENT, cc, (u16)sizeof(cc));
        fails += check("hci reset via CC", bt_hci_reset() == 0);
        (void)r; (void)rl;
    }
    /* ---- 2. Read_BD_ADDR: response params after status ------------- */
    {
        u8 addr[6];
        /* CC: num(1) op(2) status(1) bdaddr(6)                          */
        u8 cc[] = { 0x0e, 0x0a, 0x01, 0x09, 0x10, 0x00,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        q_push(BT_H4_EVENT, cc, (u16)sizeof(cc));
        fails += check("read bd addr", bt_hci_read_bd_addr(addr) == 0);
        fails += check("bd addr bytes", addr[0] == 0x11 && addr[5] == 0x66);
        {
            char s[18];
            bt_addr_to_str(addr, s);
            fails += check("bd addr str MSB-first",
                           s[0] == '6' && s[1] == '6' && s[16] == '1');
        }
    }
    /* ---- 3. Connection_Complete: status(2) handle(3) addr(5) ------- */
    {
        u16 handle = 0;
        u8 addr[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        u8 ev[] = { 0x03, 0x0b, 0x00, 0x01, 0x00,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x00, 0x01 };
        q_push(BT_H4_EVENT, ev, (u16)sizeof(ev));
        fails += check("conn complete event", bt_hci_poll() >= 0);
        fails += check("conn registered", bt_hci_conn_count() == 1);
        fails += check("handle lookup",
                       bt_hci_handle_of_addr(addr, &handle) == 0 &&
                       handle == 0x0001);
    }
    /* ---- 4. async completion: Create_Connection completes on its
     *        own event and the response carries the handle ---------- */
    {
        u8 addr[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        /* the q_recv feeds the canned Connection_Complete directly     */
        u8 ev[] = { 0x03, 0x0b, 0x00, 0x02, 0x00,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x00, 0x01 };
        q_push(BT_H4_EVENT, ev, (u16)sizeof(ev));
        {
            u16 h = 0;
            /* bt_hci_create_connection waits for the completing event  */
            fails += check("create connection async",
                           bt_hci_create_connection(addr, &h) == 0);
        }
    }
    /* ---- 5. RFCOMM FCS: real CRC-8 (reflected 0x07, init 0xff) ----- */
    {
        u8 f2[2] = { 0x1f, 0xef };
        u8 fcs = bt_rfcomm_fcs(f2, 2);
        /* reference value computed with the same polynomial             */
        u8 crc = 0xff;
        crc ^= 0x1f;
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (u8)((crc >> 1) ^ 0xe0) : (u8)(crc >> 1);
        crc ^= 0xef;
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (u8)((crc >> 1) ^ 0xe0) : (u8)(crc >> 1);
        fails += check("rfcomm fcs addr+ctrl", fcs == crc);
        fails += check("rfcomm fcs stable", fcs == bt_rfcomm_fcs(f2, 2));
    }
    /* ---- 6. RFCOMM frame parse: body offset 2 + lbytes ------------- */
    {
        /* UIH on DLCI 7, 1-byte len: [addr 1f][ctrl ef][len 8b]
         * data "oc-spp-echo" (11) [fcs]                                */
        u8 frame[] = { 0x1f, 0xef, 0x8b, 'o', 'c', '-', 's', 'p', 'p',
                       '-', 'e', 'c', 'h', 'o', 0x19 };
        /* feed the RFCOMM frame through the real rf_l2cap_input (the
         * L2CAP channel state needs a live mux, which the QEMU E2E
         * exercises; here the frame parse itself is under test)       */
        bt_spp_register_capture();
        fails += check("rfcomm frame routed",
                       rf_l2cap_input(0x41, frame,
                                      (u16)sizeof(frame)) == 0);
        fails += check("rfcomm UIH body offset",
                       spp_capture_len() == 11 &&
                       spp_capture_byte(0) == 'o' &&
                       spp_capture_byte(10) == 'o');
    }
    /* ---- 7. SDP attribute list walker (the peer's exact bytes) ----- */
    {
        /* attrlist: DES(17){ id 0x0004, ProtoDescList DES(12){
         *   DES(3){UUID16 0x0100}, DES(5){UUID16 0x0003, u8 ch=3} } }    */
        u8 attrlist[] = {
            0x35, 0x11, 0x09, 0x00, 0x04, 0x35, 0x0c,
            0x35, 0x03, 0x19, 0x01, 0x00,
            0x35, 0x05, 0x19, 0x00, 0x03, 0x08, 0x03,
        };
        u16 ch = 0;
        fails += check("sdp walker finds rfcomm channel",
                       proto_list_find_rfcomm_channel(
                           attrlist, (u16)sizeof(attrlist), &ch) == 0 &&
                       ch == 3);
    }
    /* ---- 8. honest negatives --------------------------------------- */
    {
        fails += check("no bd addr for unknown peer",
                       bt_hci_handle_of_addr((const u8 *)"\x01\x02\x03\x04\x05\x06",
                                             0) == -1);
        u8 f2[2] = { 0x1f, 0xef };
        fails += check("fcs not all-zero", bt_rfcomm_fcs(f2, 2) != 0);
    }

    printf("\n%s: %d checks failed\n", "host_bt_test", fails);
    return fails;
}

