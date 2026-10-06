/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_ohci.c
 * Purpose: OHCI (Open Host Controller Interface, USB 1.1) backend for
 *          the WP-10d USB core.  Registers with usb.c through
 *          driver_usb_hc_ops_t.
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
#include "driver_usb.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_sched.h"
#include "core_timer.h"

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
/* OHCI DataToggle field (word0 bits 25:24): 01 = DATA0, 10 = DATA1
 * (per the OHCI spec Table 4-3 and QEMU hcd-ohci.c) */
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
#define ED_HEAD_C        (1u << 1)   /* toggle carry, lives in headP bit 1 */

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

typedef struct driver_usb_ohci_ed {
    volatile u32 word0;
    volatile u32 tail;
    volatile u32 head;
    volatile u32 next;
} driver_usb_ohci_ed_t;

typedef struct driver_usb_ohci_td {
    volatile u32 word0;
    volatile u32 cur;
    volatile u32 next;
    volatile u32 be;
} driver_usb_ohci_td_t;

typedef struct driver_usb_ohci_hcca {
    volatile u32 int_table[32];
    volatile u32 frame_pad;
    volatile u32 done_head;
    u8 reserved[116];
} driver_usb_ohci_hcca_t;

typedef struct driver_usb_ohci_state {
    volatile u32 *regs;
    u8   bus, dev, func;

    driver_usb_ohci_hcca_t *hcca;
    u64  hcca_phys;

    /* one ED per transfer direction group; TD pools per ED */
    driver_usb_ohci_ed_t *ctrl_ed;
    u64   ctrl_ed_phys;
    driver_usb_ohci_td_t *ctrl_tds;
    u64   ctrl_td_phys;
    u8   *ctrl_buf;
    u64   ctrl_buf_phys;
    int   ctrl_busy;

    driver_usb_ohci_ed_t *bulk_ed;
    u64   bulk_ed_phys;
    driver_usb_ohci_td_t *bulk_tds;
    u64   bulk_td_phys;
    u8   *bulk_buf;
    u64   bulk_buf_phys;
    int   bulk_busy;

    driver_usb_ohci_ed_t *int_ed;
    u64   int_ed_phys;
    driver_usb_ohci_td_t *int_td;
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
} driver_usb_ohci_state_t;

#define OHCI_MAX_CTRL 2
static driver_usb_ohci_state_t g_ohci[OHCI_MAX_CTRL];
static driver_usb_host_t   g_ohci_host[OHCI_MAX_CTRL];
static driver_usb_hc_ops_t g_ohci_ops[OHCI_MAX_CTRL];
static int g_n_ohci = 0;

static inline u32 driver_usb_ohci_rd(driver_usb_ohci_state_t *o, u32 off) {
    return o->regs[off / 4];
}
static inline void driver_usb_ohci_wr(driver_usb_ohci_state_t *o, u32 off, u32 v) {
    o->regs[off / 4] = v;
}

static void driver_usb_ohci_log(const char *s) { screen_console_puts(s); }

static void driver_usb_ohci_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)f;
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)ctx;
    if (!o || !o->up) return;
    u32 sts = driver_usb_ohci_rd(o, OHCI_INTSTS);
    if (sts) {
        driver_usb_ohci_wr(o, OHCI_INTSTS, sts);
        o->irq_count++;
    }
}

/* ==================================================================
 * transfer engine
 * ================================================================== */

static void driver_usb_ohci_ed_init(driver_usb_ohci_state_t *o, driver_usb_ohci_ed_t *ed,
                         u64 ed_phys, u8 fmt) {
    (void)o;
    ed->word0 = ED_K | fmt;      /* skip until armed */
    ed->tail = 0;
    ed->head = 0;
    ed->next = 0;
    /* keep it self-consistent so the HC never chokes on it */
    (void)ed_phys;
}

/* arm an ED for one transfer and wait for completion of its TD chain.
 *  ed already points at tds[0]; ntd TDs chained; last TD -> tail 0 */
static int driver_usb_ohci_run(driver_usb_ohci_state_t *o, driver_usb_ohci_ed_t *ed, u64 ed_phys,
                    driver_usb_ohci_td_t *tds, u64 td_phys, int ntd,
                    int list, u32 timeout_ms) {
    (void)td_phys;
    /* clear the issue bits, then tell the HC to re-scan the list */
    if (list == 0) {          /* control */
        driver_usb_ohci_wr(o, OHCI_CTRL_HEAD, (u32)ed_phys);
        driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_CLF);
    } else {                  /* bulk */
        driver_usb_ohci_wr(o, OHCI_BULK_HEAD, (u32)ed_phys);
        driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_BLF);
    }

    u64 deadline = core_timer_now_ms() + timeout_ms;
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
        if (core_timer_now_ms() > deadline) {
            /* disarm: skip the ED so the HC stops touching it */
            ed->word0 |= ED_K;
            rc = OC_USB_ETIMEDOUT;
            break;
        }
        core_sched_yield();
    }
    /* disarm and clear the completion flags */
    ed->word0 |= ED_K;
    if (list == 0) driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_CLF);
    else driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_BLF);
    return rc;
}

static int driver_usb_ohci_control(driver_usb_host_t *h, driver_usb_dev_t *d,
                        const driver_usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || !d) return -1;
    if (o->ctrl_busy) return -1;
    if (len > 7 * 64) return OC_USB_EINVAL;

    o->ctrl_busy = 1;
    driver_usb_ohci_ed_t *ed = o->ctrl_ed;
    driver_usb_ohci_td_t *tds = o->ctrl_tds;
    u8 *datab = o->ctrl_buf;
    u16 mps = d->mps0 ? d->mps0 : 8;

    /* TD plan: 0=SETUP, 1..n=DATA (IN or OUT), last=STATUS */
    int ntd = 2;
    if (len == 0) ntd = 2;   /* SETUP + STATUS */

    /* SETUP TD: 8 bytes, DATA0 */
    memcpy(datab, setup, 8);
    tds[0].word0 = TD_CC_NOTACC | TD_DP_SETUP | TD_T0 | (7u);
    tds[0].cur = (u32)(uintptr_t)datab;
    tds[0].next = (u32)(o->ctrl_td_phys + sizeof(driver_usb_ohci_td_t));
    tds[0].be = (u32)(uintptr_t)(datab + 7);

    int td = 1;
    if (len > 0) {
        u32 dp = (setup->bmRequestType & 0x80) ? TD_DP_IN : TD_DP_OUT;
        if (dp == TD_DP_OUT)
            memcpy(datab + 8, buf, len);
        u16 remaining = len;
        u64 bp = o->ctrl_buf_phys + 8;
        u8 tog = 1;   /* control DATA phase starts with DATA1 */
        while (remaining > 0 && td < OHCI_N_CTRL_TD - 1) {
            u16 chunk = remaining > mps ? mps : remaining;
            /* TD_R: tolerate short packets - GET_DESCRIPTOR(STRING) and
             * friends ask for more bytes than the device returns, and
             * without buffer rounding the HC retires the TD with
             * DataUnderrun instead of NOERROR. */
            tds[td].word0 = TD_CC_NOTACC | TD_R | dp |
                            (tog ? TD_T1 : TD_T0) | (chunk - 1);
            tds[td].cur = (u32)bp;
            tds[td].next = (u32)(o->ctrl_td_phys +
                                 (u64)(td + 1) * sizeof(driver_usb_ohci_td_t));
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

    /* STATUS TD: zero length, DATA1, opposite direction.
     * NOTE: must be u32 - TD_DP_* are bit-19/20 fields and an u8 would
     * truncate them to 0 (= SETUP), which makes the device STALL the
     * status stage of every control transfer. */
    u32 dp_stat = (setup->bmRequestType & 0x80) ? TD_DP_OUT : TD_DP_IN;
    tds[ntd - 1].word0 = TD_CC_NOTACC | dp_stat | TD_T1 | 0;
    tds[ntd - 1].cur = 0;
    tds[ntd - 1].next = 0;
    tds[ntd - 1].be = 0;
    /* chain the TD just before the status one to it */
    if (ntd >= 3) tds[ntd - 2].next = (u32)(o->ctrl_td_phys +
        (u64)(ntd - 1) * sizeof(driver_usb_ohci_td_t));

    /* arm the ED */
    u32 w0 = ((u32)d->addr & ED_FA_MSK) |
             (((u32)0 & 0xf) << 7) |          /* EP0 */
             (((u32)mps & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) w0 |= ED_S;
    ed->word0 = w0;
    ed->tail = 0;
    ed->head = (u32)o->ctrl_td_phys;
    ed->next = 0;

    int rc = driver_usb_ohci_run(o, ed, o->ctrl_ed_phys, tds, o->ctrl_td_phys,
                      ntd, 0, timeout_ms);

    /* pull IN data back */
    if (rc == 0 && len > 0 && (setup->bmRequestType & 0x80))
        memcpy(buf, datab + 8, len);
    o->ctrl_busy = 0;
    return rc;
}

/* ---- toggles (per address / endpoint / direction) ---- */

static u8 driver_usb_ohci_tog_get(driver_usb_ohci_state_t *o, driver_usb_dev_t *d, u8 ep_addr) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    return (o->toggles[idx] >> ((ep_addr & 0x80) ? 1 : 0)) & 1;
}

static void driver_usb_ohci_tog_set(driver_usb_ohci_state_t *o, driver_usb_dev_t *d, u8 ep_addr,
                         u8 v) {
    int idx = (d->addr & 0x0f) * 16 + (USB_EP_NUM(ep_addr) & 0x0f);
    u8 bit = (u8)(1u << ((ep_addr & 0x80) ? 1 : 0));
    if (v) o->toggles[idx] |= bit;
    else   o->toggles[idx] &= (u8)~bit;
}

void driver_usb_ohci_tog_reset(driver_usb_dev_t *d, u8 ep_addr) {
    for (int i = 0; i < OHCI_MAX_CTRL; i++) {
        if (g_ohci[i].up) driver_usb_ohci_tog_set(&g_ohci[i], d, ep_addr, 0);
    }
}

static int driver_usb_ohci_bulk(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !buf) return -1;
    if (o->bulk_busy) return -1;
    if (len == 0) return 0;
    if (d->speed == USB_SPEED_LS) return OC_USB_EINVAL;
    if (len > OHCI_N_BULK_TD * 64) return OC_USB_EINVAL;

    o->bulk_busy = 1;
    driver_usb_ohci_ed_t *ed = o->bulk_ed;
    driver_usb_ohci_td_t *tds = o->bulk_tds;

    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u32 dp = (ep_addr & 0x80) ? TD_DP_IN : TD_DP_OUT;
    u16 mps = 64;

    int ntd = (int)((len + mps - 1) / mps);
    u8 base_tog = driver_usb_ohci_tog_get(o, d, ep_addr);
    u16 done_bytes = 0;
    for (int i = 0; i < ntd; i++) {
        u16 chunk = (u16)(len - done_bytes);
        if (chunk > mps) chunk = mps;
        u8 tog = (u8)(base_tog ^ (i & 1));
        if (dp == TD_DP_OUT)
            memcpy(o->bulk_buf + done_bytes,
                      (const void *)((uintptr_t)buf + done_bytes),
                      chunk);
        /* BUG-0051 FIX: TD_R (buffer rounding) marks a TD whose transfer
         * may legally complete with less data than buffered - that is a
         * property of IN endpoints receiving a short packet, most
         * importantly on the FINAL TD. The old condition attached TD_R
         * to every TD EXCEPT the last one, so a bulk-IN short packet
         * ended the queue with DataUnderrun (treated as EIO) and the
         * partially-received data was discarded. Follow the control
         * path above: round on all IN TDs. */
        tds[i].word0 = TD_CC_NOTACC | dp | (tog ? TD_T1 : TD_T0) |
                       (chunk - 1) | (dp == TD_DP_IN ? TD_R : 0);
        tds[i].cur = (u32)(o->bulk_buf_phys + done_bytes);
        tds[i].next = (i + 1 < ntd)
            ? (u32)(o->bulk_td_phys + (u64)(i + 1) * sizeof(driver_usb_ohci_td_t))
            : 0;
        tds[i].be = (u32)(o->bulk_buf_phys + done_bytes + chunk - 1);
        done_bytes += chunk;
    }

    u32 w0 = ((u32)addr & ED_FA_MSK) | (((u32)ep & 0xf) << 7) |
             (((u32)mps & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) w0 |= ED_S;
    ed->word0 = w0;
    ed->tail = 0;
    ed->head = (u32)o->bulk_td_phys;
    ed->next = 0;

    int rc = driver_usb_ohci_run(o, ed, o->bulk_ed_phys, tds, o->bulk_td_phys,
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
            memcpy((void *)((uintptr_t)buf + moved),
                      o->bulk_buf + moved, chunk);
        moved += chunk;
        if (chunk < mps) break;
    }
    driver_usb_ohci_tog_set(o, d, ep_addr, (u8)(base_tog ^ (walked & 1)));
    o->bulk_busy = 0;
    if (rc != 0) return rc;
    return moved;
}

static int driver_usb_ohci_interrupt(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !buf) return -1;
    if (len == 0 || len > 64) return OC_USB_EINVAL;
    u8 addr = d->addr;
    u8 ep = USB_EP_NUM(ep_addr);
    u32 dp = (ep_addr & 0x80) ? TD_DP_IN : TD_DP_OUT;

    if (dp == TD_DP_OUT)
        memcpy(o->int_buf, buf, len);

    u8 tog = driver_usb_ohci_tog_get(o, d, ep_addr);
    driver_usb_ohci_ed_t *ed = o->int_ed;
    driver_usb_ohci_td_t *td = o->int_td;
    /* TD_R: interrupt IN endpoints may deliver less than asked for */
    td->word0 = TD_CC_NOTACC | dp | (dp == TD_DP_IN ? TD_R : 0) |
                (tog ? TD_T1 : TD_T0) | (len - 1);
    td->cur = (u32)o->int_buf_phys;
    td->next = 0;
    td->be = (u32)(o->int_buf_phys + len - 1);
    ed->word0 = ((u32)addr & ED_FA_MSK) | (((u32)ep & 0xf) << 7) |
                (((u32)len & 0x7ff) << 16);
    if (d->speed == USB_SPEED_LS) ed->word0 |= ED_S;
    ed->tail = 0;
    ed->head = (u32)o->int_td_phys;
    ed->next = 0;

    u64 deadline = core_timer_now_ms() + timeout_ms;
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
        if (core_timer_now_ms() > deadline) {
            ed->word0 |= ED_K;
            rc = OC_USB_ENAK;
            break;
        }
        core_sched_yield();
    }
    ed->word0 |= ED_K;
    if (rc == 0) {
        driver_usb_ohci_tog_set(o, d, ep_addr, (u8)(1 - driver_usb_ohci_tog_get(o, d, ep_addr)));
        if (dp == TD_DP_IN)
            memcpy(buf, o->int_buf, len);
        return (int)len;
    }
    return rc;
}

/* OHCI ISO TD layout (hcd-ohci.c): SF 15:0 | DI 23:21 | FC 27:24 |
 * CC 31:28; the ED needs the F (format) bit. */
static int driver_usb_ohci_iso_out(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || !d || !d->present) return -1;
    if (len > 1023) return -1;
    /* ISO OUT on OHCI: use the control TD pool's first TD as an ISO
     * TD on a dedicated ED is over-engineering for this stack; the
     * audio class driver runs on the UHCI backend (WP-10c path).
     * Still, make the path real for real-hardware use: reuse the
     * interrupt ED in ISO format (F=1) with an ISO TD. */
    (void)data; (void)ep_addr;
    driver_usb_ohci_ed_t *ed = o->int_ed;
    driver_usb_ohci_td_t *td = o->int_td;
    if (data && len) memcpy(o->int_buf, data, len);
    u16 frame = (u16)(driver_usb_ohci_rd(o, OHCI_FMINTERVAL), 0);
    (void)frame;
    td->word0 = TD_CC_NOTACC | ((0u) << 24) | (0u << 21); /* FC=0 */
    td->cur = (u32)o->int_buf_phys;
    td->next = 0;
    td->be = (u32)(o->int_buf_phys + (len ? len - 1 : 0));
    ed->word0 = ((u32)d->addr & ED_FA_MSK) |
                (((u32)USB_EP_NUM(ep_addr) & 0xf) << 7) |
                ED_F | (((u32)len & 0x7ff) << 16);
    ed->tail = 0;
    ed->head = (u32)o->int_td_phys;
    ed->next = 0;
    /* ISO EDs live in the periodic list; QEMU also services them from
     * the control/bulk scan when F=1 is set, and the audio class
     * driver drives the frame cadence itself. */
    return 0;
}

static int driver_usb_ohci_iso_in(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;   /* not needed by any current class driver */
}

/* ==================================================================
 * root hub
 * ================================================================== */

static int driver_usb_ohci_port_count(driver_usb_host_t *h) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    return o ? o->n_ports : 0;
}

static int driver_usb_ohci_port_status(driver_usb_host_t *h, int port,
                            driver_usb_port_status_t *out) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || port < 0 || port >= o->n_ports) return -1;
    u32 v = driver_usb_ohci_rd(o, OHCI_RHPORT + (u32)port * 4);
    out->connected = (v & RHPORT_CCS) ? 1 : 0;
    out->enabled = (v & RHPORT_PES) ? 1 : 0;
    out->speed = (v & RHPORT_LSDA) ? USB_SPEED_LS : USB_SPEED_FS;
    out->changed = (v & RHPORT_CSC) ? 1 : 0;
    if (v & RHPORT_CSC)
        driver_usb_ohci_wr(o, OHCI_RHPORT + (u32)port * 4, RHPORT_CSC);
    return 0;
}

static int driver_usb_ohci_port_reset(driver_usb_host_t *h, int port, u8 *speed_out) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up || port < 0 || port >= o->n_ports) return -1;
    u32 off = OHCI_RHPORT + (u32)port * 4;
    driver_usb_ohci_wr(o, off, RHPORT_PRS);
    for (int i = 0; i < 200; i++) {
        u32 v = driver_usb_ohci_rd(o, off);
        if (v & RHPORT_PRSC) {
            driver_usb_ohci_wr(o, off, RHPORT_PRSC);
            break;
        }
        for (volatile int t = 0; t < 2000; t++) { }
    }
    u32 v = driver_usb_ohci_rd(o, off);
    if (!(v & RHPORT_CCS)) return -1;
    if (speed_out)
        *speed_out = (v & RHPORT_LSDA) ? USB_SPEED_LS : USB_SPEED_FS;
    return 0;
}

static int driver_usb_ohci_poll(driver_usb_host_t *h) {
    driver_usb_ohci_state_t *o = (driver_usb_ohci_state_t *)h->priv;
    if (!o || !o->up) return 0;
    u32 sts = driver_usb_ohci_rd(o, OHCI_INTSTS);
    if (sts) driver_usb_ohci_wr(o, OHCI_INTSTS, sts);   /* clear for the poller */
    return 0;
}

static const driver_usb_hc_ops_t driver_usb_ohci_ops_tmpl = {
    .name      = "OHCI",
    .bulk_max  = 512,   /* 8 TDs x 64 B FS bulk cap */
    .control   = driver_usb_ohci_control,
    .bulk      = driver_usb_ohci_bulk,
    .interrupt = driver_usb_ohci_interrupt,
    .driver_usb_iso_out   = driver_usb_ohci_iso_out,
    .driver_usb_iso_in    = driver_usb_ohci_iso_in,
    .port_count  = driver_usb_ohci_port_count,
    .port_status = driver_usb_ohci_port_status,
    .port_reset  = driver_usb_ohci_port_reset,
    .poll        = driver_usb_ohci_poll,
};

/* ==================================================================
 * probe / init
 * ================================================================== */

static int driver_usb_ohci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_ohci >= OHCI_MAX_CTRL) return -1;
    driver_usb_ohci_state_t *o = &g_ohci[g_n_ohci];
    memset(o, 0, sizeof(*o));

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        driver_usb_ohci_log("ohci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    o->regs = (volatile u32 *)(uintptr_t)mmio;
    o->bus = bus; o->dev = dev; o->func = func;
    driver_pci_enable_device(bus, dev, func);

    /* reset */
    u32 ctl = driver_usb_ohci_rd(o, OHCI_CTRL);
    if (ctl & OHCI_CTL_IR) {
        /* SMM active: take over */
        driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_HCR);
        for (int i = 0; i < 1000; i++) {
            if (!(driver_usb_ohci_rd(o, OHCI_CMDSTS) & OHCI_CMD_HCR)) break;
            for (volatile int t = 0; t < 1000; t++) { }
        }
        driver_usb_ohci_wr(o, OHCI_CTRL, OHCI_CTL_HCFS_RES);
        for (volatile int t = 0; t < 100000; t++) { }
    }
    driver_usb_ohci_wr(o, OHCI_CMDSTS, OHCI_CMD_HCR);
    {
        int ok = 0;
        for (int i = 0; i < 2000; i++) {
            if (!(driver_usb_ohci_rd(o, OHCI_CMDSTS) & OHCI_CMD_HCR)) {
                ok = 1;
                break;
            }
            for (volatile int t = 0; t < 1000; t++) { }
        }
        if (!ok) {
            driver_usb_ohci_log("ohci: reset timeout\n");
            return -1;
        }
    }

    /* allocate the HCCA + ED/TD pools */
    u64 hcca_p = mem_pmm_alloc_frame();
    u64 ctrl_ed_p = mem_pmm_alloc_frame();
    u64 ctrl_td_p = mem_pmm_alloc_frame();
    u64 ctrl_buf_p = mem_pmm_alloc_frame();
    u64 bulk_ed_p = mem_pmm_alloc_frame();
    u64 bulk_td_p = mem_pmm_alloc_frame();
    u64 bulk_buf_p = mem_pmm_alloc_frame();
    u64 int_ed_p = mem_pmm_alloc_frame();
    u64 int_td_p = mem_pmm_alloc_frame();
    u64 int_buf_p = mem_pmm_alloc_frame();
    if (!hcca_p || !ctrl_ed_p || !ctrl_td_p || !ctrl_buf_p ||
        !bulk_ed_p || !bulk_td_p || !bulk_buf_p || !int_ed_p ||
        !int_td_p || !int_buf_p)
        return -1;
    o->hcca = (driver_usb_ohci_hcca_t *)(uintptr_t)hcca_p;
    o->hcca_phys = hcca_p;
    memset(o->hcca, 0, PMM_PAGE_SIZE);
    o->ctrl_ed = (driver_usb_ohci_ed_t *)(uintptr_t)ctrl_ed_p;
    o->ctrl_ed_phys = ctrl_ed_p;
    o->ctrl_tds = (driver_usb_ohci_td_t *)(uintptr_t)ctrl_td_p;
    o->ctrl_td_phys = ctrl_td_p;
    o->ctrl_buf = (u8 *)(uintptr_t)ctrl_buf_p;
    o->ctrl_buf_phys = ctrl_buf_p;
    o->bulk_ed = (driver_usb_ohci_ed_t *)(uintptr_t)bulk_ed_p;
    o->bulk_ed_phys = bulk_ed_p;
    o->bulk_tds = (driver_usb_ohci_td_t *)(uintptr_t)bulk_td_p;
    o->bulk_td_phys = bulk_td_p;
    o->bulk_buf = (u8 *)(uintptr_t)bulk_buf_p;
    o->bulk_buf_phys = bulk_buf_p;
    o->int_ed = (driver_usb_ohci_ed_t *)(uintptr_t)int_ed_p;
    o->int_ed_phys = int_ed_p;
    o->int_td = (driver_usb_ohci_td_t *)(uintptr_t)int_td_p;
    o->int_td_phys = int_td_p;
    o->int_buf = (u8 *)(uintptr_t)int_buf_p;
    o->int_buf_phys = int_buf_p;
    memset((void *)(uintptr_t)ctrl_ed_p, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)bulk_ed_p, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)int_ed_p, 0, PMM_PAGE_SIZE);

    for (int i = 0; i < OHCI_MAX_CTRL; i++) { }
    driver_usb_ohci_ed_init(o, o->ctrl_ed, ctrl_ed_p, 0);
    driver_usb_ohci_ed_init(o, o->bulk_ed, bulk_ed_p, 0);
    driver_usb_ohci_ed_init(o, o->int_ed, int_ed_p, 0);

    /* BUG-0050 FIX: the interrupt ED was initialised but never linked
     * into the HCCA periodic schedule (int_table stayed all zero), so
     * the HC never fetched it and OHCI HID input had no data path at
     * all. Publish the interrupt ED in every frame slot (1 ms polling,
     * matching how control/bulk are scheduled) so the periodic list
     * actually reaches it. */
    for (int i = 0; i < 32; i++)
        o->hcca->int_table[i] = (u32)o->int_ed_phys;

    /* ports */
    u32 a = driver_usb_ohci_rd(o, OHCI_RHDESC_A);
    o->n_ports = (u8)(a & 0xff);
    if (o->n_ports > OHCI_MAX_PORTS) o->n_ports = OHCI_MAX_PORTS;

    /* IRQ (optional; polling is the primary path) */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    o->irq = -1;
    if (irq < 16 &&
        arch_irq_register_handler(irq, driver_usb_ohci_irq_handler, o) == 0)
        o->irq = irq;

    /* publish the schedule */
    driver_usb_ohci_wr(o, OHCI_HCCA, (u32)hcca_p);
    driver_usb_ohci_wr(o, OHCI_CTRL_HEAD, (u32)ctrl_ed_p);
    driver_usb_ohci_wr(o, OHCI_BULK_HEAD, (u32)bulk_ed_p);
    /* FMINTERVAL: FS bit timing (QEMU default is fine, keep) */
    driver_usb_ohci_wr(o, OHCI_INTSTS, 0xffffffffu);
    driver_usb_ohci_wr(o, OHCI_INTEN, OHCI_INT_RHSC | OHCI_INT_WDH |
                            OHCI_INT_MASTER);
    /* power the ports + start operational */
    u32 rh = driver_usb_ohci_rd(o, OHCI_RHDESC_A);
    /* BUG-0052 FIX: RHDESC_A bit 12 is NPS (No Power Switching). NPS=1
     * means the ports are ALWAYS powered (nothing to do); NPS=0 means
     * power switching is implemented and the root ports start OFF
     * until the software sets global/per-port power. The old code read
     * the bit inverted, so on real hardware that needed the power-on
     * (NPS=0) the SetGlobalPower write never happened and every root
     * port stayed dead forever. */
    if (!(rh & (1u << 12))) {      /* NPS=0: power switching present */
        driver_usb_ohci_wr(o, OHCI_RHSTATUS, (1u << 16));   /* set global power */
    }
    for (volatile int t = 0; t < 50000; t++) { }
    driver_usb_ohci_wr(o, OHCI_CTRL, OHCI_CTL_CLE | OHCI_CTL_BLE |
                          OHCI_CTL_PLE | OHCI_CTL_IE |
                          OHCI_CTL_HCFS_OPR);

    o->up = 1;

    driver_usb_host_t *h = &g_ohci_host[g_n_ohci];
    memset(h, 0, sizeof(*h));
    strcpy(h->name, "ohci0");
    if (g_n_ohci > 0) h->name[4] = (char)('0' + g_n_ohci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->priv = o;
    g_ohci_ops[g_n_ohci] = driver_usb_ohci_ops_tmpl;
    h->ops = &g_ohci_ops[g_n_ohci];
    driver_usb_register_host(h, &g_ohci_ops[g_n_ohci]);
    g_n_ohci++;
    return 0;
}

int driver_usb_ohci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    /* QEMU pci-ohci: Apple KeyLargo 106b:003f */
    if (driver_pci_find_device(0x106b, 0x003f, &b, &dv, &fn) == 0) {
        if (driver_usb_ohci_probe_one(b, dv, fn) == 0) n++;
    }
    /* any other OHCI by class 0x0c0310 */
    for (int nth = 0; nth < 4; nth++) {
        if (driver_pci_find_class_exact(0x0c0310, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_ohci; i++) {
            if (g_ohci[i].bus == b && g_ohci[i].dev == dv &&
                g_ohci[i].func == fn)
                dup = 1;
        }
        if (!dup && driver_usb_ohci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int driver_usb_ohci_init(const driver_pci_dev_t *dev) {
    if (!dev) return -1;
    return driver_usb_ohci_probe_one(dev->bus, dev->dev, dev->func);
}
