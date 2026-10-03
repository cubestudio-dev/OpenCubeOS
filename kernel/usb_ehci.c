/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_ehci.c
 * Purpose: EHCI (Enhanced Host Controller Interface, USB 2.0) backend
 *          for the WP-10d USB core.
 *
 * QEMU models it as "usb-ehci" (Intel 8086:1002-ish default, also
 * ich9-usb-ehci1).  Register block per the EHCI 1.0 spec:
 *
 *   CAP  0x00 CAPLENGTH (byte)  0x04 HCSPARAMS (ports bits 7:0)
 *   OP   0x00 USBCMD (RUNSTOP 0, HCRESET 1, PSE 4, ASE 5, IAAD 6)
 *        0x04 USBSTS (HALT 12, ASS 15, PSS 14)
 *        0x08 USBINTR, 0x0c FRINDEX, 0x14 PERIODICLISTBASE,
 *        0x18 ASYNCLISTADDR, 0x40 CONFIGFLAG, 0x44 PORTSC (N*4)
 *
 * Descriptors (layouts verified against QEMU include/hw/usb/hcd-ehci.h):
 *   QH:  next | epchar (DEVADDR 6:0, EP 11:8, EPS 13:12, DTC 14,
 *        H 15, MPLEN 26:16) | epcap (SMASK 7:0, CMASK 15:8,
 *        HUBADDR 22:16, PORTNUM 29:23, MULT 31:30) | current_qtd |
 *        next_qtd | altnext_qtd | token | bufptr[5]
 *   qTD: next | altnext | token (PID 9:8, CERR 11:10, CPAGE 14:12,
 *        IOC 15, TBYTES 30:16, ACTIVE 7, HALT 6, ..., DTOGGLE 31) |
 *        bufptr[5]
 *
 * Schedule:
 *   - async ring: ctrl QH <-> bulk QH (horizontal links, type QH),
 *     ASE enabled
 *   - periodic: 1024-entry frame list, every entry -> interrupt QH
 *     (1 ms interval, SMASK=1), PSE enabled
 *   - data toggle handled by the HC (QH DTC=1, qTD DTOGGLE=1)
 *   - blocking path: write QH.next_qtd = first qTD, wait for the last
 *     qTD's ACTIVE bit to clear
 */
#include "usb.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"

/* ---- registers (ehci-regs.h verified) ---- */
#define EHCI_CAPLENGTH      0x00
#define EHCI_HCSPARAMS      0x04

#define EHCI_USBCMD         0x00
#define EHCI_CMD_RUN        (1u << 0)
#define EHCI_CMD_HCRESET    (1u << 1)
#define EHCI_CMD_PSE        (1u << 4)
#define EHCI_CMD_ASE        (1u << 5)
#define EHCI_CMD_IAAD       (1u << 6)
#define EHCI_USBSTS         0x04
#define EHCI_STS_HALT       (1u << 12)
#define EHCI_USBINTR        0x08
#define EHCI_FRINDEX        0x0c
#define EHCI_PERIODICBASE   0x14
#define EHCI_ASYNCADDR      0x18
#define EHCI_CONFIGFLAG     0x40
#define EHCI_PORTSC         0x44

#define PORTSC_CONNECT      (1u << 0)
#define PORTSC_CSC          (1u << 1)
#define PORTSC_PED          (1u << 2)
#define PORTSC_PEDC         (1u << 3)
#define PORTSC_PRESET       (1u << 8)
#define PORTSC_PPOWER       (1u << 12)
#define PORTSC_POWNER       (1u << 13)
#define PORTSC_LINESTAT_SH  10

/* link type bits (bits 3:2 of a link pointer: 0=ITD 1=QH 2=SITD 3=FSTN) */
#define EHCI_LTYPE_ITD      0u
#define EHCI_LTYPE_QH       (1u << 2)
#define EHCI_LTYPE_SITD     (2u << 2)
#define EHCI_LTYPE_FSTN     (3u << 2)
#define EHCI_LINK_T         1u
#define EHCI_LINK_TYPE(v)   (((v) >> 1) & 3u)
#define EHCI_LINK_ADDR(v)   ((v) & ~0x1fu)

/* QH epchar */
#define QH_EPCHAR_DEVADDR_SH 0
#define QH_EPCHAR_EP_SH      8
#define QH_EPCHAR_EPS_SH     12
#define QH_EPCHAR_DTC        (1u << 14)
#define QH_EPCHAR_H          (1u << 15)
#define QH_EPCHAR_MPLEN_SH   16
#define QH_EPCHAR_C          (1u << 27)

/* qTD token */
#define QTD_TOKEN_PID_SH     8
#define QTD_PID_OUT          0u
#define QTD_PID_IN           1u
#define QTD_PID_SETUP        2u
#define QTD_TOKEN_CERR_SH    10
#define QTD_TOKEN_IOC        (1u << 15)
#define QTD_TOKEN_TBYTES_SH  16
#define QTD_TOKEN_ACTIVE     (1u << 7)
#define QTD_TOKEN_HALT       (1u << 6)
#define QTD_TOKEN_DBERR      (1u << 5)
#define QTD_TOKEN_BABBLE     (1u << 4)
#define QTD_TOKEN_XACTERR    (1u << 3)
#define QTD_TOKEN_DTOGGLE    (1u << 31)

#define EHCI_QH_SIZE       44    /* 11 words (32-bit layout) */
#define EHCI_QTD_SIZE      32    /* 8 words */
#define EHCI_MAX_PORTS     8
#define EHCI_N_CTRL_QTD    10
#define EHCI_N_BULK_QTD    8

typedef struct ehci_qh {
    volatile u32 next;
    volatile u32 epchar;
    volatile u32 epcap;
    volatile u32 current_qtd;
    volatile u32 next_qtd;
    volatile u32 altnext_qtd;
    volatile u32 token;
    volatile u32 bufptr[5];
} ehci_qh_t;

typedef struct ehci_qtd {
    volatile u32 next;
    volatile u32 altnext;
    volatile u32 token;
    volatile u32 bufptr[5];
} ehci_qtd_t;

typedef struct ehci_state {
    volatile u32 *cap;
    volatile u32 *op;
    u8   bus, dev, func;
    u8   n_ports;

    /* one page holds: QH ctrl | QH bulk | QH int | frame list? no -
     * frame list needs its own 4 KiB page */
    ehci_qh_t *qh_ctrl;
    u64  qh_ctrl_phys;
    ehci_qh_t *qh_bulk;
    u64  qh_bulk_phys;
    ehci_qh_t *qh_int;
    u64  qh_int_phys;

    ehci_qtd_t *ctrl_qtds;
    u64  ctrl_qtd_phys;
    u8  *ctrl_buf;
    u64  ctrl_buf_phys;

    ehci_qtd_t *bulk_qtds;
    u64  bulk_qtd_phys;
    u8  *bulk_buf;
    u64  bulk_buf_phys;

    volatile u32 *frame_list;
    u64  frame_list_phys;

    int  irq;
    u64  irq_count;
    int  up;
    int  busy_ctrl, busy_bulk;
} ehci_state_t;

#define EHCI_MAX_CTRL 2
static ehci_state_t g_ehci[EHCI_MAX_CTRL];
static usb_host_t   g_ehci_host[EHCI_MAX_CTRL];
static usb_hc_ops_t g_ehci_ops[EHCI_MAX_CTRL];
static int g_n_ehci = 0;

static inline u32 ehci_rd(ehci_state_t *e, u32 off) {
    return e->op[off / 4];
}
static inline void ehci_wr(ehci_state_t *e, u32 off, u32 v) {
    e->op[off / 4] = v;
}

static void ehci_log(const char *s) { oc_console_puts(s); }

static void ehci_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)f;
    ehci_state_t *e = (ehci_state_t *)ctx;
    if (!e || !e->up) return;
    u32 sts = ehci_rd(e, EHCI_USBSTS);
    if (sts) {
        ehci_wr(e, EHCI_USBSTS, sts);
        e->irq_count++;
    }
}

/* qTD buffer pointers: 5 page pointers at 4 KiB stride */
static void ehci_qtd_fill(ehci_qtd_t *td, u8 pid, const void *buf,
                          u16 len, int last, u8 toggle_hc) {
    u32 token = ((u32)pid << QTD_TOKEN_PID_SH) |
                (3u << QTD_TOKEN_CERR_SH) |
                (((u32)len & 0x7fff) << QTD_TOKEN_TBYTES_SH) |
                QTD_TOKEN_ACTIVE | QTD_TOKEN_IOC;
    if (toggle_hc) token |= QTD_TOKEN_DTOGGLE;
    if (last) token &= ~QTD_TOKEN_IOC;
    td->token = token;
    u64 p = (u64)(uintptr_t)buf;
    for (int i = 0; i < 5; i++) {
        u64 a = p ? (p + (u64)i * 4096) : 0;
        td->bufptr[i] = (u32)(a & 0xfffff000u);
    }
    td->next = EHCI_LINK_T;
    td->altnext = EHCI_LINK_T;
}


static int ehci_wait_qtd(ehci_state_t *e, ehci_qtd_t *tds, int n,
                         u32 timeout_ms) {
    (void)e;
    u64 deadline = oc_timer_now_ms() + timeout_ms;
    for (;;) {
        int busy = 0;
        for (int i = 0; i < n; i++) {
            u32 tok = tds[i].token;
            if (tok & QTD_TOKEN_ACTIVE) {
                busy = 1;
            } else if (tok & (QTD_TOKEN_HALT | QTD_TOKEN_BABBLE |
                              QTD_TOKEN_XACTERR | QTD_TOKEN_DBERR)) {
                return (tok & QTD_TOKEN_HALT) ? OC_USB_ESTALL
                                              : OC_USB_EIO;
            }
        }
        if (!busy) return 0;
        if (oc_timer_now_ms() > deadline) {
            for (int i = 0; i < n; i++)
                tds[i].token &= ~QTD_TOKEN_ACTIVE;
            return OC_USB_ETIMEDOUT;
        }
        sched_yield();
    }
}

static int ehci_control(usb_host_t *h, usb_dev_t *d,
                        const usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up || !d) return -1;
    if (e->busy_ctrl) return -1;
    if (len > 7 * 64) return OC_USB_EINVAL;

    e->busy_ctrl = 1;
    ehci_qh_t *qh = e->qh_ctrl;
    ehci_qtd_t *tds = e->ctrl_qtds;
    u8 *datab = e->ctrl_buf;

    u32 epchar = ((u32)d->addr & 0x7f) |
                 (((u32)0 & 0xf) << QH_EPCHAR_EP_SH) |
                 (((d->speed == USB_SPEED_LS) ? 1 :
                   (d->speed == USB_SPEED_HS) ? 2 : 0)
                  << QH_EPCHAR_EPS_SH) |
                 QH_EPCHAR_DTC |
                 (((u32)(d->mps0 ? d->mps0 : 64) & 0x7ff)
                  << QH_EPCHAR_MPLEN_SH);
    qh->epchar = epchar;
    qh->epcap = (1u << 30);

    /* qTD chain: SETUP + DATA + STATUS */
    ehci_qtd_fill(&tds[0], QTD_PID_SETUP, setup, 8, 0, 1);
    int n = 1;
    if (len > 0) {
        u8 pid = (setup->bmRequestType & 0x80) ? QTD_PID_IN
                                               : QTD_PID_OUT;
        if (pid == QTD_PID_OUT) oc_memcpy(datab, buf, len);
        ehci_qtd_fill(&tds[n], pid, datab, len, 0, 1);
        tds[n - 1].next = (u32)(e->ctrl_qtd_phys +
                                (u64)n * EHCI_QTD_SIZE);
        n++;
    }
    /* STATUS: opposite direction, zero length */
    {
        u8 pid = (setup->bmRequestType & 0x80) ? QTD_PID_OUT
                                               : QTD_PID_IN;
        ehci_qtd_fill(&tds[n], pid, NULL, 0, 1, 1);
        tds[n - 1].next = (u32)(e->ctrl_qtd_phys +
                                (u64)n * EHCI_QTD_SIZE);
        n++;
    }

    qh->next_qtd = (u32)e->ctrl_qtd_phys;   /* link first qTD */
    int rc = ehci_wait_qtd(e, tds, n, timeout_ms);
    qh->next_qtd = EHCI_LINK_T;
    if (rc == 0 && len > 0 && (setup->bmRequestType & 0x80))
        oc_memcpy(buf, datab, len);
    e->busy_ctrl = 0;
    return rc;
}

static int ehci_bulk(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up || !d || !buf) return -1;
    if (e->busy_bulk) return -1;
    if (len == 0) return 0;
    /* the bulk bounce buffer is ONE page: one qTD moves at most 4 KiB
     * per call (MSC splits with ops->bulk_max) */
    if (len > 4096) return OC_USB_EINVAL;

    e->busy_bulk = 1;
    ehci_qh_t *qh = e->qh_bulk;
    ehci_qtd_t *tds = e->bulk_qtds;

    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? QTD_PID_IN : QTD_PID_OUT;
    u16 mps = 512;
    usb_endpoint_t *epd = usb_find_ep(d, 0xff, USB_EP_ATTR_BULK,
                                      (ep_addr & 0x80) ? 1 : 0);
    if (epd && USB_EP_NUM(epd->addr) == ep && epd->maxpack)
        mps = epd->maxpack;
    if (mps == 0 || mps > 512) mps = 512;

    u32 epchar = ((u32)d->addr & 0x7f) |
                 (((u32)ep & 0xf) << QH_EPCHAR_EP_SH) |
                 (((d->speed == USB_SPEED_LS) ? 1 :
                   (d->speed == USB_SPEED_HS) ? 2 : 0)
                  << QH_EPCHAR_EPS_SH) |
                 QH_EPCHAR_DTC |
                 (((u32)mps & 0x7ff) << QH_EPCHAR_MPLEN_SH);
    qh->epchar = epchar;
    qh->epcap = (1u << 30);

    /* one qTD, one page */
    int n = 0;
    if (pid == QTD_PID_OUT)
        oc_memcpy(e->bulk_buf, buf, len);
    ehci_qtd_fill(&tds[0], pid, e->bulk_buf, len, 1, 1);
    n = 1;

    qh->next_qtd = (u32)e->bulk_qtd_phys;
    int rc = ehci_wait_qtd(e, tds, n, timeout_ms);
    qh->next_qtd = EHCI_LINK_T;
    int moved = (int)len;
    if (rc == 0) {
        /* total-bytes field holds the bytes NOT transferred */
        u32 rem = (tds[0].token >> QTD_TOKEN_TBYTES_SH) & 0x7fffu;
        if (rem > len) rem = len;
        moved = (int)(len - rem);
        if (pid == QTD_PID_IN)
            oc_memcpy(buf, e->bulk_buf, (u32)moved);
    }
    e->busy_bulk = 0;
    return (rc == 0) ? moved : rc;
}

static int ehci_interrupt(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up || !d || !buf) return -1;
    if (len == 0 || len > 512) return OC_USB_EINVAL;
    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? QTD_PID_IN : QTD_PID_OUT;

    ehci_qh_t *qh = e->qh_int;
    ehci_qtd_t *td = e->ctrl_qtds;   /* reuse slot 0 (ctrl is idle
                                        during interrupt use) */
    u32 epchar = ((u32)d->addr & 0x7f) |
                 (((u32)ep & 0xf) << QH_EPCHAR_EP_SH) |
                 (((d->speed == USB_SPEED_LS) ? 1 :
                   (d->speed == USB_SPEED_HS) ? 2 : 0)
                  << QH_EPCHAR_EPS_SH) |
                 QH_EPCHAR_DTC |
                 (((u32)(len & 0x7ff)) << QH_EPCHAR_MPLEN_SH);
    qh->epchar = epchar;
    /* interrupt QH: SMASK = micro-frame 0 of every frame (1 ms) */
    qh->epcap = (1u << 30) | 0x01u;

    if (pid == QTD_PID_OUT) oc_memcpy(e->ctrl_buf, buf, len);
    ehci_qtd_fill(td, pid, e->ctrl_buf, len, 1, 1);
    qh->next_qtd = (u32)e->ctrl_qtd_phys;

    u64 deadline = oc_timer_now_ms() + timeout_ms;
    int rc = 0;
    for (;;) {
        u32 tok = td->token;
        if (!(tok & QTD_TOKEN_ACTIVE)) {
            if (tok & QTD_TOKEN_HALT) rc = OC_USB_ESTALL;
            else if (tok & (QTD_TOKEN_BABBLE | QTD_TOKEN_XACTERR))
                rc = OC_USB_EIO;
            else rc = 0;
            break;
        }
        if (oc_timer_now_ms() > deadline) {
            td->token &= ~QTD_TOKEN_ACTIVE;
            qh->next_qtd = EHCI_LINK_T;
            rc = OC_USB_ENAK;
            break;
        }
        sched_yield();
    }
    qh->next_qtd = EHCI_LINK_T;
    if (rc == 0) {
        if (pid == QTD_PID_IN) oc_memcpy(buf, e->ctrl_buf, len);
        return (int)len;
    }
    return rc;
}

/* EHCI ISO: high-speed iTD (8 transactions, one per micro-frame).
 * Provided for real-hardware use; no current class driver consumes
 * it (the audio driver runs ISO on the UHCI backend). */
typedef struct ehci_itd {
    volatile u32 next;
    volatile u32 trans[8];
    volatile u32 bufptr[7];
} ehci_itd_t;

static int ehci_iso_out(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)data; (void)len;
    return OC_USB_EINVAL;   /* iTD scheduling needs a periodic-ring
                               owner; not consumed by this stack */
}

static int ehci_iso_in(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;
}

/* ==================================================================
 * root hub
 * ================================================================== */

static int ehci_port_count(usb_host_t *h) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    return e ? e->n_ports : 0;
}

static int ehci_port_status(usb_host_t *h, int port,
                            usb_port_status_t *out) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up || port < 0 || port >= e->n_ports) return -1;
    u32 v = ehci_rd(e, EHCI_PORTSC + (u32)port * 4);
    out->connected = (v & PORTSC_CONNECT) ? 1 : 0;
    out->enabled = (v & PORTSC_PED) ? 1 : 0;
    u32 lines = (v >> PORTSC_LINESTAT_SH) & 3;
    if (out->connected && lines == 1)
        out->speed = USB_SPEED_LS;   /* K-state = low speed */
    else
        out->speed = USB_SPEED_HS;   /* EHCI ports are high-speed */
    out->changed = (v & PORTSC_CSC) ? 1 : 0;
    if (v & PORTSC_CSC)
        ehci_wr(e, EHCI_PORTSC + (u32)port * 4, PORTSC_CSC);
    return 0;
}

static int ehci_port_reset(usb_host_t *h, int port, u8 *speed_out) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up || port < 0 || port >= e->n_ports) return -1;
    u32 off = EHCI_PORTSC + (u32)port * 4;
    u32 v = ehci_rd(e, off);
    ehci_wr(e, off, v | PORTSC_PPOWER);
    for (volatile int t = 0; t < 20000; t++) { }
    ehci_wr(e, off, ehci_rd(e, off) | PORTSC_PRESET);
    for (int i = 0; i < 300; i++) {
        u32 w = ehci_rd(e, off);
        if (!(w & PORTSC_PRESET)) break;
        for (volatile int t = 0; t < 5000; t++) { }
    }
    for (volatile int t = 0; t < 200000; t++) { }   /* recovery */
    u32 w = ehci_rd(e, off);
    if (!(w & PORTSC_CONNECT)) return -1;
    if (speed_out) {
        if ((w >> PORTSC_LINESTAT_SH & 3) == 1)
            *speed_out = USB_SPEED_LS;
        else
            *speed_out = USB_SPEED_HS;
    }
    return 0;
}

static int ehci_poll(usb_host_t *h) {
    ehci_state_t *e = (ehci_state_t *)h->priv;
    if (!e || !e->up) return 0;
    u32 sts = ehci_rd(e, EHCI_USBSTS);
    if (sts) ehci_wr(e, EHCI_USBSTS, sts);
    return 0;
}

static const usb_hc_ops_t ehci_ops_tmpl = {
    .name      = "EHCI",
    .bulk_max  = 4096,   /* one qTD, one bounce page */
    .control   = ehci_control,
    .bulk      = ehci_bulk,
    .interrupt = ehci_interrupt,
    .iso_out   = ehci_iso_out,
    .iso_in    = ehci_iso_in,
    .port_count  = ehci_port_count,
    .port_status = ehci_port_status,
    .port_reset  = ehci_port_reset,
    .poll        = ehci_poll,
};

/* ==================================================================
 * probe / init
 * ================================================================== */

static int ehci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_ehci >= EHCI_MAX_CTRL) return -1;
    ehci_state_t *e = &g_ehci[g_n_ehci];
    oc_memset(e, 0, sizeof(*e));

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        ehci_log("ehci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    e->cap = (volatile u32 *)(uintptr_t)mmio;
    u8 caplen = (u8)(e->cap[0] & 0xff);
    if (caplen < 0x20) caplen = 0x20;
    e->op = (volatile u32 *)(uintptr_t)(mmio + caplen);
    e->bus = bus; e->dev = dev; e->func = func;
    pci_enable_device(bus, dev, func);

    u32 hcs = e->cap[EHCI_HCSPARAMS / 4];
    e->n_ports = (u8)(hcs & 0xff);
    if (e->n_ports > EHCI_MAX_PORTS) e->n_ports = EHCI_MAX_PORTS;

    /* HC reset */
    ehci_wr(e, EHCI_USBCMD, ehci_rd(e, EHCI_USBCMD) |
            EHCI_CMD_HCRESET);
    for (int i = 0; i < 2000; i++) {
        if (!(ehci_rd(e, EHCI_USBCMD) & EHCI_CMD_HCRESET)) break;
        for (volatile int t = 0; t < 1000; t++) { }
    }
    /* clear CONFIGFLAG so ports route to this controller (no
     * companion controllers exist in this stack) */
    ehci_wr(e, EHCI_CONFIGFLAG, 0);

    /* allocate descriptors */
    u64 qpage = pmm_alloc_frame();
    u64 ctd = pmm_alloc_frame();
    u64 cbuf = pmm_alloc_frame();
    u64 btd = pmm_alloc_frame();
    u64 bbuf = pmm_alloc_frame();
    u64 fl = pmm_alloc_frame();
    if (!qpage || !ctd || !cbuf || !btd || !bbuf || !fl) return -1;
    oc_memset((void *)(uintptr_t)qpage, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)ctd, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)cbuf, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)btd, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)bbuf, 0, PMM_PAGE_SIZE);
    e->qh_ctrl = (ehci_qh_t *)(uintptr_t)qpage;
    e->qh_ctrl_phys = qpage;
    e->qh_bulk = (ehci_qh_t *)(uintptr_t)(qpage + 64);
    e->qh_bulk_phys = qpage + 64;
    e->qh_int = (ehci_qh_t *)(uintptr_t)(qpage + 128);
    e->qh_int_phys = qpage + 128;
    e->ctrl_qtds = (ehci_qtd_t *)(uintptr_t)ctd;
    e->ctrl_qtd_phys = ctd;
    e->ctrl_buf = (u8 *)(uintptr_t)cbuf;
    e->ctrl_buf_phys = cbuf;
    e->bulk_qtds = (ehci_qtd_t *)(uintptr_t)btd;
    e->bulk_qtd_phys = btd;
    e->bulk_buf = (u8 *)(uintptr_t)bbuf;
    e->bulk_buf_phys = bbuf;
    e->frame_list = (volatile u32 *)(uintptr_t)fl;
    e->frame_list_phys = fl;

    /* async ring: ctrl QH <-> bulk QH */
    e->qh_ctrl->next = (u32)e->qh_bulk_phys | EHCI_LTYPE_QH;
    e->qh_bulk->next = (u32)e->qh_ctrl_phys | EHCI_LTYPE_QH;
    e->qh_ctrl->next_qtd = EHCI_LINK_T;
    e->qh_bulk->next_qtd = EHCI_LINK_T;

    /* periodic frame list: every entry -> int QH (1 ms) */
    for (int i = 0; i < 1024; i++)
        e->frame_list[i] = (u32)e->qh_int_phys | EHCI_LTYPE_QH;
    e->qh_int->next = EHCI_LINK_T;
    e->qh_int->next_qtd = EHCI_LINK_T;

    /* IRQ */
    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    e->irq = -1;
    if (irq < 16 &&
        oc_irq_register_handler(irq, ehci_irq_handler, e) == 0)
        e->irq = irq;

    /* publish schedules + run */
    ehci_wr(e, EHCI_PERIODICBASE, (u32)fl);
    ehci_wr(e, EHCI_ASYNCADDR, (u32)e->qh_ctrl_phys);
    ehci_wr(e, EHCI_USBINTR, 0);   /* polling mode */
    ehci_wr(e, EHCI_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASE |
            EHCI_CMD_PSE);

    e->up = 1;

    usb_host_t *h = &g_ehci_host[g_n_ehci];
    oc_memset(h, 0, sizeof(*h));
    oc_strcpy(h->name, "ehci0");
    if (g_n_ehci > 0) h->name[4] = (char)('0' + g_n_ehci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->priv = e;
    g_ehci_ops[g_n_ehci] = ehci_ops_tmpl;
    h->ops = &g_ehci_ops[g_n_ehci];
    usb_register_host(h, &g_ehci_ops[g_n_ehci]);
    g_n_ehci++;
    return 0;
}

int ehci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    for (int nth = 0; nth < 4; nth++) {
        if (pci_find_class_exact(0x0c0320, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_ehci; i++) {
            if (g_ehci[i].bus == b && g_ehci[i].dev == dv &&
                g_ehci[i].func == fn)
                dup = 1;
        }
        if (!dup && ehci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int ehci_init(const pci_dev_t *dev) {
    if (!dev) return -1;
    return ehci_probe_one(dev->bus, dev->dev, dev->func);
}
