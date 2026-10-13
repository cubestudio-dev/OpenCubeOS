/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/bluetooth/driver_bt_h4.c
 *
 * H4 UART Bluetooth transport (Core 5.x vol 4.A) on the second serial
 * port COM2 (0x2f8, IRQ3) - a real 16550 flow: loopback probe, 115200
 * 8N1 + FIFO, interrupt-driven RX ring, H4 frame reassembly, poll-time
 * TX.  QEMU maps the second -serial to this port, so the E2E harness
 * speaks real UART bytes with the kernel.
 */
#include "types.h"
#include "driver_bt.h"
#include "arch_irq.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

/* 16550 registers (COM2)                                              */
#define UART2_BASE   0x2f8
#define UART2_THR    (UART2_BASE + 0)
#define UART2_RBR    (UART2_BASE + 0)
#define UART2_IER    (UART2_BASE + 1)
#define UART2_FCR    (UART2_BASE + 2)
#define UART2_LCR    (UART2_BASE + 3)
#define UART2_MCR    (UART2_BASE + 4)
#define UART2_LSR    (UART2_BASE + 5)
#define UART2_MSR    (UART2_BASE + 6)
#define UART2_SCR    (UART2_BASE + 7)

#define UART2_LSR_DR      0x01
#define UART2_LSR_THRE    0x20
#define UART2_LSR_TEMT    0x40

#define H4_RX_RING    1024
#define H4_PKT_MAX    1030     /* ACL: 4 hdr + 1024 payload               */

typedef enum h4_state {
    H4_WAIT_TYPE = 0,
    H4_WAIT_HDR,
    H4_WAIT_BODY,
} h4_state_t;

typedef struct h4_rx {
    h4_state_t state;
    u8  type;
    u8  hdr[4];
    u8  hdr_need;
    u8  hdr_have;
    u16 body_need;
    u16 body_have;
    u8  pkt[H4_PKT_MAX];
    u16 pkt_len;
    u8  pkt_ready;
} h4_rx_t;

static h4_rx_t g_rx;
static volatile u8  g_ring[H4_RX_RING];
static volatile u16 g_ring_head, g_ring_tail;
static u8 g_uart_ok;

/* ------------------------------------------------------------------ */
/* Byte plumbing                                                       */
/* ------------------------------------------------------------------ */
static inline void outb2(u16 port, u8 v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static inline u8 inb2(u16 port) { u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }

static void ring_push(u8 b)
{
    u16 next = (u16)((g_ring_head + 1) % H4_RX_RING);
    if (next == g_ring_tail) return;   /* overrun: drop (bounded IRQ)   */
    g_ring[g_ring_head] = b;
    g_ring_head = next;
}

static int ring_pop(u8 *b)
{
    if (g_ring_head == g_ring_tail) return 0;
    *b = g_ring[g_ring_tail];
    g_ring_tail = (u16)((g_ring_tail + 1) % H4_RX_RING);
    return 1;
}

static void h4_irq_handler(void *ctx, arch_irq_frame_t *frame)
{
    (void)ctx; (void)frame;
    /* drain bounded (BUG-0323 pattern: never spin inside an IRQ)       */
    for (int i = 0; i < 32; i++) {
        if (!(inb2(UART2_LSR) & UART2_LSR_DR)) break;
        ring_push(inb2(UART2_RBR));
    }
}

/* ------------------------------------------------------------------ */
/* Probe + init                                                        */
/* ------------------------------------------------------------------ */
int bt_h4_probe(void)
{
    u8 lsr, sent, got;
    /* a 16550 at 0x2f8 answers a loopback byte; an absent port reads
     * 0xff everywhere (QEMU without the second -serial).               */
    lsr = inb2(UART2_LSR);
    if (lsr == 0xff || lsr == 0x00) return 0;
    outb2(UART2_MCR, 0x10);            /* loopback mode                  */
    outb2(UART2_SCR, 0x00);
    sent = 0x5a;
    outb2(UART2_THR, sent);
    {
        u64 dl = core_timer_now_ms() + 20;
        for (;;) {
            if (inb2(UART2_LSR) & UART2_LSR_DR) break;
            if (core_timer_now_ms() >= dl) {
                outb2(UART2_MCR, 0x0b);
                return 0;
            }
            core_sched_yield();
        }
        got = inb2(UART2_RBR);
    }
    outb2(UART2_MCR, 0x0b);            /* normal: DTR|RTS|OUT2           */
    return got == sent ? 1 : 0;
}

int bt_h4_init(void)
{
    if (!bt_h4_probe()) return -1;
    g_uart_ok = 1;

    /* 115200 8N1 + FIFO                                                */
    outb2(UART2_IER, 0x00);
    outb2(UART2_LCR, 0x80);            /* DLAB                           */
    outb2(UART2_THR, 0x01);            /* divisor 1 = 115200             */
    outb2(UART2_IER, 0x00);
    outb2(UART2_LCR, 0x03);            /* 8N1                            */
    outb2(UART2_FCR, 0xc7);            /* enable + clear FIFOs, 14B trig */
    outb2(UART2_MCR, 0x0b);

    g_ring_head = g_ring_tail = 0;
    memset((void *)&g_rx, 0, sizeof(g_rx));

    arch_irq_register_handler(3, h4_irq_handler, 0);
    outb2(UART2_IER, 0x01);            /* RX interrupt enable            */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Framed TX                                                           */
/* ------------------------------------------------------------------ */
static void uart_write(u8 b)
{
    while (!(inb2(UART2_LSR) & UART2_LSR_THRE)) core_sched_yield();
    outb2(UART2_THR, b);
}

/* ------------------------------------------------------------------ */
/* Framed RX state machine (H4: type + per-type header lengths)        */
/*   COMMAND: opcode(2) plen(1) params                                 */
/*   EVENT:   code(1) plen(1) params                                   */
/*   ACL:     handle/flags(2) dlen(2) data                             */
/*   SCO:     handle(2) len(1) data                                    */
/* ------------------------------------------------------------------ */
static u8 h4_hdr_len(u8 type)
{
    switch (type) {
    case BT_H4_COMMAND: return 3;
    case BT_H4_EVENT:   return 2;
    case BT_H4_ACL:     return 4;
    case BT_H4_SCO:     return 3;
    default:            return 0;
    }
}

static u16 h4_body_len(u8 type, const u8 *hdr)
{
    switch (type) {
    case BT_H4_COMMAND: return hdr[2];
    case BT_H4_EVENT:   return hdr[1];
    case BT_H4_ACL:     return (u16)(hdr[2] | ((u16)hdr[3] << 8));
    case BT_H4_SCO:     return hdr[2];
    default:            return 0;
    }
}

static void h4_feed(u8 b)
{
    switch (g_rx.state) {
    case H4_WAIT_TYPE:
        if (b < BT_H4_COMMAND || b > BT_H4_EVENT) return;  /* SCO: skip */
        g_rx.type = b;
        g_rx.hdr_have = 0;
        g_rx.hdr_need = h4_hdr_len(b);
        g_rx.state = H4_WAIT_HDR;
        break;
    case H4_WAIT_HDR:
        g_rx.hdr[g_rx.hdr_have++] = b;
        if (g_rx.hdr_have >= g_rx.hdr_need) {
            g_rx.body_need = h4_body_len(g_rx.type, g_rx.hdr);
            if (g_rx.body_need > (u16)(H4_PKT_MAX - 4)) {
                g_rx.state = H4_WAIT_TYPE;   /* oversize: honest drop    */
                break;
            }
            g_rx.body_have = 0;
            g_rx.state = g_rx.body_need ? H4_WAIT_BODY : H4_WAIT_TYPE;
            if (!g_rx.body_need) {
                for (u8 i = 0; i < g_rx.hdr_need; i++)
                    g_rx.pkt[i] = g_rx.hdr[i];
                g_rx.pkt_len = g_rx.hdr_need;
                g_rx.pkt_ready = 1;
            }
        }
        break;
    case H4_WAIT_BODY:
        if (g_rx.body_have == 0)
            for (u8 i = 0; i < g_rx.hdr_need; i++)
                g_rx.pkt[i] = g_rx.hdr[i];
        /* payload directly follows the per-type header                 */
        g_rx.pkt[g_rx.hdr_need + g_rx.body_have] = b;
        g_rx.body_have++;
        if (g_rx.body_have >= g_rx.body_need) {
            g_rx.pkt_len = (u16)(g_rx.hdr_need + g_rx.body_need);
            g_rx.pkt_ready = 1;
            g_rx.state = H4_WAIT_TYPE;
        }
        break;
    }
}

int h4_recv(u8 *type_out, u8 *buf, u16 buflen, u16 *len_out)
{
    u8 b;

    if (!g_uart_ok) return -1;
    if (g_rx.pkt_ready) {
        if (g_rx.pkt_len > buflen) { g_rx.pkt_ready = 0; return -2; }
        for (u16 i = 0; i < g_rx.pkt_len; i++) buf[i] = g_rx.pkt[i];
        *type_out = g_rx.type;
        *len_out  = g_rx.pkt_len;
        g_rx.pkt_ready = 0;
        return 1;
    }
    /* no pending packet: drain IRQ ring into the frame machine        */
    while (ring_pop(&b)) {
        h4_feed(b);
        if (g_rx.pkt_ready) return h4_recv(type_out, buf, buflen, len_out);
    }
    /* also drain the FIFO directly in case the IRQ was late           */
    for (int i = 0; i < 16; i++) {
        if (!(inb2(UART2_LSR) & UART2_LSR_DR)) break;
        h4_feed(inb2(UART2_RBR));
        if (g_rx.pkt_ready) return h4_recv(type_out, buf, buflen, len_out);
    }
    return 0;
}

static int h4_send(u8 type, const u8 *data, u16 len)
{
    if (!g_uart_ok) return -1;
    uart_write(type);
    for (u16 i = 0; i < len; i++) uart_write(data[i]);
    return 0;
}

static int h4_poll_status(char *out, int outlen)
{
    int o = 0;
    if (outlen > 0) out[0] = '\0';
    if (!g_uart_ok) {
        const char *s = "h4: uart2 not responding\n";
        while (s[o] && o < outlen - 1) { out[o] = s[o]; o++; }
        out[o] = '\0';
        return -1;
    }
    {
        const char *s = "h4: uart2 ok, rx ring ";
        while (s[o] && o < outlen - 1) { out[o] = s[o]; o++; }
        {
            char num[8];
            u64_to_str((u64)((g_ring_head - g_ring_tail + H4_RX_RING) %
                             H4_RX_RING), num);
            int i = 0;
            while (num[i] && o < outlen - 1) out[o++] = num[i++];
        }
        if (o < outlen - 2) { out[o++] = '\n'; }
        out[o] = '\0';
    }
    return 0;
}

static const bt_transport_ops_t g_h4_ops = {
    "h4-uart2", h4_send, h4_recv, h4_poll_status,
};

const bt_transport_ops_t *bt_h4_ops(void) { return &g_h4_ops; }
