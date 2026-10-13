/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_usb.c
 *
 * HCI-USB Bluetooth transport (Core 5.x vol 4.B "USB Transport Layer"):
 *   HCI commands  -> default control pipe (class request 0x00)
 *   HCI events    -> interrupt IN endpoint
 *   ACL data      -> bulk OUT/IN endpoints
 *
 * The btusb class driver registers on the existing USB stack with
 * interface class 0xE0 (wireless controller).  With no Bluetooth dongle
 * attached (QEMU has no usb-bt-dongle model) the class driver simply
 * never claims a device and bt_usb_present() reports 0 honestly.
 */
#include "types.h"
#include "driver_bt.h"
#include "driver_usb.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

static driver_usb_dev_t *g_dev;
static u8 g_ep_evt;                  /* interrupt IN (events)          */
static u8 g_ep_acl_in;               /* bulk IN  (ACL RX)              */
static u8 g_ep_acl_out;              /* bulk OUT (ACL TX)              */

static int bt_usb_probe(driver_usb_dev_t *d);
static void bt_usb_disconnect(driver_usb_dev_t *d);

int bt_usb_init(void)
{
    g_dev = 0;
    return driver_usb_register_driver("btusb", 0xe0, bt_usb_probe,
                                      bt_usb_disconnect);
}

int bt_usb_present(void)
{
    return g_dev != 0;
}

static int bt_usb_probe(driver_usb_dev_t *d)
{
    driver_usb_endpoint_t *ep;
    driver_usb_interface_t *ifc;

    if (!d || g_dev) return -1;        /* one dongle                     */
    /* HCI-USB requires interface class 0xE0 subclass 1 protocol 1      */
    ifc = driver_usb_find_if(d, 0xe0, 0x01, 0x01, 0);
    if (!ifc) return -1;
    ep = driver_usb_find_ep(d, 0, USB_EP_ATTR_INTERRUPT, 1);
    if (!ep) return -1;                /* events are mandatory           */
    g_ep_evt = ep->addr;
    ep = driver_usb_find_ep(d, 0, USB_EP_ATTR_BULK, 1);
    g_ep_acl_in  = ep ? ep->addr : 0;
    ep = driver_usb_find_ep(d, 0, USB_EP_ATTR_BULK, 0);
    g_ep_acl_out = ep ? ep->addr : 0;
    g_dev = d;
    return 0;
}

static void bt_usb_disconnect(driver_usb_dev_t *d)
{
    if (g_dev == d) {
        g_dev = 0;
        g_ep_evt = g_ep_acl_in = g_ep_acl_out = 0;
    }
}

static int bt_usb_send(u8 type, const u8 *data, u16 len)
{
    if (!g_dev) return -1;
    if (type == BT_H4_COMMAND) {
        /* class request on the default pipe (vol 4.B 3.2)              */
        driver_usb_setup_t st;
        memset((void *)&st, 0, sizeof(st));
        st.bmRequestType = 0x21;       /* host-to-device, class, iface  */
        st.bRequest      = 0x00;       /* HCI command                   */
        st.wValue        = 0;
        st.wIndex        = 0;
        st.wLength       = len;
        return driver_usb_control_transfer(g_dev, &st, (void *)data, len);
    }
    if (type == BT_H4_ACL) {
        if (!g_ep_acl_out) return -2;
        return driver_usb_bulk_transfer(g_dev, g_ep_acl_out,
                                        (void *)data, len);
    }
    return -3;                          /* we never send events/SCO      */
}

static int bt_usb_recv(u8 *type_out, u8 *buf, u16 buflen, u16 *len_out)
{
    int n;

    if (!g_dev) return -1;
    if (g_ep_evt) {
        n = driver_usb_interrupt_transfer(g_dev, g_ep_evt,
                                          buf, (u16)(buflen - 1));
        if (n > 0) {
            *type_out = BT_H4_EVENT;
            *len_out  = (u16)n;
            return 1;
        }
    }
    if (g_ep_acl_in) {
        n = driver_usb_bulk_transfer_timeout(g_dev, g_ep_acl_in,
                                             buf, (u16)(buflen - 1), 1);
        if (n > 0) {
            *type_out = BT_H4_ACL;
            *len_out  = (u16)n;
            return 1;
        }
    }
    return 0;                           /* nothing pending: honest        */
}

static int bt_usb_poll_status(char *out, int outlen)
{
    int o = 0;
    if (outlen > 0) out[0] = '\0';
    if (!g_dev) {
        const char *s = "usb-bt: no dongle attached\n";
        while (s[o] && o < outlen - 1) { out[o] = s[o]; o++; }
        out[o] = '\0';
        return -1;
    }
    {
        const char *s = "usb-bt: dongle present\n";
        while (s[o] && o < outlen - 1) { out[o] = s[o]; o++; }
        out[o] = '\0';
    }
    return 0;
}

static const bt_transport_ops_t g_usb_ops = {
    "hci-usb", bt_usb_send, bt_usb_recv, bt_usb_poll_status,
};

const bt_transport_ops_t *bt_usb_ops(void) { return &g_usb_ops; }
