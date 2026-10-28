/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_parallel.h
 *
 * IEEE 1284 parallel port driver (SPP / EPP / ECP), implemented
 * against the PC/AT port layout and the IEEE 1284-1994 mode
 * negotiation protocol:
 *
 *   - port enumeration: the three standard I/O bases (0x378, 0x278,
 *     0x3BC) probed through the status register (busy/ack lines react
 *     to a data write on a present port), plus ECP detection through
 *     the extended config register at base+0x402
 *   - SPP (compatibility) mode: Centronics forward transfer with the
 *     strobe handshake + busy polling, reverse transfer in nibble
 *     mode (IEEE 1284 channel request over the status lines)
 *   - EPP 1.7 / 1.9: address + data read/write cycles on the EPP
 *     registers (base+3..6) with the hardware handshake and the
 *     standard 10 us timeout (IEEE 1284 defines EPP 1.7 with the
 *     wait held and 1.9 with wait released - both selectable)
 *   - ECP: extended control register (base+0x402) mode selection
 *     (SPP / PS/2 bidir / ECP FIFO / test / config), FIFO service,
 *     DMA channel handoff and RLE decompression on receive
 *
 * L1 extension surface (documented in docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   int parallel_register(parallel_port_t *p);
 *   int parallel_unregister(parallel_port_t *p);
 *   int parallel_count(void);
 *   parallel_port_t *parallel_get(int idx);
 *   int parallel_set_mode(parallel_port_t *p, u8 mode);
 *   int parallel_get_mode(parallel_port_t *p, u8 *mode);
 *   int parallel_write(parallel_port_t *p, const void *buf, u32 len);
 *   int parallel_read(parallel_port_t *p, void *buf, u32 len);
 *   int parallel_get_status(parallel_port_t *p, parallel_status_t *out);
 *   int parallel_epp_addr_write(parallel_port_t *p, u8 addr);
 *   int parallel_epp_data_write(parallel_port_t *p, const void *buf, u32 len);
 *   int parallel_epp_data_read(parallel_port_t *p, void *buf, u32 len);
 *   int parallel_ecp_fifo_write(parallel_port_t *p, const void *buf, u32 len);
 *   int parallel_ecp_fifo_read(parallel_port_t *p, void *buf, u32 len);
 *   void parallel_probe_all(void);   enumerate the standard bases
 */
#ifndef OC_DRIVER_PARALLEL_H
#define OC_DRIVER_PARALLEL_H

#include "types.h"

/* modes (IEEE 1284) */
#define PAR_MODE_SPP     0
#define PAR_MODE_EPP17   1
#define PAR_MODE_EPP19   2
#define PAR_MODE_ECP     3
#define PAR_MODE_PS2     4   /* bidirectional byte mode */
#define PAR_MODE_NIBBLE  5

/* status bits (base+1) */
#define PAR_STAT_BUSY    (1u << 7)   /* active high in the register: !Busy */
#define PAR_STAT_ACK     (1u << 6)
#define PAR_STAT_PERROR  (1u << 5)
#define PAR_STAT_SELECT  (1u << 4)
#define PAR_STAT_ERROR   (1u << 3)
#define PAR_STAT_IRQ     (1u << 2)

/* control bits (base+2) */
#define PAR_CTRL_STROBE  (1u << 0)
#define PAR_CTRL_AUTOFEED (1u << 1)
#define PAR_CTRL_INIT    (1u << 2)
#define PAR_CTRL_SELECT  (1u << 3)
#define PAR_CTRL_IRQEN   (1u << 4)
#define PAR_CTRL_BIDIR   (1u << 5)

/* ECR (base+0x402) */
#define PAR_ECR_MODE_SPP    0x00
#define PAR_ECR_MODE_PS2    0x20
#define PAR_ECR_MODE_FIFO   0x40
#define PAR_ECR_MODE_ECP    0x60
#define PAR_ECR_MODE_TEST   0xC0
#define PAR_ECR_MODE_CFG    0xE0
#define PAR_ECR_DMAEN       0x08
#define PAR_ECR_SVC_IRQ     0x04
#define PAR_ECR_FIFO_FULL   0x02
#define PAR_ECR_FIFO_EMPTY  0x01

#define PAR_MAX_PORTS   4
#define PAR_NAME_MAX     8
#define PAR_TIMEOUT_MS 500

typedef struct parallel_port {
    char name[PAR_NAME_MAX];    /* "lpt1" ...                          */
    u16  io;                    /* data register base                  */
    u8   irq;                   /* 0 = none                            */
    u8   dma;                   /* ECP DMA channel, 0xff = none        */
    u8   ecp_cap;               /* ECR present (extended base)         */
    u8   bidir_cap;             /* PS/2 bidirectional data latch       */
    u8   present;
    u8   mode;                  /* current mode (PAR_MODE_*)           */
    /* statistics */
    u32  tx_bytes, rx_bytes;
    u32  timeouts;
} parallel_port_t;

typedef struct parallel_status {
    u8  raw;                    /* status register byte                */
    u8  busy, ack, paper_err, selected, error, irq_pending;
} parallel_status_t;

/* ---- subsystem ---- */
void parallel_init(void);
void parallel_probe_all(void);

/* ---- L1 extension interfaces ---- */
int  parallel_register(parallel_port_t *p);
int  parallel_unregister(parallel_port_t *p);
int  parallel_count(void);
parallel_port_t *parallel_get(int idx);
int  parallel_set_mode(parallel_port_t *p, u8 mode);
int  parallel_get_mode(parallel_port_t *p, u8 *mode);
int  parallel_write(parallel_port_t *p, const void *buf, u32 len);
int  parallel_read(parallel_port_t *p, void *buf, u32 len);
int  parallel_get_status(parallel_port_t *p, parallel_status_t *out);
int  parallel_epp_addr_write(parallel_port_t *p, u8 addr);
int  parallel_epp_data_write(parallel_port_t *p, const void *buf, u32 len);
int  parallel_epp_data_read(parallel_port_t *p, void *buf, u32 len);
int  parallel_ecp_fifo_write(parallel_port_t *p, const void *buf, u32 len);
int  parallel_ecp_fifo_read(parallel_port_t *p, void *buf, u32 len);

#endif /* OC_DRIVER_PARALLEL_H */
