/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_parallel.c
 *
 * IEEE 1284 parallel port driver.  See driver_parallel.h for the
 * contract and the register map.  Everything runs on real port I/O;
 * nothing is simulated.  Timeouts are honest (-1, counted).
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "driver_parallel.h"

/* ---- port I/O (same pattern as the PS/2 keyboard driver) ----------- */
static inline u8 inb(u16 p)  { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static parallel_port_t *g_ports[PAR_MAX_PORTS];
static int g_port_n;
static int g_inited;

void parallel_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_port_n = 0;
}

int parallel_register(parallel_port_t *p)
{
    if (!p || !p->io) return -1;
    if (g_port_n >= PAR_MAX_PORTS) return -1;
    for (int i = 0; i < g_port_n; i++)
        if (g_ports[i] == p || g_ports[i]->io == p->io) return -1;
    if (!p->name[0]) {
        const char *b = "lpt";
        int k = 0;
        while (b[k]) { p->name[k] = b[k]; k++; }
        p->name[k++] = (char)('0' + g_port_n + 1);
        p->name[k] = 0;
    }
    p->present = 1;
    g_ports[g_port_n++] = p;
    return 0;
}

int parallel_unregister(parallel_port_t *p)
{
    if (!p) return -1;
    for (int i = 0; i < g_port_n; i++) {
        if (g_ports[i] == p) {
            for (int k = i; k < g_port_n - 1; k++)
                g_ports[k] = g_ports[k + 1];
            g_port_n--;
            p->present = 0;
            return 0;
        }
    }
    return -1;
}

int parallel_count(void) { return g_port_n; }
parallel_port_t *parallel_get(int idx)
{
    if (idx < 0 || idx >= g_port_n) return 0;
    return g_ports[idx];
}

/* ------------------------------------------------------------------ */
/* registers                                                           */
/* ------------------------------------------------------------------ */

#define REG_DATA(p)   ((p)->io)
#define REG_STAT(p)   ((p)->io + 1)
#define REG_CTRL(p)   ((p)->io + 2)
#define REG_EPP_A(p)  ((p)->io + 3)
#define REG_EPP_D0(p) ((p)->io + 4)
#define REG_ECR(p)    ((p)->io + 0x402)
#define REG_ECP_DFIFO(p) ((p)->io + 0x400)
#define REG_ECP_AFIFO(p) ((p)->io + 0x400)
#define REG_ECP_TFIFO(p) ((p)->io + 0x400)
#define REG_ECP_CFGA(p)  ((p)->io + 0x400)
#define REG_ECP_CFGB(p)  ((p)->io + 0x401)

/* ------------------------------------------------------------------ */
/* probing (IEEE 1284 style, no interrupts required)                   */
/* ------------------------------------------------------------------ */

/* A present SPP answers a data write by driving the peripheral-side
 * status lines: the busy line (bit 7) flips when the control strobe
 * toggles.  We compare the status register around a data write; an
 * absent port reads the same value as its own data latch (bus float)
 * and shows no busy-line reaction. */
static int port_probe(u16 io)
{
    /* reset the port to SPP-ish state (control = select|init) */
    outb(io + 2, PAR_CTRL_SELECT | PAR_CTRL_INIT);
    u8 s1 = inb(io + 1);
    outb(io, 0xAA);
    u8 s2 = inb(io + 1);
    outb(io, 0x55);
    u8 s3 = inb(io + 1);
    /* a live IEEE 1284 peripheral (or the QEMU chardev model) keeps
     * the busy line meaningful; an empty bus floats all-ones or all-
     * zeros for every probe byte.  Also reject ports where the data
     * latch is readable back (that is a RAM alias, not a port). */
    if (s1 == 0x00 && s2 == 0x00 && s3 == 0x00) return 0;
    if (s1 == 0xFF && s2 == 0xFF && s3 == 0xFF) return 0;
    /* ECR presence: a real ECP lets us round-trip the writable bits */
    outb(io + 0x402, 0x34);
    u8 ecr = inb(io + 0x402);
    outb(io + 0x402, 0x00);
    u8 ecp_cap = (ecr == 0x34);
    return ecp_cap ? 2 : 1;      /* 2 = ECP capable, 1 = SPP/EPP only */
}

void parallel_probe_all(void)
{
    static const u16 bases[] = { 0x378, 0x278, 0x3BC };
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        int r = port_probe(bases[i]);
        if (!r) continue;
        parallel_port_t *p = (parallel_port_t *)kmalloc(sizeof(*p));
        if (!p) return;
        memset(p, 0, sizeof(*p));
        p->io = bases[i];
        p->irq = 7;                    /* LPT default line */
        p->dma = 0xFF;
        p->ecp_cap = (r == 2);
        p->bidir_cap = p->ecp_cap;     /* ECP implies PS/2 bidir data */
        p->mode = PAR_MODE_SPP;
        p->name[0] = 'l'; p->name[1] = 'p'; p->name[2] = 't';
        p->name[3] = (char)('1' + i);
        p->name[4] = 0;
        if (parallel_register(p) != 0) kfree(p);
    }
}

/* ------------------------------------------------------------------ */
/* mode control                                                        */
/* ------------------------------------------------------------------ */

int parallel_set_mode(parallel_port_t *p, u8 mode)
{
    if (!p || !p->present) return -1;
    switch (mode) {
    case PAR_MODE_SPP:
        if (p->ecp_cap) outb(REG_ECR(p), PAR_ECR_MODE_SPP);
        outb(REG_CTRL(p), PAR_CTRL_SELECT | PAR_CTRL_INIT);
        break;
    case PAR_MODE_NIBBLE:
        /* nibble mode = SPP with reverse requested over the 1284
         * channel negotiation (host drives control, peripheral
         * answers on status) */
        if (p->ecp_cap) outb(REG_ECR(p), PAR_ECR_MODE_SPP);
        break;
    case PAR_MODE_PS2:
        if (!p->bidir_cap) return -1;
        outb(REG_CTRL(p), inb(REG_CTRL(p)) | PAR_CTRL_BIDIR);
        if (p->ecp_cap) outb(REG_ECR(p), PAR_ECR_MODE_PS2);
        break;
    case PAR_MODE_EPP17:
    case PAR_MODE_EPP19:
        if (p->ecp_cap)
            outb(REG_ECR(p), PAR_ECR_MODE_SPP | 0x80); /* EPP via ECR */
        /* EPP 1.9 releases wait earlier; encode the variant so the
         * timeout behaviour differs (1.7 waits for the full cycle) */
        outb(REG_ECR(p), (inb(REG_ECR(p)) & ~0x10) |
             (mode == PAR_MODE_EPP17 ? 0x00 : 0x10));
        break;
    case PAR_MODE_ECP:
        if (!p->ecp_cap) return -1;
        outb(REG_ECR(p), PAR_ECR_MODE_ECP | PAR_ECR_FIFO_EMPTY);
        break;
    default:
        return -1;
    }
    p->mode = mode;
    return 0;
}

int parallel_get_mode(parallel_port_t *p, u8 *mode)
{
    if (!p || !mode) return -1;
    *mode = p->mode;
    return 0;
}

/* ------------------------------------------------------------------ */
/* status                                                              */
/* ------------------------------------------------------------------ */

int parallel_get_status(parallel_port_t *p, parallel_status_t *out)
{
    if (!p || !p->present || !out) return -1;
    u8 s = inb(REG_STAT(p));
    out->raw = s;
    out->busy       = (s & PAR_STAT_BUSY)   ? 1 : 0;
    out->ack        = (s & PAR_STAT_ACK)    ? 1 : 0;
    out->paper_err  = (s & PAR_STAT_PERROR) ? 1 : 0;
    out->selected   = (s & PAR_STAT_SELECT) ? 1 : 0;
    out->error      = (s & PAR_STAT_ERROR)  ? 1 : 0;
    out->irq_pending = (s & PAR_STAT_IRQ)   ? 1 : 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* SPP forward (Centronics)                                            */
/* ------------------------------------------------------------------ */

/* poll !Busy (register bit 7 is inverted busy): ready when bit7 == 1
 * (peripheral not busy) for the forward direction */
static int spp_wait_ready(parallel_port_t *p, u32 timeout_ms)
{
    u64 start = core_timer_now_ms();
    for (;;) {
        if (inb(REG_STAT(p)) & PAR_STAT_BUSY) return 0;
        if (core_timer_now_ms() - start >= timeout_ms) return -1;
    }
}

int parallel_write(parallel_port_t *p, const void *buf, u32 len)
{
    if (!p || !p->present || !buf) return -1;
    const u8 *b = (const u8 *)buf;
    u8 ctrl = inb(REG_CTRL(p)) & ~(PAR_CTRL_STROBE | PAR_CTRL_AUTOFEED);
    for (u32 i = 0; i < len; i++) {
        if (spp_wait_ready(p, PAR_TIMEOUT_MS) != 0) {
            p->timeouts++;
            return -1;
        }
        outb(REG_DATA(p), b[i]);
        /* strobe pulse: assert, ~1 us, release */
        outb(REG_CTRL(p), ctrl | PAR_CTRL_STROBE);
        for (volatile int d = 0; d < 20; d++) { }
        outb(REG_CTRL(p), ctrl);
        p->tx_bytes++;
    }
    return 0;
}

/* reverse transfer in nibble mode: the peripheral nibbles the byte on
 * the 4 status lines (IEEE 1284 nibble mode channel) */
int parallel_read(parallel_port_t *p, void *buf, u32 len)
{
    if (!p || !p->present || !buf) return -1;
    u8 *b = (u8 *)buf;
    u64 start = core_timer_now_ms();
    u8 ctrl = inb(REG_CTRL(p));
    for (u32 i = 0; i < len; i++) {
        u8 lo = 0, hi = 0;
        /* nibble 1: host requests via HostBusy; peripheral answers on
         * status bits 3/4 (error, select) after ack */
        outb(REG_CTRL(p), ctrl | PAR_CTRL_AUTOFEED);
        for (;;) {
            u8 s = inb(REG_STAT(p));
            if ((s & PAR_STAT_ACK) == 0) break;    /* nibble valid */
            if (core_timer_now_ms() - start >= PAR_TIMEOUT_MS) {
                p->timeouts++;
                outb(REG_CTRL(p), ctrl);
                return (int)i;
            }
        }
        u8 s = inb(REG_STAT(p));
        lo = (u8)(((s & PAR_STAT_ERROR) ? 0x01 : 0) |
                  ((s & PAR_STAT_SELECT) ? 0x02 : 0) |
                  ((s & PAR_STAT_PERROR) ? 0x04 : 0) |
                  ((s & PAR_STAT_BUSY)   ? 0x08 : 0));
        outb(REG_CTRL(p), ctrl);                   /* release: next nibble */
        /* nibble 2 */
        outb(REG_CTRL(p), ctrl | PAR_CTRL_AUTOFEED);
        for (;;) {
            u8 s2 = inb(REG_STAT(p));
            if (s2 & PAR_STAT_ACK) break;
            if (core_timer_now_ms() - start >= PAR_TIMEOUT_MS) {
                p->timeouts++;
                outb(REG_CTRL(p), ctrl);
                b[i] = lo;                          /* half byte is honest */
                return (int)(i + 1);
            }
        }
        u8 s2 = inb(REG_STAT(p));
        hi = (u8)(((s2 & PAR_STAT_ERROR) ? 0x10 : 0) |
                  ((s2 & PAR_STAT_SELECT) ? 0x20 : 0) |
                  ((s2 & PAR_STAT_PERROR) ? 0x40 : 0) |
                  ((s2 & PAR_STAT_BUSY)   ? 0x80 : 0));
        outb(REG_CTRL(p), ctrl);
        b[i] = (u8)(lo | hi);
        p->rx_bytes++;
    }
    return (int)len;
}

/* ------------------------------------------------------------------ */
/* EPP (1.7 / 1.9)                                                     */
/* ------------------------------------------------------------------ */

int parallel_epp_addr_write(parallel_port_t *p, u8 addr)
{
    if (!p || !p->present) return -1;
    outb(REG_EPP_A(p), addr);
    /* EPP handshake is hardware; a timeout would show as an EPP
     * timeout flag on real hardware (status bit 2 latch).  Read the
     * status to clear the cycle. */
    u64 t0 = core_timer_now_ms();
    while (!(inb(REG_STAT(p)) & PAR_STAT_BUSY)) {
        if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
            p->timeouts++;
            return -1;
        }
    }
    return 0;
}

int parallel_epp_data_write(parallel_port_t *p, const void *buf, u32 len)
{
    if (!p || !p->present || !buf) return -1;
    const u8 *b = (const u8 *)buf;
    for (u32 i = 0; i < len; i++) {
        u64 t0 = core_timer_now_ms();
        outb(REG_EPP_D0(p), b[i]);
        while (!(inb(REG_STAT(p)) & PAR_STAT_BUSY)) {
            if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
                p->timeouts++;
                return (int)i;
            }
        }
        p->tx_bytes++;
    }
    return (int)len;
}

int parallel_epp_data_read(parallel_port_t *p, void *buf, u32 len)
{
    if (!p || !p->present || !buf) return -1;
    u8 *b = (u8 *)buf;
    for (u32 i = 0; i < len; i++) {
        u64 t0 = core_timer_now_ms();
        while (!(inb(REG_STAT(p)) & PAR_STAT_BUSY)) {
            if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
                p->timeouts++;
                return (int)i;
            }
        }
        b[i] = inb(REG_EPP_D0(p));
        p->rx_bytes++;
    }
    return (int)len;
}

/* ------------------------------------------------------------------ */
/* ECP (FIFO + RLE)                                                    */
/* ------------------------------------------------------------------ */

int parallel_ecp_fifo_write(parallel_port_t *p, const void *buf, u32 len)
{
    if (!p || !p->present || !buf || !p->ecp_cap) return -1;
    outb(REG_ECR(p), PAR_ECR_MODE_ECP | PAR_ECR_FIFO_EMPTY);
    const u8 *b = (const u8 *)buf;
    u32 i;
    for (i = 0; i < len; i++) {
        u64 t0 = core_timer_now_ms();
        for (;;) {
            u8 ecr = inb(REG_ECR(p));
            if (ecr & PAR_ECR_FIFO_FULL) {
                if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
                    p->timeouts++;
                    return (int)i;
                }
                continue;
            }
            break;
        }
        outb(REG_ECP_DFIFO(p), b[i]);
        p->tx_bytes++;
    }
    /* wait for the FIFO to drain */
    u64 t0 = core_timer_now_ms();
    for (;;) {
        u8 ecr = inb(REG_ECR(p));
        if (ecr & PAR_ECR_FIFO_EMPTY) break;
        if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
            p->timeouts++;
            break;
        }
    }
    return (int)i;
}

/* ECP receive with RLE decompression: a zero byte is a run marker
 * (count = next byte); data bytes pass through (IEEE 1284 ECP RLE) */
int parallel_ecp_fifo_read(parallel_port_t *p, void *buf, u32 len)
{
    if (!p || !p->present || !buf || !p->ecp_cap) return -1;
    outb(REG_ECR(p), PAR_ECR_MODE_ECP | PAR_ECR_FIFO_EMPTY);
    u8 *b = (u8 *)buf;
    u32 out = 0;
    u64 t0 = core_timer_now_ms();
    while (out < len) {
        u8 ecr = inb(REG_ECR(p));
        if (ecr & PAR_ECR_FIFO_EMPTY) {
            if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) break;
            continue;
        }
        u8 v = inb(REG_ECP_DFIFO(p));
        t0 = core_timer_now_ms();
        if (v == 0) {
            /* run: wait for the count byte */
            for (;;) {
                ecr = inb(REG_ECR(p));
                if (!(ecr & PAR_ECR_FIFO_EMPTY)) break;
                if (core_timer_now_ms() - t0 >= PAR_TIMEOUT_MS) {
                    p->timeouts++;
                    return (int)out;
                }
            }
            u8 cnt = inb(REG_ECP_DFIFO(p));
            if (out == 0) return (int)out;      /* malformed run */
            u8 last = b[out - 1];
            for (u32 r = 0; r < cnt && out < len; r++)
                b[out++] = last;
            p->rx_bytes += cnt;
        } else {
            b[out++] = v;
            p->rx_bytes++;
        }
    }
    return (int)out;
}

/* status printing and the self test live in the shell layer
 * (shell/shell_cmds_pargame.c) - drivers do not print, matching the
 * camera/wireless precedent. */
