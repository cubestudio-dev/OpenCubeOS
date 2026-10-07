/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_serial.c
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
 * bulk IN drained into a small RX ring by driver_usb_serial_poll().
 *
 * Multi-port surface (BUG-0170 FIX, A11-44): the legacy handle-free
 * driver_usb_serial_write()/read() target the FIRST up port, with
 * read() aggregating across all ports. Explicit routing to any port
 * is available through driver_usb_serial_write_port(port, buf, len)
 * (prototype declared in this file; header sync note in the worklog).
 */
#include "driver_usb_serial.h"
#include "driver_usb.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"

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

typedef struct driver_usb_serial_port {
    driver_usb_dev_t *dev;
    ser_type_t type;
    driver_usb_endpoint_t *ep_in, *ep_out;
    u8  ctrl_iface;
    int up;
    u8  rx[SER_RX_RING];
    volatile int rx_head, rx_tail;
    u64 tx_bytes, rx_bytes, rx_overruns;
    u32 baud;
} driver_usb_serial_port_t;

static driver_usb_serial_port_t g_port[SER_MAX_PORTS];

static driver_usb_interface_t *driver_usb_serial_find_cdc_ctrl(
        driver_usb_dev_t *dev);
static void ser_log(const char *s) { screen_console_puts(s); }

/* port-indexed write entry point (BUG-0170); prototype lives here
 * because the class header is outside this fix's file scope */
int driver_usb_serial_write_port(int port, const void *buf, int len);

static void ser_rx_push(driver_usb_serial_port_t *p, const u8 *b, int n) {
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

static int driver_usb_serial_cdc_configure(driver_usb_serial_port_t *p) {
    /* 115200 8N1 line coding */
    u8 lc[7];
    u32 baud = 115200;
    lc[0] = (u8)baud; lc[1] = (u8)(baud >> 8);
    lc[2] = (u8)(baud >> 16); lc[3] = (u8)(baud >> 24);
    lc[4] = 0;   /* 1 stop bit */
    lc[5] = 0;   /* no parity */
    lc[6] = 8;   /* 8 data bits */
    if (driver_usb_control(p->dev, 0x21, CDC_SET_LINE_CODING, 0,
                    p->ctrl_iface, lc, 7) != 0) return -1;
    /* DTR + RTS on */
    if (driver_usb_control(p->dev, 0x21, CDC_SET_CONTROL_LINE, 0x03,
                    p->ctrl_iface, NULL, 0) != 0) return -1;
    p->baud = baud;
    return 0;
}

/* ------------------------------------------------------------------
 * FTDI transport
 * ------------------------------------------------------------------ */

static int driver_usb_serial_ftdi_configure(driver_usb_serial_port_t *p) {
    /* reset, then 9600-ish baud divisor (QEMU ignores the value),
     * 8N1 */
    driver_usb_control(p->dev, 0x40, FTDI_REQ_RESET, 0, p->ctrl_iface,
                NULL, 0);
    driver_usb_control(p->dev, 0x40, FTDI_REQ_SET_BAUD, 0x4138,
                p->ctrl_iface, NULL, 0);
    driver_usb_control(p->dev, 0x40, FTDI_REQ_SET_DATA, 0x0008,
                p->ctrl_iface, NULL, 0);
    driver_usb_control(p->dev, 0x40, FTDI_REQ_MODEM_CTRL, 0x0303,
                p->ctrl_iface, NULL, 0);
    p->baud = 9600;
    return 0;
}

/* ------------------------------------------------------------------
 * class-driver probes
 * ------------------------------------------------------------------ */

static int ser_claim(driver_usb_dev_t *dev, ser_type_t type) {
    for (int i = 0; i < SER_MAX_PORTS; i++)
        if (g_port[i].up && g_port[i].dev == dev) return 0;
    /* the bulk endpoints live on the CDC data interface (class 0x0a)
     * or the FTDI vendor interface (0xff/0xff/0xff); find_ep's first
     * argument is the INTERFACE NUMBER, not a class */
    driver_usb_interface_t *difp = driver_usb_find_if(dev, USB_CLASS_CDC_DATA, 0, 0, 0);
    driver_usb_interface_t *vif = driver_usb_find_if(dev, USB_CLASS_VENDOR, 0xff, 0xff, 0);
    u8 data_iface = 0;
    if (type == SER_T_CDC && difp) data_iface = difp->number;
    else if (vif) data_iface = vif->number;
    else if (type == SER_T_CDC) return -1;
    driver_usb_endpoint_t *ep_in = driver_usb_find_ep(dev, data_iface,
                                        USB_EP_ATTR_BULK, 1);
    driver_usb_endpoint_t *ep_out = driver_usb_find_ep(dev, data_iface,
                                         USB_EP_ATTR_BULK, 0);
    if (!ep_in || !ep_out) return -1;
    u8 ctrl_iface = 0;
    if (type == SER_T_CDC) {
        /* BUG-0055 FIX (connect side): same protocol restriction as the
         * probe had - the port was claimed with bInterfaceProtocol=0x00
         * but the open path only accepted 0x02 and failed right after. */
        driver_usb_interface_t *ifp = driver_usb_serial_find_cdc_ctrl(dev);
        if (!ifp) return -1;
        ctrl_iface = ifp->number;
    }
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        driver_usb_serial_port_t *p = &g_port[i];
        if (p->up) continue;
        memset(p, 0, sizeof(*p));
        p->dev = dev;
        p->type = type;
        p->ep_in = ep_in;
        p->ep_out = ep_out;
        p->ctrl_iface = ctrl_iface;
        int rc = (type == SER_T_CDC) ? driver_usb_serial_cdc_configure(p)
                                     : driver_usb_serial_ftdi_configure(p);
        if (rc != 0) {
            ser_log("usb-serial: configure failed\n");
            return -1;
        }
        p->up = 1;
        screen_console_puts(type == SER_T_CDC
                        ? "usb-serial: CDC-ACM port attached (115200 8N1)\n"
                        : "usb-serial: FTDI port attached (9600 8N1)\n");
        return 0;
    }
    return -1;
}

/* BUG-0055 FIX: locate the CDC-ACM control interface without pinning
 * bInterfaceProtocol. driver_usb_find_if matches exactly, and the old
 * probe asked for protocol 0xff first (a value real devices essentially
 * never report) and 0x02 second - so the overwhelmingly common
 * bInterfaceProtocol=0x00 devices were never claimed at all. Try the
 * documented values in order: 0x02 (AT commands, v.25ter), 0x01
 * (AT commands), 0x00 (no specific protocol). */
static driver_usb_interface_t *driver_usb_serial_find_cdc_ctrl(
        driver_usb_dev_t *dev) {
    static const u8 protos[] = { 0x02, 0x01, 0x00 };
    for (int i = 0; i < 3; i++) {
        driver_usb_interface_t *ifp =
            driver_usb_find_if(dev, USB_CLASS_CDC, 0x02, protos[i], 0);
        if (ifp) return ifp;
    }
    return NULL;
}

static int driver_usb_serial_cdc_probe(driver_usb_dev_t *dev) {
    /* CDC-ACM: control iface class 2 subclass 2 (any protocol) +
     * data iface class 0x0a */
    driver_usb_interface_t *ctrl = driver_usb_serial_find_cdc_ctrl(dev);
    if (!ctrl) return -1;
    if (!driver_usb_find_if(dev, USB_CLASS_CDC_DATA, 0, 0, 0)) return -1;
    return ser_claim(dev, SER_T_CDC);
}

static void driver_usb_serial_cdc_disconnect(driver_usb_dev_t *dev) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        if (g_port[i].up && g_port[i].dev == dev &&
            g_port[i].type == SER_T_CDC) {
            memset(&g_port[i], 0, sizeof(g_port[i]));
            ser_log("usb-serial: CDC-ACM port removed\n");
        }
    }
}

static int driver_usb_serial_ftdi_probe(driver_usb_dev_t *dev) {
    /* FTDI/QEMU usb-serial: vendor-class INTERFACE (0xff/0xff/0xff;
     * the device class itself is 0x00 = interface-defined) with two
     * bulk endpoints (checked in ser_claim) */
    if (!driver_usb_find_if(dev, USB_CLASS_VENDOR, 0xff, 0xff, 0)) return -1;
    return ser_claim(dev, SER_T_FTDI);
}

static void driver_usb_serial_ftdi_disconnect(driver_usb_dev_t *dev) {
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        if (g_port[i].up && g_port[i].dev == dev &&
            g_port[i].type == SER_T_FTDI) {
            memset(&g_port[i], 0, sizeof(g_port[i]));
            ser_log("usb-serial: FTDI port removed\n");
        }
    }
}

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

void driver_usb_serial_poll(void) {
    u8 buf[64];
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        driver_usb_serial_port_t *p = &g_port[i];
        if (!p->up || !p->dev->present) continue;
        /* bulk IN with a short timeout: OC_USB_ETIMEDOUT simply
         * means "no data waiting this round" */
        /* BUG-0171 FIX (A11-45): the idle probe used to run a 30 ms
         * bulk-IN wait while HOLDING the core g_usb_lock
         * (driver_usb_bulk_transfer_timeout locks the core around the
         * host ops->bulk call), so a single idle serial port stalled
         * keyboard/MSC traffic by up to 30 ms per poll round - 60 ms
         * for two ports. The final probe timeout is 3 ms: still a
         * handful of 1 ms frame opportunities for a NAKing device to
         * hand over queued data, but the lock is released ~10x sooner
         * so same-round HID/MSC work is no longer perceptibly
         * delayed. */
        int rc = driver_usb_bulk_transfer_timeout(p->dev, p->ep_in->addr,
                                           buf, 64, 3);
        if (rc > 0) {
            ser_rx_push(p, buf, rc);
            p->rx_bytes += (u64)rc;
        } else if (rc == OC_USB_ESTALL) {
            driver_usb_control(p->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                        p->ep_in->addr, NULL, 0);
            driver_usb_tog_reset(p->dev, p->ep_in->addr);
        }
    }
}

int driver_usb_serial_num_ports(void) {
    int n = 0;
    for (int i = 0; i < SER_MAX_PORTS; i++)
        if (g_port[i].up) n++;
    return n;
}

/* Chunked blocking bulk-OUT to ONE port with stall recovery.
 * Returns the number of bytes sent, or a negative error when nothing
 * could be sent. */
static int ser_port_write(driver_usb_serial_port_t *p, const void *buf, int len) {
    const u8 *src = (const u8 *)buf;
    int sent = 0;
    while (sent < len) {
        u16 chunk = (u16)(len - sent);
        if (chunk > 64) chunk = 64;
        int rc = driver_usb_bulk_transfer(p->dev, p->ep_out->addr,
                                   (void *)(src + sent), chunk);
        if (rc < 0) {
            if (rc == OC_USB_ESTALL) {
                driver_usb_control(p->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                            p->ep_out->addr, NULL, 0);
                driver_usb_tog_reset(p->dev, p->ep_out->addr);
            }
            return sent ? sent : rc;
        }
        if (rc == 0) break;   /* defensive (A11-47): never spin on 0 */
        sent += rc;
    }
    p->tx_bytes += (u64)sent;
    return sent;
}

/* BUG-0170 FIX (A11-44): explicit per-port routing. The legacy
 * handle-free write() can only ever talk to the first up port; with
 * two ports attached the second one was write-dead. Callers that
 * know which port they target use this entry point. */
int driver_usb_serial_write_port(int port, const void *buf, int len) {
    if (port < 0 || port >= SER_MAX_PORTS) return -1;
    driver_usb_serial_port_t *p = &g_port[port];
    if (!p->up || !p->dev->present) return -1;
    return ser_port_write(p, buf, len);
}

int driver_usb_serial_write(const void *buf, int len) {
    /* BUG-0170 FIX (A11-44): the legacy entry point routes to the
     * first up port (the same port read() drains first), now via the
     * shared ser_port_write helper so both entry points behave
     * identically; the previous comment claimed per-port routing that
     * the code never did. */
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        driver_usb_serial_port_t *p = &g_port[i];
        if (!p->up || !p->dev->present) continue;
        return ser_port_write(p, buf, len);
    }
    return -1;
}

int driver_usb_serial_read(void *buf, int max) {
    if (!buf || max <= 0) return -1;
    u8 *dst = (u8 *)buf;
    int total = 0;
    for (int i = 0; i < SER_MAX_PORTS && total < max; i++) {
        driver_usb_serial_port_t *p = &g_port[i];
        if (!p->up) continue;
        /* BUG-0170 FIX (A11-44): an empty ring must not short-circuit
         * the scan. The old loop returned at the FIRST up port even
         * when its ring was empty, so with two ports attached data
         * received on the second port was unreachable. Keep scanning
         * the remaining ports and aggregate the bytes into the
         * caller's buffer until it is full or every ring is drained. */
        while (total < max && p->rx_tail < p->rx_head) {
            dst[total++] = p->rx[p->rx_tail % SER_RX_RING];
            p->rx_tail++;
        }
    }
    return total > 0 ? total : -1;
}

void driver_usb_serial_print_state(void) {
    char line[128]; char n[24];
    int any = 0;
    for (int i = 0; i < SER_MAX_PORTS; i++) {
        driver_usb_serial_port_t *p = &g_port[i];
        if (!p->up) continue;
        any = 1;
        strcpy(line, "  serial: ");
        strcat(line, p->type == SER_T_CDC ? "CDC-ACM" : "FTDI");
        strcat(line, " baud=");
        u64_to_str(p->baud, n); strcat(line, n);
        strcat(line, " tx=");
        u64_to_str(p->tx_bytes, n); strcat(line, n);
        strcat(line, " rx=");
        u64_to_str(p->rx_bytes, n); strcat(line, n);
        strcat(line, " queued=");
        u64_to_str((u64)(p->rx_head - p->rx_tail), n);
        strcat(line, n);
        strcat(line, " overruns=");
        u64_to_str(p->rx_overruns, n); strcat(line, n);
        screen_console_puts(line);
        screen_console_puts("\n");
    }
    if (!any) screen_console_puts("  serial: none\n");
}

int driver_usb_serial_init(void) {
    driver_usb_register_driver("cdc-acm", USB_CLASS_CDC,
                        driver_usb_serial_cdc_probe, driver_usb_serial_cdc_disconnect);
    return driver_usb_register_driver("ftdi-serial", USB_CLASS_VENDOR,
                               driver_usb_serial_ftdi_probe, driver_usb_serial_ftdi_disconnect);
}
