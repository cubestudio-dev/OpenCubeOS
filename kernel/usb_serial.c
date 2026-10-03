/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_serial.c
 * Purpose: USB serial drivers - CDC-ACM and FTDI SIO.
 *
 * CDC-ACM is the standard USB virtual-COM profile (class 0x02
 * control interface with subclass 2 + class 0x0a data interface,
 * bulk IN + bulk OUT + optional notification interrupt IN).  Line
 * coding via SET_LINE_CODING (7 bit rate + 1 stop/parity/data bits),
 * DTR/RTS via SET_CONTROL_LINE_STATE.
 *
 * FTDI SIO is what QEMU's `-device usb-serial` models (vendor class
 * 0xff, vendor requests): RESET(0), MODEM_CTRL(1), FLOW(2),
 * BAUD(3), DATA(4) - so the same driver covers QEMU testing and the
 * huge installed base of FTDI USB-serial adapters (rule 7: mainstream).
 *
 * Both transports end in the same port object: bulk OUT for TX,
 * bulk IN drained into a small RX ring by usb_serial_poll().
 */
#include "usb_serial.h"
#include "usb.h"
#include "console.h"
#include "string.h"
#include "timer.h"

#define SER_MAX_PORTS 2
#define SER_RX_RING   256

/* FTDI vendor requests */
#define FTDI_REQ_RESET      0x00
#define FTDI_REQ_MODEM_CTRL 0x01
#define FTDI_REQ_SET_BAUD   0x03
#define FTDI_REQ_SET_DATA   0x04

/* CDC requests */
#define CDC_SET_LINE_CODING   0x20
#define CDC_SET_CONTROL_LINE  0x22

typedef enum { SER_T_CDC, SER_T_FTDI } ser_type_t;

typedef struct usb_serial_port {
    usb_dev_t *dev;
    ser_type_t type;
    usb_endpoint_t *ep_in, *ep_out;
    u8  ctrl_iface;
    int up;
    u8  rx[SER_RX_RING];
    volatile int rx_head, rx_tail;
    u64 tx_bytes, rx_bytes, rx_overruns;
    u32 baud;
} usb_serial_port_t;

static usb_serial_port_t g_port[SER_MAX_PORTS];

static void ser_log(const char *s) { oc_console_puts(s); }

static void ser_rx_push(usb_serial_port_t *p, const u8 *b, int n) {
    for (int i = 0; i < n; i++) {
        int used = (int)(p->rx_head - p->rx_tail);
        if (used >= SER_RX_RING) {
            p->rx_overruns++;
            break;
        }
        p->rx[p->rx_head % SER_RX_RING] = b[i];
        p->rx_head++;
    }
}

/* ------------------------------------------------------------------
 * CDC-ACM transport
 * ------------------------------------------------------------------ */

static int cdc_configure(usb_serial_port_t *p) {
    /* 115200 8N1 line coding */
    u8 lc[7];
    u32 baud = 115200;
    lc[0] = (u8)baud; lc[1] = (u8)(baud >> 8);
    lc[2] = (u8)(baud >> 16); lc[3] = (u8)(baud >> 24);
    lc[4] = 0;   /* 1 stop bit */
    lc[5] = 0;   /* no parity */
    lc[6] = 8;   /* 8 data bits */
    if (usb_control(p->dev, 0x21, CDC_SET_LINE_CODING, 0,
                    p->ctrl_iface, lc, 7) != 0) return -1;
    /* DTR + RTS on */
    if (usb_control(p->dev, 0x21, CDC_SET_CONTROL_LINE, 0x03,
                    p->ctrl_iface, NULL, 0) != 0) return -1;
    p->baud = baud;
    return 0;
}

/* ------------------------------------------------------------------
 * FTDI transport
 * ------------------------------------------------------------------ */

static int ftdi_configure(usb_serial_port_t *p) {
    /* reset, then 9600-ish baud divisor (QEMU ignores the value),
     * 8N1 */
    usb_control(p->dev, 0x40, FTDI_REQ_RESET, 0, p->ctrl_iface,
                NULL, 0);
    usb_control(p->dev, 0x40, FTDI_REQ_SET_BAUD, 0x4138,
                p->ctrl_iface, NULL, 0);
    usb_control(p->dev, 0x40, FTDI_REQ_SET_DATA, 0x0008,
                p->ctrl_iface, NULL, 0);
    usb_control(p->dev, 0x40, FTDI_REQ_MODEM_CTRL, 0x0303,
                p->ctrl_iface, NULL, 0);
    p->baud = 9600;
    return 0;
}

/* ------------------------------------------------------------------
 * class-driver probes
 * ------------------------------------------------------------------ */

static int ser_claim(usb_dev_t *dev, ser_type_t type) {
    for (int i = 0; i < SER_MAX_PORTS; i++)
        if (g_port[i].up && g_port[i].dev == dev) return 0;
    /* the bulk endpoints live on the CDC data interface (class 0x0a)
     * or the FTDI vendor interface (0xff/0xff/0xff); find_ep's first
     * argument is the INTERFACE NUMBER, not a class */
    usb_interface_t *difp = usb_find_if(dev, USB_CLASS_CDC_DATA, 0, 0, 0);
    usb_interface_t *vif = usb_find_if(dev, USB_CLASS_VENDOR, 0xff, 0xff, 0);
    u8 data_iface = 0;
    if (type == SER_T_CDC && difp) data_iface = difp->number;
    else if (vif) data_iface = vif->number;
    else if (type == SER_T_CDC) return -1;
    usb_endpoint_t *ep_in = usb_find_ep(dev, data_iface,
                                        USB_EP_ATTR_BULK, 1);
    usb_endpoint_t *ep_out = usb_find_ep(dev, data_iface,
                                         USB_EP_ATTR_BULK, 0);
    if (!ep_in || !ep_out) return -1;
    u8 ctrl_iface = 0;
    if (type == SER_T_CDC) {
        usb_interface_t *ifp =
            usb_find_if(dev, USB_CLASS_CDC, 0x02, 0x02, 0);
        if (!ifp) return -1;
        ctrl_iface = ifp->number;
    }
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        usb_serial_port_t *p = &g_port[i];
        if (p->up) continue;
        oc_memset(p, 0, sizeof(*p));
        p->dev = dev;
        p->type = type;
        p->ep_in = ep_in;
        p->ep_out = ep_out;
        p->ctrl_iface = ctrl_iface;
        int rc = (type == SER_T_CDC) ? cdc_configure(p)
                                     : ftdi_configure(p);
        if (rc != 0) {
            ser_log("usb-serial: configure failed\n");
            return -1;
        }
        p->up = 1;
        oc_console_puts(type == SER_T_CDC
                        ? "usb-serial: CDC-ACM port attached (115200 8N1)\n"
                        : "usb-serial: FTDI port attached (9600 8N1)\n");
        return 0;
    }
    return -1;
}

static int cdc_probe(usb_dev_t *dev) {
    /* CDC-ACM: control iface class 2 subclass 2 (protocol usually 1
     * for AT commands; accept any) + data iface class 0x0a */
    usb_interface_t *ctrl =
        usb_find_if(dev, USB_CLASS_CDC, 0x02, 0xff, 0);
    if (!ctrl) ctrl = usb_find_if(dev, USB_CLASS_CDC, 0x02, 0x02, 0);
    if (!ctrl) return -1;
    if (!usb_find_if(dev, USB_CLASS_CDC_DATA, 0, 0, 0)) return -1;
    return ser_claim(dev, SER_T_CDC);
}

static void cdc_disconnect(usb_dev_t *dev) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        if (g_port[i].up && g_port[i].dev == dev &&
            g_port[i].type == SER_T_CDC) {
            oc_memset(&g_port[i], 0, sizeof(g_port[i]));
            ser_log("usb-serial: CDC-ACM port removed\n");
        }
    }
}

static int ftdi_probe(usb_dev_t *dev) {
    /* FTDI/QEMU usb-serial: vendor-class INTERFACE (0xff/0xff/0xff;
     * the device class itself is 0x00 = interface-defined) with two
     * bulk endpoints (checked in ser_claim) */
    if (!usb_find_if(dev, USB_CLASS_VENDOR, 0xff, 0xff, 0)) return -1;
    return ser_claim(dev, SER_T_FTDI);
}

static void ftdi_disconnect(usb_dev_t *dev) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        if (g_port[i].up && g_port[i].dev == dev &&
            g_port[i].type == SER_T_FTDI) {
            oc_memset(&g_port[i], 0, sizeof(g_port[i]));
            ser_log("usb-serial: FTDI port removed\n");
        }
    }
}

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

void usb_serial_poll(void) {
    u8 buf[64];
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        usb_serial_port_t *p = &g_port[i];
        if (!p->up || !p->dev->present) continue;
        /* bulk IN with a short timeout: OC_USB_ETIMEDOUT simply
         * means "no data waiting this round" */
        int rc = usb_bulk_transfer_timeout(p->dev, p->ep_in->addr,
                                           buf, 64, 30);
        if (rc > 0) {
            ser_rx_push(p, buf, rc);
            p->rx_bytes += (u64)rc;
        } else if (rc == OC_USB_ESTALL) {
            usb_control(p->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                        p->ep_in->addr, NULL, 0);
            usb_tog_reset(p->dev, p->ep_in->addr);
        }
    }
}

int usb_serial_num_ports(void) {
    int n = 0;
    for (int i = 0; i < SER_MAX_PORTS; i++)
        if (g_port[i].up) n++;
    return n;
}

int usb_serial_write(const void *buf, int len) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        usb_serial_port_t *p = &g_port[i];
        if (!p->up || !p->dev->present) continue;
        const u8 *src = (const u8 *)buf;
        int sent = 0;
        while (sent < len) {
            u16 chunk = (u16)(len - sent);
            if (chunk > 64) chunk = 64;
            int rc = usb_bulk_transfer(p->dev, p->ep_out->addr,
                                       (void *)(src + sent), chunk);
            if (rc < 0) {
                if (rc == OC_USB_ESTALL) {
                    usb_control(p->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                                p->ep_out->addr, NULL, 0);
                    usb_tog_reset(p->dev, p->ep_out->addr);
                }
                return sent ? sent : rc;
            }
            sent += rc;
        }
        p->tx_bytes += (u64)sent;
        return sent;
    }
    return -1;
}

int usb_serial_read(void *buf, int max) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        usb_serial_port_t *p = &g_port[i];
        if (!p->up) continue;
        int n = 0;
        u8 *dst = (u8 *)buf;
        while (n < max && p->rx_tail < p->rx_head) {
            dst[n++] = p->rx[p->rx_tail % SER_RX_RING];
            p->rx_tail++;
        }
        return n;
    }
    return -1;
}

void usb_serial_print_state(void) {
    char line[128]; char n[24];
    int any = 0;
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        usb_serial_port_t *p = &g_port[i];
        if (!p->up) continue;
        any = 1;
        oc_strcpy(line, "  serial: ");
        oc_strcat(line, p->type == SER_T_CDC ? "CDC-ACM" : "FTDI");
        oc_strcat(line, " baud=");
        oc_u64_to_str(p->baud, n); oc_strcat(line, n);
        oc_strcat(line, " tx=");
        oc_u64_to_str(p->tx_bytes, n); oc_strcat(line, n);
        oc_strcat(line, " rx=");
        oc_u64_to_str(p->rx_bytes, n); oc_strcat(line, n);
        oc_strcat(line, " queued=");
        oc_u64_to_str((u64)(p->rx_head - p->rx_tail), n);
        oc_strcat(line, n);
        oc_strcat(line, " overruns=");
        oc_u64_to_str(p->rx_overruns, n); oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    if (!any) oc_console_puts("  serial: none\n");
}

int usb_serial_init(void) {
    usb_register_driver("cdc-acm", USB_CLASS_CDC,
                        cdc_probe, cdc_disconnect);
    return usb_register_driver("ftdi-serial", USB_CLASS_VENDOR,
                               ftdi_probe, ftdi_disconnect);
}
