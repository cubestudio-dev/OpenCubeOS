/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: services/camera/cam_cmds_ir.c
 *
 * ir (status) + irtest (self test).  The self test runs the same
 * round-trip vectors as the host build (NEC / RC-5 / RC-6 / SIRC
 * encode -> decode), the known-answer frames, and the negative cases;
 * live-hardware send/recv runs only when an IR device is bound.
 */
#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "driver_ir.h"
#include "cam_cmds_ir.h"

static void put(const char *s)
{
    screen_console_puts(s);
}
static void put_u64(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}

static int step(const char *name, int ok)
{
    put("  ");
    put(name);
    put(": ");
    put(ok ? "PASS" : "FAIL");
    put("\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* ir (status)                                                         */
/* ------------------------------------------------------------------ */

int cam_cmd_ir(const char *args)
{
    (void)args;
    int n = ir_count();
    put("ir devices: ");
    put_u64((u64)n);
    put("\n");
    for (int i = 0; i < n; i++) {
        ir_dev_t *d = ir_get(i);
        if (!d) continue;
        put("  ");
        put(d->name);
        put(" kind=");
        put(d->kind == IR_KIND_CIR ? "CIR" : "SIR");
        put(" carrier=");
        put_u64(d->carrier_hz);
        put(" tx=");
        put_u64(d->tx_frames);
        put(" rx=");
        put_u64(d->rx_frames);
        put("\n");
    }
    if (n == 0)
        put("  no IR device present (QEMU models neither CIR hardware"
            " nor an IrDA UART; the SIR probe runs against the"
            " secondary 16550 at boot)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* irtest                                                              */
/* ------------------------------------------------------------------ */

/* one encode -> decode round trip */
static int rt(ir_proto_t proto, u16 addr, u16 cmd, u8 toggle)
{
    ir_pulsebuf_t b;
    memset(&b, 0, sizeof(b));
    if (ir_encode(proto, addr, cmd, toggle, &b) != 0) return -1;
    ir_decoded_t d;
    if (ir_decode(&b, &d) != 0) return -2;
    if (d.proto != proto) return -3;
    if (d.addr != addr || d.cmd != cmd) return -4;
    if ((proto == IR_PROTO_RC5 || proto == IR_PROTO_RC6) &&
        d.toggle != (toggle & 1)) return -5;
    return 0;
}

int cam_cmd_irtest(const char *args)
{
    (void)args;
    int fails = 0;
    put("irtest: NEC / RC5 / RC6 / SIRC codec round trips\n");

    /* known-answer vectors (hand-built per spec) */
    {
        /* NEC addr 0x1A cmd 0x3C */
        ir_pulsebuf_t b;
        memset(&b, 0, sizeof(b));
        b.n = 66;
        b.t[0] = 9000;
        b.t[1] = 4500;
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
        fails += step("nec known (1A/3C)", rc == 0 && d.addr == 0x1A &&
                      d.cmd == 0x3C);
    }
    {
        /* RC5 addr 0x05 cmd 0x15 toggle 1 */
        ir_pulsebuf_t b;
        memset(&b, 0, sizeof(b));
        u32 bits = (1u << 13) | (1u << 12) | (1u << 11) |
                   (0x05 << 6) | 0x15;
        i8 h[64];
        u32 n = 0;
        for (i32 i = 13; i >= 0; i--) {
            h[n++] = (i8)(((bits >> i) & 1) ? -1 : 1);
            h[n++] = (i8)(((bits >> i) & 1) ? 1 : -1);
        }
        u32 start = (h[0] < 0) ? 1 : 0;
        for (u32 i = start; i < n; ) {
            i8 v = h[i];
            u32 run = 0;
            while (i < n && h[i] == v) { run++; i++; }
            b.t[b.n++] = run * 889;
        }
        ir_decoded_t d;
        int rc = ir_decode_rc5(&b, &d);
        fails += step("rc5 known (05/15 t1)", rc == 0 && d.addr == 0x05 &&
                      d.cmd == 0x15 && d.toggle == 1);
    }
    {
        /* RC6 mode0 addr 0x12 cmd 0x34 toggle 0 */
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
        fails += step("rc6 known (12/34 t0)", rc == 0 && d.addr == 0x12 &&
                      d.cmd == 0x34);
    }

    /* round trips */
    fails += step("rt nec 0x12/0x34", rt(IR_PROTO_NEC, 0x12, 0x34, 0) == 0);
    fails += step("rt nec 0xFF/0x00", rt(IR_PROTO_NEC, 0xFF, 0x00, 0) == 0);
    fails += step("rt nec repeat", rt(IR_PROTO_NEC_R, 0, 0, 0) == 0);
    fails += step("rt rc5 0x0A/0x0C t0", rt(IR_PROTO_RC5, 0x0A, 0x0C, 0) == 0);
    fails += step("rt rc5 0x1B/0x2D t1", rt(IR_PROTO_RC5, 0x1B, 0x2D, 1) == 0);
    fails += step("rt rc6 0x00/0xFF t0", rt(IR_PROTO_RC6, 0x00, 0xFF, 0) == 0);
    fails += step("rt rc6 0xA5/0x5A t1", rt(IR_PROTO_RC6, 0xA5, 0x5A, 1) == 0);
    fails += step("rt sirc 0x15/0x01", rt(IR_PROTO_SIRC, 0x15, 0x01, 0) == 0);

    /* negative: corrupted NEC frame */
    {
        ir_pulsebuf_t b;
        memset(&b, 0, sizeof(b));
        ir_encode(IR_PROTO_NEC, 0x12, 0x34, 0, &b);
        b.t[5] = 4000;
        ir_decoded_t d;
        fails += step("nec corrupted rejected", ir_decode_nec(&b, &d) == -1);
    }

    /* live device (SIR loopback peer) */
    {
        int n = ir_count();
        put("  live ir devices: ");
        put_u64((u64)n);
        put("\n");
        if (n > 0) {
            ir_dev_t *d = ir_get(0);
            ir_pulsebuf_t b;
            memset(&b, 0, sizeof(b));
            ir_encode(IR_PROTO_NEC, 0x42, 0x24, 0, &b);
            int rc = ir_send(d, &b);
            fails += step("live send", rc == 0);
            ir_pulsebuf_t rx;
            memset(&rx, 0, sizeof(rx));
            rc = ir_recv(d, &rx, 300);
            fails += step("live recv", rc == 0);
        } else {
            put("  (no live device: codec tests above ran on the real"
                " code paths)\n");
        }
    }

    put("irtest: ");
    put_u64((u64)fails);
    put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    return fails == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
