/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_xhci.c
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
 * Rings: every ring is 64 TRBs with a LINK TRB (Toggle Cycle set; its
 * cycle bit is re-written on every wrap to the PRE-wrap cycle state so
 * the controller consumes it and toggles its own CCS - xHCI 4.9.4) in
 * the last entry.  One transfer = one event (IOC on the last TRB of
 * the chain only).  The producer also tracks how many TRBs are still
 * un-retired (events not yet observed) and refuses to enqueue into a
 * full ring (xHCI 4.9.3: never write TRBs the HC has not consumed).
 * Device bring-up follows the spec sequence: Enable Slot ->
 * Address Device BSR=1 -> (core reads desc8 through EP0) ->
 * Address Device BSR=0 with the real MPS0 (intercepted from the
 * core's SET_ADDRESS) -> lazy Configure Endpoint when a class driver
 * first uses a non-default endpoint.
 */
#include "driver_usb.h"
#include "mem_pmm.h"
#include "mem_heap.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_sched.h"
#include "core_timer.h"

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
#define XHCI_MAX_ERING     31          /* BUG-0153 FIX (A10-30): was 5
                                           (ep3+ silently EINVAL). Ring 0 =
                                           ep0, rings 1..30 = doorbells 2..31
                                           = USB ep1..15 OUT/IN; doorbell 31
                                           (ep15 IN) was wrongly rejected */
#define XHCI_INT_LATCHES   (XHCI_MAX_ERING - 1) /* one interrupt latch per
                                           non-default ring (1..30) */
#define USB_DT_SS_EP_COMPANION 0x30    /* USB 3.0 spec 9.6.2.5 */

typedef volatile u32 xtrb_t[4];     /* one TRB: param lo/hi, status, ctrl */

typedef struct driver_usb_xhci_ring {
    xtrb_t *trb;             /* 16-byte entries */
    u64     phys;
    int     enq;             /* next index to fill */
    int     deq;             /* producer slot whose completion event is
                               expected next (BUG-0146 producer-side
                               dequeue tracking, see transfer_event) */
    int     in_flight;       /* TRBs enqueued whose completion event has
                               not been observed yet; ring_put refuses at
                               XHCI_RING_N-1 so a consumed TRB is never
                               overwritten (BUG-0146, xHCI 4.9.3) */
    u8      cycle;           /* cycle bit for the next filled TRB */
    u8      configured;      /* CONFIG_EP done */
} driver_usb_xhci_ring_t;

typedef struct driver_usb_xhci_slot {
    int        used;
    int        hw_slot;      /* xhci slot id (1..MaxSlots) */
    driver_usb_dev_t *dev;          /* core device this slot serves */
    int        addressed;    /* Address Device BSR=0 done */
    u32        mps0_prog;    /* ep0 MPS we last programmed */
    u8        *out_ctx;      /* output device context (page) */
    u64        out_ctx_phys;
    u8        *in_ctx;       /* input control + device context (page) */
    u64        in_ctx_phys;
    u8        *ring_page;    /* ring storage (8 pages for 31 rings) */
    u64        ring_page_phys;
    u64        ring_extra_phys[7];  /* BUG-0151: tracked for teardown */
    driver_usb_xhci_ring_t *ep;   /* [XHCI_MAX_ERING], kmalloc'd in
                                       ensure_slot - a static array here
       blew the 0x400000 identity window (BUG-0153 ring expansion) */
    u8        *bulk_buf;     /* 4 KiB bulk bounce (BUG-0152: never shared
                                  with the ISO bounce below) */
    u64        bulk_buf_phys;
    u8        *iso_buf;      /* BUG-0152: iso OUT bounce, NOT shared with bulk */
    u64        iso_buf_phys;
    u8        *ctl_buf;      /* control data bounce (512 B) */
    u64        ctl_buf_phys; /* ctl_buf and int_buf share this page */
    u8        *int_buf;      /* interrupt latch buffers, one 64 B slice
                                  per non-default ring (BUG-0153: was 4,
                                  now XHCI_INT_LATCHES = 30) */
    volatile int latch_valid[XHCI_INT_LATCHES];
    volatile int latch_len[XHCI_INT_LATCHES];
    volatile int int_pending[XHCI_INT_LATCHES]; /* int TRB in flight (per ring) */
    u64        int_trb_phys[XHCI_INT_LATCHES];
    u8         halted[XHCI_MAX_ERING]; /* endpoint halted (stall seen;
                                  was u8[6] and overflowed at ring idx >= 6
                                  after the BUG-0153 ring expansion) */
} driver_usb_xhci_slot_t;

typedef struct driver_usb_xhci_state {
    volatile u32 *cap;
    volatile u32 *op;
    volatile u32 *run;       /* runtime base */
    volatile u32 *db;        /* doorbell base */
    u8   bus, dev, func;
    u8   n_ports;
    u32  max_slots;
    u32  ctx_stride;         /* 32 or 64 (HCCPARAMS1.CSZ) */

    driver_usb_xhci_slot_t slots[XHCI_MAX_SLOTS];

    /* command ring */
    xtrb_t *cmd;
    u64     cmd_phys;
    int     cmd_enq;
    u8      cmd_cycle;
    u32     cmd_slot;        /* slot id returned by the last command */
    int     cmd_in_flight;   /* enqueued commands with no CMD_DONE event
                               seen yet (BUG-0146 full-ring guard) */

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
} driver_usb_xhci_state_t;

#define XHCI_MAX_CTRL 2
static driver_usb_xhci_state_t g_xhci[XHCI_MAX_CTRL];
static driver_usb_host_t   g_xhci_host[XHCI_MAX_CTRL];
static driver_usb_hc_ops_t g_xhci_ops[XHCI_MAX_CTRL];
static int g_n_xhci = 0;

static inline u32 xrd(driver_usb_xhci_state_t *x, volatile u32 *base, u32 off) {
    (void)x;
    return base[off / 4];
}
static inline void xwr(driver_usb_xhci_state_t *x, volatile u32 *base, u32 off,
                       u32 v) {
    (void)x;
    base[off / 4] = v;
}
static void driver_usb_xhci_log(const char *s) { screen_console_puts(s); }

/* ring a doorbell: DB[slot] = target.  The doorbell registers are
 * 4 bytes apart; DB[0] is the command ring.  (The generic xwr() takes
 * a BYTE offset - passing a slot id there made hw_slot 1..3 all ring
 * the command doorbell instead of the endpoint's, which left every
 * EP0 transfer waiting forever.) */
static inline void driver_usb_xhci_doorbell(driver_usb_xhci_state_t *x, u32 slot, u32 target) {
    x->db[slot] = target;
}

static void driver_usb_xhci_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)f;
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)ctx;
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

static void driver_usb_xhci_trb_set(xtrb_t *t, u64 param, u32 status, u32 ctrl) {
    t[0][0] = (u32)(param & 0xffffffffu);
    t[0][1] = (u32)(param >> 32);
    t[0][2] = status;
    t[0][3] = ctrl;
}

/* (re)write the ring's LINK TRB.  `cycle` must be the cycle state the
 * controller is CURRENTLY expecting when it arrives at the LINK (the
 * pre-wrap RCS) - xHCI 4.9.4: the HC only follows a LINK whose C bit
 * equals its own CCS, then toggles its CCS to land on TRB[0] expecting
 * the new cycle. */
static void driver_usb_xhci_write_link(driver_usb_xhci_ring_t *r, u8 cycle) {
    driver_usb_xhci_trb_set(&r->trb[XHCI_RING_N - 1], r->phys, 0,
                 (TRB_LINK << TRB_TYPE_SH) | TRB_TC |
                 (cycle ? TRB_C : 0));
}

static void driver_usb_xhci_ring_init(driver_usb_xhci_ring_t *r, xtrb_t *trb, u64 phys) {
    r->trb = trb;
    r->phys = phys;
    r->enq = 0;
    r->deq = 0;
    r->in_flight = 0;
    r->cycle = 1;
    r->configured = 0;
    for (int i = 0; i < XHCI_RING_N; i++)
        driver_usb_xhci_trb_set(&trb[i], 0, 0, 0);
    driver_usb_xhci_write_link(r, 1);
}

/* enqueue one TRB; returns its physical address (for event matching),
 * or 0 when the ring is FULL (caller must refuse the transfer).
 * BUG-0146 FIX (A10-23): the old producer only tracked the enqueue side
 * and silently overwrote TRBs the controller had not consumed yet when
 * the ring wrapped - xHCI 4.9.3 forbids writing TRBs ahead of the HC's
 * dequeue pointer.  in_flight counts enqueued-but-unretired TRBs (the
 * event ring is the only consumption report) and hard-stops at the 63
 * usable slots. */
static u64 driver_usb_xhci_ring_put(driver_usb_xhci_ring_t *r, u64 param, u32 status,
                         u32 ctrl) {
    if (r->in_flight >= XHCI_RING_N - 1) return 0;   /* full: refuse */
    if (r->enq == XHCI_RING_N - 1) {
        /* BUG-0146 FIX (A10-23): the LINK TRB's C bit must equal the HC's
         * CURRENT cycle state (the PRE-wrap cycle), so a real host
         * controller consumes the LINK, flips its own ccs and lands on
         * ring[0] expecting the NEW cycle - which is what ring_put writes
         * from here on. The old code wrote the NEW cycle into the LINK:
         * QEMU ignores LINK cycle checking so it kept running, but a real
         * xHC compares C != ccs and parks on the LINK forever (ring
         * stall). */
        u8 old_cycle = r->cycle;
        r->enq = 0;
        r->cycle ^= 1;
        driver_usb_xhci_write_link(r, old_cycle);
    }
    xtrb_t *t = &r->trb[r->enq];
    u64 phys = r->phys + (u64)r->enq * 16;
    driver_usb_xhci_trb_set(t, param, status, ctrl | (r->cycle ? TRB_C : 0));
    r->enq++;
    r->in_flight++;
    return phys;
}

/* ==================================================================
 * event ring
 * ================================================================== */

static int driver_usb_xhci_next_event(driver_usb_xhci_state_t *x, xtrb_t *out) {
    for (;;) {
        xtrb_t *t = &x->ev[x->ev_deq];
        u32 ctrl = t[0][3];

        /* BUG-0045 FIX: the last slot of the event ring holds a LINK TRB
         * (now actually written at init — see the event ring setup),
         * matching the command/transfer rings. Real controllers never
         * park an event on it; QEMU, which wraps the ring without
         * honouring the LINK, can. Handle both: a LINK is stepped over
         * (wrap to slot 0, flip the cycle); anything else is consumed
         * when its cycle bit matches. The old code never wrote the LINK
         * and skipped slot 63 unconditionally, losing one event out of
         * every 64 and desyncing from QEMU's wrap point. */
        if (((ctrl >> TRB_TYPE_SH) & 0x3fu) == TRB_LINK) {
            x->ev_deq = 0;
            x->ev_cycle ^= 1;
            continue;
        }
        if (!(ctrl & x->ev_cycle)) return 0;   /* no (new) event */

        out[0][0] = t[0][0]; out[0][1] = t[0][1];
        out[0][2] = t[0][2]; out[0][3] = ctrl;
        x->ev_deq++;
        if (x->ev_deq == XHCI_RING_N) {
            /* consumed slot 63 (QEMU had parked an event there): wrap */
            x->ev_deq = 0;
            x->ev_cycle ^= 1;
        }
        return 1;
    }
}

/* publish the dequeue pointer so the controller frees consumed events */
static void driver_usb_xhci_erdp_update(driver_usb_xhci_state_t *x) {
    u64 addr = x->ev_phys + (u64)x->ev_deq * 16;
    xwr(x, x->run, XHCI_ERDP(0), (u32)((addr & 0xfffffff0u) | 8u));
    xwr(x, x->run, XHCI_ERDP(0) + 4, (u32)(addr >> 32));
}

/* BUG-0146 FIX (A10-23) bookkeeping core: EVERY transfer event observed
 * on any path (wait_trb / poll / do_cmd) retires the TRB it completes,
 * so the producer's in-flight count tracks the hardware dequeue (the
 * event ring is the only consumption report; xHCI 4.9.3), and
 * completions of interrupt TRBs parked by a NAK get latched for the
 * next poll.  do_cmd used to silently DROP transfer events it drained
 * while waiting for a command, which lost exactly this bookkeeping.
 *
 * Retirement is a SPAN, not a single TRB: a TD only carries IOC on its
 * last TRB (a control TD enqueues SETUP+DATA+STATUS but produces one
 * event), so one decrement per event leaked 2 slots per control
 * transfer and wedged the EP0 ring after ~30 transfers.  The ring is
 * consumed strictly FIFO, so an event for slot k proves the HC has
 * consumed every producer slot before k as well: producer slots are
 * 0..62 (63 = LINK, never fired), so the consumed count is
 * ((k - deq) mod 63) + 1, with deq = the slot expected to fire next.
 * Callers keep updating ERDP themselves. */
static void driver_usb_xhci_transfer_event(driver_usb_xhci_state_t *x,
                                           const xtrb_t *ev) {
    u32 cc = ((*ev)[2] >> 24) & 0xffu;
    u64 ptr = (((u64)(*ev)[0] | ((u64)(*ev)[1] << 32)) & ~0xfull);
    u32 slotid = ((*ev)[3] >> 24) & 0xffu;
    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        driver_usb_xhci_slot_t *s = &x->slots[i];
        if (!s->used || (u32)s->hw_slot != slotid) continue;
        if (s->ep) {
            for (int ridx = 0; ridx < XHCI_MAX_ERING; ridx++) {
                driver_usb_xhci_ring_t *r = &s->ep[ridx];
                if (!r->trb) continue;
                if (ptr < r->phys ||
                    ptr >= r->phys + (u64)XHCI_RING_N * 16) continue;
                /* retire every slot the HC consumed through this TRB */
                int k = (int)((ptr - r->phys) / 16);
                int d = (((k - r->deq) % (XHCI_RING_N - 1))
                         + (XHCI_RING_N - 1)) % (XHCI_RING_N - 1) + 1;
                if (r->in_flight >= d) r->in_flight -= d;
                else r->in_flight = 0;   /* stale/duplicate event */
                r->deq = (k + 1) % (XHCI_RING_N - 1);
                break;
            }
        }
        for (int l = 0; l < XHCI_INT_LATCHES; l++) {
            if (s->int_pending[l] && s->int_trb_phys[l] == ptr) {
                if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET)
                    s->latch_valid[l] = 1;
                s->int_pending[l] = 0;
            }
        }
        break;
    }
}

/* ==================================================================
 * commands (caller holds the USB core transfer lock)
 * ================================================================== */

static int driver_usb_xhci_do_cmd(driver_usb_xhci_state_t *x, u64 param, u32 status,
                       u32 ctrl, u32 timeout_ms) {
    if (!x->up) return -1;
    /* BUG-0146 FIX (A10-23): refuse when 63 commands are still un-retired
     * (their TRBs would be overwritten).  This only happens when the HC
     * is wedged and commands time out; the caller sees an error instead
     * of a silently corrupted command ring. */
    if (x->cmd_in_flight >= XHCI_RING_N - 1) return OC_USB_EIO;
    if (x->cmd_enq == XHCI_RING_N - 1) {
        /* BUG-0146 FIX (A10-23): same LINK cycle rule as ring_put - the
         * LINK's C bit must equal the cycle state the HC is CURRENTLY
         * expecting (pre-wrap), so the controller consumes it, toggles
         * its own CCS and lands on TRB[0] expecting the new cycle.
         * do_cmd still wrote the NEW cycle (A10-23's second half):
         * a real xHC that has not prefetched the LINK parks on it with
         * C != ccs and the command ring stalls permanently (xHCI 4.9.4).
         * QEMU does not validate the LINK cycle, so it kept running. */
        u8 old_cycle = x->cmd_cycle;
        x->cmd_enq = 0;
        x->cmd_cycle ^= 1;
        driver_usb_xhci_write_link(
            &(driver_usb_xhci_ring_t){ .trb = x->cmd, .phys = x->cmd_phys },
            old_cycle);
    }
    xtrb_t *t = &x->cmd[x->cmd_enq];
    u64 cmd_phys = x->cmd_phys + (u64)x->cmd_enq * 16;
    driver_usb_xhci_trb_set(t, param, status, ctrl | (x->cmd_cycle ? TRB_C : 0));
    x->cmd_enq++;
    x->cmd_in_flight++;

    x->cmd_rc = -1;
    x->cmd_slot = 0;
    driver_usb_xhci_doorbell(x, 0, 0);   /* ring doorbell 0 (command ring) */

    u64 deadline = core_timer_now_ms() + timeout_ms;
    for (;;) {
        xtrb_t ev;
        while (driver_usb_xhci_next_event(x, &ev)) {
            u32 type = (ev[3] >> TRB_TYPE_SH) & 0x3fu;
            driver_usb_xhci_erdp_update(x);
            if (type == TRB_EV_CMD_DONE) {
                /* Command Completion Event: CC = DW2[31:24],
                 * Slot ID = DW3[31:24] */
                u64 ptr = ((u64)ev[0] | ((u64)ev[1] << 32)) & ~0xfull;
                /* BUG-0146: retire this (or an older) command so the
                 * producer-side count tracks the HC again */
                if (ptr >= x->cmd_phys &&
                    ptr < x->cmd_phys + (u64)XHCI_RING_N * 16 &&
                    x->cmd_in_flight > 0)
                    x->cmd_in_flight--;
                if (ptr == cmd_phys) {
                    u32 cc = (ev[2] >> 24) & 0xffu;
                    x->cmd_rc = (int)cc;
                    x->cmd_slot = (int)((ev[3] >> 24) & 0xffu);
                    return (cc == CC_SUCCESS) ? 0 : -2;
                }
                /* completion of an older command: retired above, ignore */
            } else if (type == TRB_EV_TRANSFER) {
                /* never drop transfer events seen during a command wait:
                 * they retire transfer-ring TRBs and latch interrupt
                 * completions (BUG-0146 bookkeeping) */
                driver_usb_xhci_transfer_event(x, &ev);
            }
        }
        driver_usb_xhci_erdp_update(x);
        if (core_timer_now_ms() > deadline) return OC_USB_ETIMEDOUT;
        core_sched_yield();
    }
}

/* ==================================================================
 * slot management
 * ================================================================== */

static driver_usb_xhci_slot_t *driver_usb_xhci_slot_by_dev(driver_usb_xhci_state_t *x, driver_usb_dev_t *d) {
    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        if (x->slots[i].used && x->slots[i].dev == d)
            return &x->slots[i];
    }
    return NULL;
}

/* endpoint doorbell id for a core ep_addr */
static int driver_usb_xhci_epid(u8 ep_addr) {
    if ((ep_addr & 0x0f) == 0) return 1;             /* ep0 */
    int num = USB_EP_NUM(ep_addr);
    return (ep_addr & 0x80) ? (2 * num + 1) : (2 * num);
}

/* ring index for a doorbell id (0 = ep0, 2..31 -> 1..30); -1 = none.
 * BUG-0153 FIX (A10-30): covers every USB endpoint number 1..15 in both
 * directions (doorbells 2..31); the old 5-ring table rejected ep3+ with
 * an undocumented EINVAL.  Doorbell 31 (ep15 IN) was still wrongly
 * rejected by an off-by-one bound even after the ring expansion. */
static int driver_usb_xhci_ring_idx(int epid) {
    if (epid == 1) return 0;
    if (epid >= 2 && epid <= XHCI_MAX_ERING) return epid - 1;
    return -1;
}

static u8 driver_usb_xhci_core_speed_to_xhci(u8 speed) {
    switch (speed) {
        case USB_SPEED_FS: return 1;
        case USB_SPEED_LS: return 2;
        case USB_SPEED_SS: return 4;
        default:           return 3;
    }
}

/* Route String + root hub port for a device (xHCI spec 6.2.5):
 * the root hub port number lives in slot-context DW1 only; the Route
 * String carries NO root-port bits (it is 0 for devices attached
 * directly to a root port) and each hub tier below the root
 * contributes one 4-bit field whose value IS the hub port number
 * (1-based): bits 7:4 = port on the root-attached hub, bits 11:8 =
 * port on the next hub down the path, and so on.  Without the route
 * string the controller cannot address devices behind an external
 * hub (the old code passed the DOWNSTREAM hub port as the root hub
 * port number, which broke enumeration on external hubs). */
static void driver_usb_xhci_topology_of(const driver_usb_dev_t *d,
                                        u8 *rootport, u32 *route) {
    *rootport = (u8)(d->driver_usb_hub_port + 1);
    *route = 0;
    if (d->parent < 0) return;                   /* root-attached */
    u32 r = 0;
    const driver_usb_dev_t *cur = d;
    while (cur->parent >= 0) {
        const driver_usb_dev_t *ph = driver_usb_get_device(cur->parent);
        if (!ph) break;
        if (ph->parent < 0) {                    /* root-attached hub */
            *rootport = (u8)(ph->driver_usb_hub_port + 1);
        }
        /* collect deepest port first; QEMU/xHCI consume fields from
         * bits 3:0 upward (tier2 first) */
        r = (r << 4) | ((u32)cur->driver_usb_hub_port + 1);
        cur = ph;
    }
    *route = r;
}

/* build the input context for Address Device / Evaluate Context.
 * Slot context layout per the xHCI spec (6.2.2.1): DW0 carries the
 * route string (19:0), port speed (23:20) and context entries (31:27),
 * DW1 the root hub port number (23:16), DW3 the device address (31:24,
 * zero while the device is still on address 0 / BSR=1). */
static void driver_usb_xhci_build_addr_ctx(driver_usb_xhci_state_t *x, driver_usb_xhci_slot_t *s,
                                u8 driver_usb_xhci_speed, u32 mps0) {
    u8 rootport; u32 route;
    driver_usb_xhci_topology_of(s->dev, &rootport, &route);
    u32 st = x->ctx_stride;
    /* BUG-0153 FIX (A10-30): the input context now spans the control
     * context, slot context and up to 31 endpoint contexts (2*64 + 31*64
     * = 2112 bytes with CSZ=1) - zero the whole backing page so no stale
     * bytes leak into high endpoint contexts (the old 1024-byte memset
     * already truncated at CSZ=0 / epid >= 30). */
    memset(s->in_ctx, 0, PMM_PAGE_SIZE);
    u32 *ic = (u32 *)(void *)s->in_ctx;
    ic[0] = 0;                                   /* drop flags */
    ic[1] = (1u << 0) | (1u << 1);               /* add slot + EP0 */
    u32 *sc = (u32 *)(void *)(s->in_ctx + st);
    sc[0] = ((u32)driver_usb_xhci_speed << 20) | (1u << 27)
          | (route & 0xfffffu);                  /* speed, 1 ctx entry, route */
    sc[1] = ((u32)rootport << 16);               /* root hub port number */
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
    /* BUG-0148 FIX (A10-25): Average TRB Length (DW4 15:0, spec 6.2.3)
     * was never written; the spec's recommendation for control
     * endpoints is 8.  It feeds the controller's bandwidth/scheduling
     * estimates (real hosts; QEMU ignores it). */
    ep0[4] = 8u;
}

/* run Address Device (BSR per flag) */
static int driver_usb_xhci_address_slot(driver_usb_xhci_state_t *x, driver_usb_xhci_slot_t *s,
                             u8 driver_usb_xhci_speed, u32 mps0, int bsr) {
    driver_usb_xhci_build_addr_ctx(x, s, driver_usb_xhci_speed, mps0);
    xwr(x, x->op, XHCI_DCBAAP, (u32)(x->dcbaa_phys & 0xffffffffu));
    xwr(x, x->op, XHCI_DCBAAP + 4, (u32)(x->dcbaa_phys >> 32));
    u32 ctrl = (TRB_ADDRESS_DEV << TRB_TYPE_SH) |
               ((u32)s->hw_slot << TRB_SLOT_SH);
    if (bsr) ctrl |= TRB_BSR;
    return driver_usb_xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
}

/* SuperSpeed Max Burst Size for an endpoint, taken from its SuperSpeed
 * Endpoint Companion descriptor (USB 3.0 spec 9.6.2.5: type 0x30, 6
 * bytes, directly follows the endpoint descriptor it belongs to) as
 * captured in the core's raw config block.  Returns bMaxBurst (0-based
 * "packets per burst minus one"); 0 when no companion is present, which
 * per xHCI 6.2.3 is also the required value for endpoints that never
 * burst.  BUG-0148 FIX (A10-25): Max Burst was never programmed. */
static u32 driver_usb_xhci_ss_burst(const driver_usb_dev_t *d, u8 ep_addr) {
    const u8 *p = d->cfg_raw;
    const u8 *end = d->cfg_raw + d->cfg_len;
    u8 last_ep = 0;
    int have_ep = 0;
    if (d->cfg_len == 0) return 0;
    while (p + 2 <= end) {
        u8 len = p[0];
        if (len < 2 || p + len > end) break;
        if (p[1] == USB_DT_ENDPOINT && len >= 7) {
            last_ep = p[2];
            have_ep = 1;
        } else if (p[1] == USB_DT_SS_EP_COMPANION && len >= 6 && have_ep &&
                   last_ep == ep_addr) {
            /* companion layout (USB 3.0 9.6.2.5): bLength, bDescriptorType,
             * bMaxBurst, bmAttributes, wBytesPerInterval - bMaxBurst is
             * offset 2 (the old code returned bmAttributes at offset 3) */
            return p[2];
        }
        p += len;
    }
    return 0;
}

/* lazily configure a non-default endpoint (first use by a class
 * driver).  the ring must already be initialised. */
static int driver_usb_xhci_config_ep(driver_usb_xhci_state_t *x, driver_usb_xhci_slot_t *s, u8 ep_addr) {
    u32 st = x->ctx_stride;
    int epid = driver_usb_xhci_epid(ep_addr);
    int ridx = driver_usb_xhci_ring_idx(epid);
    if (ridx < 0) return OC_USB_EINVAL;
    driver_usb_xhci_ring_t *r = &s->ep[ridx];

    u16 mps = 64;
    u8  attr = USB_EP_ATTR_BULK;
    for (int i = 0; i < s->dev->n_ep; i++) {
        driver_usb_endpoint_t *e = &s->dev->eps[i];
        if (e->addr == ep_addr) {
            mps = e->maxpack ? e->maxpack : mps;
            attr = e->attr;
        }
    }
    /* SuperSpeed devices carry 1024-byte bulk endpoints (the 512 cap
     * is only valid for high-speed); keep the descriptor value when
     * it already fits one transfer */
    if (attr == USB_EP_ATTR_BULK && mps > 1024) mps = 1024;
    /* BUG-0148 FIX (A10-25): the interrupt MPS was clamped to 64 - a
     * made-up limit.  USB 2.0 9.6.6 allows wMaxPacketSize up to 1024
     * for high-speed interrupt endpoints (SuperSpeed too), so devices
     * polling > 64 B per transaction were split into wrong-sized
     * packets.  FS/LS devices cannot legally report more than 64/8, so
     * trusting the descriptor with a field-width cap of 1024 is safe. */
    if (attr == USB_EP_ATTR_INTERRUPT && mps > 1024) mps = 1024;
    if (attr == USB_EP_ATTR_ISO && mps > 1023) mps = 1023;
    /* BUG-0148 FIX (A10-25, EP context fields): xHCI EP type codes per
     * spec Table 6-31 (cross-checked against Linux xhci.h EP_TYPE and
     * QEMU's EPType enum): the OUT types come first, then control, then
     * the IN types: 1=Isoch Out, 2=Bulk Out, 3=Interrupt Out,
     * 4=Control Bidirectional, 5=Isoch In, 6=Bulk In, 7=Interrupt In.
     * The old table paired OUT/IN instead (bulk 3/4, interrupt 6/7,
     * iso 1/2, control 5): QEMU's xhci_submit() rejects type 4 on data
     * endpoints (default: return -1) so bulk IN never completed, type 3
     * put bulk OUT on the periodic kick timer, and real hosts would
     * schedule bulk as periodic / interrupt as bulk. */
    u8 eptype;
    if (attr == USB_EP_ATTR_BULK)
        eptype = (ep_addr & 0x80) ? 6u : 2u;
    else if (attr == USB_EP_ATTR_INTERRUPT)
        eptype = (ep_addr & 0x80) ? 7u : 3u;
    else if (attr == USB_EP_ATTR_ISO)
        eptype = (ep_addr & 0x80) ? 5u : 1u;
    else
        eptype = 4u;

    /* BUG-0153: full-page zeroing - see build_addr_ctx (the input
     * context spans up to 33 context slots) */
    memset(s->in_ctx, 0, PMM_PAGE_SIZE);
    u32 *ic = (u32 *)(void *)s->in_ctx;
    ic[0] = 0;
    ic[1] = (1u << 0) | (1u << epid);            /* add slot + this EP */
    u32 *sc = (u32 *)(void *)(s->in_ctx + st);
    memcpy(sc, s->out_ctx + st, 32);          /* keep speed/port */
    u32 entries = (sc[0] >> 27) & 0x1fu;         /* context entries live
                                                    in slot DW0 31:27 */
    if ((u32)epid > entries)
        sc[0] = (sc[0] & ~(0x1fu << 27)) | ((u32)epid << 27);
    u32 *ec = (u32 *)(void *)(s->in_ctx + (u32)(1 + epid) * st);
    /* EP context layout: see driver_usb_xhci_build_addr_ctx - DW1 carries the
     * EP type and Max Packet Size, DW2 the dequeue pointer + DCS */
    /* BUG-0148 FIX (A10-25): Max Burst Size comes from the SuperSpeed
     * Endpoint Companion descriptor when the device provides one (USB 3.0
     * 9.6.2.5); 0 = one packet per burst, which the spec mandates for
     * non-bursting endpoints.  It was hardwired to 0 even when a
     * companion was present, and the first fix wrote it to DW1[31:24],
     * which OVERLAPS Max Packet Size (DW1 31:16) - for MPS 1024 with
     * burst 3 the controller decoded MPS 1792.  Per xHCI 6.2.3 Max Burst
     * Size lives in DW1 bits 15:8 (Linux MAX_BURST(p) = ((p)&0xff)<<8;
     * QEMU decodes ctx[1]>>8 and multiplies max_psize by burst+1). */
    u32 burst = driver_usb_xhci_ss_burst(s->dev, ep_addr);
    ec[1] = (eptype << 3) | ((u32)mps << 16) |
            ((burst & 0xffu) << 8);                  /* EP state 0 in input */
    ec[2] = (u32)((r->phys & 0xffffffffu) | 1u); /* dequeue low | DCS = 1 */
    ec[3] = (u32)(r->phys >> 32);                /* dequeue high */
    /* BUG-0148 FIX (A10-25): Average TRB Length (DW4 15:0, spec 6.2.3)
     * is a bandwidth-scheduling input real hosts need: control EPs use 8,
     * bulk/interrupt default to the max packet size. Also program the
     * interrupt interval (DW0 23:16) from bInterval instead of leaving 0
     * (= every microframe, wasting scheduling bandwidth). */
    {
        u32 avg = mps;
        if (eptype == 4u) avg = 8;   /* control: spec-recommended 8 */
        ec[4] = avg & 0xffffu;
        if (attr == USB_EP_ATTR_INTERRUPT) {
            u8 biv = 0;
            for (int i = 0; i < s->dev->n_ep; i++)
                if (s->dev->eps[i].addr == ep_addr) biv = s->dev->eps[i].interval;
            if (biv == 0) biv = 3;
            u32 iv = (u32)(biv - 1); if (iv > 15) iv = 15;
            ec[0] = iv << 16;
        }
    }

    u32 ctrl = (TRB_CONFIG_EP << TRB_TYPE_SH) |
               ((u32)s->hw_slot << TRB_SLOT_SH);
    int rc = driver_usb_xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
    if (rc == 0) r->configured = 1;
    return rc;
}

/* reset an endpoint after STALL + CLEAR_FEATURE(ENDPOINT_HALT) */
int driver_usb_xhci_reset_ep(driver_usb_dev_t *d, u8 ep_addr) {
    for (int i = 0; i < g_n_xhci; i++) {
        driver_usb_xhci_state_t *x = &g_xhci[i];
        if (!x->up) continue;
        driver_usb_xhci_slot_t *s = driver_usb_xhci_slot_by_dev(x, d);
        if (!s) continue;
        int epid = driver_usb_xhci_epid(ep_addr);
        int ridx = driver_usb_xhci_ring_idx(epid);
        if (ridx < 0) return OC_USB_EINVAL;
        driver_usb_xhci_ring_t *r = &s->ep[ridx];
        if (!s->halted[ridx]) return 0;
        /* Reset Endpoint / Set TR Dequeue: Endpoint ID lives in
         * control bits 20:16 (xHCI 1.2 Table 6-42/6-44) and the slot
         * in bits 31:24.  An EPID of 0 is invalid - the controller
         * retires such a command with TRB Error and the endpoint
         * stays halted forever. */
        u32 slotf = ((u32)s->hw_slot << TRB_SLOT_SH) |
                    ((u32)epid << 16);
        /* 1. Reset Endpoint (clears the halt in the controller) */
        driver_usb_xhci_do_cmd(x, 0, 0, (TRB_RESET_EP << TRB_TYPE_SH) | slotf,
                    1000);
        /* 2. re-position the ring on the next TRB we will fill */
        u64 deq = r->phys + (u64)r->enq * 16;
        driver_usb_xhci_do_cmd(x, deq | (u64)r->cycle, 0,
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
static int driver_usb_xhci_wait_trb(driver_usb_xhci_state_t *x, u64 trb_phys, u32 *cc_out,
                         u32 *rem_out, u32 timeout_ms) {
    u64 deadline = core_timer_now_ms() + timeout_ms;
    for (;;) {
        xtrb_t ev;
        while (driver_usb_xhci_next_event(x, &ev)) {
            u32 type = (ev[3] >> TRB_TYPE_SH) & 0x3fu;
            driver_usb_xhci_erdp_update(x);
            if (type != TRB_EV_TRANSFER) continue;
            /* retire the completed TRB and latch pending interrupt
             * completions (shared bookkeeping, BUG-0146) */
            driver_usb_xhci_transfer_event(x, &ev);
            /* Transfer Event: TL = DW2[23:0], CC = DW2[31:24],
             * Slot ID = DW3[31:24] */
            u32 cc = (ev[2] >> 24) & 0xffu;
            u64 ptr = ((u64)ev[0] | ((u64)ev[1] << 32)) & ~0xfull;
            if (ptr == trb_phys) {
                if (cc_out) *cc_out = cc;
                if (rem_out) *rem_out = ev[2] & 0xffffffu;
                return 1;
            }
        }
        driver_usb_xhci_erdp_update(x);
        if (core_timer_now_ms() > deadline) return 0;
        core_sched_yield();
    }
}

/* release a slot: DISABLE the hardware slot, clear its DCBAA entry and
 * free every per-device frame.  BUG-0151 FIX (A10-28): used by the
 * hot-unplug reaper, the ENOMEM path and the ADDRESS_DEVICE failure
 * path; the old code only did this (partially) in the reaper and
 * leaked 3 of the per-device pages (bulk / ctl+int / iso bounce) there
 * and everything on the ADDRESS failure path. */
static void driver_usb_xhci_slot_release(driver_usb_xhci_state_t *x,
                                         driver_usb_xhci_slot_t *s) {
    if (s->hw_slot) {
        /* best effort: a wedged HC cannot retire the command, but the
         * pages below must go back regardless */
        driver_usb_xhci_do_cmd(x, 0, 0,
            (TRB_DISABLE_SLOT << TRB_TYPE_SH) |
            ((u32)s->hw_slot << TRB_SLOT_SH), 200);
        if (x->dcbaa) {
            volatile u64 *dc = (volatile u64 *)(void *)x->dcbaa;
            dc[s->hw_slot] = 0;
        }
    }
    if (s->out_ctx_phys) mem_pmm_free_frame(s->out_ctx_phys);
    if (s->in_ctx_phys) mem_pmm_free_frame(s->in_ctx_phys);
    if (s->ring_page_phys) mem_pmm_free_frame(s->ring_page_phys);
    for (int pg = 0; pg < 7; pg++)
        if (s->ring_extra_phys[pg])
            mem_pmm_free_frame(s->ring_extra_phys[pg]);
    if (s->bulk_buf_phys) mem_pmm_free_frame(s->bulk_buf_phys);
    if (s->iso_buf_phys) mem_pmm_free_frame(s->iso_buf_phys);
    if (s->ctl_buf_phys) mem_pmm_free_frame(s->ctl_buf_phys); /* + int_buf */
    if (s->ep) { kfree(s->ep); s->ep = 0; }
    memset(s, 0, sizeof(*s));
}

/* ==================================================================
 * host ops
 * ================================================================== */

/* lazily bring a slot up on the first control transfer (the core's
 * enumeration starts with GET_DESCRIPTOR at address 0) */
static driver_usb_xhci_slot_t *driver_usb_xhci_ensure_slot(driver_usb_xhci_state_t *x, driver_usb_dev_t *d,
                                     int *err) {
    driver_usb_xhci_slot_t *s = driver_usb_xhci_slot_by_dev(x, d);
    if (s) { *err = 0; return s; }
    if (d->slot < 0 || d->slot >= XHCI_MAX_SLOTS) { *err = -1; return NULL; }
    s = &x->slots[d->slot];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->dev = d;

    /* enable a slot */
    int rc = driver_usb_xhci_do_cmd(x, 0, 0, TRB_ENABLE_SLOT << TRB_TYPE_SH, 1000);
    if (rc != 0 || x->cmd_slot == 0) {
        screen_console_puts("xhci: ENABLE_SLOT failed\n");
        s->used = 0;
        *err = -1;
        return NULL;
    }
    s->hw_slot = (int)x->cmd_slot;

    /* allocate contexts + rings.
     * BUG-0151 FIX (A10-28): ring storage is now 8 pages (31 rings x 1 KiB
     * for full ep1..15 support, BUG-0153) and every partial-failure path
     * releases what it got. */
    u64 p1 = mem_pmm_alloc_frame();   /* out ctx */
    u64 p2 = mem_pmm_alloc_frame();   /* in ctx  */
    u64 p3 = mem_pmm_alloc_frame();   /* ring page 0 */
    u64 p4 = mem_pmm_alloc_frame();   /* bulk bounce */
    u64 p5 = mem_pmm_alloc_frame();   /* ctl + int bounce */
    u64 p6 = mem_pmm_alloc_frame();   /* iso bounce (BUG-0152: not shared) */
    u64 p7 = mem_pmm_alloc_frame();   /* ring page 1 */
    u64 p8 = mem_pmm_alloc_frame();   /* ring page 2 */
    u64 p9 = mem_pmm_alloc_frame();   /* ring page 3 */
    u64 p10 = mem_pmm_alloc_frame();  /* ring page 4 */
    u64 p11 = mem_pmm_alloc_frame();  /* ring page 5 */
    u64 p12 = mem_pmm_alloc_frame();  /* ring page 6 */
    u64 p13 = mem_pmm_alloc_frame();  /* ring page 7 */
    u64 ring_extra[7];
    ring_extra[0]=p7; ring_extra[1]=p8; ring_extra[2]=p9;
    ring_extra[3]=p10; ring_extra[4]=p11; ring_extra[5]=p12;
    ring_extra[6]=p13;
    s->ep = kmalloc(sizeof(driver_usb_xhci_ring_t) * XHCI_MAX_ERING);
    if (!p1 || !p2 || !p3 || !p4 || !p5 || !p6 || !p7 || !p8 || !p9 ||
        !p10 || !p11 || !p12 || !p13 || !s->ep) {
        /* BUG-0151 FIX (A10-28): DISABLE the hardware slot and release
         * every page we got; the old path leaked the hw slot AND the
         * pages, so repeated failures ate MaxSlots and PMM permanently. */
        if (s->hw_slot) {
            driver_usb_xhci_do_cmd(x, 0, 0,
                (TRB_DISABLE_SLOT << TRB_TYPE_SH) |
                ((u32)s->hw_slot << TRB_SLOT_SH), 200);
        }
        if (p1) mem_pmm_free_frame(p1);
        if (p2) mem_pmm_free_frame(p2);
        if (p3) mem_pmm_free_frame(p3);
        if (p4) mem_pmm_free_frame(p4);
        if (p5) mem_pmm_free_frame(p5);
        if (p6) mem_pmm_free_frame(p6);
        for (int i = 0; i < 7; i++)
            if (ring_extra[i]) mem_pmm_free_frame(ring_extra[i]);
        if (s->ep) { kfree(s->ep); s->ep = 0; }
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
    for (int i = 0; i < 7; i++) s->ring_extra_phys[i] = ring_extra[i];
    s->bulk_buf = (u8 *)(uintptr_t)p4;
    s->bulk_buf_phys = p4;
    s->iso_buf = (u8 *)(uintptr_t)p6;
    s->iso_buf_phys = p6;
    s->ctl_buf = (u8 *)(uintptr_t)p5;
    s->ctl_buf_phys = p5;
    s->int_buf = (u8 *)(uintptr_t)(p5 + 512);
    memset(s->out_ctx, 0, PMM_PAGE_SIZE);
    memset(s->in_ctx, 0, PMM_PAGE_SIZE);
    memset(s->ring_page, 0, PMM_PAGE_SIZE);
    memset(s->bulk_buf, 0, PMM_PAGE_SIZE);
    memset(s->iso_buf, 0, PMM_PAGE_SIZE);
    /* ctl_buf (512 B) + one 64 B latch slice per non-default ring
     * (BUG-0153: 30 slices now; 512 + 30*64 = 2432 B of the page) */
    memset((void *)(uintptr_t)p5, 0, 512 + XHCI_INT_LATCHES * 64);

    driver_usb_xhci_ring_init(&s->ep[0], (xtrb_t *)(void *)s->ring_page, p3);
    {
        /* rings live contiguously across the 8 allocated pages */
        u64 page_phys[8];
        u8 *page_virt[8];
        page_phys[0]=p3; page_phys[1]=p7; page_phys[2]=p8; page_phys[3]=p9;
        page_phys[4]=p10; page_phys[5]=p11; page_phys[6]=p12; page_phys[7]=p13;
        page_virt[0]=s->ring_page;
        for (int pg = 1; pg < 8; pg++)
            page_virt[pg] = (u8*)(uintptr_t)page_phys[pg];
        for (int i = 1; i < XHCI_MAX_ERING; i++) {
            u64 gp = (u64)i * 1024;
            u64 which = gp >> 12;            /* 1 KiB -> 4 rings per page */
            u64 off = gp & 0xfff;
            driver_usb_xhci_ring_init(&s->ep[i],
                           (xtrb_t *)(void *)(page_virt[which] + off),
                           page_phys[which] + off);
        }
    }

    /* DCBAA[hw_slot] = output ctx (the HC writes the output context
     * there when Address Device completes) */
    volatile u64 *dc = (volatile u64 *)(void *)x->dcbaa;
    dc[s->hw_slot] = s->out_ctx_phys;

    /* Address Device with BSR=1: the device stays at address 0 and
     * the core reads desc8 through EP0 next.  MPS for the default
     * endpoint is fixed by the port speed (spec 6.2.3.1). */
    u8 xsp = driver_usb_xhci_core_speed_to_xhci(d->speed);
    u32 bsr_mps = (d->speed == USB_SPEED_SS) ? 512u :
                  (d->speed == USB_SPEED_LS) ? 8u : 64u;
    rc = driver_usb_xhci_address_slot(x, s, xsp, bsr_mps, 1);
    if (rc != 0) {
        screen_console_puts("xhci: ADDRESS_DEVICE(BSR) failed\n");
        /* BUG-0151 FIX (A10-28): this path left the ENABLED hardware slot
         * AND all 13 frames behind on every failure; release everything
         * like the reaper does. */
        driver_usb_xhci_slot_release(x, s);
        *err = -1;
        return NULL;
    }
    *err = 0;
    return s;
}

static int driver_usb_xhci_control(driver_usb_host_t *h, driver_usb_dev_t *d,
                        const driver_usb_setup_t *setup, void *buf, u16 len,
                        u32 timeout_ms) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    if (!x || !x->up || !d) return -1;
    int err;
    driver_usb_xhci_slot_t *s = driver_usb_xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    if (len > 512) return OC_USB_EINVAL;

    /* XHCI does SET_ADDRESS natively: intercept the core's request */
    if (setup->bmRequestType == 0x00 &&
        setup->bRequest == USB_REQ_SET_ADDR && !s->addressed) {
        u32 mps = d->mps0 ? d->mps0 : 8;
        s->mps0_prog = mps;
        int rc = driver_usb_xhci_address_slot(x, s,
                                   driver_usb_xhci_core_speed_to_xhci(d->speed),
                                   mps, 0);
        if (rc == 0) s->addressed = 1;
        return rc;
    }
    /* MPS0 discovery corrected the value: re-evaluate the context */
    if (s->addressed && d->mps0 && d->mps0 != s->mps0_prog) {
        driver_usb_xhci_build_addr_ctx(x, s, driver_usb_xhci_core_speed_to_xhci(d->speed),
                            d->mps0);
        u32 ctrl = (TRB_EVALUATE_CTX << TRB_TYPE_SH) |
                   ((u32)s->hw_slot << TRB_SLOT_SH);
        driver_usb_xhci_do_cmd(x, s->in_ctx_phys, 0, ctrl, 1000);
        s->mps0_prog = d->mps0;
    }

    /* SETUP (+DATA +STATUS) on the EP0 ring */
    driver_usb_xhci_ring_t *r = &s->ep[0];
    u8 dir_in = (setup->bmRequestType & 0x80) ? 1 : 0;
    u64 setup_param = 0;
    memcpy(&setup_param, setup, 8);
    /* BUG-0147 FIX (A10-24): Setup Stage TRB DW3 bits 17:16 = TRT
     * (Transfer Type, xHCI spec 6.4.1.2.1 Table 6-9): 0 = no data
     * stage, 2 = OUT data stage, 3 = IN data stage (cross-checked
     * against Linux TRB_TX_TYPE: TRB_DATA_OUT=2 / TRB_DATA_IN=3; the
     * first fix wrote the inverted 1/2 pair and 1 is Reserved - real
     * hosts reject it).  TRT only exists from xHCI 1.0 (HCIVERSION
     * halfword at CAPLENGTH+2, exactly like Linux's hci_version test);
     * 0.96 hosts leave it 0.  QEMU (HCIVERSION 0x0100) sequences TRBs
     * without validating TRT, so either encoding runs there; a real
     * host rejects a reserved TRT or mis-sequences the transfer. */
    u32 trt = 0;
    if (len != 0 && (x->cap[0] >> 16) >= 0x100)
        trt = dir_in ? (3u << 16) : (2u << 16);
    /* BUG-0146 (A10-23): a control TD is 2-3 TRBs; refuse up front
     * rather than overwrite TRBs the controller has not consumed yet
     * (xHCI 4.9.3) when the EP0 ring ever fills. */
    if (r->in_flight + 3 > XHCI_RING_N - 1) return OC_USB_ENOMEM;
    u64 setup_phys = driver_usb_xhci_ring_put(r, setup_param, 8,
                  (TRB_SETUP_STAGE << TRB_TYPE_SH) | TRB_IDT | TRB_CH | trt);
    u64 tr_last = 0;
    if (setup_phys) {
        if (len == 0) {
            tr_last = driver_usb_xhci_ring_put(r, 0, 0,
                                    (TRB_STATUS_STAGE << TRB_TYPE_SH) |
                                    (dir_in ? 0u : TRB_DIR_IN) | TRB_IOC);
        } else {
            u8 *bounce = s->ctl_buf;
            if (!dir_in) memcpy(bounce, buf, len);
            u64 data_phys = driver_usb_xhci_ring_put(r, (u64)(uintptr_t)bounce, len,
                              (TRB_DATA_STAGE << TRB_TYPE_SH) |
                              (dir_in ? TRB_DIR_IN : 0u) | TRB_CH);
            if (data_phys)
                tr_last = driver_usb_xhci_ring_put(r, 0, 0,
                                        (TRB_STATUS_STAGE << TRB_TYPE_SH) |
                                        (dir_in ? 0u : TRB_DIR_IN) | TRB_IOC);
        }
    }
    if (!setup_phys || !tr_last) return OC_USB_ENOMEM;
    driver_usb_xhci_doorbell(x, (u32)s->hw_slot, 1);   /* slot doorbell, EP0 */

    u32 cc = 0, rem = 0;
    int got = driver_usb_xhci_wait_trb(x, tr_last, &cc, &rem, timeout_ms);
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
        memcpy(buf, s->ctl_buf, (u32)((u32)len - rlen));
    }
    return 0;
}

static int driver_usb_xhci_bulk(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                     void *buf, u16 len, u32 timeout_ms) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !buf) return -1;
    if (len == 0) return 0;
    if (len > 4096) return OC_USB_EINVAL;   /* single bounce page */
    int err;
    driver_usb_xhci_slot_t *s = driver_usb_xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = driver_usb_xhci_epid(ep_addr);
    int ridx = driver_usb_xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    driver_usb_xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = driver_usb_xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    if (s->halted[ridx]) driver_usb_xhci_reset_ep(d, ep_addr);

    u8 dir_in = (ep_addr & 0x80) ? 1 : 0;
    /* BUG-0152 hardening (A10-29): after a timed-out bulk the HC still
     * owns bulk_buf through its pending TRB; refuse the new transfer
     * instead of corrupting the in-flight DMA.  poll() retires the
     * pending event via transfer_event, which clears in_flight again. */
    if (r->in_flight != 0) return OC_USB_EIO;
    memcpy(s->bulk_buf, buf, len);
    u64 tr = driver_usb_xhci_ring_put(r, (u64)(uintptr_t)s->bulk_buf, len,
                           (TRB_NORMAL << TRB_TYPE_SH) | TRB_IOC);
    if (!tr) return OC_USB_ENOMEM;   /* BUG-0146: ring full, HC behind */
    driver_usb_xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);

    u32 cc = 0, rem = 0;
    int got = driver_usb_xhci_wait_trb(x, tr, &cc, &rem, timeout_ms);
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
    if (dir_in) memcpy(buf, s->bulk_buf, (u32)moved);
    return moved;
}

static int driver_usb_xhci_interrupt(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                          void *buf, u16 len, u32 timeout_ms) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !buf) return -1;
    if (len == 0 || len > 64) return OC_USB_EINVAL;
    int err;
    driver_usb_xhci_slot_t *s = driver_usb_xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = driver_usb_xhci_epid(ep_addr);
    int ridx = driver_usb_xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    driver_usb_xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = driver_usb_xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    if (s->halted[ridx]) driver_usb_xhci_reset_ep(d, ep_addr);

    int latch = ridx - 1;      /* 0..XHCI_INT_LATCHES-1, one per data ring */
    u8 dir_in = (ep_addr & 0x80) ? 1 : 0;

    /* latched event from an earlier poll? */
    if (dir_in && s->latch_valid[latch]) {
        s->latch_valid[latch] = 0;
        memcpy(buf, s->int_buf + latch * 64, len);
        return (int)s->latch_len[latch];
    }
    if (s->int_pending[latch]) {
        /* a TRB is already in flight; wait for it to finish */
        u32 cc = 0, rem = 0;
        int got = driver_usb_xhci_wait_trb(x, s->int_trb_phys[latch], &cc, &rem,
                                timeout_ms);
        if (!got) return OC_USB_ENAK;
        s->int_pending[latch] = 0;
        if (cc == CC_STALL) {
            s->halted[ridx] = 1;
            return OC_USB_ESTALL;
        }
        if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
        if (dir_in) memcpy(buf, s->int_buf + latch * 64, len);
        return (int)len;
    }

    /* submit one interrupt TRB; if the device NAKs the whole window
     * the TRB stays pending and its event gets latched later */
    if (!dir_in) memcpy(s->int_buf + latch * 64, buf, len);
    else memset(s->int_buf + latch * 64, 0, 64);
    s->latch_len[latch] = len;
    u64 tr = driver_usb_xhci_ring_put(r, (u64)(uintptr_t)(s->int_buf + latch * 64),
                           len, (TRB_NORMAL << TRB_TYPE_SH) | TRB_IOC);
    if (!tr) return OC_USB_ENOMEM;   /* BUG-0146: ring full, HC behind */
    s->int_trb_phys[latch] = tr;
    s->int_pending[latch] = 1;
    driver_usb_xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);

    u32 cc = 0, rem = 0;
    int got = driver_usb_xhci_wait_trb(x, tr, &cc, &rem, timeout_ms);
    if (!got) return OC_USB_ENAK;     /* TRB stays pending (latched) */
    s->int_pending[latch] = 0;
    if (cc == CC_STALL) {
        s->halted[ridx] = 1;
        return OC_USB_ESTALL;
    }
    if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) return OC_USB_EIO;
    if (dir_in) memcpy(buf, s->int_buf + latch * 64, len);
    return (int)len;
}

static int driver_usb_xhci_iso_out(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                        const void *data, u16 len) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    if (!x || !x->up || !d || !d->present) return -1;
    if (len > 1023) return -1;
    int err;
    driver_usb_xhci_slot_t *s = driver_usb_xhci_ensure_slot(x, d, &err);
    if (!s) return err;
    int epid = driver_usb_xhci_epid(ep_addr);
    int ridx = driver_usb_xhci_ring_idx(epid);
    if (ridx < 1) return OC_USB_EINVAL;
    driver_usb_xhci_ring_t *r = &s->ep[ridx];
    if (!r->configured) {
        int rc = driver_usb_xhci_config_ep(x, s, ep_addr);
        if (rc != 0) return OC_USB_EIO;
    }
    /* BUG-0152 FIX (A10-29): ISO OUT uses its OWN bounce page (iso_buf,
     * not the bulk page).  The shared bulk_buf meant a fire-and-forget
     * ISO TRB still owned the same page a concurrent bulk/interrupt
     * transfer memcpy'd into - the HC DMA'd corrupted audio (or the
     * bulk transfer got audio).  The global usb lock cannot protect a
     * buffer the hardware is still consuming; only a private buffer
     * can.  BUG-0146: while a previous fire-and-forget ISO TRB is still
     * in flight on this ring it still OWNS iso_buf, so a new packet is
     * dropped instead of overwriting it (same outcome as the 5 ms
     * timeout path; audio re-arms at the 1 ms frame cadence). */
    if (r->in_flight != 0) return 0;
    if (data && len) memcpy(s->iso_buf, data, len);
    u64 tr = driver_usb_xhci_ring_put(r, (u64)(uintptr_t)s->iso_buf, len,
                           (TRB_ISOCH << TRB_TYPE_SH) | TRB_IOC);
    if (!tr) return 0;    /* ring full: drop, do not corrupt the owner */
    driver_usb_xhci_doorbell(x, (u32)s->hw_slot, (u32)epid);
    /* wait a few ms so the event ring stays clean; audio re-arms at
     * the 1 ms frame cadence anyway */
    u32 cc = 0, rem = 0;
    int got = driver_usb_xhci_wait_trb(x, tr, &cc, &rem, 5);
    if (!got) return 0;    /* fire-and-forget; the poll drains it */
    if (cc == CC_STALL) {
        s->halted[ridx] = 1;
        return OC_USB_ESTALL;
    }
    return 0;
}

static int driver_usb_xhci_iso_in(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                       void *buf, u16 len) {
    (void)h; (void)d; (void)ep_addr; (void)buf; (void)len;
    return OC_USB_EINVAL;   /* no current class driver consumes ISO IN */
}

/* ---- root hub ---- */

static int driver_usb_xhci_port_count(driver_usb_host_t *h) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    return x ? x->n_ports : 0;
}

static int driver_usb_xhci_port_status(driver_usb_host_t *h, int port,
                            driver_usb_port_status_t *out) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
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

static int driver_usb_xhci_port_reset(driver_usb_host_t *h, int port, u8 *speed_out) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
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
        strcpy(l, "xhci: port reset fail ccs=0 w=0x");
        u64_to_hex(w, n, 8); strcat(l, n);
        strcat(l, "\n");
        screen_console_puts(l);
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
static void driver_usb_xhci_reap_slots(driver_usb_xhci_state_t *x) {
    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        driver_usb_xhci_slot_t *s = &x->slots[i];
        if (!s->used) continue;
        if (s->dev && !s->dev->present)
            /* BUG-0151 FIX (A10-28): slot_release DISABLEs the hardware
             * slot, clears the DCBAA entry and frees ALL 13 per-slot
             * frames (out/in ctx, 8 ring pages, bulk, iso, ctl+int) plus
             * the kmalloc'd ring array; the old reaper leaked the bulk,
             * iso and ctl/int pages on every hot-unplug. */
            driver_usb_xhci_slot_release(x, s);
    }
}

static int driver_usb_xhci_poll(driver_usb_host_t *h) {
    driver_usb_xhci_state_t *x = (driver_usb_xhci_state_t *)h->priv;
    if (!x || !x->up) return 0;
    xtrb_t ev;
    while (driver_usb_xhci_next_event(x, &ev)) {
        driver_usb_xhci_erdp_update(x);
        u32 type = (ev[3] >> TRB_TYPE_SH) & 0x3fu;
        if (type == TRB_EV_TRANSFER)
            /* BUG-0046/BUG-0146: transfer events must be accounted even
             * on the idle poll path - transfer_event retires the TRB
             * (producer in-flight tracking) and latches completions of
             * interrupt TRBs parked by a NAK, exactly like wait_trb.  An
             * idle system would otherwise drop the completion, leave
             * int_pending set and the endpoint dead after any idle gap. */
            driver_usb_xhci_transfer_event(x, &ev);
    }
    driver_usb_xhci_reap_slots(x);
    return 0;
}

static const driver_usb_hc_ops_t driver_usb_xhci_ops_tmpl = {
    .name      = "XHCI",
    .bulk_max  = 4096,   /* single bounce page per transfer */
    .control   = driver_usb_xhci_control,
    .bulk      = driver_usb_xhci_bulk,
    .interrupt = driver_usb_xhci_interrupt,
    .driver_usb_iso_out   = driver_usb_xhci_iso_out,
    .driver_usb_iso_in    = driver_usb_xhci_iso_in,
    .port_count  = driver_usb_xhci_port_count,
    .port_status = driver_usb_xhci_port_status,
    .port_reset  = driver_usb_xhci_port_reset,
    .poll        = driver_usb_xhci_poll,
};

/* ==================================================================
 * init / probe
 * ================================================================== */

static int driver_usb_xhci_probe_one(u8 bus, u8 dev, u8 func) {
    if (g_n_xhci >= XHCI_MAX_CTRL) return -1;
    driver_usb_xhci_state_t *x = &g_xhci[g_n_xhci];
    memset(x, 0, sizeof(*x));

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (bar0 & 1) {
        driver_usb_xhci_log("xhci: BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    /* BUG-0150 FIX (A10-27): xHCI BAR0 is commonly a 64-bit MMIO BAR.
     * Per the PCI Local Bus spec 6.2.5.1 the memory-BAR bits 2:1 encode
     * the width (00 = 32-bit, 10 = 64-bit) and a 64-bit BAR's upper 32
     * address bits live in the NEXT config dword (BAR1, offset 0x14).
     * driver_pci_read_bar() returns only the low word, so a controller
     * mapped above 4 GiB would silently truncate its MMIO base here and
     * the driver would do volatile reads/writes at a wrong physical
     * address (real #PF / bus error risk).  Refuse to probe in that
     * case; a 64-bit-typed BAR whose current assignment still fits
     * below 4 GiB (high word 0 - the common QEMU case) probes normally
     * because the low word is then the exact address. */
    u32 bar_type = (bar0 >> 1) & 0x3u;
    if (bar_type == 0x1u || bar_type == 0x3u) {
        driver_usb_xhci_log("xhci: BAR0 uses a reserved memory type\n");
        return -1;
    }
    if (bar_type == 0x2u) {
        u32 bar_hi = driver_pci_read_config(bus, dev, func, 0x14);
        if (bar_hi != 0) {
            char l[80], n[16];
            strcpy(l, "xhci: BAR0 is 64-bit above 4GiB (high=0x");
            u64_to_hex((u64)bar_hi, n, 8);
            strcat(l, n);
            strcat(l, "), refusing probe\n");
            screen_console_puts(l);
            return -1;
        }
    }
    u32 mmio = bar0 & 0xFFFFF000u;
    if (!mmio) return -1;
    x->cap = (volatile u32 *)(uintptr_t)mmio;
    x->bus = bus; x->dev = dev; x->func = func;
    driver_pci_enable_device(bus, dev, func);

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
        driver_usb_xhci_log("xhci: controller without 64-bit addressing\n");
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
        driver_usb_xhci_log("xhci: controller reset timeout\n");
        return -1;
    }

    /* scratchpad buffers */
    u32 hsp2 = x->cap[XHCI_HCSPARAMS2 / 4];
    u32 n_scratch = (hsp2 >> 27) & 0x1fu;
    if (hsp2 & (1u << 26)) n_scratch += 32;
    /* BUG-0149 FIX (A10-26): MaxScratchpadBuffers = bits31:27 +
     * SPBHC(bit26)*32, max 63 (spec 5.3.7). The old hard cap of 32 left
     * DCBAA[0] entries for buffers 33..63 pointing at garbage on
     * controllers that need them (real hardware exists; qemu-xhci needs
     * 0 so it never showed). */
    if (n_scratch > 63) {
        screen_console_puts("xhci: insane scratchpad count\n");
        return -1;
    }
    if (n_scratch) {
        u64 arr = mem_pmm_alloc_frame();
        if (!arr) {
            driver_usb_xhci_log("xhci: no page for scratchpad array\n");
            goto fail_up;
        }
        x->scratch_arr_phys = arr;   /* record now: fail_up frees it */
        u64 *ent = (u64 *)(uintptr_t)arr;
        memset(ent, 0, PMM_PAGE_SIZE);
        for (u32 i = 0; i < n_scratch; i++) {
            u64 p = mem_pmm_alloc_frame();
            if (!p) {
                /* BUG-0151 FIX (A10-28): the old mid-loop bailout left
                 * every scratchpad page and the array leaked */
                driver_usb_xhci_log("xhci: scratchpad page alloc failed\n");
                goto fail_up;
            }
            memset((void *)(uintptr_t)p, 0, PMM_PAGE_SIZE);
            ent[i] = p;
        }
    }

    /* DCBAA */
    u64 dc = mem_pmm_alloc_frame();
    if (!dc) {
        driver_usb_xhci_log("xhci: no page for DCBAA\n");
        goto fail_up;
    }
    x->dcbaa = (u8 *)(uintptr_t)dc;
    x->dcbaa_phys = dc;
    memset(x->dcbaa, 0, PMM_PAGE_SIZE);
    if (x->scratch_arr_phys)
        ((volatile u64 *)(void *)x->dcbaa)[0] = x->scratch_arr_phys;

    /* command ring */
    u64 cr = mem_pmm_alloc_frame();
    if (!cr) {
        driver_usb_xhci_log("xhci: no page for command ring\n");
        goto fail_up;
    }
    x->cmd = (xtrb_t *)(uintptr_t)cr;
    x->cmd_phys = cr;
    memset((void *)x->cmd, 0, PMM_PAGE_SIZE);
    driver_usb_xhci_trb_set(&x->cmd[XHCI_RING_N - 1], cr, 0,
                 (TRB_LINK << TRB_TYPE_SH) | TRB_TC | TRB_C);
    x->cmd_enq = 0;
    x->cmd_cycle = 1;
    x->cmd_in_flight = 0;

    /* event ring + segment table (one page: ERST at 0, ring at 64) */
    u64 evp = mem_pmm_alloc_frame();
    if (!evp) {
        driver_usb_xhci_log("xhci: no page for event ring\n");
        goto fail_up;
    }
    x->ev = (xtrb_t *)(uintptr_t)(evp + 64);
    x->ev_phys = evp + 64;
    x->erst = (u32 *)(uintptr_t)evp;
    x->erst_phys = evp;
    memset((void *)(uintptr_t)evp, 0, PMM_PAGE_SIZE);
    x->erst[0] = (u32)(x->ev_phys & 0xffffffffu);
    x->erst[1] = (u32)(x->ev_phys >> 32);
    x->erst[2] = XHCI_RING_N;
    x->erst[3] = 0;
    /* BUG-0045 FIX: write the LINK TRB at the end of the event ring so
     * hardware and software agree on the ring layout, exactly like the
     * command ring above. The old init left slot 63 zeroed while the
     * consumer still stepped over it as if a LINK were there — on real
     * hardware the segment simply ends after 63 events (every command
     * and transfer then times out); under QEMU one event out of every
     * 64 was lost. Cycle = 1 to match the ring's initial cycle state. */
    driver_usb_xhci_trb_set(&x->ev[XHCI_RING_N - 1], x->ev_phys, 0,
                 (TRB_LINK << TRB_TYPE_SH) | TRB_TC | TRB_C);
    x->ev_deq = 0;
    x->ev_cycle = 1;

    /* publish everything, then run */
    xwr(x, x->op, XHCI_CRCR, (u32)(cr | 1u));          /* CRCR + cycle */
    xwr(x, x->op, XHCI_CRCR + 4, (u32)(cr >> 32));
    {
        char dl[96], dn[16];
        u32 rb0 = xrd(x, x->op, XHCI_CRCR);
        u32 rb1 = xrd(x, x->op, XHCI_CRCR + 4);
        strcpy(dl, "xhci: CRCR readback 0x");
        u64_to_hex(((u64)rb1 << 32) | rb0, dn, 8);
        strcat(dl, dn); strcat(dl, " op=0x");
        u64_to_hex((u64)(uintptr_t)x->op, dn, 8);
        strcat(dl, dn); strcat(dl, "\n");
        screen_console_puts(dl);
    }
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
    driver_usb_xhci_erdp_update(x);
    xwr(x, x->run, XHCI_IMAN(0), 2u);   /* IE=1; polling is primary */

    xwr(x, x->op, XHCI_USBCMD, xrd(x, x->op, XHCI_USBCMD) | XHCI_CMD_RS);
    for (int i = 0; i < 3000; i++) {
        if (!(xrd(x, x->op, XHCI_USBSTS) & XHCI_STS_HCH)) break;
        for (volatile int t = 0; t < 1000; t++) { }
    }
    /* optional IRQ (polling stays the primary path) */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    x->irq = -1;
    if (irq < 16 &&
        arch_irq_register_handler(irq, driver_usb_xhci_irq_handler, x) == 0)
        x->irq = irq;

    x->up = 1;

    driver_usb_host_t *h = &g_xhci_host[g_n_xhci];
    memset(h, 0, sizeof(*h));
    strcpy(h->name, "xhci0");
    if (g_n_xhci > 0) h->name[4] = (char)('0' + g_n_xhci);
    h->bus = bus; h->dev = dev; h->func = func;
    h->flags = USB_HOSTF_NATIVE_ADDR;
    h->priv = x;
    g_xhci_ops[g_n_xhci] = driver_usb_xhci_ops_tmpl;
    h->ops = &g_xhci_ops[g_n_xhci];
    driver_usb_register_host(h, &g_xhci_ops[g_n_xhci]);
    g_n_xhci++;
    return 0;

fail_up:
    /* BUG-0151 FIX (A10-28): init-failure paths release every frame
     * allocated so far; the old probe leaked the scratchpad pages,
     * DCBAA, command ring and event ring page on the way down.  The
     * scratchpad buffers are reachable through the array itself (filled
     * entries are contiguous and non-zero), so they are freed first. */
    if (x->scratch_arr_phys) {
        u64 *e = (u64 *)(uintptr_t)x->scratch_arr_phys;
        for (int i = 0; i < 64 && e[i]; i++)
            mem_pmm_free_frame(e[i]);
        mem_pmm_free_frame(x->scratch_arr_phys);
    }
    if (x->dcbaa_phys) mem_pmm_free_frame(x->dcbaa_phys);
    if (x->cmd_phys) mem_pmm_free_frame(x->cmd_phys);
    if (x->erst_phys) mem_pmm_free_frame(x->erst_phys);
    memset(x, 0, sizeof(*x));
    driver_usb_xhci_log("xhci: probe failed, allocated frames released\n");
    return -1;
}

int driver_usb_xhci_probe_all(void) {
    int n = 0;
    u8 b, dv, fn;
    /* by class 0x0c0330 (covers qemu-xhci, nec-usb-xhci, real HW) */
    for (int nth = 0; nth < 4; nth++) {
        if (driver_pci_find_class_exact(0x0c0330, nth, &b, &dv, &fn) != 0)
            break;
        int dup = 0;
        for (int i = 0; i < g_n_xhci; i++) {
            if (g_xhci[i].bus == b && g_xhci[i].dev == dv &&
                g_xhci[i].func == fn)
                dup = 1;
        }
        if (!dup && driver_usb_xhci_probe_one(b, dv, fn) == 0) n++;
    }
    return n;
}

int driver_usb_xhci_init(const driver_pci_dev_t *dev) {
    if (!dev) return -1;
    return driver_usb_xhci_probe_one(dev->bus, dev->dev, dev->func);
}
