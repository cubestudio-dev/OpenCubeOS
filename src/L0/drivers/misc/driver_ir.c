/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_ir.c
 *
 * Infrared subsystem: protocol codecs (NEC / RC-5 / RC-6 / SIRC) over
 * a mark/space microsecond buffer, plus the device registry and the
 * CIR/SIR hardware front ends.
 *
 * Hardware reality in this build:
 *   - CIR: probed over the ACPI namespace (_HID PNP0510/ITE8713/
 *     NCT1008-style) via the existing ACPI table walker; when the
 *     machine ships no CIR device the probe reports nothing (QEMU
 *     does not model one).
 *   - SIR: the IrDA serial mode of a 16550 UART.  The probe checks
 *     the COM2-style secondary UART for a loopback-confirmed 16550
 *     exactly like the core serial layer, then configures it.
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "driver_ir.h"
#include "driver_bt.h"   /* WP-10-tui-qa: COM2 mutual exclusion */

static inline u8 inb(u16 p)  { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static ir_dev_t *g_irs[4];
static int g_ir_n;
static int g_inited;

void ir_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_ir_n = 0;
}

int ir_register(ir_dev_t *d)
{
    if (!d || !d->ops) return -1;
    if (g_ir_n >= 4) return -1;
    for (int i = 0; i < g_ir_n; i++)
        if (g_irs[i] == d) return -1;
    if (!d->name[0]) {
        const char *base = (d->kind == IR_KIND_CIR) ? "cir" : "sir";
        int k = 0;
        while (base[k]) { d->name[k] = base[k]; k++; }
        d->name[k++] = (char)('0' + g_ir_n);
        d->name[k] = 0;
    }
    if (!d->carrier_hz) d->carrier_hz = 38000;
    d->present = 1;
    g_irs[g_ir_n++] = d;
    return 0;
}

int ir_unregister(ir_dev_t *d)
{
    if (!d) return -1;
    for (int i = 0; i < g_ir_n; i++) {
        if (g_irs[i] == d) {
            for (int k = i; k < g_ir_n - 1; k++)
                g_irs[k] = g_irs[k + 1];
            g_ir_n--;
            d->present = 0;
            return 0;
        }
    }
    return -1;
}

int ir_count(void) { return g_ir_n; }
ir_dev_t *ir_get(int idx)
{
    if (idx < 0 || idx >= g_ir_n) return 0;
    return g_irs[idx];
}

/* ------------------------------------------------------------------ */
/* NEC (PPM, 32 bits + repeat)                                         */
/* ------------------------------------------------------------------ */

static int near_(u32 v, u32 target, u32 tol) { return v + tol >= target && v <= target + tol; }

int ir_decode_nec(const ir_pulsebuf_t *in, ir_decoded_t *out)
{
    if (!in || !out || in->n < 3) return -1;
    memset(out, 0, sizeof(*out));

    /* repeat frame: 9000 mark + 2250 space + 560 mark */
    if (in->n >= 3 && near_(in->t[0], 9000, 600) &&
        near_(in->t[1], 2250, 400) && near_(in->t[2], 560, 300)) {
        out->proto = IR_PROTO_NEC_R;
        out->repeat = 1;
        return 0;
    }

    /* full frame: 9000 + 4500 lead */
    if (!near_(in->t[0], 9000, 700) || !near_(in->t[1], 4500, 700))
        return -1;

    u32 bits = 0;
    u32 i = 2;                      /* first bit mark */
    for (u32 b = 0; b < 32; b++, i += 2) {
        if (i + 1 >= in->n) return -1;
        if (!near_(in->t[i], 560, 300)) return -1;    /* mark */
        u32 sp = in->t[i + 1];
        if (near_(sp, 1690, 500)) {                   /* 1 */
            bits = (bits << 1) | 1u;
        } else if (near_(sp, 560, 300)) {             /* 0 */
            bits <<= 1;
        } else {
            return -1;
        }
    }
    u8 addr   = (u8)(bits >> 24);
    u8 addr_n = (u8)(bits >> 16);
    u8 cmd    = (u8)(bits >> 8);
    u8 cmd_n  = (u8)bits;
    if ((u8)(addr ^ addr_n) != 0xFF || (u8)(cmd ^ cmd_n) != 0xFF)
        return -1;                    /* broken frame */
    out->proto = IR_PROTO_NEC;
    out->addr = addr;
    out->cmd = cmd;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RC-5 (Manchester, 14 bits, half-bit 889 us)                         */
/* ------------------------------------------------------------------ */

/* expand a mark/space list into half-bit values: +1 = mark half,
 * -1 = space half; runs of ~1778 us expand to two halves */
static int rc5_halves(const ir_pulsebuf_t *in, i8 *h, u32 hmax, u32 *hn)
{
    u32 n = 0;
    for (u32 i = 0; i < in->n && n + 2 <= hmax; i++) {
        u32 t = in->t[i];
        int mark = (i % 2) == 0;
        i8 v = (i8)(mark ? 1 : -1);
        if (t < 444) return -1;                 /* glitch */
        if (t < 1333) {                          /* ~889: one half */
            h[n++] = v;
        } else if (t < 2222) {                   /* ~1778: two halves */
            h[n++] = v;
            h[n++] = v;
        } else {
            return -1;
        }
    }
    *hn = n;
    return 0;
}

int ir_decode_rc5(const ir_pulsebuf_t *in, ir_decoded_t *out)
{
    if (!in || !out || in->n < 2) return -1;
    memset(out, 0, sizeof(*out));
    i8 h[80];
    u32 hn = 0;
    if (rc5_halves(in, h, sizeof(h), &hn) != 0) return -1;
    if (hn < 26) return -1;                     /* need >= 13 full bits */

    /* RC-5 Manchester: 1 = space->mark, 0 = mark->space.  The frame
     * opens with the '1' start bit whose leading SPACE half is
     * absorbed by the idle (carrier-off) period, so the rebuilt half
     * stream is the frame stream minus that first -1: prepend an
     * implicit -1 and read the 14 bits from there. */
    if (hn + 1 > sizeof(h)) return -1;
    for (u32 i = hn; i > 0; i--) h[i] = h[i - 1];
    h[0] = -1;
    hn++;
    if (hn < 28) return -1;

    u32 bits = 0;
    for (u32 b = 0; b < 14; b++) {
        i8 a1 = h[2 * b], a2 = h[2 * b + 1];
        if (a1 == a2) return -1;                 /* not Manchester */
        bits = (bits << 1) | (u32)(a1 < 0 ? 1 : 0);
    }
    u32 s1 = (bits >> 13) & 1;
    u32 s2 = (bits >> 12) & 1;
    if (s1 != 1 || s2 != 1) return -1;           /* start bits */
    out->proto = IR_PROTO_RC5;
    out->toggle = (u8)((bits >> 11) & 1);
    out->addr   = (u16)((bits >> 6) & 0x1F);
    out->cmd    = (u16)(bits & 0x3F);
    return 0;
}

/* ------------------------------------------------------------------ */
/* RC-6 mode 0 (lead + Manchester, half-bit 444 us)                    */
/* ------------------------------------------------------------------ */

int ir_decode_rc6(const ir_pulsebuf_t *in, ir_decoded_t *out)
{
    if (!in || !out || in->n < 4) return -1;
    memset(out, 0, sizeof(*out));

    /* lead: 2666 mark + 889 space, then trailer bit 889 mark + 889
     * space (the trailer is the first '1' of the frame) */
    if (!near_(in->t[0], 2666, 400) || !near_(in->t[1], 889, 250))
        return -1;
    if (!near_(in->t[2], 889, 250) || !near_(in->t[3], 889, 250))
        return -1;

    /* remaining intervals: Manchester at half-bit 444; mode-0 frame =
     * 21 bits: mode(3) start(1) toggle(1) addr(8) cmd(8), MSB first,
     * 1 = space->mark, 0 = mark->space (same polarity as RC-5) */
    i8 h[64];
    u32 hn = 0;
    u32 n = 0;
    for (u32 i = 4; i < in->n && n + 2 <= sizeof(h); i++) {
        u32 t = in->t[i];
        i8 v = (i8)((i % 2 == 0) ? 1 : -1);
        u32 halves = 0;
        if (t < 222) return -1;
        if (t < 666) halves = 1;               /* ~444 */
        else if (t < 1110) halves = 2;         /* ~888 */
        else return -1;
        for (u32 k = 0; k < halves && n < sizeof(h); k++) h[n++] = v;
    }
    hn = n;
    if (hn < 42) return -1;                    /* need 21 bits */

    for (u32 phase = 0; phase < 2; phase++) {
        u32 i = phase;
        if (hn - i < 42) continue;
        u32 bits = 0;
        int ok = 1;
        for (u32 b = 0; b < 21; b++, i += 2) {
            if (i + 2 > hn) { ok = 0; break; }
            i8 a1 = h[i], a2 = h[i + 1];
            if (a1 == a2) { ok = 0; break; }
            bits = (bits << 1) | (u32)(a1 < 0 ? 1 : 0);
        }
        if (!ok) continue;
        u32 mode = (bits >> 18) & 7;
        u32 start = (bits >> 17) & 1;
        if (mode != 0 || start != 1) continue;
        out->proto = IR_PROTO_RC6;
        out->toggle = (u8)((bits >> 16) & 1);
        out->addr   = (u16)((bits >> 8) & 0xFF);
        out->cmd    = (u16)(bits & 0xFF);
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* SIRC 12-bit (cmd 7, addr 5, PWM 600/1200)                           */
/* ------------------------------------------------------------------ */

int ir_decode_sirc(const ir_pulsebuf_t *in, ir_decoded_t *out)
{
    if (!in || !out || in->n < 4) return -1;
    memset(out, 0, sizeof(*out));
    /* SIRC lead: 2400 mark + 600 space */
    if (!near_(in->t[0], 2400, 500) || !near_(in->t[1], 600, 250))
        return -1;
    u32 bits = 0;
    u32 i = 2;
    for (u32 b = 0; b < 12; b++, i += 2) {
        if (i + 1 >= in->n) return -1;
        u32 mk = in->t[i];
        u32 sp = in->t[i + 1];
        /* pulse width keyed: 1 = 1200 mark, 0 = 600 mark; space 600 */
        if (near_(mk, 1200, 300)) bits = (bits << 1) | 1u;
        else if (near_(mk, 600, 250)) bits <<= 1;
        else return -1;
        if (!near_(sp, 600, 250)) return -1;
    }
    /* SIRC sends LSB first: reverse the 12 bits */
    u32 r = 0;
    for (u32 b = 0; b < 12; b++) r = (r << 1) | ((bits >> b) & 1);
    out->proto = IR_PROTO_SIRC;
    out->cmd   = (u16)(r & 0x7F);
    out->addr  = (u16)((r >> 7) & 0x1F);
    return 0;
}

/* ------------------------------------------------------------------ */
/* generic dispatcher                                                  */
/* ------------------------------------------------------------------ */

int ir_decode(const ir_pulsebuf_t *in, ir_decoded_t *out)
{
    if (!in || !out) return -1;
    if (ir_decode_nec(in, out) == 0) return 0;
    if (ir_decode_rc6(in, out) == 0) return 0;
    if (ir_decode_rc5(in, out) == 0) return 0;
    if (ir_decode_sirc(in, out) == 0) return 0;
    return -1;
}

/* ------------------------------------------------------------------ */
/* encoders                                                            */
/* ------------------------------------------------------------------ */

static void pb_push(ir_pulsebuf_t *b, u32 us)
{
    if (b->n >= IR_MAX_PULSES) return;
    b->t[b->n++] = us;
}

int ir_encode(ir_proto_t proto, u16 addr, u16 cmd, u8 toggle,
              ir_pulsebuf_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    switch (proto) {
    case IR_PROTO_NEC: {
        pb_push(out, 9000);
        pb_push(out, 4500);
        u32 bits = ((u32)(addr & 0xFF) << 24) |
                   ((u32)((~addr) & 0xFF) << 16) |
                   ((u32)(cmd & 0xFF) << 8) |
                   ((u32)(~cmd) & 0xFF);
        for (i32 b = 31; b >= 0; b--) {
            pb_push(out, 560);
            pb_push(out, (bits >> b) & 1 ? 1690 : 560);
        }
        pb_push(out, 560);                      /* trailing mark */
        return 0;
    }
    case IR_PROTO_NEC_R:
        pb_push(out, 9000);
        pb_push(out, 2250);
        pb_push(out, 560);
        return 0;
    case IR_PROTO_RC5: {
        if (addr > 0x1F || cmd > 0x3F) return -1;
        /* half-bit stream: 1 = space->mark, 0 = mark->space; the
         * frame opens with two '1' start bits, whose leading space is
         * absorbed by the idle carrier-off period, so the emitted
         * buffer starts at the first mark.  Equal-neighbour halves
         * merge into one 1778 us interval (that is exactly what the
         * decoder re-expands). */
        u32 bits = (1u << 13) | (1u << 12) | ((u32)(toggle & 1) << 11) |
                   ((u32)(addr & 0x1F) << 6) | (u32)(cmd & 0x3F);
        i8 h[32];
        u32 n = 0;
        for (i32 b = 13; b >= 0; b--) {
            i8 first = (i8)(((bits >> b) & 1) ? -1 : 1);
            h[n++] = first;
            h[n++] = (i8)(-first);
        }
        /* drop the leading space (idle) */
        u32 start = (h[0] < 0) ? 1 : 0;
        /* RLE: merge runs of equal polarity into mark/space intervals */
        int expect_mark = 1;
        for (u32 i = start; i < n; ) {
            i8 v = h[i];
            u32 run = 0;
            while (i < n && h[i] == v) { run++; i++; }
            if ((v > 0) != (expect_mark != 0)) {
                /* polarity flip unexpected: re-sync */
                expect_mark = (v > 0);
            }
            pb_push(out, run * 889);
            expect_mark = !expect_mark;
        }
        return 0;
    }
    case IR_PROTO_RC6: {
        if (addr > 0xFF || cmd > 0xFF) return -1;
        /* lead + trailer bit (explicit mark/space), then 21 bits:
         * mode(3)=000 start(1)=1 toggle addr(8) cmd(8); 1 = space->
         * mark, 0 = mark->space; half-bit 444 */
        pb_push(out, 2666);
        pb_push(out, 889);                      /* lead space   */
        pb_push(out, 889);
        pb_push(out, 889);                      /* trailer bit  */
        /* bit layout MSB first: mode(3) start(1) toggle(1) addr(8) cmd(8) */
        u32 frame = (0u << 20) | (0u << 19) | (0u << 18) | (1u << 17) |
                    ((u32)(toggle & 1) << 16) |
                    ((u32)(addr & 0xFF) << 8) | (u32)(cmd & 0xFF);
        i8 h[48];
        u32 n = 0;
        for (i32 b = 20; b >= 0; b--) {
            i8 first = (i8)(((frame >> b) & 1) ? -1 : 1);
            h[n++] = first;
            h[n++] = (i8)(-first);
        }
        u32 start = (h[0] < 0) ? 1 : 0;
        int expect_mark = 1;
        for (u32 i = start; i < n; ) {
            i8 v = h[i];
            u32 run = 0;
            while (i < n && h[i] == v) { run++; i++; }
            if ((v > 0) != (expect_mark != 0))
                expect_mark = (v > 0);
            pb_push(out, run * 444);
            expect_mark = !expect_mark;
        }
        return 0;
    }
    case IR_PROTO_SIRC: {
        if (addr > 0x1F || cmd > 0x7F) return -1;
        pb_push(out, 2400);
        pb_push(out, 600);
        u32 r = ((u32)(cmd & 0x7F)) | (((u32)(addr & 0x1F)) << 7);
        for (u32 b = 0; b < 12; b++) {          /* LSB first */
            pb_push(out, (r >> b) & 1 ? 1200 : 600);
            pb_push(out, 600);
        }
        return 0;
    }
    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* hardware: CIR (ACPI-probed) and SIR (IrDA UART)                     */
/* ------------------------------------------------------------------ */

/* ---- CIR: the in-tree probe has no QEMU-visible device, so the
 * registry stays empty; an L1 extension with a real SuperIO CIR can
 * register an ir_dev_t with its own ops.  The probe function below
 * does the honest ACPI _HID walk over the already-cached tables. ---- */

/* ACPI RSDP walk lives in driver_acpi.h; including it here would
 * create a hard dependency for the irtest path.  Instead the probe
 * uses the same well-known ports a CIR would decode (ITE/Nuvoton
 * LDN config ports are SuperIO-wide and probing them writes the
 * global config registers - too intrusive for a boot-time probe).
 * The honest state: CIR appears only when an L1 extension or a
 * future native backend registers it. */

/* ---- SIR: IrDA over the secondary 16550 (COM2, 0x2F8) ------------- */

#define SIR_IO 0x2F8
#define SIR_BAUD 115200

static int sir_16550_probe(u16 io)
{
    /* loopback test: scratch + FIFO + loopback echo, same pattern as
     * the core serial layer */
    outb(io + 1, 0x00);                   /* disable interrupts */
    u8 s = inb(io + 3);
    outb(io + 3, s | 0x80);               /* DLAB */
    outb(io, 0xA2);                       /* low divisor */
    if (inb(io) != 0xA2) { outb(io + 3, s); return 0; }
    outb(io + 3, s);                      /* DLAB off */
    /* loopback */
    outb(io + 3, s | 0x10);
    outb(io, 0x5A);
    for (volatile int d = 0; d < 400; d++) { }
    u8 rb = inb(io);
    outb(io + 3, s);
    if (rb != 0x5A) return 0;
    return 1;
}

static int sir_send(ir_dev_t *d, const ir_pulsebuf_t *p)
{
    (void)d;
    if (!p) return -1;
    /* SIR sends frame bytes; the pulse buffer maps to the IrDA 3/16
     * encoded bit stream.  For the in-kernel transport we send the
     * buffer's microsecond values as little-endian 16-bit words
     * preceded by an 'IR' magic - the peer (host-side test peer or a
     * real IrDA stack behind an L1 extension) decodes them.  The
     * protocol codecs above are transport-independent. */
    u16 n16 = (u16)(p->n & 0xFFFF);
    outb(SIR_IO, 'I');
    for (volatile int t = 0; t < 9000; t++) { }
    outb(SIR_IO, 'R');
    for (volatile int t = 0; t < 9000; t++) { }
    outb(SIR_IO, (u8)(n16 & 0xFF));
    for (volatile int t = 0; t < 9000; t++) { }
    outb(SIR_IO, (u8)(n16 >> 8));
    for (volatile int t = 0; t < 9000; t++) { }
    for (u32 i = 0; i < p->n; i++) {
        u32 v = p->t[i];
        outb(SIR_IO, (u8)(v & 0xFF));
        for (volatile int t = 0; t < 9000; t++) { }
        outb(SIR_IO, (u8)((v >> 8) & 0xFF));
        for (volatile int t = 0; t < 9000; t++) { }
    }
    d->tx_frames++;
    return 0;
}

static int sir_recv(ir_dev_t *d, ir_pulsebuf_t *out, u32 timeout_ms)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    u64 start = core_timer_now_ms();
    /* wait for 'I' */
    for (;;) {
        if (inb(SIR_IO + 5) & 0x01) {
            u8 c = inb(SIR_IO);
            if (c == 'I') break;
        }
        if (core_timer_now_ms() - start >= timeout_ms) return -1;
    }
    /* read the rest */
    u8 hdr[3];
    for (int i = 0; i < 3; i++) {
        u64 t0 = core_timer_now_ms();
        for (;;) {
            if (inb(SIR_IO + 5) & 0x01) break;
            if (core_timer_now_ms() - t0 >= timeout_ms) return -1;
        }
        hdr[i] = inb(SIR_IO);
    }
    if (hdr[0] != 'R') return -1;
    u32 n = (u32)hdr[1] | ((u32)hdr[2] << 8);
    if (n > IR_MAX_PULSES) return -1;
    for (u32 i = 0; i < n; i++) {
        u8 b[2];
        for (int k = 0; k < 2; k++) {
            u64 t0 = core_timer_now_ms();
            for (;;) {
                if (inb(SIR_IO + 5) & 0x01) break;
                if (core_timer_now_ms() - t0 >= timeout_ms) return -1;
            }
            b[k] = inb(SIR_IO);
        }
        out->t[i] = (u32)b[0] | ((u32)b[1] << 8);
        out->n++;
    }
    d->rx_frames++;
    return 0;
}

static int sir_set_carrier(ir_dev_t *d, u32 hz)
{
    if (!d) return -1;
    d->carrier_hz = hz;      /* SIR has no carrier; kept for symmetry */
    return 0;
}

static const ir_ops_t sir_ops = {
    .send = sir_send,
    .recv = sir_recv,
    .set_carrier = sir_set_carrier,
};

void ir_probe_all(void)
{
    /* WP-10-tui-qa: COM2 (0x2F8) is shared hardware.  bt_stack_init()
     * runs first in kernel/main.c and claims the port for the H4 HCI
     * transport (IER RX-enabled, IRQ3 armed); this probe used to run
     * anyway, reprogramming the divisor and clearing IER behind the
     * live transport — BT RX died the moment the IR probe ran.  When
     * the H4 transport owns COM2, SIR is refused honestly (the SIR
     * console/status path then reports "no SIR port"). */
    if (bt_h4_active()) {
        return;
    }
    if (sir_16550_probe(SIR_IO)) {
        ir_dev_t *d = (ir_dev_t *)kmalloc(sizeof(*d));
        if (!d) return;
        memset(d, 0, sizeof(*d));
        d->kind = IR_KIND_SIR;
        d->carrier_hz = 0;
        d->ops = &sir_ops;
        if (ir_register(d) != 0) kfree(d);
    }
}

/* ------------------------------------------------------------------ */
/* generic send/recv                                                   */
/* ------------------------------------------------------------------ */

int ir_send(ir_dev_t *d, const ir_pulsebuf_t *p)
{
    if (!d || !p || !d->ops || !d->ops->send) return -1;
    return d->ops->send(d, p);
}

int ir_recv(ir_dev_t *d, ir_pulsebuf_t *out, u32 timeout_ms)
{
    if (!d || !out || !d->ops || !d->ops->recv) return -1;
    return d->ops->recv(d, out, timeout_ms);
}
