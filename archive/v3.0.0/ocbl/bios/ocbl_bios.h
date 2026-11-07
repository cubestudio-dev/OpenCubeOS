/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_bios.h
 * Purpose: BIOS services from the 64-bit core (BIOS flavor only).
 *
 *          ocbl_bios_int() hands a register block to the 64->16 real-mode
 *          trampoline (ocbl/bios/tramp16.asm, copied to 0x40000 at init),
 *          which runs the INT, saves the results and returns.
 *
 *          CALLEE-SAVED registers (rbx/rbp/r12-r15) are preserved by the
 *          wrapper around the whole trampoline round trip; volatile regs
 *          are per the C ABI.  XMM/x87 are NOT preserved - the OCBL core
 *          is compiled -mno-sse and uses no floating point at all, so a
 *          BIOS clobbering them is harmless (declared design).
 *
 *          Register block offsets are MIRRORED in tramp16.asm (B_*).
 *          The trampoline base and the block address are fixed by
 *          construction (0x40000 / 0x40100).
 */
#ifndef OCBL_BIOS_H
#define OCBL_BIOS_H

#include "ocbl_types.h"

#define OCBL_TRAMP_BASE   0x40000u
#define OCBL_TRAMP_BLOCK  0x40300u
#define OCBL_TRAMP_DAP    0x40500u   /* 16-byte DAP area */
#define OCBL_TRAMP_SCRATCH 0x40600u  /* AH=48h geometry scratch */
#define OCBL_TRAMP_STACK  0x40E00u

/* Register block (mirror tramp16.asm B_*).  General registers are
 * FULL 32-bit slots: E820 passes EDX='SMAP' and returns EAX='SM' -
 * real-mode 0x66-prefixed 32-bit moves are legal and used by the
 * trampoline.  Set the 32-bit fields even for 16-bit services. */
typedef struct ocbl_bios_regs {
    u32 eax, ebx, ecx, edx;   /* in/out */
    u32 esi, edi;             /* in/out */
    u16 es, ds;               /* in (es also out) */
    u16 flags;                /* out: real-mode FLAGS after the INT */
} ocbl_bios_regs_t;

_Static_assert(sizeof(ocbl_bios_regs_t) == 32, "regs struct layout drift");

/* Copy the trampoline blob to 0x40000 (once, before the first call). */
void ocbl_tramp_init(void);

/* Run a BIOS INT.  Returns the CF bit (1 = carry set). */
u32 ocbl_bios_int(u16 int_no, ocbl_bios_regs_t* r);

#endif /* OCBL_BIOS_H */
