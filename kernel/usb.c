/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/usb.c
 * Purpose: USB 1.1 host stack: UHCI controller + control transfers +
 *          device enumeration + isochronous OUT scheduling.
 *
 * UHCI (Intel Universal Host Controller) is the USB 1.1 controller of
 * the Intel PIIX3/PIIX4 south bridges - QEMU attaches piix3-usb-uhci
 * at PCI 00:01.2 for `-usb`.  Register layout (I/O BAR, offsets per
 * the Intel UHCI spec / QEMU hw/usb/hcd-uhci):
 *
 *   0x00 USBCMD (RS bit0, HCRESET bit1), 0x02 USBSTS (HCHalted bit5),
 *   0x04 USBINTR, 0x06 FRNUM, 0x08 FLBASEADD (4 KiB aligned),
 *   0x0C SOFMOD, 0x10/0x12 PORTSC1/2.
 *
 * Schedule: every frame-list entry points at our single ISO OUT TD
 * followed by the control QH:
 *
 *   entry[n] -> ISO TD -> (link) -> QH -> terminate
 *                              el -> control TD chain (or terminate)
 *
 * When the ISO TD is inactive the controller simply follows the link,
 * so control transfers work at any time; the ISO TD is re-armed every
 * frame by the audio class driver while a stream is open.
 *
 * Control transfers: SETUP (DATA0, 8 bytes) + DATA (DATA1) + STATUS
 * TDs linked depth-first through the QH element pointer; completion is
 * detected by polling the TD status word (ACTIVE bit clears, QEMU
 * writes the status back into guest memory).
 *
 * Bit definitions verified against QEMU include/hw/usb/uhci-regs.h.
 */
#include "usb.h"
#include "pci.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"

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
#define TD_CTRL_BABBLE    (1u << 20)
#define TD_CTRL_NAK       (1u << 19)
#define TD_CTRL_TIMEOUT   (1u << 18)
#define TD_CTRL_ERR_MSK   (3u << 27)

/* link bits */
#define LINK_TERMINATE    1u
#define LINK_IS_QH        2u
#define LINK_DEPTH        4u

/* PIDs */
#define USB_PID_SETUP     0x2d
#define USB_PID_IN        0x69
#define USB_PID_OUT       0xe1

/* standard requests */
#define USB_REQ_GET_DESC  0x06
#define USB_REQ_SET_ADDR  0x05
#define USB_REQ_SET_CFG   0x09
#define USB_REQ_SET_IFACE 0x0b

#define USB_DT_DEVICE     0x01
#define USB_DT_CONFIG     0x02

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

typedef struct usb_setup_pkt {
    u8  bmRequestType;
    u8  bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} usb_setup_pkt_t;

typedef struct usb_state {
    u16  io;
    u8   bus, dev, func;
    u64  frame_list_phys;   /* 1024 * 4 bytes */
    volatile u32 *frame_list;

    /* schedule structures (one PMM page)
     *   0x000 ISO TD          (32 B)
     *   0x040 control QH      (16 B)
     *   0x080 ctrl TDs[16]    (512 B)
     *   0x280 setup scratch   ( 8 B) */
    uhci_td_t  *iso_td;
    uhci_qh_t  *ctrl_qh;
    uhci_td_t  *ctrl_tds;   /* 16 TDs: 1 setup + 14 data + 1 status */
    u8         *setup_scr;

    u64  sched_phys;
    u64  iso_buf_phys;      /* one frame of ISO data (192 bytes) */
    u8  *iso_buf;

    usb_device_t devices[USB_MAX_DEVICES];
    int  n_devices;
    int  irq;
    u64  irq_count;
    int  up;
} usb_state_t;

#define USB_CTRL_TD_COUNT  16

static usb_state_t g_usb;

static inline void uhci_outw(u16 port, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port)); }
static inline void uhci_outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline u16  uhci_inw(u16 port)  { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u32  uhci_inl(u16 port)  { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static void usb_log(const char *s) { oc_console_puts(s); }

static u16 uhci_portsc(usb_state_t *u, int port) {
    return uhci_inw(u->io + UHCI_USBPORTSC1 + port * 2);
}

static void uhci_portsc_write(usb_state_t *u, int port, u16 v) {
    uhci_outw(u->io + UHCI_USBPORTSC1 + port * 2, v);
}

/* ---- IRQ (counts completions; the polling paths do the real work) ---- */
static void usb_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    usb_state_t *u = &g_usb;
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
    td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_IOC | (3u << 27); /* 3 retries */
    td->token = token;
    td->buffer = (u32)buf;
}

/* Wait until all ctrl_tds with ACTIVE are done or timeout. */
static int usb_ctrl_wait(usb_state_t *u, int ntds) {
    u64 deadline = oc_timer_now_ms() + 1000;
    for (;;) {
        int busy = 0;
        for (int i = 0; i < ntds; i++) {
            u32 ctrl = u->ctrl_tds[i].ctrl;
            if (ctrl & TD_CTRL_ACTIVE) busy = 1;
            else if (ctrl & (TD_CTRL_STALL | TD_CTRL_BABBLE |
                             TD_CTRL_TIMEOUT | TD_CTRL_NAK)) {
                char ln[128]; char nn[16];
                u32 tok = u->ctrl_tds[i].token;
                oc_strcpy(ln, "usb: TD err i=");
                oc_u64_to_str((u64)i, nn); oc_strcat(ln, nn);
                oc_strcat(ln, " ctrl=0x");
                oc_u64_to_hex(ctrl, nn, 8); oc_strcat(ln, nn);
                oc_strcat(ln, " token=0x");
                oc_u64_to_hex(tok, nn, 8); oc_strcat(ln, nn);
                oc_strcat(ln, "\n");
                oc_console_puts(ln);
                return -1;
            }
        }
        if (!busy) return 0;
        if (oc_timer_now_ms() > deadline) {
            usb_log("usb: control transfer timeout\n");
            /* stop the schedule briefly to retire the TDs */
            u16 cmd = uhci_inw(u->io + UHCI_USBCMD);
            uhci_outw(u->io + UHCI_USBCMD, cmd & ~UHCI_CMD_RS);
            for (int i = 0; i < ntds; i++) u->ctrl_tds[i].ctrl &= ~TD_CTRL_ACTIVE;
            uhci_outw(u->io + UHCI_USBCMD, cmd);
            return -1;
        }
        sched_yield();
    }
}

/* ---- control transfer ----
 *
 * TD pool layout: td[0] SETUP, td[1..14] DATA chunks, td[15] STATUS.
 * The chain is linked depth-first so the controller walks it inside
 * one frame (control transfers are tiny).
 */

#define USB_TD_OFF(n)   (0x80u + (u32)(n) * 32u)
#define USB_SCR_OFF     0x280u

typedef struct usb_setup_pkt_s {
    u8  bmRequestType;
    u8  bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} usb_setup_pkt_local_t;

int usb_control(usb_device_t *dev, u8 req_type, u8 request,
                u16 value, u16 index, void *buf, u16 len) {
    usb_state_t *u = &g_usb;
    /* NOTE: present is NOT required here - the enumeration sequence
     * itself runs control transfers before the device is fully marked
     * present. */
    if (!u->up || !dev) return -1;
    if (len > 14 * 64) return -1;   /* data stage must fit the TD pool */

    u8 addr = dev->addr;
    u8 data_pid = (req_type & 0x80) ? USB_PID_IN : USB_PID_OUT;
    u8 status_pid = (req_type & 0x80) ? USB_PID_OUT : USB_PID_IN;
    u16 mps = dev->max_packet0 ? dev->max_packet0 : 8;
    uhci_td_t *tds = u->ctrl_tds;

    usb_setup_pkt_local_t setup;
    setup.bmRequestType = req_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = len;
    oc_memcpy(u->setup_scr, &setup, 8);

    /* SETUP: DATA0, 8 bytes */
    int next = 1;
    usb_td_fill(&tds[0],
                (u32)(uintptr_t)&tds[next] | LINK_DEPTH,
                USB_PID_SETUP, addr, 0, 8,
                (u64)(uintptr_t)u->setup_scr, 0);

    int ntds = 2;
    if (len == 0) {
        /* STATUS: zero-length DATA1 */
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
            ntds = next + 1;
        }
        if (remaining > 0) return -1;
        /* STATUS: zero-length DATA1 in the last slot */
        usb_td_fill(&tds[USB_CTRL_TD_COUNT - 1], LINK_TERMINATE,
                    status_pid, addr, 0, mps, 0, 1);
        ntds = USB_CTRL_TD_COUNT;
    }

    /* hand the chain to the QH element pointer */
    u->ctrl_qh->el_link = (u32)(uintptr_t)&tds[0];
    int rc = usb_ctrl_wait(u, ntds);
    u->ctrl_qh->el_link = LINK_TERMINATE;
    return rc;
}

int usb_set_interface(usb_device_t *dev, u16 interface, u16 alt) {
    return usb_control(dev, 0x01, USB_REQ_SET_IFACE, alt, interface,
                       NULL, 0);
}

/* ---- ISO OUT ---- */

int usb_iso_out_submit(usb_device_t *dev, const void *data, u16 len) {
    usb_state_t *u = &g_usb;
    if (!u->up || !dev || !dev->present) return -1;
    if (len > 1023) return -1;

    /* copy the payload into the (single) ISO buffer and re-arm the TD */
    if (data && len) oc_memcpy(u->iso_buf, data, len);

    u32 token = USB_PID_OUT
        | ((u32)(dev->addr & 0x7f) << 8)
        | ((u32)(dev->iso_out_ep & 0xf) << 15)
        | (((u32)(len - 1) & 0x7ff) << 21)      /* QEMU/maxlen encoding */
        | ((u32)(len & 0x7ff));                 /* spec: packet size 10:0 */
    u->iso_td->token = token;
    u->iso_td->buffer = (u32)u->iso_buf_phys;
    u->iso_td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_IOS | (3u << 27);
    return 0;
}

/* ---- enumeration ---- */

static usb_device_t *usb_alloc_slot(usb_state_t *u) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!u->devices[i].present) return &u->devices[i];
    }
    return NULL;
}

static int usb_enum_device(usb_state_t *u, int port, int ls) {
    usb_device_t *d = usb_alloc_slot(u);
    if (!d) return 0;
    oc_memset(d, 0, sizeof(*d));
    d->present = 1;   /* slot in use while we enumerate */

    /* reset the port */
    u16 psc = uhci_portsc(u, port);
    uhci_portsc_write(u, port, (u16)(psc | UHCI_PORT_RESET));
    for (volatile int i = 0; i < 100000; i++) { }
    uhci_portsc_write(u, port, (u16)(psc & ~UHCI_PORT_RESET));
    for (volatile int i = 0; i < 100000; i++) { }
    psc = uhci_portsc(u, port);
    if (!(psc & UHCI_PORT_CCS)) return 0;
    /* enable */
    uhci_portsc_write(u, port, (u16)(psc | UHCI_PORT_EN));

    d->port = (u8)port;
    d->speed_ls = (u8)ls;
    d->max_packet0 = 8;
    d->addr = 0;

    /* GET_DESCRIPTOR device (first 8 bytes) */
    u8 buf[18];
    oc_memset(buf, 0, sizeof(buf));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC, (USB_DT_DEVICE << 8) | 0,
                    0, buf, 8) != 0) {
        usb_log("usb: GET_DESCRIPTOR(8) failed\n");
        d->present = 0;
        return 0;
    }
    d->max_packet0 = buf[7];
    if (d->max_packet0 == 0 || d->max_packet0 > 64) d->max_packet0 = 8;

    /* SET_ADDRESS (1..n) */
    u8 new_addr = (u8)(u->n_devices + 1);
    if (usb_control(d, 0x00, USB_REQ_SET_ADDR, new_addr, 0, NULL, 0) != 0) {
        d->present = 0;
        return 0;
    }
    d->addr = new_addr;

    /* GET_DESCRIPTOR device (18 bytes) */
    oc_memset(buf, 0, sizeof(buf));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC, (USB_DT_DEVICE << 8) | 0,
                    0, buf, 18) != 0) {
        d->present = 0;
        return 0;
    }
    d->vid = (u16)(buf[8] | (buf[9] << 8));
    d->pid = (u16)(buf[10] | (buf[11] << 8));
    d->class = buf[4];
    d->subclass = buf[5];
    d->protocol = buf[6];

    /* GET_DESCRIPTOR config (9 bytes then full) */
    u8 cfg9[9];
    oc_memset(cfg9, 0, sizeof(cfg9));
    if (usb_control(d, 0x80, USB_REQ_GET_DESC, (USB_DT_CONFIG << 8) | 0,
                    0, cfg9, 9) != 0) {
        d->present = 0;
        return 0;
    }
    u16 total = (u16)(cfg9[2] | (cfg9[3] << 8));
    if (total > USB_RAW_CFG_MAX) total = USB_RAW_CFG_MAX;
    oc_memset(d->raw_cfg, 0, USB_RAW_CFG_MAX);
    if (usb_control(d, 0x80, USB_REQ_GET_DESC, (USB_DT_CONFIG << 8) | 0,
                    0, d->raw_cfg, total) != 0) {
        d->present = 0;
        return 0;
    }
    d->raw_cfg_len = total;

    /* SET_CONFIGURATION 1 */
    if (usb_control(d, 0x00, USB_REQ_SET_CFG, 1, 0, NULL, 0) != 0) {
        d->present = 0;
        return 0;
    }

    d->present = 1;
    return 1;
}

int usb_enumerate(void) {
    usb_state_t *u = &g_usb;
    if (!u->up) return -1;

    int found = 0;
    for (int port = 0; port < 2; port++) {
        u16 psc = uhci_portsc(u, port);
        if (psc & UHCI_PORT_CSC) {
            uhci_portsc_write(u, port, psc);   /* clear connect status change */
        }
        if (!(psc & UHCI_PORT_CCS)) continue;
        /* skip ports whose device we already enumerated */
        int known = 0;
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            if (u->devices[i].present && u->devices[i].port == port)
                known = 1;
        }
        if (known) continue;

        int ls = (psc & UHCI_PORT_LSDA) ? 1 : 0;
        found += usb_enum_device(u, port, ls);
    }
    /* refresh the device count */
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (u->devices[i].present) n++;
    }
    u->n_devices = n;
    return found;
}

int usb_num_devices(void) {
    return g_usb.n_devices;
}

usb_device_t *usb_get_device(int idx) {
    if (idx < 0 || idx >= USB_MAX_DEVICES) return NULL;
    return g_usb.devices[idx].present ? &g_usb.devices[idx] : NULL;
}

/* ---- init ---- */

static void usb_build_schedule(usb_state_t *u) {
    /* schedule page layout (see usb_state_t comment):
     *   0x000 ISO TD, 0x040 control QH, 0x080 ctrl TDs[16],
     *   0x280 setup scratch */
    u64 sched = u->sched_phys;
    u->iso_td  = (uhci_td_t *)(uintptr_t)sched;
    u->ctrl_qh = (uhci_qh_t *)(uintptr_t)(sched + 0x40);
    u->ctrl_tds = (uhci_td_t *)(uintptr_t)(sched + 0x80);
    u->setup_scr = (u8 *)(uintptr_t)(sched + 0x280);
    oc_memset((void *)(uintptr_t)sched, 0, PMM_PAGE_SIZE);

    u->iso_td->link = (u32)(sched + 0x40) | LINK_IS_QH;   /* -> QH */
    u->iso_td->ctrl = TD_CTRL_IOS;              /* inactive */

    u->ctrl_qh->link = LINK_TERMINATE;
    u->ctrl_qh->el_link = LINK_TERMINATE;

    for (int i = 0; i < 1024; i++) {
        u->frame_list[i] = (u32)sched;          /* every frame -> ISO TD */
    }
}

int usb_init(void) {
    usb_state_t *u = &g_usb;
    if (u->up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    int found = pci_find_device(0x8086, 0x7020, &bus, &dev, &func) == 0;
    if (!found) found = pci_find_device(0x8086, 0x7112, &bus, &dev, &func) == 0;
    if (!found) return -1;

    u32 ids = pci_read_config(bus, dev, func, 0x00);
    (void)ids;
    u32 bar4 = pci_read_config(bus, dev, func, 0x20);
    if (!(bar4 & 1)) return -1;
    u->io = (u16)(bar4 & 0xFFF0);
    u->bus = bus; u->dev = dev; u->func = func;
    pci_enable_device(bus, dev, func);

    /* frame list: 4 KiB, 4 KiB aligned */
    u64 fl = pmm_alloc_frame();
    if (!fl) return -1;
    u->frame_list_phys = fl;
    u->frame_list = (volatile u32 *)(uintptr_t)fl;

    /* schedule page + iso buffer */
    u64 sched = pmm_alloc_frame();
    u64 iso = pmm_alloc_frame();
    if (!sched || !iso) return -1;
    u->sched_phys = sched;
    u->iso_buf_phys = iso;
    u->iso_buf = (u8 *)(uintptr_t)iso;
    oc_memset(u->iso_buf, 0, PMM_PAGE_SIZE);

    /* IRQ */
    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    u->irq = -1;
    if (irq < 16 && oc_irq_register_handler(irq, usb_irq_handler, NULL) == 0)
        u->irq = irq;

    /* reset the controller */
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
    return 0;
}

void usb_print_state(void) {
    usb_state_t *u = &g_usb;
    char line[128];
    char n[24];

    oc_console_puts("USB (UHCI): ");
    if (!u->up) {
        oc_console_puts("not present\n");
        return;
    }
    oc_strcpy(line, "io=0x");
    oc_u64_to_hex(u->io, n, 4); oc_strcat(line, n);
    oc_strcat(line, " irq=");
    oc_u64_to_str(u->irq, n); oc_strcat(line, n);
    oc_strcat(line, " irqs=");
    oc_u64_to_str(u->irq_count, n); oc_strcat(line, n);
    oc_strcat(line, " devices=");
    oc_u64_to_str((u64)u->n_devices, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");
    for (int port = 0; port < 2; port++) {
        u16 psc = uhci_portsc(u, port);
        oc_strcpy(line, "  port");
        oc_u64_to_str((u64)port, n); oc_strcat(line, n);
        oc_strcat(line, " sc=0x");
        oc_u64_to_hex(psc, n, 4); oc_strcat(line, n);
        oc_strcat(line, (psc & UHCI_PORT_CCS) ? " connected" : " empty");
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t *d = &u->devices[i];
        if (!d->present) continue;
        oc_strcpy(line, "  dev");
        oc_u64_to_str((u64)i, n); oc_strcat(line, n);
        oc_strcat(line, " addr=");
        oc_u64_to_str(d->addr, n); oc_strcat(line, n);
        oc_strcat(line, " vid=0x");
        oc_u64_to_hex(d->vid, n, 4); oc_strcat(line, n);
        oc_strcat(line, " pid=0x");
        oc_u64_to_hex(d->pid, n, 4); oc_strcat(line, n);
        oc_strcat(line, " class=");
        oc_u64_to_hex(d->class, n, 2); oc_strcat(line, n);
        oc_strcat(line, "/");
        oc_u64_to_hex(d->subclass, n, 2); oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
}

/* used by usb_audio.c to bump the device count after enumeration */
void usb_mark_enumerated(void);
void usb_mark_enumerated(void) {
    usb_state_t *u = &g_usb;
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (u->devices[i].present) n++;
    }
    u->n_devices = n;
}

/* used by usb_audio.c: current USB frame number (0..1023) */
u16 usb_uhci_frnum(void);
u16 usb_uhci_frnum(void) {
    usb_state_t *u = &g_usb;
    if (!u->up) return 0;
    return uhci_inw(u->io + UHCI_USBFRNUM);
}
