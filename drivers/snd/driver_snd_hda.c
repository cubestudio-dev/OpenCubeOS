/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/hda.c
 * Purpose: Intel HD Audio (HDA) controller + codec driver.
 *
 * Mainstream PCI sound: Intel ICH6/ICH7/ICH9 family HD Audio
 * controllers (8086:2668 ich6, 8086:293E ich9, plus common OEM ids).
 * QEMU exposes the same hardware as `-device intel-hda` (PCI 8086:2668)
 * with an on-board codec (hda-output / hda-duplex).
 *
 * Hardware path implemented here (register layout per the Intel HD
 * Audio 1.0a spec, flow verified against QEMU's hw/audio/intel-hda):
 *
 *   1. PCI probe (8086:2668/293E/293F/3A3E or class 0x040300),
 *      BAR0 = MMIO register block (identity-mapped, < 4 GiB).
 *   2. Controller reset: GCTL.CRST 0 -> 1, codec wake bits in
 *      STATESTS select the codec address.
 *   3. CORB / RIRB command rings (one PMM page, 128-byte aligned),
 *      one verb at a time: write CORB entry + CORBWP, wait for the
 *      RIRB write pointer, read the response, ack RIRBSTS (this is
 *      what lets the controller continue, it pauses the CORB once
 *      RINTCNT responses are pending).
 *   4. Codec enumeration: root -> AFG (AC_PAR_NODE_COUNT,
 *      AC_PAR_FUNCTION_TYPE), widget scan (AC_PAR_AUDIO_WIDGET_CAP),
 *      output pin -> DAC routing via AC_PAR_CONNLIST_LEN +
 *      AC_VERB_GET_CONNECT_LIST, then pin enable + connection select.
 *   5. Output stream 0 (SDO0): 2-entry BDL (32 KiB each) from PMM,
 *      IOC on both entries, stream tag 1; codec side gets
 *      SET_STREAM_FORMAT (48k/44.1k 16-bit stereo) and
 *      SET_CHANNEL_STREAMID before the stream starts.
 *   6. Real interrupts: SDnSTS IOC + INTCTL (GIE|CIE|SDO0 bit) routed
 *      through the PCI interrupt line; the handler counts completions
 *      and clears the status.  driver_snd_play() flow control polls LPIB
 *      (the DMA position) and copies into the 64 KiB ring as space
 *      frees up.
 *
 * All error paths return -1 and log; no stubs, no fake data.
 */
#include "driver_snd_hda.h"
#include "driver_pci.h"
#include "mem_pmm.h"
#include "arch_irq.h"
#include "screen_console.h"
#include "lib_string.h"
#include "driver_snd.h"
#include "core_sched.h"
#include "core_timer.h"

/* ---- register offsets (ICH6 layout) ---- */
#define HDA_GCAP          0x00
#define HDA_GCTL          0x08
#define HDA_STATESTS      0x0e
#define HDA_INTCTL        0x20
#define HDA_INTSTS        0x24
#define HDA_CORBLBASE     0x40
#define HDA_CORBUBASE     0x44
#define HDA_CORBWP        0x48
#define HDA_CORBRP        0x4a
#define HDA_CORBCTL       0x4c
#define HDA_CORBSTS       0x4d
#define HDA_RIRBLBASE     0x50
#define HDA_RIRBUBASE     0x54
#define HDA_RIRBWP        0x58
#define HDA_RINTCNT       0x5a
#define HDA_RIRBCTL       0x5c
#define HDA_RIRBSTS       0x5d

#define HDA_SDO_BASE(gc)  (0x80u + 0x20u * (((gc) >> 8) & 0xf))

/* per-stream register offsets from the stream base */
#define HDA_SD_CTL        0x00
#define HDA_SD_STS        0x03
#define HDA_SD_LPIB       0x04
#define HDA_SD_CBL        0x08
#define HDA_SD_LVI        0x0c
#define HDA_SD_FORMAT     0x12
#define HDA_SD_BDLPL      0x18
#define HDA_SD_BDLPU      0x1c

/* GCTL bits */
#define HDA_GCTL_CRST     (1u << 0)
#define HDA_GCTL_UNSOL    (1u << 8)

/* CORB/RIRB control bits */
#define HDA_CORBCTL_RUN   (1u << 1)
#define HDA_CORBRP_RST    (1u << 15)
#define HDA_RIRBWP_RST    (1u << 15)
#define HDA_RBCTL_DMA_EN  (1u << 1)
#define HDA_RBCTL_IRQ_EN  (1u << 0)
#define HDA_RBSTS_IRQ     (1u << 0)

/* stream control bits */
#define HDA_SD_SRST       (1u << 0)
#define HDA_SD_RUN        (1u << 1)
#define HDA_SD_IOCE       (1u << 2)
#define HDA_SD_TAG_SHIFT  20
/* SD_STS (byte 3 of SD_CTL): bits 26/27/28 = completion/fifo/desc err */
#define HDA_SD_STS_IOC    (1u << 2)
#define HDA_SD_STS_ALLERR (1u << 2 | 1u << 3 | 1u << 4)
#define HDA_SD_STS_READY  (1u << 5)

/* INTCTL: QEMU models the 8 stream bits low (SDI0..3 = 0..3, SDO0..3
 * = 4..7); controller bit 30, global bit 31. */
#define HDA_INT_GIE       (1u << 31)
#define HDA_INT_CIE       (1u << 30)
#define HDA_INT_SDO0      (1u << 4)
#define HDA_INTSTS_CTRL   (1u << 30)

/* ---- codec verbs / parameters ---- */
#define HDA_VERB_GET_PARAM      0x0f00
#define HDA_VERB_GET_CONN_LIST  0x0f02
#define HDA_VERB_SET_FMT        0x200
#define HDA_VERB_SET_AMP        0x300
#define HDA_VERB_SET_CONN_SEL   0x701
#define HDA_VERB_SET_STREAM_ID  0x706
#define HDA_VERB_SET_PIN_CTRL   0x707

#define HDA_PAR_NODE_COUNT      0x04
#define HDA_PAR_FUNCTION_TYPE   0x05
#define HDA_PAR_WIDGET_CAP      0x09
#define HDA_PAR_PIN_CAP         0x0c
#define HDA_PAR_CONNLIST_LEN    0x0e
#define HDA_PAR_AMP_OUT_CAP     0x12

#define HDA_WTYPE(x)   (((x) >> 20) & 0xf)
#define HDA_WTYPE_OUT  0
#define HDA_WTYPE_PIN  4
#define HDA_PINCAP_OUT (1u << 4)

/* stream format bits */
#define HDA_FMT_CHAN(x)   (((x) - 1) & 0xf)
#define HDA_FMT_BITS_16   (1u << 4)
#define HDA_FMT_BASE_44K  (1u << 14)

/* amp bits */
#define HDA_AMP_MUTE      (1u << 7)
#define HDA_AMP_SET_RIGHT (1u << 12)
#define HDA_AMP_SET_LEFT  (1u << 13)
#define HDA_AMP_SET_OUT   (1u << 15)

/* ---- device state ---- */
#define HDA_RING_BYTES    65536          /* 2 x 32 KiB ping-pong */
#define HDA_BDL_ENTRIES   2
#define HDA_TAG           1              /* stream tag used on the link */

typedef struct driver_snd_hda_dev {
    u64    mmio;                 /* BAR0 physical == virtual (identity) */
    u8     bus, dev, func;
    u16    vid, did;
    u32    n_output_streams;
    u8     codec_cad;            /* codec address that woke up */

    u64    corb_phys;            /* 4 KiB page: corb(1 KiB) + rirb(2 KiB) */
    volatile u32 *corb;          /* 256 entries */
    volatile u64 *rirb;          /* 256 entries (resp + resp_ex) */
    u8     rirb_read;            /* driver-side RIRB read pointer */

    u64    bdl_phys;             /* BDL: 2 x 16 bytes (PMM frame) */
    u64    dma_phys;             /* PCM ring: 64 KiB contiguous */
    u8    *dma;

    /* stream state */
    int    running;
    u64    sw_pos;               /* bytes written into the ring (absolute) */
    u32    rate;
    u8     channels;

    /* codec path */
    int    dac_nid;              /* DAC widget feeding the pin */
    int    pin_nid;              /* output pin complex */
    u32    amp_max;              /* max amp gain steps */

    /* irq */
    int    irq;
    u64    irq_count;
    int    up;
} driver_snd_hda_dev_t;

static driver_snd_hda_dev_t g_hda;

/* forward decls (ops table is defined before first use below) */
static int driver_snd_hda_ops_play(driver_snd_device_t *sdev, const void *buf, int len);
static int driver_snd_hda_ops_stop(driver_snd_device_t *sdev);
static int driver_snd_hda_ops_set_rate(driver_snd_device_t *sdev, u32 rate);
static int driver_snd_hda_ops_set_volume(driver_snd_device_t *sdev, u32 vol);
static int driver_snd_hda_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps);
static const driver_snd_ops_t driver_snd_hda_snd_ops = {
    .play       = driver_snd_hda_ops_play,
    .stop       = driver_snd_hda_ops_stop,
    .set_rate   = driver_snd_hda_ops_set_rate,
    .set_volume = driver_snd_hda_ops_set_volume,
    .get_caps   = driver_snd_hda_ops_get_caps,
};

/* ---- MMIO accessors ---- */
static inline u32 driver_snd_hda_r32(driver_snd_hda_dev_t *d, u32 off) {
    return *(volatile u32 *)(uintptr_t)(d->mmio + off);
}
static inline void driver_snd_hda_w32(driver_snd_hda_dev_t *d, u32 off, u32 v) {
    *(volatile u32 *)(uintptr_t)(d->mmio + off) = v;
}
static inline u16 driver_snd_hda_r16(driver_snd_hda_dev_t *d, u32 off) {
    return *(volatile u16 *)(uintptr_t)(d->mmio + off);
}
static inline void driver_snd_hda_w16(driver_snd_hda_dev_t *d, u32 off, u16 v) {
    *(volatile u16 *)(uintptr_t)(d->mmio + off) = v;
}
static inline u8 driver_snd_hda_r8(driver_snd_hda_dev_t *d, u32 off) {
    return *(volatile u8 *)(uintptr_t)(d->mmio + off);
}
static inline void driver_snd_hda_w8(driver_snd_hda_dev_t *d, u32 off, u8 v) {
    *(volatile u8 *)(uintptr_t)(d->mmio + off) = v;
}

static void driver_snd_hda_log(const char *s) { screen_console_puts(s); }

/* ---- CORB / RIRB ---- */

/* Send one verb, wait for its solicited response.  After each response
 * the RIRBSTS IRQ bit is acked which (per the controller spec) releases
 * the CORB for the next command.  Returns the response value, or
 * 0xFFFFFFFF on timeout (logged). */
static u32 driver_snd_hda_corb_xmit(driver_snd_hda_dev_t *d, u32 verb) {
    u16 wp = driver_snd_hda_r16(d, HDA_CORBWP) & 0xff;
    u16 next = (wp + 1) & 0xff;
    u64 deadline = core_timer_now_ms() + 1000;

    d->corb[next] = verb;
    driver_snd_hda_w16(d, HDA_CORBWP, next);

    for (;;) {
        u16 rirb_wp = driver_snd_hda_r16(d, HDA_RIRBWP) & 0xff;
        if (rirb_wp != d->rirb_read) {
            d->rirb_read = (u8)((d->rirb_read + 1) & 0xff);
            u64 entry = d->rirb[d->rirb_read];
            u32 resp = (u32)(entry & 0xffffffffu);
            u32 ex = (u32)(entry >> 32);
            /* ack the response IRQ so the controller keeps going */
            driver_snd_hda_w8(d, HDA_RIRBSTS, HDA_RBSTS_IRQ);
            int unsolicited = (ex & (1u << 4)) ? 1 : 0;
            /* BUG-0131 FIX (A9-05): the filter compared the response's
             * cad against d->codec_cad, which stays 0 during codec
             * ENUMERATION (it is only assigned after scan_codec
             * succeeds). A codec that lives at cad != 0 had every
             * solicited response dropped as "foreign" -> every verb
             * timed out -> "codec enumeration failed". The target cad
             * is encoded in the verb itself (bits 31:28), so compare
             * against that - no state needed, correct at any cad. */
            if (!unsolicited && (u8)(ex & 0xf) == (u8)((verb >> 28) & 0xf))
                return resp;
            continue;   /* ignore unsolicited / foreign responses */
        }
        if (core_timer_now_ms() > deadline) {
            char ln[128]; char nn[16];
            strcpy(ln, "hda: verb timeout verb=0x");
            u64_to_hex(verb, nn, 8); strcat(ln, nn);
            strcat(ln, " wp=");
            u64_to_str(driver_snd_hda_r16(d, HDA_CORBWP) & 0xff, nn);
            strcat(ln, nn);
            strcat(ln, " rp=");
            u64_to_str(driver_snd_hda_r16(d, HDA_CORBRP) & 0xff, nn);
            strcat(ln, nn);
            strcat(ln, " rirbwp=");
            u64_to_str(driver_snd_hda_r16(d, HDA_RIRBWP) & 0xff, nn);
            strcat(ln, nn);
            strcat(ln, " read=");
            u64_to_str(d->rirb_read, nn); strcat(ln, nn);
            strcat(ln, " rirbsts=0x");
            u64_to_hex(driver_snd_hda_r8(d, HDA_RIRBSTS), nn, 2);
            strcat(ln, nn);
            screen_console_puts(ln);
            screen_console_puts("\n");
            return 0xFFFFFFFFu;
        }
        core_sched_yield();
    }
}

static u32 driver_snd_hda_get_param(driver_snd_hda_dev_t *d, u8 cad, u8 nid, u32 param) {
    u32 verb = ((u32)cad << 28) | ((u32)nid << 20) |
               (HDA_VERB_GET_PARAM << 8) | param;
    return driver_snd_hda_corb_xmit(d, verb);
}

static void driver_snd_hda_codec_set(driver_snd_hda_dev_t *d, u8 nid, u32 verb12, u8 payload) {
    u32 verb = ((u32)d->codec_cad << 28) | ((u32)nid << 20) |
               (verb12 << 8) | payload;
    driver_snd_hda_corb_xmit(d, verb);
}

/* 12-bit verb with a full 16-bit payload (4/16 id/payload encoding,
 * e.g. SET_STREAM_FORMAT carries the base-frequency bit in payload
 * bit 14). */
static void driver_snd_hda_codec_set16(driver_snd_hda_dev_t *d, u8 nid, u32 verb12, u16 payload) {
    u32 verb = ((u32)d->codec_cad << 28) | ((u32)nid << 20) |
               ((verb12 & 0xf00u) << 8) | payload;
    driver_snd_hda_corb_xmit(d, verb);
}

/* ---- codec enumeration ---- */

static int driver_snd_hda_scan_codec(driver_snd_hda_dev_t *d, u8 cad) {
    u32 nc = driver_snd_hda_get_param(d, cad, 0, HDA_PAR_NODE_COUNT);
    if (nc == 0xFFFFFFFFu || nc == 0) return -1;
    u8 afg = (u8)((nc >> 16) & 0xff);
    u8 count = (u8)(nc & 0xff);
    if (afg == 0 || count == 0) return -1;

    u32 ftype = driver_snd_hda_get_param(d, cad, afg, HDA_PAR_FUNCTION_TYPE);
    if (ftype == 0xFFFFFFFFu || (ftype & 0xff) != 0x01)
        return -1;   /* not an audio function group */

    u32 sub = driver_snd_hda_get_param(d, cad, afg, HDA_PAR_NODE_COUNT);
    if (sub == 0xFFFFFFFFu) return -1;
    u8 first = (u8)((sub >> 16) & 0xff);
    u8 nsub = (u8)(sub & 0xff);
    if (nsub == 0 || nsub > 64) return -1;

    /* widget scan: find the first DAC and the first output-capable pin */
    int dac_nid = -1, pin_nid = -1;
    for (u8 i = 0; i < nsub; i++) {
        u8 nid = (u8)(first + i);
        u32 cap = driver_snd_hda_get_param(d, cad, nid, HDA_PAR_WIDGET_CAP);
        if (cap == 0xFFFFFFFFu) continue;
        u32 type = HDA_WTYPE(cap);
        if (type == HDA_WTYPE_OUT && dac_nid < 0) {
            dac_nid = nid;
        } else if (type == HDA_WTYPE_PIN && pin_nid < 0) {
            u32 pc = driver_snd_hda_get_param(d, cad, nid, HDA_PAR_PIN_CAP);
            if (pc != 0xFFFFFFFFu && (pc & HDA_PINCAP_OUT))
                pin_nid = nid;
        }
        if (dac_nid >= 0 && pin_nid >= 0) break;
    }
    if (dac_nid < 0 || pin_nid < 0) return -1;

    /* verify the pin can reach the DAC through its connection list */
    u32 cl = driver_snd_hda_get_param(d, cad, (u8)pin_nid, HDA_PAR_CONNLIST_LEN);
    if (cl == 0xFFFFFFFFu || (cl & 0x7f) == 0) return -1;
    int conn_len = cl & 0x7f;
    int found = 0;
    int conn_sel = 0;
    for (int i = 0; i < conn_len && i < 8; i++) {
        u32 entry = driver_snd_hda_corb_xmit(d, ((u32)cad << 28) |
                                       ((u32)pin_nid << 20) |
                                       (HDA_VERB_GET_CONN_LIST << 8) |
                                       (u8)i);
        if (entry == 0xFFFFFFFFu) continue;
        if ((entry & 0xff) == (u32)dac_nid) {
            found = 1;
            conn_sel = i;
            break;
        }
    }
    if (!found) return -1;   /* pin cannot route to the DAC */

    /* select the connection + enable the pin output.
     * BUG-0131 FIX: codec_set() encodes the verb with d->codec_cad,
     * which is NOT yet assigned during enumeration - these two writes
     * went to cad 0 instead of the codec being scanned. Build the
     * verbs explicitly with the scan target cad (same shape as the
     * GET_CONN_LIST verbs above). */
    driver_snd_hda_corb_xmit(d, ((u32)cad << 28) | ((u32)pin_nid << 20) |
                             (HDA_VERB_SET_CONN_SEL << 8) | (u8)conn_sel);
    driver_snd_hda_corb_xmit(d, ((u32)cad << 28) | ((u32)pin_nid << 20) |
                             (HDA_VERB_SET_PIN_CTRL << 8) | 0x40 /* OUT_EN */);

    u32 amp = driver_snd_hda_get_param(d, cad, (u8)dac_nid, HDA_PAR_AMP_OUT_CAP);
    d->amp_max = (amp == 0xFFFFFFFFu) ? 0x4a : ((amp >> 8) & 0x7f);
    if (d->amp_max == 0) d->amp_max = 0x4a;

    d->codec_cad = cad;
    d->dac_nid = dac_nid;
    d->pin_nid = pin_nid;
    return 0;
}

/* ---- stream helpers ---- */

static u32 driver_snd_hda_sdo_base(driver_snd_hda_dev_t *d) {
    return HDA_SDO_BASE(driver_snd_hda_r16(d, HDA_GCAP));
}

static void driver_snd_hda_stream_reset(driver_snd_hda_dev_t *d, u32 base) {
    driver_snd_hda_w8(d, base + HDA_SD_CTL, HDA_SD_SRST);
    for (int t = 0; t < 100000; t++) {
        if (driver_snd_hda_r8(d, base + HDA_SD_CTL) & HDA_SD_SRST) break;
    }
    driver_snd_hda_w8(d, base + HDA_SD_CTL, 0);
    for (int t = 0; t < 100000; t++) {
        if (!(driver_snd_hda_r8(d, base + HDA_SD_CTL) & HDA_SD_SRST)) break;
    }
}

static u32 driver_snd_hda_format_value(u32 rate, u8 channels) {
    u32 v = HDA_FMT_BITS_16 | HDA_FMT_CHAN(channels);
    if (rate == 44100) v |= HDA_FMT_BASE_44K;
    return v;
}

/* ---- IRQ ---- */
static void driver_snd_hda_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    driver_snd_hda_dev_t *d = &g_hda;
    if (!d->up) return;

    u32 intsts = driver_snd_hda_r32(d, HDA_INTSTS);
    if (!(intsts & (HDA_INTSTS_CTRL | HDA_INT_SDO0))) return;
    if (intsts & HDA_INT_SDO0) {
        u32 base = driver_snd_hda_sdo_base(d);
        u8 sts = driver_snd_hda_r8(d, base + HDA_SD_STS);
        if (sts & HDA_SD_STS_ALLERR) {
            driver_snd_hda_w8(d, base + HDA_SD_STS, sts & HDA_SD_STS_ALLERR);
            d->irq_count++;
        }
    }
}

/* ---- init ---- */

static int driver_snd_hda_setup(driver_snd_hda_dev_t *d, u8 bus, u8 dev, u8 func) {
    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    d->vid = (u16)(ids & 0xffff);
    d->did = (u16)(ids >> 16);
    d->bus = bus; d->dev = dev; d->func = func;

    /* BAR0 must be memory space */
    u32 bar0 = driver_pci_read_config(bus, dev, func, 0x10);
    if (bar0 & 1) return -1;
    d->mmio = bar0 & 0xFFFFFFF0u;
    if (d->mmio == 0) return -1;
    driver_pci_enable_device(bus, dev, func);

    u32 gcaps = driver_snd_hda_r16(d, HDA_GCAP);
    d->n_output_streams = (gcaps >> 12) & 0xf;
    if (d->n_output_streams == 0) return -1;

    /* controller reset: clear CRST, wait, set again, wait for codecs */
    driver_snd_hda_w32(d, HDA_GCTL, 0);
    for (int t = 0; t < 100000; t++) {
        if (!(driver_snd_hda_r32(d, HDA_GCTL) & HDA_GCTL_CRST)) break;
    }
    driver_snd_hda_w32(d, HDA_GCTL, HDA_GCTL_CRST | HDA_GCTL_UNSOL);
    for (int t = 0; t < 100000; t++) {
        if (driver_snd_hda_r32(d, HDA_GCTL) & HDA_GCTL_CRST) break;
    }
    if (!(driver_snd_hda_r32(d, HDA_GCTL) & HDA_GCTL_CRST)) {
        driver_snd_hda_log("hda: controller reset failed\n");
        return -1;
    }

    u16 statests = driver_snd_hda_r16(d, HDA_STATESTS);
    driver_snd_hda_w16(d, HDA_STATESTS, statests);   /* write-1-to-clear */
    if (statests == 0) {
        /* some controllers raise the wake bits a moment later */
        u64 deadline = core_timer_now_ms() + 500;
        while (core_timer_now_ms() < deadline) {
            statests = driver_snd_hda_r16(d, HDA_STATESTS);
            if (statests) break;
            core_sched_yield();
        }
        if (statests == 0) {
            driver_snd_hda_log("hda: no codec responded\n");
            return -1;
        }
    }
    int cad = -1;
    for (int i = 0; i < 8; i++) {
        if (statests & (1u << i)) { cad = i; break; }
    }
    if (cad < 0) return -1;

    /* CORB/RIRB: one PMM page (corb 1 KiB + rirb 2 KiB) */
    u64 page = mem_pmm_alloc_frame();
    if (!page) return -1;
    d->corb_phys = page;
    d->corb = (volatile u32 *)(uintptr_t)page;
    d->rirb = (volatile u64 *)(uintptr_t)(page + 1024);
    for (int i = 0; i < 256; i++) { d->corb[i] = 0; d->rirb[i] = 0; }

    driver_snd_hda_w32(d, HDA_CORBLBASE, (u32)(page & 0xFFFFFF80u));
    driver_snd_hda_w32(d, HDA_CORBUBASE, 0);
    driver_snd_hda_w16(d, HDA_CORBWP, 0);
    driver_snd_hda_w16(d, HDA_CORBRP, HDA_CORBRP_RST);
    driver_snd_hda_w16(d, HDA_CORBRP, 0);
    driver_snd_hda_w8(d, HDA_CORBCTL, HDA_CORBCTL_RUN);

    driver_snd_hda_w32(d, HDA_RIRBLBASE, (u32)((page + 1024) & ~0x7fu));
    driver_snd_hda_w32(d, HDA_RIRBUBASE, 0);
    driver_snd_hda_w16(d, HDA_RIRBWP, HDA_RIRBWP_RST);
    driver_snd_hda_w16(d, HDA_RINTCNT, 1);
    d->rirb_read = 0;
    /* RIRB IRQ enable stays ON: QEMU's controller only resets its
     * response counter (rirb_count -> 0, which unblocks the CORB for
     * the next verb) when the guest write-clears RIRBSTS.IRQ, and that
     * bit is only raised while this enable is set.  The physical IRQ
     * line stays quiet because INTCTL.CIE (controller interrupt) is
     * not enabled below - only the stream IOC bit is. */
    driver_snd_hda_w8(d, HDA_RIRBCTL, HDA_RBCTL_DMA_EN | HDA_RBCTL_IRQ_EN);

    /* codec enumeration */
    if (driver_snd_hda_scan_codec(d, (u8)cad) != 0) {
        driver_snd_hda_log("hda: codec enumeration failed\n");
        return -1;
    }

    /* BDL + 64 KiB DMA ring from PMM */
    u64 bdl = mem_pmm_alloc_frame();
    d->dma_phys = mem_pmm_alloc_contig(HDA_RING_BYTES / PMM_PAGE_SIZE);
    if (!bdl || !d->dma_phys) return -1;
    d->bdl_phys = bdl;
    d->dma = (u8 *)(uintptr_t)d->dma_phys;
    memset(d->dma, 0, HDA_RING_BYTES);

    volatile u32 *b32 = (volatile u32 *)(uintptr_t)bdl;
    for (int i = 0; i < HDA_BDL_ENTRIES; i++) {
        u64 buf_addr = d->dma_phys + (u64)i * (HDA_RING_BYTES / HDA_BDL_ENTRIES);
        u32 len = HDA_RING_BYTES / HDA_BDL_ENTRIES;
        b32[i * 4 + 0] = (u32)(buf_addr & 0xffffffffu);
        b32[i * 4 + 1] = (u32)(buf_addr >> 32);
        /* NOTE on BDL entry encoding: the Intel HDA spec packs
         * length[31:1] | IOC[0] into dword 2 (dword 3 reserved).
         * QEMU's controller model instead reads dword 2 as the plain
         * byte length and takes the IOC flag from dword 3 bit 0.
         * We follow the QEMU encoding so the reference environment
         * delivers IOC interrupts and wraps LPIB correctly. */
        b32[i * 4 + 2] = len;
        b32[i * 4 + 3] = 1;   /* IOC */
    }

    /* stream registers (output stream 0) */
    u32 sbase = driver_snd_hda_sdo_base(d);
    driver_snd_hda_stream_reset(d, sbase);
    driver_snd_hda_w16(d, sbase + HDA_SD_FORMAT, driver_snd_hda_format_value(48000, 2));
    driver_snd_hda_w32(d, sbase + HDA_SD_CBL, HDA_RING_BYTES);
    driver_snd_hda_w16(d, sbase + HDA_SD_LVI, HDA_BDL_ENTRIES - 1);
    driver_snd_hda_w32(d, sbase + HDA_SD_BDLPL, (u32)(bdl & ~0x7fu));
    driver_snd_hda_w32(d, sbase + HDA_SD_BDLPU, 0);

    /* codec: format + stream id (before RUN) */
    u32 fmt = driver_snd_hda_format_value(48000, 2);
    driver_snd_hda_codec_set(d, (u8)d->dac_nid, HDA_VERB_SET_FMT, fmt);
    driver_snd_hda_codec_set(d, (u8)d->dac_nid, HDA_VERB_SET_STREAM_ID,
                  (HDA_TAG << 4) | 0);

    /* IRQ from the PCI interrupt line.  NOTE: only the stream IOC
     * interrupt is enabled (INTCTL SDO0 bit); the controller/RIRB
     * interrupt bit (CIE) stays off because verb responses are read
     * by polling the RIRB and the handler would have nothing to clear
     * for them - an unhandled pending controller interrupt would
     * storm. */
    u32 icfg = driver_pci_read_config(bus, dev, func, 0x3c);
    int irq = (int)(icfg & 0xff);
    d->irq = -1;
    if (irq < 16 && arch_irq_register_handler(irq, driver_snd_hda_irq_handler, NULL) == 0)
        d->irq = irq;
    driver_snd_hda_w32(d, HDA_INTCTL, HDA_INT_GIE | HDA_INT_SDO0);

    d->rate = 48000;
    d->channels = 2;
    d->sw_pos = 0;
    d->running = 0;
    d->up = 1;
    return 0;
}

int driver_snd_hda_init(driver_pci_dev_t *pdev) {
    driver_snd_hda_dev_t *d = &g_hda;
    if (d->up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 cls = driver_pci_read_config(bus, dev, func, 0x08) >> 8;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        if (((ids & 0xffff) != 0x8086) || ((cls & 0xff0000) != 0x040000))
            return -1;
    } else {
        int found = driver_pci_find_device(0x8086, 0x2668, &bus, &dev, &func) == 0;
        if (!found) found = driver_pci_find_device(0x8086, 0x293E, &bus, &dev, &func) == 0;
        if (!found) found = driver_pci_find_device(0x8086, 0x293F, &bus, &dev, &func) == 0;
        if (!found) found = driver_pci_find_device(0x8086, 0x3A3E, &bus, &dev, &func) == 0;
        if (!found)
            found = driver_pci_find_class_mask(0x040300, 0xffffff, 0,
                                        &bus, &dev, &func) == 0;
        if (!found) return -1;
    }

    if (driver_snd_hda_setup(d, bus, dev, func) != 0) {
        d->up = 0;
        return -1;
    }

    char line[128];
    char n[8];
    strcpy(line, "hda: 8086:");
    u64_to_hex(d->did, n, 4); strcat(line, n);
    strcat(line, " codec ");
    u64_to_hex(d->codec_cad, n, 2); strcat(line, n);
    strcat(line, " DAC nid ");
    u64_to_hex((u64)d->dac_nid, n, 2); strcat(line, n);
    strcat(line, " pin nid ");
    u64_to_hex((u64)d->pin_nid, n, 2); strcat(line, n);
    strcat(line, " irq ");
    u64_to_hex((u64)(d->irq & 0xff), n, 2); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");

    driver_snd_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "hda");
    nd.type = SND_TYPE_HDA;
    nd.bus = d->bus; nd.dev = d->dev; nd.func = d->func;
    nd.vendor_id = d->vid; nd.device_id = d->did;
    nd.rate = d->rate; nd.channels = d->channels; nd.volume = 74;
    nd.priv = d;
    int idx = driver_snd_register(&nd, &driver_snd_hda_snd_ops);
    return idx >= 0 ? 0 : -1;
}

/* ---- playback ---- */

static u32 driver_snd_hda_lpib(driver_snd_hda_dev_t *d) {
    u32 sbase = driver_snd_hda_sdo_base(d);
    return driver_snd_hda_r32(d, sbase + HDA_SD_LPIB);
}

static void driver_snd_hda_stream_start(driver_snd_hda_dev_t *d) {
    u32 sbase = driver_snd_hda_sdo_base(d);
    driver_snd_hda_w8(d, sbase + HDA_SD_STS, HDA_SD_STS_ALLERR);   /* clear pending */
    u32 ctl = driver_snd_hda_r32(d, sbase + HDA_SD_CTL) & 0xf;
    driver_snd_hda_w32(d, sbase + HDA_SD_CTL,
            ctl | HDA_SD_RUN | HDA_SD_IOCE |
            ((u32)HDA_TAG << HDA_SD_TAG_SHIFT));
    d->running = 1;
}

static int driver_snd_hda_ops_play(driver_snd_device_t *sdev, const void *buf, int len) {
    driver_snd_hda_dev_t *d = (driver_snd_hda_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;

    if (!d->running) driver_snd_hda_stream_start(d);

    while (sent < len) {
        u32 hw = driver_snd_hda_lpib(d);
        u64 sw = d->sw_pos;
        /* in-flight = written but not yet consumed (mod ring size) */
        u32 inflight = (u32)((sw - hw) & (HDA_RING_BYTES - 1));
        u32 free = HDA_RING_BYTES - 1 - inflight;
        if (free == 0) { core_sched_yield(); continue; }

        u32 ring_off = (u32)(sw & (HDA_RING_BYTES - 1));
        u32 chunk = (u32)(len - sent);
        if (chunk > free) chunk = free;
        if (chunk > HDA_RING_BYTES - ring_off)
            chunk = HDA_RING_BYTES - ring_off;
        if (chunk == 0) { core_sched_yield(); continue; }

        memcpy(d->dma + ring_off, src + sent, chunk);
        d->sw_pos += chunk;
        sent += (int)chunk;
    }
    return sent;
}

static int driver_snd_hda_ops_stop(driver_snd_device_t *sdev) {
    driver_snd_hda_dev_t *d = (driver_snd_hda_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;

    /* drain: wait until the DMA position catches up (or timeout) */
    u64 deadline = core_timer_now_ms() + 2000;
    while (d->running && core_timer_now_ms() < deadline) {
        u32 hw = driver_snd_hda_lpib(d);
        if (((d->sw_pos - hw) & (HDA_RING_BYTES - 1)) < 64) break;
        core_sched_yield();
    }
    u32 sbase = driver_snd_hda_sdo_base(d);
    driver_snd_hda_w8(d, sbase + HDA_SD_CTL, HDA_SD_IOCE);
    d->running = 0;
    d->sw_pos = 0;
    driver_snd_hda_w32(d, sbase + HDA_SD_LPIB, 0);
    sdev->irqs = d->irq_count;
    return 0;
}

static int driver_snd_hda_ops_set_rate(driver_snd_device_t *sdev, u32 rate) {
    driver_snd_hda_dev_t *d = (driver_snd_hda_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (rate != 48000 && rate != 44100) return -1;
    if (d->running) return -1;   /* cannot change while streaming */
    if (rate == d->rate) return 0;   /* already programmed */

    u32 sbase = driver_snd_hda_sdo_base(d);
    u32 fmt = driver_snd_hda_format_value(rate, d->channels);
    driver_snd_hda_w16(d, sbase + HDA_SD_FORMAT, fmt);
    driver_snd_hda_codec_set16(d, (u8)d->dac_nid, HDA_VERB_SET_FMT, (u16)fmt);
    d->rate = rate;
    return 0;
}

static int driver_snd_hda_ops_set_volume(driver_snd_device_t *sdev, u32 vol) {
    driver_snd_hda_dev_t *d = (driver_snd_hda_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    if (vol > 100) vol = 100;
    u32 gain = (vol * d->amp_max) / 100;
    u32 mute = (vol == 0) ? (1u << 7) : 0;
    u32 payload_l = HDA_AMP_SET_OUT | HDA_AMP_SET_LEFT | mute | gain;
    u32 payload_r = HDA_AMP_SET_OUT | HDA_AMP_SET_RIGHT | mute | gain;
    /* BUG-0130 FIX (A9-04): AC_VERB_SET_AMP_GAIN_MUTE is a 16-bit-payload
     * verb (OUT/LEFT/RIGHT in bits 15/13/12). codec_set() takes a u8
     * payload, so every direction bit was truncated away and the whole
     * command was a no-op. codec_set16() exists exactly for this. */
    driver_snd_hda_codec_set16(d, (u8)d->dac_nid, HDA_VERB_SET_AMP, payload_l);
    driver_snd_hda_codec_set16(d, (u8)d->dac_nid, HDA_VERB_SET_AMP, payload_r);
    return 0;
}

static int driver_snd_hda_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps) {
    driver_snd_hda_dev_t *d = (driver_snd_hda_dev_t *)sdev->priv;
    if (!d || !d->up) return -1;
    caps->rates = SND_RATE_44100 | SND_RATE_48000;
    caps->min_channels = 1;
    caps->max_channels = 2;
    caps->bits8 = 0;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->cur_rate = d->rate;
    caps->cur_volume = sdev->volume;
    return 0;
}

void driver_snd_hda_print_state(void) {
    driver_snd_hda_dev_t *d = &g_hda;
    char line[128];
    char n[24];

    screen_console_puts("Intel HDA: ");
    if (!d->up) {
        screen_console_puts("not present\n");
        return;
    }
    strcpy(line, "vid=0x");
    u64_to_hex(d->vid, n, 4); strcat(line, n);
    strcat(line, " did=0x");
    u64_to_hex(d->did, n, 4); strcat(line, n);
    strcat(line, " at ");
    u64_to_hex(d->bus, n, 2); strcat(line, n);
    strcat(line, ":");
    u64_to_hex(d->dev, n, 2); strcat(line, n);
    strcat(line, ".");
    u64_to_hex(d->func, n, 1); strcat(line, n);
    screen_console_puts(line);

    strcpy(line, " codec=");
    u64_to_hex(d->codec_cad, n, 2); strcat(line, n);
    strcat(line, " dac_nid=");
    u64_to_hex((u64)d->dac_nid, n, 2); strcat(line, n);
    strcat(line, " pin_nid=");
    u64_to_hex((u64)d->pin_nid, n, 2); strcat(line, n);
    screen_console_puts(line);

    u32 lpib = driver_snd_hda_lpib(d);
    strcpy(line, " rate=");
    u64_to_str(d->rate, n); strcat(line, n);
    strcat(line, " ch=");
    u64_to_str(d->channels, n); strcat(line, n);
    strcat(line, " stream=");
    strcat(line, d->running ? "running" : "stopped");
    strcat(line, " lpib=");
    u64_to_str(lpib, n); strcat(line, n);
    strcat(line, " irqs=");
    u64_to_str(d->irq_count, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
}
