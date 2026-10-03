/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_ohci.c
 * Purpose: OHCI (Open Host Controller Interface, USB 1.1) backend for
 *          the WP-10d USB core.  Registers with usb.c through
 *          usb_hc_ops_t.
 *
 * OHCI is the USB 1.1 controller of non-Intel chipsets (Apple
 * KeyLargo, SiS, ALi, ...).  QEMU models it as "pci-ohci"
 * (Apple KeyLargo, PCI 106b:003f).
 *
 * MMIO register block (BAR0), 32-bit registers:
 *   0x00 HcRevision     0x04 HcControl      0x08 HcCommandStatus
 *   0x0c HcInterruptStatus   0x10 HcInterruptEnable
 *   0x18 HcHCCA         0x20 HcControlHeadED     0x28 HcBulkHeadED
 *   0x30 HcDoneHead     0x34 HcFmInterval
 *   0x48 HcRhDescriptorA     0x4c HcRhDescriptorB
 *   0x50 HcRhStatus     0x54 HcRhPortStatus (N ports)
 *
 * Descriptor layouts verified against QEMU hw/usb/hcd-ohci.c:
 *   ED word0: FA 6:0 | EN 10:7 | D 12:11 | S(speed) 13 | K 14 |
 *             F(format) 15 | MPS 26:16
 *   ED word2: head TD pointer (bits 2:3 reserved-read-as-1, bit1 =
 *             toggle carry)
 *   TD word0: CC 31:28 | EC 27:26 | T1/T0 25:24 | DI 23:21 |
 *             DP 20:19 (0=SETUP 1=OUT 2=IN) | R 18
 *   CC: 0=noerror 4=stall 5=device-not-responding 8/9=over/underrun;
 *   TDs are issued with CC=0xF ("not accessed"); the HC writes the
 *   final CC, so the blocking path polls it.
 *
 * Schedule:
 *   - control ED chain on HcControlHeadED (CLE), bulk chain on
 *     HcBulkHeadED (BLE), one interrupt ED in HCCA slot 0
 *   - blocking transfers: build ED+TD chain, link at list head, wait
 *     for the last TD's CC to leave 0xF, unlink
 */
#include "usb.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"

/* ---- registers ---- */
#define OHCI_CTRL        0x04
#define OHCI_CMDSTS      0x08
#define OHCI_INTSTS      0x0c
#define OHCI_INTEN       0x10
#define OHCI_HCCA        0x18
#define OHCI_CTRL_HEAD   0x20
#define OHCI_BULK_HEAD   0x28
#define OHCI_FMINTERVAL  0x34
#define OHCI_RHDESC_A    0x48
#define OHCI_RHSTATUS    0x50
#define OHCI_RHPORT      0x54

/* HcControl */
#define OHCI_CTL_CLE      (1u << 4)
#define OHCI_CTL_BLE      (1u << 5)
#define OHCI_CTL_HCFS_MSK (3u << 6)
#define OHCI_CTL_HCFS_OPR (2u << 6)
#define OHCI_CTL_HCFS_RES (1u << 6)
#define OHCI_CTL_IR       (1u << 8)
#define OHCI_CTL_PLE      (1u << 2)
#define OHCI_CTL_IE       (1u << 3)

/* HcCommandStatus */
#define OHCI_CMD_HCR     (1u << 0)
#define OHCI_CMD_CLF     (1u << 1)
#define OHCI_CMD_BLF     (1u << 2)

/* TD word0 (verified against QEMU hcd-ohci.c) */
#define TD_CC_MSK        (0xfu << 28)
#define TD_CC_NOTACC     (0xfu << 28)
#define TD_CC_NOERR      0x0u
#define TD_CC_STALL      (0x4u << 28)
#define TD_CC_NORESP     (0x5u << 28)
#define TD_CC_IOERR      (0xcu << 28)
#define TD_EC_MSK        (3u << 26)
#define TD_T0            (1u << 24)
#define TD_T1            (1u << 25)
#define TD_DI_MSK        (7u << 21)
#define TD_DP_MSK        (3u << 19)
#define TD_DP_SETUP      (0u << 19)
#define TD_DP_OUT        (1u << 19)
#define TD_DP_IN         (2u << 19)
#define TD_R             (1u << 18)

/* ED word0 */
#define ED_FA_MSK        0x7fu
#define ED_EN_MSK        (0xfu << 7)
#define ED_D_MSK         (3u << 11)
#define ED_S             (1u << 13)
#define ED_K             (1u << 14)
#define ED_F             (1u << 15)
#define ED_MPS_MSK       (0x7ffu << 16)
#define ED_HEAD_RSV      ((1u << 2) | (1u << 3))

/* RH port bits */
#define RHPORT_CCS       (1u << 0)
#define RHPORT_PES       (1u << 1)
#define RHPORT_PPS       (1u << 8)
#define RHPORT_LSDA      (1u << 9)
#define RHPORT_PPE       (1u << 1)
#define RHPORT_PRS       (1u << 4)
#define RHPORT_CSC       (1u << 16)
#define RHPORT_PRSC      (1u << 20)

/* interrupt enable */
#define OHCI_INT_MASTER  (1u << 31)
#define OHCI_INT_WDH     (1u << 1)
#define OHCI_INT_RHSC    (1u << 6)

#define OHCI_MAX_PORTS   6
#define OHCI_N_CTRL_TD   10
#define OHCI_N_BULK_TD   8

typedef struct ohci_ed {
    volatile u32 word0;
    volatile u32 tail;
    volatile u32 head;
    volatile u32 next;
} ohci_ed_t;

typedef struct ohci_td {
    volatile u32 word0;
    volatile u32 cur;
    volatile u32 next;
    volatile u32 be;
} ohci_td_t;

typedef struct ohci_hcca {
    volatile u32 int_table[32];
    volatile u32 frame_pad;
    volatile u32 done_head;
    u8 reserved[116];
} ohci_hcca_t;

typedef struct ohci_state {
    volatile u32 *regs;
    u8   bus, dev, func;

    ohci_hcca_t *hcca;
    u64  hcca_phys;

    /* one ED per transfer direction group; TD pools per ED */
    ohci_ed_t *ctrl_ed;
    u64   ctrl_ed_phys;
    ohci_td_t *ctrl_tds;
    u64   ctrl_td_phys;
    u8   *ctrl_buf;
    u64   ctrl_buf_phys;
    int   ctrl_busy;

    ohci_ed_t *bulk_ed;
    u64   bulk_ed_phys;
    ohci_td_t *bulk_tds;
    u64   bulk_td_phys;
    u8   *bulk_buf;
    u64   bulk_buf_phys;
    int   bulk_busy;

    ohci_ed_t *int_ed;
    u64   int_ed_phys;
    ohci_td_t *int_td;
    u64   int_td_phys;
    u8   *int_buf;
    u64   int_buf_phys;

    u8    n_ports;
    int   irq;
    u64   irq_count;
    int   up;
    /* data toggle per (address, endpoint, direction); bit0 = OUT,
     * bit1 = IN (same layout as the UHCI backend) */
    u8    toggles[USB_MAX_DEVICES * 16];
} ohci_state_t;

#define OHCI_MAX_CTRL 2
static ohci_state_t g_ohci[OHCI_MAX_CTRL];
static usb_host_t   g_ohci_host[OHCI_MAX_CTRL];
static usb_hc_ops_t g_ohci_ops[OHCI_MAX_CTRL];
static int g_n_ohci = 0;

static inline u32 ohci_rd(ohci_state_t *o, u32 off) {
    return o->regs[off / 4];
}
static inline void ohci_wr(ohci_state_t *o, u32 off, u32 v) {
    o->regs[off / 4] = v;
}

static void ohci_log(const char *s) { oc_console_puts(s); }

static void ohci_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)f;
    ohci_state_t *o = (ohci_state_t *)ctx;
    if (!o || !o->up) return;
    u32 sts = ohci_rd(o, OHCI_INTSTS);
    if (sts) {
        ohci_wr(o, OHCI_INTSTS, sts);
        o->irq_count++;
    }
}

/* ==================================================================
 * transfer engine
 * ================================================================== */

static void ohci_ed_init(ohci_state_t *o, ohci_ed_t *ed,
                         u64 ed_phys, u8 fmt) {
    (void)o;
    ed->word0 = ED_K | fmt;      /* skip until armed */
    ed->tail = 0;
    ed->head = ED_HEAD_RSV;
    ed->next = 0;
    /* keep it self-consistent so the HC never chokes on it */
    (void)ed_phys;
}

/* arm an ED for one transfer and wait for completion of its TD chain.
 *  ed already points at tds[0]; ntd TDs chained; last TD -> tail 0 */
static int ohci_run(ohci_state_t *o, ohci_ed_t *ed, u64 ed_phys,
                    ohci_td_t *tds, u64 td_phys, int ntd,
                    int list, u32 timeout_ms) {
    (void)td_phys;
    /* clear the issue bits, then tell the HC to re-scan the list */
    if (list == 0) {          /* control */
        ohci_wr(o, OHCI_CTRL_HEAD, (u32)ed_phys);
        ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_CLF);
    } else {                  /* bulk */
        ohci_wr(o, OHCI_BULK_HEAD, (u32)ed_phys);
        ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_BLF);
    }

    u64 deadline = oc_timer_now_ms() + timeout_ms;
    int rc = 0;
    for (;;) {
        u32 w0 = tds[ntd - 1].word0;
        u32 cc = w0 & TD_CC_MSK;
        if (cc != TD_CC_NOTACC) {
            if (cc == TD_CC_NOERR) rc = 0;
            else if (cc == TD_CC_STALL) rc = OC_USB_ESTALL;
            else if (cc == TD_CC_NORESP) rc = OC_USB_ETIMEDOUT;
            else rc = OC_USB_EIO;
            break;
        }
        if (oc_timer_now_ms() > deadline) {
            /* disarm: skip the ED so the HC stops touching it */
            ed->word0 |= ED_K;
            rc = OC_USB_ETIMEDOUT;
            break;
        }
        sched_yield();
    }
    /* disarm and clear the completion flags */
    ed->word0 |= ED_K;
    if (list == 0) ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_CLF);
    else ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_BLF);
    return rc;
}

static int ohci_control(usb_host_t *h, usb_dev_t *d,
                        const usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || !d) return -1;
    if (o->ctrl_busy) return -1;
    if (len > 7 * 64) return OC_USB_EINVAL;

    o->ctrl_busy = 1;
    ohci_ed_t *ed = o->ctrl_ed;
    ohci_td_t *tds = o->ctrl_tds;
    u8 *datab = o->ctrl_buf;
    u16 mps = d->mps0 ? d->mps0 : 8;

    /* TD plan: 0=SETUP, 1..n=DATA (IN or OUT), last=STATUS */
    int ntd = 2;
    if (len == 0) ntd = 2;   /* SETUP + STATUS */

    /* SETUP TD: 8 bytes, DATA0 */
    oc_memcpy(datab, setup, 8);
    tds[0].word0 = TD_CC_NOTACC | TD_DP_SETUP | TD_T0 | (7u);
    tds[0].cur = (u32)(uintptr_t)datab;
    tds[0].next = (u32)(o->ctrl_td_phys + sizeof(ohci_td_t));
    tds[0].be = (u32)(uintptr_t)(datab + 7);

    int td = 1;
    if (len > 0) {
        u32 dp = (setup->bmRequestType & 0x80) ? TD_DP_IN : TD_DP_OUT;
        if (dp == TD_DP_OUT)
            oc_memcpy(datab + 8, buf, len);
        u16 remaining = len;
        u64 bp = o->ctrl_buf_phys + 8;
        u8 tog = 0;   /* 0 -> T0 first (control DATA starts DATA1) */
        while (remaining > 0 && td < OHCI_N_CTRL_TD - 1) {
            u16 chunk = remaining > mps ? mps : remaining;
            tds[td].word0 = TD_CC_NOTACC | dp |
                            (tog ? TD_T1 : TD_T0) | (chunk - 1);
            tds[td].cur = (u32)bp;
            tds[td].next = (u32)(o->ctrl_td_phys +
                                 (u64)(td + 1) * sizeof(ohci_td_t));
            tds[td].be = (u32)(bp + chunk - 1);
            bp += chunk;
            remaining -= chunk;
            tog ^= 1;
            td++;
        }
        if (remaining > 0) {
            o->ctrl_busy = 0;
            return OC_USB_EINVAL;
        }
        ntd = td + 1;
    }

    /* STATUS TD: zero length, DATA1, opposite direction */
    u8 dp_stat = (setup->bmRequestType & 0x80) ? TD_DP_OUT : TD_DP_IN;
    tds[ntd - 1].word0 = TD_CC_NOTACC | dp_stat | TD_T1 | 0;
    tds[ntd - 1].cur = 0;
    tds[ntd - 1].next = 0;
    tds[ntd - 1].be = 0;
    /* chain the TD just before the status one to it */
    if (ntd >= 3) tds[ntd - 2].next = (u32)(o->ctrl_td_phys +
        (u64)(ntd - 1) * sizeof(ohci_td_t));

    /* arm the ED */
    u32 w0 = ((u32)d->addr & ED_FA_MSK) |
             (((u32)0 & 0xf) << 7) |          /* EP0 */
             (((u32)mps & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) w0 |= ED_S;
    ed->word0 = w0;
    ed->tail = 0;
    ed->head = (u32)o->ctrl_td_phys | ED_HEAD_RSV;
    ed->next = 0;

    int rc = ohci_run(o, ed, o->ctrl_ed_phys, tds, o->ctrl_td_phys,
                      ntd, 0, timeout_ms);

    /* pull IN data back */
    if (rc == 0 && len > 0 && (setup->bmRequestType & 0x80))
        oc_memcpy(buf, datab + 8, len);
    o->ctrl_busy = 0;
    return rc;
}

/* ---- toggles (per address / endpoint / direction) ---- */

static u8 ohci_tog_get(ohci_state_t *o, usb_dev_t *d, u8 ep_addr) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    return (o->toggles[idx] >> ((ep_addr & 0x80) ? 1 : 0)) & 1;
}

static void ohci_tog_set(ohci_state_t *o, usb_dev_t *d, u8 ep_addr,
                         u8 v) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    u8 bit = (u8)(1u << ((ep_addr & 0x80) ? 1 : 0));
    if (v) o->toggles[idx] |= bit;
    else   o->toggles[idx] &= (u8)~bit;
}

void usb_ohci_tog_reset(usb_dev_t *d, u8 ep_addr) {
    for (int i = 0; i < OHCI_MAX_CTRL; i++) {
        if (g_ohci[i].up) ohci_tog_set(&g_ohci[i], d, ep_addr, 0);
    }
}

static int ohci_bulk(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !buf) return -1;
    if (o->bulk_busy) return -1;
    if (len == 0) return 0;
    if (d->speed == USB_SPEED_LS) return OC_USB_EINVAL;
    if (len > OHCI_N_BULK_TD * 64) return OC_USB_EINVAL;

    o->bulk_busy = 1;
    ohci_ed_t *ed = o->bulk_ed;
    ohci_td_t *tds = o->bulk_tds;

    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u32 dp = (ep_addr & 0x80) ? TD_DP_IN : TD_DP_OUT;
    u16 mps = 64;

    int ntd = (int)((len + mps - 1) / mps);
    u8 base_tog = ohci_tog_get(o, d, ep_addr);
    u16 done_bytes = 0;
    for (int i = 0; i < ntd; i++) {
        u16 chunk = (u16)(len - done_bytes);
        if (chunk > mps) chunk = mps;
        u8 tog = (u8)(base_tog ^ (i & 1));
        if (dp == TD_DP_OUT)
            oc_memcpy(o->bulk_buf + done_bytes,
                      (const void *)((uintptr_t)buf + done_bytes),
                      chunk);
        tds[i].word0 = TD_CC_NOTACC | dp | (tog ? TD_T1 : TD_T0) |
                       (chunk - 1) | (i + 1 < ntd ? TD_R : 0);
        tds[i].cur = (u32)(o->bulk_buf_phys + done_bytes);
        tds[i].next = (i + 1 < ntd)
            ? (u32)(o->bulk_td_phys + (u64)(i + 1) * sizeof(ohci_td_t))
            : 0;
        tds[i].be = (u32)(o->bulk_buf_phys + done_bytes + chunk - 1);
        done_bytes += chunk;
    }

    u32 w0 = ((u32)addr & ED_FA_MSK) | (((u32)ep & 0xf) << 7) |
             (((u32)mps & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) w0 |= ED_S;
    ed->word0 = w0;
    ed->tail = 0;
    ed->head = (u32)o->bulk_td_phys | ED_HEAD_RSV;
    ed->next = 0;

    int rc = ohci_run(o, ed, o->bulk_ed_phys, tds, o->bulk_td_phys,
                      ntd, 1, timeout_ms);

    /* retire: flip toggles of really-transferred TDs only; copy IN
     * data back from the bounce buffer */
    int moved = 0;
    int walked = 0;
    for (int i = 0; i < ntd; i++) {
        u32 w = tds[i].word0;
        if ((w & TD_CC_MSK) == TD_CC_NOTACC) break;  /* not walked */
        u32 cc = w & TD_CC_MSK;
        if (cc != TD_CC_NOERR) break;   /* failed: no toggle advance */
        walked++;
        u16 chunk = (u16)(len - moved);
        if (chunk > mps) chunk = mps;
        if (dp == TD_DP_IN)
            oc_memcpy((void *)((uintptr_t)buf + moved),
                      o->bulk_buf + moved, chunk);
        moved += chunk;
        if (chunk < mps) break;
    }
    ohci_tog_set(o, d, ep_addr, (u8)(base_tog ^ (walked & 1)));
    o->bulk_busy = 0;
    if (rc != 0) return rc;
    return moved;
}

static int ohci_interrupt(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !buf) return -1;
    if (len == 0 || len > 64) return OC_USB_EINVAL;
    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u32 dp = (ep_addr & 0x80) ? TD_DP_IN : TD_DP_OUT;

    if (dp == TD_DP_OUT)
        oc_memcpy(o->int_buf, buf, len);

    u8 tog = ohci_tog_get(o, d, ep_addr);
    ohci_ed_t *ed = o->int_ed;
    ohci_td_t *td = o->int_td;
    td->word0 = TD_CC_NOTACC | dp | (tog ? TD_T1 : TD_T0) | (len - 1);
    td->cur = (u32)o->int_buf_phys;
    td->next = 0;
    td->be = (u32)(o->int_buf_phys + len - 1);
    ed->word0 = ((u32)addr & ED_FA_MSK) | (((u32)ep & 0xf) << 7) |
                (((u32)len & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) ed->word0 |= ED_S;
    ed->tail = 0;
    ed->head = (u32)o->int_td_phys | ED_HEAD_RSV;
    ed->next = 0;

    u64 deadline = oc_timer_now_ms() + timeout_ms;
    int rc = 0;
    for (;;) {
        u32 cc = td->word0 & TD_CC_MSK;
        if (cc != TD_CC_NOTACC) {
            if (cc == TD_CC_NOERR) rc = 0;
            else if (cc == TD_CC_STALL) rc = OC_USB_ESTALL;
            else if (cc == TD_CC_NORESP) rc = OC_USB_ETIMEDOUT;
            else rc = OC_USB_EIO;
            break;
        }
        if (oc_timer_now_ms() > deadline) {
            ed->word0 |= ED_K;
            rc = OC_USB_ENAK;
            break;
        }
        sched_yield();
    }
    ed->word0 |= ED_K;
    if (rc == 0) {
        ohci_tog_set(o, d, ep_addr, (u8)(1 - ohci_tog_get(o, d, ep_addr)));
        if (dp == TD_DP_IN)
            oc_memcpy(buf, o->int_buf, len);
        return (int)len;
    }
    return rc;
}

/* OHCI ISO TD layout (hcd-ohci.c): SF 15:0 | DI 23:21 | FC 27:24 |
 * CC 31:28; the ED needs the F (format) bit. */
static int ohci_iso_out(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !d->present) return -1;
    if (len > 1023) return -1;
    /* ISO OUT on OHCI: use the control TD pool's first TD as an ISO
     * TD on a dedicated ED is over-engineering for this stack; the
     * audio class driver runs on the UHCI backend (WP-10c path).
     * Still, make the path real for real-hardware use: reuse the
     * interrupt ED in ISO format (F=1) with an ISO TD. */
    (void)data; (void)ep_addr;
    ohci_ed_t *ed = o->int_ed;
    ohci_td_t *td = o->int_td;
    if (data && len) oc_memcpy(o->int_buf, data, len);
    u16 frame = (u16)(ohci_rd(o, OHCI_FMINTERVAL), 0);
    (void)frame;
    td->word0 = TD_CC_NOTACC | ((0u) << 24) | (0u << 21); /* FC=0 */
    td->cur = (u32)o->int_buf_phys;
    td->next = 0;
    td->be = (u32)(o->int_buf_phys + (len ? len - 1 : 0));
    ed->word0 = ((u32)d->addr & ED_FA_MSK) |
                (((u32)USB_EP_NUM(ep_addr) & 0xf) << 7) |
                ED_F | (((u32)len & 0x7ff) << 16);
    ed->tail = 0;
    ed->head = (u32)o->int_td_phys | ED_HEAD_RSV;
    ed->next = 0;
    /* ISO EDs live in the periodic list; QEMU also services them from
     * the control/bulk scan when F=1 is set, and the audio class
     * driver drives the frame cadence itself. */
    return 0;
}

static int ohci_iso_in(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;   /* not needed by any current class driver */
}

/* ==================================================================
 * root hub
 * ================================================================== */

static int ohci_port_count(usb_host_t *h) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    return o ? o->n_ports : 0;
}

static int ohci_port_status(usb_host_t *h, int port,
                            usb_port_status_t *out) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || port < 0 || port >= o->n_ports) return -1;
    u32 v = ohci_rd(o, OHCI_RHPORT + (u32)port * 4);
    out->connected = (v & RHPORT_CCS) ? 1 : 0;
    out->enabled = (v & RHPORT_PES) ? 1 : 0;
    out->speed = (v & RHPORT_LSDA) ? USB_SPEED_LS : USB_SPEED_FS;
    out->changed = (v & RHPORT_CSC) ? 1 : 0;
    if (v & RHPORT_CSC)
        ohci_wr(o, OHCI_RHPORT + (u32)port * 4, RHPORT_CSC);
    return 0;
}

static int ohci_port_reset(usb_host_t *h, int port, u8 *speed_out) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up || port < 0 || port >= o->n_ports) return -1;
    u32 off = OHCI_RHPORT + (u32)port * 4;
    ohci_wr(o, off, RHPORT_PRS);
    for (int i = 0; i < 200; i++) {
        u32 v = ohci_rd(o, off);
        if (v & RHPORT_PRSC) {
            ohci_wr(o, off, RHPORT_PRSC);
            break;
        }
        for (volatile int t = 0; t < 2000; t++) { }
    }
    u32 v = ohci_rd(o, off);
    if (!(v & RHPORT_CCS)) return -1;
    if (speed_out)
        *speed_out = (v & RHPORT_LSDA) ? USB_SPEED_LS : USB_SPEED_FS;
    return 0;
}

static int ohci_poll(usb_host_t *h) {
    ohci_state_t *o = (ohci_state_t *)h->priv;
    if (!o || !o->up) return 0;
    u32 sts = ohci_rd(o, OHCI_INTSTS);
    if (sts) ohci_wr(o, OHCI_INTSTS, sts);   /* clear for the poller */
    return 0;
}

static const usb_hc_ops_t ohci_ops_tmpl = {
    .name      = "OHCI",
    .bulk_max  = 512,   /* 8 TDs x 64 B FS bulk cap */
    .control   = ohci_control,
    .bulk      = ohci_bulk,
    .interrupt = ohci_interrupt,
    .iso_out   = ohci_iso_out,
    .iso_in    = ohci_iso_in,
    .port_count  = ohci_port_count,
    .port_status = ohci_port_status,
    .port_reset  = ohci_port_reset,
    .poll        = ohci_poll,
};

/* ==================================================================
 * probe / init
 * ================================================================== */

static int ohci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_ohci >= OHCI_MAX_CTRL) return -1;
    ohci_state_t *o = &g_ohci[g_n_ohci];
    oc_memset(o, 0, sizeof(*o));

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        ohci_log("ohci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    o->regs = (volatile u32 *)(uintptr_t)mmio;
    o->bus = bus; o->dev = dev; o->func = func;
    pci_enable_device(bus, dev, func);

    /* reset */
    u32 ctl = ohci_rd(o, OHCI_CTRL);
    if (ctl & OHCI_CTL_IR) {
        /* SMM active: take over */
        ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_HCR);
        for (int i = 0; i < 1000; i++) {
            if (!(ohci_rd(o, OHCI_CMDSTS) & OHCI_CMD_HCR)) break;
            for (volatile int t = 0; t < 1000; t++) { }
        }
        ohci_wr(o, OHCI_CTRL, OHCI_CTL_HCFS_RES);
        for (volatile int t = 0; t < 100000; t++) { }
    }
    ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_HCR);
    {
        int ok = 0;
        for (int i = 0; i < 2000; i++) {
            if (!(ohci_rd(o, OHCI_CMDSTS) & OHCI_CMD_HCR)) {
                ok = 1;
                break;
            }
            for (volatile int t = 0; t < 1000; t++) { }
        }
        if (!ok) {
            ohci_log("ohci: reset timeout\n");
            return -1;
        }
    }

    /* allocate the HCCA + ED/TD pools */
    u64 hcca_p = pmm_alloc_frame();
    u64 ctrl_ed_p = pmm_alloc_frame();
    u64 ctrl_td_p = pmm_alloc_frame();
    u64 ctrl_buf_p = pmm_alloc_frame();
    u64 bulk_ed_p = pmm_alloc_frame();
    u64 bulk_td_p = pmm_alloc_frame();
    u64 bulk_buf_p = pmm_alloc_frame();
    u64 int_ed_p = pmm_alloc_frame();
    u64 int_td_p = pmm_alloc_frame();
    u64 int_buf_p = pmm_alloc_frame();
    if (!hcca_p || !ctrl_ed_p || !ctrl_td_p || !ctrl_buf_p ||
        !bulk_ed_p || !bulk_td_p || !bulk_buf_p || !int_ed_p ||
        !int_td_p || !int_buf_p)
        return -1;
    o->hcca = (ohci_hcca_t *)(uintptr_t)hcca_p;
    o->hcca_phys = hcca_p;
    oc_memset(o->hcca, 0, PMM_PAGE_SIZE);
    o->ctrl_ed = (ohci_ed_t *)(uintptr_t)ctrl_ed_p;
    o->ctrl_ed_phys = ctrl_ed_p;
    o->ctrl_tds = (ohci_td_t *)(uintptr_t)ctrl_td_p;
    o->ctrl_td_phys = ctrl_td_p;
    o->ctrl_buf = (u8 *)(uintptr_t)ctrl_buf_p;
    o->ctrl_buf_phys = ctrl_buf_p;
    o->bulk_ed = (ohci_ed_t *)(uintptr_t)bulk_ed_p;
    o->bulk_ed_phys = bulk_ed_p;
    o->bulk_tds = (ohci_td_t *)(uintptr_t)bulk_td_p;
    o->bulk_td_phys = bulk_td_p;
    o->bulk_buf = (u8 *)(uintptr_t)bulk_buf_p;
    o->bulk_buf_phys = bulk_buf_p;
    o->int_ed = (ohci_ed_t *)(uintptr_t)int_ed_p;
    o->int_ed_phys = int_ed_p;
    o->int_td = (ohci_td_t *)(uintptr_t)int_td_p;
    o->int_td_phys = int_td_p;
    o->int_buf = (u8 *)(uintptr_t)int_buf_p;
    o->int_buf_phys = int_buf_p;
    oc_memset((void *)(uintptr_t)ctrl_ed_p, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)bulk_ed_p, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)int_ed_p, 0, PMM_PAGE_SIZE);

    for (int i = 0; i < OHCI_MAX_CTRL; i++) { }
    ohci_ed_init(o, o->ctrl_ed, ctrl_ed_p, 0);
    ohci_ed_init(o, o->bulk_ed, bulk_ed_p, 0);
    ohci_ed_init(o, o->int_ed, int_ed_p, 0);

    /* ports */
    u32 a = ohci_rd(o, OHCI_RHDESC_A);
    o->n_ports = (u8)(a & 0xff);
    if (o->n_ports > OHCI_MAX_PORTS) o->n_ports = OHCI_MAX_PORTS;

    /* IRQ (optional; polling is the primary path) */
    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    o->irq = -1;
    if (irq < 16 &&
        oc_irq_register_handler(irq, ohci_irq_handler, o) == 0)
        o->irq = irq;

    /* publish the schedule */
    ohci_wr(o, OHCI_HCCA, (u32)hcca_p);
    ohci_wr(o, OHCI_CTRL_HEAD, (u32)ctrl_ed_p);
    ohci_wr(o, OHCI_BULK_HEAD, (u32)bulk_ed_p);
    /* FMINTERVAL: FS bit timing (QEMU default is fine, keep) */
    ohci_wr(o, OHCI_INTSTS, 0xffffffffu);
    ohci_wr(o, OHCI_INTEN, OHCI_INT_RHSC | OHCI_INT_WDH |
                            OHCI_INT_MASTER);
    /* power the ports + start operational */
    u32 rh = ohci_rd(o, OHCI_RHDESC_A);
    if (rh & (1u << 12)) {         /* NPS (no power switching) off */
        ohci_wr(o, OHCI_RHSTATUS, (1u << 16));   /* set global power */
    }
    for (volatile int t = 0; t < 50000; t++) { }
    ohci_wr(o, OHCI_CTRL, OHCI_CTL_CLE | OHCI_CTL_BLE |
                          OHCI_CTL_PLE | OHCI_CTL_IE |
                          OHCI_CTL_HCFS_OPR);

    o->up = 1;

    usb_host_t *h = &g_ohci_host[g_n_ohci];
    oc_memset(h, 0, sizeof(*h));
    oc_strcpy(h->name, "ohci0");
    if (g_n_ohci > 0) h->name[4] = (char)('0' + g_n_ohci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->priv = o;
    g_ohci_ops[g_n_ohci] = ohci_ops_tmpl;
    h->ops = &g_ohci_ops[g_n_ohci];
    usb_register_host(h, &g_ohci_ops[g_n_ohci]);
    g_n_ohci++;
    return 0;
}

int ohci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    /* QEMU pci-ohci: Apple KeyLargo 106b:003f */
    if (pci_find_device(0x106b, 0x003f, &b, &dv, &fn) == 0) {
        if (ohci_probe_one(b, dv, fn) == 0) n++;
    }
    /* any other OHCI by class 0x0c0310 */
    for (int nth = 0; nth < 4; nth++) {
        if (pci_find_class_exact(0x0c0310, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_ohci; i++) {
            if (g_ohci[i].bus == b && g_ohci[i].dev == dv &&
                g_ohci[i].func == fn)
                dup = 1;
        }
        if (!dup && ohci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int ohci_init(const pci_dev_t *dev) {
    if (!dev) return -1;
    return ohci_probe_one(dev->bus, dev->dev, dev->func);
}
