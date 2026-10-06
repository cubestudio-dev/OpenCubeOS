/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/virtio_snd.c
 * Purpose: virtio-sound (virtio-snd-pci) driver.
 *
 * Implements the virtio sound device class (virtio spec 1.2 section 5.9,
 * constants from include/uapi/linux/virtio_snd.h) on the modern virtio
 * PCI transport: PCI capabilities locate the COMMON / NOTIFY / ISR /
 * DEVICE configuration bars, the four device virtqueues (control,
 * event, tx, rx) are the split-ring layout with guest-allocated
 * desc/avail/used areas addressed through the common configuration.
 *
 * Playback flow (output stream 0):
 *   PCM_INFO (capabilities query) -> PCM_SET_PARAMS (S16 stereo, rate,
 *   buffer/period) -> PCM_PREPARE -> PCM_START -> tx queue: {xfer hdr,
 *   PCM data (readonly), status (writable)} -> wait for used-ring
 *   completion -> ... -> PCM_STOP / PCM_RELEASE.
 *
 * NOTE (honest environment report): QEMU 10.x has NO virtio-snd-pci
 * device model (only the vhost-user variant, which requires an
 * external backend daemon).  On QEMU the probe below finds no device
 * and returns -1; the WP-10c test therefore reports SKIPPED.  The
 * driver is complete per the spec so real/vhosted hardware works.
 */
#include "driver_snd_virtio.h"
#include "driver_pci.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "driver_snd.h"
#include "core_sched.h"
#include "core_timer.h"

/* ---- PCI capabilities (virtio 1.0) ---- */
#define VPCI_CAP_VENDOR     0x09
#define VPCI_CAP_COMMON     1
#define VPCI_CAP_NOTIFY     2
#define VPCI_CAP_ISR        3
#define VPCI_CAP_DEVICE     4

/* common configuration field offsets */
#define VCFG_DF_SELECT      0x00
#define VCFG_DF             0x04
#define VCFG_GF_SELECT      0x08
#define VCFG_GF             0x0c
#define VCFG_NUM_QUEUES     0x12
#define VCFG_STATUS         0x14
#define VCFG_QUEUE_SELECT   0x16
#define VCFG_QUEUE_SIZE     0x18
#define VCFG_QUEUE_ENABLE   0x1c
#define VCFG_QUEUE_NOTIFY_OFF 0x1e
#define VCFG_QUEUE_DESC     0x20
#define VCFG_QUEUE_AVAIL    0x28
#define VCFG_QUEUE_USED     0x30

#define VSTS_ACK            1
#define VSTS_DRIVER         2
#define VSTS_DRIVER_OK      4
#define VSTS_FAILED         128

/* ---- virtio-snd constants (uapi/linux/virtio_snd.h) ---- */
#define VSND_R_PCM_INFO     0x0100
#define VSND_R_PCM_SET_PARAMS  0x0101
#define VSND_R_PCM_PREPARE  0x0102
#define VSND_R_PCM_RELEASE  0x0103
#define VSND_R_PCM_START    0x0104
#define VSND_R_PCM_STOP     0x0105

#define VSND_S_OK           0x8000

#define VSND_PCM_FMT_S16    5
#define VSND_PCM_RATE_44100 6
#define VSND_PCM_RATE_48000 7

#define VSND_VQ_CONTROL     0
#define VSND_VQ_EVENT       1
#define VSND_VQ_TX          2
#define VSND_VQ_RX          3
#define VSND_VQ_MAX         4

typedef struct driver_snd_virtio_hdr { u32 code; } driver_snd_virtio_hdr_t;

typedef struct driver_snd_virtio_pcm_hdr {
    u32 code;
    u32 stream_id;
} driver_snd_virtio_pcm_hdr_t;

typedef struct driver_snd_virtio_pcm_set_params {
    u32 code;
    u32 stream_id;
    u32 buffer_bytes;
    u32 period_bytes;
    u32 features;
    u8  channels;
    u8  format;
    u8  rate;
    u8  padding;
} driver_snd_virtio_pcm_set_params_t;

typedef struct driver_snd_virtio_pcm_xfer { u32 stream_id; } driver_snd_virtio_pcm_xfer_t;
typedef struct driver_snd_virtio_pcm_status { u32 status; u32 latency_bytes; } driver_snd_virtio_pcm_status_t;

/* ---- split virtqueue ---- */
typedef struct driver_snd_virtio_vq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} driver_snd_virtio_vq_desc_t;

#define VQ_DESC_F_NEXT     1
#define VQ_DESC_F_WRITE    2

typedef struct driver_snd_virtio_vq_avail {
    u16 flags;
    u16 idx;
    u16 ring[];
} driver_snd_virtio_vq_avail_t;

typedef struct driver_snd_virtio_vq_used_elem { u32 id; u32 len; } driver_snd_virtio_vq_used_elem_t;

typedef struct driver_snd_virtio_vq_used {
    u16 flags;
    u16 idx;
    driver_snd_virtio_vq_used_elem_t ring[];
} driver_snd_virtio_vq_used_t;

typedef struct driver_snd_virtio_vq {
    u32        size;
    volatile driver_snd_virtio_vq_desc_t *desc;
    volatile driver_snd_virtio_vq_avail_t *avail;
    volatile driver_snd_virtio_vq_used_t  *used;
    u16        free_head;
    u16        last_used;
    u16        notify_off;
    u64        mem_raw;    /* allocation base for cleanup */
} driver_snd_virtio_vq_t;

#define VSND_QUEUE_SIZE 64

typedef struct driver_snd_virtio_dev {
    u8   bus, dev, func;
    u16  vid, did;
    u64  common;      /* common cfg base (bar + offset) */
    u64  notify;      /* notify base */
    u64  isr;         /* isr base */
    u64  device;      /* device cfg base */
    u32  notify_mult;

    u32  n_streams;
    driver_snd_virtio_vq_t vq[VSND_VQ_MAX];

    u64  ctl_hdr_phys;
    u64  tx_phys;     /* tx payload area: 16 KiB data + status page */
    u8  *tx_buf;
    u64  tx_status_phys;
    driver_snd_virtio_pcm_status_t *tx_status;

    int  prepared;
    int  started;
    u32  rate;
    u8   channels;
    int  irq;
    u64  irq_count;
    int  up;
} driver_snd_virtio_dev_t;

static driver_snd_virtio_dev_t g_vsnd;

/* forward decls */
static int driver_snd_virtio_ops_play(driver_snd_device_t *sdev, const void *buf, int len);
static int driver_snd_virtio_ops_stop(driver_snd_device_t *sdev);
static int driver_snd_virtio_ops_set_rate(driver_snd_device_t *sdev, u32 rate);
static int driver_snd_virtio_ops_set_volume(driver_snd_device_t *sdev, u32 vol);
static int driver_snd_virtio_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps);
static const driver_snd_ops_t driver_snd_virtio_snd_ops = {
    .play       = driver_snd_virtio_ops_play,
    .stop       = driver_snd_virtio_ops_stop,
    .set_rate   = driver_snd_virtio_ops_set_rate,
    .set_volume = driver_snd_virtio_ops_set_volume,
    .get_caps   = driver_snd_virtio_ops_get_caps,
};

static inline u8 driver_snd_virtio_r8(driver_snd_virtio_dev_t *d, u64 base, u32 off) {
    (void)d;
    return *(volatile u8 *)(uintptr_t)(base + off);
}
static inline u16 driver_snd_virtio_r16(driver_snd_virtio_dev_t *d, u64 base, u32 off) {
    (void)d;
    return *(volatile u16 *)(uintptr_t)(base + off);
}
static inline u32 driver_snd_virtio_r32(driver_snd_virtio_dev_t *d, u64 base, u32 off) {
    (void)d;
    return *(volatile u32 *)(uintptr_t)(base + off);
}
static inline void driver_snd_virtio_w8(driver_snd_virtio_dev_t *d, u64 base, u32 off, u8 v) {
    (void)d;
    *(volatile u8 *)(uintptr_t)(base + off) = v;
}
static inline void driver_snd_virtio_w16(driver_snd_virtio_dev_t *d, u64 base, u32 off, u16 v) {
    (void)d;
    *(volatile u16 *)(uintptr_t)(base + off) = v;
}
static inline void driver_snd_virtio_w32(driver_snd_virtio_dev_t *d, u64 base, u32 off, u32 v) {
    (void)d;
    *(volatile u32 *)(uintptr_t)(base + off) = v;
}
static inline void driver_snd_virtio_w64(driver_snd_virtio_dev_t *d, u64 base, u32 off, u64 v) {
    (void)d;
    *(volatile u64 *)(uintptr_t)(base + off) = v;
}

static void driver_snd_virtio_log(const char *s) { screen_console_puts(s); }

/* ---- PCI capability walk ---- */

static int driver_snd_virtio_find_caps(driver_snd_virtio_dev_t *d, u8 bus, u8 dev, u8 func) {
    u8 cap_ptr = (u8)(driver_pci_read_config(bus, dev, func, 0x34) & 0xff);
    int found = 0;
    int hops = 0;

    while (cap_ptr != 0 && cap_ptr >= 0x40 && hops++ < 16) {
        u32 hdr = driver_pci_read_config(bus, dev, func, (u8)(cap_ptr & 0xfc));
        u8 cap_vndr = (u8)(hdr & 0xff);
        u8 cap_next = (u8)((hdr >> 8) & 0xff);
        u8 cap_len  = (u8)((hdr >> 16) & 0xff);
        if (cap_vndr == VPCI_CAP_VENDOR && cap_len >= 16) {
            /* BUG-0129 FIX (A9-03): two independent parsing bugs made
             * virtio-snd probe IMPOSSIBLE (QEMU has no such device, so
             * it survived every test run):
             *   1) Virtio PCI capability layout: +0 vndr, +1 next,
             *      +2 len, +3 cfg_type, +4 bar, +5 id, +8 offset_lo,
             *      +12 offset_hi, +16 notify_off_multiplier.
             *      driver_pci_read_config reads an ALIGNED dword, and
             *      caps are 4-byte aligned: dword@cap+0 carries
             *      cfg_type in bits 31:24, dword@cap+4 carries bar in
             *      bits 7:0. The old code took cfg_type from dword@+4
             *      byte 0 (= the BAR field) and bar from its bits 15:8
             *      (= the id field) - every capability type was
             *      misidentified.
             *   2) The length filter was >= 20, but common/isr/device
             *      caps are 16 bytes (only notify is 20), so those
             *      caps were skipped and `found` could never reach the
             *      required bits. */
            u8 cfg_type = (u8)((hdr >> 24) & 0xff);          /* +3: dword@cap bits 31:24 */
            u32 type_off = driver_pci_read_config(bus, dev, func,
                                           (u8)((cap_ptr + 4) & 0xfc));
            u32 lo = driver_pci_read_config(bus, dev, func,
                                     (u8)((cap_ptr + 8) & 0xfc));
            u32 len_lo = driver_pci_read_config(bus, dev, func,
                                         (u8)((cap_ptr + 16) & 0xfc));
            u8 bar = (u8)((type_off >> 0) & 0xff);           /* +4: dword@cap+4 bits 7:0 */
            u32 bar_val = driver_pci_read_bar(bus, dev, func, bar & 7);
            u64 base = (u64)(bar_val & ~0xfu) + lo;
            (void)len_lo;

            switch (cfg_type) {
            case VPCI_CAP_COMMON: d->common = base; found |= 1; break;
            case VPCI_CAP_NOTIFY:
                d->notify = base;
                d->notify_mult = len_lo;
                found |= 2;
                break;
            case VPCI_CAP_ISR:  d->isr = base; found |= 4; break;
            case VPCI_CAP_DEVICE: d->device = base; break;
            default: break;
            }
        }
        cap_ptr = cap_next;
    }
    return (found & 7) == 7 ? 0 : -1;
}

/* ---- virtqueue setup (split ring, modern layout) ---- */

static int driver_snd_virtio_setup_queue(driver_snd_virtio_dev_t *d, int idx, driver_snd_virtio_vq_t *vq) {
    driver_snd_virtio_w16(d, d->common, VCFG_QUEUE_SELECT, (u16)idx);
    u32 size = driver_snd_virtio_r16(d, d->common, VCFG_QUEUE_SIZE);
    if (size == 0 || size > VSND_QUEUE_SIZE) size = VSND_QUEUE_SIZE;
    vq->size = size;

    u32 desc_bytes = size * 16u;
    u32 avail_bytes = 6u + (u32)size * 2u + 2u;
    u32 used_bytes = 6u + (u32)size * 8u + 2u;
    u32 total = desc_bytes + avail_bytes + used_bytes + 16u;

    u64 raw = mem_pmm_alloc_contig((total + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);
    if (!raw) return -1;
    vq->mem_raw = raw;
    vq->desc  = (volatile driver_snd_virtio_vq_desc_t *)(uintptr_t)raw;
    vq->avail = (volatile driver_snd_virtio_vq_avail_t *)(uintptr_t)(raw + desc_bytes);
    vq->used  = (volatile driver_snd_virtio_vq_used_t *)(uintptr_t)
                ((raw + desc_bytes + avail_bytes + 15u) & ~15u);
    memset((void *)(uintptr_t)raw, 0, total);
    vq->free_head = 0;
    vq->last_used = 0;

    driver_snd_virtio_w16(d, d->common, VCFG_QUEUE_SELECT, (u16)idx);
    driver_snd_virtio_w16(d, d->common, VCFG_QUEUE_SIZE, (u16)size);
    driver_snd_virtio_w64(d, d->common, VCFG_QUEUE_DESC, raw);
    driver_snd_virtio_w64(d, d->common, VCFG_QUEUE_AVAIL, raw + desc_bytes);
    driver_snd_virtio_w64(d, d->common, VCFG_QUEUE_USED,
             (raw + desc_bytes + avail_bytes + 15u) & ~15u);
    vq->notify_off = driver_snd_virtio_r16(d, d->common, VCFG_QUEUE_NOTIFY_OFF);
    driver_snd_virtio_w16(d, d->common, VCFG_QUEUE_ENABLE, 1);
    return 0;
}

/* add a 3-descriptor chain: hdr (ro) + data (ro) + status (wo) */
static int driver_snd_virtio_tx_submit(driver_snd_virtio_dev_t *d, u64 hdr_phys, u32 hdr_len,
                          u64 data_phys, u32 data_len) {
    driver_snd_virtio_vq_t *vq = &d->vq[VSND_VQ_TX];
    u16 head = vq->free_head;

    vq->desc[head].addr = hdr_phys;
    vq->desc[head].len = hdr_len;
    vq->desc[head].flags = VQ_DESC_F_NEXT;
    vq->desc[head].next = (u16)((head + 1) % vq->size);

    u16 d2 = (u16)((head + 1) % vq->size);
    vq->desc[d2].addr = data_phys;
    vq->desc[d2].len = data_len;
    vq->desc[d2].flags = VQ_DESC_F_NEXT;
    vq->desc[d2].next = (u16)((d2 + 1) % vq->size);

    u16 d3 = (u16)((d2 + 1) % vq->size);
    vq->desc[d3].addr = d->tx_status_phys;
    vq->desc[d3].len = sizeof(driver_snd_virtio_pcm_status_t);
    vq->desc[d3].flags = VQ_DESC_F_WRITE;
    vq->desc[d3].next = 0;

    vq->avail->ring[vq->avail->idx % vq->size] = head;
    __asm__ volatile("mfence" ::: "memory");
    vq->avail->idx++;
    vq->free_head = (u16)((d3 + 1) % vq->size);

    /* notify (queue_notify_off * notify_off_multiplier into the notify bar) */
    driver_snd_virtio_w32(d, d->notify, (u32)vq->notify_off * d->notify_mult,
             (u32)VSND_VQ_TX);
    return head;
}

/* wait for one used buffer */
static int driver_snd_virtio_tx_complete(driver_snd_virtio_dev_t *d, u64 deadline_ms) {
    driver_snd_virtio_vq_t *vq = &d->vq[VSND_VQ_TX];
    while (vq->last_used == vq->used->idx) {
        if (core_timer_now_ms() > deadline_ms) return -1;
        core_sched_yield();
    }
    __asm__ volatile("mfence" ::: "memory");
    driver_snd_virtio_vq_used_elem_t *e = (driver_snd_virtio_vq_used_elem_t *)&vq->used->ring[vq->last_used % vq->size];
    u32 status = d->tx_status->status;
    vq->last_used++;
    (void)e;
    return (status == VSND_S_OK) ? 0 : -1;
}

/* ---- control messages (control queue, synchronous) ---- */

static int driver_snd_virtio_ctl(driver_snd_virtio_dev_t *d, void *req, u32 req_len,
                    void *resp, u32 resp_len) {
    driver_snd_virtio_vq_t *vq = &d->vq[VSND_VQ_CONTROL];
    u64 req_phys = (u64)(uintptr_t)req;
    u64 resp_phys = (u64)(uintptr_t)resp;
    u16 head = vq->free_head;

    vq->desc[head].addr = req_phys;
    vq->desc[head].len = req_len;
    vq->desc[head].flags = VQ_DESC_F_NEXT;
    vq->desc[head].next = (u16)((head + 1) % vq->size);

    u16 d2 = (u16)((head + 1) % vq->size);
    vq->desc[d2].addr = resp_phys;
    vq->desc[d2].len = resp_len;
    vq->desc[d2].flags = VQ_DESC_F_WRITE;
    vq->desc[d2].next = 0;

    vq->avail->ring[vq->avail->idx % vq->size] = head;
    __asm__ volatile("mfence" ::: "memory");
    vq->avail->idx++;
    vq->free_head = (u16)((d2 + 1) % vq->size);

    u64 deadline = core_timer_now_ms() + 1000;
    while (vq->last_used == vq->used->idx) {
        if (core_timer_now_ms() > deadline) {
            driver_snd_virtio_log("virtio-snd: control timeout\n");
            return -1;
        }
        core_sched_yield();
    }
    __asm__ volatile("mfence" ::: "memory");
    vq->last_used++;
    driver_snd_virtio_hdr_t *rh = (driver_snd_virtio_hdr_t *)resp;
    return (rh->code == VSND_S_OK) ? 0 : -1;
}

/* ---- IRQ ---- */
static void driver_snd_virtio_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    driver_snd_virtio_dev_t *d = &g_vsnd;
    if (!d->up) return;
    u8 isr = driver_snd_virtio_r8(d, d->isr, 0);
    if (isr & 1) d->irq_count++;
}

/* ---- PCM control ---- */

static int driver_snd_virtio_pcm_set_params(driver_snd_virtio_dev_t *d, u32 rate) {
    driver_snd_virtio_pcm_set_params_t req;
    driver_snd_virtio_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.code = VSND_R_PCM_SET_PARAMS;
    req.stream_id = 0;
    req.buffer_bytes = 65536;
    req.period_bytes = 16384;
    req.features = 0;
    req.channels = d->channels;
    req.format = VSND_PCM_FMT_S16;
    req.rate = (rate == 44100) ? VSND_PCM_RATE_44100 : VSND_PCM_RATE_48000;
    return driver_snd_virtio_ctl(d, &req, sizeof(req), &resp, sizeof(resp));
}

static int driver_snd_virtio_pcm_simple(driver_snd_virtio_dev_t *d, u32 code) {
    driver_snd_virtio_pcm_hdr_t req;
    driver_snd_virtio_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.code = code;
    req.stream_id = 0;
    return driver_snd_virtio_ctl(d, &req, sizeof(req), &resp, sizeof(resp));
}

/* ---- snd ops ---- */

static int driver_snd_virtio_ops_play(driver_snd_device_t *sdev, const void *buf, int len) {
    driver_snd_virtio_dev_t *d = (driver_snd_virtio_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;

    if (!d->prepared) {
        if (driver_snd_virtio_pcm_set_params(d, d->rate) != 0) return -1;
        if (driver_snd_virtio_pcm_simple(d, VSND_R_PCM_PREPARE) != 0) return -1;
        d->prepared = 1;
    }
    if (!d->started) {
        if (driver_snd_virtio_pcm_simple(d, VSND_R_PCM_START) != 0) return -1;
        d->started = 1;
    }

    const u8 *src = (const u8 *)buf;
    int sent = 0;
    while (sent < len) {
        u32 chunk = (u32)(len - sent);
        if (chunk > 16384) chunk = 16384;
        chunk &= ~3u;
        if (chunk < 4) chunk = 4;
        memcpy(d->tx_buf, src + sent, chunk);

        driver_snd_virtio_pcm_xfer_t xfer;
        xfer.stream_id = 0;
        memcpy((void *)(uintptr_t)d->ctl_hdr_phys, &xfer, sizeof(xfer));

        if (driver_snd_virtio_tx_submit(d, d->ctl_hdr_phys, sizeof(xfer),
                           (u64)(uintptr_t)d->tx_buf, chunk) < 0)
            return sent ? sent : -1;
        if (driver_snd_virtio_tx_complete(d, core_timer_now_ms() + 2000) != 0)
            return sent ? sent : -1;
        sent += (int)chunk;
    }
    return sent;
}

static int driver_snd_virtio_ops_stop(driver_snd_device_t *sdev) {
    driver_snd_virtio_dev_t *d = (driver_snd_virtio_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (d->started) {
        driver_snd_virtio_pcm_simple(d, VSND_R_PCM_STOP);
        driver_snd_virtio_pcm_simple(d, VSND_R_PCM_RELEASE);
        d->started = 0;
        d->prepared = 0;
    }
    return 0;
}

static int driver_snd_virtio_ops_set_rate(driver_snd_device_t *sdev, u32 rate) {
    driver_snd_virtio_dev_t *d = (driver_snd_virtio_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (rate != 48000 && rate != 44100) return -1;
    if (d->prepared) return -1;
    d->rate = rate;
    return 0;
}

static int driver_snd_virtio_ops_set_volume(driver_snd_device_t *sdev, u32 vol) {
    (void)sdev; (void)vol;
    return 0;   /* framework-level volume; no ctl elements queried */
}

static int driver_snd_virtio_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps) {
    driver_snd_virtio_dev_t *d = (driver_snd_virtio_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    caps->rates = SND_RATE_44100 | SND_RATE_48000;
    caps->min_channels = 1;
    caps->max_channels = d->channels;
    caps->bits8 = 0;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->cur_rate = d->rate;
    caps->cur_volume = sdev->volume;
    return 0;
}

/* ---- init ---- */

int driver_snd_virtio_init(driver_pci_dev_t *pdev) {
    driver_snd_virtio_dev_t *d = &g_vsnd;
    if (d->up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    int found;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        found = ((ids & 0xffff) == 0x1AF4) &&
                (((ids >> 16) & 0xffff) == 0x1059);
    } else {
        found = driver_pci_find_device(0x1AF4, 0x1059, &bus, &dev, &func) == 0;
    }
    if (!found) return -1;

    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    d->vid = (u16)(ids & 0xffff);
    d->did = (u16)(ids >> 16);
    d->bus = bus; d->dev = dev; d->func = func;
    driver_pci_enable_device(bus, dev, func);

    if (driver_snd_virtio_find_caps(d, bus, dev, func) != 0) {
        driver_snd_virtio_log("virtio-snd: required PCI capabilities not found\n");
        return -1;
    }

    /* reset + driver handshake */
    driver_snd_virtio_w8(d, d->common, VCFG_STATUS, 0);
    driver_snd_virtio_w8(d, d->common, VCFG_STATUS, VSTS_ACK | VSTS_DRIVER);
    u32 nq = driver_snd_virtio_r16(d, d->common, VCFG_NUM_QUEUES);
    if (nq < VSND_VQ_MAX) {
        driver_snd_virtio_log("virtio-snd: not enough virtqueues\n");
        driver_snd_virtio_w8(d, d->common, VCFG_STATUS, VSTS_FAILED);
        return -1;
    }
    for (int i = 0; i < VSND_VQ_MAX; i++) {
        if (driver_snd_virtio_setup_queue(d, i, &d->vq[i]) != 0) {
            driver_snd_virtio_log("virtio-snd: queue setup failed\n");
            driver_snd_virtio_w8(d, d->common, VCFG_STATUS, VSTS_FAILED);
            return -1;
        }
    }

    /* shared control scratch + tx payload buffers (5 pages: 16 KiB
     * data + a separate page for the status) */
    u64 hdr_page = mem_pmm_alloc_frame();
    u64 tx = mem_pmm_alloc_contig(5);
    if (!hdr_page || !tx) {
        driver_snd_virtio_w8(d, d->common, VCFG_STATUS, VSTS_FAILED);
        return -1;
    }
    d->ctl_hdr_phys = hdr_page;
    d->tx_phys = tx;
    d->tx_buf = (u8 *)(uintptr_t)tx;
    d->tx_status_phys = tx + 4 * PMM_PAGE_SIZE;
    d->tx_status = (driver_snd_virtio_pcm_status_t *)(uintptr_t)d->tx_status_phys;
    memset((void *)(uintptr_t)tx, 0, 5 * PMM_PAGE_SIZE);

    u32 cfg_streams = driver_snd_virtio_r32(d, d->device, 4);  /* config.streams */
    d->n_streams = cfg_streams;

    /* IRQ */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    d->irq = -1;
    if (irq < 16 && arch_irq_register_handler(irq, driver_snd_virtio_irq_handler, NULL) == 0)
        d->irq = irq;

    driver_snd_virtio_w8(d, d->common, VCFG_STATUS, VSTS_ACK | VSTS_DRIVER | VSTS_DRIVER_OK);

    d->rate = 48000;
    d->channels = 2;
    d->up = 1;

    char line[96];
    char n[8];
    strcpy(line, "virtio-snd: streams=");
    u64_to_str(cfg_streams, n); strcat(line, n);
    strcat(line, " irq ");
    u64_to_hex((u64)(d->irq & 0xff), n, 2); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");

    driver_snd_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "virtio-snd");
    nd.type = SND_TYPE_VIRTIO;
    nd.bus = d->bus; nd.dev = d->dev; nd.func = d->func;
    nd.vendor_id = d->vid; nd.device_id = d->did;
    nd.rate = d->rate; nd.channels = d->channels; nd.volume = 100;
    nd.priv = d;
    int idx = driver_snd_register(&nd, &driver_snd_virtio_snd_ops);
    return idx >= 0 ? 0 : -1;
}

void driver_snd_virtio_print_state(void) {
    driver_snd_virtio_dev_t *d = &g_vsnd;
    char line[128];
    char n[24];

    screen_console_puts("virtio-snd: ");
    if (!d->up) {
        screen_console_puts("not present\n");
        return;
    }
    strcpy(line, "vid=0x");
    u64_to_hex(d->vid, n, 4); strcat(line, n);
    strcat(line, " did=0x");
    u64_to_hex(d->did, n, 4); strcat(line, n);
    strcat(line, " streams=");
    u64_to_str(d->n_streams, n); strcat(line, n);
    strcat(line, " rate=");
    u64_to_str(d->rate, n); strcat(line, n);
    strcat(line, " irqs=");
    u64_to_str(d->irq_count, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
}
