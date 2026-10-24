/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b (P1-fix4 rewrite)
 * File: drivers/nic/driver_nic_bcm57xx.c
 * Purpose: Broadcom BCM57xx (Tigon3) GbE driver for the WP-10b nic
 *          framework.
 *
 * P1fix4 BUG-0128 (A9 domain) FIX: full rewrite against the real tg3
 * (Tigon3) architecture.  The previous driver was written from imagination:
 * wrong mailbox offsets (0x2620/0x3200/0x3208 are not tg3 registers),
 * 8-byte descriptors (tg3 uses 16-byte BDs), and - the central
 * misconception - it polled the STANDARD producer ring for chip writeback.
 * The chip NEVER writes the producer ring ("This works because the card
 * does not write into the rx buffer posting rings" - Linux tg3.c, comment
 * above tg3_rx_prodring_alloc's invariant loop).  Completion comes via the
 * chip-WRITTEN receive return ring plus the DMA'd status block; the host
 * advances the return ring by writing the RCVRET consumer mailbox, and
 * hands new buffers to the chip by writing addresses into the std producer
 * ring and bumping the std producer mailbox.
 *
 * Register map + descriptor layout below were verified line-by-line
 * against Linux v6.10 drivers/net/ethernet/broadcom/tg3.h and tg3.c
 * (fetched during this fix session; evidence cited per define).
 * NOT run on real hardware: this sandbox has no Broadcom NIC, and QEMU
 * ships no bcm57xx device model, so runtime verification = none.  The
 * driver compiles, links and PCI-probes (finding nothing under QEMU);
 * every hardware interaction is source-verified only.  (Previous session's
 * header said the same: 文件头诚实声明未在真硬件上运行.)
 *
 * Corrections vs the earlier (lost-session) notes, re-verified against
 * tg3.h this session - the earlier notes contained four wrong offsets:
 *   - RX return consumer mailbox is MAILBOX_RCVRET_CON_IDX_0 (0x280, low
 *     dword at 0x284), host-written.  0x380 is MAILBOX_SNDNIC_PROD_IDX_0
 *     (chip-side send producer), not an RX register.
 *   - Jumbo/mini producer mailboxes are 0x270/0x278 (not 0x280/0x300);
 *     0x300 is the TX host producer mailbox (doorbell).
 *   - SNDBDI (send BD initiator) is at 0x1800, not a "0x6000 area".
 *   - The "0x100 / 0x200 RCB" values are NIC on-chip SRAM offsets
 *     (NIC_SRAM_SEND_RCB / NIC_SRAM_RCV_RET_RCB), reached through the
 *     0x7c/0x84 memory window - NOT BAR0 registers.  BAR0 0x100 is unused
 *     config-mirror space; there is no "RCV_CONFIG" in tg3 (the analogous
 *     programming is RCVLPC_CONFIG 0x2010 = 0x0181).
 *   - GRC_MODE is 0x6800 (confirmed; the old guess "0x4600?" was wrong).
 *
 * Hardware covered (PCI ID table kept from the legacy driver):
 *   14E4:1644  BCM5700  (Tigon3-1, pre-5705 class)
 *   14E4:1653  BCM5705M
 *   14E4:1673  BCM5751M
 *   14E4:1677  BCM5752
 *   14E4:4401 / 14E4:170C  BCM4401 - HONESTY NOTE: these are b44-family
 *     parts, NOT Tigon3; they are retained only because the legacy table
 *     probed them.  On such hardware this driver's enable polls
 *     (BUFMGR/MAC) will not complete and init fails cleanly.
 *
 * tg3 architecture implemented here (all offsets = BAR0 32-bit MMIO):
 *   - First 256 bytes of BAR0 mirror PCI config space (tg3.h:40).
 *   - Mailboxes are 64-bit registers; the accessible 32-bit port is the
 *     LOW dword at base+4 (TG3_64BIT_REG_LOW, tg3.h:17-18; every tg3.c
 *     access passes "+ TG3_64BIT_REG_LOW").
 *   - Host->chip mailboxes (tg3.h:306-374):
 *       0x204  MAILBOX_INTERRUPT_0 low   (write 1 = mask/ack irq)
 *       0x26c  MAILBOX_RCV_STD_PROD_IDX low  (std ring producer)
 *       0x274  MAILBOX_RCV_JUMBO_PROD_IDX low
 *       0x27c  MAILBOX_RCV_RCVRET/MAILBOX_RCV_MINI... (0x278+4, mini)
 *       0x284  MAILBOX_RCVRET_CON_IDX_0 low (return-ring consumer)
 *       0x304  MAILBOX_SNDHOST_PROD_IDX_0 low (TX doorbell)
 *       0x384  MAILBOX_SNDNIC_PROD_IDX_0 low (chip-written, zeroed at init)
 *   - RCVDBDI_STD_BD 0x2450: BDINFO for the standard receive producer
 *     ring {+0 host addr hi, +4 host addr lo, +8 maxlen_flags, +0xc nic
 *     addr} (tg3.h:1055, TG3_BDINFO_* tg3.h:21-28).
 *   - SRAM window: TG3PCI_MEM_WIN_BASE_ADDR 0x7c / TG3PCI_MEM_WIN_DATA
 *     0x84 (tg3.h:280,282; tg3_write_mem tg3.c:625).  Used to program the
 *     send/receive-return Ring Control Blocks that live in NIC SRAM:
 *       NIC_SRAM_SEND_RCB 0x100, NIC_SRAM_RCV_RET_RCB 0x200 (tg3.h:2158-9)
 *   - BUFMGR 0x4400 block: Mode/Status/MBPoolAddr/MBPoolSize/water marks
 *     (tg3.h:1338-1380).
 *   - SNDBDI 0x1800 (tg3.h:966), SNDDATAI 0xc00 (tg3.h:889), FTQ_RESET
 *     0x5c00 (tg3.h:1620; reset pulse tg3.c:8996-8997).
 *   - GRC_MODE 0x6800 (tg3.h:1710), GRC_MISC_CFG 0x6804 (core clock
 *     reset + 66 MHz/65 prescaler, tg3.c:9194/10164-10167).
 *   - MAC block 0x400: MAC_MODE 0x400 / MAC_STATUS 0x404 / MAC_ADDR_0
 *     0x410+0x414 / MAC_TX_MODE 0x45c / MAC_RX_MODE 0x468 (tg3.h:377-547).
 *   - Host coalescing (status block DMA): HOSTCC_MODE 0x3c00,
 *     HOSTCC_STATUS_BLK_HOST_ADDR 0x3c38 (tg3.h:1218-1275).  The chip
 *     DMA-writes the 0x50-byte status block; idx[0].rx_producer and
 *     idx[0].tx_consumer drive our poll paths (struct tg3_hw_status
 *     tg3.h:2678-2710; same pattern as tg3's loopback self-test,
 *     tg3.c:13584-13621).
 *
 * Descriptors (tg3.h:2553-2643), 16 bytes each, little-endian (x86_64 is
 * LE, so native stores; MISC_HOST_CTRL byte-swap bits stay clear):
 *   TX BD:   { u32 addr_hi; u32 addr_lo; u32 len_flags; u32 vlan_tag }
 *            len<<16 | flags; flags: TXD_FLAG_END 0x4.
 *   RX std producer entry (host-written, chip READ-only):
 *            { addr_hi, addr_lo, idx_len = dma_size(1536),
 *              type_flags = RXD_FLAG_END<<0, opaque = STD_RING | slot }
 *   RX return entry (chip-WRITTEN):
 *            idx_len[15:0] = frame length (+FCS), err_vlan[31:16] =
 *            RXD_ERR_* bits, opaque echoes the std slot index.
 *
 * 4GiB DMA tripwire: all descriptors carry 64-bit addresses but the
 * supported chips are 32-bit DMA parts in practice; every page/buffer is
 * checked to live below 4 GiB and init refuses honestly otherwise
 * (mirrors tg3's 32-bit DMA mask and tg3_4g_overflow_test, tg3.c:7683).
 *
 * Untested-by-construction items (honest disclosure): no PHY-specific
 * workarounds (tg3_setup_phy is a large table-driven MDIO machine); no
 * TSO/jumbo/ASF firmware download (5701-A0/TSO/57766 only); GRC_LOCAL_CTRL
 * and DMA_RW_CTRL are left at power-on values; tg3_poll_fw is shortened.
 */
#include "../../services/nic/nic_svc.h"
#include "../../services/nic/drv_internal.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"

#include <stdint.h>
#include "core_registry.h"

static inline u32 mmio_read32(volatile void *p) { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* =====================================================================
 * Verified tg3 register map (BAR0).  Every define cites its tg3.h line.
 * ===================================================================== */

/* --- PCI config mirror in BAR0 (tg3.h:40-300) --- */
#define BCM_MISC_HOST_CTRL      0x0068   /* TG3PCI_MISC_HOST_CTRL tg3.h:118 */
#define  BCM_MHC_MASK_PCI_INT   0x00000002u  /* MISC_HOST_CTRL_MASK_PCI_INT */
#define  BCM_MHC_CHIPREV_SHIFT  16           /* MISC_HOST_CTRL_CHIPREV_SHIFT */
#define BCM_PCISTATE            0x0070   /* TG3PCI_PCISTATE tg3.h:255 */
#define  BCM_PCISTATE_CONV_PCI  0x00000004u  /* PCISTATE_CONV_PCI_MODE */
#define BCM_CLOCK_CTRL          0x0074   /* TG3PCI_CLOCK_CTRL tg3.h:268 */
#define  BCM_CC_DELAY_PCI_GRANT 0x80000000u  /* CLOCK_CTRL_DELAY_PCI_GRANT */
#define BCM_MEM_WIN_BASE        0x007C   /* TG3PCI_MEM_WIN_BASE_ADDR tg3.h:280 */
#define BCM_MEM_WIN_DATA        0x0084   /* TG3PCI_MEM_WIN_DATA tg3.h:282 */

/* --- Mailboxes (tg3.h:306-374).  64-bit regs, low port at +4 --- */
#define BCM_MBOX_LOW            0x4      /* TG3_64BIT_REG_LOW tg3.h:18 */
#define BCM_INT_MBOX_0          (0x0200 + BCM_MBOX_LOW)  /* 0x204 */
#define BCM_STD_PROD_MBOX       (0x0268 + BCM_MBOX_LOW)  /* 0x26c */
#define BCM_JMB_PROD_MBOX       (0x0270 + BCM_MBOX_LOW)  /* 0x274 */
#define BCM_MINI_PROD_MBOX      (0x0278 + BCM_MBOX_LOW)  /* 0x27c */
#define BCM_RET_CON_MBOX_0      (0x0280 + BCM_MBOX_LOW)  /* 0x284 */
#define BCM_TX_PROD_MBOX_0      (0x0300 + BCM_MBOX_LOW)  /* 0x304 */
#define BCM_SNDNIC_PROD_MBOX_0  (0x0380 + BCM_MBOX_LOW)  /* 0x384 */

/* --- MAC block (tg3.h:377-547) --- */
#define BCM_MAC_MODE            0x0400   /* tg3.h:377 */
#define  BCM_MAC_MODE_LINK_POLARITY  0x00000400u  /* tg3.h:389 */
#define  BCM_MAC_MODE_RXSTAT_ENABLE  0x00000800u  /* tg3.h:390 */
#define  BCM_MAC_MODE_TXSTAT_ENABLE  0x00004000u  /* tg3.h:393 */
#define  BCM_MAC_MODE_RXSTAT_CLEAR   0x00001000u  /* tg3.h:391 */
#define  BCM_MAC_MODE_TXSTAT_CLEAR   0x00008000u  /* tg3.h:394 */
#define  BCM_MAC_MODE_TDE_ENABLE     0x00200000u  /* tg3.h:400 */
#define  BCM_MAC_MODE_RDE_ENABLE     0x00400000u  /* tg3.h:401 */
#define  BCM_MAC_MODE_FHDE_ENABLE    0x00800000u  /* tg3.h:402 */
#define BCM_MAC_STATUS          0x0404   /* tg3.h:406 */
#define  BCM_MAC_STATUS_SIGNAL_DET   0x00000002u  /* tg3.h:408 */
#define BCM_MAC_ADDR_0_HIGH     0x0410   /* tg3.h:451 (upper 2 bytes) */
#define BCM_MAC_ADDR_0_LOW      0x0414   /* tg3.h:452 (lower 4 bytes) */
#define BCM_MAC_TX_BACKOFF_SEED 0x0438   /* tg3.h:465 */
#define  BCM_BACKOFF_SEED_MASK  0x000003ffu      /* TX_BACKOFF_SEED_MASK */
#define BCM_MAC_RX_MTU_SIZE     0x043C   /* tg3.h:467 */
#define BCM_MAC_MI_COM          0x044C   /* tg3.h:479 */
#define  BCM_MI_COM_CMD_WRITE   0x04000000u  /* tg3.h:481 */
#define  BCM_MI_COM_CMD_READ    0x08000000u  /* tg3.h:482 */
#define  BCM_MI_COM_READ_FAILED 0x10000000u  /* tg3.h:483 */
#define  BCM_MI_COM_START       0x20000000u  /* tg3.h:484 */
#define  BCM_MI_COM_BUSY        0x20000000u  /* tg3.h:485 */
#define  BCM_MI_COM_PHY_SHIFT   21           /* tg3.h:487 */
#define  BCM_MI_COM_REG_SHIFT   16           /* tg3.h:489 */
#define  BCM_MI_COM_DATA_MASK   0x0000ffffu  /* tg3.h:490 */
#define BCM_MAC_MI_STAT         0x0450   /* tg3.h:491 */
#define  BCM_MI_STAT_LNKSTAT_ATTN_ENAB 0x00000001u  /* tg3.h:492 */
#define BCM_MAC_TX_MODE         0x045C   /* tg3.h:502 */
#define  BCM_TX_MODE_ENABLE     0x00000002u  /* tg3.h:504 */
#define BCM_MAC_TX_LENGTHS      0x0464   /* tg3.h:518 */
#define BCM_MAC_RX_MODE         0x0468   /* tg3.h:527 */
#define  BCM_RX_MODE_ENABLE     0x00000002u  /* tg3.h:529 */
#define BCM_MAC_RCV_RULE_CFG    0x0500   /* tg3.h:588 */
#define  BCM_RCV_RULE_CFG_DEFAULT_CLASS 0x00000008u /* tg3.h:589 */
#define BCM_MAC_LOW_WMARK_MAX_RX_FRAME 0x0504  /* tg3.h:591 */

/* --- Send / receive DMA engines (tg3.h:889-1119) --- */
#define BCM_SNDDATAI_MODE       0x0C00   /* tg3.h:889 */
#define  BCM_MODE_ENABLE        0x00000002u  /* *_MODE_ENABLE everywhere */
#define  BCM_MODE_ATTN_ENABLE   0x00000004u  /* *_MODE_ATTN_ENABLE */
#define BCM_SNDDATAI_STATSCTRL  0x0C08   /* tg3.h:895 */
#define  BCM_SNDI_SCTRL_ENABLE  0x00000001u  /* tg3.h:896 */
#define  BCM_SNDI_SCTRL_FASTUPD 0x00000002u  /* tg3.h:897 */
#define BCM_SNDDATAI_STATSENAB  0x0C0C   /* tg3.h:901 */
#define BCM_SNDDATAC_MODE       0x1000   /* tg3.h:932 */
#define BCM_SNDBDS_MODE         0x1400   /* tg3.h:939 */
#define BCM_SNDBDI_MODE         0x1800   /* tg3.h:966 */
#define BCM_SNDBDC_MODE         0x1C00   /* tg3.h:992 */
#define BCM_RCVLPC_MODE         0x2000   /* tg3.h:999 */
#define BCM_RCVLPC_CONFIG       0x2010   /* tg3.h:1016 */
#define BCM_RCVLPC_STATS_ENABLE 0x2018   /* tg3.h:1020 */
#define BCM_RCVLPC_STATSCTRL    0x2014   /* tg3.h:1017 */
#define  BCM_RCVLPC_STATSCTRL_ENABLE 0x00000001u  /* tg3.h:1018 */
#define BCM_RCVDBDI_MODE        0x2400   /* tg3.h:1041 */
#define  BCM_RCVDBDI_MODE_INV_RING_SZ 0x00000010u /* tg3.h:1046 */
#define BCM_RCVDBDI_JUMBO_BD    0x2440   /* tg3.h:1054 */
#define BCM_RCVDBDI_STD_BD      0x2450   /* tg3.h:1055 */
#define BCM_RCVDBDI_MINI_BD     0x2460   /* tg3.h:1056 */
#define  BCM_BDINFO_HOST_ADDR_HI   0x0   /* TG3_BDINFO_HOST_ADDR hi tg3.h:21 */
#define  BCM_BDINFO_HOST_ADDR_LO   0x4   /* TG3_BDINFO_HOST_ADDR lo */
#define  BCM_BDINFO_MAXLEN_FLAGS   0x8   /* TG3_BDINFO_MAXLEN_FLAGS */
#define  BCM_BDINFO_NIC_ADDR       0xC   /* TG3_BDINFO_NIC_ADDR */
#define  BCM_BDINFO_FLAGS_DISABLED 0x00000002u    /* tg3.h:24 */
#define  BCM_BDINFO_MAXLEN_SHIFT   16            /* tg3.h:26 */
#define BCM_RCVDCC_MODE         0x2800   /* tg3.h:1081 */
#define BCM_RCVBDI_MODE         0x2C00   /* tg3.h:1088 */
#define  BCM_RCVBDI_MODE_RCB_ATTN_ENAB 0x00000004u /* tg3.h:1090 */
#define BCM_RCVBDI_STD_THRESH   0x2C18   /* tg3.h:1098 */
#define BCM_RCVCC_MODE          0x3000   /* tg3.h:1107 */
#define BCM_RCVLSC_MODE         0x3400   /* tg3.h:1119 */

/* --- Host coalescing (tg3.h:1218-1306) --- */
#define BCM_HOSTCC_MODE         0x3C00   /* tg3.h:1218 */
#define  BCM_HOSTCC_MODE_ENABLE 0x00000002u  /* tg3.h:1220 */
#define  BCM_HOSTCC_MODE_NOW    0x00000008u  /* tg3.h:1222 */
#define BCM_HOSTCC_RXCOL_TICKS  0x3C08   /* tg3.h:1233 */
#define BCM_HOSTCC_TXCOL_TICKS  0x3C0C   /* tg3.h:1239 */
#define BCM_HOSTCC_RXMAX_FRAMES 0x3C10   /* tg3.h:1245 */
#define BCM_HOSTCC_TXMAX_FRAMES 0x3C14   /* tg3.h:1250 */
#define BCM_HOSTCC_STAT_COAL_TICKS 0x3C28 /* tg3.h:1269 */
#define BCM_HOSTCC_STATUS_BLK_HOST_ADDR 0x3C38 /* 64-bit tg3.h:1275 */

/* --- Buffer manager (tg3.h:1338-1380) --- */
#define BCM_BUFMGR_MODE         0x4400   /* tg3.h:1338 */
#define BCM_BUFMGR_STATUS       0x4404   /* tg3.h:1345 */
#define BCM_BUFMGR_MB_POOL_ADDR 0x4408   /* tg3.h:1348 */
#define BCM_BUFMGR_MB_POOL_SIZE 0x440C   /* tg3.h:1349 */
#define BCM_BUFMGR_MB_RDMA_LOW_WATER 0x4410 /* tg3.h:1350 */
#define BCM_BUFMGR_MB_MACRX_LOW_WATER 0x4414 /* tg3.h:1355 */
#define BCM_BUFMGR_MB_HIGH_WATER 0x4418  /* tg3.h:1363 */
#define BCM_BUFMGR_DMA_DESC_POOL_ADDR 0x442C /* tg3.h:1376 */
#define BCM_BUFMGR_DMA_DESC_POOL_SIZE 0x4430 /* tg3.h:1377 */
#define BCM_BUFMGR_DMA_LOW_WATER 0x4434  /* tg3.h:1378 */
#define BCM_BUFMGR_DMA_HIGH_WATER 0x4438 /* tg3.h:1380 */

/* --- Memory arbiter + DMA engines + GRC (tg3.h:1329-1766) --- */
#define BCM_MEMARB_MODE         0x4000   /* tg3.h:1329 */
#define BCM_MBFREE_MODE         0x3800   /* MBFREE_MODE tg3.h:1211 (pre-5705) */
#define BCM_RDMAC_MODE          0x4800   /* tg3.h:1392 */
#define  BCM_RDMAC_ERRS         0x000003feu /* TGTABORT|MSTABORT|PARITY|ADDR|
                                                FIFOOFLOW|FIFOURUN|FIFOOREAD|
                                                LNGREAD (tg3.h:1395-1402) */
#define  BCM_RDMAC_FIFO_LONG_BURST 0x00030000u /* tg3.h:1409 */
#define BCM_WDMAC_MODE          0x4C00   /* tg3.h:1450 */
#define  BCM_WDMAC_ERRS         0x000003feu /* same error set tg3.h:1453-1460 */
#define BCM_FTQ_RESET           0x5C00   /* tg3.h:1620 */
#define BCM_DMAC_MODE           0x6400   /* tg3.h:1704 (pre-5705 only) */
#define BCM_GRC_MODE            0x6800   /* tg3.h:1710 */
#define  BCM_GRC_MODE_HOST_STACKUP   0x00010000u /* tg3.h:1726 */
#define  BCM_GRC_MODE_HOST_SENDBDS   0x00020000u /* tg3.h:1727 */
#define  BCM_GRC_MODE_NO_TX_PHDR_CSUM 0x00100000u /* tg3.h:1730 */
#define  BCM_GRC_MODE_IRQ_ON_MAC_ATTN 0x04000000u /* tg3.h:1737 */
#define BCM_GRC_MISC_CFG        0x6804   /* tg3.h:1748 */
#define  BCM_GRC_MISC_CORECLK_RESET 0x00000001u   /* tg3.h:1749 */

/* --- NIC on-chip SRAM layout (tg3.h:2157-2264), via 0x7c/0x84 window --- */
#define BCM_SRAM_SEND_RCB       0x0100   /* NIC_SRAM_SEND_RCB tg3.h:2158 */
#define BCM_SRAM_RCV_RET_RCB    0x0200   /* NIC_SRAM_RCV_RET_RCB tg3.h:2159 */
#define BCM_SRAM_STATUS_BLK     0x0B00   /* NIC_SRAM_STATUS_BLK tg3.h:2161 */
#define BCM_SRAM_FW_MBOX        0x0B50   /* NIC_SRAM_FIRMWARE_MBOX tg3.h:2163 */
#define  BCM_SRAM_FW_MBOX_MAGIC1 0x4B657654u /* tg3.h:2164 */
#define BCM_SRAM_MAC_ADDR_HI_MBOX 0x0C14 /* NIC_SRAM_MAC_ADDR_HIGH_MBOX tg3.h:2217 */
#define BCM_SRAM_MAC_ADDR_LO_MBOX 0x0C18 /* NIC_SRAM_MAC_ADDR_LOW_MBOX tg3.h:2218 */
#define  BCM_SRAM_MAC_MAGIC     0x484B   /* "HK" tag, tg3.c:17058 */
#define BCM_SRAM_DMA_DESC_POOL_BASE 0x2000 /* tg3.h:2255 */
#define BCM_SRAM_DMA_DESC_POOL_SIZE  0x2000 /* tg3.h:2256 */
#define BCM_SRAM_TX_BUFFER_DESC 0x4000  /* tg3.h:2257 */
#define BCM_SRAM_RX_BUFFER_DESC 0x6000  /* tg3.h:2258 */
#define BCM_SRAM_MBUF_POOL_BASE 0x8000  /* tg3.h:2260 */
#define BCM_SRAM_MBUF_POOL_SIZE96 0x18000u /* tg3.h:2261 */

/* --- RX CPU (tg3.h:1476-1528) --- */
#define BCM_RX_CPU_BASE         0x5000   /* tg3.h:1528 */

/* =====================================================================
 * Descriptor formats (tg3.h:2553-2643) - 16 bytes, little-endian.
 * ===================================================================== */

/* tg3_tx_buffer_desc (tg3.h:2553).  len_flags = len<<16 | flags. */
typedef struct {
    volatile u32 addr_hi;      /* 64-bit DMA address, high dword */
    volatile u32 addr_lo;      /* low dword (buffers are < 4 GiB) */
    volatile u32 len_flags;    /* TXD_LEN_SHIFT 16; TXD_FLAG_END 0x4 */
    volatile u32 vlan_tag;     /* 0 (no VLAN offload) */
} __attribute__((packed)) bcm_tx_bd_t;

#define BCM_TXD_FLAG_END        0x0004u  /* TXD_FLAG_END tg3.h:2560 */
#define BCM_TXD_LEN_SHIFT       16       /* TXD_LEN_SHIFT tg3.h:2572 */

/* tg3_rx_buffer_desc (tg3.h:2584) - used BOTH for the host-written std
 * producer entries (chip READ-only!) and the chip-written return entries.
 * Host-written fields (std producer): addr, idx_len = dma size,
 * type_flags = RXD_FLAG_END<<RXD_FLAGS_SHIFT(0), opaque = ring id | slot.
 * Chip-written fields (return): idx_len[15:0] = frame len incl. FCS,
 * err_vlan[31:16] = RXD_ERR_* bits, opaque echoes the std slot. */
typedef struct {
    volatile u32 addr_hi;
    volatile u32 addr_lo;
    volatile u32 idx_len;      /* RXD_IDX_MASK 0xffff0000 / RXD_LEN_MASK 0xffff */
    volatile u32 type_flags;   /* RXD_TYPE_SHIFT 16 / RXD_FLAGS_SHIFT 0 */
    volatile u32 ip_tcp_csum;
    volatile u32 err_vlan;     /* RXD_ERR_* in 0x01ff0000 */
    volatile u32 reserved;
    volatile u32 opaque;       /* RXD_OPAQUE_* tg3.h:2637-2642 */
} __attribute__((packed)) bcm_rx_bd_t;

#define BCM_RXD_FLAG_END        0x0004u  /* RXD_FLAG_END tg3.h:2598 */
#define BCM_RXD_LEN_MASK        0x0000ffffu /* tg3.h:2591 */
#define BCM_RXD_ERR_MASK        0x01ff0000u /* RXD_ERR_MASK tg3.h:2630-2633 */
#define BCM_RXD_OPAQUE_IDX_MASK 0x0000ffffu /* RXD_OPAQUE_INDEX_MASK tg3.h:2637 */
#define BCM_RXD_OPAQUE_RING_STD 0x00010000u /* RXD_OPAQUE_RING_STD tg3.h:2639 */

/* tg3_hw_status (tg3.h:2678-2710), TG3_HW_STATUS_SIZE 0x50.  DMA'd by the
 * chip; idx[0].rx_producer / idx[0].tx_consumer are our poll cursors. */
typedef struct {
    volatile u32 status;       /* SD_STATUS_UPDATED 0x1 */
    volatile u32 status_tag;
    volatile u16 rx_jumbo_consumer;
    volatile u16 rx_consumer;
    volatile u16 rx_mini_consumer;
    volatile u16 reserved;
    struct {
        volatile u16 rx_producer;
        volatile u16 tx_consumer;
    } idx[16];
} __attribute__((packed)) bcm_status_blk_t;

/* =====================================================================
 * Ring geometry (fixed by NIC firmware for pre-5717 tg3 - tg3.c:133-139:
 * "These numbers seem to be hard coded in the NIC firmware somehow.  You
 * can't change the ring sizes").  The std and TX rings therefore span all
 * 512 entries in host memory; only RX_STD_POSTED buffers / TX_BUDGET
 * frames are kept in flight.
 * ===================================================================== */
#define BCM_STD_RING_SIZE       512      /* TG3_RX_STD_MAX_SIZE_5700 tg3.h:30 */
#define BCM_TX_RING_SIZE        512      /* TG3_TX_RING_SIZE tg3.c:153 */
#define BCM_RET_RING_SIZE       64       /* chosen; <= TG3_RX_RET_MAX_SIZE_5705 (512) */
#define BCM_RX_DMA_SZ           1536     /* TG3_RX_STD_DMA_SZ tg3.c:168 */
#define BCM_BUF_SIZE            2048     /* host carve chunk (>= 1536 + slack) */
#define BCM_RX_STD_POSTED       32       /* our rx_pending (tg3 default 200) */
#define BCM_TX_BUDGET           32       /* our tx_pending */

/* =====================================================================
 * Device state
 * ===================================================================== */
typedef struct {
    volatile u8 *mmio;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    u32  chip_rev_id;          /* tg3_asic_rev() = >>12 (tg3.h:3448) */
    u8   is_5705_plus;         /* 5705/5750/5752... class */
    u8   is_pcie;
    u32  pci_clock_ctrl;       /* saved before core reset (tg3.c:10074) */

    /* host rings (physical == virtual in the identity map) */
    bcm_tx_bd_t     *tx_ring;           /* 512 * 16 = 8 KiB */
    bcm_rx_bd_t     *rx_std_ring;       /* 512 * 16 = 8 KiB */
    bcm_rx_bd_t     *rx_ret_ring;       /* 64 * 16 = 1 KiB */
    volatile bcm_status_blk_t *sblk;    /* 0x50 bytes in 1 page */

    /* buffer bookkeeping: slot -> buffer for the walking producer slots */
    u8   *rx_std_buf[BCM_STD_RING_SIZE];
    u8   *tx_slot_buf[BCM_TX_RING_SIZE];
    u8   *tx_free_buf[BCM_TX_BUDGET];   /* free TX buffer stack */
    int   tx_free_cnt;

    u32   rx_std_prod;         /* counts mod 512 */
    u32   rx_ret_cons;         /* counts mod 64 */
    u32   tx_prod, tx_cons;    /* counts mod 512 */

    u64  tx_packets, rx_packets, rx_dropped, tx_errors;
} bcm_dev_t;

static bcm_dev_t g_bcm;

static const struct { u16 vid; u16 did; } driver_nic_bcm57xx_ids[] = {
    { 0x14E4, 0x1644 }, { 0x14E4, 0x1653 }, { 0x14E4, 0x1673 },
    { 0x14E4, 0x1677 }, { 0x14E4, 0x4401 }, { 0x14E4, 0x170C },
};

/* =====================================================================
 * DMA memory with the 4GiB tripwire.  Pages come from the identity-mapped
 * PMM region (same as every sibling NIC driver); a page at or above 4 GiB
 * is released and init fails honestly - the tg3 descriptors we program
 * must never point above the 32-bit DMA window.
 * ===================================================================== */
static u8 *driver_nic_bcm57xx_dma_page = NULL;
static int driver_nic_bcm57xx_dma_off = 0;
static int driver_nic_bcm57xx_dma_over4g = 0;

static void *driver_nic_bcm57xx_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return NULL;   /* carve only */
    if (driver_nic_bcm57xx_dma_page == NULL ||
        driver_nic_bcm57xx_dma_off + size > (int)PMM_PAGE_SIZE) {
        u64 p = mem_pmm_alloc_frame();
        if (p == 0) return NULL;
        /* 4GiB tripwire: 32-bit DMA window only. */
        if (p + PMM_PAGE_SIZE > 0x100000000ull) {
            mem_pmm_free_frame(p);
            driver_nic_bcm57xx_dma_over4g = 1;
            return NULL;
        }
        driver_nic_bcm57xx_dma_page = (u8 *)(uintptr_t)p;
        driver_nic_bcm57xx_dma_off = 0;
    }
    driver_nic_bcm57xx_dma_off = (driver_nic_bcm57xx_dma_off + 15) & ~15;
    void *ret = driver_nic_bcm57xx_dma_page + driver_nic_bcm57xx_dma_off;
    driver_nic_bcm57xx_dma_off += size;
    return ret;
}

/* Contiguous pages for the 8 KiB rings, same tripwire. */
static void *driver_nic_bcm57xx_alloc_pages(u64 count) {
    u64 p = mem_pmm_alloc_contig(count);
    if (p == 0) return NULL;
    if (p + count * PMM_PAGE_SIZE > 0x100000000ull) {
        for (u64 i = 0; i < count; i++) mem_pmm_free_frame(p + i * PMM_PAGE_SIZE);
        driver_nic_bcm57xx_dma_over4g = 1;
        return NULL;
    }
    return (void *)(uintptr_t)p;
}

/* =====================================================================
 * MMIO helpers
 * ===================================================================== */
static u32 bcm_reg(bcm_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)(d->mmio + off));
}
static void bcm_wreg(bcm_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)(d->mmio + off), v);
}
/* tw32_f semantics (tg3.c:565-582): posted write + read-back flush. */
static void bcm_wreg_f(bcm_dev_t *d, u32 off, u32 v) {
    bcm_wreg(d, off, v);
    (void)bcm_reg(d, off);
}
/* Mailbox low-dword write with optional flush (tw32_tx_mbox /
 * tw32_mailbox_f pattern, tg3.c:584-602). */
static void bcm_mbox(bcm_dev_t *d, u32 mbox, u32 v) {
    bcm_wreg(d, mbox, v);
    (void)bcm_reg(d, mbox);
}

/* Rough delay: no calibrated udelay in this kernel yet; a volatile spin
 * of ~1000 iterations is microsecond-scale (same approach the igb driver
 * uses for its MDIO waits).  tg3's udelay() arguments are cited at each
 * call so the intended real delay is on record. */
static void bcm_delay(int spins) {
    for (volatile int i = 0; i < spins; i++) { }
}

/* Windowed NIC SRAM write/read (tg3_write_mem/tg3_read_mem tg3.c:625-675):
 * set window base (0x7c), access data (0x84), always leave base at 0. */
static void bcm_sram_write(bcm_dev_t *d, u32 off, u32 val) {
    bcm_wreg_f(d, BCM_MEM_WIN_BASE, off);
    bcm_wreg_f(d, BCM_MEM_WIN_DATA, val);
    bcm_wreg_f(d, BCM_MEM_WIN_BASE, 0);
}
static u32 bcm_sram_read(bcm_dev_t *d, u32 off) {
    bcm_wreg_f(d, BCM_MEM_WIN_BASE, off);
    u32 v = bcm_reg(d, BCM_MEM_WIN_DATA);
    bcm_wreg_f(d, BCM_MEM_WIN_BASE, 0);
    return v;
}

/* MDIO via MAC_MI_COM (tg3 __tg3_readphy/__tg3_writephy tg3.c:1150-1230).
 * PHY address 1 = TG3_PHY_MII_ADDR (tg3.h:2281). */
#define BCM_MDIO_PHY_ADDR 1
static int bcm_mdio_op(bcm_dev_t *d, int write, int reg, u16 *val) {
    u32 frame = ((u32)BCM_MDIO_PHY_ADDR << BCM_MI_COM_PHY_SHIFT) |
                ((u32)reg << BCM_MI_COM_REG_SHIFT) |
                (write ? BCM_MI_COM_CMD_WRITE : BCM_MI_COM_CMD_READ) |
                BCM_MI_COM_START |
                (write ? ((u32)*val & BCM_MI_COM_DATA_MASK) : 0);
    bcm_wreg_f(d, BCM_MAC_MI_COM, frame);
    for (int i = 0; i < 2000; i++) {          /* PHY_BUSY_LOOPS scaled */
        bcm_delay(10);                        /* tg3: udelay(10)/loop */
        u32 v = bcm_reg(d, BCM_MAC_MI_COM);
        if (!(v & BCM_MI_COM_BUSY)) {
            if (!write) {
                if (v & BCM_MI_COM_READ_FAILED) return -1;
                *val = (u16)(v & BCM_MI_COM_DATA_MASK);
            }
            return 0;
        }
    }
    return -1;
}
static int bcm_mdio_write(bcm_dev_t *d, int reg, u16 val) {
    return bcm_mdio_op(d, 1, reg, &val);
}
static int bcm_mdio_read(bcm_dev_t *d, int reg, u16 *val) {
    return bcm_mdio_op(d, 0, reg, val);
}

/* =====================================================================
 * PCI helpers
 * ===================================================================== */
static void bcm_pci_enable(bcm_dev_t *d) {
    u32 cs = driver_pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;   /* BME|MAE|IOSE */
    driver_pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

/* Walk the PCI capability list to find PCIe (cap id 0x10). */
static int bcm_is_pcie(bcm_dev_t *d) {
    u32 cap_ptr = driver_pci_read_config(d->bus, d->dev, d->func, 0x34) & 0xFF;
    for (int i = 0; cap_ptr != 0 && i < 16; i++) {
        u32 cap = driver_pci_read_config(d->bus, d->dev, d->func, (u8)cap_ptr);
        if ((cap & 0xFF) == 0x10) return 1;
        cap_ptr = (cap >> 8) & 0xFF;
    }
    return 0;
}

/* =====================================================================
 * Core reset (tg3_chip_reset tg3.c:9123-9360, trimmed to our classes)
 * ===================================================================== */
static void bcm_core_reset(bcm_dev_t *d) {
    /* Disable interrupts first (tg3_disable_ints tg3.c:997). */
    u32 mhc = bcm_reg(d, BCM_MISC_HOST_CTRL);
    bcm_wreg(d, BCM_MISC_HOST_CTRL, mhc | BCM_MHC_MASK_PCI_INT);
    bcm_mbox(d, BCM_INT_MBOX_0, 0x00000001);

    /* GRC_MISC_CFG core clock reset (tg3.c:9193-9227).  PCIe parts also
     * set bit 29 (tg3.c:9204-9207).  MMIO is unreliable from here on. */
    u32 val = BCM_GRC_MISC_CORECLK_RESET;
    if (d->is_pcie) {
        bcm_wreg(d, BCM_GRC_MISC_CFG, (1u << 29));
        val |= (1u << 29);
    }
    bcm_wreg(d, BCM_GRC_MISC_CFG, val);

    bcm_delay(120000);   /* tg3: udelay(120) + cfg flush read + udelay(120) */
    (void)driver_pci_read_config(d->bus, d->dev, d->func, 0x04);
    bcm_delay(120000);

    /* Restore PCI command (the reset can clear MEM/BM enable). */
    bcm_pci_enable(d);

    /* MEMARB back on (tg3.c:9296-9299). */
    bcm_wreg(d, BCM_MEMARB_MODE, BCM_MODE_ENABLE);

    /* Wait (bounded, non-fatal) for bootcode firmware to signal life
     * (tg3_poll_fw tg3.c:1806-1858; shortened bound documented in the
     * file header - failure is not fatal, tg3 also just informs). */
    for (int i = 0; i < 20000; i++) {
        if (bcm_sram_read(d, BCM_SRAM_FW_MBOX) == ~BCM_SRAM_FW_MBOX_MAGIC1)
            break;
        bcm_delay(10);
    }

    /* GRC_MODE (tg3.c:10129-10151): host-resident send BDs, software
     * pseudo-header TX checksum, stack-up + MAC attention IRQ routing. */
    bcm_wreg_f(d, BCM_GRC_MODE,
               BCM_GRC_MODE_HOST_SENDBDS | BCM_GRC_MODE_NO_TX_PHDR_CSUM |
               BCM_GRC_MODE_HOST_STACKUP | BCM_GRC_MODE_IRQ_ON_MAC_ATTN);

    /* Copper MAC_MODE = 0 after reset (tg3.c:9336-9346). */
    bcm_wreg_f(d, BCM_MAC_MODE, 0);
    bcm_delay(40000);   /* tg3: udelay(40) */

    /* Clock control (tg3.c:10066-10075): no CPMU on our classes; only
     * conventional-PCI parts need DELAY_PCI_GRANT. */
    if (!d->is_pcie) d->pci_clock_ctrl |= BCM_CC_DELAY_PCI_GRANT;
    bcm_wreg_f(d, BCM_CLOCK_CTRL, d->pci_clock_ctrl);
}

/* =====================================================================
 * Ring setup
 * ===================================================================== */
static int bcm_alloc_rings(bcm_dev_t *d) {
    d->tx_ring     = (bcm_tx_bd_t *)driver_nic_bcm57xx_alloc_pages(2);
    d->rx_std_ring = (bcm_rx_bd_t *)driver_nic_bcm57xx_alloc_pages(2);
    d->rx_ret_ring = (bcm_rx_bd_t *)driver_nic_bcm57xx_alloc_pages(1);
    if (!d->tx_ring || !d->rx_std_ring || !d->rx_ret_ring) return -1;
    memset(d->tx_ring, 0, BCM_TX_RING_SIZE * sizeof(bcm_tx_bd_t));
    memset(d->rx_std_ring, 0, BCM_STD_RING_SIZE * sizeof(bcm_rx_bd_t));
    memset(d->rx_ret_ring, 0, BCM_RET_RING_SIZE * sizeof(bcm_rx_bd_t));

    d->sblk = (volatile bcm_status_blk_t *)driver_nic_bcm57xx_alloc_pages(1);
    if (!d->sblk) return -1;
    memset((void *)d->sblk, 0, PMM_PAGE_SIZE);

    /* RX std buffers + invariants (tg3_rx_prodring_alloc tg3.c:8460-8481):
     * the address is the ONLY field updated on re-arm - the card never
     * writes this ring. */
    for (int i = 0; i < BCM_STD_RING_SIZE; i++) {
        d->rx_std_ring[i].idx_len     = (u32)BCM_RX_DMA_SZ;
        d->rx_std_ring[i].type_flags  = BCM_RXD_FLAG_END;
        d->rx_std_ring[i].opaque      = BCM_RXD_OPAQUE_RING_STD | (u32)i;
    }
    for (int i = 0; i < BCM_RX_STD_POSTED; i++) {
        u8 *buf = (u8 *)driver_nic_bcm57xx_dma_alloc(BCM_BUF_SIZE);
        if (!buf) return -1;
        d->rx_std_buf[i] = buf;
        d->rx_std_ring[i].addr_hi = 0;
        d->rx_std_ring[i].addr_lo = (u32)(uintptr_t)buf;
    }

    /* TX buffers: a free stack of carve chunks; slot->buffer map mirrors
     * tg3's tx_buffers[] bookkeeping. */
    for (int i = 0; i < BCM_TX_BUDGET; i++) {
        u8 *buf = (u8 *)driver_nic_bcm57xx_dma_alloc(BCM_BUF_SIZE);
        if (!buf) return -1;
        d->tx_free_buf[i] = buf;
    }
    d->tx_free_cnt = BCM_TX_BUDGET;
    return 0;
}

/* tg3_set_bdinfo (tg3.c:9477-9495) through the SRAM window. */
static void bcm_set_rcb(bcm_dev_t *d, u32 sram_rcb, u32 host_addr_lo,
                        u32 maxlen_entries, u32 nic_addr) {
    bcm_sram_write(d, sram_rcb + BCM_BDINFO_HOST_ADDR_HI, 0);
    bcm_sram_write(d, sram_rcb + BCM_BDINFO_HOST_ADDR_LO, host_addr_lo);
    bcm_sram_write(d, sram_rcb + BCM_BDINFO_MAXLEN_FLAGS,
                   maxlen_entries << BCM_BDINFO_MAXLEN_SHIFT);
    if (!d->is_5705_plus)
        bcm_sram_write(d, sram_rcb + BCM_BDINFO_NIC_ADDR, nic_addr);
}

/* =====================================================================
 * Hardware init (tg3_reset_hw tg3.c:9936-10774, class-branched)
 * ===================================================================== */
static int bcm_init_hw(bcm_dev_t *d) {
    u32 val;
    int i;

    bcm_core_reset(d);

    /* --- Buffer manager (tg3.c:10169-10228) --- */
    if (!d->is_5705_plus) {
        /* 5700-class: MBUF + DMA descriptor pools live in SRAM. */
        bcm_wreg(d, BCM_BUFMGR_MB_POOL_ADDR, BCM_SRAM_MBUF_POOL_BASE);
        bcm_wreg(d, BCM_BUFMGR_MB_POOL_SIZE, (u32)BCM_SRAM_MBUF_POOL_SIZE96);
        bcm_wreg(d, BCM_BUFMGR_DMA_DESC_POOL_ADDR, BCM_SRAM_DMA_DESC_POOL_BASE);
        bcm_wreg(d, BCM_BUFMGR_DMA_DESC_POOL_SIZE, BCM_SRAM_DMA_DESC_POOL_SIZE);
    }
    /* 5705/5750-class: no pool programming (tg3.c:10170-10171); a 5705
     * without loaded TSO firmware also leaves the pool alone. */
    if (d->is_5705_plus) {
        bcm_wreg(d, BCM_BUFMGR_MB_RDMA_LOW_WATER, 0x00); /* DEF...5705 */
        bcm_wreg(d, BCM_BUFMGR_MB_MACRX_LOW_WATER, 0x10);/* DEF...5705 */
        bcm_wreg(d, BCM_BUFMGR_MB_HIGH_WATER, 0x60);     /* DEF...5705 */
    } else {
        bcm_wreg(d, BCM_BUFMGR_MB_RDMA_LOW_WATER, 0x50);
        bcm_wreg(d, BCM_BUFMGR_MB_MACRX_LOW_WATER, 0x20);
        bcm_wreg(d, BCM_BUFMGR_MB_HIGH_WATER, 0x60);
    }
    bcm_wreg(d, BCM_BUFMGR_DMA_LOW_WATER, 0x05);   /* DEFAULT_DMA_LOW_WATER */
    bcm_wreg(d, BCM_BUFMGR_DMA_HIGH_WATER, 0x0A);  /* DEFAULT_DMA_HIGH_WATER */
    bcm_wreg(d, BCM_BUFMGR_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);
    for (i = 0; i < 200000; i++) {          /* tg3: 2000 x udelay(10) */
        if (bcm_reg(d, BCM_BUFMGR_MODE) & BCM_MODE_ENABLE) break;
        bcm_delay(100);
    }
    if (i >= 200000) return -1;             /* honest: BUFMGR not alive */

    /* --- RX BD fetch threshold (tg3_setup_rxbd_thresholds tg3.c:9740):
     * bdcache 128 (pre-5755), nic = min(64, 511), host = max(32/8, 1) = 4. */
    bcm_wreg(d, BCM_RCVBDI_STD_THRESH, 4);

    /* --- Standard receive ring BDINFO (tg3.c:10252-10299) --- */
    bcm_wreg(d, BCM_RCVDBDI_STD_BD + BCM_BDINFO_HOST_ADDR_HI, 0);
    bcm_wreg(d, BCM_RCVDBDI_STD_BD + BCM_BDINFO_HOST_ADDR_LO,
             (u32)(uintptr_t)d->rx_std_ring);
    bcm_wreg(d, BCM_RCVDBDI_STD_BD + BCM_BDINFO_NIC_ADDR,
             BCM_SRAM_RX_BUFFER_DESC);
    /* MAXLEN_FLAGS: jumbo-capable (pre-5705) = DMA size 1536; 5705-class
     * = TG3_RX_STD_MAX_SIZE_5700 (512).  Jumbo/mini rings disabled. */
    val = d->is_5705_plus ? 512u : (u32)BCM_RX_DMA_SZ;
    bcm_wreg(d, BCM_RCVDBDI_STD_BD + BCM_BDINFO_MAXLEN_FLAGS,
             val << BCM_BDINFO_MAXLEN_SHIFT);
    bcm_wreg(d, BCM_RCVDBDI_JUMBO_BD + BCM_BDINFO_MAXLEN_FLAGS,
             BCM_BDINFO_FLAGS_DISABLED);
    if (!d->is_5705_plus)
        bcm_wreg(d, BCM_RCVDBDI_MINI_BD + BCM_BDINFO_MAXLEN_FLAGS,
                 BCM_BDINFO_FLAGS_DISABLED);

    /* --- Producer mailboxes (tg3.c:10301-10306) --- */
    d->rx_std_prod = BCM_RX_STD_POSTED;
    bcm_mbox(d, BCM_STD_PROD_MBOX, d->rx_std_prod & (BCM_STD_RING_SIZE - 1));
    bcm_mbox(d, BCM_JMB_PROD_MBOX, 0);

    /* --- tg3_rings_reset (tg3.c:9670-9738) --- */
    bcm_mbox(d, BCM_INT_MBOX_0, 0x00000001);        /* ints stay masked */
    bcm_mbox(d, BCM_TX_PROD_MBOX_0, 0);
    bcm_mbox(d, BCM_RET_CON_MBOX_0, 0);
    d->tx_prod = d->tx_cons = 0;
    d->rx_ret_cons = 0;
    if (!d->is_5705_plus) {
        /* Zero the NIC-side send BD ring mailboxes (tg3.c:9708-9713). */
        for (i = 0; i < 16; i++)
            bcm_mbox(d, BCM_SNDNIC_PROD_MBOX_0 + (u32)i * 8, 0);
    }
    memset((void *)d->sblk, 0, sizeof(bcm_status_blk_t));
    bcm_wreg(d, BCM_HOSTCC_STATUS_BLK_HOST_ADDR + 0, 0);   /* hi */
    bcm_wreg(d, BCM_HOSTCC_STATUS_BLK_HOST_ADDR + 4,       /* lo */
             (u32)(uintptr_t)d->sblk);
    if (!d->is_5705_plus) {
        /* Status block also mirrored into NIC SRAM (tg3.c:10469-10479). */
        for (i = 0; i < (int)sizeof(bcm_status_blk_t); i += 4)
            bcm_sram_write(d, BCM_SRAM_STATUS_BLK + (u32)i, 0);
    }

    /* --- Send / receive-return RCBs in NIC SRAM (tg3.c:9736-9737 and
     * tg3_tx_rcbs_init/tg3_rx_ret_rcbs_init tg3.c:9605-9667).  TX ring
     * carries its fixed 512-entry size; the return ring carries our
     * chosen 64 entries. */
    bcm_set_rcb(d, BCM_SRAM_SEND_RCB, (u32)(uintptr_t)d->tx_ring,
                BCM_TX_RING_SIZE, BCM_SRAM_TX_BUFFER_DESC);
    bcm_set_rcb(d, BCM_SRAM_RCV_RET_RCB, (u32)(uintptr_t)d->rx_ret_ring,
                BCM_RET_RING_SIZE, 0);

    /* --- MAC address (tg3_get_device_address tg3.c:17024-17096 +
     * __tg3_set_one_mac_addr tg3.c:3946).  Try the SRAM mailbox first,
     * then MAC_ADDR_0 regs; if nothing valid, synthesise a local one. */
    {
        u32 hi = bcm_sram_read(d, BCM_SRAM_MAC_ADDR_HI_MBOX);
        u32 lo = bcm_sram_read(d, BCM_SRAM_MAC_ADDR_LO_MBOX);
        int ok = 0;
        if ((hi >> 16) == BCM_SRAM_MAC_MAGIC) {
            d->mac[0] = (u8)(hi >> 8);
            d->mac[1] = (u8)hi;
            d->mac[2] = (u8)(lo >> 24);
            d->mac[3] = (u8)(lo >> 16);
            d->mac[4] = (u8)(lo >> 8);
            d->mac[5] = (u8)lo;
            ok = 1;
        } else {
            hi = bcm_reg(d, BCM_MAC_ADDR_0_HIGH);
            lo = bcm_reg(d, BCM_MAC_ADDR_0_LOW);
            d->mac[0] = (u8)(hi >> 8);
            d->mac[1] = (u8)hi;
            d->mac[2] = (u8)(lo >> 24);
            d->mac[3] = (u8)(lo >> 16);
            d->mac[4] = (u8)(lo >> 8);
            d->mac[5] = (u8)lo;
        }
        if (!ok && !((d->mac[0] | d->mac[1] | d->mac[2] |
                      d->mac[3] | d->mac[4] | d->mac[5]) & 0xFF)) {
            /* No NVM address visible: locally administered fallback,
             * derived from the PCI coordinates (never all zero). */
            d->mac[0] = 0x02; d->mac[1] = 0x00; d->mac[2] = 0x5E;
            d->mac[3] = d->bus; d->mac[4] = d->dev; d->mac[5] = d->func;
        }
        if (d->mac[0] & 0x01) d->mac[0] &= 0xFE;   /* not multicast */
        bcm_wreg(d, BCM_MAC_ADDR_0_HIGH,
                 ((u32)d->mac[0] << 8) | d->mac[1]);
        bcm_wreg(d, BCM_MAC_ADDR_0_LOW,
                 ((u32)d->mac[2] << 24) | ((u32)d->mac[3] << 16) |
                 ((u32)d->mac[4] << 8) | d->mac[5]);
        bcm_wreg(d, BCM_MAC_TX_BACKOFF_SEED,
                 ((u32)d->mac[0] + d->mac[1] + d->mac[2] + d->mac[3] +
                  d->mac[4] + d->mac[5]) & BCM_BACKOFF_SEED_MASK);
    }

    /* --- MTU / TX lengths / receive rules (tg3.c:10313-10334) --- */
    bcm_wreg(d, BCM_MAC_RX_MTU_SIZE, 1500 + 14 + 4 + 4);
    bcm_wreg(d, BCM_MAC_TX_LENGTHS, (2u << 12) | (6u << 8) | 32u);
    bcm_wreg(d, BCM_MAC_RCV_RULE_CFG, BCM_RCV_RULE_CFG_DEFAULT_CLASS);
    bcm_wreg(d, BCM_RCVLPC_CONFIG, 0x0181);

    /* --- statistics enables (tg3.c:10431-10448) --- */
    if (d->is_5705_plus) {
        val = bcm_reg(d, BCM_RCVLPC_STATS_ENABLE) & ~0x00040000u;
        bcm_wreg(d, BCM_RCVLPC_STATS_ENABLE, val);
    } else {
        bcm_wreg(d, BCM_RCVLPC_STATS_ENABLE, 0x00FFFFFFu);
    }
    bcm_wreg(d, BCM_RCVLPC_STATSCTRL, BCM_RCVLPC_STATSCTRL_ENABLE);
    bcm_wreg(d, BCM_SNDDATAI_STATSENAB, 0x00FFFFFFu);
    bcm_wreg(d, BCM_SNDDATAI_STATSCTRL,
             BCM_SNDI_SCTRL_ENABLE | BCM_SNDI_SCTRL_FASTUPD);

    /* --- Host coalescing / status block (tg3.c:10450-10482).  We poll,
     * so coalescing is degenerate: 0 ticks + 1 frame = update at once,
     * and the poll paths poke HOSTCC_MODE_NOW to force a snapshot (the
     * verified pattern of tg3's loopback self-test, tg3.c:13584-13621). */
    bcm_wreg(d, BCM_HOSTCC_MODE, 0);
    for (i = 0; i < 200000; i++) {
        if (!(bcm_reg(d, BCM_HOSTCC_MODE) & BCM_HOSTCC_MODE_ENABLE)) break;
        bcm_delay(100);
    }
    bcm_wreg(d, BCM_HOSTCC_RXCOL_TICKS, 0);
    bcm_wreg(d, BCM_HOSTCC_TXCOL_TICKS, 0);
    bcm_wreg(d, BCM_HOSTCC_RXMAX_FRAMES, 1);
    bcm_wreg(d, BCM_HOSTCC_TXMAX_FRAMES, 1);
    bcm_wreg(d, BCM_HOSTCC_STAT_COAL_TICKS, 0);
    bcm_wreg(d, BCM_HOSTCC_STATUS_BLK_HOST_ADDR + 0, 0);
    bcm_wreg(d, BCM_HOSTCC_STATUS_BLK_HOST_ADDR + 4, (u32)(uintptr_t)d->sblk);
    bcm_wreg_f(d, BCM_HOSTCC_MODE, BCM_HOSTCC_MODE_ENABLE);

    /* --- Engine enables (tg3.c:10484-10640) --- */
    bcm_wreg(d, BCM_RCVCC_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);
    bcm_wreg(d, BCM_RCVLPC_MODE, BCM_MODE_ENABLE);
    if (!d->is_5705_plus)
        bcm_wreg(d, BCM_RCVLSC_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);

    /* MAC_MODE (tg3.c:10496-10506). */
    val = BCM_MAC_MODE_TXSTAT_ENABLE | BCM_MAC_MODE_RXSTAT_ENABLE |
          BCM_MAC_MODE_TDE_ENABLE | BCM_MAC_MODE_RDE_ENABLE |
          BCM_MAC_MODE_FHDE_ENABLE;
    if (!d->is_5705_plus && (d->chip_rev_id >> 12) != 0x07)
        val |= BCM_MAC_MODE_LINK_POLARITY;      /* not on the 5700 */
    bcm_wreg_f(d, BCM_MAC_MODE,
               val | BCM_MAC_MODE_RXSTAT_CLEAR | BCM_MAC_MODE_TXSTAT_CLEAR);
    bcm_delay(40000);                           /* tg3: udelay(40) */

    bcm_wreg_f(d, BCM_WDMAC_MODE, BCM_MODE_ENABLE | BCM_WDMAC_ERRS);
    bcm_delay(40000);                           /* tg3: udelay(40) */
    val = BCM_MODE_ENABLE | BCM_RDMAC_ERRS;
    if (d->is_pcie) val |= BCM_RDMAC_FIFO_LONG_BURST;
    bcm_wreg_f(d, BCM_RDMAC_MODE, val);
    bcm_delay(40000);                           /* tg3: udelay(40) */

    bcm_wreg(d, BCM_RCVDCC_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);
    if (!d->is_5705_plus) {
        bcm_wreg_f(d, BCM_DMAC_MODE, BCM_MODE_ENABLE);
        bcm_delay(40000);                       /* tg3: udelay(40) */
        bcm_wreg(d, BCM_MBFREE_MODE, BCM_MODE_ENABLE);
    }
    bcm_wreg(d, BCM_SNDDATAC_MODE, BCM_MODE_ENABLE);
    bcm_wreg(d, BCM_SNDBDC_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);
    bcm_wreg(d, BCM_RCVBDI_MODE,
             BCM_MODE_ENABLE | BCM_RCVBDI_MODE_RCB_ATTN_ENAB);
    bcm_wreg(d, BCM_RCVDBDI_MODE,
             BCM_MODE_ENABLE | BCM_RCVDBDI_MODE_INV_RING_SZ);
    bcm_wreg(d, BCM_SNDDATAI_MODE, BCM_MODE_ENABLE);
    bcm_wreg(d, BCM_SNDBDI_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);
    bcm_wreg(d, BCM_SNDBDS_MODE, BCM_MODE_ENABLE | BCM_MODE_ATTN_ENABLE);

    /* --- MAC TX/RX enables (tg3.c:10661-10714) --- */
    bcm_wreg_f(d, BCM_MAC_TX_MODE, BCM_TX_MODE_ENABLE);
    bcm_delay(100000);                          /* tg3: udelay(100) */
    bcm_wreg_f(d, BCM_MAC_RX_MODE, BCM_RX_MODE_ENABLE);
    bcm_delay(10000);                           /* tg3: udelay(10) */
    bcm_wreg(d, BCM_MAC_MI_STAT, BCM_MI_STAT_LNKSTAT_ATTN_ENAB);
    bcm_wreg_f(d, BCM_MAC_RX_MODE, BCM_RX_MODE_ENABLE);
    bcm_delay(10000);                           /* tg3: udelay(10) */
    bcm_wreg_f(d, BCM_MAC_LOW_WMARK_MAX_RX_FRAME, 2);

    /* --- PHY: restart autonegotiation (802.3 BMCR 0x1200).  tg3's full
     * tg3_setup_phy is a table-driven MDIO machine; this minimal kick is
     * the same idea as the igb driver's BMCR write.  Non-fatal. */
    (void)bcm_mdio_write(d, 0, 0x1200);

    return 0;
}

/* =====================================================================
 * Datapath helpers
 * ===================================================================== */

/* Force the chip to DMA a fresh status-block snapshot
 * (HOSTCC_MODE_NOW - tg3.c:13584/13611 loopback pattern: tw32_f then
 * udelay(10) before reading hw_status). */
static void bcm_status_refresh(bcm_dev_t *d) {
    bcm_wreg_f(d, BCM_HOSTCC_MODE,
               BCM_HOSTCC_MODE_ENABLE | BCM_HOSTCC_MODE_NOW);
    bcm_delay(1000);                            /* tg3: udelay(10) */
}

/* Reap completed TX descriptors by status-block tx_consumer (tg3_tx
 * tg3.c:6557-6645): return freed buffers to the free stack. */
static void bcm_tx_reap(bcm_dev_t *d) {
    u16 hw = d->sblk->idx[0].tx_consumer;
    while (d->tx_cons != hw) {
        u32 slot = d->tx_cons & (BCM_TX_RING_SIZE - 1);
        u8 *buf = d->tx_slot_buf[slot];
        if (buf && d->tx_free_cnt < BCM_TX_BUDGET)
            d->tx_free_buf[d->tx_free_cnt++] = buf;
        d->tx_slot_buf[slot] = NULL;
        d->tx_cons = (d->tx_cons + 1) & (BCM_TX_RING_SIZE - 1);
    }
}

/* Re-arm a std producer slot with a buffer and advance the producer
 * mailbox (tg3_alloc_rx_data + tg3_rx refill, tg3.c:6698/7010-7020).
 * The chip only READS this ring - never writes it back. */
static void bcm_rx_post(bcm_dev_t *d, u8 *buf) {
    u32 slot = d->rx_std_prod & (BCM_STD_RING_SIZE - 1);
    d->rx_std_ring[slot].addr_hi = 0;
    d->rx_std_ring[slot].addr_lo = (u32)(uintptr_t)buf;
    d->rx_std_buf[slot] = buf;
    __asm__ volatile("sfence" ::: "memory");
    d->rx_std_prod = (d->rx_std_prod + 1) & (BCM_STD_RING_SIZE - 1);
    bcm_mbox(d, BCM_STD_PROD_MBOX, d->rx_std_prod);
}

/* =====================================================================
 * Framework operations (interface preserved 1:1 from the legacy driver)
 * ===================================================================== */
static int driver_nic_bcm57xx_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int driver_nic_bcm57xx_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int driver_nic_bcm57xx_ops_link(driver_nic_device_t *ndev);
static int driver_nic_bcm57xx_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t driver_nic_bcm57xx_nic_ops = {
    .send        = driver_nic_bcm57xx_ops_send,
    .recv        = driver_nic_bcm57xx_ops_recv,
    .link_status = driver_nic_bcm57xx_ops_link,
    .get_mac     = driver_nic_bcm57xx_ops_mac,
};

static int driver_nic_bcm57xx_setup(bcm_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (!bar0) return -1;
    /* BUG-0332 (A8-14): read_bar strips the space-type bits so the old
     * (bar0 & 0x1) test never fired; check the raw config dword. */
    if (driver_pci_read_config(bus, dev, func, 0x10) & 0x1) return -1;
    bcm_pci_enable(d);
    d->mmio = (volatile u8 *)(uintptr_t)(bar0 & 0xFFFFFFF0);
    d->is_pcie = (u8)bcm_is_pcie(d);

    /* Chip revision from the config mirror of MISC_HOST_CTRL
     * (tg3_detect_asic_rev tg3.c:16133; asic rev = id >> 12, tg3.h:3448).
     * 5705_PLUS covers 5705(3)/5750(4)/5752(6)/5780(8)/5714(9)/later. */
    d->chip_rev_id = bcm_reg(d, BCM_MISC_HOST_CTRL) >> BCM_MHC_CHIPREV_SHIFT;
    u32 asic = d->chip_rev_id >> 12;
    d->is_5705_plus = (asic == 0x03 || asic == 0x04 || asic == 0x06 ||
                       asic >= 0x08) ? 1 : 0;

    /* Save clock control before the core reset clears it
     * (tg3_save_pci_state + tg3.c:10071-10075). */
    d->pci_clock_ctrl = bcm_reg(d, BCM_CLOCK_CTRL);

    if (bcm_alloc_rings(d) != 0) {
        if (driver_nic_bcm57xx_dma_over4g)
            screen_console_puts("bcm57xx: DMA memory above 4GiB - refused\n");
        return -1;
    }
    if (bcm_init_hw(d) != 0) return -1;

    d->up = 1;
    return 0;
}

int bcm57xx_init(driver_pci_dev_t *pdev) {
    if (g_bcm.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        /* BUG-0336 (A8-8): the old loop only ever SET known - the
         * whitelist degraded to "any Broadcom device". Match vid AND did. */
        int known = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_bcm57xx_ids) / sizeof(driver_nic_bcm57xx_ids[0]); i++)
            if (vid == 0x14E4 && did == driver_nic_bcm57xx_ids[i].did) { known = 1; break; }
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_bcm57xx_ids) / sizeof(driver_nic_bcm57xx_ids[0]) && !found; i++) {
            if (driver_pci_find_device(driver_nic_bcm57xx_ids[i].vid, driver_nic_bcm57xx_ids[i].did,
                                &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (driver_nic_bcm57xx_setup(&g_bcm, bus, dev, func) != 0) {
        screen_console_puts("bcm57xx: init failed\n");
        return -1;
    }

    driver_nic_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "bcm57xx");
    nd.type = NIC_TYPE_BCM57XX;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_bcm.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x14E4;
    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_bcm;
    int idx = nic_dev_add(&nd, &driver_nic_bcm57xx_nic_ops);

    char line[96];
    char n[24];
    strcpy(line, "bcm57xx: Tigon3 at ");
    u64_to_hex(bus, n, 2);  strcat(line, n); strcat(line, ":");
    u64_to_hex(dev, n, 2);  strcat(line, n); strcat(line, ".");
    u64_to_hex(func, n, 1); strcat(line, n);
    strcat(line, " mac=");
    for (int k = 0; k < 6; k++) {
        u64_to_hex(g_bcm.mac[k], n, 2); strcat(line, n);
        if (k < 5) strcat(line, ":");
    }
    strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    screen_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int driver_nic_bcm57xx_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up || len <= 0 || len > BCM_BUF_SIZE) return -1;

    bcm_tx_reap(d);

    /* Ring-budget check (tg3_tx_avail tg3.c:6545-6551); force one status
     * refresh before giving up so a stale block cannot fake a full ring. */
    u32 inflight = (d->tx_prod - d->tx_cons) & (BCM_TX_RING_SIZE - 1);
    if (inflight >= BCM_TX_BUDGET || d->tx_free_cnt == 0) {
        bcm_status_refresh(d);
        bcm_tx_reap(d);
        inflight = (d->tx_prod - d->tx_cons) & (BCM_TX_RING_SIZE - 1);
        if (inflight >= BCM_TX_BUDGET || d->tx_free_cnt == 0) {
            d->tx_errors++;
            return -1;              /* honest failure, no silent drop */
        }
    }

    u32 slot = d->tx_prod & (BCM_TX_RING_SIZE - 1);
    u8 *dma = d->tx_free_buf[--d->tx_free_cnt];
    memcpy(dma, buf, (usize)len);

    /* tg3_tx_set_bd (tg3.c:7717-7725); single-BD frame, END flag.  The
     * 4GiB tripwire also rejects a buffer that would straddle the 4 GiB
     * boundary (tg3_4g_overflow_test tg3.c:7683). */
    u32 base = (u32)(uintptr_t)dma;
    if (base + (u32)len + 8 < base) {           /* wrapped past 4 GiB */
        d->tx_free_buf[d->tx_free_cnt++] = dma;
        d->tx_errors++;
        return -1;
    }
    d->tx_ring[slot].addr_hi   = 0;
    d->tx_ring[slot].addr_lo   = base;
    d->tx_ring[slot].vlan_tag  = 0;
    d->tx_slot_buf[slot]       = dma;
    __asm__ volatile("sfence" ::: "memory");
    d->tx_ring[slot].len_flags = ((u32)len << BCM_TXD_LEN_SHIFT) | BCM_TXD_FLAG_END;

    /* Doorbell: host TX producer mailbox (tg3.c:13601-13605). */
    d->tx_prod = (d->tx_prod + 1) & (BCM_TX_RING_SIZE - 1);
    bcm_mbox(d, BCM_TX_PROD_MBOX_0, d->tx_prod);

    /* BUG-0126 semantics (honest completion): poll the status-block
     * tx_consumer until it passes this descriptor, bounded; a timeout is
     * reported as -1 instead of counting the frame as sent. */
    int ok = 0;
    for (int t = 0; t < 20000; t++) {
        if (d->tx_cons == d->tx_prod) { ok = 1; break; }
        if ((t & 0x3F) == 0) bcm_status_refresh(d);
        bcm_tx_reap(d);
    }
    if (!ok) {
        d->tx_errors++;
        return -1;
    }
    d->tx_packets++;
    return len;
}

static int driver_nic_bcm57xx_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up) return -1;

    /* RX = return-ring polling (tg3_rx tg3.c:6835-7043): the chip writes
     * completions here and the producer comes from the DMA'd status
     * block.  If the block shows no work, force one snapshot and look
     * again before reporting "no frame". */
    u16 hw = d->sblk->idx[0].rx_producer;
    if (hw == (u16)d->rx_ret_cons) {
        bcm_status_refresh(d);
        hw = d->sblk->idx[0].rx_producer;
        if (hw == (u16)d->rx_ret_cons) return 0;
    }

    u32 cons = d->rx_ret_cons & (BCM_RET_RING_SIZE - 1);
    bcm_rx_bd_t *desc = &d->rx_ret_ring[cons];

    /* ACK the return ring FIRST, then refill the std producer - the
     * mailbox order of tg3_rx (tg3.c:7006-7020: consmbox, then prod
     * mailbox after the BD writes are visible). */
    d->rx_ret_cons = (d->rx_ret_cons + 1) & (BCM_RET_RING_SIZE - 1);
    bcm_mbox(d, BCM_RET_CON_MBOX_0, d->rx_ret_cons);

    u32 slot = desc->opaque & BCM_RXD_OPAQUE_IDX_MASK;
    if (slot >= BCM_STD_RING_SIZE || !d->rx_std_buf[slot]) {
        /* Bogus completion: drop it (defensive). */
        d->rx_dropped++;
        return 0;
    }
    u8 *data = d->rx_std_buf[slot];

    if (desc->err_vlan & BCM_RXD_ERR_MASK) {
        /* Error frame (tg3.c:6883-6891): recycle and drop. */
        bcm_rx_post(d, data);
        d->rx_dropped++;
        return 0;
    }

    int len = (int)(desc->idx_len & BCM_RXD_LEN_MASK) - 4;   /* strip FCS */
    if (len < 14 || !(desc->type_flags & BCM_RXD_FLAG_END)) {
        /* Runts or multi-BD frames (impossible with 1536-byte buffers
         * and a 1522-byte MTU window): drop honestly. */
        bcm_rx_post(d, data);
        d->rx_dropped++;
        return 0;
    }
    if (len > maxlen) len = maxlen;
    memcpy(buf, data, (usize)len);

    /* Recycle the same buffer into the next producer slot. */
    bcm_rx_post(d, data);
    d->rx_packets++;
    return len;
}

static int driver_nic_bcm57xx_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up || !d->mmio) return -1;
    /* Copper link via PHY BMSR (reg 1): bit 2 = link status (latched low,
     * so read twice per 802.3); fall back to MAC_STATUS SIGNAL_DET
     * (tg3.h:408) when MDIO is unavailable. */
    u16 bmsr = 0;
    if (bcm_mdio_read(d, 1, &bmsr) == 0) {
        u16 bmsr2 = 0;
        (void)bcm_mdio_read(d, 1, &bmsr2);
        return ((bmsr | bmsr2) & 0x0004) ? 1 : 0;
    }
    return (bcm_reg(d, BCM_MAC_STATUS) & BCM_MAC_STATUS_SIGNAL_DET) ? 1 : 0;
}

static int driver_nic_bcm57xx_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void bcm57xx_print_state(void) {
    bcm_dev_t *d = &g_bcm;
    char line[160];
    char n[24];
    if (!d->up) {
        screen_console_puts("bcm57xx: not present\n");
        return;
    }
    strcpy(line, "bcm57xx: pci=");
    u64_to_hex(d->bus, n, 2); strcat(line, n); strcat(line, ":");
    u64_to_hex(d->dev, n, 2); strcat(line, n); strcat(line, ".");
    u64_to_hex(d->func, n, 1); strcat(line, n);
    strcat(line, " tg3-asic=");
    u64_to_hex(d->chip_rev_id >> 12, n, 1); strcat(line, n);
    strcat(line, " link=");
    strcat(line, (driver_nic_bcm57xx_ops_link(NULL) > 0) ? "up" : "down");
    strcat(line, " tx=");
    u64_to_str(d->tx_packets, n); strcat(line, n);
    strcat(line, " rx=");
    u64_to_str(d->rx_packets, n); strcat(line, n);
    strcat(line, " drop=");
    u64_to_str(d->rx_dropped, n); strcat(line, n);
    strcat(line, " txerr=");
    u64_to_str(d->tx_errors, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
}

/* ---- v2.0.0 file-local driver identity (collected at boot) ---- */
static int nic_probe_bcm57xx(void) { return bcm57xx_init(NULL); }
static const nic_drv_ops_t g_nic_probe_bcm57xx = { nic_probe_bcm57xx };
OC_DRIVER_DECL("nic", "bcm57xx", &g_nic_probe_bcm57xx, "Broadcom 57xx (tg3)");
