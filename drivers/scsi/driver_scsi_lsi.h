/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/scsi/driver_scsi_lsi.h
 *
 * LSI 53C895A SCSI HBA backend.  The chip is driven through its
 * PCI I/O BAR (256 bytes of registers) and its on-board SCRIPTS
 * engine: for every transaction the driver builds a small SCRIPTS
 * program in host memory (SELECT with ATN -> MSG OUT the IDENTIFY
 * -> COMMAND the CDB -> the data phase -> STATUS -> MSG IN -> a
 * terminating Interrupt instruction), points DSP at it and waits
 * for the completion interrupt the INT instruction raises.
 *
 * Command set notes (53C895A Programming Guide + QEMU model):
 *   Block Move   insn>>30 == 0, phase in bits 26:24, count 23:0,
 *                then one dword with the (physical) buffer address
 *   Select       insn>>30 == 1, opcode 0, target id in bits 19:16,
 *                0x47000000 == "Select with ATN, relative jump"
 *   Interrupt    insn>>30 == 2, opcode 3 -> raises DSTAT.SIR
 *
 * Polling model (like AHCI/NVMe in this kernel): wait for
 * ISTAT0.DIP|SIP, read DSTAT/SIST0/SIST1 to identify the outcome.
 */
#ifndef OC_DRIVER_SCSI_LSI_H
#define OC_DRIVER_SCSI_LSI_H

#include "types.h"

/* PCI coordinates of the QEMU -device lsi model */
#define LSI_PCI_VENDOR   0x1000   /* PCI_VENDOR_ID_LSI_LOGIC          */
#define LSI_PCI_DEVICE   0x0012   /* PCI_DEVICE_ID_LSI_53C895A        */

/* 53C895A register offsets (I/O BAR, 256 bytes)                      */
#define LSI_SCNTL0       0x00
#define LSI_SCNTL1       0x01
#define LSI_SCNTL1_CON   0x10     /* connected bit                   */
#define LSI_SCNTL2       0x02
#define LSI_SCNTL3       0x03
#define LSI_SCID         0x04
#define LSI_SXFER        0x05
#define LSI_SDID         0x06
#define LSI_SFBR         0x08
#define LSI_DSTAT        0x0C
#define LSI_SSTAT0       0x0D
#define LSI_SSTAT1       0x0E
#define LSI_SSTAT2       0x0F
#define LSI_ISTAT0       0x14
#define LSI_ISTAT1       0x15
#define LSI_CTEST2       0x1A
#define LSI_DBC          0x24     /* 24-bit count, DCMD at 0x27       */
#define LSI_DNAD         0x28
#define LSI_DSP          0x2C     /* writing byte 3 starts SCRIPTS    */
#define LSI_DSPS         0x30
#define LSI_SCRATCHA     0x34
#define LSI_DMODE        0x38
#define LSI_DIEN         0x39
#define LSI_SBR          0x3A
#define LSI_DCNTL        0x3B
#define LSI_SIEN0        0x40
#define LSI_SIEN1        0x41
#define LSI_SIST0        0x42
#define LSI_SIST1        0x43
#define LSI_STEST2       0x4E
#define LSI_STEST3       0x4F
#define LSI_DSA          0x54

/* ISTAT0 bits                                                        */
#define LSI_ISTAT0_DIP   0x01     /* DMA interrupt pending            */
#define LSI_ISTAT0_SIP   0x02     /* SCSI interrupt pending           */
#define LSI_ISTAT0_SIGP  0x20
#define LSI_ISTAT0_SRST  0x40     /* soft reset                       */
#define LSI_ISTAT0_ABRT  0x80

/* DSTAT bits (read clears)                                           */
#define LSI_DSTAT_SIR    0x04     /* SCRIPTS interrupt instruction    */
#define LSI_DSTAT_IID    0x01     /* illegal instruction              */
#define LSI_DSTAT_ABRT   0x10
#define LSI_DSTAT_BF     0x20     /* bus fault                        */
#define LSI_DSTAT_MDPE   0x40     /* master data parity error         */

/* SIST0 bits (read clears)                                           */
#define LSI_SIST0_MA     0x80     /* phase mismatch                   */
#define LSI_SIST0_CMP    0x40
#define LSI_SIST0_SEL    0x20
#define LSI_SIST0_RSL    0x10
#define LSI_SIST0_SGE    0x08     /* SCSI gross error                 */
#define LSI_SIST0_UDC    0x04     /* unexpected disconnect            */
#define LSI_SIST0_RST    0x02
#define LSI_SIST0_PAR    0x01

/* SIST1 bits (read clears)                                           */
#define LSI_SIST1_RSL    0x10     /* reselected                     */
#define LSI_SIST1_STO    0x04     /* selection timeout                */
#define LSI_SIST1_GEN    0x02
#define LSI_SIST1_HTH    0x01

/* SCSI bus phases (Block Move phase field / SSTAT1 low nibble)       */
#define LSI_PHASE_DO    0
#define LSI_PHASE_DI    1
#define LSI_PHASE_CMD   2
#define LSI_PHASE_STAT  3
#define LSI_PHASE_MO    6
#define LSI_PHASE_MI    7

/* SCRIPTS engine state (driver_scsi_lsi.c)                           */
typedef struct scsi_lsi_state {
    u16   io_base;                  /* I/O BAR port base              */
    u8    irq_line;
    u8    present;
    u8    initiator_id;             /* 7 by default                   */
    u32  *script;                   /* kmalloc'd SCRIPTS buffer       */
    /* per-transaction I/O windows (physical == virtual here)          */
    u8    msg_out;
    u8    status;
    u8    msg_in;
} scsi_lsi_state_t;

/* Probe PCI for the LSI 53C895A, reset + configure it and scan
 * targets 0..6 (INQUIRY per target) against the SCSI core.  Returns
 * the number of targets attached (0 when the chip is absent, which
 * is the honest state under machines that do not model one). */
int  driver_scsi_lsi_init(void);

#endif /* OC_DRIVER_SCSI_LSI_H */
