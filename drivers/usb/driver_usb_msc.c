/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_msc.c
 * Purpose: USB Mass Storage Class (Bulk-Only Transport + SCSI) driver,
 *          registered with the blk layer so USB sticks/disks behave
 *          exactly like AHCI/NVMe/virtio drives: lsblk shows them,
 *          driver_block_part_parse reads their MBR/GPT, fatmount mounts their
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
#include "driver_usb_msc.h"
#include "driver_usb.h"
#include "driver_block_blk.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"
#include "core_sched.h"

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

typedef struct driver_usb_msc_dev {
    driver_usb_dev_t *dev;
    driver_usb_endpoint_t *ep_in, *ep_out;
    u32 tag;
    u8  lun;
    u8  instance;   /* BUG-0168: global MSC device ordinal for naming */
    int up;
    int driver_block_idx;              /* blk layer index, -1 = not registered */
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
} driver_usb_msc_dev_t;

static driver_usb_msc_dev_t g_msc[MSC_MAX_DEV];
static u8 g_msc_instances;   /* BUG-0168: global instance counter */

static void driver_usb_msc_log(const char *s) { screen_console_puts(s); }

/* ------------------------------------------------------------------
 * low-level BOT
 * ------------------------------------------------------------------ */

static int driver_usb_msc_clear_halt(driver_usb_msc_dev_t *m, u8 ep_addr) {
    driver_usb_control(m->dev, 0x02, USB_REQ_CLEAR_FEAT, 0, ep_addr, NULL, 0);
    driver_usb_tog_reset(m->dev, ep_addr);
    return 0;
}

/* one BOT command with an optional data phase; returns 0 on
 * CSW-PASS, -1 on error (stall handled inside) */
#define MSC_BOT_TIMEOUT_MS  5000   /* data-carrying commands */

/* BUG-0167 FIX (A11-37): the busy window is split from the wire
 * sequence. driver_usb_msc_bot_to owns the atomic check-and-set on
 * m->busy; driver_usb_msc_bot_locked runs the actual CBW/data/CSW -
 * and, on a failed command, its REQUEST_SENSE - with busy already
 * held, so no other thread can start a BOT between a failed command
 * and its sense. */
static int driver_usb_msc_bot_locked(driver_usb_msc_dev_t *m, const u8 *cb,
                      u8 cb_len, u8 dir_in, void *data, u32 data_len,
                      u32 timeout_ms);

static int driver_usb_msc_bot_to(driver_usb_msc_dev_t *m, const u8 *cb, u8 cb_len,
                      u8 dir_in, void *data, u32 data_len,
                      u32 timeout_ms) {
    if (!m->up || !m->dev->present) return -1;
    /* atomic check-and-set: BOT is a stateful protocol, exactly one
     * command may be in flight; the poll thread's TEST_UNIT_READY and
     * block-layer I/O run on different kernel threads and the core
     * transfer mutex only spans individual bulk calls, not the whole
     * BOT sequence */
    if (__sync_lock_test_and_set(&m->busy, 1)) return -1;
    int rc = driver_usb_msc_bot_locked(m, cb, cb_len, dir_in, data,
                            data_len, timeout_ms);
    m->busy = 0;
    return rc;
}

/* BOT wire sequence only; the caller MUST already hold m->busy */
static int driver_usb_msc_bot_locked(driver_usb_msc_dev_t *m, const u8 *cb, u8 cb_len,
                      u8 dir_in, void *data, u32 data_len,
                      u32 timeout_ms) {
    u8 cbw[31];
    u8 csw[13];
    memset(cbw, 0, sizeof(cbw));
    u32 tag = ++m->tag;
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;
    cbw[4] = (u8)tag; cbw[5] = (u8)(tag >> 8);
    cbw[6] = (u8)(tag >> 16); cbw[7] = (u8)(tag >> 24);
    cbw[8] = (u8)data_len; cbw[9] = (u8)(data_len >> 8);
    cbw[10] = (u8)(data_len >> 16); cbw[11] = (u8)(data_len >> 24);
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = m->lun;
    cbw[14] = cb_len;
    memcpy(cbw + 15, cb, cb_len);

    u16 chunk_max = m->dev->host && m->dev->host->ops &&
                    m->dev->host->ops->bulk_max
                        ? m->dev->host->ops->bulk_max : 512;
    if (chunk_max == 0) chunk_max = 512;

    int rc = driver_usb_bulk_transfer_timeout(m->dev, m->ep_out->addr, cbw,
                                       31, timeout_ms);
    if (rc != 31) {
        screen_console_puts("msc: CBW bulk out failed\n");
        if (rc == OC_USB_ESTALL) {
            driver_usb_msc_clear_halt(m, m->ep_out->addr);
            m->stalls_recovered++;
        }
        return -1;
    }

    /* data phase (chunked) */
    u32 done = 0;
    int data_err = 0;
    while (done < data_len) {
        u16 chunk = (u16)(data_len - done);
        if (chunk > chunk_max) chunk = chunk_max;
        rc = driver_usb_bulk_transfer_timeout(m->dev,
                               dir_in ? m->ep_in->addr : m->ep_out->addr,
                               (u8 *)data + done, chunk, timeout_ms);
        if (rc < 0) { data_err = rc; break; }
        if (rc == 0) { data_err = -1; break; }
        done += (u32)rc;
        if ((u32)rc < (u32)chunk) break;   /* short packet */
    }
    if (data_err == OC_USB_ESTALL) {
        driver_usb_msc_clear_halt(m, dir_in ? m->ep_in->addr : m->ep_out->addr);
        m->stalls_recovered++;
    }
    if (data_err && data_len) {
        screen_console_puts("msc: data phase failed\n");
        return -1;
    }

    /* CSW (retry: once after a stall, once more after a timeout -
     * some devices complete the SCSI command asynchronously and are
     * not ready to hand out the CSW on the first attempt) */
    rc = driver_usb_bulk_transfer_timeout(m->dev, m->ep_in->addr, csw, 13,
                                   timeout_ms);
    if (rc != 13) {
        if (rc == OC_USB_ESTALL) {
            driver_usb_msc_clear_halt(m, m->ep_in->addr);
            m->stalls_recovered++;
            rc = driver_usb_bulk_transfer_timeout(m->dev, m->ep_in->addr,
                                           csw, 13, timeout_ms);
        } else {
            for (volatile int d = 0; d < 40000; d++) { }
            rc = driver_usb_bulk_transfer_timeout(m->dev, m->ep_in->addr,
                                           csw, 13, 500);
        }
        if (rc != 13) {
            char l[64]; char n[12];
            strcpy(l, "msc: CSW bulk in failed rc=");
            u64_to_str((u64)(rc < 0 ? -rc : rc), n); strcat(l, n);
            strcat(l, "\n");
            screen_console_puts(l);
            return -1;
        }
    }
    u32 sig = (u32)csw[0] | ((u32)csw[1] << 8) | ((u32)csw[2] << 16) |
              ((u32)csw[3] << 24);
    u32 rtag = (u32)csw[4] | ((u32)csw[5] << 8) | ((u32)csw[6] << 16) |
               ((u32)csw[7] << 24);
    u32 res = (u32)csw[8] | ((u32)csw[9] << 8) | ((u32)csw[10] << 16) |
              ((u32)csw[11] << 24);
    u8 status = csw[12];
    if (sig != MSC_CSW_SIG || rtag != tag) {
        screen_console_puts("msc: CSW bad signature or tag\n");
        return -1;
    }
    /* BUG-0053 FIX: validate dCBWDataResidue. The data phase used to
     * treat a short IN packet as a clean early exit, and the CSW
     * residue field was never read - so a device (or an attacker) that
     * delivered less data than the command asked for still reported
     * CSW status PASS and the caller happily used a partially-filled
     * buffer (silent data corruption on reads). Now: on a short IN
     * transfer the missing bytes must equal the CSW residue, and any
     * shortfall on a read is an error, never a success. */
    if (data_len) {
        u32 expected = data_len - done;   /* OUT: done == data_len -> 0 */
        if (res != expected || done < data_len) {
            char l[96]; char n[12]; char n2[12];
            strcpy(l, "msc: short data phase (got ");
            u64_to_str(done, n); strcat(l, n);
            strcat(l, " of ");
            u64_to_str(data_len, n2); strcat(l, n2);
            strcat(l, ", residue ");
            u64_to_str(res, n); strcat(l, n);
            strcat(l, ")\n");
            screen_console_puts(l);
            return -1;
        }
    }
    if (status != 0) {
        /* command failed: fetch sense to clear the condition.
         * BUG-0167 FIX (A11-37): a BOT device processes exactly ONE bulk
         * command at a time. The old code cleared busy BEFORE issuing
         * REQUEST_SENSE, so a concurrent block-layer I/O could slip a
         * new CBW in between the failed command's CSW and the sense and
         * interleave the two on the device's state machine. The sense
         * now runs INSIDE the same busy window: this wire function
         * re-enters itself directly, bypassing the busy check-and-set
         * of driver_usb_msc_bot_to (which would instantly bail - and
         * which is exactly what competing threads see: a transient -1
         * and a block-layer retry, never wire interleaving). Nothing
         * waits on busy while holding it, so the window cannot
         * deadlock. Sense-of-sense is skipped: a REQUEST_SENSE that
         * itself reports an error would otherwise recurse unboundedly. */
        u8 sense_cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
        u8 sense[18];
        memset(sense, 0, sizeof(sense));
        if (cb[0] != SCSI_REQUEST_SENSE)
            (void)driver_usb_msc_bot_locked(m, sense_cb, 6, 1, sense, 18,
                                MSC_BOT_TIMEOUT_MS);
        return -1;
    }
    if (data_err && data_len) return -1;
    return 0;
}

/* ------------------------------------------------------------------
 * SCSI wrappers
 * ------------------------------------------------------------------ */

static int driver_usb_msc_bot(driver_usb_msc_dev_t *m, const u8 *cb, u8 cb_len,
                   u8 dir_in, void *data, u32 data_len) {
    return driver_usb_msc_bot_to(m, cb, cb_len, dir_in, data, data_len,
                      MSC_BOT_TIMEOUT_MS);
}

/* TEST_UNIT_READY used by the poll thread: SHORT timeout so the busy
 * window (which blocks the block layer) stays tiny under load. */
static int driver_usb_msc_test_unit_ready(driver_usb_msc_dev_t *m) {
    u8 cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return driver_usb_msc_bot_to(m, cb, 6, 0, NULL, 0, 200);
}

static int driver_usb_msc_inquiry(driver_usb_msc_dev_t *m) {
    u8 cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    u8 r[36];
    memset(r, 0, sizeof(r));
    if (driver_usb_msc_bot(m, cb, 6, 1, r, 36) != 0) return -1;
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

static int driver_usb_msc_read_capacity(driver_usb_msc_dev_t *m) {
    u8 cb[10] = { SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    u8 r[8];
    memset(r, 0, sizeof(r));
    if (driver_usb_msc_bot(m, cb, 10, 1, r, 8) != 0) return -1;
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
 * device-side data toggles to DATA0, matching driver_usb_tog_reset here).
 * Used when a transfer keeps failing - a single lost packet would
 * otherwise desync the data toggles forever. */
static int driver_usb_msc_reset_recovery(driver_usb_msc_dev_t *m) {
    driver_usb_control(m->dev, 0x21, 0xFF, 0, m->dev->ifs[0].number,
                NULL, 0);
    driver_usb_msc_clear_halt(m, m->ep_out->addr);
    driver_usb_msc_clear_halt(m, m->ep_in->addr);
    m->stalls_recovered++;
    return 0;
}

static int driver_usb_msc_rw10(driver_usb_msc_dev_t *m, int write, u64 lba, u32 count,
                    void *buf) {
    /* Split into <=128-sector BOTs; on a failed sub-transfer reset
     * both bulk endpoints (CLEAR_FEATURE(ENDPOINT_HALT) restores the
     * device's data toggles to DATA0, matching driver_usb_tog_reset on our
     * side) and resume the REMAINING portion - standard BOT error
     * recovery, no data is given up on a hiccup mid-transfer. */
    u32 done = 0;
    while (done < count) {
        u32 chunk = count - done;
        if (chunk > MSC_RW_MAX_SECTORS) chunk = MSC_RW_MAX_SECTORS;
        u8 cb[10];
        memset(cb, 0, sizeof(cb));
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
                u64 idle_deadline = core_timer_now_ms() + 800;
                while (m->busy && core_timer_now_ms() < idle_deadline)
                    core_sched_yield();
            }
            last_rc = driver_usb_msc_bot(m, cb, 10, write ? 0 : 1,
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
                driver_usb_msc_clear_halt(m, m->ep_out->addr);
                driver_usb_msc_clear_halt(m, m->ep_in->addr);
                m->stalls_recovered++;
            }
        }
        if (!ok) {
            /* last resort: full BOT reset + toggles re-synced, then
             * one final attempt of this sub-transfer. BUG-0167
             * corollary: a Mass Storage Reset aborts whatever BOT
             * command the device is executing, so it must not fire
             * under another thread's in-flight command (e.g. an
             * error-recovery REQUEST_SENSE). Take the same busy window
             * the BOT path uses; if it is held, give up on this
             * transfer (the block layer retries) instead of stomping
             * the other command. */
            if (__sync_lock_test_and_set(&m->busy, 1)) return -1;
            driver_usb_msc_reset_recovery(m);
            m->busy = 0;
            if (driver_usb_msc_bot(m, cb, 10, write ? 0 : 1,
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

static int driver_usb_msc_blk_read(driver_block_device_t *dev, u64 lba, u32 count,
                        void *buf) {
    driver_usb_msc_dev_t *m = (driver_usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    if (lba + count > m->sectors) return -1;
    return driver_usb_msc_rw10(m, 0, lba, count, buf);
}

static int driver_usb_msc_blk_write(driver_block_device_t *dev, u64 lba, u32 count,
                         const void *buf) {
    driver_usb_msc_dev_t *m = (driver_usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    if (lba + count > m->sectors) return -1;
    return driver_usb_msc_rw10(m, 1, lba, count, (void *)buf);
}

static int driver_usb_msc_blk_flush(driver_block_device_t *dev) {
    driver_usb_msc_dev_t *m = (driver_usb_msc_dev_t *)dev->priv;
    if (!m || !m->up) return -1;
    u8 cb[10] = { SCSI_SYNCHRONIZE10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    return driver_usb_msc_bot(m, cb, 10, 0, NULL, 0);
}

static const driver_block_ops_t driver_usb_msc_blk_ops = {
    .read  = driver_usb_msc_blk_read,
    .write = driver_usb_msc_blk_write,
    .flush = driver_usb_msc_blk_flush,
};

static void driver_usb_msc_register_blk(driver_usb_msc_dev_t *m) {
    driver_block_device_t bd;
    memset(&bd, 0, sizeof(bd));
    /* "usda", "usdb", ... - USB SCSI disk */
    bd.name[0] = 'u'; bd.name[1] = 's'; bd.name[2] = 'd';
    /* BUG-0168 FIX (A11-38): name by global instance count, not LUN.
     * m->lun is always 0 (multi-LUN devices are not yet driven), so
     * every stick registered as "usda" and a second device collided
     * (driver_block_register does not reject duplicate names). The
     * letter is kept inside 'a'..'z' even after the monotonic counter
     * wraps the alphabet; the probe rejects letters still owned by a
     * live device, so registered names stay unique. */
    bd.name[3] = (char)('a' + (m->instance % 26));
    bd.name[4] = 0;
    bd.type = BLK_TYPE_USB;
    bd.sectors = m->sectors;
    /* BUG-0054 FIX: report the device's real block size from
     * READ_CAPACITY instead of a hardcoded 512. The old line decoupled
     * the block layer from the BOT/SCSI layer (which already uses
     * m->block_size everywhere), so a device with 2048/4096-byte
     * blocks got its LBAs and capacity interpreted at the wrong
     * granularity - reads/writes landed on the wrong offsets. */
    bd.sector_size = m->block_size;
    bd.present = 1;
    bd.priv = m;
    int idx = driver_block_register(&bd, &driver_usb_msc_blk_ops);
    m->driver_block_idx = idx;
    char line[96];
    char n[24];
    strcpy(line, "usb-msc: registered block device ");
    strcat(line, bd.name);
    strcat(line, " (");
    u64_to_str(m->sectors, n); strcat(line, n);
    strcat(line, " sectors, ");
    strcat(line, m->vendor);
    strcat(line, " ");
    strcat(line, m->product);
    strcat(line, ")\n");
    screen_console_puts(line);
}

/* ------------------------------------------------------------------
 * class-driver probe
 * ------------------------------------------------------------------ */

static int driver_usb_msc_probe(driver_usb_dev_t *dev) {
    for (int i = 0; i < MSC_MAX_DEV; i++)
        if (g_msc[i].up && g_msc[i].dev == dev) return 0;
    driver_usb_interface_t *ifp = driver_usb_find_if(dev, USB_CLASS_MSC, 0x06, 0x50, 0);
    if (!ifp) return -1;
    driver_usb_endpoint_t *ep_in = driver_usb_find_ep(dev, ifp->number,
                                        USB_EP_ATTR_BULK, 1);
    driver_usb_endpoint_t *ep_out = driver_usb_find_ep(dev, ifp->number,
                                         USB_EP_ATTR_BULK, 0);
    if (!ep_in || !ep_out) return -1;

    for (int i = 0; i < MSC_MAX_DEV; i++) {
        driver_usb_msc_dev_t *m = &g_msc[i];
        if (m->up) continue;
        memset(m, 0, sizeof(*m));
        m->dev = dev;
        m->ep_in = ep_in;
        m->ep_out = ep_out;
        m->driver_block_idx = -1;
        /* BUG-0168 FIX (A11-38): per-instance block device letter from
         * the global instance counter (first device = "usda"). The
         * counter is monotonic across hotplug events; skip letters
         * still owned by a live device so two concurrent drives can
         * never register the same name even after the counter wraps. */
        m->instance = g_msc_instances++;
        for (;;) {
            char letter = (char)('a' + (m->instance % 26));
            int clash = 0;
            for (int j = 0; j < MSC_MAX_DEV; j++) {
                if (&g_msc[j] != m && g_msc[j].up &&
                    (char)('a' + (g_msc[j].instance % 26)) == letter) {
                    clash = 1;
                    break;
                }
            }
            if (!clash) break;
            m->instance = g_msc_instances++;
        }
        m->up = 1;   /* driver_usb_msc_bot refuses transfers while !up */
        /* wait for the device to become ready (spinning media) */
        int ready = 0;
        for (int t = 0; t < 20; t++) {
            if (driver_usb_msc_test_unit_ready(m) == 0) { ready = 1; break; }
            for (volatile int d = 0; d < 20000; d++) { }
        }
        if (!ready) {
            driver_usb_msc_log("usb-msc: device not ready\n");
            m->up = 0;
            return -1;
        }
        if (driver_usb_msc_inquiry(m) != 0) {
            m->up = 0;
            driver_usb_msc_log("usb-msc: inquiry failed\n");
            return -1;
        }
        if (driver_usb_msc_read_capacity(m) != 0) {
            m->up = 0;
            driver_usb_msc_log("usb-msc: read capacity failed\n");
            return -1;
        }
        driver_usb_msc_register_blk(m);
        return 0;
    }
    return -1;
}

static void driver_usb_msc_disconnect(driver_usb_dev_t *dev) {
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        driver_usb_msc_dev_t *m = &g_msc[i];
        if (m->up && m->dev == dev) {
            if (m->driver_block_idx >= 0) driver_block_unregister_device(m->driver_block_idx);
            memset(m, 0, sizeof(*m));
            driver_usb_msc_log("usb-msc: drive removed\n");
        }
    }
}

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

void driver_usb_msc_poll(void) {
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        driver_usb_msc_dev_t *m = &g_msc[i];
        if (!m->up) continue;
        if (!m->dev->present) continue;
        driver_usb_msc_test_unit_ready(m);   /* keep sense state clean */
    }
}

int driver_usb_msc_num_devices(void) {
    int n = 0;
    for (int i = 0; i < MSC_MAX_DEV; i++)
        if (g_msc[i].up) n++;
    return n;
}

void driver_usb_msc_print_state(void) {
    char line[128]; char n[24];
    int any = 0;
    for (int i = 0; i < MSC_MAX_DEV; i++) {
        driver_usb_msc_dev_t *m = &g_msc[i];
        if (!m->up) continue;
        any = 1;
        strcpy(line, "  msc: ");
        strcat(line, m->vendor);
        strcat(line, " ");
        strcat(line, m->product);
        strcat(line, " sectors=");
        u64_to_str(m->sectors, n); strcat(line, n);
        strcat(line, " block=");
        u64_to_str(m->block_size, n); strcat(line, n);
        strcat(line, " reads=");
        u64_to_str(m->reads, n); strcat(line, n);
        strcat(line, " writes=");
        u64_to_str(m->writes, n); strcat(line, n);
        strcat(line, " stalls_rec=");
        u64_to_str(m->stalls_recovered, n); strcat(line, n);
        screen_console_puts(line);
        screen_console_puts("\n");
    }
    if (!any) screen_console_puts("  msc: none\n");
}

int driver_usb_msc_init(void) {
    return driver_usb_register_driver("usb-msc", USB_CLASS_MSC,
                               driver_usb_msc_probe, driver_usb_msc_disconnect);
}
