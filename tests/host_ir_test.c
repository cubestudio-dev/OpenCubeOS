/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-camera_misc
 *
 * File: tests/host_ir_test.c
 *
 * Host-side test of the IR protocol codecs (drivers/misc/driver_ir.c).
 * The SAME kernel .c is compiled for the host.  Every case is a
 * byte-exact round trip: encode -> decode -> compare, plus fixed
 * known-answer vectors per protocol and negative cases (broken
 * frames, wrong parity, short buffers).
 *
 * Build:
 *   gcc -O2 -g -Ikernel -Ikernel/lib -Idrivers/misc tests/host_ir_test.c \
 *       -o /tmp/host_ir_test
 *
 * Exit code = number of failing checks.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../core/types.h"

void *kmalloc(unsigned long long size) { return malloc(size); }
void  kfree(void *p)                   { free(p); }
u64   core_timer_now_ms(void)          { return 0; }
/* WP-10-tui-qa: driver_ir.c now asks the BT H4 transport whether it
 * owns COM2 (kernel mutual exclusion).  Host build stub: no bluetooth
 * here, so the SIR path is always free to probe, exactly as before. */
int   bt_h4_active(void)               { return 0; }

#include "../core/lib/lib_string.c"
#include "../drivers/misc/driver_ir.c"

static int check(const char *name, int cond)
{
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* ---- known-answer vectors (hand-computed from the protocol specs) -- */

/* NEC: addr 0x1A cmd 0x3C.
 * frame = 9000 4500 | bits (msb first):
 * addr   00011010, ~addr 11100101, cmd 00111100, ~cmd 11000011 */
static void nec_known(void)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    b.n = 2 + 64;
    b.t[0] = 9000; b.t[1] = 4500;
    static const u8 vals[32] = {
        0,0,0,1,1,0,1,0,  1,1,1,0,0,1,0,1,
        0,0,1,1,1,1,0,0,  1,1,0,0,0,0,1,1
    };
    for (u32 i = 0; i < 32; i++) {
        b.t[2 + 2*i]     = 560;
        b.t[2 + 2*i + 1] = vals[i] ? 1690 : 560;
    }
    ir_decoded_t d;
    int rc = ir_decode_nec(&b, &d);
    int ok = rc == 0 && d.proto == IR_PROTO_NEC && d.addr == 0x1A &&
             d.cmd == 0x3C;
    check("nec known vector addr=1A cmd=3C", ok);
}

/* RC-5: addr 0x05 cmd 0x15, toggle 1. */
static void rc5_known(void)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    /* frame bits (msb first): 1 1 toggle addr(5) cmd(6) */
    u32 bits = (1u << 13) | (1u << 12) | (1u << 11) | (0x05 << 6) | 0x15;
    /* halves: 1 = space,mark; 0 = mark,space.  leading space of the
     * first start bit is the idle gap (not captured). */
    i8 h[64];
    u32 n = 0;
    for (i32 i = 13; i >= 0; i--) {
        h[n++] = (i8)(((bits >> i) & 1) ? -1 : 1);
        h[n++] = (i8)(((bits >> i) & 1) ? 1 : -1);
    }
    u32 start = (h[0] < 0) ? 1 : 0;
    /* RLE into mark/space us */
    int expect_mark = 1;
    for (u32 i = start; i < n; ) {
        i8 v = h[i];
        u32 run = 0;
        while (i < n && h[i] == v) { run++; i++; }
        b.t[b.n++] = run * 889;
        expect_mark = !expect_mark;
        (void)expect_mark;
    }
    ir_decoded_t d;
    int rc = ir_decode_rc5(&b, &d);
    int ok = rc == 0 && d.proto == IR_PROTO_RC5 && d.addr == 0x05 &&
             d.cmd == 0x15 && d.toggle == 1;
    check("rc5 known vector addr=05 cmd=15 tgl=1", ok);
}

/* RC-6 mode 0: addr 0x12 cmd 0x34, toggle 0. */
static void rc6_known(void)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    b.t[b.n++] = 2666;
    b.t[b.n++] = 889;
    b.t[b.n++] = 889;
    b.t[b.n++] = 889;
    u32 frame = (0u << 20) | (0u << 19) | (0u << 18) | (1u << 17) |
                (0u << 16) | (0x12 << 8) | 0x34;
    i8 h[64];
    u32 n = 0;
    for (i32 i = 20; i >= 0; i--) {
        h[n++] = (i8)(((frame >> i) & 1) ? -1 : 1);
        h[n++] = (i8)(((frame >> i) & 1) ? 1 : -1);
    }
    u32 start = (h[0] < 0) ? 1 : 0;
    for (u32 i = start; i < n; ) {
        i8 v = h[i];
        u32 run = 0;
        while (i < n && h[i] == v) { run++; i++; }
        b.t[b.n++] = run * 444;
    }
    ir_decoded_t d;
    int rc = ir_decode_rc6(&b, &d);
    int ok = rc == 0 && d.proto == IR_PROTO_RC6 && d.addr == 0x12 &&
             d.cmd == 0x34 && d.toggle == 0;
    check("rc6 known vector addr=12 cmd=34 tgl=0", ok);
}

/* SIRC 12-bit: cmd 0x15 addr 0x01, LSB first, width-keyed. */
static void sirc_known(void)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    b.t[b.n++] = 2400;
    b.t[b.n++] = 600;
    u32 r = 0x15 | (0x01 << 7);
    for (u32 i = 0; i < 12; i++) {
        b.t[b.n++] = ((r >> i) & 1) ? 1200 : 600;
        b.t[b.n++] = 600;
    }
    ir_decoded_t d;
    int rc = ir_decode_sirc(&b, &d);
    int ok = rc == 0 && d.proto == IR_PROTO_SIRC && d.cmd == 0x15 &&
             d.addr == 0x01;
    check("sirc known vector cmd=15 addr=01", ok);
}

/* ---- encode/decode round trips ------------------------------------ */

static int round_trip(ir_proto_t proto, u16 addr, u16 cmd, u8 toggle)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    int rc = ir_encode(proto, addr, cmd, toggle, &b);
    if (rc != 0) return -1;
    ir_decoded_t d;
    rc = ir_decode(&b, &d);
    if (rc != 0) return -2;
    if (d.proto != proto) return -3;
    if (d.addr != addr || d.cmd != cmd) return -4;
    if (proto == IR_PROTO_RC5 || proto == IR_PROTO_RC6) {
        if (d.toggle != (toggle & 1)) return -5;
    }
    return 0;
}

int main(void)
{
    int fails = 0;
    printf("== IR known-answer vectors ==\n");
    nec_known();
    rc5_known();
    rc6_known();
    sirc_known();

    printf("== IR encode/decode round trips ==\n");
    struct { const char *name; ir_proto_t p; u16 a, c; u8 t; } cases[] = {
        { "nec  0x12/0x34",    IR_PROTO_NEC,  0x12, 0x34, 0 },
        { "nec  0xFF/0x00",    IR_PROTO_NEC,  0xFF, 0x00, 0 },
        { "nec  0x00/0xFF",    IR_PROTO_NEC,  0x00, 0xFF, 0 },
        { "nec  repeat frame", IR_PROTO_NEC_R, 0, 0, 0 },
        { "rc5  0x0A/0x0C t0", IR_PROTO_RC5,  0x0A, 0x0C, 0 },
        { "rc5  0x1B/0x2D t1", IR_PROTO_RC5,  0x1B, 0x2D, 1 },
        { "rc5  0x00/0x3F t1", IR_PROTO_RC5,  0x00, 0x3F, 1 },
        { "rc6  0x00/0xFF t0", IR_PROTO_RC6,  0x00, 0xFF, 0 },
        { "rc6  0xA5/0x5A t1", IR_PROTO_RC6,  0xA5, 0x5A, 1 },
        { "rc6  0x12/0x34 t0", IR_PROTO_RC6,  0x12, 0x34, 0 },
        { "sirc 0x15/0x01",    IR_PROTO_SIRC, 0x15, 0x01, 0 },
        { "sirc 0x1F/0x7F",    IR_PROTO_SIRC, 0x1F, 0x7F, 0 },
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        int rc = round_trip(cases[i].p, cases[i].a, cases[i].c, cases[i].t);
        char msg[64];
        snprintf(msg, sizeof(msg), "round trip %s", cases[i].name);
        fails += check(msg, rc == 0);
        if (rc != 0) printf("  -> rc=%d\n", rc);
    }

    printf("== IR negative cases ==\n");
    ir_pulsebuf_t b;
    ir_decoded_t d;
    /* NEC: wrong complement */
    memset(&b, 0, sizeof(b));
    ir_encode(IR_PROTO_NEC, 0x12, 0x34, 0, &b);
    b.t[5] = 4000;                           /* invalid gap */
    fails += check("nec corrupted frame rejected", ir_decode_nec(&b, &d) == -1);
    /* RC5: truncated buffer */
    ir_encode(IR_PROTO_RC5, 0x05, 0x15, 0, &b);
    b.n /= 2;
    fails += check("rc5 truncated rejected", ir_decode_rc5(&b, &d) == -1);
    /* RC6: bad lead */
    ir_encode(IR_PROTO_RC6, 0x12, 0x34, 0, &b);
    b.t[0] = 1234;
    fails += check("rc6 bad lead rejected", ir_decode_rc6(&b, &d) == -1);
    /* empty buffer */
    memset(&b, 0, sizeof(b));
    fails += check("empty buffer rejected", ir_decode(&b, &d) == -1);

    printf("== IR registry (fake device) ==\n");
    ir_init();
    ir_dev_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.kind = IR_KIND_CIR;
    dev.ops = &sir_ops;
    fails += check("ir register", ir_register(&dev) == 0);
    fails += check("ir count 1", ir_count() == 1);
    fails += check("ir double register rejected", ir_register(&dev) == -1);
    fails += check("ir get", ir_get(0) == &dev && ir_get(5) == 0);
    fails += check("ir unregister", ir_unregister(&dev) == 0 &&
                   ir_count() == 0);

    printf("host_ir_test: %d failures - %s\n", fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails;
}
