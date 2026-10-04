/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/es1370.c
 * Purpose: Ensoniq AudioPCI ES1370/ES1371 (Ensoniq Soundscape / AudioPCI
 *          97, Creative SB PCI 64/128 ancestors) PCI sound driver.
 *
 * Mainstream legacy PCI sound: Ensoniq ES1370 (1274:5000) and ES1371
 * (1274:1371).  QEMU exposes it as `-device ES1370` with the same
 * register layout.
 *
 * Register layout (verified against QEMU hw/audio/es1370; BAR0 = I/O):
 *   0x00 CONTROL    0x04 STATUS     0x0C MEMPAGE   0x10 CODEC
 *   0x20 SCTRL      0x24 DAC1_SCOUNT  0x28 DAC2_SCOUNT  0x2C ADC_SCOUNT
 *   Frame descriptors live behind the MEMPAGE window: MEMPAGE = 0xC maps
 *   0x30 -> DAC1_FRAMEADR, 0x34 -> DAC1_FRAMECNT, 0x38 -> DAC2_FRAMEADR,
 *   0x3C -> DAC2_FRAMECNT.
 *   FRAMECNT: bits 15:0 = buffer size in dwords, bits 31:16 = consumed
 *   position in dwords.  SCOUNT: written reload value r produces an IRQ
 *   every (r+1) frames (frame = 4 bytes in 16-bit stereo mode).
 *
 * This driver plays through DAC2 (programmable clock):
 *   rate = 1411200 / (PCLKDIV + 2), PCLKDIV in CONTROL bits 29:16.
 *   44100 -> PCLKDIV 30 (exact), 48000 -> PCLKDIV 27 (48662 Hz, the
 *   closest this silicon gets), 22050 -> 62 (exact), 8000 -> 175.
 *
 * Playback model (single-cycle, stop-at-end mode):
 *   - copy one <= 16 KiB block into the PMM buffer
 *   - FRAMEADR = buffer, FRAMECNT = size dwords (cnt 0), SCOUNT reload
 *   - SCTRL: P2INTEN | P2FMT=11 (16-bit stereo), P2LOOPSEL = 1 (stop)
 *   - CONTROL: DAC2_EN | PCLKDIV -> the block plays; the completion IRQ
 *     (STAT_DAC2) is cleared by toggling P2INTEN, then the next block
 *     is queued.
 */
#include "driver_snd_es1370.h"
#include "driver_pci.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "driver_snd.h"
#include "core_sched.h"
#include "core_timer.h"

/* registers */
#define ES1370_REG_CONTROL        0x00
#define ES1370_REG_STATUS         0x04
#define ES1370_REG_MEMPAGE        0x0c
#define ES1370_REG_CODEC          0x10
#define ES1370_REG_SCTRL          0x20
#define ES1370_REG_DAC1_SCOUNT    0x24
#define ES1370_REG_DAC2_SCOUNT    0x28
#define ES1370_REG_DAC1_FRAMEADR  0x30
#define ES1370_REG_DAC1_FRAMECNT  0x34
#define ES1370_REG_DAC2_FRAMEADR  0x38
#define ES1370_REG_DAC2_FRAMECNT  0x3c

/* CONTROL bits */
#define ES_CTRL_DAC2_EN     (1u << 5)
#define ES_CTRL_PCLKDIV_SH  16
#define ES_CTRL_PCLKDIV_MSK (0x1fffu << 16)

/* STATUS bits */
#define ES_STAT_DAC2        (1u << 1)

/* SCTRL bits (DAC2) */
#define ES_SCTRL_P2FMT_MSK  (3u << 2)   /* 11 = 16-bit stereo */
#define ES_SCTRL_P2INTEN    (1u << 9)
#define ES_SCTRL_P2LOOPSEL  (1u << 14)
#define ES_SCTRL_P2PAUSE    (1u << 12)

/* block geometry: 4096 frames x 4 bytes */
#define ES_BLOCK_FRAMES     4096
#define ES_BLOCK_BYTES      (ES_BLOCK_FRAMES * 4)

typedef struct driver_snd_es1370_dev {
    u16 io;
    u8  bus, dev, func;
    u16 vid, did;
    u64 dma_phys;
    u8 *dma;
    int irq;
    volatile u64 irq_seen;
    u64 irq_count;
    u32 rate;
    u32 pclkdiv;
    u8  channels;
    int up;
} driver_snd_es1370_dev_t;

static driver_snd_es1370_dev_t g_es1370;

/* forward decls */
static int driver_snd_es1370_ops_play(driver_snd_device_t *sdev, const void *buf, int len);
static int driver_snd_es1370_ops_stop(driver_snd_device_t *sdev);
static int driver_snd_es1370_ops_set_rate(driver_snd_device_t *sdev, u32 rate);
static int driver_snd_es1370_ops_set_volume(driver_snd_device_t *sdev, u32 vol);
static int driver_snd_es1370_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps);
static const driver_snd_ops_t driver_snd_es1370_snd_ops = {
    .play       = driver_snd_es1370_ops_play,
    .stop       = driver_snd_es1370_ops_stop,
    .set_rate   = driver_snd_es1370_ops_set_rate,
    .set_volume = driver_snd_es1370_ops_set_volume,
    .get_caps   = driver_snd_es1370_ops_get_caps,
};

static inline void es_outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline u32   es_inl(u16 port)        { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static void driver_snd_es1370_log(const char *s) { screen_console_puts(s); }

/* PCLKDIV for the closest achievable rate; returns the actual rate */
static u32 es_rate_to_pclkdiv(u32 rate, u32 *pclkdiv_out) {
    /* div = round(1411200/rate) - 2, clamped */
    u32 div = (1411200u + rate / 2) / rate;
    if (div < 2) div = 2;
    div -= 2;
    if (div > 0x1fff) div = 0x1fff;
    *pclkdiv_out = div;
    return 1411200u / (div + 2);
}

/* ---- IRQ ---- */
static void driver_snd_es1370_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    driver_snd_es1370_dev_t *d = &g_es1370;
    if (!d->up) return;

    u32 status = es_inl(d->io + ES1370_REG_STATUS);
    if (!(status & ES_STAT_DAC2)) return;

    /* clear by toggling P2INTEN (the "lower IRQ" path of the part) */
    u32 sctl = es_inl(d->io + ES1370_REG_SCTRL);
    es_outl(d->io + ES1370_REG_SCTRL, sctl & ~ES_SCTRL_P2INTEN);
    es_outl(d->io + ES1370_REG_SCTRL, sctl);
    d->irq_seen++;
    d->irq_count++;
}

/* ---- playback ---- */

static int es_play_block(driver_snd_es1370_dev_t *d, const u8 *src, u32 bytes) {
    if (bytes == 0 || bytes > ES_BLOCK_BYTES || bytes & 3) return -1;
    u32 dwords = bytes / 4;

    memcpy(d->dma, src, bytes);

    u64 before = d->irq_seen;

    /* MEMPAGE = 0xC -> window exposes DAC1/DAC2 frame registers */
    es_outl(d->io + ES1370_REG_MEMPAGE, 0x0c);
    es_outl(d->io + ES1370_REG_DAC2_FRAMEADR, (u32)d->dma_phys);
    es_outl(d->io + ES1370_REG_DAC2_FRAMECNT, dwords);   /* size, cnt=0 */
    /* SCOUNT reload: IRQ every dwords frames */
    es_outl(d->io + ES1370_REG_DAC2_SCOUNT, dwords - 1);
    /* SCTRL: 16-bit stereo, interrupt on, stop at end (no loop) */
    es_outl(d->io + ES1370_REG_SCTRL,
            ES_SCTRL_P2INTEN | ES_SCTRL_P2FMT_MSK | ES_SCTRL_P2LOOPSEL);
    /* CONTROL: enable DAC2 with the programmed clock */
    u32 ctl = es_inl(d->io + ES1370_REG_CONTROL);
    ctl = (ctl & ~ES_CTRL_PCLKDIV_MSK) | (d->pclkdiv << ES_CTRL_PCLKDIV_SH);
    es_outl(d->io + ES1370_REG_CONTROL, ctl | ES_CTRL_DAC2_EN);

    u64 deadline = core_timer_now_ms() + 2000;
    while (d->irq_seen == before) {
        if (core_timer_now_ms() > deadline) {
            driver_snd_es1370_log("es1370: block IRQ timeout\n");
            es_outl(d->io + ES1370_REG_CONTROL,
                    es_inl(d->io + ES1370_REG_CONTROL) & ~ES_CTRL_DAC2_EN);
            return -1;
        }
        core_sched_yield();
    }
    return (int)bytes;
}

static int driver_snd_es1370_ops_play(driver_snd_device_t *sdev, const void *buf, int len) {
    driver_snd_es1370_dev_t *d = (driver_snd_es1370_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;

    while (sent < len) {
        u32 chunk = (u32)(len - sent);
        if (chunk > ES_BLOCK_BYTES) chunk = ES_BLOCK_BYTES;
        chunk &= ~3u;
        if (chunk < 4) chunk = 4;
        int rc = es_play_block(d, src + sent, chunk);
        if (rc < 0) return sent ? sent : -1;
        sent += rc;
    }
    return sent;
}

static int driver_snd_es1370_ops_stop(driver_snd_device_t *sdev) {
    driver_snd_es1370_dev_t *d = (driver_snd_es1370_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    u32 ctl = es_inl(d->io + ES1370_REG_CONTROL);
    es_outl(d->io + ES1370_REG_CONTROL, ctl & ~ES_CTRL_DAC2_EN);
    sdev->irqs = d->irq_count;
    return 0;
}

static int driver_snd_es1370_ops_set_rate(driver_snd_device_t *sdev, u32 rate) {
    driver_snd_es1370_dev_t *d = (driver_snd_es1370_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (rate < 4000 || rate > 96000) return -1;
    u32 pclkdiv, actual;
    actual = es_rate_to_pclkdiv(rate, &pclkdiv);
    d->pclkdiv = pclkdiv;
    d->rate = rate;       /* requested rate is the logical state */
    sdev->rate = rate;
    (void)actual;         /* exact on 44100/22050/8000; 48000 -> 48662 */
    return 0;
}

static int driver_snd_es1370_ops_set_volume(driver_snd_device_t *sdev, u32 vol) {
    driver_snd_es1370_dev_t *d = (driver_snd_es1370_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (vol > 100) vol = 100;
    /* ES1370 codec register write: [31]=write, [22:16]=addr, [15:0]=data.
     * The AC97-style master volume register (0x02) takes 1.5 dB steps
     * where 0 = loudest. */
    u32 att = ((100 - vol) * 0x1f) / 100;
    u16 data = (u16)(att | (att << 8));
    if (vol == 0) data |= 0x8080;
    es_outl(d->io + ES1370_REG_CODEC, (1u << 31) | (0x02 << 16) | data);
    return 0;
}

static int driver_snd_es1370_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps) {
    driver_snd_es1370_dev_t *d = (driver_snd_es1370_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    caps->rates = SND_RATE_8000 | SND_RATE_22050 | SND_RATE_44100 |
                  SND_RATE_48000;
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

int driver_snd_es1370_init(driver_pci_dev_t *pdev) {
    driver_snd_es1370_dev_t *d = &g_es1370;
    if (d->up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        if ((ids & 0xffff) != 0x1274) return -1;
    } else {
        int found = driver_pci_find_device(0x1274, 0x5000, &bus, &dev, &func) == 0;
        if (!found) found = driver_pci_find_device(0x1274, 0x1371, &bus, &dev, &func) == 0;
        if (!found) return -1;
    }

    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    d->vid = (u16)(ids & 0xffff);
    d->did = (u16)(ids >> 16);
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = driver_pci_read_config(bus, dev, func, 0x10);
    if (!(bar0 & 1)) return -1;
    d->io = (u16)(bar0 & 0xFFFC);
    if (!d->io) return -1;
    driver_pci_enable_device(bus, dev, func);

    d->dma_phys = mem_pmm_alloc_contig(ES_BLOCK_BYTES / PMM_PAGE_SIZE + 1);
    if (!d->dma_phys) return -1;
    d->dma = (u8 *)(uintptr_t)d->dma_phys;
    memset(d->dma, 0, ES_BLOCK_BYTES);

    /* IRQ from the PCI interrupt line */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    d->irq = -1;
    if (irq < 16 && arch_irq_register_handler(irq, driver_snd_es1370_irq_handler, NULL) == 0)
        d->irq = irq;

    /* reset-ish: codec interface on, DACs off */
    u32 ctl = es_inl(d->io + ES1370_REG_CONTROL);
    es_outl(d->io + ES1370_REG_CONTROL, ctl & ~ES_CTRL_DAC2_EN);

    es_rate_to_pclkdiv(44100, &d->pclkdiv);
    d->rate = 44100;
    d->channels = 2;
    d->up = 1;

    char line[96];
    char n[8];
    strcpy(line, "es1370: 1274:");
    u64_to_hex(d->did, n, 4); strcat(line, n);
    strcat(line, " io=0x");
    u64_to_hex(d->io, n, 4); strcat(line, n);
    strcat(line, " irq ");
    u64_to_hex((u64)(d->irq & 0xff), n, 2); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");

    driver_snd_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "es1370");
    nd.type = SND_TYPE_ES1370;
    nd.bus = d->bus; nd.dev = d->dev; nd.func = d->func;
    nd.vendor_id = d->vid; nd.device_id = d->did;
    nd.rate = d->rate; nd.channels = d->channels; nd.volume = 100;
    nd.priv = d;
    int idx = driver_snd_register(&nd, &driver_snd_es1370_snd_ops);
    return idx >= 0 ? 0 : -1;
}

void driver_snd_es1370_print_state(void) {
    driver_snd_es1370_dev_t *d = &g_es1370;
    char line[128];
    char n[24];

    screen_console_puts("ES1370: ");
    if (!d->up) {
        screen_console_puts("not present\n");
        return;
    }
    strcpy(line, "vid=0x");
    u64_to_hex(d->vid, n, 4); strcat(line, n);
    strcat(line, " did=0x");
    u64_to_hex(d->did, n, 4); strcat(line, n);
    strcat(line, " io=0x");
    u64_to_hex(d->io, n, 4); strcat(line, n);
    strcat(line, " irq=");
    u64_to_str(d->irq, n); strcat(line, n);
    strcat(line, " rate=");
    u64_to_str(d->rate, n); strcat(line, n);
    strcat(line, " pclkdiv=");
    u64_to_str(d->pclkdiv, n); strcat(line, n);
    strcat(line, " irqs=");
    u64_to_str(d->irq_count, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
}
