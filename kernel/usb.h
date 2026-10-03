/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c / WP-10d
 * File: kernel/usb.h
 * Purpose: USB host stack interface - implementation: usb.c (+ the
 *          per-controller backends usb_ohci.c / usb_ehci.c /
 *          usb_xhci.c; UHCI lives in usb.c).
 *
 * WP-10c shipped the minimal-but-real USB 1.1 host stack used by the
 * USB audio class driver:
 *   - UHCI controller driver (Intel 8086:7020 piix3 / 8086:7112 piix4)
 *   - blocking control transfers (SETUP + DATA + STATUS TD chains)
 *   - device enumeration (descriptors, SET_ADDRESS / SET_CONFIGURATION
 *     / SET_INTERFACE)
 *   - isochronous OUT scheduling (one ISO TD per 1 ms frame)
 *
 * WP-10d turns that stack into the full USB core:
 *   - a host-controller abstraction (usb_hc_ops_t) so UHCI, OHCI,
 *     EHCI and XHCI backends all plug into one core
 *   - all four transfer types: control, bulk, interrupt, isochronous
 *   - device tree (root hubs + external hubs), hub class support
 *   - hot-plug: connect/disconnect handling on any port
 *   - a class-driver registry (usb_register_driver) with probe()
 *     callbacks; HID / MSC / CDC-ACM / UAC class drivers sit on top
 *
 * The WP-10c entry points (usb_init/usb_enumerate/usb_control/
 * usb_set_interface/usb_iso_out_submit/usb_num_devices/
 * usb_get_device/usb_print_state) keep their exact semantics so the
 * audio class driver and the WP-10c regression stay valid.
 *
 * L1 extension surface (WP-10d):
 *
 *   int usb_register_host(usb_host_t *host, const usb_hc_ops_t *ops);
 *       Register a host controller with the USB core.  The caller
 *       fills host->name and keeps host->priv for itself.  Returns 0
 *       on success (host->index assigned), -1 on error.
 *
 *   int uhci_init(const pci_dev_t *dev);   (usb.c)
 *   int ohci_init(const pci_dev_t *dev);   (usb_ohci.c)
 *   int ehci_init(const pci_dev_t *dev);   (usb_ehci.c)
 *   int xhci_init(const pci_dev_t *dev);   (usb_xhci.c)
 *       Probe/claim one PCI function as the given controller type,
 *       reset it, start the schedule and register it with the core.
 *       Each returns 0 on success, -1 when the controller is absent
 *       or init fails.  usb_probe_all() calls all four in turn.
 *
 *   int usb_probe_all(void);
 *       Scan the PCI bus for every supported controller type
 *       (UHCI/OHCI/EHCI/XHCI), initialize each and enumerate the
 *       devices on their root hubs.  Returns the number of hosts
 *       brought up (>= 0).
 *
 *   int usb_enumerate(usb_host_t *host);
 *       Enumerate the devices hanging off `host` (root-hub ports and
 *       any hubs).  Returns the number of devices found (>= 0).
 *       (usb_enumerate(void) below enumerates every registered host
 *       and keeps the WP-10c signature.)
 *
 *   int usb_control_transfer(usb_dev_t *d, const usb_setup_t *setup,
 *                            void *buf, u16 len);
 *       Blocking control transfer (HOST->dev SETUP, then optional
 *       DATA stage, then STATUS).  Returns 0 on success, negative on
 *       error / timeout.
 *
 *   int usb_bulk_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
 *                         u16 len);
 *       Blocking bulk transfer on endpoint `ep_addr` (bit7 = IN).
 *       Returns bytes transferred (>= 0) or a negative error.
 *
 *   int usb_interrupt_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
 *                              u16 len);
 *       One blocking interrupt-IN/OUT transaction on `ep_addr`.
 *       Returns bytes transferred (>= 0), -OC_USB_ETIMEDOUT when the
 *       endpoint NAKs for the whole timeout window (a normal "no
 *       event" result for HID devices).
 *
 *   int usb_isochronous_transfer(usb_dev_t *d, u8 ep_addr,
 *                                const void *buf, u16 len);
 *       Schedule one isochronous packet (audio: one per 1 ms frame).
 *
 *   int usb_register_driver(const char *name, u8 class_code,
 *                           int (*probe)(usb_dev_t *dev),
 *                           void (*disconnect)(usb_dev_t *dev));
 *       Register a class driver.  The core calls probe() for every
 *       device whose interface/device class matches class_code
 *       (0xff = match everything).  disconnect() runs on hot-unplug.
 */
#ifndef OC_USB_H
#define OC_USB_H

#include "types.h"
#include "pci.h"

/* ---- constants ---- */
#define USB_MAX_DEVICES   16
#define USB_MAX_HOSTS      8
#define USB_MAX_EPS       8
#define USB_MAX_IFS       4
#define USB_RAW_CFG_MAX  512
#define USB_NAME_MAX      40

/* speeds */
#define USB_SPEED_LS 0
#define USB_SPEED_FS 1
#define USB_SPEED_HS 2
#define USB_SPEED_SS 3

/* device / interface classes */
#define USB_CLASS_AUDIO       0x01
#define USB_CLASS_HID         0x03
#define USB_CLASS_HUB         0x09
#define USB_CLASS_CDC         0x02
#define USB_CLASS_CDC_DATA    0x0a
#define USB_CLASS_MSC         0x08
#define USB_CLASS_VENDOR      0xff

/* standard requests */
#define USB_REQ_GET_STATUS  0x00
#define USB_REQ_CLEAR_FEAT  0x01
#define USB_REQ_SET_FEAT    0x03
#define USB_REQ_SET_ADDR    0x05
#define USB_REQ_GET_DESC    0x06
#define USB_REQ_SET_DESC    0x07
#define USB_REQ_GET_CFG     0x08
#define USB_REQ_SET_CFG     0x09
#define USB_REQ_GET_IFACE   0x0a
#define USB_REQ_SET_IFACE   0x0b
#define USB_REQ_SYNCH_FRAME 0x0c

/* descriptor types */
#define USB_DT_DEVICE    0x01
#define USB_DT_CONFIG    0x02
#define USB_DT_STRING    0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT  0x05
#define USB_DT_HID       0x21
#define USB_DT_REPORT    0x22
#define USB_DT_HUB       0x29

/* error codes (negative) */
#define OC_USB_ETIMEDOUT  (-100)
#define OC_USB_ENODEV     (-101)
#define OC_USB_ESTALL     (-102)
#define OC_USB_EIO        (-103)
#define OC_USB_ENOMEM     (-104)
#define OC_USB_EINVAL     (-105)
#define OC_USB_ENAK       (-106)

/* ---- descriptor structs (little-endian, packed) ---- */
typedef struct usb_setup {
    u8  bmRequestType;
    u8  bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} usb_setup_t;

typedef struct usb_dev_desc {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} usb_dev_desc_t;

typedef struct usb_if_desc {
    u8  bLength;
    u8  bDescriptorType;
    u8  bInterfaceNumber;
    u8  bAlternateSetting;
    u8  bNumEndpoints;
    u8  bInterfaceClass;
    u8  bInterfaceSubClass;
    u8  bInterfaceProtocol;
    u8  iInterface;
} usb_if_desc_t;

typedef struct usb_ep_desc {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} usb_ep_desc_t;

/* endpoint attribute types */
#define USB_EP_ATTR_CONTROL     0x00
#define USB_EP_ATTR_ISO         0x01
#define USB_EP_ATTR_BULK        0x02
#define USB_EP_ATTR_INTERRUPT   0x03
#define USB_EP_IN(ep)  ((ep) & 0x80)
#define USB_EP_NUM(ep) ((ep) & 0x0f)

/* ---- host controller abstraction ---- */

typedef struct usb_dev    usb_dev_t;
typedef struct usb_host   usb_host_t;

/* port status returned by ops->port_status */
typedef struct usb_port_status {
    u8 connected;
    u8 enabled;
    u8 speed;             /* USB_SPEED_* */
    u8 changed;           /* connect-status-change seen (hot-plug) */
} usb_port_status_t;

typedef struct usb_hc_ops {
    const char *name;
    /* max bytes in ONE bulk transfer call (0 = 512 default).  MSC
     * chunks its BOT data phase with this so every backend gets a
     * working split: UHCI/OHCI 512, EHCI 4096, XHCI 16 KiB. */
    u16  bulk_max;
    /* blocking control transfer (SETUP+DATA+STATUS) */
    int  (*control)(usb_host_t *h, usb_dev_t *d,
                    const usb_setup_t *setup, void *buf, u16 len,
                    u32 timeout_ms);
    /* blocking bulk transfer; returns bytes moved or negative error */
    int  (*bulk)(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                 void *buf, u16 len, u32 timeout_ms);
    /* one interrupt transaction; returns bytes moved, -OC_USB_ENAK
     * when the endpoint NAKs the whole window */
    int  (*interrupt)(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                      void *buf, u16 len, u32 timeout_ms);
    /* one isochronous packet (audio: one per 1 ms frame) */
    int  (*iso_out)(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                    const void *buf, u16 len);
    int  (*iso_in)(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                   void *buf, u16 len);
    /* root hub */
    int  (*port_count)(usb_host_t *h);
    int  (*port_status)(usb_host_t *h, int port,
                        usb_port_status_t *out);
    int  (*port_reset)(usb_host_t *h, int port, u8 *speed_out);
    /* periodic work: retire completed transfers, poll hot-plug */
    int  (*poll)(usb_host_t *h);
} usb_hc_ops_t;

/* host flags */
#define USB_HOSTF_NATIVE_ADDR  0x01   /* controller does SET_ADDRESS
                                         itself (XHCI): the core must
                                         not send SET_ADDRESS, the
                                         backend maps it to its native
                                         AddressDevice command */

struct usb_host {
    int   index;
    char  name[12];          /* "uhci0", "xhci0", ... */
    u8    bus, dev, func;    /* PCI location */
    u8    flags;
    void *priv;              /* backend-private */
    const usb_hc_ops_t *ops;
    int   up;
};

/* ---- device tree node ---- */

typedef struct usb_endpoint {
    u8  addr;                /* | 0x80 for IN */
    u8  attr;                /* USB_EP_ATTR_* */
    u16 maxpack;
    u8  interval;
    u8  iface;               /* interface it belongs to */
} usb_endpoint_t;

typedef struct usb_interface {
    u8  number;
    u8  alt;
    u8  class, subclass, protocol;
    u8  ep_start, ep_count;  /* slice into usb_dev_t.eps */
} usb_interface_t;

struct usb_dev {
    int   present;
    int   slot;              /* index in the device table == USB address-1 */
    u8    addr;
    u8    speed;
    usb_host_t *host;

    int   parent;            /* index of the parent hub device, -1 = root */
    u8    hub_port;          /* port number on the parent (or root) hub */

    u16   vid, pid;
    u8    class, subclass, protocol;
    u16   mps0;
    char  product[USB_NAME_MAX];

    usb_endpoint_t  eps[USB_MAX_EPS];
    usb_interface_t ifs[USB_MAX_IFS];
    u8    n_ep, n_if;

    u8    cfg_raw[USB_RAW_CFG_MAX];
    u16   cfg_len;

    void *class_priv;        /* class-driver private state */
    int   class_drv;         /* driver index, -1 = none */
};

/* ---- core API ---- */

/* WP-10c-compatible entry points */
int  usb_init(void);                        /* UHCI bring-up (legacy path) */
int  usb_probe_all(void);                   /* WP-10d: all four backends */
int  usb_num_devices(void);
int  usb_num_hosts(void);                   /* WP-10d: registered HC count */
usb_dev_t *usb_get_device(int idx);
int  usb_control(usb_dev_t *dev, u8 req_type, u8 request,
                 u16 value, u16 index, void *buf, u16 len);
int  usb_set_interface(usb_dev_t *dev, u16 interface, u16 alt);
int  usb_iso_out_submit(usb_dev_t *dev, const void *data, u16 len);
u16  usb_uhci_frnum(void);
void usb_print_state(void);

/* WP-10d core */
int  usb_register_host(usb_host_t *host, const usb_hc_ops_t *ops);
int  usb_enumerate_host(usb_host_t *host);
int  usb_enumerate(void);                   /* every registered host */
void usb_poll(void);                        /* hot-plug + transfer poll */

int  usb_control_transfer(usb_dev_t *d, const usb_setup_t *setup,
                          void *buf, u16 len);
int  usb_bulk_transfer(usb_dev_t *d, u8 ep_addr, void *buf, u16 len);
int  usb_bulk_transfer_timeout(usb_dev_t *d, u8 ep_addr, void *buf,
                               u16 len, u32 timeout_ms);
int  usb_interrupt_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
                            u16 len);
int  usb_isochronous_transfer(usb_dev_t *d, u8 ep_addr, void *buf,
                              u16 len);

typedef int  (*usb_probe_fn)(usb_dev_t *dev);
typedef void (*usb_disconnect_fn)(usb_dev_t *dev);
int  usb_register_driver(const char *name, u8 class_code,
                         usb_probe_fn probe,
                         usb_disconnect_fn disconnect);
int  usb_num_drivers(void);

/* backend probe helpers (called by usb_probe_all) */
int  ohci_probe_all(void);   /* usb_ohci.c */
int  ehci_probe_all(void);   /* usb_ehci.c */
int  xhci_probe_all(void);   /* usb_xhci.c */

/* helpers shared with the class drivers / backends */
int  usb_get_string(usb_dev_t *d, u8 idx, char *out, int out_max);
int  usb_get_report_desc(usb_dev_t *d, u8 iface, void *buf, u16 len);
usb_endpoint_t *usb_find_ep(usb_dev_t *d, u8 iface, u8 attr, int in);
usb_interface_t *usb_find_if(usb_dev_t *d, u8 class, u8 subclass,
                             u8 proto, int nth);
int  usb_dev_by_class(usb_dev_t *d, u8 class);

/* stall recovery: after CLEAR_FEATURE(ENDPOINT_HALT) the class driver
 * resets the endpoint's data toggle (UHCI/OHCI keep it in software;
 * XHCI runs a native Reset Endpoint command).  EHCI tracks the toggle
 * in hardware, so it is a no-op there. */
void usb_tog_reset(usb_dev_t *d, u8 ep_addr);

/* status printing (usb/usbdev commands) */
void usb_print_tree(void);
void usb_print_device(int slot);

#endif /* OC_USB_H */
