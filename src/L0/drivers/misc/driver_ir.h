/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_ir.h
 *
 * Consumer / serial infrared subsystem: carrier-based CIR hardware and
 * UART-based SIR hardware behind one interface, with protocol codecs
 * for NEC, RC-5 and RC-6 (mode 0) - plus Sony SIRC 12-bit.
 *
 * The time-domain currency is ir_pulsebuf_t: an alternating mark/space
 * list in microseconds, starting with a mark.  Decoders take a buffer
 * of captured edges; encoders produce one for the transmitter.  All
 * protocol code is pure integer logic and is exercised both by the
 * host build (tests/host_ir_test.c) and by the in-kernel `irtest`
 * command on the same round-trip vectors.
 *
 * Timing tolerances follow the IR remote protocols in general use:
 *   NEC  (38 kHz): lead 9 ms + 4.5 ms; bit mark 560 us; space
 *        560 us (0) / 1690 us (1); 32 bits addr,~addr,cmd,~cmd;
 *        repeat frame 9 ms + 2.25 ms + 560 us mark.
 *   RC-5 (36 kHz): half-bit 889 us, Manchester; 14 bits:
 *        start1 start2 toggle addr(5) cmd(6).
 *   RC-6 (36 kHz): lead mark 2666 us + space 889; trailer bit 889/889;
 *        half-bit 444 us; 20 bits: mode(3)=0 start(1) toggle addr(8)
 *        cmd(8) (mode 0 layout).
 *   SIRC (40 kHz): lead 2400/600; 12 bits: cmd(7) addr(5), 600/1200
 *        pulse widths (12-bit variant).
 *
 * L1 extension surface (documented in docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   int ir_register(ir_dev_t *d);
 *   int ir_unregister(ir_dev_t *d);
 *   int ir_count(void);
 *   ir_dev_t *ir_get(int idx);
 *   int ir_recv(ir_dev_t *d, ir_pulsebuf_t *out, u32 timeout_ms);
 *   int ir_decode(const ir_pulsebuf_t *in, ir_decoded_t *out);
 *   int ir_encode(ir_proto_t proto, u16 addr, u16 cmd, u8 toggle,
 *                 ir_pulsebuf_t *out);
 *   int ir_send(ir_dev_t *d, const ir_pulsebuf_t *p);
 */
#ifndef OC_DRIVER_IR_H
#define OC_DRIVER_IR_H

#include "types.h"

/* device kinds */
#define IR_KIND_NONE  0
#define IR_KIND_CIR   1      /* consumer IR (carrier hardware)         */
#define IR_KIND_SIR   2      /* serial IR (IrDA 115.2k UART, 3/16 pulse) */

/* protocols */
#define IR_PROTO_NONE  0
#define IR_PROTO_NEC   1
#define IR_PROTO_RC5   2
#define IR_PROTO_RC6   3
#define IR_PROTO_SIRC  4
#define IR_PROTO_NEC_R 5   /* NEC repeat frame */

typedef u8 ir_proto_t;

#define IR_MAX_PULSES  128

typedef struct ir_pulsebuf {
    u32 n;                       /* number of intervals (mark first)  */
    u32 t[IR_MAX_PULSES];        /* microseconds per interval         */
} ir_pulsebuf_t;

typedef struct ir_decoded {
    u8  proto;                   /* IR_PROTO_*                        */
    u16 addr;
    u16 cmd;
    u8  toggle;                  /* RC-5/RC-6 toggle bit              */
    u8  repeat;                  /* NEC repeat frame                  */
} ir_decoded_t;

typedef struct ir_dev ir_dev_t;

typedef struct ir_ops {
    int (*send)(ir_dev_t *d, const ir_pulsebuf_t *p);
    int (*recv)(ir_dev_t *d, ir_pulsebuf_t *out, u32 timeout_ms);
    int (*set_carrier)(ir_dev_t *d, u32 hz);
} ir_ops_t;

struct ir_dev {
    char name[8];
    u8   kind;                   /* IR_KIND_*                         */
    u32  carrier_hz;             /* current tx carrier                */
    u8   present;
    const ir_ops_t *ops;
    void *priv;
    u32  tx_frames, rx_frames;
};

/* ---- subsystem ---- */
void ir_init(void);
void ir_probe_all(void);

/* ---- L1 extension interfaces ---- */
int  ir_register(ir_dev_t *d);
int  ir_unregister(ir_dev_t *d);
int  ir_count(void);
ir_dev_t *ir_get(int idx);
int  ir_recv(ir_dev_t *d, ir_pulsebuf_t *out, u32 timeout_ms);
int  ir_decode(const ir_pulsebuf_t *in, ir_decoded_t *out);
int  ir_encode(ir_proto_t proto, u16 addr, u16 cmd, u8 toggle,
               ir_pulsebuf_t *out);
int  ir_send(ir_dev_t *d, const ir_pulsebuf_t *p);

/* protocol helpers exported for the tests */
int  ir_decode_nec(const ir_pulsebuf_t *in, ir_decoded_t *out);
int  ir_decode_rc5(const ir_pulsebuf_t *in, ir_decoded_t *out);
int  ir_decode_rc6(const ir_pulsebuf_t *in, ir_decoded_t *out);
int  ir_decode_sirc(const ir_pulsebuf_t *in, ir_decoded_t *out);

#endif /* OC_DRIVER_IR_H */
