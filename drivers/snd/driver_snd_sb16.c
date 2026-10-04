/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/sb16.c
 * Purpose: Sound Blaster 16 (ISA) DSP + ISA DMA driver.
 *
 * The classic ISA sound card, still emulated by every QEMU build
 * (`-device sb16,audiodev=...` at io 0x220, IRQ 5, DMA 1/5).
 *
 * DSP command path (verified against QEMU hw/audio/sb16):
 *   - reset: 0x226 <- 1, wait, 0x226 <- 0; DSP answers 0xAA
 *   - version: 0xE1 -> 2 bytes (QEMU reports 4.05)
 *   - sample rate: 0x41 lo hi (Hz, 5000..45000 supported)
 *   - 16-bit playback (single cycle): 0xB0 mode lo hi where
 *     mode bit4 = signed, bit5 = stereo, and len counts 16-bit
 *     words (bytes = (len+1) * 2).  QEMU's dma_cmd: block_size =
 *     (len+1) << 1 for 16-bit.
 *   - IRQ: DSP raises IRQ5 when a block completes; read 0x22F to
 *     ack a 16-bit IRQ (0x22E acks 8-bit ones).
 *
 * ISA DMA (8237 second controller, 16-bit channel 5):
 *   page 0x8B, address 0xC4 / count 0xC6 (word units), mask 0xD4,
 *   mode 0xD6, clear byte-pointer flip-flop 0xD8.  Mode byte:
 *   single-cycle read transfer = 0x48.  Addresses/counts are in
 *   words (>>1) and pages are 64 KiB (bits 23:16).
 */
#include "driver_snd_sb16.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "driver_snd.h"
#include "core_sched.h"
#include "core_timer.h"

#define SB_BASE        0x220
#define SB_RESET       (SB_BASE + 0x06)
#define SB_READ        (SB_BASE + 0x0a)
#define SB_WRITE       (SB_BASE + 0x0c)
#define SB_STATUS      (SB_BASE + 0x0e)
#define SB_ACK16       (SB_BASE + 0x0f)

#define SB_IRQ         5
#define SB_DMA16       5

/* DMA controller 2 (16-bit channels 4..7) */
#define DMA2_FF        0xd8
#define DMA2_MASK      0xd4
#define DMA2_MODE      0xd6
#define DMA2_ADDR(ch)  (0xc0 + ((ch) - 4) * 4)   /* ch5 -> 0xc4 */
#define DMA2_CNT(ch)   (0xc2 + ((ch) - 4) * 4)   /* ch5 -> 0xc6 */
#define DMA2_PAGE(ch)  (0x88 + (ch) - 4)         /* ch5 page register */

#define DSP_CMD_SET_RATE    0x41
#define DSP_CMD_PLAY16      0xb0   /* single-cycle 16-bit DAC */
#define DSP_CMD_GET_VERSION 0xe1

#define SB_DMA_BUF_BYTES    8192   /* 2048 stereo frames = 4096 words */

typedef struct driver_snd_sb16_dev {
    u16 io;
    u8  dsp_ver_hi, dsp_ver_lo;
    u64 dma_phys;
    u8 *dma;
    int irq;
    volatile u64 irq_seen;
    u64 irq_count;
    u32 rate;
    u8  channels;
    int up;
} driver_snd_sb16_dev_t;

static driver_snd_sb16_dev_t g_sb16;

/* forward decls */
static int driver_snd_sb16_ops_play(driver_snd_device_t *sdev, const void *buf, int len);
static int driver_snd_sb16_ops_stop(driver_snd_device_t *sdev);
static int driver_snd_sb16_ops_set_rate(driver_snd_device_t *sdev, u32 rate);
static int driver_snd_sb16_ops_set_volume(driver_snd_device_t *sdev, u32 vol);
static int driver_snd_sb16_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps);
static const driver_snd_ops_t driver_snd_sb16_snd_ops = {
    .play       = driver_snd_sb16_ops_play,
    .stop       = driver_snd_sb16_ops_stop,
    .set_rate   = driver_snd_sb16_ops_set_rate,
    .set_volume = driver_snd_sb16_ops_set_volume,
    .get_caps   = driver_snd_sb16_ops_get_caps,
};

static inline void driver_snd_sb16_outb(u16 port, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port)); }
static inline u8   driver_snd_sb16_inb(u16 port)         { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static void driver_snd_sb16_log(const char *s) { screen_console_puts(s); }

static void driver_snd_sb16_io_delay(void) {
    for (int i = 0; i < 8; i++) driver_snd_sb16_inb(0x80);
}

/* ---- DSP helpers ---- */

static int driver_snd_sb16_dsp_write(driver_snd_sb16_dev_t *d, u8 v) {
    for (int t = 0; t < 100000; t++) {
        if (!(driver_snd_sb16_inb(d->io + 0x0c) & 0x80)) {
            driver_snd_sb16_outb(SB_WRITE, v);
            return 0;
        }
    }
    return -1;   /* DSP busy */
}

static int driver_snd_sb16_dsp_read(driver_snd_sb16_dev_t *d, u8 *out) {
    (void)d;   /* DSP data port is fixed at 0x22A */
    for (int t = 0; t < 100000; t++) {
        if (driver_snd_sb16_inb(SB_STATUS) & 0x80) {
            *out = driver_snd_sb16_inb(SB_READ);
            return 0;
        }
    }
    return -1;
}

/* reset the DSP; returns 0 when the DSP answers 0xAA */
static int driver_snd_sb16_dsp_reset(driver_snd_sb16_dev_t *d) {
    driver_snd_sb16_outb(SB_RESET, 1);
    driver_snd_sb16_io_delay();
    driver_snd_sb16_outb(SB_RESET, 0);

    for (int t = 0; t < 100000; t++) {
        u8 v;
        if (driver_snd_sb16_dsp_read(d, &v) == 0) {
            if (v == 0xAA) return 0;
        }
    }
    return -1;
}

/* ---- IRQ ---- */
static void driver_snd_sb16_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    driver_snd_sb16_dev_t *d = &g_sb16;
    if (!d->up) return;
    /* ack 16-bit IRQ (read 0x22F); EOI is done by the PIC layer */
    driver_snd_sb16_inb(SB_ACK16);
    d->irq_seen++;
    d->irq_count++;
}

/* ---- ISA DMA programming (16-bit channel) ---- */

static int driver_snd_sb16_dma_setup(driver_snd_sb16_dev_t *d, u32 bytes) {
    u32 words = bytes / 2;
    u64 addr = d->dma_phys;
    if (bytes == 0 || bytes & 1) return -1;
    if (words > 65536) return -1;
    if (addr & 1) return -1;   /* word aligned */

    u16 word_addr = (u16)((addr >> 1) & 0xffff);
    u8  page = (u8)((addr >> 16) & 0xff);

    driver_snd_sb16_outb(DMA2_MASK, 0x05);          /* mask channel 5 */
    driver_snd_sb16_outb(DMA2_FF, 0);               /* clear flip-flop */
    driver_snd_sb16_outb(DMA2_MODE, 0x48);          /* single-cycle read (mem->dev) */
    driver_snd_sb16_outb(DMA2_ADDR(SB_DMA16), (u8)(word_addr & 0xff));
    driver_snd_sb16_outb(DMA2_ADDR(SB_DMA16), (u8)(word_addr >> 8));
    driver_snd_sb16_outb(DMA2_PAGE(SB_DMA16), page);
    driver_snd_sb16_outb(DMA2_FF, 0);
    u16 cnt = (u16)(words - 1);
    driver_snd_sb16_outb(DMA2_CNT(SB_DMA16), (u8)(cnt & 0xff));
    driver_snd_sb16_outb(DMA2_CNT(SB_DMA16), (u8)(cnt >> 8));
    driver_snd_sb16_outb(DMA2_MASK, 0x01);          /* unmask channel 5 */
    return 0;
}

/* ---- playback ---- */

/* Program one 16-bit single-cycle DMA block and wait for its IRQ. */
static int driver_snd_sb16_play_block(driver_snd_sb16_dev_t *d, const u8 *src, u32 bytes) {
    if (bytes < 2 || bytes > SB_DMA_BUF_BYTES) return -1;
    u32 words = bytes / 2;

    memcpy(d->dma, src, bytes);
    if (driver_snd_sb16_dma_setup(d, bytes) != 0) return -1;

    u64 before = d->irq_seen;
    u8 mode = 0x10 | 0x20;   /* signed + stereo */
    if (driver_snd_sb16_dsp_write(d, DSP_CMD_PLAY16) != 0) return -1;
    if (driver_snd_sb16_dsp_write(d, mode) != 0) return -1;
    u16 len = (u16)(words - 1);
    if (driver_snd_sb16_dsp_write(d, (u8)(len & 0xff)) != 0) return -1;
    if (driver_snd_sb16_dsp_write(d, (u8)(len >> 8)) != 0) return -1;

    /* wait for the block-completion IRQ (2 s grace period) */
    u64 deadline = core_timer_now_ms() + 2000;
    while (d->irq_seen == before) {
        if (core_timer_now_ms() > deadline) {
            driver_snd_sb16_log("sb16: block IRQ timeout\n");
            return -1;
        }
        core_sched_yield();
    }
    return (int)bytes;
}

static int driver_snd_sb16_ops_play(driver_snd_device_t *sdev, const void *buf, int len) {
    driver_snd_sb16_dev_t *d = (driver_snd_sb16_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;

    while (sent < len) {
        u32 chunk = (u32)(len - sent);
        if (chunk > SB_DMA_BUF_BYTES) chunk = SB_DMA_BUF_BYTES;
        /* keep 4-byte frame alignment */
        chunk &= ~3u;
        if (chunk < 4) chunk = 4;
        int rc = driver_snd_sb16_play_block(d, src + sent, chunk);
        if (rc < 0) return sent ? sent : -1;
        sent += rc;
    }
    return sent;
}

static int driver_snd_sb16_ops_stop(driver_snd_device_t *sdev) {
    driver_snd_sb16_dev_t *d = (driver_snd_sb16_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    /* halt 16-bit DMA */
    driver_snd_sb16_dsp_write(d, 0xd5);
    driver_snd_sb16_outb(DMA2_MASK, 0x05);
    sdev->irqs = d->irq_count;
    return 0;
}

static int driver_snd_sb16_set_rate_hw(driver_snd_sb16_dev_t *d, u32 rate) {
    if (rate < 5000 || rate > 45000) return -1;
    if (driver_snd_sb16_dsp_write(d, DSP_CMD_SET_RATE) != 0) return -1;
    if (driver_snd_sb16_dsp_write(d, (u8)(rate & 0xff)) != 0) return -1;
    if (driver_snd_sb16_dsp_write(d, (u8)(rate >> 8)) != 0) return -1;
    d->rate = rate;
    return 0;
}

static int driver_snd_sb16_ops_set_rate(driver_snd_device_t *sdev, u32 rate) {
    driver_snd_sb16_dev_t *d = (driver_snd_sb16_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    return driver_snd_sb16_set_rate_hw(d, rate);
}

static int driver_snd_sb16_ops_set_volume(driver_snd_device_t *sdev, u32 vol) {
    (void)sdev;
    (void)vol;   /* the SB16 mixer is not wired through this driver yet;
                  * volume is tracked by the framework */
    return 0;
}

static int driver_snd_sb16_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps) {
    driver_snd_sb16_dev_t *d = (driver_snd_sb16_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    caps->rates = 0;
    caps->min_channels = 1;
    caps->max_channels = 2;
    caps->bits8 = 1;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->cur_rate = d->rate;
    caps->cur_volume = sdev->volume;
    return 0;
}

/* ---- init ---- */

int driver_snd_sb16_init(void *isa_dev) {
    (void)isa_dev;
    driver_snd_sb16_dev_t *d = &g_sb16;
    if (d->up) return 0;

    d->io = SB_BASE;
    if (driver_snd_sb16_dsp_reset(d) != 0) {
        return -1;   /* no DSP at 0x220 (normal on most systems) */
    }

    u8 vh = 0, vl = 0;
    if (driver_snd_sb16_dsp_write(d, DSP_CMD_GET_VERSION) != 0) return -1;
    if (driver_snd_sb16_dsp_read(d, &vh) != 0) return -1;
    if (driver_snd_sb16_dsp_read(d, &vl) != 0) return -1;
    d->dsp_ver_hi = vh;
    d->dsp_ver_lo = vl;
    if (vh < 4) {
        driver_snd_sb16_log("sb16: DSP older than SB16 (v<4.00)\n");
        return -1;
    }

    /* DMA buffer */
    d->dma_phys = mem_pmm_alloc_contig(SB_DMA_BUF_BYTES / PMM_PAGE_SIZE + 1);
    if (!d->dma_phys) return -1;
    d->dma = (u8 *)(uintptr_t)d->dma_phys;
    memset(d->dma, 0, SB_DMA_BUF_BYTES);

    /* speaker on */
    driver_snd_sb16_dsp_write(d, 0xd1);

    /* IRQ: the classic SB16 IRQ 5 */
    if (arch_irq_register_handler(SB_IRQ, driver_snd_sb16_irq_handler, NULL) == 0)
        d->irq = SB_IRQ;
    else
        d->irq = -1;

    d->channels = 2;
    d->up = 1;
    driver_snd_sb16_set_rate_hw(d, 44100);   /* program the default rate */

    char line[96];
    char n[8];
    strcpy(line, "sb16: DSP ");
    u64_to_hex(vh, n, 1); strcat(line, n);
    strcat(line, ".");
    u64_to_hex(vl, n, 2); strcat(line, n);
    strcat(line, " io=0x220 irq=");
    u64_to_str(SB_IRQ, n); strcat(line, n);
    strcat(line, " dma16=");
    u64_to_str(SB_DMA16, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");

    driver_snd_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "sb16");
    nd.type = SND_TYPE_SB16;
    nd.rate = d->rate; nd.channels = d->channels; nd.volume = 100;
    nd.priv = d;
    int idx = driver_snd_register(&nd, &driver_snd_sb16_snd_ops);
    return idx >= 0 ? 0 : -1;
}

void driver_snd_sb16_print_state(void) {
    driver_snd_sb16_dev_t *d = &g_sb16;
    char line[128];
    char n[24];

    screen_console_puts("Sound Blaster 16: ");
    if (!d->up) {
        screen_console_puts("not present\n");
        return;
    }
    strcpy(line, "dsp=");
    u64_to_hex(d->dsp_ver_hi, n, 1); strcat(line, n);
    strcat(line, ".");
    u64_to_hex(d->dsp_ver_lo, n, 2); strcat(line, n);
    strcat(line, " io=0x220 irq=");
    u64_to_str(d->irq, n); strcat(line, n);
    strcat(line, " dma16=");
    u64_to_str(SB_DMA16, n); strcat(line, n);
    strcat(line, " rate=");
    u64_to_str(d->rate, n); strcat(line, n);
    strcat(line, " irqs=");
    u64_to_str(d->irq_count, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
}
