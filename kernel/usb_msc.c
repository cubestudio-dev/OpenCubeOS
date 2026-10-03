/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_msc.c
 * Purpose: USB Mass Storage Class (Bulk-Only Transport + SCSI) driver,
 *          registered with the blk layer so USB sticks/disks behave
 *          exactly like AHCI/NVMe/virtio drives: lsblk shows them,
 *          part_parse reads their MBR/GPT, fatmount mounts their
 *          FAT32 partitions.
 *
 * Wire format (BOT, usbmassbulk 1.0):
 *   CBW 31 bytes: sig "USBC" (0x43425355 LE), tag, data length,
 *                 flags (0x80 = IN), LUN, CB length, CB[16]
 *   data phase:   dCBWDataTransferLength bytes
 *   CSW 13 bytes: sig "USBS" (0x53425355), tag echo, residue,
 *                 status (0 PASS, 1 FAIL, 2 phase error)
 *
 * SCSI commands used: TEST_UNIT_READY, REQUEST_SENSE, INQUIRY,
 * READ_CAPACITY(10), READ(10), WRITE(10), SYNCHRONIZE_CACHE(10).
 * The data phase is chunked by the backend's ops->bulk_max so the
 * same driver runs over UHCI (512 B), OHCI (512 B), EHCI (4 KiB) and
 * XHCI (4 KiB) without caring about their buffer limits.
 *
 * QEMU: `-device usb-storage,drive=...` is a BOT + SPC disk with one
 * bulk IN (0x81) and one bulk OUT (0x01) endpoint.
 */
#include "usb_msc.h"
#include "usb.h"
#include "blk.h"
#include "console.h"
#include "string.h"
#include "timer.h"
#include "sched.h"

#define MSC_MAX_DEV 4

/* SCSI opcodes */
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_REQUEST_SENSE    0x03
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY    0x25
#define SCSI_READ10           0x28
#define SCSI_WRITE10          0x2a
#define SCSI_SYNCHRONIZE10    0x35

#define MSC_CBW_SIG   0x43425355u
#define MSC_CSW_SIG   0x53425355u

typedef struct usb_msc_dev {
    usb_dev_t *dev;
    usb_endpoint_t *ep_in, *ep_out;
    u32 tag;
    u8  lun;
    int up;
    int blk_idx;              /* blk layer index, -1 = not registered */
    u64 sectors;
    u32 block_size;
    char vendor[9];
    char product[17];
    u64 reads, writes, rd_sectors, wr_sectors;
    u64 stalls_recovered;
    volatile int busy;   /* a BOT transfer is in progress: the poll
                            thread must not inject TEST_UNIT_READY
                            into the middle of it (BOT is stateful:
                            exactly one command may be outstanding) */
} usb_msc_dev_t;

static usb_msc_dev_t g_msc[MSC_MAX_DEV];

static void msc_log(const char *s) { oc_console_puts(s); }

/* ------------------------------------------------------------------
 * low-level BOT
 * ------------------------------------------------------------------ */

static int msc_clear_halt(usb_msc_dev_t *m, u8 ep_addr) {
    usb_control(m->dev, 0x02, USB_REQ_CLEAR_FEAT, 0, ep_addr, NULL, 0);
    usb_tog_reset(m->dev, ep_addr);
    return 0;
}

/* one BOT command with an optional data phase; returns 0 on
 * CSW-PASS, -1 on error (stall handled inside) */
#define MSC_BOT_TIMEOUT_MS  5000   /* data-carrying commands */

static int msc_bot_to(usb_msc_dev_t *m, const u8 *cb, u8 cb_len,
                      u8 dir_in, void *data, u32 data_len,
                      u32 timeout_ms) {
    if (!m->up || !m->dev->present) return -1;
    /* atomic check-and-set: BOT is a stateful protocol, exactly one
     * command may be in flight; the poll thread's TEST_UNIT_READY and
     * block-layer I/O run on different kernel threads and the core
     * transfer mutex only spans individual bulk calls, not the whole
     * BOT sequence */
    if (__sync_lock_test_and_set(&m->busy, 1)) return -1;
    u8 cbw[31];
    u8 csw[13];
    oc_memset(cbw, 0, sizeof(cbw));
    u32 tag = ++m->tag;
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;
    cbw[4] = (u8)tag; cbw[5] = (u8)(tag >> 8);
    cbw[6] = (u8)(tag >> 16); cbw[7] = (u8)(tag >> 24);
    cbw[8] = (u8)data_len; cbw[9] = (u8)(data_len >> 8);
    cbw[10] = (u8)(data_len >> 16); cbw[11] = (u8)(data_len >> 24);
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = m->lun;
    cbw[14] = cb_len;
    oc_memcpy(cbw + 15, cb, cb_len);

    u16 chunk_max = m->dev->host && m->dev->host->ops &&
                    m->dev->host->ops->bulk_max
                        ? m->dev->host->ops->bulk_max : 512;
    if (chunk_max == 0) chunk_max = 512;

    int rc = usb_bulk_transfer_timeout(m->dev, m->ep_out->addr, cbw,
                                       31, timeout_ms);
    if (rc != 31) {
        oc_console_puts("msc: CBW bulk out failed\n");
        if (rc == OC_USB_ESTALL) {
            msc_clear_halt(m, m->ep_out->addr);
            m->stalls_recovered++;
        }
        m->busy = 0;
        return -1;
    }

    /* data phase (chunked) */
    u32 done = 0;
    int data_err = 0;
    while (done < data_len) {
        u16 chunk = (u16)(data_len - done);
        if (chunk > chunk_max) chunk = chunk_max;
        rc = usb_bulk_transfer_timeout(m->dev,
                               dir_in ? m->ep_in->addr : m->ep_out->addr,
                               (u8 *)data + done, chunk, timeout_ms);
        if (rc < 0) { data_err = rc; break; }
        if (rc == 0) { data_err = -1; break; }
        done += (u32)rc;
        if ((u32)rc < (u32)chunk) break;   /* short packet */
    }
    if (data_err == OC_USB_ESTALL) {
        msc_clear_halt(m, dir_in ? m->ep_in->addr : m->ep_out->addr);
        m->stalls_recovered++;
    }
    if (data_err && data_len) {
        oc_console_puts("msc: data phase failed\n");
        m->busy = 0;
        return -1;
    }

    /* CSW (retry: once after a stall, once more after a timeout -
     * some devices complete the SCSI command asynchronously and are
     * not ready to hand out the CSW on the first attempt) */
    rc = usb_bulk_transfer_timeout(m->dev, m->ep_in->addr, csw, 13,
                                   timeout_ms);
    if (rc != 13) {
        if (rc == OC_USB_ESTALL) {
            msc_clear_halt(m, m->ep_in->addr);
            m->stalls_recovered++;
            rc = usb_bulk_transfer_timeout(m->dev, m->ep_in->addr,
                                           csw, 13, timeout_ms);
        } else {
            for (volatile int d = 0; d < 40000; d++) { }
            rc = usb_bulk_transfer_timeout(m->dev, m->ep_in->addr,
                                           csw, 13, 500);
        }
        if (rc != 13) {
            char l[64]; char n[12];
            oc_strcpy(l, "msc: CSW bulk in failed rc=");
            oc_u64_to_str((u64)(rc < 0 ? -rc : rc), n); oc_strcat(l, n);
            oc_strcat(l, "\n");
            oc_console_puts(l);
            m->busy = 0;
            return -1;
        }
    }
    u32 sig = (u32)csw[0] | ((u32)csw[1] << 8) | ((u32)csw[2] << 16) |
              ((u32)csw[3] << 24);
    u32 rtag = (u32)csw[4] | ((u32)csw[5] << 8) | ((u32)csw[6] << 16) |
               ((u32)csw[7] << 24);
    u8 status = csw[12];
    m->busy = 0;
    if (sig != MSC_CSW_SIG || rtag != tag) {
        oc_console_puts("msc: CSW bad signature or tag\n");
        return -1;
    }
    if (status != 0) {
        /* command failed: fetch sense to clear the condition.  busy
         * is already cleared here; the sense BOT manages its own busy
         * window (a nested call with busy=1 would bail instantly and
         * leave the flag stuck forever). */
        u8 sense_cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
        u8 sense[18];
        oc_memset(sense, 0, sizeof(sense));
        msc_bot_to(m, sense_cb, 6, 1, sense, 18, MSC_BOT_TIMEOUT_MS);
        return -1;
    }
    if (data_err && data_len) return -1;
    return 0;
}

/* ------------------------------------------------------------------
 * SCSI wrappers
 * ------------------------------------------------------------------ */

static int msc_bot(usb_msc_dev_t *m, const u8 *cb, u8 cb_len,
                   u8 dir_in, void *data, u32 data_len) {
    return msc_bot_to(m, cb, cb_len, dir_in, data, data_len,
                      MSC_BOT_TIMEOUT_MS);
}

/* TEST_UNIT_READY used by the poll thread: SHORT timeout so the busy
 * window (which blocks the block layer) stays tiny under load. */
static int msc_test_unit_ready(usb_msc_dev_t *m) {
    u8 cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_bot_to(m, cb, 6, 0, NULL, 0, 200);
}

static int msc_inquiry(usb_msc_dev_t *m) {
    u8 cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    u8 r[36];
    oc_memset(r, 0, sizeof(r));
    if (msc_bot(m, cb, 6, 1, r, 36) != 0) return -1;
    for (int i = 0; i < 8; i++) {
        char c = (char)r[8 + i];
        m->vendor[i] = (c >= 0x20 && c < 0x7f) ? c : ' ';
    }
    m->vendor[8] = 0;
    for (int i = 0; i < 16; i++) {
        char c = (char)r[16 + i];
        m->product[i] = (c >= 0x20 && c < 0x7f) ? c : ' ';
    }
    m->product[16] = 0;
    return 0;
}

static int msc_read_capacity(usb_msc_dev_t *m) {
    u8 cb[10] = { SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    u8 r[8];
    oc_memset(r, 0, sizeof(r));
    if (msc_bot(m, cb, 10, 1, r, 8) != 0) return -1;
    u32 last = (u32)r[0] << 24 | (u32)r[1] << 16 | (u32)r[2] << 8 |
               (u32)r[3];
    u32 bsize = (u32)r[4] << 24 | (u32)r[5] << 16 | (u32)r[6] << 8 |
                (u32)r[7];
    if (bsize == 0) bsize = 512;
    m->sectors = (u64)last + 1;
    m->block_size = bsize;
    return 0;
}

/* Sectors per BOT data phase: mainstream sizing for USB storage. */
#define MSC_RW_MAX_SECTORS 8

/* mainstream BOT recovery (usbmassbulk 1.0 §5.3.4): the class-specific
 * Mass Storage Reset request re-initialises the device's BOT state
 * machine, then both bulk endpoints are cleared (which also resets the
 * device-side data toggles to DATA0, matching usb_tog_reset here).
 * Used when a transfer keeps failing - a single lost packet would
 * otherwise desync the data toggles forever. */
static int msc_reset_recovery(usb_msc_dev_t *m) {
    usb_control(m->dev, 0x21, 0xFF, 0, m->dev->ifs[0].number,
                NULL, 0);
    msc_clear_halt(m, m->ep_out->addr);
    msc_clear_halt(m, m->ep_in->addr);
    m->stalls_recovered++;
    return 0;
}

static int msc_rw10(usb_msc_dev_t *m, int write, u64 lba, u32 count,
                    void *buf) {
    /* Split into <=128-sector BOTs; on a failed sub-transfer reset
     * both bulk endpoints (CLEAR_FEATURE(ENDPOINT_HALT) restores the
     * device's data toggles to DATA0, matching usb_tog_reset on our
     * side) and resume the REMAINING portion - standard BOT error
     * recovery, no data is given up on a hiccup mid-transfer. */
    u32 done = 0;
    while (done < count) {
        u32 chunk = count - done;
        if (chunk > MSC_RW_MAX_SECTORS) chunk = MSC_RW_MAX_SECTORS;
        u8 cb[10];
        oc_memset(cb, 0, sizeof(cb));
        cb[0] = write ? SCSI_WRITE10 : SCSI_READ10;
        cb[2] = (u8)(lba >> 24); cb[3] = (u8)(lba >> 16);
        cb[4] = (u8)(lba >> 8);  cb[5] = (u8)lba;
        cb[7] = (u8)(chunk >> 8); cb[8] = (u8)chunk;
        u32 bytes = chunk * m->block_size;
        int ok = 0;
        int last_rc = 0;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            /* the poll thread's short TEST_UNIT_READY holds the busy
             * window for up to a few hundred ms; block-layer callers
             * wait for it instead of failing instantly */
            if (m->busy) {
                u64 idle_deadline = oc_timer_now_ms() + 800;
                while (m->busy && oc_timer_now_ms() < idle_deadline)
                    sched_yield();
            }
            last_rc = msc_bot(m, cb, 10, write ? 0 : 1,
                              (u8 *)buf + (u64)done * m->block_size,
                              bytes);
            if (last_rc == 0) {
                ok = 1;
                break;
            }
            /* CLEAR_FEATURE(ENDPOINT_HALT) only for real stalls: the
             * device resets ITS data toggle to DATA0 there.  Clearing
             * a halted endpoint after a transient timeout would reset
             * our toggle while the device keeps its own - permanent
             * desync. */
            if (last_rc == OC_USB_ESTALL) {
                msc_clear_halt(m, m->ep_out->addr);
                msc_clear_halt(m, m->ep_in->addr);
                m->stalls_recovered++;
            }
        }
        if (!ok) {
            /* last resort: full BOT reset + toggles re-synced, then
             * one final attempt of this sub-transfer */
            msc_reset_recovery(m);
            if (msc_bot(m, cb, 10, write ? 0 : 1,
                        (u8 *)buf + (u64)done * m->block_size,
                        bytes) == 0) {
                ok = 1;
            } else {
                return -1;
            }
        }
        if (write) { m->writes++; m->wr_sectors += chunk; }
        else { m->reads++; m->rd_sectors += chunk; }
        done += chunk;
        lba += chunk;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * blk layer glue
 * ------------------------------------------------------------------ */

static int msc_blk_read(blk_device_t *dev, u64 lba, u32 count,
                        void *buf) {
    usb_msc_dev_t *m = (usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    if (lba + count > m->sectors) return -1;
    return msc_rw10(m, 0, lba, count, buf);
}

static int msc_blk_write(blk_device_t *dev, u64 lba, u32 count,
                         const void *buf) {
    usb_msc_dev_t *m = (usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    if (lba + count > m->sectors) return -1;
    return msc_rw10(m, 1, lba, count, (void *)buf);
}

static int msc_blk_flush(blk_device_t *dev) {
    usb_msc_dev_t *m = (usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    u8 cb[10] = { SCSI_SYNCHRONIZE10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    return msc_bot(m, cb, 10, 0, NULL, 0);
}

static const blk_ops_t msc_blk_ops = {
    .read  = msc_blk_read,
    .write = msc_blk_write,
    .flush = msc_blk_flush,
};

static void msc_register_blk(usb_msc_dev_t *m) {
    blk_device_t bd;
    oc_memset(&bd, 0, sizeof(bd));
    /* "usda", "usdb", ... - USB SCSI disk */
    bd.name[0] = 'u'; bd.name[1] = 's'; bd.name[2] = 'd';
    bd.name[3] = (char)('a' + m->lun);   /* per-device letter */
    bd.name[4] = 0;
    bd.type = BLK_TYPE_USB;
    bd.sectors = m->sectors;
    bd.sector_size = 512;
    bd.present = 1;
    bd.priv = m;
    int idx = blk_register(&bd, &msc_blk_ops);
    m->blk_idx = idx;
    char line[96];
    char n[24];
    oc_strcpy(line, "usb-msc: registered block device ");
    oc_strcat(line, bd.name);
    oc_strcat(line, " (");
    oc_u64_to_str(m->sectors, n); oc_strcat(line, n);
    oc_strcat(line, " sectors, ");
    oc_strcat(line, m->vendor);
    oc_strcat(line, " ");
    oc_strcat(line, m->product);
    oc_strcat(line, ")\n");
    oc_console_puts(line);
}

/* ------------------------------------------------------------------
 * class-driver probe
 * ------------------------------------------------------------------ */

static int msc_probe(usb_dev_t *dev) {
    for (int i = 0; i < MSC_MAX_DEV; i++)
        if (g_msc[i].up && g_msc[i].dev == dev) return 0;
    usb_interface_t *ifp = usb_find_if(dev, USB_CLASS_MSC, 0x06, 0x50, 0);
    if (!ifp) return -1;
    usb_endpoint_t *ep_in = usb_find_ep(dev, ifp->number,
                                        USB_EP_ATTR_BULK, 1);
    usb_endpoint_t *ep_out = usb_find_ep(dev, ifp->number,
                                         USB_EP_ATTR_BULK, 0);
    if (!ep_in || !ep_out) return -1;

    for (int i = 0; i < MSC_MAX_DEV; i++) {
        usb_msc_dev_t *m = &g_msc[i];
        if (m->up) continue;
        oc_memset(m, 0, sizeof(*m));
        m->dev = dev;
        m->ep_in = ep_in;
        m->ep_out = ep_out;
        m->blk_idx = -1;
        m->up = 1;   /* msc_bot refuses transfers while !up */
        /* wait for the device to become ready (spinning media) */
        int ready = 0;
        for (int t = 0; t < 20; t++) {
            if (msc_test_unit_ready(m) == 0) { ready = 1; break; }
            for (volatile int d = 0; d < 20000; d++) { }
        }
        if (!ready) {
            msc_log("usb-msc: device not ready\n");
            m->up = 0;
            return -1;
        }
        if (msc_inquiry(m) != 0) {
            m->up = 0;
            msc_log("usb-msc: inquiry failed\n");
            return -1;
        }
        if (msc_read_capacity(m) != 0) {
            m->up = 0;
            msc_log("usb-msc: read capacity failed\n");
            return -1;
        }
        msc_register_blk(m);
        return 0;
    }
    return -1;
}

static void msc_disconnect(usb_dev_t *dev) {
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        usb_msc_dev_t *m = &g_msc[i];
        if (m->up && m->dev == dev) {
            if (m->blk_idx >= 0) blk_unregister_device(m->blk_idx);
            oc_memset(m, 0, sizeof(*m));
            msc_log("usb-msc: drive removed\n");
        }
    }
}

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

void usb_msc_poll(void) {
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        usb_msc_dev_t *m = &g_msc[i];
        if (!m->up) continue;
        if (!m->dev->present) continue;
        msc_test_unit_ready(m);   /* keep sense state clean */
    }
}

int usb_msc_num_devices(void) {
    int n = 0;
    for (int i = 0; i < MSC_MAX_DEV; i++)
        if (g_msc[i].up) n++;
    return n;
}

void usb_msc_print_state(void) {
    char line[128]; char n[24];
    int any = 0;
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        usb_msc_dev_t *m = &g_msc[i];
        if (!m->up) continue;
        any = 1;
        oc_strcpy(line, "  msc: ");
        oc_strcat(line, m->vendor);
        oc_strcat(line, " ");
        oc_strcat(line, m->product);
        oc_strcat(line, " sectors=");
        oc_u64_to_str(m->sectors, n); oc_strcat(line, n);
        oc_strcat(line, " block=");
        oc_u64_to_str(m->block_size, n); oc_strcat(line, n);
        oc_strcat(line, " reads=");
        oc_u64_to_str(m->reads, n); oc_strcat(line, n);
        oc_strcat(line, " writes=");
        oc_u64_to_str(m->writes, n); oc_strcat(line, n);
        oc_strcat(line, " stalls_rec=");
        oc_u64_to_str(m->stalls_recovered, n); oc_strcat(line, n);
        oc_console_puts(line);
        oc_console_puts("\n");
    }
    if (!any) oc_console_puts("  msc: none\n");
}

int usb_msc_init(void) {
    return usb_register_driver("usb-msc", USB_CLASS_MSC,
                               msc_probe, msc_disconnect);
}
