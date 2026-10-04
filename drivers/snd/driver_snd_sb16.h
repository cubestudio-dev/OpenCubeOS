/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/sb16.h
 * Purpose: Sound Blaster 16 driver interface (sb16.c).
 *
 * L1 extension surface:
 *
 *   int driver_snd_sb16_init(void *isa_dev);
 *       Detect and initialise a Sound Blaster 16 at the classic ISA
 *       location (io 0x220, IRQ 5, 8-bit DMA 1, 16-bit DMA 5 - the
 *       QEMU sb16 defaults).  `isa_dev` is unused (ISA has no bus
 *       object).  On success a SND_TYPE_SB16 device is registered with
 *       the snd framework; -1 when the DSP does not answer.
 *
 *   void driver_snd_sb16_print_state(void);
 *       One-line DSP status for the `sb16` shell command.
 */
#ifndef OC_SB16_H
#define OC_SB16_H

int  driver_snd_sb16_init(void *isa_dev);
void driver_snd_sb16_print_state(void);

#endif /* OC_SB16_H */
