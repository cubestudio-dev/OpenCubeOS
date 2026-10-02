/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/ac97.c
 * Purpose: Intel 82801AA/AB AC'97 audio controller driver.
 *
 * The legacy mainstream PCI sound card: Intel AC'97 (8086:2415 i82801AA,
 * 8086:2445 i82801AB, plus common OEM ids).  QEMU exposes it as
 * `-device AC97` with the same register layout.
 *
 * Register layout (per the Intel 82801AA datasheet, verified against
 * QEMU hw/audio/ac97):
 *
 *   BAR0 = NAM  (Native Audio Mixer, I/O): codec registers
 *           0x00 reset, 0x02 master volume, 0x18 PCM out volume,
 *           0x26 powerdown, 0x28 extended audio id, 0x2C PCM front
 *           DAC rate, 0x7C/0x7E vendor id.
 *   BAR1 = NABM (Native Audio Bus Mastering, I/O): DMA engines
 *           slot 0 PI @ 0x00, slot 1 PO @ 0x10, slot 2 MC @ 0x20,
 *           GLOB_CNT 0x2C, GLOB_STA 0x30, CAS 0x34.
 *           Per slot: BDBAR +0, CIV +4, LVI +5, SR +6, PICB +8,
 *           PIV +A, CR +B.
 *
 * PCM out (slot 1) programming:
 *   - sample rate: NAM 0x2C (0xBB80 = 48000, 0xAC44 = 44100)
 *   - BDL: 32 entries x 1 KiB buffers from one PMM block; each entry
 *     is {addr, len<<16 | IOC<<31} where len counts 16-bit samples
 *     (a stereo frame = 2 samples = 4 bytes).
 *   - CR = RPBM (run) | IOCE (completion IRQ on each buffer).
 *   - Position tracking: CIV * entry_bytes + (entry_samples - PICB)*2.
 *   - IRQ: PCI interrupt line -> oc_irq framework; the handler clears
 *     SR.BCIS/LVBCI (write-1) and counts completions.
 */
#include "ac97.h"
#include "pci.h"
#include "pmm.h"
#include "irq.h"
#include "console.h"
#include "string.h"
#include "snd.h"
#include "sched.h"
#include "timer.h"

/* NAM (mixer) registers */
#define AC97_REG_RESET          0x00
#define AC97_REG_MASTER_VOL     0x02
#define AC97_REG_PCM_OUT_VOL    0x18
#define AC97_REG_POWERDOWN      0x26
#define AC97_REG_EXT_AUDIO_ID   0x28
#define AC97_REG_PCM_FRONT_RATE 0x2c
#define AC97_REG_VENDOR_ID1     0x7c
#define AC97_REG_VENDOR_ID2     0x7e

/* NABM (bus master) offsets */
#define AC97_PO_BASE            0x10
#define AC97_BDBAR              0x00
#define AC97_CIV                0x04
#define AC97_LVI                0x05
#define AC97_SR                 0x06
#define AC97_PICB               0x08
#define AC97_CR                 0x0b
#define AC97_GLOB_CNT           0x2c
#define AC97_GLOB_STA           0x30

/* SR / CR bits */
#define AC97_SR_DCH             0x01
#define AC97_SR_CELV            0x02
#define AC97_SR_LVBCI           0x04
#define AC97_SR_BCIS            0x08
#define AC97_SR_FIFOE           0x10
#define AC97_CR_IOCE            0x10
#define AC97_CR_RR              0x02
#define AC97_CR_RPBM            0x01

/* GLOB_STA bits */
#define AC97_GS_POINT           (1u << 6)   /* PCM out interrupt */

/* BDL geometry */
#define AC97_BDL_ENTRIES        32
#define AC97_ENTRY_BYTES        1024        /* 256 samples = 128 frames */
#define AC97_ENTRY_SAMPLES      (AC97_ENTRY_BYTES / 2)
#define AC97_RING_BYTES         (AC97_BDL_ENTRIES * AC97_ENTRY_BYTES)

typedef struct ac97_dev {
    u16 nam;          /* mixer I/O base (BAR0) */
    u16 nabm;         /* bus master I/O base (BAR1) */
    u8  bus, dev, func;
    u16 vid, did;
    u16 codec_id1, codec_id2;

    u64 bdl_phys;     /* 32 entries x 8 bytes */
    u64 dma_phys;     /* 32 KiB contiguous PCM ring */
    u8 *dma;

    int running;
    u64 sw_pos;
    u32 rate;
    u8  channels;

    int irq;
    u64 irq_count;
    int up;
} ac97_dev_t;

static ac97_dev_t g_ac97;

/* forward decls */
static int ac97_ops_play(snd_device_t *sdev, const void *buf, int len);
static int ac97_ops_stop(snd_device_t *sdev);
static int ac97_ops_set_rate(snd_device_t *sdev, u32 rate);
static int ac97_ops_set_volume(snd_device_t *sdev, u32 vol);
static int ac97_ops_get_caps(snd_device_t *sdev, snd_caps_t *caps);
static const snd_ops_t ac97_snd_ops = {
    .play       = ac97_ops_play,
    .stop       = ac97_ops_stop,
    .set_rate   = ac97_ops_set_rate,
    .set_volume = ac97_ops_set_volume,
    .get_caps   = ac97_ops_get_caps,
};

/* ---- I/O accessors ---- */
static inline void ac97_outb(u16 port, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v),  "Nd"(port)); }
static inline void ac97_outw(u16 port, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v),  "Nd"(port)); }
static inline void ac97_outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v),  "Nd"(port)); }
static inline u8   ac97_inb(u16 port)  { u8 v;  __asm__ volatile("inb %1, %0"  : "=a"(v) : "Nd"(port)); return v; }
static inline u16  ac97_inw(u16 port)  { u16 v; __asm__ volatile("inw %1, %0"  : "=a"(v) : "Nd"(port)); return v; }
static inline u32  ac97_inl(u16 port)  { u32 v; __asm__ volatile("inl %1, %0"  : "=a"(v) : "Nd"(port)); return v; }

static void ac97_log(const char *s) { oc_console_puts(s); }

/* ---- codec (mixer) register access ---- */

static u16 ac97_codec_read(ac97_dev_t *d, u8 reg) {
    /* CAS (codec access semaphore) must be clear before a read */
    for (int t = 0; t < 100000; t++) {
        if (!(ac97_inb(d->nabm + 0x34) & 0x01)) break;
    }
    return ac97_inw(d->nam + reg);
}

static void ac97_codec_write(ac97_dev_t *d, u8 reg, u16 val) {
    ac97_outw(d->nam + reg, val);
}

/* ---- IRQ ---- */
static void ac97_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    ac97_dev_t *d = &g_ac97;
    if (!d->up) return;

    u32 sta = ac97_inl(d->nabm + AC97_GLOB_STA);
    if (!(sta & AC97_GS_POINT)) return;

    u16 sr = ac97_inw(d->nabm + AC97_PO_BASE + AC97_SR);
    if (sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)) {
        ac97_outw(d->nabm + AC97_PO_BASE + AC97_SR,
                  sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE));
        d->irq_count++;
    }
}

/* ---- init ---- */

static int ac97_setup(ac97_dev_t *d, u8 bus, u8 dev, u8 func) {
    u32 ids = pci_read_config(bus, dev, func, 0x00);
    d->vid = (u16)(ids & 0xffff);
    d->did = (u16)(ids >> 16);
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = pci_read_config(bus, dev, func, 0x10);
    u32 bar1 = pci_read_config(bus, dev, func, 0x14);
    if (!(bar0 & 1) || !(bar1 & 1)) return -1;   /* both are I/O BARs */
    d->nam  = (u16)(bar0 & 0xFFFC);
    d->nabm = (u16)(bar1 & 0xFFFC);
    if (!d->nam || !d->nabm) return -1;
    pci_enable_device(bus, dev, func);

    /* codec signature (sanity: must not be all-ones) */
    d->codec_id1 = ac97_codec_read(d, AC97_REG_VENDOR_ID1);
    d->codec_id2 = ac97_codec_read(d, AC97_REG_VENDOR_ID2);
    if (d->codec_id1 == 0xFFFF && d->codec_id2 == 0xFFFF) {
        ac97_log("ac97: primary codec not present\n");
        return -1;
    }

    /* reset the codec (register 0x00 write), then set the sample rate */
    ac97_codec_write(d, AC97_REG_RESET, 0x0000);
    for (int t = 0; t < 100000; t++) { }
    ac97_codec_write(d, AC97_REG_PCM_FRONT_RATE, 48000);
    /* PCM out volume: 0 dB, unmuted */
    ac97_codec_write(d, AC97_REG_PCM_OUT_VOL, 0x0000);
    ac97_codec_write(d, AC97_REG_MASTER_VOL, 0x0000);

    /* BDL: 32 entries x 8 bytes; one PMM frame is enough */
    u64 bdl = pmm_alloc_frame();
    d->dma_phys = pmm_alloc_contig(AC97_RING_BYTES / PMM_PAGE_SIZE);
    if (!bdl || !d->dma_phys) return -1;
    d->bdl_phys = bdl;
    d->dma = (u8 *)(uintptr_t)d->dma_phys;
    oc_memset(d->dma, 0, AC97_RING_BYTES);

    volatile u32 *b32 = (volatile u32 *)(uintptr_t)bdl;
    for (int i = 0; i < AC97_BDL_ENTRIES; i++) {
        u32 addr = (u32)(d->dma_phys + (u64)i * AC97_ENTRY_BYTES);
        /* len = sample count (16-bit units); stereo frame = 2 samples */
        u32 ctl = (u32)(AC97_ENTRY_SAMPLES & 0xffff) | (1u << 31) /* IOC */;
        b32[i * 2 + 0] = addr & ~3u;
        b32[i * 2 + 1] = ctl;
    }

    /* program the PCM out slot: BDL base, last index, then run */
    ac97_outl(d->nabm + AC97_PO_BASE + AC97_BDBAR, (u32)bdl);
    ac97_outb(d->nabm + AC97_PO_BASE + AC97_LVI, AC97_BDL_ENTRIES - 1);
    ac97_outw(d->nabm + AC97_PO_BASE + AC97_SR,
              AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE);

    /* IRQ from the PCI interrupt line */
    u32 icfg = pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    d->irq = -1;
    if (irq < 16 && oc_irq_register_handler(irq, ac97_irq_handler, NULL) == 0)
        d->irq = irq;

    d->rate = 48000;
    d->channels = 2;
    d->sw_pos = 0;
    d->running = 0;
    d->up = 1;
    return 0;
}

int ac97_init(pci_dev_t *pdev) {
    ac97_dev_t *d = &g_ac97;
    if (d->up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = pci_read_config(bus, dev, func, 0x00);
        if ((ids & 0xffff) != 0x8086) return -1;
    } else {
        int found = pci_find_device(0x8086, 0x2415, &bus, &dev, &func) == 0;
        if (!found) found = pci_find_device(0x8086, 0x2445, &bus, &dev, &func) == 0;
        if (!found) found = pci_find_device(0x8086, 0x2485, &bus, &dev, &func) == 0;
        if (!found) found = pci_find_device(0x8086, 0x24C5, &bus, &dev, &func) == 0;
        if (!found) found = pci_find_device(0x8086, 0x24D5, &bus, &dev, &func) == 0;
        if (!found) return -1;
    }

    if (ac97_setup(d, bus, dev, func) != 0) {
        d->up = 0;
        return -1;
    }

    char line[128];
    char n[8];
    oc_strcpy(line, "ac97: 8086:");
    oc_u64_to_hex(d->did, n, 4); oc_strcat(line, n);
    oc_strcat(line, " codec ");
    oc_u64_to_hex(d->codec_id1, n, 4); oc_strcat(line, n);
    oc_strcat(line, ":");
    oc_u64_to_hex(d->codec_id2, n, 4); oc_strcat(line, n);
    oc_strcat(line, " nam=0x");
    oc_u64_to_hex(d->nam, n, 4); oc_strcat(line, n);
    oc_strcat(line, " nabm=0x");
    oc_u64_to_hex(d->nabm, n, 4); oc_strcat(line, n);
    oc_strcat(line, " irq ");
    oc_u64_to_hex((u64)(d->irq & 0xff), n, 2); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");

    snd_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strcpy(nd.name, "ac97");
    nd.type = SND_TYPE_AC97;
    nd.bus = d->bus; nd.dev = d->dev; nd.func = d->func;
    nd.vendor_id = d->vid; nd.device_id = d->did;
    nd.rate = d->rate; nd.channels = d->channels; nd.volume = 100;
    nd.priv = d;
    int idx = snd_register(&nd, &ac97_snd_ops);
    return idx >= 0 ? 0 : -1;
}

/* ---- playback ---- */

/* Consumed bytes, derived from CIV (current entry) and PICB (remaining
 * samples in the current entry). */
static u32 ac97_hw_pos(ac97_dev_t *d) {
    u8 civ = ac97_inb(d->nabm + AC97_PO_BASE + AC97_CIV);
    u16 picb = ac97_inw(d->nabm + AC97_PO_BASE + AC97_PICB);
    u32 consumed_in_entry = (u32)(AC97_ENTRY_SAMPLES - picb) * 2;
    return civ * AC97_ENTRY_BYTES + consumed_in_entry;
}

static void ac97_stream_start(ac97_dev_t *d) {
    ac97_outw(d->nabm + AC97_PO_BASE + AC97_SR,
              AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE);
    ac97_outb(d->nabm + AC97_PO_BASE + AC97_CR, AC97_CR_IOCE | AC97_CR_RPBM);
    d->running = 1;
}

static int ac97_ops_play(snd_device_t *sdev, const void *buf, int len) {
    ac97_dev_t *d = (ac97_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;

    if (!d->running) ac97_stream_start(d);

    while (sent < len) {
        u32 hw = ac97_hw_pos(d);
        u64 sw = d->sw_pos;
        u32 inflight = (u32)((sw - hw) & (AC97_RING_BYTES - 1));
        u32 free = AC97_RING_BYTES - 1 - inflight;
        if (free == 0) { sched_yield(); continue; }

        u32 ring_off = (u32)(sw & (AC97_RING_BYTES - 1));
        u32 chunk = (u32)(len - sent);
        if (chunk > free) chunk = free;
        if (chunk > AC97_RING_BYTES - ring_off)
            chunk = AC97_RING_BYTES - ring_off;
        if (chunk == 0) { sched_yield(); continue; }

        oc_memcpy(d->dma + ring_off, src + sent, chunk);
        d->sw_pos += chunk;
        sent += (int)chunk;
    }
    return sent;
}

static int ac97_ops_stop(snd_device_t *sdev) {
    ac97_dev_t *d = (ac97_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;

    u64 deadline = oc_timer_now_ms() + 2000;
    while (d->running && oc_timer_now_ms() < deadline) {
        u32 hw = ac97_hw_pos(d);
        if (((d->sw_pos - hw) & (AC97_RING_BYTES - 1)) < 64) break;
        sched_yield();
    }
    ac97_outb(d->nabm + AC97_PO_BASE + AC97_CR, AC97_CR_IOCE);
    for (int t = 0; t < 100000; t++) {
        if (ac97_inw(d->nabm + AC97_PO_BASE + AC97_SR) & AC97_SR_DCH) break;
    }
    d->running = 0;
    d->sw_pos = 0;
    sdev->irqs = d->irq_count;
    return 0;
}

static int ac97_ops_set_rate(snd_device_t *sdev, u32 rate) {
    ac97_dev_t *d = (ac97_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (rate != 48000 && rate != 44100 && rate != 22050 && rate != 8000)
        return -1;
    if (d->running) return -1;
    ac97_codec_write(d, AC97_REG_PCM_FRONT_RATE, (u16)rate);
    d->rate = rate;
    return 0;
}

static int ac97_ops_set_volume(snd_device_t *sdev, u32 vol) {
    ac97_dev_t *d = (ac97_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (vol > 100) vol = 100;
    /* AC'97 volume: 0 = loudest, 0x1f steps of 1.5 dB, bit15 = mute.
     * Map 0..100 to the attenuation register value. */
    u32 att = ((100 - vol) * 0x1f) / 100;
    u16 v = (u16)(att | (att << 8));
    if (vol == 0) v |= 0x8080;
    ac97_codec_write(d, AC97_REG_PCM_OUT_VOL, v);
    return 0;
}

static int ac97_ops_get_caps(snd_device_t *sdev, snd_caps_t *caps) {
    ac97_dev_t *d = (ac97_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    caps->rates = SND_RATE_8000 | SND_RATE_22050 | SND_RATE_44100 |
                  SND_RATE_48000;
    caps->min_channels = 1;
    caps->max_channels = 2;
    caps->bits8 = 0;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->cur_rate = d->rate;
    caps->cur_volume = sdev->volume;
    return 0;
}

void ac97_print_state(void) {
    ac97_dev_t *d = &g_ac97;
    char line[128];
    char n[24];

    oc_console_puts("AC'97: ");
    if (!d->up) {
        oc_console_puts("not present\n");
        return;
    }
    oc_strcpy(line, "vid=0x");
    oc_u64_to_hex(d->vid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " did=0x");
    oc_u64_to_hex(d->did, n, 4); oc_strcat(line, n);
    oc_strcat(line, " codec=");
    oc_u64_to_hex(d->codec_id1, n, 4); oc_strcat(line, n);
    oc_strcat(line, ":");
    oc_u64_to_hex(d->codec_id2, n, 4); oc_strcat(line, n);
    oc_console_puts(line);

    u16 sr = ac97_inw(d->nabm + AC97_PO_BASE + AC97_SR);
    u8 civ = ac97_inb(d->nabm + AC97_PO_BASE + AC97_CIV);
    oc_strcpy(line, " rate=");
    oc_u64_to_str(d->rate, n); oc_strcat(line, n);
    oc_strcat(line, " stream=");
    oc_strcat(line, d->running ? "running" : "stopped");
    oc_strcat(line, " civ=");
    oc_u64_to_str(civ, n); oc_strcat(line, n);
    oc_strcat(line, " sr=0x");
    oc_u64_to_hex(sr, n, 2); oc_strcat(line, n);
    oc_strcat(line, " irqs=");
    oc_u64_to_str(d->irq_count, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");
}
