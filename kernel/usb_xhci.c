/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_xhci.c
 * Purpose: XHCI (eXtensible Host Controller Interface, USB 3.x/2.0/1.1)
 *          backend for the WP-10d USB core.
 *
 * XHCI is the controller every modern machine uses (and the one QEMU
 * models as "qemu-xhci" (1b36:000d) / "nec-usb-xhci" (1033:0194), both
 * PCI class 0x0c0330).  Unlike UHCI/OHCI/EHCI it manages device
 * addresses itself: the host software talks in "slots" and rings, and
 * SET_ADDRESS happens inside the Address Device command.
 *
 * Register layout (BAR0, 64-bit MMIO):
 *   CAP   0x00 CAPLENGTH (byte)     0x04 HCSPARAMS1 (MaxPorts 31:24,
 *         MaxSlots 27:0)            0x08 HCSPARAMS2 (MaxScratchpad
 *         31:27 (+SPBHC bit26), MaxERST 7:4)
 *         0x10 HCCPARAMS1 (AC64 bit0, CSZ bit2)
 *         0x14 DBOFF                0x18 RTSOFF
 *   OP    +0x00 USBCMD (RS bit0, HCRST bit1)   +0x04 USBSTS (HCH bit0,
 *         CNR bit11)   +0x18 CRCR (64)   +0x30 DCBAAP (64)
 *         +0x38 CONFIG (MaxSlotsEn 31:16)
 *         +0x400 PORTSC[n]  (n = 1..MaxPorts)
 *   RUN   RTSOFF+0x20+0x20*n: IMAN 0x00  IMOD 0x04  ERSTSZ 0x08
 *         ERSTBA 0x10 (64)  ERDP 0x18 (64, bit3 = Event Handler Busy)
 *   DB    DBOFF + 4*target: target 0 = command ring; device EPs:
 *         EP0 = 1, epN OUT = 2*N, epN IN = 2*N+1
 *
 * PORTSC bits: CCS 0, PED 1, OCA 2, PR 4, PLS 5:8, PP 9, Speed 13:10
 *   (1=FS 2=LS 3=HS 4=SS), CSC 17, PEC 18, WRC 19, OCC 20, PRC 21,
 *   PLC 22 (change bits are write-1-to-clear).
 *
 * TRB types (spec Table 6-91): 1 NORMAL  2 SETUP_STAGE  3 DATA_STAGE
 *   4 STATUS_STAGE  5 ISOCH  6 LINK  7 EVENT_DATA  9 ENABLE_SLOT
 *   10 DISABLE_SLOT  11 ADDRESS_DEVICE (BSR bit9)  12 CONFIGURE_EP
 *   13 EVALUATE_CONTEXT  14 RESET_ENDPOINT  16 SET_TR_DEQ  23 NO_OP;
 *   events: 32 TRANSFER  33 COMMAND_COMPLETION  34 PORT_STATUS_CHANGE.
 *   Completion codes: 1 success  6 stall  13 short packet  ...
 *
 * Rings: every ring is 64 TRBs with a LINK TRB (Toggle Cycle set, and
 * its cycle bit re-written on every wrap) in the last entry.  One
 * transfer = one event (IOC on the last TRB of the chain only).
 * Device bring-up follows the spec sequence: Enable Slot ->
 * Address Device BSR=1 -> (core reads desc8 through EP0) ->
 * Address Device BSR=0 with the real MPS0 (intercepted from the
 * core's SET_ADDRESS) -> lazy Configure Endpoint when a class driver
 * first uses a non-default endpoint.
 */
#include "usb.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"

/* ---- capability / operational registers ---- */
#define XHCI_CAPLENGTH     0x00
#define XHCI_HCSPARAMS1    0x04
#define XHCI_HCSPARAMS2    0x08
#define XHCI_HCCPARAMS1    0x10
#define XHCI_DBOFF         0x14
#define XHCI_RTSOFF        0x18

#define XHCI_USBCMD        0x00
#define XHCI_CMD_RS        (1u << 0)
#define XHCI_CMD_HCRST     (1u << 1)
#define XHCI_USBSTS        0x04
#define XHCI_STS_HCH       (1u << 0)
#define XHCI_STS_CNR       (1u << 11)
#define XHCI_CRCR          0x18
#define XHCI_DCBAAP        0x30
#define XHCI_CONFIG        0x38
#define XHCI_PORTSC_BASE   0x400

/* PORTSC bits */
#define P_CCS              (1u << 0)
#define P_PED              (1u << 1)
#define P_PR               (1u << 4)
#define P_PP               (1u << 9)
#define P_SPEED_SH         10
#define P_SPEED_MSK        (0xfu << 10)
#define P_CSC              (1u << 17)
#define P_PEC              (1u << 18)
#define P_WRC              (1u << 19)
#define P_OCC              (1u << 20)
#define P_PRC              (1u << 21)
#define P_PLC              (1u << 22)
#define P_CEC              (1u << 23)
#define P_CHANGES          (P_CSC | P_PEC | P_WRC | P_OCC | P_PRC | \
                            P_PLC | P_CEC)

/* runtime registers (per interrupter n) */
#define XHCI_IMAN(n)       (0x20u + (u32)(n) * 0x20u)
#define XHCI_ERSTSZ(n)     (XHCI_IMAN(n) + 0x08u)
#define XHCI_ERSTBA(n)     (XHCI_IMAN(n) + 0x10u)
#define XHCI_ERDP(n)       (XHCI_IMAN(n) + 0x18u)

/* TRB types */
#define TRB_NORMAL         1u
#define TRB_SETUP_STAGE    2u
#define TRB_DATA_STAGE     3u
#define TRB_STATUS_STAGE   4u
#define TRB_ISOCH          5u
#define TRB_LINK           6u
#define TRB_ENABLE_SLOT    9u
#define TRB_DISABLE_SLOT   10u
#define TRB_ADDRESS_DEV    11u
#define TRB_CONFIG_EP      12u
#define TRB_EVALUATE_CTX   13u
#define TRB_RESET_EP       14u
#define TRB_SET_DEQ        16u
#define TRB_NOOP_CMD       23u
#define TRB_EV_TRANSFER    32u
#define TRB_EV_CMD_DONE    33u
#define TRB_EV_PORT_CHANGE 34u

/* completion codes */
#define CC_SUCCESS         1u
#define CC_STALL           6u
#define CC_SHORT_PACKET    13u

/* TRB control bits */
#define TRB_C              (1u << 0)
#define TRB_TC             (1u << 1)   /* LINK: toggle cycle */
#define TRB_IOC            (1u << 5)
#define TRB_IDT            (1u << 6)   /* immediate data (SETUP) */
#define TRB_CH             (1u << 7)   /* chained with the next TRB */
#define TRB_BSR            (1u << 9)
#define TRB_DIR_IN         (1u << 16)
#define TRB_TYPE_SH        10
#define TRB_SLOT_SH        24

/* ring sizes (TRBs per ring must be a power of two) */
#define XHCI_RING_N        64
#define XHCI_MAX_SLOTS     16          /* = USB_MAX_DEVICES */
#define XHCI_MAX_PORTS     8
#define XHCI_MAX_ERING     5           /* 0 = ep0, 1..4 = doorbell 2..5 */

typedef volatile u32 xtrb_t[4];     /* one TRB: param lo/hi, status, ctrl */

typedef struct xhci_ring {
    xtrb_t *trb;             /* 16-byte entries */
    u64     phys;
    int     enq;             /* next index to fill */
    u8      cycle;           /* cycle bit for the next filled TRB */
    u8      configured;      /* CONFIG_EP done */
} xhci_ring_t;

typedef struct xhci_slot {
    int        used;
    int        hw_slot;      /* xhci slot id (1..MaxSlots) */
    usb_dev_t *dev;          /* core device this slot serves */
    int        addressed;    /* Address Device BSR=0 done */
    u32        mps0_prog;    /* ep0 MPS we last programmed */
    u8        *out_ctx;      /* output device context (page) */
    u64        out_ctx_phys;
    u8        *in_ctx;       /* input control + device context (page) */
    u64        in_ctx_phys;
    u8        *ring_page;    /* ep0 ring + 4 data rings (4 KiB) */
    u64        ring_page_phys;
    xhci_ring_t ep[XHCI_MAX_ERING];
    u8        *bulk_buf;     /* 4 KiB bulk/iso bounce */
    u8        *ctl_buf;      /* control data bounce (512 B) */
    u8        *int_buf;      /* interrupt latch buffers (4 x 64 B) */
    volatile int latch_valid[4];
    volatile int latch_len[4];
    volatile int int_pending[4];   /* int TRB in flight (per ring) */
    u64        int_trb_phys[4];
    u8         halted[6];    /* endpoint halted (stall seen) */
} xhci_slot_t;

typedef struct xhci_state {
    volatile u32 *cap;
    volatile u32 *op;
    volatile u32 *run;       /* runtime base */
    volatile u32 *db;        /* doorbell base */
    u8   bus, dev, func;
    u8   n_ports;
    u32  max_slots;
    u32  ctx_stride;         /* 32 or 64 (HCCPARAMS1.CSZ) */

    xhci_slot_t slots[XHCI_MAX_SLOTS];

    /* command ring */
    xtrb_t *cmd;
    u64     cmd_phys;
    int     cmd_enq;
    u8      cmd_cycle;
    u32     cmd_slot;        /* slot id returned by the last command */

    /* event ring + segment table */
    xtrb_t *ev;
    u64     ev_phys;
    u32    *erst;
    u64     erst_phys;
    int     ev_deq;
    u8      ev_cycle;

    /* scratchpad */
    u64  scratch_arr_phys;

    u8   *dcbaa;             /* device context base address array */
    u64  dcbaa_phys;

    int  irq;
    u64  irq_count;
    int  up;
    int  cmd_rc;             /* completion code of the last command */
} xhci_state_t;

#define XHCI_MAX_CTRL 2
static xhci_state_t g_xhci[XHCI_MAX_CTRL];
static usb_host_t   g_xhci_host[XHCI_MAX_CTRL];
static usb_hc_ops_t g_xhci_ops[XHCI_MAX_CTRL];
static int g_n_xhci = 0;

static inline u32 xrd(xhci_state_t *x, volatile u32 *base, u32 off) {
    (void)x;
    return base[off / 4];
}
static inline void xwr(xhci_state_t *x, volatile u32 *base, u32 off,
                       u32 v) {
    (void)x;
    base[off / 4] = v;
}
static void xhci_log(const char *s) { oc_console_puts(s); }

/* ring a doorbell: DB[slot] = target.  The doorbell registers are
 * 4 bytes apart; DB[0] is the command ring.  (The generic xwr() takes
 * a BYTE offset - passing a slot id there made hw_slot 1..3 all ring
 * the command doorbell instead of the endpoint's, which left every
 * EP0 transfer waiting forever.) */
static inline void xhci_doorbell(xhci_state_t *x, u32 slot, u32 target) {
    x->db[slot] = target;
}

static void xhci_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)f;
    xhci_state_t *x = (xhci_state_t *)ctx;
    if (!x || !x->up) return;
    u32 iman = x->run[XHCI_IMAN(0) / 4];
    if (iman & 1u) {
        x->run[XHCI_IMAN(0) / 4] = iman;   /* w1c IP */
        x->irq_count++;
    }
}

/* ==================================================================
 * TRB / ring helpers
 * ================================================================== */

static void xhci_trb_set(xtrb_t *t, u64 param, u32 status, u32 ctrl) {
    t[0][0] = (u32)(param & 0xffffffffu);
    t[0][1] = (u32)(param >> 32);
    t[0][2] = status;
    t[0][3] = ctrl;
}

/* (re)write the ring's LINK TRB; the cycle bit matches `cycle` */
static void xhci_write_link(xhci_ring_t *r, u8 cycle) {
    xhci_trb_set(&r->trb[XHCI_RING_N - 1], r->phys, 0,
                 (TRB_LINK << TRB_TYPE_SH) | TRB_TC |
                 (cycle ? TRB_C : 0));
}

static void xhci_ring_init(xhci_ring_t *r, xtrb_t *trb, u64 phys) {
    r->trb = trb;
    r->phys = phys;
    r->enq = 0;
    r->cycle = 1;
    r->configured = 0;
    for (int i = 0; i < XHCI_RING_N; i++)
        xhci_trb_set(&trb[i], 0, 0, 0);
    xhci_write_link(r, 1);
}

/* enqueue one TRB; returns its physical address (for event matching) */
static u64 xhci_ring_put(xhci_ring_t *r, u64 param, u32 status,
                         u32 ctrl) {
    if (r->enq == XHCI_RING_N - 1) {
        /* step over the LINK TRB: flip cycle and refresh its cycle
         * bit so both QEMU and real hardware keep following the ring */
        r->enq = 0;
        r->cycle ^= 1;
        xhci_write_link(r, r->cycle);
    }
    xtrb_t *t = &r->trb[r->enq];
    u64 phys = r->phys + (u64)r->enq * 16;
    xhci_trb_set(t, param, status, ctrl | (r->cycle ? TRB_C : 0));
    r->enq++;
    return phys;
}

/* ==================================================================
 * event ring
 * ================================================================== */

static int xhci_next_event(xhci_state_t *x, xtrb_t *out) {
    xtrb_t *t = &x->ev[x->ev_deq];
    u32 ctrl = t[0][3];
    if (!(ctrl & x->ev_cycle)) return 0;   /* no (new) event */
    out[0][0] = t[0][0]; out[0][1] = t[0][1];
    out[0][2] = t[0][2]; out[0][3] = ctrl;
    x->ev_deq++;
    if (x->ev_deq == XHCI_RING_N - 1) {    /* step over the LINK TRB */
        x->ev_deq = 0;
        x->ev_cycle ^= 1;
    }
    return 1;
}

/* publish the dequeue pointer so the controller frees consumed events */
static void xhci_erdp_update(xhci_state_t *x) {
    u64 addr = x->ev_phys + (u64)x->ev_deq * 16;
    xwr(x, x->run, XHCI_ERDP(0), (u32)((addr & 0xfffffff0u) | 8u));
    xwr(x, x->run, XHCI_ERDP(0) + 4, (u32)(addr >> 32));
}

/* ==================================================================
 * commands (caller holds the USB core transfer lock)
 * ================================================================== */

static int xhci_do_cmd(xhci_state_t *x, u64 param, u32 status,
                       u32 ctrl, u32 timeout_ms) {
    if (!x->up) return -1;
    if (x->cmd_enq == XHCI_RING_N - 1) {
        x->cmd_enq = 0;
        x->cmd_cycle ^= 1;
        xhci_write_link(
            &(xhci_ring_t){ .trb = x->cmd, .phys = x->cmd_phys },
            x->cmd_cycle);
    }
    xtrb_t *t = &x->cmd[x->cmd_enq];
    u64 cmd_phys = x->cmd_phys + (u64)x->cmd_enq * 16;
    xhci_trb_set(t, param, status, ctrl | (x->cmd_cycle ? TRB_C : 0));
    x->cmd_enq++;

    x->cmd_rc = -1;
    x->cmd_slot = 0;
    xhci_doorbell(x, 0, 0);   /* ring doorbell 0 (command ring) */

    u64 deadline = oc_timer_now_ms() + timeout_ms;
    for (;;) {
        xtrb_t ev;
        while (xhci_next_event(x, &ev)) {
            u32 type = (ev[3] >> TRB_TYPE_SH) & 0x3fu;
            xhci_erdp_update(x);
            if (type != TRB_EV_CMD_DONE) continue;
            u64 ptr = ((u64)ev[0] | ((u64)ev[1] << 32)) & ~0xfull;
            if (ptr == cmd_phys) {
                /* Command Completion Event: CC = DW2[31:24],
                 * Slot ID = DW3[31:24] */
                u32 cc = (ev[2] >> 24) & 0xffu;
                x->cmd_rc = (int)cc;
                x->cmd_slot = (int)((ev[3] >> 24) & 0xffu);
                return (cc == CC_SUCCESS) ? 0 : -2;
            }
            /* completion of an older command: ignore */
        }
        xhci_erdp_update(x);
        if (oc_timer_now_ms() > deadline) return OC_USB_ETIMEDOUT;
        sched_yield();
    }
}

/* ==================================================================
 * slot management
 * ================================================================== */

static xhci_slot_t *xhci_slot_by_dev(xhci_state_t *x, usb_dev_t *d) {
    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        if (x->slots[i].used && x->slots[i].dev == d)
            return &x->slots[i];
    }
    return NULL;
}

/* endpoint doorbell id for a core ep_addr */
static int xhci_epid(u8 ep_addr) {
    if ((ep_addr & 0x0f) == 0) return 1;             /* ep0 */
    int num = USB_EP_NUM(ep_addr);
    return (ep_addr & 0x80) ? (2 * num + 1) : (2 * num);
}

/* ring index for a doorbell id (0 = ep0, 2..5 -> 1..4); -1 = none */
static int xhci_ring_idx(int epid) {
    if (epid == 1) return 0;
    if (epid >= 2 && epid <= 5) return epid - 1;
    return -1;
}

static u8 xhci_core_speed_to_xhci(u8 speed) {
    switch (speed) {
        case USB_SPEED_FS: return 1;
        case USB_SPEED_LS: return 2;
        case USB_SPEED_SS: return 4;
        default:           return 3;
    }
}

/* build the input context for Address Device / Evaluate Context.
 * Slot context layout per the xHCI spec (6.2.2.1): DW0 carries the
 * port speed (23:20) and context entries (31:27), DW1 the root hub
 * port number (23:16), DW3 the device address (31:24, zero while the
 * device is still on address 0 / BSR=1). */
static void xhci_build_addr_ctx(xhci_state_t *x, xhci_slot_t *s,
                                u8 xhci_speed, u8 port, u32 mps0) {
    u32 st = x->ctx_stride;
    oc_memset(s->in_ctx, 0, 1024);
    u32 *ic = (u32 *)(void *)s->in_ctx;
    ic[0] = 0;                                   /* drop flags */
    ic[1] = (1u << 0) | (1u << 1);               /* add slot + EP0 */
    u32 *sc = (u32 *)(void *)(s->in_ctx + st);
    sc[0] = ((u32)xhci_speed << 20) | (1u << 27);/* speed, 1 ctx entry */
    sc[1] = ((u32)port << 16);                   /* root hub port number */
    sc[3] = 0;                                   /* address 0 until the
                                                    device is addressed */
    u32 *ep0 = (u32 *)(void *)(s->in_ctx + 2 * st);
    /* Endpoint context layout per the xHCI spec 6.4.1.2 (and matching
     * QEMU/Linux): DW1 = EP type (5:3) + Max Packet Size (31:16),
     * DW2 = TR Dequeue Pointer low (31:4) + DCS (0), DW3 = dequeue
     * high. */
    ep0[1] = (4u << 3) | ((u32)mps0 << 16);       /* EP type = control,
                                                     Max Packet Size */
    ep0[2] = (u32)((s->ep[0].phys & 0xffffffffu) | 1u); /* dequeue low |
                                                            DCS = 1 */
    ep0[3] = (u32)(s->ep[0].phys >> 32);          /* dequeue high */
}

/* run Address Device (BSR per flag) */
static int xhci_address_slot(xhci_state_t *x, xhci_slot_t *s,
                             u8 xhci_speed, u8 port, u32 mps0, int bsr) {
    xhci_build_addr_ctx(x, s, xhci_speed, port, mps0);
    xwr(x, x->op, XHCI_DCBAAP, (u32)(x->dcbaa_phys & 0xffffffffu));
    xwr(x, x->op, XHCI_DCBAAP + 4, (u32)(x->dcbaa_phys >> 32));
    u32 ctrl = (TRB_ADDRESS_DEV << TRB_TYPE_SH) |
               ((u32)s->hw_slot << TRB_SLOT_SH);
    if (bsr) ctrl |= TRB_BSR;
    return xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
}

/* lazily configure a non-default endpoint (first use by a class
 * driver).  the ring must already be initialised. */
static int xhci_config_ep(xhci_state_t *x, xhci_slot_t *s, u8 ep_addr) {
    u32 st = x->ctx_stride;
    int epid = xhci_epid(ep_addr);
    int ridx = xhci_ring_idx(epid);
    if (ridx < 0) return OC_USB_EINVAL;
    xhci_ring_t *r = &s->ep[ridx];

    u16 mps = 64;
    u8  attr = USB_EP_ATTR_BULK;
    for (int i = 0; i < s->dev->n_ep; i++) {
        usb_endpoint_t *e = &s->dev->eps[i];
        if (e->addr == ep_addr) {
            mps = e->maxpack ? e->maxpack : mps;
            attr = e->attr;
        }
    }
    /* SuperSpeed devices carry 1024-byte bulk endpoints (the 512 cap
     * is only valid for high-speed); keep the descriptor value when
     * it already fits one transfer */
    if (attr == USB_EP_ATTR_BULK && mps > 1024) mps = 1024;
    if (attr == USB_EP_ATTR_INTERRUPT && mps > 64) mps = 64;
    if (attr == USB_EP_ATTR_ISO && mps > 1023) mps = 1023;
    /* xHCI EP type codes (spec Table 6-31): 1=Isoch Out, 2=Isoch In,
     * 3=Bulk Out, 4=Bulk In, 5=Control Bidir, 6=Interrupt Out,
     * 7=Interrupt In - the direction must match the endpoint */
    u8 eptype;
    if (attr == USB_EP_ATTR_BULK)
        eptype = (ep_addr & 0x80) ? 4u : 3u;
    else if (attr == USB_EP_ATTR_INTERRUPT)
        eptype = (ep_addr & 0x80) ? 7u : 6u;
    else if (attr == USB_EP_ATTR_ISO)
        eptype = (ep_addr & 0x80) ? 2u : 1u;
    else
        eptype = 5u;

    oc_memset(s->in_ctx, 0, 1024);
    u32 *ic = (u32 *)(void *)s->in_ctx;
    ic[0] = 0;
    ic[1] = (1u << 0) | (1u << epid);            /* add slot + this EP */
    u32 *sc = (u32 *)(void *)(s->in_ctx + st);
    oc_memcpy(sc, s->out_ctx + st, 32);          /* keep speed/port */
    u32 entries = (sc[0] >> 27) & 0x1fu;         /* context entries live
                                                    in slot DW0 31:27 */
    if ((u32)epid > entries)
        sc[0] = (sc[0] & ~(0x1fu << 27)) | ((u32)epid << 27);
    u32 *ec = (u32 *)(void *)(s->in_ctx + (u32)(1 + epid) * st);
    /* EP context layout: see xhci_build_addr_ctx - DW1 carries the
     * EP type and Max Packet Size, DW2 the dequeue pointer + DCS */
    ec[1] = (eptype << 3) | ((u32)mps << 16);    /* EP state 0 in input */
    ec[2] = (u32)((r->phys & 0xffffffffu) | 1u); /* dequeue low | DCS = 1 */
    ec[3] = (u32)(r->phys >> 32);                /* dequeue high */

    u32 ctrl = (TRB_CONFIG_EP << TRB_TYPE_SH) |
               ((u32)s->hw_slot << TRB_SLOT_SH);
    int rc = xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
    if (rc == 0) r->configured = 1;
    return rc;
}

/* reset an endpoint after STALL + CLEAR_FEATURE(ENDPOINT_HALT) */
int xhci_reset_ep(usb_dev_t *d, u8 ep_addr) {
    for (int i = 0; i < g_n_xhci; i++) {
        xhci_state_t *x = &g_xhci[i];
        if (!x->up) continue;
        xhci_slot_t *s = xhci_slot_by_dev(x, d);
        if (!s) continue;
        int epid = xhci_epid(ep_addr);
        int ridx = xhci_ring_idx(epid);
        if (ridx < 0) return OC_USB_EINVAL;
        xhci_ring_t *r = &s->ep[ridx];
        if (!s->halted[ridx]) return 0;
        u32 slotf = ((u32)s->hw_slot << TRB_SLOT_SH) | (u32)epid;
        /* 1. Reset Endpoint (clears the halt in the controller) */
        xhci_do_cmd(x, 0, 0, (TRB_RESET_EP << TRB_TYPE_SH) | slotf,
                    1000);
        /* 2. re-position the ring on the next TRB we will fill */
        u64 deq = r->phys + (u64)r->enq * 16;
        xhci_do_cmd(x, deq | (u64)r->cycle, 0,
                    (TRB_SET_DEQ << TRB_TYPE_SH) | slotf, 1000);
        s->halted[ridx] = 0;
        return 0;
    }
    return -1;
}

/* ==================================================================
 * transfer waits
 * ================================================================== */

/* drain events; return 1 when the given TRB completes.  Other
 * transfer events belonging to pending interrupt TRBs are latched so
 * the next poll picks the data up (XHCI has no per-TRB abort, so a
 * NAKed interrupt TRB simply stays in flight). */
static int xhci_wait_trb(xhci_state_t *x, u64 trb_phys, u32 *cc_out,
                         u32 *rem_out, u32 timeout_ms) {
    u64 deadline = oc_timer_now_ms() + timeout_ms;
    for (;;) {
        xtrb_t ev;
        while (xhci_next_event(x, &ev)) {
            u32 type = (ev[3] >> TRB_TYPE_SH) & 0x3fu;
            xhci_erdp_update(x);
            if (type != TRB_EV_TRANSFER) continue;
            /* Transfer Event: TL = DW2[23:0], CC = DW2[31:24],
             * Slot ID = DW3[31:24] */
            u32 cc = (ev[2] >> 24) & 0xffu;
            u64 ptr = ((u64)ev[0] | ((u64)ev[1] << 32)) & ~0xfull;
            u32 slotid = (ev[3] >> 24) & 0xffu;
            if (ptr == trb_phys) {
                if (cc_out) *cc_out = cc;
                if (rem_out) *rem_out = ev[2] & 0xffffffu;
                return 1;
            }
            for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
                xhci_slot_t *s = &x->slots[i];
                if (!s->used || (u32)s->hw_slot != slotid) continue;
                for (int l = 0; l < 4; l++) {
                    if (s->int_pending[l] &&
                        s->int_trb_phys[l] == ptr) {
                        if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET)
                            s->latch_valid[l] = 1;
                        s->int_pending[l] = 0;
                    }
                }
            }
        }
        xhci_erdp_update(x);
        if (oc_timer_now_ms() > deadline) return 0;
        sched_yield();
    }
}

/* ==================================================================
 * host ops
 * ================================================================== */

/* lazily bring a slot up on the first control transfer (the core's
 * enumeration starts with GET_DESCRIPTOR at address 0) */
static xhci_slot_t *xhci_ensure_slot(xhci_state_t *x, usb_dev_t *d,
                                     int *err) {
    xhci_slot_t *s = xhci_slot_by_dev(x, d);
    if (s) { *err = 0; return s; }
    if (d->slot < 0 || d->slot >= XHCI_MAX_SLOTS) { *err = -1; return NULL; }
    s = &x->slots[d->slot];
    oc_memset(s, 0, sizeof(*s));
    s->used = 1;
    s->dev = d;

    /* enable a slot */
    int rc = xhci_do_cmd(x, 0, 0, TRB_ENABLE_SLOT << TRB_TYPE_SH, 1000);
    if (rc != 0 || x->cmd_slot == 0) {
        oc_console_puts("xhci: ENABLE_SLOT failed\n");
        s->used = 0;
        *err = -1;
        return NULL;
    }
    s->hw_slot = (int)x->cmd_slot;

    /* allocate contexts + rings */
    u64 p1 = pmm_alloc_frame();   /* out ctx */
    u64 p2 = pmm_alloc_frame();   /* in ctx  */
    u64 p3 = pmm_alloc_frame();   /* rings   */
    u64 p4 = pmm_alloc_frame();   /* bulk/iso bounce */
    u64 p5 = pmm_alloc_frame();   /* ctl + int bounce */
    if (!p1 || !p2 || !p3 || !p4 || !p5) {
        s->used = 0;
        *err = OC_USB_ENOMEM;
        return NULL;
    }
    s->out_ctx = (u8 *)(uintptr_t)p1;
    s->out_ctx_phys = p1;
    s->in_ctx = (u8 *)(uintptr_t)p2;
    s->in_ctx_phys = p2;
    s->ring_page = (u8 *)(uintptr_t)p3;
    s->ring_page_phys = p3;
    s->bulk_buf = (u8 *)(uintptr_t)p4;
    s->ctl_buf = (u8 *)(uintptr_t)p5;
    s->int_buf = (u8 *)(uintptr_t)(p5 + 512);
    oc_memset(s->out_ctx, 0, PMM_PAGE_SIZE);
    oc_memset(s->in_ctx, 0, PMM_PAGE_SIZE);
    oc_memset(s->ring_page, 0, PMM_PAGE_SIZE);
    oc_memset(s->bulk_buf, 0, PMM_PAGE_SIZE);
    oc_memset((void *)(uintptr_t)p5, 0, 512 + 4 * 64);

    xhci_ring_init(&s->ep[0], (xtrb_t *)(void *)s->ring_page, p3);
    for (int i = 1; i < XHCI_MAX_ERING; i++) {
        xhci_ring_init(&s->ep[i],
                       (xtrb_t *)(void *)(s->ring_page + i * 1024),
                       p3 + (u64)i * 1024);
    }

    /* DCBAA[hw_slot] = output ctx (the HC writes the output context
     * there when Address Device completes) */
    volatile u64 *dc = (volatile u64 *)(void *)x->dcbaa;
    dc[s->hw_slot] = s->out_ctx_phys;

    /* Address Device with BSR=1: the device stays at address 0 and
     * the core reads desc8 through EP0 next.  MPS for the default
     * endpoint is fixed by the port speed (spec 6.2.3.1). */
    u8 xsp = xhci_core_speed_to_xhci(d->speed);
    u8 port = (u8)(d->hub_port + 1);
    u32 bsr_mps = (d->speed == USB_SPEED_SS) ? 512u :
                  (d->speed == USB_SPEED_LS) ? 8u : 64u;
    rc = xhci_address_slot(x, s, xsp, port, bsr_mps, 1);
    if (rc != 0) {
        oc_console_puts("xhci: ADDRESS_DEVICE(BSR) failed\n");
        s->used = 0;
        *err = -1;
        return NULL;
    }
    *err = 0;
    return s;
}

static int xhci_control(usb_host_t *h, usb_dev_t *d,
                        const usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || !d) return -1;
    int err;
    xhci_slot_t *s = xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    if (len > 512) return OC_USB_EINVAL;

    /* XHCI does SET_ADDRESS natively: intercept the core's request */
    if (setup->bmRequestType == 0x00 &&
        setup->bRequest == USB_REQ_SET_ADDR && !s->addressed) {
        u32 mps = d->mps0 ? d->mps0 : 8;
        s->mps0_prog = mps;
        int rc = xhci_address_slot(x, s,
                                   xhci_core_speed_to_xhci(d->speed),
                                   (u8)(d->hub_port + 1), mps, 0);
        if (rc == 0) s->addressed = 1;
        return rc;
    }
    /* MPS0 discovery corrected the value: re-evaluate the context */
    if (s->addressed && d->mps0 && d->mps0 != s->mps0_prog) {
        xhci_build_addr_ctx(x, s, xhci_core_speed_to_xhci(d->speed),
                            (u8)(d->hub_port + 1), d->mps0);
        u32 ctrl = (TRB_EVALUATE_CTX << TRB_TYPE_SH) |
                   ((u32)s->hw_slot << TRB_SLOT_SH);
        xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
        s->mps0_prog = d->mps0;
    }

    /* SETUP (+DATA +STATUS) on the EP0 ring */
    xhci_ring_t *r = &s->ep[0];
    u8 dir_in = (setup->bmRequestType & 0x80) ? 1 : 0;
    u64 setup_param = 0;
    oc_memcpy(&setup_param, setup, 8);
    xhci_ring_put(r, setup_param, 8,
                  (TRB_SETUP_STAGE << TRB_TYPE_SH) | TRB_IDT | TRB_CH);
    u64 tr_last;
    if (len == 0) {
        tr_last = xhci_ring_put(r, 0, 0,
                                (TRB_STATUS_STAGE << TRB_TYPE_SH) |
                                (dir_in ? 0u : TRB_DIR_IN) | TRB_IOC);
    } else {
        u8 *bounce = s->ctl_buf;
        if (!dir_in) oc_memcpy(bounce, buf, len);
        xhci_ring_put(r, (u64)(uintptr_t)bounce, len,
                      (TRB_DATA_STAGE << TRB_TYPE_SH) |
                      (dir_in ? TRB_DIR_IN : 0u) | TRB_CH);
        tr_last = xhci_ring_put(r, 0, 0,
                                (TRB_STATUS_STAGE << TRB_TYPE_SH) |
                                (dir_in ? 0u : TRB_DIR_IN) | TRB_IOC);
    }
    xhci_doorbell(x, (u32)s->hw_slot, 1);   /* slot doorbell, EP0 */

    u32 cc = 0, rem = 0;
    int got = xhci_wait_trb(x, tr_last, &cc, &rem, timeout_ms);
    if (!got) {
        return OC_USB_ETIMEDOUT;
    }
    if (cc == CC_STALL) {
        s->halted[0] = 1;
        return OC_USB_ESTALL;
    }
    if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
    if (len > 0 && dir_in) {
        u32 rlen = rem;
        if (rlen > (u32)len) rlen = len;
        oc_memcpy(buf, s->ctl_buf, (u32)((u32)len - rlen));
    }
    return 0;
}

static int xhci_bulk(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !buf) return -1;
    if (len == 0) return 0;
    if (len > 4096) return OC_USB_EINVAL;   /* single bounce page */
    int err;
    xhci_slot_t *s = xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = xhci_epid(ep_addr);
    int ridx = xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    if (s->halted[ridx]) xhci_reset_ep(d, ep_addr);

    u8 dir_in = (ep_addr & 0x80) ? 1 : 0;
    oc_memcpy(s->bulk_buf, buf, len);
    u64 tr = xhci_ring_put(r, (u64)(uintptr_t)s->bulk_buf, len,
                           (TRB_NORMAL << TRB_TYPE_SH) | TRB_IOC);
    xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);

    u32 cc = 0, rem = 0;
    int got = xhci_wait_trb(x, tr, &cc, &rem, timeout_ms);
    if (!got) return OC_USB_ETIMEDOUT;
    if (cc == CC_STALL) {
        s->halted[ridx] = 1;
        return OC_USB_ESTALL;
    }
    if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
    /* the event's length field holds the bytes NOT transferred */
    u32 rlen = rem;
    if (rlen > (u32)len) rlen = len;
    int moved = (int)((u32)len - rlen);
    if (dir_in) oc_memcpy(buf, s->bulk_buf, (u32)moved);
    return moved;
}

static int xhci_interrupt(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !buf) return -1;
    if (len == 0 || len > 64) return OC_USB_EINVAL;
    int err;
    xhci_slot_t *s = xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = xhci_epid(ep_addr);
    int ridx = xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    if (s->halted[ridx]) xhci_reset_ep(d, ep_addr);

    int latch = ridx - 1;          /* 0..3, one per data ring */
    u8 dir_in = (ep_addr & 0x80) ? 1 : 0;

    /* latched event from an earlier poll? */
    if (dir_in && s->latch_valid[latch]) {
        s->latch_valid[latch] = 0;
        oc_memcpy(buf, s->int_buf + latch * 64, len);
        return (int)s->latch_len[latch];
    }
    if (s->int_pending[latch]) {
        /* a TRB is already in flight; wait for it to finish */
        u32 cc = 0, rem = 0;
        int got = xhci_wait_trb(x, s->int_trb_phys[latch], &cc, &rem,
                                timeout_ms);
        if (!got) return OC_USB_ENAK;
        s->int_pending[latch] = 0;
        if (cc == CC_STALL) {
            s->halted[ridx] = 1;
            return OC_USB_ESTALL;
        }
        if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
        if (dir_in) oc_memcpy(buf, s->int_buf + latch * 64, len);
        return (int)len;
    }

    /* submit one interrupt TRB; if the device NAKs the whole window
     * the TRB stays pending and its event gets latched later */
    if (!dir_in) oc_memcpy(s->int_buf + latch * 64, buf, len);
    else oc_memset(s->int_buf + latch * 64, 0, 64);
    s->latch_len[latch] = len;
    u64 tr = xhci_ring_put(r, (u64)(uintptr_t)(s->int_buf + latch * 64),
                           len, (TRB_NORMAL << TRB_TYPE_SH) | TRB_IOC);
    s->int_trb_phys[latch] = tr;
    s->int_pending[latch] = 1;
    xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);

    u32 cc = 0, rem = 0;
    int got = xhci_wait_trb(x, tr, &cc, &rem, timeout_ms);
    if (!got) return OC_USB_ENAK;     /* TRB stays pending (latched) */
    s->int_pending[latch] = 0;
    if (cc == CC_STALL) {
        s->halted[ridx] = 1;
        return OC_USB_ESTALL;
    }
    if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
    if (dir_in) oc_memcpy(buf, s->int_buf + latch * 64, len);
    return (int)len;
}

static int xhci_iso_out(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !d->present) return -1;
    if (len > 1023) return -1;
    int err;
    xhci_slot_t *s = xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = xhci_epid(ep_addr);
    int ridx = xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    if (data && len) oc_memcpy(s->bulk_buf, data, len);
    u64 tr = xhci_ring_put(r, (u64)(uintptr_t)s->bulk_buf, len,
                           (TRB_ISOCH << TRB_TYPE_SH) | TRB_IOC);
    xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);
    /* wait a few ms so the event ring stays clean; audio re-arms at
     * the 1 ms frame cadence anyway */
    u32 cc = 0, rem = 0;
    int got = xhci_wait_trb(x, tr, &cc, &rem, 5);
    if (!got) return 0;    /* fire-and-forget; the poll drains it */
    if (cc == CC_STALL) {
        s->halted[ridx] = 1;
        return OC_USB_ESTALL;
    }
    return 0;
}

static int xhci_iso_in(usb_host_t *h, usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;   /* no current class driver consumes ISO IN */
}

/* ---- root hub ---- */

static int xhci_port_count(usb_host_t *h) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    return x ? x->n_ports : 0;
}

static int xhci_port_status(usb_host_t *h, int port,
                            usb_port_status_t *out) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || port < 0 || port >= x->n_ports) return -1;
    u32 off = XHCI_PORTSC_BASE + (u32)port * 0x10;   /* port = 0-based,
                                                   * PORTSC register set stride
                                                   * is 16 bytes per port */
    u32 v = x->op[off / 4];
    out->connected = (v & P_CCS) ? 1 : 0;
    out->enabled = (v & P_PED) ? 1 : 0;
    switch ((v & P_SPEED_MSK) >> P_SPEED_SH) {
        case 2:  out->speed = USB_SPEED_LS; break;
        case 1:  out->speed = USB_SPEED_FS; break;
        case 4:  out->speed = USB_SPEED_SS; break;
        default: out->speed = USB_SPEED_HS; break;
    }
    out->changed = (v & P_CSC) ? 1 : 0;
    if (v & P_CHANGES)
        /* write-1-to-clear the change bits; PP is NOT a change bit -
         * writing 0 to it would power the port off */
        xwr(x, x->op, off, (v & P_CHANGES) | (v & P_PP));
    return 0;
}

static int xhci_port_reset(usb_host_t *h, int port, u8 *speed_out) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up || port < 0 || port >= x->n_ports) return -1;
    u32 off = XHCI_PORTSC_BASE + (u32)port * 0x10;
    xwr(x, x->op, off, xrd(x, x->op, off) | P_PP);   /* power on */
    xwr(x, x->op, off, (xrd(x, x->op, off) & ~P_CHANGES) | P_PR);
    for (int i = 0; i < 400; i++) {
        u32 w = xrd(x, x->op, off);
        if (!(w & P_PR) && (w & P_PRC)) break;
        for (volatile int t = 0; t < 3000; t++) { }
    }
    /* w1c the reset change, preserving PP (see port_status) */
    xwr(x, x->op, off, (xrd(x, x->op, off) & P_CHANGES) |
                       (xrd(x, x->op, off) & P_PP));
    u32 w = xrd(x, x->op, off);
    if (!(w & P_CCS)) {
        char l[64]; char n[12];
        oc_strcpy(l, "xhci: port reset fail ccs=0 w=0x");
        oc_u64_to_hex(w, n, 8); oc_strcat(l, n);
        oc_strcat(l, "\n");
        oc_console_puts(l);
        return -1;
    }
    if (speed_out) {
        switch ((w & P_SPEED_MSK) >> P_SPEED_SH) {
            case 2:  *speed_out = USB_SPEED_LS; break;
            case 1:  *speed_out = USB_SPEED_FS; break;
            case 4:  *speed_out = USB_SPEED_SS; break;
            default: *speed_out = USB_SPEED_HS; break;
        }
    }
    return 0;
}

/* free slots whose core device vanished (hot-unplug) */
static void xhci_reap_slots(xhci_state_t *x) {
    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        xhci_slot_t *s = &x->slots[i];
        if (!s->used) continue;
        if (s->dev && !s->dev->present) {
            u32 ctrl = (TRB_DISABLE_SLOT << TRB_TYPE_SH) |
                       ((u32)s->hw_slot << TRB_SLOT_SH);
            xhci_do_cmd(x, 0, 0, ctrl, 200);
            volatile u64 *dc = (volatile u64 *)(void *)x->dcbaa;
            dc[s->hw_slot] = 0;
            oc_memset(s, 0, sizeof(*s));
        }
    }
}

static int xhci_poll(usb_host_t *h) {
    xhci_state_t *x = (xhci_state_t *)h->priv;
    if (!x || !x->up) return 0;
    xtrb_t ev;
    while (xhci_next_event(x, &ev)) {
        xhci_erdp_update(x);
        /* PORT STATUS CHANGE events are handled through PORTSC
         * polling in the core's hot-plug scan */
    }
    xhci_reap_slots(x);
    return 0;
}

static const usb_hc_ops_t xhci_ops_tmpl = {
    .name      = "XHCI",
    .bulk_max  = 4096,   /* single bounce page per transfer */
    .control   = xhci_control,
    .bulk      = xhci_bulk,
    .interrupt = xhci_interrupt,
    .iso_out   = xhci_iso_out,
    .iso_in    = xhci_iso_in,
    .port_count  = xhci_port_count,
    .port_status = xhci_port_status,
    .port_reset  = xhci_port_reset,
    .poll        = xhci_poll,
};

/* ==================================================================
 * init / probe
 * ================================================================== */

static int xhci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_xhci >= XHCI_MAX_CTRL) return -1;
    xhci_state_t *x = &g_xhci[g_n_xhci];
    oc_memset(x, 0, sizeof(*x));

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        xhci_log("xhci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    x->cap = (volatile u32 *)(uintptr_t)mmio;
    x->bus = bus; x->dev = dev; x->func = func;
    pci_enable_device(bus, dev, func);

    u8 caplen = (u8)(x->cap[0] & 0xff);
    if (caplen < 0x20) caplen = 0x20;
    x->op = (volatile u32 *)(uintptr_t)(mmio + caplen);
    u32 rtsoff = x->cap[XHCI_RTSOFF / 4] & ~0x1fu;
    u32 dboff  = x->cap[XHCI_DBOFF / 4] & ~0x3u;
    x->run = (volatile u32 *)(uintptr_t)(mmio + rtsoff);
    x->db  = (volatile u32 *)(uintptr_t)(mmio + dboff);

    u32 hsp1 = x->cap[XHCI_HCSPARAMS1 / 4];
    x->n_ports = (u8)((hsp1 >> 24) & 0xffu);
    x->max_slots = (hsp1 & 0x7ffu);
    if (x->n_ports > XHCI_MAX_PORTS) x->n_ports = XHCI_MAX_PORTS;
    if (x->max_slots > XHCI_MAX_SLOTS) x->max_slots = XHCI_MAX_SLOTS;
    u32 hcc1 = x->cap[XHCI_HCCPARAMS1 / 4];
    x->ctx_stride = (hcc1 & (1u << 2)) ? 64 : 32;
    if (!(hcc1 & 1u)) {
        xhci_log("xhci: controller without 64-bit addressing\n");
        return -1;
    }

    /* controller reset */
    xwr(x, x->op, XHCI_USBCMD, xrd(x, x->op, XHCI_USBCMD) |
                               XHCI_CMD_HCRST);
    for (int i = 0; i < 3000; i++) {
        u32 sts = xrd(x, x->op, XHCI_USBSTS);
        if (!(sts & XHCI_STS_CNR) &&
            !(xrd(x, x->op, XHCI_USBCMD) & XHCI_CMD_HCRST)) break;
        for (volatile int t = 0; t < 2000; t++) { }
    }
    if (xrd(x, x->op, XHCI_USBSTS) & XHCI_STS_CNR) {
        xhci_log("xhci: controller reset timeout\n");
        return -1;
    }

    /* scratchpad buffers */
    u32 hsp2 = x->cap[XHCI_HCSPARAMS2 / 4];
    u32 n_scratch = (hsp2 >> 27) & 0x1fu;
    if (hsp2 & (1u << 26)) n_scratch += 32;
    if (n_scratch > 32) n_scratch = 32;
    if (n_scratch) {
        u64 arr = pmm_alloc_frame();
        if (!arr) return -1;
        u64 *ent = (u64 *)(uintptr_t)arr;
        for (u32 i = 0; i < n_scratch; i++) {
            u64 p = pmm_alloc_frame();
            if (!p) return -1;
            oc_memset((void *)(uintptr_t)p, 0, PMM_PAGE_SIZE);
            ent[i] = p;
        }
        x->scratch_arr_phys = arr;
    }

    /* DCBAA */
    u64 dc = pmm_alloc_frame();
    if (!dc) return -1;
    x->dcbaa = (u8 *)(uintptr_t)dc;
    x->dcbaa_phys = dc;
    oc_memset(x->dcbaa, 0, PMM_PAGE_SIZE);
    if (x->scratch_arr_phys)
        ((volatile u64 *)(void *)x->dcbaa)[0] = x->scratch_arr_phys;

    /* command ring */
    u64 cr = pmm_alloc_frame();
    if (!cr) return -1;
    x->cmd = (xtrb_t *)(uintptr_t)cr;
    x->cmd_phys = cr;
    oc_memset((void *)x->cmd, 0, PMM_PAGE_SIZE);
    xhci_trb_set(&x->cmd[XHCI_RING_N - 1], cr, 0,
                 (TRB_LINK << TRB_TYPE_SH) | TRB_TC | TRB_C);
    x->cmd_enq = 0;
    x->cmd_cycle = 1;

    /* event ring + segment table (one page: ERST at 0, ring at 64) */
    u64 evp = pmm_alloc_frame();
    if (!evp) return -1;
    x->ev = (xtrb_t *)(uintptr_t)(evp + 64);
    x->ev_phys = evp + 64;
    x->erst = (u32 *)(uintptr_t)evp;
    x->erst_phys = evp;
    oc_memset((void *)(uintptr_t)evp, 0, PMM_PAGE_SIZE);
    x->erst[0] = (u32)(x->ev_phys & 0xffffffffu);
    x->erst[1] = (u32)(x->ev_phys >> 32);
    x->erst[2] = XHCI_RING_N;
    x->erst[3] = 0;
    x->ev_deq = 0;
    x->ev_cycle = 1;

    /* publish everything, then run */
    xwr(x, x->op, XHCI_CRCR, (u32)(cr | 1u));          /* CRCR + cycle */
    xwr(x, x->op, XHCI_CRCR + 4, (u32)(cr >> 32));
    xwr(x, x->op, XHCI_DCBAAP, (u32)(dc & 0xffffffffu));
    xwr(x, x->op, XHCI_DCBAAP + 4, (u32)(dc >> 32));
    xwr(x, x->op, XHCI_CONFIG, (x->max_slots & 0xffffu) << 16);
    for (u32 p = 0; p < x->n_ports; p++) {   /* power all ports */
        u32 off = XHCI_PORTSC_BASE + p * 0x10;
        xwr(x, x->op, off, xrd(x, x->op, off) | P_PP);
    }
    xwr(x, x->run, XHCI_ERSTSZ(0), 1);
    xwr(x, x->run, XHCI_ERSTBA(0), (u32)(x->erst_phys & 0xffffffffu));
    xwr(x, x->run, XHCI_ERSTBA(0) + 4, (u32)(x->erst_phys >> 32));
    xhci_erdp_update(x);
    xwr(x, x->run, XHCI_IMAN(0), 2u);   /* IE=1; polling is primary */

    xwr(x, x->op, XHCI_USBCMD, xrd(x, x->op, XHCI_USBCMD) | XHCI_CMD_RS);
    for (int i = 0; i < 3000; i++) {
        if (!(xrd(x, x->op, XHCI_USBSTS) & XHCI_STS_HCH)) break;
        for (volatile int t = 0; t < 1000; t++) { }
    }
    /* optional IRQ (polling stays the primary path) */
    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    x->irq = -1;
    if (irq < 16 &&
        oc_irq_register_handler(irq, xhci_irq_handler, x) == 0)
        x->irq = irq;

    x->up = 1;

    usb_host_t *h = &g_xhci_host[g_n_xhci];
    oc_memset(h, 0, sizeof(*h));
    oc_strcpy(h->name, "xhci0");
    if (g_n_xhci > 0) h->name[4] = (char)('0' + g_n_xhci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->flags = USB_HOSTF_NATIVE_ADDR;
    h->priv = x;
    g_xhci_ops[g_n_xhci] = xhci_ops_tmpl;
    h->ops = &g_xhci_ops[g_n_xhci];
    usb_register_host(h, &g_xhci_ops[g_n_xhci]);
    g_n_xhci++;
    return 0;
}

int xhci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    /* by class 0x0c0330 (covers qemu-xhci, nec-usb-xhci, real HW) */
    for (int nth = 0; nth < 4; nth++) {
        if (pci_find_class_exact(0x0c0330, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_xhci; i++) {
            if (g_xhci[i].bus == b && g_xhci[i].dev == dv &&
                g_xhci[i].func == fn)
                dup = 1;
        }
        if (!dup && xhci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int xhci_init(const pci_dev_t *dev) {
    if (!dev) return -1;
    return xhci_probe_one(dev->bus, dev->dev, dev->func);
}
