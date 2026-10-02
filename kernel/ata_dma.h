/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: kernel/ata_dma.h
 * Purpose: PCI Bus-Master IDE (BMDMA) driver interface.
 *
 * WP-10a adds true DMA transfers for the mainstream IDE/ATA controller
 * (PCI class 0x0101, the Bus Master IDE registers introduced by the PCI
 * IDE controller spec).  The legacy WP-05 PIO path in ata.c remains as
 * the fallback: drives whose controller has no working BMDMA BAR (or a
 * DMA transfer that fails) still work through ata_read/write_sectors.
 *
 * Interfaces (L1 extension API):
 *   ata_dma_init()      - probe controllers, register DMA-capable drives
 *   ata_dma_read/write  - sector I/O on a drive (0..3, same numbering as ata.c)
 *   ata_dma_flush()     - FLUSH CACHE (EXT)
 *   ata_dma_available() - 1 if drive `n` transfers via DMA
 *   ata_dma_print_state()
 */
#ifndef OC_ATA_DMA_H
#define OC_ATA_DMA_H

#include "types.h"
#include "pci.h"

/* WP-10a L1 extension interface: initialize Bus-Master IDE DMA.
 *
 * pdev != NULL : program the BMDMA registers of exactly that PCI
 *                function (class 0x0101xx) and register its drives.
 * pdev == NULL : enumerate the PCI bus and initialize every IDE
 *                controller found (the kmain boot path).
 *
 * Returns the number of controllers initialized (>= 0), or -1 when an
 * explicit pdev is not an IDE controller. */
int ata_dma_init(pci_dev_t *pdev);

/* Returns 1 if drive `n` (0..3) is handled by DMA, 0 if PIO or absent. */
int ata_dma_available(int drive);

/* DMA read/write: same drive numbering and semantics as ata.c
 * (LBA28, returns count on success, negative on error). */
int ata_dma_read_sectors(int drive, u64 lba, int count, void *buf);
int ata_dma_write_sectors(int drive, u64 lba, int count, const void *buf);

/* Flush the drive write cache (ATA FLUSH CACHE / FLUSH CACHE EXT). */
int ata_dma_flush(int drive);

/* Print BMDMA controller state (BDF, BAR4, PRDT, status words). */
void ata_dma_print_state(void);

#endif /* OC_ATA_DMA_H */
