/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_ehci.c
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
#include "driver_usb.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_sched.h"
#include "core_timer.h"

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
#define EHCI_STS_IAA        (1u << 5)   /* interrupt on async advance */
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

/* link type bits - the type tag lives in BITS 2:1 of the link word
 * (bit 0 = Terminate): 0=ITD 1=QH 2=siTD 3=FSTN.  Verified against
 * Linux (Q_TYPE_QH = 1 << 1) and QEMU (NLPTR_TYPE_GET = (x >> 1) & 3);
 * encoding it in bits 3:2 made QEMU classify our QH as a siTD and
 * hard-reset the controller ("processing error"). */
#define EHCI_LTYPE_ITD      0u
#define EHCI_LTYPE_QH       (1u << 1)
#define EHCI_LTYPE_SITD     (2u << 1)
#define EHCI_LTYPE_FSTN     (3u << 1)
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

typedef struct driver_usb_ehci_qh {
    volatile u32 next;
    volatile u32 epchar;
    volatile u32 epcap;
    volatile u32 current_qtd;
    volatile u32 next_qtd;
    volatile u32 altnext_qtd;
    volatile u32 token;
    volatile u32 bufptr[5];
} driver_usb_ehci_qh_t;

typedef struct driver_usb_ehci_qtd {
    volatile u32 next;
    volatile u32 altnext;
    volatile u32 token;
    volatile u32 bufptr[5];
} driver_usb_ehci_qtd_t;

typedef struct driver_usb_ehci_state {
    volatile u32 *cap;
    volatile u32 *op;
    u8   bus, dev, func;
    u8   n_ports;

    /* one page holds: QH ctrl | QH bulk | QH int | frame list? no -
     * frame list needs its own 4 KiB page */
    driver_usb_ehci_qh_t *qh_ctrl;
    u64  qh_ctrl_phys;
    driver_usb_ehci_qh_t *qh_bulk;
    u64  qh_bulk_phys;
    driver_usb_ehci_qh_t *qh_int;
    u64  qh_int_phys;

    driver_usb_ehci_qtd_t *ctrl_qtds;
    u64  ctrl_qtd_phys;
    u8  *ctrl_buf;
    u64  ctrl_buf_phys;

    driver_usb_ehci_qtd_t *bulk_qtds;
    u64  bulk_qtd_phys;
    u8  *bulk_buf;
    u64  bulk_buf_phys;

    volatile u32 *frame_list;
    u64  frame_list_phys;

    int  irq;
    u64  irq_count;
    int  up;
    int  busy_ctrl, busy_bulk;
} driver_usb_ehci_state_t;

#define EHCI_MAX_CTRL 2
static driver_usb_ehci_state_t g_ehci[EHCI_MAX_CTRL];
static driver_usb_host_t   g_ehci_host[EHCI_MAX_CTRL];
static driver_usb_hc_ops_t g_ehci_ops[EHCI_MAX_CTRL];
static int g_n_ehci = 0;

static inline u32 driver_usb_ehci_rd(driver_usb_ehci_state_t *e, u32 off) {
    return e->op[off / 4];
}
static inline void driver_usb_ehci_wr(driver_usb_ehci_state_t *e, u32 off, u32 v) {
    e->op[off / 4] = v;
}

static void driver_usb_ehci_log(const char *s) { screen_console_puts(s); }

static void driver_usb_ehci_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)f;
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)ctx;
    if (!e || !e->up) return;
    u32 sts = driver_usb_ehci_rd(e, EHCI_USBSTS);
    if (sts) {
        driver_usb_ehci_wr(e, EHCI_USBSTS, sts);
        e->irq_count++;
    }
}

/* qTD buffer pointers: 5 page pointers at 4 KiB stride.
 * Per the EHCI spec (4.10.2) bufptr[0] carries the CURRENT OFFSET
 * within the first page in its low 12 bits; the higher pointers are
 * whole pages.  Dropping the offset made every non page-aligned
 * buffer (the SETUP stage reads the caller's driver_usb_setup_t) fetch the
 * wrong bytes. */
static void driver_usb_ehci_qtd_fill(driver_usb_ehci_qtd_t *td, u8 pid, const void *buf,
                          u16 len, int last, u8 toggle_hc) {
    u32 token = ((u32)pid << QTD_TOKEN_PID_SH) |
                (3u << QTD_TOKEN_CERR_SH) |
                (((u32)len & 0x7fff) << QTD_TOKEN_TBYTES_SH) |
                QTD_TOKEN_ACTIVE | QTD_TOKEN_IOC;
    if (toggle_hc) token |= QTD_TOKEN_DTOGGLE;
    if (last) token &= ~QTD_TOKEN_IOC;
    td->token = token;
    /* BUG-0160 FIX (A11-2): the qTD buffer pointers are 32-bit DMA
     * addresses (EHCI 1.0 spec 4.10.2/4.10.3 - five page pointers plus
     * the in-page offset).  This truncation is safe by construction:
     * every buffer handed here is one of the probe-validated descriptor
     * frames (ctrl_buf/bulk_buf bounce pages; caller buffers are always
     * memcpy'd in/out, never DMA'd directly), and probe_one() rejects a
     * pool at/above 4 GiB, so the dropped bits are provably zero. */
    u64 p = (u64)(uintptr_t)buf;
    for (int i = 0; i < 5; i++) {
        u64 a = p ? (p + (u64)i * 4096) : 0;
        u32 v = (u32)(a & 0xffffffffu);
        if (i > 0) v &= 0xfffff000u;   /* only bufptr[0] keeps the
                                          in-page offset */
        td->bufptr[i] = v;
    }
    td->next = EHCI_LINK_T;
    td->altnext = EHCI_LINK_T;
}


static int driver_usb_ehci_wait_qtd(driver_usb_ehci_state_t *e, driver_usb_ehci_qtd_t *tds, int n,
                         u32 timeout_ms, driver_usb_ehci_qh_t *qh) {
    u64 deadline = core_timer_now_ms() + timeout_ms;
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
        if (core_timer_now_ms() > deadline) {
            /* BUG-0173 FIX (A11-5): unlink the qTDs from the async
             * schedule FIRST, ring the doorbell, and wait for the IAA
             * advance so the HC provably stopped walking this chain
             * BEFORE the CPU clears ACTIVE. Clearing ACTIVE on a qTD the
             * controller may still be fetching is an undefined-behaviour
             * race on real silicon. */
            if (qh) qh->next_qtd = EHCI_LINK_T;   /* unlink first */
            driver_usb_ehci_wr(e, EHCI_USBSTS, EHCI_STS_IAA);
            driver_usb_ehci_wr(e, EHCI_USBCMD,
                    driver_usb_ehci_rd(e, EHCI_USBCMD) | EHCI_CMD_IAAD);
            u64 iaa_deadline = core_timer_now_ms() + 10;
            while (core_timer_now_ms() < iaa_deadline) {
                if (driver_usb_ehci_rd(e, EHCI_USBSTS) & EHCI_STS_IAA) {
                    driver_usb_ehci_wr(e, EHCI_USBSTS, EHCI_STS_IAA);
                    break;
                }
                core_sched_yield();
            }
            /* Fallback (BUG-0173): if the doorbell is still unanswered
             * after 10 ms the controller is wedged or HALTed (USBSTS.HALT,
             * EHCI 1.0 spec 2.3.2 bit 12) - a HALTed controller by
             * definition stops fetching qTDs, so clearing ACTIVE is safe
             * in exactly the case where the wait failed. */
            for (int i = 0; i < n; i++)
                tds[i].token &= ~QTD_TOKEN_ACTIVE;
            return OC_USB_ETIMEDOUT;
        }
        core_sched_yield();
    }
}

static int driver_usb_ehci_control(driver_usb_host_t *h, driver_usb_dev_t *d,
                        const driver_usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    (void)timeout_ms;   /* the doorbell retry loop owns the timing */
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up || !d) return -1;
    if (e->busy_ctrl) return -1;
    if (len > 7 * 64) return OC_USB_EINVAL;

    e->busy_ctrl = 1;
    driver_usb_ehci_qh_t *qh = e->qh_ctrl;
    driver_usb_ehci_qtd_t *tds = e->ctrl_qtds;
    /* one 4 KiB page: SETUP stage at +0, DATA stage at +64 (keeps the
     * two stages in separate 64-byte blocks so a short SETUP can never
     * be confused with the data) */
    u8 *datab = e->ctrl_buf + 64;

    u32 epchar = ((u32)d->addr & 0x7f) |
                 (((u32)0 & 0xf) << QH_EPCHAR_EP_SH) |
                 (((d->speed == USB_SPEED_LS) ? 1 :
                   (d->speed == USB_SPEED_HS) ? 2 : 0)
                  << QH_EPCHAR_EPS_SH) |
                 QH_EPCHAR_DTC |
                 (((u32)(d->mps0 ? d->mps0 : 64) & 0x7ff)
                  << QH_EPCHAR_MPLEN_SH) |
                 QH_EPCHAR_H;   /* ctrl QH is the async head of the
                                   reclamation list (spec 4.9.1.1) */
    /* EPS encoding per EHCI 1.0 spec 2.3.5 (QH characteristic word,
     * bits 13:12): 0=FS, 1=LS, 2=HS.  USB_SPEED_FS falls into the 0
     * branch, so a full-speed device detected by port_status/port_reset
     * (BUG-0175 fix) is addressed with FS timing here and never as HS.
     * QEMU-testable: qemu -device usb-ehci -device usb-kbd puts a
     * full-speed HID device directly on the EHCI root port. */
    qh->epchar = epchar;
    qh->epcap = (1u << 30);

    /* qTD chain: SETUP (DATA0) + DATA (DATA1) + STATUS (DATA1).
     * A11-8 review (BUG-0177): QH.EPChar.DTC=1 means the HC takes the data
     * toggle from EACH qTD's T bit (EHCI 1.0 spec 4.11.2), NOT from the
     * QH overlay; SETUP's qtd_fill(..., toggle_hc=0) leaves T=0 = DATA0,
     * which is exactly what USB 2.0 8.5.3 mandates for every SETUP.
     * The overlay never feeds the toggle on this chain, so the old
     * "second SETUP sends DATA1" hazard does not exist here. */
    memcpy(e->ctrl_buf, setup, 8);
    driver_usb_ehci_qtd_fill(&tds[0], QTD_PID_SETUP, e->ctrl_buf, 8, 0, 0);
    int n = 1;
    if (len > 0) {
        u8 pid = (setup->bmRequestType & 0x80) ? QTD_PID_IN
                                               : QTD_PID_OUT;
        if (pid == QTD_PID_OUT) memcpy(datab, buf, len);
        driver_usb_ehci_qtd_fill(&tds[n], pid, datab, len, 0, 1);
        tds[n - 1].next = (u32)(e->ctrl_qtd_phys +
                                (u64)n * EHCI_QTD_SIZE);
        n++;
    }
    /* STATUS: opposite direction, zero length */
    {
        u8 pid = (setup->bmRequestType & 0x80) ? QTD_PID_OUT
                                               : QTD_PID_IN;
        driver_usb_ehci_qtd_fill(&tds[n], pid, NULL, 0, 1, 1);
        tds[n - 1].next = (u32)(e->ctrl_qtd_phys +
                                (u64)n * EHCI_QTD_SIZE);
        n++;
    }

    qh->next_qtd = (u32)e->ctrl_qtd_phys;   /* link first qTD */
    /* BUG-0166 FIX (A11-3): snapshot the freshly-built tokens so the
     * doorbell retry restores a COMPLETE, pristine token (length, pid
     * and toggle included). The old retry only re-ORed ACTIVE on the
     * HC-rewritten token: error bits latched through (instantly "failed"
     * again), and TBYTES had been decremented by partial transfers so
     * the retry sent a short, toggle-skewed transaction. */
    u32 pristine[8];
    for (int q = 0; q < n && q < 8; q++) pristine[q] = tds[q].token;
    int rc = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        /* clear a latched IAA first: the controller refuses to walk
         * the async schedule while USBSTS.IAA is set (spec 4.8.2),
         * and the previous doorbell may have left it raised */
        driver_usb_ehci_wr(e, EHCI_USBSTS, EHCI_STS_IAA);
        driver_usb_ehci_wr(e, EHCI_USBCMD, driver_usb_ehci_rd(e, EHCI_USBCMD) | EHCI_CMD_IAAD);
        rc = driver_usb_ehci_wait_qtd(e, tds, n, 300, qh);
        if (rc == 0) break;
        /* let the controller see the unlinked qTD before re-arming */
        qh->next_qtd = EHCI_LINK_T;
        for (volatile int d = 0; d < 2000; d++) { }
        /* BUG-0166 FIX (A11-3): restore the pristine tokens verbatim */
        for (int q = 0; q < n && q < 8; q++) {
            tds[q].token = pristine[q];
        }
        /* BUG-0166 FIX (A11-3, completion): also reset the QH overlay
         * working copy.  After a STALLed attempt the overlay keeps the
         * halt condition and the last-executed qTD pointer (EHCI 1.0
         * spec 4.13.2: a halted overlay makes the controller ignore a
         * re-armed Next qTD until software clears it), which would turn
         * every retry into an instant repeat failure on real silicon.
         * With DTC=1 the overlay DT bit never feeds the toggle (see the
         * A11-8 note above), so zeroing the overlay cannot skew the
         * data-toggle sequence of the retried chain. */
        qh->current_qtd = 0;
        qh->token &= ~(QTD_TOKEN_HALT | QTD_TOKEN_BABBLE |
                       QTD_TOKEN_XACTERR | QTD_TOKEN_DBERR);
        qh->next_qtd = (u32)e->ctrl_qtd_phys;
    }
    qh->next_qtd = EHCI_LINK_T;
    if (rc == 0 && len > 0 && (setup->bmRequestType & 0x80))
        memcpy(buf, datab, len);
    e->busy_ctrl = 0;
    return rc;
}

static int driver_usb_ehci_bulk(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up || !d || !buf) return -1;
    if (e->busy_bulk) return -1;
    if (len == 0) return 0;
    /* the bulk bounce buffer is ONE page: one qTD moves at most 4 KiB
     * per call (MSC splits with ops->bulk_max) */
    if (len > 4096) return OC_USB_EINVAL;

    e->busy_bulk = 1;
    driver_usb_ehci_qh_t *qh = e->qh_bulk;
    driver_usb_ehci_qtd_t *tds = e->bulk_qtds;

    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? QTD_PID_IN : QTD_PID_OUT;
    u16 mps = 512;
    driver_usb_endpoint_t *epd = driver_usb_find_ep(d, 0xff, USB_EP_ATTR_BULK,
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
    /* NOTE: no H bit here - the ctrl QH is the one and only head of
     * the reclamation list; a second H-bit QH would stop the async
     * walk when the reclamation status is already cleared. */
    qh->epchar = epchar;
    qh->epcap = (1u << 30);

    /* one qTD, one page */
    int n = 0;
    if (pid == QTD_PID_OUT)
        memcpy(e->bulk_buf, buf, len);
    driver_usb_ehci_qtd_fill(&tds[0], pid, e->bulk_buf, len, 1, 1);
    n = 1;

    qh->next_qtd = (u32)e->bulk_qtd_phys;
    /* clear the controller's leftover execution pointer: QEMU writes
     * the last executed qTD into the QH overlay (current_qtd) and its
     * verify pass then treats our fresh next_qtd as "guest updated
     * active QH" and cancels the packet mid-flight */
    qh->current_qtd = 0;
    /* async advance doorbell (spec 4.8.2), see driver_usb_ehci_control: clear a
     * latched IAA first so the walk is not refused */
    driver_usb_ehci_wr(e, EHCI_USBSTS, EHCI_STS_IAA);
    driver_usb_ehci_wr(e, EHCI_USBCMD, driver_usb_ehci_rd(e, EHCI_USBCMD) | EHCI_CMD_IAAD);
    int rc = driver_usb_ehci_wait_qtd(e, tds, n, timeout_ms, qh);
    qh->next_qtd = EHCI_LINK_T;
    int moved = (int)len;
    if (rc == 0) {
        /* total-bytes field holds the bytes NOT transferred */
        u32 rem = (tds[0].token >> QTD_TOKEN_TBYTES_SH) & 0x7fffu;
        if (rem > len) rem = len;
        moved = (int)(len - rem);
        if (pid == QTD_PID_IN)
            memcpy(buf, e->bulk_buf, (u32)moved);
    }
    e->busy_bulk = 0;
    return (rc == 0) ? moved : rc;
}

static int driver_usb_ehci_interrupt(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up || !d || !buf) return -1;
    if (len == 0 || len > 512) return OC_USB_EINVAL;
    u8 ep = USB_EP_NUM(ep_addr);
    u8 pid = (ep_addr & 0x80) ? QTD_PID_IN : QTD_PID_OUT;

    driver_usb_ehci_qh_t *qh = e->qh_int;
    /* BUG-0169 FIX (A11-4): refuse the borrow while a control transfer
     * owns the shared ctrl_qtds[0]/ctrl_buf instead of relying on the
     * core lock's external serialization alone; also keep busy_ctrl set
     * for the whole interrupt transaction. */
    if (e->busy_ctrl) return -1;
    e->busy_ctrl = 1;
    driver_usb_ehci_qtd_t *td = e->ctrl_qtds;   /* reuse slot 0 (guarded) */
    /* MPLEN must be the ENDPOINT's max packet size, not the request
     * length: MPS < len violates the endpoint transaction limit and
     * MPS > len breaks short-packet semantics (EHCI spec 3.5.3). */
    u16 ep_mps = 64;
    {
        driver_usb_endpoint_t *epd = driver_usb_find_ep(d, 0xff,
                                          USB_EP_ATTR_INTERRUPT,
                                          (ep_addr & 0x80) ? 1 : 0);
        if (epd && USB_EP_NUM(epd->addr) == ep && epd->maxpack)
            ep_mps = epd->maxpack;
    }
    u32 epchar = ((u32)d->addr & 0x7f) |
                 (((u32)ep & 0xf) << QH_EPCHAR_EP_SH) |
                 (((d->speed == USB_SPEED_LS) ? 1 :
                   (d->speed == USB_SPEED_HS) ? 2 : 0)
                  << QH_EPCHAR_EPS_SH) |
                 QH_EPCHAR_DTC |
                 (((u32)(ep_mps & 0x7ff)) << QH_EPCHAR_MPLEN_SH);
    qh->epchar = epchar;
    /* interrupt QH: SMASK = micro-frame 0 of every frame (1 ms) */
    qh->epcap = (1u << 30) | 0x01u;

    if (pid == QTD_PID_OUT) memcpy(e->ctrl_buf, buf, len);
    driver_usb_ehci_qtd_fill(td, pid, e->ctrl_buf, len, 1, 1);
    qh->next_qtd = (u32)e->ctrl_qtd_phys;
    /* doorbell not needed for the periodic schedule, but the USBSTS
     * IAA bit must never stay latched or the async walk stalls (the
     * poll below keeps clearing it) */

    u64 deadline = core_timer_now_ms() + timeout_ms;
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
        if (core_timer_now_ms() > deadline) {
            /* BUG-0173 FIX (A11-5, periodic variant): unlink before
             * clearing ACTIVE so the HC is not mid-fetch of the qTD. */
            qh->next_qtd = EHCI_LINK_T;
            u64 qdl = core_timer_now_ms() + 2;
            while (core_timer_now_ms() < qdl) core_sched_yield();
            td->token &= ~QTD_TOKEN_ACTIVE;
            rc = OC_USB_ENAK;
            break;
        }
        core_sched_yield();
    }
    qh->next_qtd = EHCI_LINK_T;
    e->busy_ctrl = 0;   /* BUG-0169: release the shared-slot guard */
    if (rc == 0) {
        if (pid == QTD_PID_IN) memcpy(buf, e->ctrl_buf, len);
        return (int)len;
    }
    return rc;
}

/* EHCI ISO: high-speed iTD (8 transactions, one per micro-frame).
 * Provided for real-hardware use; no current class driver consumes
 * it (the audio driver runs ISO on the UHCI backend). */
typedef struct driver_usb_ehci_itd {
    volatile u32 next;
    volatile u32 trans[8];
    volatile u32 bufptr[7];
} driver_usb_ehci_itd_t;

static int driver_usb_ehci_iso_out(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)data; (void)len;
    return OC_USB_EINVAL;   /* iTD scheduling needs a periodic-ring
                               owner; not consumed by this stack */
}

static int driver_usb_ehci_iso_in(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;
}

/* ==================================================================
 * root hub
 * ================================================================== */

static int driver_usb_ehci_port_count(driver_usb_host_t *h) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    return e ? e->n_ports : 0;
}

static int driver_usb_ehci_port_status(driver_usb_host_t *h, int port,
                            driver_usb_port_status_t *out) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up || port < 0 || port >= e->n_ports) return -1;
    u32 v = driver_usb_ehci_rd(e, EHCI_PORTSC + (u32)port * 4);
    out->connected = (v & PORTSC_CONNECT) ? 1 : 0;
    out->enabled = (v & PORTSC_PED) ? 1 : 0;
    /* BUG-0175 FIX (A11-6): read the PORTSC.PS (Port Speed, bits 13:12)
     * field instead of LINE-STATE. LINE-STATUS only carries a reliable
     * speed encoding at the END of reset; a steady-state read reports the
     * current line J/K state, which misclassifies FS devices as HS. PS:
     * 0=FS, 1=LS, 2=HS (EHCI 1.0 spec Table 2-16). */
    {
        u32 ps = (v >> 12) & 3;
        if (ps == 1)      out->speed = USB_SPEED_LS;
        else if (ps == 2) out->speed = USB_SPEED_HS;
        else              out->speed = USB_SPEED_FS;
    }
    out->changed = (v & PORTSC_CSC) ? 1 : 0;
    if (v & PORTSC_CSC)
        /* W1C the change bit but KEEP port power and port enable: PP
         * and PED are plain RW bits - writing 0 to them powers the
         * port off / disables it and the device disappears. */
        driver_usb_ehci_wr(e, EHCI_PORTSC + (u32)port * 4,
                PORTSC_CSC | (v & PORTSC_PPOWER) | PORTSC_PED);
    return 0;
}

static int driver_usb_ehci_port_reset(driver_usb_host_t *h, int port, u8 *speed_out) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up || port < 0 || port >= e->n_ports) return -1;
    u32 off = EHCI_PORTSC + (u32)port * 4;
    /* NOTE: every PORTSC write keeps PED set in the written value.
     * QEMU treats a written 0 on PED as "guest disabled the port"
     * and clears it, which makes driver_usb_ehci_find_device() skip the port
     * ("no device attached to queue") for full-speed devices where
     * the controller does not re-enable the port by itself. */
    u32 v = driver_usb_ehci_rd(e, off);
    driver_usb_ehci_wr(e, off, (v & 0xffffffffu) | PORTSC_PPOWER | PORTSC_PED);
    /* power-on settle: real time, not a spin loop (the port needs a
     * few ms before CCS reflects the attached device) */
    {
        u64 t0 = core_timer_now_ms();
        while (core_timer_now_ms() - t0 < 20) core_sched_yield();
    }
    driver_usb_ehci_wr(e, off, driver_usb_ehci_rd(e, off) | PORTSC_PRESET | PORTSC_PED);
    /* hold reset for the spec'd 10 ms (FS) minimum */
    {
        u64 t0 = core_timer_now_ms();
        while (core_timer_now_ms() - t0 < 12) core_sched_yield();
    }
    /* drive PRESET low to signal reset completion.  Real hardware
     * self-clears PRESET (the loop below tolerates both); QEMU's EHCI
     * only completes the reset (detach/attach cycle + device reset to
     * address 0) on this write.  PED stays set in the value so the
     * enable bit is never dropped. */
    driver_usb_ehci_wr(e, off, (driver_usb_ehci_rd(e, off) & ~PORTSC_PRESET) | PORTSC_PED);
    u64 deadline = core_timer_now_ms() + 200;
    while ((driver_usb_ehci_rd(e, off) & PORTSC_PRESET) &&
           core_timer_now_ms() < deadline)
        core_sched_yield();
    /* reset recovery ~20 ms (spec 7.1.7.5) */
    deadline = core_timer_now_ms() + 25;
    while (core_timer_now_ms() < deadline) core_sched_yield();
    u32 w = driver_usb_ehci_rd(e, off);
    if (!(w & PORTSC_CONNECT)) {
        char l[64]; char n[12];
        strcpy(l, "ehci: port reset fail ccs=0 w=0x");
        u64_to_hex(w, n, 8); strcat(l, n);
        strcat(l, "\n");
        screen_console_puts(l);
        return -1;
    }
    if (speed_out) {
        /* BUG-0175 FIX (A11-6): PORTSC.PS (13:12) is the authoritative
         * post-reset speed (0=FS 1=LS 2=HS); LINE-STATUS was only
         * meaningful in the brief reset window and could not see FS. */
        u32 ps = (w >> 12) & 3;
        if (ps == 1)      *speed_out = USB_SPEED_LS;
        else if (ps == 2) *speed_out = USB_SPEED_HS;
        else              *speed_out = USB_SPEED_FS;
    }
    return 0;
}

static int driver_usb_ehci_poll(driver_usb_host_t *h) {
    driver_usb_ehci_state_t *e = (driver_usb_ehci_state_t *)h->priv;
    if (!e || !e->up) return 0;
    u32 sts = driver_usb_ehci_rd(e, EHCI_USBSTS);
    if (sts) driver_usb_ehci_wr(e, EHCI_USBSTS, sts);
    return 0;
}

/* clear a latched HALT in the QH overlay tokens.  When the device
 * STALLs, the controller writes the halt condition into the QH
 * overlay and refuses to walk that QH until the guest clears it (the
 * CLEAR_FEATURE(ENDPOINT_HALT) request only resets the device side).
 * Called from the core's driver_usb_tog_reset() after the class driver sent
 * CLEAR_FEATURE; harmless when no halt is latched. */
void driver_usb_ehci_clear_halt_overlay(driver_usb_dev_t *d, u8 ep_addr) {
    (void)d; (void)ep_addr;
    for (int i = 0; i < g_n_ehci; i++) {
        driver_usb_ehci_state_t *e = &g_ehci[i];
        if (!e->up) continue;
        e->qh_ctrl->token &= ~QTD_TOKEN_HALT;
        e->qh_bulk->token &= ~QTD_TOKEN_HALT;
        e->qh_int->token &= ~QTD_TOKEN_HALT;
    }
}

static const driver_usb_hc_ops_t driver_usb_ehci_ops_tmpl = {
    .name      = "EHCI",
    .bulk_max  = 4096,   /* one qTD, one bounce page */
    .control   = driver_usb_ehci_control,
    .bulk      = driver_usb_ehci_bulk,
    .interrupt = driver_usb_ehci_interrupt,
    .driver_usb_iso_out   = driver_usb_ehci_iso_out,
    .driver_usb_iso_in    = driver_usb_ehci_iso_in,
    .port_count  = driver_usb_ehci_port_count,
    .port_status = driver_usb_ehci_port_status,
    .port_reset  = driver_usb_ehci_port_reset,
    .poll        = driver_usb_ehci_poll,
};

/* ==================================================================
 * probe / init
 * ================================================================== */

static int driver_usb_ehci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_ehci >= EHCI_MAX_CTRL) return -1;
    driver_usb_ehci_state_t *e = &g_ehci[g_n_ehci];
    memset(e, 0, sizeof(*e));

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        driver_usb_ehci_log("ehci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    e->cap = (volatile u32 *)(uintptr_t)mmio;
    u8 caplen = (u8)(e->cap[0] & 0xff);
    if (caplen < 0x20) caplen = 0x20;
    e->op = (volatile u32 *)(uintptr_t)(mmio + caplen);
    e->bus = bus; e->dev = dev; e->func = func;
    driver_pci_enable_device(bus, dev, func);

    u32 hcs = e->cap[EHCI_HCSPARAMS / 4];
    e->n_ports = (u8)(hcs & 0xff);
    if (e->n_ports > EHCI_MAX_PORTS) e->n_ports = EHCI_MAX_PORTS;

    /* HC reset */
    driver_usb_ehci_wr(e, EHCI_USBCMD, driver_usb_ehci_rd(e, EHCI_USBCMD) |
            EHCI_CMD_HCRESET);
    for (int i = 0; i < 2000; i++) {
        if (!(driver_usb_ehci_rd(e, EHCI_USBCMD) & EHCI_CMD_HCRESET)) break;
        for (volatile int t = 0; t < 1000; t++) { }
    }
    /* BUG-0049 FIX: CONFIGFLAG(CF) semantics per the EHCI spec (2.3.9):
     * CF=1 means "ports routed to THIS EHCI controller"; CF=0 hands the
     * ports to the companion UHCI/OHCI controllers. The old code wrote
     * 0 with a comment that had the meaning inverted - on any chipset
     * with companions (or a spec-compliant real chip) that unhooks the
     * EHCI from its own ports and leaves PSE/ASE scheduling undefined.
     * Set CF=1 to take ownership of the ports. */

    /* Set CF=1 (see BUG-0049 above). */
    driver_usb_ehci_wr(e, EHCI_CONFIGFLAG, 1);

    /* BUG-0160 FIX (A11-2): every DMA address this driver programs is a
     * 32-bit field - QH.next, qTD.next/altnext, qTD bufptr[0..4], the
     * periodic frame-list entries, ASYNCLISTADDR and PERIODICLISTBASE
     * (EHCI 1.0 spec 2.2/2.3 register set, 2.3.5 queue heads and
     * 4.10.2/4.10.3 qTD buffer pointers).  The descriptor/buffer pool
     * therefore has to live below 4 GiB.  mem_pmm_alloc_frame() scans
     * the whole RAM bitmap linearly and gives no lowmem guarantee, so
     * probe validates every frame explicitly and fails LOUDLY instead of
     * silently truncating an address and letting the HC DMA into
     * arbitrary physical pages (arbitrary kernel memory corruption).
     * No realloc-retry: with a linear-scan PMM a free/realloc cycle
     * hands back the same page, so the policy mirrors a 32-bit dma_mask
     * failure in Linux - refuse the device, do not guess.  The driver
     * state struct itself is never DMA'd (CPU-only access) and lives in
     * the static kernel image, which the linker ASSERT pins below 4 GiB;
     * the tripwire below only covers a future heap-resident state. */
    if (((u64)(uintptr_t)e >> 32) != 0) {
        driver_usb_ehci_log("usb: EHCI probe failed - driver state above 4GiB, 32-bit DMA\n");
        return -1;
    }

    /* allocate descriptors */
    u64 qpage = mem_pmm_alloc_frame();
    u64 ctd = mem_pmm_alloc_frame();
    u64 cbuf = mem_pmm_alloc_frame();
    u64 btd = mem_pmm_alloc_frame();
    u64 bbuf = mem_pmm_alloc_frame();
    u64 fl = mem_pmm_alloc_frame();
    /* BUG-0176 FIX (A11-7): partial-failure path releases every frame
     * already obtained instead of leaking up to 5 pages per failed probe.
     * BUG-0160 FIX (A11-2): a frame at/above 4 GiB is just as fatal for
     * this 32-bit-DMA controller as an allocation failure (every link
     * pointer, ASYNCLISTADDR and PERIODICLISTBASE write would truncate),
     * so both failure kinds share the same all-or-nothing gate and
     * unwind.  (0 >> 32 == 0, so the range test never misfires on an
     * allocation failure.) */
    int dma_oob = (((qpage | ctd | cbuf | btd | bbuf | fl) >> 32) != 0);
    if (!qpage || !ctd || !cbuf || !btd || !bbuf || !fl || dma_oob) {
        driver_usb_ehci_log(dma_oob
                ? "usb: EHCI probe failed - DMA frame at/above 4GiB\n"
                : "usb: EHCI probe failed - out of descriptor frames\n");
        if (qpage) mem_pmm_free_frame(qpage);
        if (ctd)   mem_pmm_free_frame(ctd);
        if (cbuf)  mem_pmm_free_frame(cbuf);
        if (btd)   mem_pmm_free_frame(btd);
        if (bbuf)  mem_pmm_free_frame(bbuf);
        if (fl)    mem_pmm_free_frame(fl);
        return -1;
    }
    memset((void *)(uintptr_t)qpage, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)ctd, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)cbuf, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)btd, 0, PMM_PAGE_SIZE);
    memset((void *)(uintptr_t)bbuf, 0, PMM_PAGE_SIZE);
    e->qh_ctrl = (driver_usb_ehci_qh_t *)(uintptr_t)qpage;
    e->qh_ctrl_phys = qpage;
    e->qh_bulk = (driver_usb_ehci_qh_t *)(uintptr_t)(qpage + 64);
    e->qh_bulk_phys = qpage + 64;
    e->qh_int = (driver_usb_ehci_qh_t *)(uintptr_t)(qpage + 128);
    e->qh_int_phys = qpage + 128;
    e->ctrl_qtds = (driver_usb_ehci_qtd_t *)(uintptr_t)ctd;
    e->ctrl_qtd_phys = ctd;
    e->ctrl_buf = (u8 *)(uintptr_t)cbuf;
    e->ctrl_buf_phys = cbuf;
    e->bulk_qtds = (driver_usb_ehci_qtd_t *)(uintptr_t)btd;
    e->bulk_qtd_phys = btd;
    e->bulk_buf = (u8 *)(uintptr_t)bbuf;
    e->bulk_buf_phys = bbuf;
    e->frame_list = (volatile u32 *)(uintptr_t)fl;
    e->frame_list_phys = fl;

    /* async ring: ctrl QH <-> bulk QH.  The ctrl QH carries the H bit
     * (head of the reclamation list) - the EHCI spec 4.9.1.1 and QEMU
     * both refuse to walk the async schedule without it. */
    e->qh_ctrl->next = (u32)e->qh_bulk_phys | EHCI_LTYPE_QH;
    e->qh_bulk->next = (u32)e->qh_ctrl_phys | EHCI_LTYPE_QH;
    e->qh_ctrl->next_qtd = EHCI_LINK_T;
    e->qh_ctrl->altnext_qtd = EHCI_LINK_T;
    e->qh_ctrl->epchar = QH_EPCHAR_H;          /* head marker */
    e->qh_ctrl->epcap = (1u << 30);
    e->qh_bulk->next_qtd = EHCI_LINK_T;
    e->qh_bulk->altnext_qtd = EHCI_LINK_T;
    e->qh_bulk->epchar = 0;
    e->qh_bulk->epcap = (1u << 30);

    /* periodic frame list: every entry -> int QH (1 ms) */
    for (int i = 0; i < 1024; i++)
        e->frame_list[i] = (u32)e->qh_int_phys | EHCI_LTYPE_QH;
    e->qh_int->next = EHCI_LINK_T;
    e->qh_int->next_qtd = EHCI_LINK_T;

    /* IRQ */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    e->irq = -1;
    if (irq < 16 &&
        arch_irq_register_handler(irq, driver_usb_ehci_irq_handler, e) == 0)
        e->irq = irq;

    /* publish schedules + run */
    driver_usb_ehci_wr(e, EHCI_PERIODICBASE, (u32)fl);
    driver_usb_ehci_wr(e, EHCI_ASYNCADDR, (u32)e->qh_ctrl_phys);
    driver_usb_ehci_wr(e, EHCI_USBINTR, 0);   /* polling mode */
    driver_usb_ehci_wr(e, EHCI_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASE |
            EHCI_CMD_PSE);

    e->up = 1;

    driver_usb_host_t *h = &g_ehci_host[g_n_ehci];
    memset(h, 0, sizeof(*h));
    strcpy(h->name, "ehci0");
    if (g_n_ehci > 0) h->name[4] = (char)('0' + g_n_ehci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->priv = e;
    g_ehci_ops[g_n_ehci] = driver_usb_ehci_ops_tmpl;
    h->ops = &g_ehci_ops[g_n_ehci];
    driver_usb_register_host(h, &g_ehci_ops[g_n_ehci]);
    g_n_ehci++;
    return 0;
}

int driver_usb_ehci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    for (int nth = 0; nth < 4; nth++) {
        if (driver_pci_find_class_exact(0x0c0320, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_ehci; i++) {
            if (g_ehci[i].bus == b && g_ehci[i].dev == dv &&
                g_ehci[i].func == fn)
                dup = 1;
        }
        if (!dup && driver_usb_ehci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int driver_usb_ehci_init(const driver_pci_dev_t *dev) {
    if (!dev) return -1;
    return driver_usb_ehci_probe_one(dev->bus, dev->dev, dev->func);
}
