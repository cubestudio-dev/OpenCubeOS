/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-02
 * File: kernel/pic.h
 * Purpose: 8259 PIC interface (remap/mask/unmask/EOI).
 *          Implementation lives in idt.c.
 */
#ifndef OC_PIC_H
#define OC_PIC_H

#include "types.h"

void oc_pic_remap(void);
void oc_pic_mask(u8 irq);
void oc_pic_unmask(u8 irq);
void oc_pic_eoi(u8 irq);

#endif /* OC_PIC_H */
