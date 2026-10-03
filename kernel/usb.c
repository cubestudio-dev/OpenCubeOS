/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c / WP-10d
 * File: kernel/usb.c
 * Purpose: USB core: host-controller registry, device tree, the four
 *          transfer types, enumeration, hub support, hot-plug and the
 *          class-driver registry.  The UHCI backend lives here too.
 *
 * WP-10d architecture:
 *
 *   class drivers (usb_hid / usb_msc / usb_serial / usb_audio)
 *        |  usb_register_driver(name, class, probe, disconnect)
 *        v
 *   USB core (this file)
 *     - device table (16 slots) + device tree (parent hub / port)
 *     - enumeration: reset port, GET_DESCRIPTOR(8), SET_ADDRESS,
 *       GET_DESCRIPTOR(18), GET_DESCRIPTOR(config), SET_CONFIG,
 *       string descriptors, class-driver probe
 *     - transfer dispatch: usb_control_transfer / usb_bulk_transfer /
 *       usb_interrupt_transfer / usb_isochronous_transfer -> hc ops
 *     - hub support: hub devices are probed by the built-in hub
 *       driver; their ports are enumerated recursively (depth <= 4)
 *     - hot-plug: usb_poll() checks connect-status-change on every
 *       root-hub port and hub port; disconnects tear the device
 *       (and its children) down through the class disconnect hooks
 *        |  usb_hc_ops_t
 *        v
 *   host controllers: UHCI (here), OHCI / EHCI / XHCI (own files)
 *
 * UHCI backend (Intel PIIX3/PIIX4, the QEMU piix3-usb-uhci):
 *   I/O BAR registers per the Intel UHCI spec:
 *     0x00 USBCMD (RS bit0, HCRESET bit1), 0x02 USBSTS (HCHalted bit5),
 *     0x04 USBINTR, 0x06 FRNUM, 0x08 FLBASEADD (4 KiB aligned),
 *     0x0C SOFMOD, 0x10/0x12 PORTSC1/2.
 *   Frame schedule (per 1 ms frame):
 *     frame_list[i] -> ISO TD -> int QH -> ctrl QH -> bulk QH -> term
 *   The WP-10c audio path re-arms the ISO TD every frame and is
 *   untouched; interrupt/ctrl/bulk QHs are chained behind it so the
 *   controller reaches every schedule element each frame.
 *
 * Bit definitions verified against QEMU include/hw/usb/uhci-regs.h.
 */
#include "usb.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"
#include "sync.h"

/* ==================================================================
 * small helpers
 * ================================================================== */

static void usb_log(const char *s) { oc_console_puts(s); }


static void usb_num_dec(u64 v, char *out) {
    oc_u64_to_str(v, out);
}

static void usb_hex(u64 v, char *out, int digits) {
    oc_u64_to_hex(v, out, digits);
}

/* ==================================================================
 * core tables
 * ================================================================== */

#define USB_MAX_DRIVERS 8
#define USB_ENUM_DEPTH  4

typedef struct usb_driver {
    char        name[24];
    u8          class_code;      /* 0xff = match-all */
    usb_probe_fn      probe;
    usb_disconnect_fn disconnect;
    int         used;
} usb_driver_t;

typedef struct usb_core {
    usb_host_t   hosts[USB_MAX_HOSTS];
    int          n_hosts;
    usb_dev_t    devs[USB_MAX_DEVICES];
    usb_driver_t drivers[USB_MAX_DRIVERS];
    int          n_drivers;
    int          next_addr;      /* next SET_ADDRESS value */
    int          up;             /* at least one host registered */
} usb_core_t;

static usb_core_t g_core;

/* built-in hub helpers (defined below; used by the enumerator) */
static int  usb_hub_enumerate(usb_dev_t *hub);
static int  usb_hub_port_reset(usb_dev_t *hub, u8 port);
static u8   usb_hub_port_speed(usb_dev_t *hub, u8 port);

usb_dev_t *usb_get_device(int idx) {
    if (idx < 0 || idx >= USB_MAX_DEVICES) return NULL;
    return g_core.devs[idx].present ? &g_core.devs[idx] : NULL;
}

int usb_num_devices(void) {
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++)
        if (g_core.devs[i].present) n++;
    return n;
}

int usb_num_hosts(void) { return g_core.n_hosts; }

int usb_register_driver(const char *name, u8 class_code,
                        usb_probe_fn probe,
                        usb_disconnect_fn disconnect) {
    if (!name || !probe) return -1;
    if (g_core.n_drivers >= USB_MAX_DRIVERS) return -1;
    usb_driver_t *dr = &g_core.drivers[g_core.n_drivers];
    oc_memset(dr, 0, sizeof(*dr));
    oc_strcpy(dr->name, name);
    dr->class_code = class_code;
    dr->probe = probe;
    dr->disconnect = disconnect;
    dr->used = 1;
    return g_core.n_drivers++;
}

int usb_num_drivers(void) { return g_core.n_drivers; }

int usb_register_host(usb_host_t *host, const usb_hc_ops_t *ops) {
    if (!host || !ops || !ops->name) return -1;
    if (g_core.n_hosts >= USB_MAX_HOSTS) return -1;
    int idx = g_core.n_hosts++;
    usb_host_t *h = &g_core.hosts[idx];
    oc_memcpy(h, host, sizeof(*h));
    h->index = idx;
    h->ops = ops;
    h->up = 1;
    g_core.up = 1;
    if (host != h) oc_memcpy(host, h, sizeof(*h));
    usb_log("usb: registered host ");
    usb_log(ops->name);
    usb_log("\n");
    return 0;
}

/* ==================================================================
 * descriptor walking (shared with class drivers)
 * ================================================================== */

static void usb_parse_config(usb_dev_t *d) {
    d->n_ep = 0;
    d->n_if = 0;
    const u8 *p = d->cfg_raw;
    const u8 *end = d->cfg_raw + d->cfg_len;
    int cur_if = -1;
    while (p + 2 <= end) {
        u8 len = p[0];
        if (len < 2 || p + len > end) break;
        if (p[1] == USB_DT_INTERFACE && len >= 9 && d->n_if < USB_MAX_IFS) {
            usb_if_desc_t id;
            oc_memcpy(&id, p, sizeof(id));
            usb_interface_t *dst = &d->ifs[d->n_if++];
            dst->number = id.bInterfaceNumber;
            dst->alt = id.bAlternateSetting;
            dst->class = id.bInterfaceClass;
            dst->subclass = id.bInterfaceSubClass;
            dst->protocol = id.bInterfaceProtocol;
            dst->ep_start = d->n_ep;
            dst->ep_count = 0;
            cur_if = d->n_if - 1;
        } else if (p[1] == USB_DT_ENDPOINT && len >= 7 &&
                   d->n_ep < USB_MAX_EPS && cur_if >= 0) {
            usb_ep_desc_t ed;
            oc_memcpy(&ed, p, sizeof(ed));
            usb_endpoint_t *ep = &d->eps[d->n_ep++];
            ep->addr = ed.bEndpointAddress;
            ep->attr = (u8)(ed.bmAttributes & 3);
            ep->maxpack = ed.wMaxPacketSize;
            ep->interval = ed.bInterval;
            ep->iface = d->ifs[cur_if].number;
            d->ifs[cur_if].ep_count++;
        }
        p += len;
    }
}

usb_endpoint_t *usb_find_ep(usb_dev_t *d, u8 iface, u8 attr, int in) {
    for (int i = 0; i < d->n_ep; i++) {
        usb_endpoint_t *ep = &d->eps[i];
        int ep_in = (ep->addr & 0x80) ? 1 : 0;
        if (ep->iface == iface && ep->attr == attr && ep_in == (in ? 1 : 0))
            return ep;
    }
    return NULL;
}

usb_interface_t *usb_find_if(usb_dev_t *d, u8 class, u8 subclass,
                             u8 proto, int nth) {
    int seen = 0;
    for (int i = 0; i < d->n_if; i++) {
        usb_interface_t *ifp = &d->ifs[i];
        if (ifp->class == class && ifp->subclass == subclass &&
            ifp->protocol == proto) {
            if (seen == nth) return ifp;
            seen++;
        }
    }
    return NULL;
}

int usb_dev_by_class(usb_dev_t *d, u8 class) {
    if (d->class == class) return 1;
    for (int i = 0; i < d->n_if; i++)
        if (d->ifs[i].class == class) return 1;
    return 0;
}

/* ==================================================================
 * transfers through the host ops
 * ================================================================== */

/* One global transfer mutex: the blocking wait loops yield, so two
 * kernel threads (usb poll kthread + shell) could otherwise submit
 * into the same controller TD/QH pools concurrently.  Every transfer
 * dispatch and every backend poll() takes this lock. */
static mutex_t g_usb_lock;
static int     g_usb_lock_ready;

static void usb_lock(void) {
    if (!g_usb_lock_ready) {
        mutex_init(&g_usb_lock);
        g_usb_lock_ready = 1;
    }
    mutex_lock(&g_usb_lock);
}

static void usb_unlock(void) {
    mutex_unlock(&g_usb_lock);
}

int usb_control_transfer(usb_dev_t *d, const usb_setup_t *setup,
                         void *buf, u16 len) {
    if (!d || !d->host || !d->host->ops || !d->host->ops->control)
        return -1;
    if (!setup) return -1;
    usb_lock();
    int rc = d->host->ops->control(d->host, d, setup, buf, len, 1000);
    usb_unlock();
    return rc;
}

int usb_bulk_transfer_timeout(usb_dev_t *d, u8 ep_addr, void *buf,
                              u16 len, u32 timeout_ms) {
    if (!d || !d->host || !d->host->ops || !d->host->ops->bulk)
        return -1;
    usb_lock();
    int rc = d->host->ops->bulk(d->host, d, ep_addr, buf, len,
                                timeout_ms);
    usb_unlock();
    return rc;
}

int usb_bulk_transfer(usb_dev_t *d, u8 ep_addr, void *buf, u16 len) {
    return usb_bulk_transfer_timeout(d, ep_addr, buf, len, 3000);
}

int usb_interrupt_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
                           u16 len) {
    if (!d || !d->host || !d->host->ops || !d->host->ops->interrupt)
        return -1;
    usb_lock();
    int rc = d->host->ops->interrupt(d->host, d, ep_addr, buf, len, 50);
    usb_unlock();
    return rc;
}

int usb_isochronous_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
                             u16 len) {
    if (!d || !d->host || !d->host->ops) return -1;
    usb_lock();
    int rc;
    if (ep_addr & 0x80) {
        if (!d->host->ops->iso_in) { usb_unlock(); return -1; }
        rc = d->host->ops->iso_in(d->host, d, ep_addr, buf, len);
    } else {
        if (!d->host->ops->iso_out) { usb_unlock(); return -1; }
        rc = d->host->ops->iso_out(d->host, d, ep_addr, buf, len);
    }
    usb_unlock();
    return rc;
}

/* standard-request wrapper (WP-10c signature, unchanged) */
int usb_control(usb_dev_t *dev, u8 req_type, u8 request,
                u16 value, u16 index, void *buf, u16 len) {
    usb_setup_t s;
    s.bmRequestType = req_type;
    s.bRequest = request;
    s.wValue = value;
    s.wIndex = index;
    s.wLength = len;
    return usb_control_transfer(dev, &s, buf, len);
}

/* stall recovery dispatch (see usb.h) */
extern void usb_uhci_tog_reset(usb_dev_t *d, u8 ep_addr);
extern void usb_ohci_tog_reset(usb_dev_t *d, u8 ep_addr);
extern int  xhci_reset_ep(usb_dev_t *d, u8 ep_addr);
void usb_tog_reset(usb_dev_t *d, u8 ep_addr) {
    if (!d || !d->host || !d->host->ops) return;
    const char *n = d->host->ops->name;
    if (n && n[0] == 'U') usb_uhci_tog_reset(d, ep_addr);
    else if (n && n[0] == 'O') usb_ohci_tog_reset(d, ep_addr);
    else if (n && n[0] == 'X') xhci_reset_ep(d, ep_addr);
    /* EHCI: hardware-managed toggle, nothing to do */
}

int usb_set_interface(usb_dev_t *dev, u16 interface, u16 alt) {
    return usb_control(dev, 0x01, USB_REQ_SET_IFACE, alt, interface,
                       NULL, 0);
}

/* WP-10c legacy entry point: one isochronous OUT packet on the
 * device's first ISO OUT endpoint */
int usb_iso_out_submit(usb_dev_t *dev, const void *data, u16 len) {
    if (!dev) return -1;
    usb_endpoint_t *ep = usb_find_ep(dev, 0xff, USB_EP_ATTR_ISO, 0);
    return usb_isochronous_transfer(dev, ep ? ep->addr : 0,
                                    (void *)data, len);
}

int usb_get_string(usb_dev_t *d, u8 idx, char *out, int out_max) {
    if (!d || !idx || !out || out_max < 2) return -1;
    u8 buf[USB_NAME_MAX];
    oc_memset(buf, 0, sizeof(buf));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC,
                    (USB_DT_STRING << 8) | idx, 0, buf, USB_NAME_MAX) != 0)
        return -1;
    int n = buf[0];
    if (n < 2 || n > (int)sizeof(buf)) return -1;
    int chars = (n - 2) / 2;
    if (chars > out_max - 1) chars = out_max - 1;
    for (int i = 0; i < chars; i++) {
        u16 ch = (u16)(buf[2 + i * 2] | (buf[3 + i * 2] << 8));
        out[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    out[chars] = 0;
    return chars;
}

int usb_get_report_desc(usb_dev_t *d, u8 iface, void *buf, u16 len) {
    return usb_control(d, 0x81, USB_REQ_GET_DESC,
                       (USB_DT_REPORT << 8) | 0, iface, buf, len);
}

/* ==================================================================
 * device allocation / removal
 * ================================================================== */

static usb_dev_t *usb_alloc_slot(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!g_core.devs[i].present) return &g_core.devs[i];
    }
    return NULL;
}

static u8 usb_free_address(void) {
    /* address 0 is the default state and must never be issued */
    if (g_core.next_addr < 1) g_core.next_addr = 1;
    /* simple re-use scan over issued addresses */
    for (int i = 1; i < g_core.next_addr; i++) {
        int taken = 0;
        for (int s = 0; s < USB_MAX_DEVICES; s++)
            if (g_core.devs[s].present && g_core.devs[s].addr == i)
                taken = 1;
        if (!taken) return (u8)i;
    }
    return (u8)g_core.next_addr++;
}

static void usb_kill_slot(usb_dev_t *d) {
    if (!d || !d->present) return;
    /* tear down children first (hubs) */
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_core.devs[i].present && g_core.devs[i].parent == d->slot)
            usb_kill_slot(&g_core.devs[i]);
    }
    if (d->class_drv >= 0 && d->class_drv < g_core.n_drivers) {
        usb_driver_t *dr = &g_core.drivers[d->class_drv];
        if (dr->disconnect) dr->disconnect(d);
    }
    oc_memset(d, 0, sizeof(*d));
    d->parent = -1;
    d->class_drv = -1;
}

/* ==================================================================
 * enumeration (generic, works through any backend)
 * ================================================================== */

static int usb_match_probe(usb_dev_t *d) {
    d->class_drv = -1;
    for (int i = 0; i < g_core.n_drivers; i++) {
        usb_driver_t *dr = &g_core.drivers[i];
        if (dr->class_code == USB_CLASS_VENDOR ||
            usb_dev_by_class(d, dr->class_code)) {
            if (dr->probe(d) == 0) {
                d->class_drv = i;
                return 0;
            }
        }
    }
    return -1;
}

static int usb_enum_one(usb_host_t *h, int parent, u8 hub_port,
                        int depth);

/* enumerate the device on one port.  parent = index of the parent hub
 * device (-1 for a root-hub port), hub_port = port number on it. */
static int usb_enum_one(usb_host_t *h, int parent, u8 hub_port,
                        int depth) {
    if (depth > USB_ENUM_DEPTH) return 0;
    usb_dev_t *d = usb_alloc_slot();
    if (!d) return 0;
    int slot = (int)(d - g_core.devs);
    oc_memset(d, 0, sizeof(*d));
    d->slot = slot;
    d->parent = parent;
    d->hub_port = hub_port;
    d->host = h;
    d->class_drv = -1;
    d->present = 1;   /* slot reserved while we enumerate */

    /* reset the port through the parent hub */
    u8 speed = USB_SPEED_FS;
    if (parent < 0) {
        if (h->ops->port_reset(h, hub_port, &speed) != 0) {
            d->present = 0;
            return 0;
        }
    } else {
        usb_dev_t *hub = &g_core.devs[parent];
        if (usb_hub_port_reset(hub, hub_port) != 0) {
            d->present = 0;
            return 0;
        }
        speed = usb_hub_port_speed(hub, hub_port);
    }
    d->speed = speed;

    /* address 0: read the first 8 device-descriptor bytes to learn
     * bMaxPacketSize0.  (XHCI handles the address-0 stage itself.) */
    u8 mps0 = (speed == USB_SPEED_HS) ? 64 : 8;
    d->addr = 0;
    d->mps0 = mps0;
    u8 buf[18];
    oc_memset(buf, 0, sizeof(buf));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC,
                    (USB_DT_DEVICE << 8) | 0, 0, buf, 8) != 0) {
        usb_log("usb: GET_DESCRIPTOR(8) failed\n");
        d->present = 0;
        return 0;
    }
    if (buf[7] >= 8 && buf[7] <= 64) d->mps0 = buf[7];

    /* SET_ADDRESS */
    u8 new_addr = usb_free_address();
    if (usb_control(d, 0x00, USB_REQ_SET_ADDR, new_addr, 0, NULL, 0)
            != 0) {
        usb_log("usb: SET_ADDRESS failed\n");
        d->present = 0;
        return 0;
    }
    d->addr = new_addr;

    /* full device descriptor */
    oc_memset(buf, 0, sizeof(buf));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC,
                    (USB_DT_DEVICE << 8) | 0, 0, buf, 18) != 0) {
        d->present = 0;
        return 0;
    }
    usb_dev_desc_t dd;
    oc_memcpy(&dd, buf, sizeof(dd));
    d->vid = dd.idVendor;
    d->pid = dd.idProduct;
    d->class = dd.bDeviceClass;
    d->subclass = dd.bDeviceSubClass;
    d->protocol = dd.bDeviceProtocol;

    /* config descriptor: 9-byte header, then the full blob */
    u8 cfg9[9];
    oc_memset(cfg9, 0, sizeof(cfg9));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC,
                    (USB_DT_CONFIG << 8) | 0, 0, cfg9, 9) != 0) {
        d->present = 0;
        return 0;
    }
    u16 total = (u16)(cfg9[2] | (cfg9[3] << 8));
    if (total > USB_RAW_CFG_MAX) total = USB_RAW_CFG_MAX;
    oc_memset(d->cfg_raw, 0, USB_RAW_CFG_MAX);
    if (usb_control(d, 0x80, USB_REQ_GET_DESC,
                    (USB_DT_CONFIG << 8) | 0, 0, d->cfg_raw, total)
            != 0) {
        d->present = 0;
        return 0;
    }
    d->cfg_len = total;
    usb_parse_config(d);

    /* SET_CONFIGURATION(1) */
    if (usb_control(d, 0x00, USB_REQ_SET_CFG, 1, 0, NULL, 0) != 0) {
        usb_log("usb: SET_CONFIGURATION failed\n");
        d->present = 0;
        return 0;
    }

    /* product string (best effort) */
    if (dd.iProduct)
        usb_get_string(d, dd.iProduct, d->product, USB_NAME_MAX);

    /* class-driver probe (the hub driver is built into the core) */
    usb_match_probe(d);
    { char l[80]; char n[12];
      oc_strcpy(l, "usb dbg: enum done slot=");
      oc_u64_to_str(slot, n); oc_strcat(l, n);
      oc_strcat(l, " present="); oc_u64_to_str(d->present, n);
      oc_strcat(l, n);
      oc_console_puts(l); oc_console_puts("\n"); }
    return 1;
}

/* ---- built-in hub support (class 0x09) ----
 * Hub requests: class-specific, recipient=other for port targets. */

static int usb_hub_port_req(usb_dev_t *hub, u8 bmReq, u8 req,
                            u16 feat, u8 port, void *buf, u16 len) {
    /* bmRequestType: host->dev | class | recipient=other(3) */
    u8 rt = (u8)((bmReq & 0x80) | 0x23);
    return usb_control(hub, rt, req, feat, (u16)(port + 1), buf, len);
}

int usb_hub_port_reset(usb_dev_t *hub, u8 port) {
    if (!hub || hub->class != USB_CLASS_HUB) return -1;
    if (usb_hub_port_req(hub, 0x00, USB_REQ_SET_FEAT, 0x04, port,
                         NULL, 0) != 0)
        return -1;
    /* reset recovery ~10-20 ms; poll the reset-change bit */
    for (int i = 0; i < 40; i++) {
        u8 st[4];
        oc_memset(st, 0, sizeof(st));
        if (usb_hub_port_req(hub, 0x80, USB_REQ_GET_STATUS, 0, port,
                             st, 4) == 0) {
            u16 wPortStatus = (u16)(st[0] | (st[1] << 8));
            u16 wPortChange = (u16)(st[2] | (st[3] << 8));
            if (wPortChange & 0x0020) {   /* C_PORT_RESET */
                /* clear the change */
                usb_hub_port_req(hub, 0x01, USB_REQ_CLEAR_FEAT, 0x20,
                                 port, NULL, 0);
                return (wPortStatus & 0x0001) ? 0 : -1;
            }
            if (wPortStatus & 0x0001) return 0;   /* connected already */
        }
        for (volatile int t = 0; t < 20000; t++) { }
    }
    return -1;
}

u8 usb_hub_port_speed(usb_dev_t *hub, u8 port) {
    u8 st[4];
    oc_memset(st, 0, sizeof(st));
    if (usb_hub_port_req(hub, 0x80, USB_REQ_GET_STATUS, 0, port,
                         st, 4) != 0)
        return USB_SPEED_FS;
    u16 wPortStatus = (u16)(st[0] | (st[1] << 8));
    if (wPortStatus & 0x0200) return USB_SPEED_LS;   /* low-speed */
    if (wPortStatus & 0x0400) return USB_SPEED_HS;   /* high-speed */
    return USB_SPEED_FS;
}

/* enumerate any hub: read its descriptor then probe its ports */
static int usb_hub_enumerate(usb_dev_t *hub) {
    u8 hd[16];
    oc_memset(hd, 0, sizeof(hd));
    /* GET_DESCRIPTOR hub (class) */
    if (usb_control(hub, 0xa0, USB_REQ_GET_DESC,
                    (USB_DT_HUB << 8) | 0, 0, hd, 16) != 0) {
        usb_log("usb: hub GET_DESCRIPTOR failed\n");
        return -1;
    }
    u8 nports = hd[2];
    if (nports > 8) nports = 8;
    /* remember the port count for the hot-unplug scan in usb_poll() */
    hub->class_priv = (void *)(uintptr_t)nports;
    /* power the ports */
    for (u8 p = 0; p < nports; p++)
        usb_hub_port_req(hub, 0x00, USB_REQ_SET_FEAT, 0x08, p,
                         NULL, 0);
    for (volatile int t = 0; t < 100000; t++) { }

    int found = 0;
    for (u8 p = 0; p < nports; p++) {
        u8 st[4];
        oc_memset(st, 0, sizeof(st));
        if (usb_hub_port_req(hub, 0x80, USB_REQ_GET_STATUS, 0, p,
                             st, 4) != 0)
            continue;
        u16 wPortStatus = (u16)(st[0] | (st[1] << 8));
        if (!(wPortStatus & 0x0001)) continue;    /* nothing connected */
        if (usb_enum_one(hub->host, hub->slot, p, USB_ENUM_DEPTH) > 0) {
            found++;
            usb_log("usb: hub port device enumerated\n");
        }
    }
    return found;
}

/* ==================================================================
 * per-host enumeration + hot-plug
 * ================================================================== */

int usb_enumerate_host(usb_host_t *h) {
    if (!h || !h->up || !h->ops->port_count || !h->ops->port_status)
        return -1;
    int found = 0;
    int nports = h->ops->port_count(h);
    for (int p = 0; p < nports; p++) {
        usb_port_status_t st;
        oc_memset(&st, 0, sizeof(st));
        if (h->ops->port_status(h, p, &st) != 0) continue;
        if (!st.connected) continue;
        /* skip already-occupied root ports */
        int known = 0;
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            if (g_core.devs[i].present && g_core.devs[i].host == h &&
                g_core.devs[i].parent == -1 &&
                g_core.devs[i].hub_port == (u8)p)
                known = 1;
        }
        if (known) continue;
        found += usb_enum_one(h, -1, (u8)p, 0);
    }
    /* after the root level, walk any hubs that just appeared */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            usb_dev_t *d = &g_core.devs[i];
            if (d->present && d->host == h &&
                d->class == USB_CLASS_HUB && d->class_priv == NULL) {
                d->class_priv = (void *)(uintptr_t)1;
                found += usb_hub_enumerate(d);
            }
        }
    }
    return found;
}

int usb_enumerate(void) {
    int found = 0;
    for (int i = 0; i < g_core.n_hosts; i++)
        found += usb_enumerate_host(&g_core.hosts[i]);
    return found;
}

void usb_poll(void) {
    if (!g_core.up) return;
    usb_lock();
    for (int i = 0; i < g_core.n_hosts; i++) {
        usb_host_t *h = &g_core.hosts[i];
        if (h->ops->poll) h->ops->poll(h);
    }
    usb_unlock();
    for (int i = 0; i < g_core.n_hosts; i++) {
        usb_host_t *h = &g_core.hosts[i];
        if (!h->ops->port_count || !h->ops->port_status) continue;
        int nports = h->ops->port_count(h);
        for (int p = 0; p < nports; p++) {
            usb_port_status_t st;
            oc_memset(&st, 0, sizeof(st));
            if (h->ops->port_status(h, p, &st) != 0) continue;
            /* find the device on this root port */
            int slot = -1;
            for (int s = 0; s < USB_MAX_DEVICES; s++) {
                usb_dev_t *d = &g_core.devs[s];
                if (d->present && d->host == h && d->parent == -1 &&
                    d->hub_port == (u8)p) {
                    slot = s;
                    break;
                }
            }
            if (st.connected && slot < 0 && st.changed) {
                usb_enum_one(h, -1, (u8)p, 0);
                usb_enumerate_host(h);
            } else if (!st.connected && slot >= 0) {
                usb_log("usb: device disconnected (root port)\n");
                usb_kill_slot(&g_core.devs[slot]);
            }
        }
    }
    /* hub hot-unplug: scan the ports of every enumerated hub */
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_dev_t *hub = &g_core.devs[i];
        if (!hub->present || hub->class != USB_CLASS_HUB)
            continue;
        u8 nports = (u8)(uintptr_t)hub->class_priv;
        if (!nports) continue;
        for (u8 p = 0; p < nports; p++) {
            u8 st[4];
            oc_memset(st, 0, sizeof(st));
            if (usb_hub_port_req(hub, 0x80, USB_REQ_GET_STATUS, 0,
                                 p, st, 4) != 0)
                continue;
            u16 wPortStatus = (u16)(st[0] | (st[1] << 8));
            int connected = (wPortStatus & 0x0001) ? 1 : 0;
            int slot = -1;
            for (int s2 = 0; s2 < USB_MAX_DEVICES; s2++) {
                usb_dev_t *d = &g_core.devs[s2];
                if (d->present && d->parent == hub->slot &&
                    d->hub_port == p) {
                    slot = s2;
                    break;
                }
            }
            if (!connected && slot >= 0) {
                usb_log("usb: device disconnected (hub port)\n");
                usb_kill_slot(&g_core.devs[slot]);
            }
        }
    }
}

/* ==================================================================
 * UHCI backend
 * ================================================================== */

/* UHCI registers */
#define UHCI_USBCMD       0x00
#define UHCI_USBSTS       0x02
#define UHCI_USBINTR      0x04
#define UHCI_USBFRNUM     0x06
#define UHCI_USBFLBASEADD 0x08
#define UHCI_USBSOF       0x0c
#define UHCI_USBPORTSC1   0x10

#define UHCI_CMD_RS       (1u << 0)
#define UHCI_CMD_HCRESET  (1u << 1)
#define UHCI_CMD_GRESET   (1u << 2)
#define UHCI_CMD_EGSM     (1u << 3)
#define UHCI_CMD_FGR      (1u << 4)

#define UHCI_STS_USBINT   (1u << 0)
#define UHCI_STS_HCHALTED (1u << 5)

/* UHCI port status */
#define UHCI_PORT_CCS     (1u << 0)
#define UHCI_PORT_CSC     (1u << 1)
#define UHCI_PORT_EN      (1u << 2)
#define UHCI_PORT_ENC     (1u << 3)
#define UHCI_PORT_LSDA    (1u << 8)
#define UHCI_PORT_RESET   (1u << 9)

/* TD bits */
#define TD_CTRL_SPD       (1u << 29)
#define TD_CTRL_IOS       (1u << 25)
#define TD_CTRL_IOC       (1u << 24)
#define TD_CTRL_ACTIVE    (1u << 23)
#define TD_CTRL_STALL     (1u << 22)
#define TD_CTRL_BUFERR    (1u << 21)
#define TD_CTRL_BABBLE    (1u << 20)
#define TD_CTRL_NAK       (1u << 19)
#define TD_CTRL_TIMEOUT   (1u << 18)
#define TD_CTRL_BITSTUFF  (1u << 17)
#define TD_CTRL_ERR_MSK   (3u << 27)
#define TD_CTRL_TRERR     (TD_CTRL_STALL | TD_CTRL_BUFERR | \
                           TD_CTRL_BABBLE | TD_CTRL_TIMEOUT | \
                           TD_CTRL_BITSTUFF)

/* link bits */
#define LINK_TERMINATE    1u
#define LINK_IS_QH        2u
#define LINK_DEPTH        4u

/* PIDs */
#define USB_PID_SETUP     0x2d
#define USB_PID_IN        0x69
#define USB_PID_OUT       0xe1

typedef struct uhci_td {
    volatile u32 link;
    volatile u32 ctrl;
    volatile u32 token;
    volatile u32 buffer;
} uhci_td_t;

typedef struct uhci_qh {
    volatile u32 link;
    volatile u32 el_link;
} uhci_qh_t;

#define USB_CTRL_TD_COUNT  16
#define UHCI_BULK_TD_COUNT  8
#define UHCI_INT_TD_COUNT   2
#define UHCI_N_PORTS        2

/* schedule page layout (one PMM page):
 *   0x000 ISO TD          (32 B)
 *   0x040 int QH          (16 B)
 *   0x060 ctrl QH         (16 B)
 *   0x080 ctrl TDs[16]    (512 B)
 *   0x280 bulk QH         (16 B)
 *   0x2a0 bulk TDs[8]     (256 B)
 *   0x3a0 int TDs[2]      ( 64 B)
 *   0x3e0 setup scratch   (  8 B)
 * separate data pages: bulk bufs[8] (1023 B each -> 8 pages),
 *                      int bufs[2]                  (2 pages) */
#define UHCI_OFF_INT_QH   0x040u
#define UHCI_OFF_CTRL_QH  0x060u
#define UHCI_OFF_CTRL_TDS 0x080u
#define UHCI_OFF_BULK_QH  0x280u
#define UHCI_OFF_BULK_TDS 0x2a0u
#define UHCI_OFF_INT_TDS  0x3a0u
#define UHCI_OFF_SCR      0x3e0u

typedef struct usb_state {
    u16  io;
    u8   bus, dev, func;
    u64  frame_list_phys;
    volatile u32 *frame_list;

    uhci_td_t  *iso_td;
    uhci_qh_t  *int_qh;
    uhci_qh_t  *ctrl_qh;
    uhci_qh_t  *bulk_qh;
    uhci_td_t  *ctrl_tds;
    uhci_td_t  *bulk_tds;
    uhci_td_t  *int_tds;
    u8         *setup_scr;

    u64  sched_phys;
    u64  bulk_buf_phys[UHCI_BULK_TD_COUNT];
    u8  *bulk_buf[UHCI_BULK_TD_COUNT];
    u64  int_buf_phys[UHCI_INT_TD_COUNT];
    u8  *int_buf[UHCI_INT_TD_COUNT];

    u64  iso_buf_phys;
    u8  *iso_buf;

    int  irq;
    u64  irq_count;
    int  up;
    /* data toggle per (address, endpoint, direction): one byte per
     * (addr,ep); bit0 = OUT toggle, bit1 = IN toggle.  Kept in
     * software because UHCI TDs carry the toggle explicitly. */
    u8   toggles[USB_MAX_DEVICES * 16];
} usb_state_t;

static usb_state_t g_uhci;
static usb_host_t  g_uhci_host;
static usb_hc_ops_t g_uhci_ops;

static inline void uhci_outw(u16 port, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port)); }
static inline void uhci_outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline u16  uhci_inw(u16 port)  { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u32  uhci_inl(u16 port)  { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static u16 uhci_portsc(usb_state_t *u, int port) {
    return uhci_inw(u->io + UHCI_USBPORTSC1 + port * 2);
}

static void uhci_portsc_write(usb_state_t *u, int port, u16 v) {
    uhci_outw(u->io + UHCI_USBPORTSC1 + port * 2, v);
}

/* ---- IRQ ---- */
static void usb_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    usb_state_t *u = &g_uhci;
    if (!u->up) return;
    u16 sts = uhci_inw(u->io + UHCI_USBSTS);
    if (sts & (UHCI_STS_USBINT | (1u << 1))) {
        uhci_outw(u->io + UHCI_USBSTS, sts);
        u->irq_count++;
    }
}

/* ---- TD helpers ---- */

static void usb_td_fill(uhci_td_t *td, u32 next_link, u8 pid, u8 addr,
                        u8 ep, u16 maxlen, u64 buf, u8 toggle) {
    u32 token = pid
        | ((u32)(addr & 0x7f) << 8)
        | ((u32)(ep & 0xf) << 15)
        | ((u32)(toggle & 1) << 19)
        | (((u32)(maxlen - 1) & 0x7ff) << 21);
    td->link = next_link;
    td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_IOC | (3u << 27);
    td->token = token;
    td->buffer = (u32)buf;
}

/* wait for a run of TDs to go inactive or error out */
static int uhci_tds_wait(uhci_td_t *tds, int first, int n,
                         u32 timeout_ms) {
    u64 deadline = oc_timer_now_ms() + timeout_ms;
    for (;;) {
        int busy = 0;
        for (int i = first; i < first + n; i++) {
            u32 ctrl = tds[i].ctrl;
            if (ctrl & TD_CTRL_ACTIVE) busy = 1;
            else if (ctrl & (TD_CTRL_STALL | TD_CTRL_BABBLE)) {
                return OC_USB_ESTALL;
            } else if (ctrl & TD_CTRL_TIMEOUT) {
                return OC_USB_ETIMEDOUT;
            }
        }
        if (!busy) return 0;
        if (oc_timer_now_ms() > deadline) {
            /* retire the TDs by briefly stopping the schedule */
            u16 cmd = uhci_inw(g_uhci.io + UHCI_USBCMD);
            uhci_outw(g_uhci.io + UHCI_USBCMD, cmd & ~UHCI_CMD_RS);
            for (int i = first; i < first + n; i++)
                tds[i].ctrl &= ~TD_CTRL_ACTIVE;
            uhci_outw(g_uhci.io + UHCI_USBCMD, cmd);
            return OC_USB_ETIMEDOUT;
        }
        sched_yield();
    }
}

/* ---- control transfer (UHCI) ---- */

static int uhci_control(usb_host_t *h, usb_dev_t *d,
                        const usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || !d) return -1;
    if (len > 14 * 64) return -1;

    u8 addr = d->addr;
    u8 data_pid = (setup->bmRequestType & 0x80) ? USB_PID_IN
                                                : USB_PID_OUT;
    u8 status_pid = (setup->bmRequestType & 0x80) ? USB_PID_OUT
                                                  : USB_PID_IN;
    u16 mps = d->mps0 ? d->mps0 : 8;
    uhci_td_t *tds = u->ctrl_tds;

    oc_memcpy(u->setup_scr, setup, 8);

    /* SETUP: DATA0, 8 bytes */
    int next = 1;
    usb_td_fill(&tds[0],
                (u32)(uintptr_t)&tds[next] | LINK_DEPTH,
                USB_PID_SETUP, addr, 0, 8,
                (u64)(uintptr_t)u->setup_scr, 0);

    int ntds;
    if (len == 0) {
        usb_td_fill(&tds[next], LINK_TERMINATE, status_pid, addr, 0,
                    mps, 0, 1);
        ntds = 2;
    } else {
        u16 remaining = len;
        u64 bufp = (u64)(uintptr_t)buf;
        u8 toggle = 1;
        while (remaining > 0 && next < USB_CTRL_TD_COUNT - 1) {
            u16 chunk = remaining > mps ? mps : remaining;
            int is_last_data = (remaining <= mps);
            int nnext = is_last_data ? (USB_CTRL_TD_COUNT - 1)
                                     : (next + 1);
            usb_td_fill(&tds[next],
                        (u32)(uintptr_t)&tds[nnext] | LINK_DEPTH,
                        data_pid, addr, 0, chunk, bufp, toggle);
            bufp += chunk;
            remaining -= chunk;
            toggle ^= 1;
            next++;
        }
        if (remaining > 0) return -1;
        usb_td_fill(&tds[USB_CTRL_TD_COUNT - 1], LINK_TERMINATE,
                    status_pid, addr, 0, mps, 0, 1);
        ntds = USB_CTRL_TD_COUNT;
    }

    u->ctrl_qh->el_link = (u32)(uintptr_t)&tds[0];
    int rc = uhci_tds_wait(tds, 0, ntds, timeout_ms);
    u->ctrl_qh->el_link = LINK_TERMINATE;
    return rc;
}

/* ---- bulk transfer (UHCI) ---- */

static u8 uhci_tog_get(usb_state_t *u, usb_dev_t *d, u8 ep_addr) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    return (u->toggles[idx] >> ((ep_addr & 0x80) ? 1 : 0)) & 1;
}

static void uhci_tog_set(usb_state_t *u, usb_dev_t *d, u8 ep_addr,
                         u8 v) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    u8 bit = (u8)(1u << ((ep_addr & 0x80) ? 1 : 0));
    if (v) u->toggles[idx] |= bit;
    else   u->toggles[idx] &= (u8)~bit;
}

static void uhci_tog_flip(usb_state_t *u, usb_dev_t *d, u8 ep_addr) {
    uhci_tog_set(u, d, ep_addr, (u8)(1 - uhci_tog_get(u, d, ep_addr)));
}

/* after a STALL the endpoint toggle restarts at DATA0 (the MSC driver
 * issues CLEAR_FEATURE(ENDPOINT_HALT) and calls this) */
void usb_uhci_tog_reset(usb_dev_t *d, u8 ep_addr) {
    uhci_tog_set(&g_uhci, d, ep_addr, 0);
}

static int uhci_bulk(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || !d || !buf) return -1;
    if (len == 0) return 0;
    if (d->speed == USB_SPEED_LS) return OC_USB_EINVAL; /* no bulk LS */
    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? USB_PID_IN : USB_PID_OUT;

    /* max packet size from the endpoint descriptor (default 64) */
    u16 mps = 64;
    usb_endpoint_t *epd = usb_find_ep(d, 0xff, USB_EP_ATTR_BULK,
                                      (ep_addr & 0x80) ? 1 : 0);
    if (epd && USB_EP_NUM(epd->addr) == ep) mps = epd->maxpack;
    if (mps == 0 || mps > 64) mps = 64;   /* FS bulk cap */

    int ntds = (int)((len + mps - 1) / mps);
    if (ntds > UHCI_BULK_TD_COUNT) return OC_USB_EINVAL;

    /* fill the TD chain (bounce buffers, so caller memory need not be
     * DMA-able).  The data toggle ALTERNATES per TD (DATA0/DATA1/...)
     * starting at the endpoint's current toggle. */
    u8 base_tog = uhci_tog_get(u, d, ep_addr);
    u16 chunk_total = 0;
    for (int i = 0; i < ntds; i++) {
        u16 chunk = (u16)(len - chunk_total);
        if (chunk > mps) chunk = mps;
        if (pid == USB_PID_OUT)
            oc_memcpy(u->bulk_buf[i],
                      (const void *)((uintptr_t)buf + chunk_total),
                      chunk);
        usb_td_fill(&u->bulk_tds[i],
                    (i + 1 < ntds)
                        ? (u32)(uintptr_t)&u->bulk_tds[i + 1] |
                          LINK_DEPTH
                        : LINK_TERMINATE,
                    pid, addr, ep, chunk, u->bulk_buf_phys[i],
                    (u8)(base_tog ^ (i & 1)));
        chunk_total += chunk;
    }

    u->bulk_qh->el_link = (u32)(uintptr_t)&u->bulk_tds[0];
    int rc = uhci_tds_wait(u->bulk_tds, 0, ntds, timeout_ms);
    u->bulk_qh->el_link = LINK_TERMINATE;

    /* retire: a TD that left ACTIVE without a transaction-error bit
     * was really transferred - the device consumed its packet and
     * advanced ITS toggle.  Track exactly those so our software toggle
     * stays in sync; a failed/never-attempted TD advances nothing. */
    int moved = 0;
    int walked = 0;
    for (int i = 0; i < ntds; i++) {
        u32 ctrl = u->bulk_tds[i].ctrl;
        if (ctrl & TD_CTRL_ACTIVE) break;       /* never attempted */
        if (ctrl & TD_CTRL_TRERR) break;        /* failed transaction */
        walked++;
        u16 chunk = (u16)(len - moved);
        if (chunk > mps) chunk = mps;
        u16 actual = (u16)((ctrl & 0x7ff) + 1);
        if (actual > chunk) actual = chunk;
        if (pid == USB_PID_IN)
            oc_memcpy((void *)((uintptr_t)buf + moved),
                      u->bulk_buf[i], actual);
        moved += actual;
        if (actual < mps) break;   /* short packet ends the transfer */
    }
    uhci_tog_set(u, d, ep_addr, (u8)(base_tog ^ (walked & 1)));
    if (rc != 0) return rc;
    return moved;
}

/* ---- interrupt transfer (UHCI) ---- */

static int uhci_interrupt(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || !d || !buf) return -1;
    if (len == 0 || len > 64) return OC_USB_EINVAL;
    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? USB_PID_IN : USB_PID_OUT;

    if (pid == USB_PID_OUT)
        oc_memcpy(u->int_buf[0], buf, len);
    usb_td_fill(&u->int_tds[0], LINK_TERMINATE, pid, addr, ep, len,
                u->int_buf_phys[0],
                uhci_tog_get(u, d, ep_addr));
    u->int_qh->el_link = (u32)(uintptr_t)&u->int_tds[0];

    u64 deadline = oc_timer_now_ms() + timeout_ms;
    int rc = 0;
    for (;;) {
        u32 ctrl = u->int_tds[0].ctrl;
        if (!(ctrl & TD_CTRL_ACTIVE)) {
            if (ctrl & (TD_CTRL_STALL | TD_CTRL_BABBLE)) {
                rc = OC_USB_ESTALL;
            } else {
                rc = 0;
            }
            break;
        }
        if (oc_timer_now_ms() > deadline) {
            /* retire */
            u->int_qh->el_link = LINK_TERMINATE;
            u->int_tds[0].ctrl &= ~TD_CTRL_ACTIVE;
            rc = OC_USB_ENAK;
            break;
        }
        sched_yield();
    }
    u->int_qh->el_link = LINK_TERMINATE;
    if (rc == 0) {
        uhci_tog_flip(u, d, ep_addr);
        if (pid == USB_PID_IN)
            oc_memcpy(buf, u->int_buf[0], len);
        return (int)len;
    }
    return rc;
}

/* ---- ISO OUT (UHCI) — WP-10c audio path, byte-compatible ---- */

static int uhci_iso_out(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || !d || !d->present) return -1;
    if (len > 1023) return -1;

    if (data && len) oc_memcpy(u->iso_buf, data, len);

    u32 token = USB_PID_OUT
        | ((u32)(d->addr & 0x7f) << 8)
        | ((u32)(USB_EP_NUM(ep_addr) & 0xf) << 15)
        | (((u32)(len - 1) & 0x7ff) << 21)
        | ((u32)(len & 0x7ff));
    u->iso_td->token = token;
    u->iso_td->buffer = (u32)u->iso_buf_phys;
    u->iso_td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_IOS | (3u << 27);
    return 0;
}

static int uhci_iso_in(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;   /* not needed by any current class driver;
                               UHCI ISO IN needs per-frame reaping */
}

/* ---- root hub (UHCI) ---- */

static int uhci_port_count(usb_host_t *h) {
    (void)h;
    return UHCI_N_PORTS;
}

static int uhci_port_status(usb_host_t *h, int port,
                            usb_port_status_t *out) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || port < 0 || port >= UHCI_N_PORTS) return -1;
    u16 psc = uhci_portsc(u, port);
    out->connected = (psc & UHCI_PORT_CCS) ? 1 : 0;
    out->enabled = (psc & UHCI_PORT_EN) ? 1 : 0;
    out->speed = (psc & UHCI_PORT_LSDA) ? USB_SPEED_LS : USB_SPEED_FS;
    out->changed = (psc & UHCI_PORT_CSC) ? 1 : 0;
    if (psc & UHCI_PORT_CSC) {
        /* write-1-to-clear the change bits (keep the rest) */
        uhci_portsc_write(u, port, (u16)(psc | UHCI_PORT_CSC));
    }
    return 0;
}

static int uhci_port_reset(usb_host_t *h, int port, u8 *speed_out) {
    (void)h;
    usb_state_t *u = &g_uhci;
    if (!u->up || port < 0 || port >= UHCI_N_PORTS) return -1;
    u16 psc = uhci_portsc(u, port);
    uhci_portsc_write(u, port, (u16)(psc | UHCI_PORT_RESET));
    for (volatile int i = 0; i < 100000; i++) { }
    uhci_portsc_write(u, port, (u16)(psc & ~UHCI_PORT_RESET));
    for (volatile int i = 0; i < 100000; i++) { }
    psc = uhci_portsc(u, port);
    if (!(psc & UHCI_PORT_CCS)) return -1;
    uhci_portsc_write(u, port, (u16)(psc | UHCI_PORT_EN));
    if (speed_out) {
        *speed_out = (psc & UHCI_PORT_LSDA) ? USB_SPEED_LS
                                            : USB_SPEED_FS;
    }
    return 0;
}

static int uhci_poll(usb_host_t *h) {
    (void)h;
    /* completions are detected by polling the TD status words inside
     * the blocking transfer waits; nothing extra to do here yet */
    return 0;
}

/* ---- UHCI init (WP-10c-compatible legacy entry point) ---- */

static void usb_build_schedule(usb_state_t *u) {
    u64 sched = u->sched_phys;
    u->iso_td   = (uhci_td_t *)(uintptr_t)sched;
    u->int_qh   = (uhci_qh_t *)(uintptr_t)(sched + UHCI_OFF_INT_QH);
    u->ctrl_qh  = (uhci_qh_t *)(uintptr_t)(sched + UHCI_OFF_CTRL_QH);
    u->ctrl_tds = (uhci_td_t *)(uintptr_t)(sched + UHCI_OFF_CTRL_TDS);
    u->bulk_qh  = (uhci_qh_t *)(uintptr_t)(sched + UHCI_OFF_BULK_QH);
    u->bulk_tds = (uhci_td_t *)(uintptr_t)(sched + UHCI_OFF_BULK_TDS);
    u->int_tds  = (uhci_td_t *)(uintptr_t)(sched + UHCI_OFF_INT_TDS);
    u->setup_scr = (u8 *)(uintptr_t)(sched + UHCI_OFF_SCR);
    oc_memset((void *)(uintptr_t)sched, 0, PMM_PAGE_SIZE);

    /* chain: ISO TD -> int QH -> ctrl QH -> bulk QH -> term */
    u->iso_td->link = (u32)(sched + UHCI_OFF_INT_QH) | LINK_IS_QH;
    u->iso_td->ctrl = TD_CTRL_IOS;

    u->int_qh->link = (u32)(sched + UHCI_OFF_CTRL_QH) | LINK_IS_QH;
    u->int_qh->el_link = LINK_TERMINATE;
    u->ctrl_qh->link = (u32)(sched + UHCI_OFF_BULK_QH) | LINK_IS_QH;
    u->ctrl_qh->el_link = LINK_TERMINATE;
    u->bulk_qh->link = LINK_TERMINATE;
    u->bulk_qh->el_link = LINK_TERMINATE;

    for (int i = 0; i < 1024; i++) {
        u->frame_list[i] = (u32)sched;
    }
}

static int uhci_probe_one(u8 bus, u8 dev, u8 func) {
    usb_state_t *u = &g_uhci;
    if (u->up) return 0;

    /* fill the backend ops table once */
    if (!g_uhci_ops.name) {
        g_uhci_ops.name = "UHCI";
        g_uhci_ops.bulk_max = 512;   /* 8 TDs x 64 B FS bulk cap */
        g_uhci_ops.control = uhci_control;
        g_uhci_ops.bulk = uhci_bulk;
        g_uhci_ops.interrupt = uhci_interrupt;
        g_uhci_ops.iso_out = uhci_iso_out;
        g_uhci_ops.iso_in = uhci_iso_in;
        g_uhci_ops.port_count = uhci_port_count;
        g_uhci_ops.port_status = uhci_port_status;
        g_uhci_ops.port_reset = uhci_port_reset;
        g_uhci_ops.poll = uhci_poll;
    }

    u32 bar4 = pci_read_config(bus, dev, func, 0x20);
    if (!(bar4 & 1)) return -1;
    u->io = (u16)(bar4 & 0xFFF0);
    u->bus = bus; u->dev = dev; u->func = func;
    pci_enable_device(bus, dev, func);

    u64 fl = pmm_alloc_frame();
    if (!fl) return -1;
    u->frame_list_phys = fl;
    u->frame_list = (volatile u32 *)(uintptr_t)fl;

    u64 sched = pmm_alloc_frame();
    u64 iso = pmm_alloc_frame();
    if (!sched || !iso) return -1;
    u->sched_phys = sched;
    u->iso_buf_phys = iso;
    u->iso_buf = (u8 *)(uintptr_t)iso;
    oc_memset(u->iso_buf, 0, PMM_PAGE_SIZE);

    /* bulk/interrupt bounce buffers (one page each) */
    for (int i = 0; i < UHCI_BULK_TD_COUNT; i++) {
        u64 p = pmm_alloc_frame();
        if (!p) return -1;
        u->bulk_buf_phys[i] = p;
        u->bulk_buf[i] = (u8 *)(uintptr_t)p;
        oc_memset(u->bulk_buf[i], 0, PMM_PAGE_SIZE);
    }
    for (int i = 0; i < UHCI_INT_TD_COUNT; i++) {
        u64 p = pmm_alloc_frame();
        if (!p) return -1;
        u->int_buf_phys[i] = p;
        u->int_buf[i] = (u8 *)(uintptr_t)p;
        oc_memset(u->int_buf[i], 0, PMM_PAGE_SIZE);
    }

    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    u->irq = -1;
    if (irq < 16 && oc_irq_register_handler(irq, usb_irq_handler,
                                            NULL) == 0)
        u->irq = irq;

    uhci_outw(u->io + UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (int t = 0; t < 100000; t++) {
        if (!(uhci_inw(u->io + UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
    }

    usb_build_schedule(u);

    uhci_outl(u->io + UHCI_USBFLBASEADD, (u32)fl);
    uhci_outw(u->io + UHCI_USBFRNUM, 0);
    uhci_outw(u->io + UHCI_USBINTR, 0);          /* polling mode */
    uhci_outw(u->io + UHCI_USBCMD, UHCI_CMD_RS); /* run */

    u->up = 1;

    /* register with the core */
    usb_host_t *h = &g_uhci_host;
    oc_memset(h, 0, sizeof(*h));
    oc_strcpy(h->name, "uhci0");
    h->bus = bus; h->dev = dev; h->func = func;
    h->priv = u;
    h->ops = &g_uhci_ops;
    usb_register_host(h, &g_uhci_ops);
    return 0;
}

int usb_init(void) {
    u8 bus = 0, dev = 0, func = 0;
    int found = pci_find_device(0x8086, 0x7020, &bus, &dev,
                                &func) == 0;
    if (!found) found = pci_find_device(0x8086, 0x7112, &bus, &dev,
                                        &func) == 0;
    if (!found) return -1;
    return uhci_probe_one(bus, dev, func);
}

int uhci_init(const pci_dev_t *dev) {
    if (!dev) return -1;
    return uhci_probe_one(dev->bus, dev->dev, dev->func);
}

u16 usb_uhci_frnum(void) {
    usb_state_t *u = &g_uhci;
    if (!u->up) return 0;
    return uhci_inw(u->io + UHCI_USBFRNUM);
}

/* ==================================================================
 * probe-all: every supported controller type
 * ================================================================== */

int usb_probe_all(void) {
    int hosts = 0;
    /* UHCI (Intel PIIX3/4) */
    {
        u8 b, dv, fn;
        if (pci_find_device(0x8086, 0x7020, &b, &dv, &fn) == 0 ||
            pci_find_device(0x8086, 0x7112, &b, &dv, &fn) == 0) {
            if (uhci_probe_one(b, dv, fn) == 0) hosts++;
        }
    }
    /* OHCI / EHCI / XHCI (class 0x0c03xx backends self-identify) */
    {
        extern int ohci_probe_all(void);
        extern int ehci_probe_all(void);
        extern int xhci_probe_all(void);
        hosts += ohci_probe_all();
        hosts += ehci_probe_all();
        hosts += xhci_probe_all();
    }
    /* enumerate every root hub so the class drivers see the devices */
    if (hosts > 0) usb_enumerate();
    return hosts;
}

/* ==================================================================
 * status printing (WP-10c usb_print_state kept, plus the tree)
 * ================================================================== */

void usb_print_state(void) {
    usb_state_t *u = &g_uhci;
    char line[128];
    char n[24];

    if (!g_core.up) {
        oc_console_puts("USB: not present\n");
        return;
    }
    for (int i = 0; i < g_core.n_hosts; i++) {
        usb_host_t *h = &g_core.hosts[i];
        oc_strcpy(line, "USB host ");
        oc_strcat(line, h->name);
        oc_strcat(line, " (");
        oc_strcat(line, h->ops->name);
        oc_strcat(line, ") pci=");
        usb_num_dec(h->bus, n); oc_strcat(line, n);
        oc_strcat(line, ":");
        usb_num_dec(h->dev, n); oc_strcat(line, n);
        oc_strcat(line, ":");
        usb_num_dec(h->func, n); oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    (void)u;
    usb_print_tree();
}

void usb_print_tree(void) {
    char line[160];
    char n[24];
    int any = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_dev_t *d = &g_core.devs[i];
        if (!d->present) continue;
        any = 1;
        line[0] = 0;
        oc_strcat(line, "  dev");
        usb_num_dec((u64)d->addr, n); oc_strcat(line, n);
        oc_strcat(line, " [slot ");
        usb_num_dec((u64)d->slot, n); oc_strcat(line, n);
        oc_strcat(line, "] ");
        if (d->parent < 0) {
            oc_strcat(line, "root");
        } else {
            oc_strcat(line, "hub dev");
            usb_num_dec((u64)g_core.devs[d->parent].addr, n);
            oc_strcat(line, n);
            oc_strcat(line, " port");
            usb_num_dec(d->hub_port, n); oc_strcat(line, n);
        }
        oc_strcat(line, " vid=0x");
        usb_hex(d->vid, n, 4); oc_strcat(line, n);
        oc_strcat(line, " pid=0x");
        usb_hex(d->pid, n, 4); oc_strcat(line, n);
        oc_strcat(line, " cls=");
        usb_hex(d->class, n, 2); oc_strcat(line, n);
        oc_strcat(line, "/");
        usb_hex(d->subclass, n, 2); oc_strcat(line, n);
        oc_strcat(line, "/");
        usb_hex(d->protocol, n, 2); oc_strcat(line, n);
        oc_strcat(line, " host=");
        oc_strcat(line, d->host ? d->host->name : "?");
        if (d->product[0]) {
            oc_strcat(line, " \"");
            oc_strcat(line, d->product);
            oc_strcat(line, "\"");
        }
        oc_console_puts(line);
        oc_console_puts("\n");
        for (int e = 0; e < d->n_ep; e++) {
            usb_endpoint_t *ep = &d->eps[e];
            line[0] = 0;
            oc_strcat(line, "    ep");
            usb_num_dec(USB_EP_NUM(ep->addr), n);
            oc_strcat(line, n);
            oc_strcat(line, (ep->addr & 0x80) ? " in " : " out ");
            const char *t = "?";
            if (ep->attr == USB_EP_ATTR_CONTROL) t = "ctrl";
            else if (ep->attr == USB_EP_ATTR_ISO) t = "iso";
            else if (ep->attr == USB_EP_ATTR_BULK) t = "bulk";
            else if (ep->attr == USB_EP_ATTR_INTERRUPT) t = "int";
            oc_strcat(line, t);
            oc_strcat(line, " max=");
            usb_num_dec(ep->maxpack, n); oc_strcat(line, n);
            oc_console_puts(line);
            oc_console_puts("\n");
        }
    }
    if (!any) oc_console_puts("  (no devices)\n");
}

void usb_print_device(int slot) {
    if (slot < 0 || slot >= USB_MAX_DEVICES) return;
    usb_dev_t *d = &g_core.devs[slot];
    if (!d->present) {
        oc_console_puts("usbdev: empty slot\n");
        return;
    }
    char line[160];
    char n[24];
    line[0] = 0;
    oc_strcat(line, "dev ");
    usb_num_dec((u64)d->addr, n); oc_strcat(line, n);
    oc_strcat(line, " [slot ");
    usb_num_dec((u64)d->slot, n); oc_strcat(line, n);
    oc_strcat(line, "]\n");
    oc_console_puts(line);

    line[0] = 0;
    oc_strcat(line, "  vid=0x");
    usb_hex(d->vid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " pid=0x");
    usb_hex(d->pid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " speed=");
    const char *sp = "FS";
    if (d->speed == USB_SPEED_LS) sp = "LS";
    else if (d->speed == USB_SPEED_HS) sp = "HS";
    else if (d->speed == USB_SPEED_SS) sp = "SS";
    oc_strcat(line, sp);
    oc_strcat(line, " host=");
    oc_strcat(line, d->host ? d->host->name : "?");
    oc_console_puts(line);
    oc_console_puts("\n");

    line[0] = 0;
    oc_strcat(line, "  class=");
    usb_hex(d->class, n, 2); oc_strcat(line, n);
    oc_strcat(line, "/"); usb_hex(d->subclass, n, 2);
    oc_strcat(line, n);
    oc_strcat(line, "/"); usb_hex(d->protocol, n, 2);
    oc_strcat(line, n);
    oc_strcat(line, " mps0=");
    usb_num_dec(d->mps0, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");

    if (d->product[0]) {
        line[0] = 0;
        oc_strcat(line, "  product=\"");
        oc_strcat(line, d->product);
        oc_strcat(line, "\"\n");
        oc_console_puts(line);
    }
    line[0] = 0;
    oc_strcat(line, "  interfaces=");
    usb_num_dec(d->n_if, n); oc_strcat(line, n);
    oc_strcat(line, " endpoints=");
    usb_num_dec(d->n_ep, n); oc_strcat(line, n);
    oc_strcat(line, " cfg_len=");
    usb_num_dec(d->cfg_len, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");
    for (int i = 0; i < d->n_if; i++) {
        usb_interface_t *ifp = &d->ifs[i];
        line[0] = 0;
        oc_strcat(line, "    if");
        usb_num_dec(ifp->number, n); oc_strcat(line, n);
        oc_strcat(line, " alt="); usb_num_dec(ifp->alt, n);
        oc_strcat(line, n);
        oc_strcat(line, " cls="); usb_hex(ifp->class, n, 2);
        oc_strcat(line, n);
        oc_strcat(line, "/"); usb_hex(ifp->subclass, n, 2);
        oc_strcat(line, n);
        oc_strcat(line, "/"); usb_hex(ifp->protocol, n, 2);
        oc_strcat(line, n);
        oc_strcat(line, " eps="); usb_num_dec(ifp->ep_count, n);
        oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    for (int e = 0; e < d->n_ep; e++) {
        usb_endpoint_t *ep = &d->eps[e];
        line[0] = 0;
        oc_strcat(line, "    ep");
        usb_num_dec(USB_EP_NUM(ep->addr), n); oc_strcat(line, n);
        oc_strcat(line, (ep->addr & 0x80) ? " in " : " out ");
        const char *t = "?";
        if (ep->attr == USB_EP_ATTR_CONTROL) t = "ctrl";
        else if (ep->attr == USB_EP_ATTR_ISO) t = "iso";
        else if (ep->attr == USB_EP_ATTR_BULK) t = "bulk";
        else if (ep->attr == USB_EP_ATTR_INTERRUPT) t = "int";
        oc_strcat(line, t);
        oc_strcat(line, " max=");
        usb_num_dec(ep->maxpack, n); oc_strcat(line, n);
        oc_strcat(line, " interval=");
        usb_num_dec(ep->interval, n); oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    line[0] = 0;
    oc_strcat(line, "  driver=");
    if (d->class_drv >= 0 && d->class_drv < g_core.n_drivers)
        oc_strcat(line, g_core.drivers[d->class_drv].name);
    else
        oc_strcat(line, "(none)");
    oc_console_puts(line);
    oc_console_puts("\n");
}

/* used by usb_audio.c to bump the device count after enumeration */
void usb_mark_enumerated(void);
void usb_mark_enumerated(void) {
    /* the core recomputes the count on demand; kept for WP-10c
     * binary compatibility */
}
