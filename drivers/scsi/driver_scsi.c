/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/scsi/driver_scsi.c
 *
 * SCSI subsystem core.  Implements the SAM-style command model on
 * top of any registered HBA backend:
 *
 *   - CDB builders:  TEST UNIT READY / INQUIRY / REQUEST SENSE /
 *     READ CAPACITY(10|16) / READ(10,16) / WRITE(10,16) /
 *     MODE SENSE(6) / SYNCHRONIZE CACHE(10)
 *   - Response parsers for INQUIRY, both capacities and sense data
 *   - A per-device pending command queue with the submit/complete
 *     split the SAM architecture describes (scsi_submit queues and
 *     pumps; scsi_complete dequeues and fires the callback)
 *   - The L1 extension surface (7 interfaces declared in the header)
 *   - The block-layer hookup: direct-access targets register as
 *     sda..sdh through blk_dev_add() and MBR/GPT
 *     parsing then runs on them like on every other backend
 *
 * The polled execution model matches the rest of the kernel's block
 * stack (AHCI/NVMe are polled too): scsi_submit() drives the HBA's
 * exec hook, then invokes scsi_complete() with the finished command.
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "screen_console.h"
#include "../../services/block/block_svc.h"
#include "../../services/block/drv_internal.h"
#include "driver_scsi.h"
#include "core_registry.h"

/* ------------------------------------------------------------------ */
/* big-endian field helpers (SCSI wire format is big-endian)           */
/* ------------------------------------------------------------------ */

static inline u16 be16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static inline u64 be64(const u8 *p)
{
    return ((u64)be32(p) << 32) | (u64)be32(p + 4);
}

/* ------------------------------------------------------------------ */
/* registries                                                          */
/* ------------------------------------------------------------------ */

static scsi_hba_t *hbas[SCSI_MAX_HBAS];
static int         hba_n;

static scsi_dev_t *devs[SCSI_MAX_DEVS];
static int         dev_n;

void driver_scsi_init(void)
{
    hba_n = 0;
    dev_n = 0;
}

/* ------------------------------------------------------------------ */
/* HBA registry                                                        */
/* ------------------------------------------------------------------ */

int scsi_hba_register(scsi_hba_t *hba)
{
    if (!hba || !hba->ops || !hba->ops->exec)
        return -1;
    if (hba_n >= SCSI_MAX_HBAS)
        return -1;
    hbas[hba_n++] = hba;
    hba->present = 1;
    return hba_n - 1;
}

int scsi_hba_count(void)      { return hba_n; }
scsi_hba_t *scsi_hba_get(int i)
{
    return (i >= 0 && i < hba_n) ? hbas[i] : NULL;
}

/* ================================================================== */
/* CDB builders (pure functions, golden-tested by host_scsi_test)      */
/* ================================================================== */

void scsi_cdb_test_unit_ready(u8 cdb[6])
{
    cdb[0] = 0x00; cdb[1] = cdb[2] = cdb[3] = cdb[4] = 0;
    cdb[5] = 0;                       /* CONTROL                       */
}

void scsi_cdb_inquiry(u8 cdb[6], u8 alloc_len)
{
    cdb[0] = 0x12;                    /* INQUIRY                       */
    cdb[1] = 0x00;                    /* EVPD = 0 (standard data)      */
    cdb[2] = 0x00;                    /* PAGE CODE = 0                 */
    cdb[3] = 0x00;
    cdb[4] = alloc_len;               /* ALLOCATION LENGTH             */
    cdb[5] = 0x00;                    /* CONTROL                       */
}

void scsi_cdb_request_sense(u8 cdb[6], u8 alloc_len)
{
    cdb[0] = 0x03;                    /* REQUEST SENSE                 */
    cdb[1] = 0x00;                    /* DESC = 0 (fixed sense)        */
    cdb[2] = cdb[3] = 0x00;
    cdb[4] = alloc_len;
    cdb[5] = 0x00;
}

void scsi_cdb_read_capacity10(u8 cdb[10])
{
    cdb[0] = 0x25;                    /* READ CAPACITY(10)             */
    cdb[1] = 0x00;
    cdb[2] = cdb[3] = cdb[4] = cdb[5] = 0;   /* LBA = 0                */
    cdb[6] = cdb[7] = cdb[8] = 0;     /* reserved, PMI = 0             */
    cdb[9] = 0x00;                    /* CONTROL                       */
}

void scsi_cdb_read_capacity16(u8 cdb[16], u32 alloc_len)
{
    cdb[0]  = 0x9E;                   /* SERVICE ACTION IN(16)         */
    cdb[1]  = 0x10;                   /* SERVICE ACTION = READ CAP(16) */
    cdb[2]  = cdb[3] = cdb[4] = cdb[5] = 0;  /* LBA = 0                */
    cdb[6]  = (u8)(alloc_len >> 24);
    cdb[7]  = (u8)(alloc_len >> 16);
    cdb[8]  = (u8)(alloc_len >> 8);
    cdb[9]  = (u8)(alloc_len);
    cdb[10] = 0x00;                   /* reserved                      */
    cdb[11] = 0x00;
    cdb[12] = 0x00;
    cdb[13] = 0x00;
    cdb[14] = 0x00;
    cdb[15] = 0x00;                   /* CONTROL                       */
}

void scsi_cdb_read10(u8 cdb[10], u32 lba, u16 blocks)
{
    cdb[0] = 0x28;                    /* READ(10)                      */
    cdb[1] = 0x00;                    /* RDPROTECT/FUA/DPO = 0         */
    cdb[2] = (u8)(lba >> 24);
    cdb[3] = (u8)(lba >> 16);
    cdb[4] = (u8)(lba >> 8);
    cdb[5] = (u8)(lba);
    cdb[6] = 0x00;                    /* group number                  */
    cdb[7] = (u8)(blocks >> 8);
    cdb[8] = (u8)(blocks);
    cdb[9] = 0x00;                    /* CONTROL                       */
}

void scsi_cdb_write10(u8 cdb[10], u32 lba, u16 blocks)
{
    cdb[0] = 0x2A;                    /* WRITE(10)                     */
    cdb[1] = 0x00;
    cdb[2] = (u8)(lba >> 24);
    cdb[3] = (u8)(lba >> 16);
    cdb[4] = (u8)(lba >> 8);
    cdb[5] = (u8)(lba);
    cdb[6] = 0x00;
    cdb[7] = (u8)(blocks >> 8);
    cdb[8] = (u8)(blocks);
    cdb[9] = 0x00;
}

void scsi_cdb_read16(u8 cdb[16], u64 lba, u32 blocks)
{
    cdb[0]  = 0x88;                   /* READ(16)                      */
    cdb[1]  = 0x00;
    cdb[2]  = (u8)(lba >> 56);
    cdb[3]  = (u8)(lba >> 48);
    cdb[4]  = (u8)(lba >> 40);
    cdb[5]  = (u8)(lba >> 32);
    cdb[6]  = (u8)(lba >> 24);
    cdb[7]  = (u8)(lba >> 16);
    cdb[8]  = (u8)(lba >> 8);
    cdb[9]  = (u8)(lba);
    cdb[10] = (u8)(blocks >> 24);
    cdb[11] = (u8)(blocks >> 16);
    cdb[12] = (u8)(blocks >> 8);
    cdb[13] = (u8)(blocks);
    cdb[14] = 0x00;                   /* reserved                      */
    cdb[15] = 0x00;                   /* CONTROL                       */
}

void scsi_cdb_write16(u8 cdb[16], u64 lba, u32 blocks)
{
    scsi_cdb_read16(cdb, lba, blocks);
    cdb[0] = 0x8A;                    /* WRITE(16)                     */
}

void scsi_cdb_mode_sense6(u8 cdb[6], u8 page_code, u8 alloc_len)
{
    cdb[0] = 0x1A;                    /* MODE SENSE(6)                 */
    cdb[1] = 0x00;                    /* DBD = 0                       */
    cdb[2] = page_code;               /* PC = 0 (current), page        */
    cdb[3] = 0x00;                    /* subpage                       */
    cdb[4] = alloc_len;
    cdb[5] = 0x00;
}

void scsi_cdb_sync_cache10(u8 cdb[10])
{
    cdb[0] = 0x35;                    /* SYNCHRONIZE CACHE(10)         */
    cdb[1] = 0x00;                    /* SYNCHREL = 0 (sync, blocking) */
    cdb[2] = cdb[3] = cdb[4] = cdb[5] = 0;   /* LBA = 0                */
    cdb[6] = cdb[7] = cdb[8] = 0;     /* block count = 0 = all         */
    cdb[9] = 0x00;                    /* CONTROL                       */
}

/* ================================================================== */
/* response parsers                                                    */
/* ================================================================== */

void scsi_parse_inquiry(const u8 *r, u32 len, scsi_inq_t *out)
{
    int i;
    if (!r || !out || len < SCSI_INQ_STD)
        return;
    memset(out, 0, sizeof(*out));
    out->qualifier = (u8)(r[0] >> 5);
    out->pdt       = (u8)(r[0] & 0x1F);
    out->removable = (u8)((r[1] >> 7) & 1);
    out->scsi_level = r[2];
    out->data_format = r[3];
    for (i = 0; i < 8; i++)  out->vendor[i]  = (char)r[8 + i];
    for (i = 0; i < 16; i++) out->product[i] = (char)r[16 + i];
    for (i = 0; i < 4; i++)  out->rev[i]     = (char)r[32 + i];
    out->vendor[8] = '\0';
    out->product[16] = '\0';
    out->rev[4] = '\0';
}

void scsi_parse_capacity10(const u8 *r, u64 *last_lba, u32 *block_size)
{
    if (!r)
        return;
    if (last_lba)
        *last_lba = be32(r);
    if (block_size)
        *block_size = be32(r + 4);
}

void scsi_parse_capacity16(const u8 *r, u64 *last_lba, u32 *block_size)
{
    if (!r)
        return;
    if (last_lba)
        *last_lba = be64(r);
    if (block_size)
        *block_size = be32(r + 8);
}

void scsi_parse_sense(const u8 *s, u8 len, scsi_sense_t *out)
{
    u8 code;
    if (!s || !out || len < 1)
        return;
    memset(out, 0, sizeof(*out));
    code = (u8)(s[0] & 0x7F);
    if (code == 0x70 || code == 0x71) {   /* fixed-format sense       */
        out->valid    = (u8)((s[0] & 0x80) ? 1 : 0);
        out->filemark = (u8)((s[2] >> 7) & 1);
        out->eom      = (u8)((s[2] >> 6) & 1);
        out->ili      = (u8)((s[2] >> 5) & 1);
        out->key      = (u8)(s[2] & 0x0F);
        out->info     = be32(s + 3);
        if (len >= 15) {
            out->asc  = s[12];
            out->ascq = s[13];
        }
    } else if (code == 0x72 || code == 0x73) {   /* descriptor format  */
        /* the first descriptor carries the sense key in bits 3:0 of
           byte 1 with ASC/ASCQ in bytes 2..3                        */
        out->valid = 0;
        out->key   = (u8)(s[1] & 0x0F);
        if (len >= 4) {
            out->asc  = s[2];
            out->ascq = s[3];
        }
    }
    /* unknown/other sense layouts leave the zeroed parse result */
}

/* ================================================================== */
/* registry + L1 interface (7)                                         */
/* ================================================================== */

int scsi_count(void)
{
    return dev_n;
}

scsi_dev_t *scsi_get(int idx)
{
    return (idx >= 0 && idx < dev_n) ? devs[idx] : NULL;
}

scsi_dev_t *scsi_get_device(u8 target, u8 lun)
{
    int i;
    for (i = 0; i < dev_n; i++)
        if (devs[i]->present &&
            devs[i]->target == target && devs[i]->lun == lun)
            return devs[i];
    return NULL;
}

/* next free sda..sdh name for a direct-access device */
static void scsi_assign_name(scsi_dev_t *d)
{
    int i;
    for (i = 0; i < SCSI_MAX_DEVS; i++) {
        int taken = 0, j;
        char cand[SCSI_NAME_LEN];
        cand[0] = 's'; cand[1] = 'd';
        cand[2] = (char)('a' + i);
        cand[3] = '\0';
        for (j = 0; j < dev_n; j++)
            if (devs[j]->present && strcmp(devs[j]->name, cand) == 0)
                taken = 1;
        if (!taken) {
            memcpy(d->name, cand, 4);
            return;
        }
    }
    memcpy(d->name, "sd?", 4);       /* registry full upstream         */
}

/* make a freshly built device object usable by the command paths:
 * probe-time INQUIRY/CAPACITY go through scsi_submit exactly like
 * post-registration commands, so present + the queue must be live
 * before scsi_register() inserts the device into the registry. */
static void scsi_dev_arm(scsi_dev_t *d)
{
    d->present  = 1;
    d->q_head = d->q_tail = NULL;
    d->q_len  = 0;
    d->q_max  = 1;                    /* polled core: one at a time    */
}

int scsi_register(scsi_dev_t *d)
{
    if (!d || !d->hba || !d->hba->ops)
        return -1;
    if (dev_n >= SCSI_MAX_DEVS)
        return -1;
    if (d->pdt == SCSI_PDT_DIRECT_ACCESS && !d->name[0])
        scsi_assign_name(d);
    if (!d->name[0]) {
        memcpy(d->name, "lun", 3);
        d->name[3] = '\0';            /* non-disk LUs keep a generic
                                         name; enumerate via scsi_get */
    }
    d->present  = 1;
    d->blk_idx  = -1;
    scsi_dev_arm(d);                  /* idempotent: re-arm the queue */
    devs[dev_n++] = d;
    return dev_n - 1;
}

int scsi_unregister(scsi_dev_t *d)
{
    int i, j;
    if (!d)
        return -1;
    for (i = 0; i < dev_n; i++) {
        if (devs[i] == d) {
            if (d->blk_idx >= 0)
                blk_dev_remove(d->blk_idx);
            for (j = i; j < dev_n - 1; j++)
                devs[j] = devs[j + 1];
            devs[dev_n - 1] = NULL;
            dev_n--;
            d->present = 0;
            return 0;
        }
    }
    return -1;
}

/* ================================================================== */
/* command queue: submit / complete                                    */
/* ================================================================== */

static void scsi_queue_push(scsi_dev_t *d, scsi_cmd_t *c)
{
    c->q_next = NULL;
    if (d->q_tail)
        d->q_tail->q_next = c;
    else
        d->q_head = c;
    d->q_tail = c;
    d->q_len++;
}

static scsi_cmd_t *scsi_queue_pop(scsi_dev_t *d)
{
    scsi_cmd_t *c = d->q_head;
    if (c) {
        d->q_head = c->q_next;
        if (!d->q_head)
            d->q_tail = NULL;
        c->q_next = NULL;
        d->q_len--;
    }
    return c;
}

int scsi_complete(scsi_dev_t *d, scsi_cmd_t *c)
{
    scsi_cmd_t *popped;
    if (!d || !c)
        return -1;
    popped = scsi_queue_pop(d);
    if (popped != c)
        return -1;                    /* not the head / not ours       */
    c->done = 1;
    d->n_completed++;
    if (c->on_complete)
        c->on_complete(c, c->arg);
    return 0;
}

int scsi_submit(scsi_dev_t *d, scsi_cmd_t *c)
{
    if (!d || !d->present || !d->hba || !c || !c->cdb_len)
        return -1;
    if (c->cdb_len > SCSI_CDB_MAX)
        return -1;
    if (d->q_len >= d->q_max)
        return -1;                    /* queue full (ordering kept)    */

    c->done    = 0;
    c->error   = 0;
    c->status  = SCSI_STATUS_GOOD;
    c->xfered  = 0;
    c->sense_len = 0;
    memset(c->sense, 0, sizeof(c->sense));

    scsi_queue_push(d, c);
    d->n_submitted++;

    /* pump: run the transport through the HBA, then complete.  The
       exec hook fills status/sense/data; its return value carries
       transport-level errors.
       SAM unit-attention handling: a POWER-ON/RESET UA (or any other
       UA, e.g. after a bus reset by an earlier firmware stage) makes
       the FIRST command after it return CHECK CONDITION; the UA is
       transient, so the same command is retried transparently. */
    for (u32 attempt = 0; attempt < 3; attempt++) {
        scsi_sense_t se;
        c->error = d->hba->ops->exec(d->hba, d->target, d->lun, c);
        if (c->error != 0 || c->status != SCSI_STATUS_CHECK_COND)
            break;
        scsi_parse_sense(c->sense, c->sense_len, &se);
        if (se.key != SCSI_SENSE_UNIT_ATTENTION)
            break;
        /* UNIT ATTENTION: retry the identical command */
    }
    scsi_complete(d, c);
    return 0;
}

/* convenience: synchronous one-shot command */
int scsi_exec(scsi_dev_t *d, u8 *cdb, u8 cdb_len, u8 dir,
              u8 *data, u32 data_len, scsi_cmd_t *out)
{
    scsi_cmd_t c;
    if (!d || !cdb)
        return -1;
    memset(&c, 0, sizeof(c));
    memcpy(c.cdb, cdb, cdb_len);
    c.cdb_len  = cdb_len;
    c.dir      = dir;
    c.data     = data;
    c.data_len = data_len;
    if (scsi_submit(d, &c) != 0)
        return -1;
    if (out)
        memcpy(out, &c, sizeof(c));
    return (c.error != 0) ? -1 : (int)c.status;
}

/* ================================================================== */
/* convenience wrappers                                                */
/* ================================================================== */

int scsi_inquiry(scsi_dev_t *d, scsi_inq_t *out)
{
    u8 cdb[6], r[SCSI_INQ_STD];
    int rc;
    if (!d || !d->present)
        return -1;
    scsi_cdb_inquiry(cdb, SCSI_INQ_STD);
    rc = scsi_exec(d, cdb, 6, SCSI_DIR_IN, r, sizeof(r), NULL);
    if (rc != 0)
        return -1;
    scsi_parse_inquiry(r, sizeof(r), out);
    return 0;
}

int scsi_test_unit_ready(scsi_dev_t *d)
{
    u8 cdb[6];
    u8 sense[SCSI_SENSE_MAX];
    scsi_cmd_t c;
    int rc;
    if (!d || !d->present)
        return -1;
    scsi_cdb_test_unit_ready(cdb);
    rc = scsi_exec(d, cdb, 6, SCSI_DIR_NONE, NULL, 0, &c);
    if (rc == (int)SCSI_STATUS_CHECK_COND) {
        /* fetch sense data so callers see why it is not ready        */
        memset(&c, 0, sizeof(c));
        scsi_cdb_request_sense(cdb, SCSI_SENSE_MAX);
        memcpy(c.cdb, cdb, 6);
        c.cdb_len = 6;
        c.dir = SCSI_DIR_IN;
        c.data = sense;
        c.data_len = sizeof(sense);
        /* sense data lands in c.sense for the caller to inspect; the
           core deliberately keeps no per-device sense cache          */
        (void)scsi_submit(d, &c);
        return (int)SCSI_STATUS_CHECK_COND;
    }
    return rc;
}

int scsi_read_capacity(scsi_dev_t *d, u64 *sectors, u32 *block_size)
{
    u8 cdb[16], r[32];
    u64 last_lba;
    u32 bs;
    int rc;
    if (!d || !d->present)
        return -1;
    scsi_cdb_read_capacity10(cdb);
    memset(r, 0, sizeof(r));
    rc = scsi_exec(d, cdb, 10, SCSI_DIR_IN, r, 8, NULL);
    if (rc != 0)
        return -1;
    scsi_parse_capacity10(r, &last_lba, &bs);
    if (last_lba == 0xFFFFFFFFull) {
        /* > 2 TiB: escalate to READ CAPACITY(16) (SPC-4)            */
        memset(r, 0, sizeof(r));
        scsi_cdb_read_capacity16(cdb, 32);
        rc = scsi_exec(d, cdb, 16, SCSI_DIR_IN, r, 32, NULL);
        if (rc != 0)
            return -1;
        scsi_parse_capacity16(r, &last_lba, &bs);
    }
    if (sectors)
        *sectors = last_lba + 1;
    if (block_size)
        *block_size = bs;
    return 0;
}

int scsi_read_blocks(scsi_dev_t *d, u64 lba, u32 count, void *buf)
{
    u8 cdb[16];
    int rc;
    if (!d || !d->present || !buf || !count || !d->block_size)
        return -1;
    if (lba > 0xFFFFFFFFull) {
        if (count > 0xFFFF)
            return -1;
        scsi_cdb_read16(cdb, lba, count);
        rc = scsi_exec(d, cdb, 16, SCSI_DIR_IN,
                       (u8 *)buf, count * d->block_size, NULL);
    } else {
        scsi_cdb_read10(cdb, (u32)lba, (u16)count);
        rc = scsi_exec(d, cdb, 10, SCSI_DIR_IN,
                       (u8 *)buf, count * d->block_size, NULL);
    }
    return rc == 0 ? 0 : -1;
}

int scsi_write_blocks(scsi_dev_t *d, u64 lba, u32 count, const void *buf)
{
    u8 cdb[16];
    int rc;
    if (!d || !d->present || !buf || !count || !d->block_size)
        return -1;
    if (lba > 0xFFFFFFFFull) {
        if (count > 0xFFFF)
            return -1;
        scsi_cdb_write16(cdb, lba, count);
        rc = scsi_exec(d, cdb, 16, SCSI_DIR_OUT,
                       (u8 *)buf, count * d->block_size, NULL);
    } else {
        scsi_cdb_write10(cdb, (u32)lba, (u16)count);
        rc = scsi_exec(d, cdb, 10, SCSI_DIR_OUT,
                       (u8 *)buf, count * d->block_size, NULL);
    }
    return rc == 0 ? 0 : -1;
}

/* ================================================================== */
/* block-layer hookup                                                  */
/* ================================================================== */

static int scsi_blk_read(driver_block_device_t *dev, u64 lba, u32 count, void *buf)
{
    scsi_dev_t *d = (scsi_dev_t *)dev->priv;
    u64 dlba;
    u32 dcount;
    if (!d || !d->present || d->blk_idx < 0)
        return -1;
    /* the blk layer always works in 512-byte sectors; scale to the
       device's own logical block size (identity for 512B devices)  */
    if (!d->block_size || (d->block_size % BLK_SECTOR_SIZE))
        return -1;
    dlba   = (lba * (u64)BLK_SECTOR_SIZE) / d->block_size;
    dcount = (count * BLK_SECTOR_SIZE) / d->block_size;
    return scsi_read_blocks(d, dlba, dcount, buf);
}

static int scsi_blk_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf)
{
    scsi_dev_t *d = (scsi_dev_t *)dev->priv;
    u64 dlba;
    u32 dcount;
    if (!d || !d->present || d->blk_idx < 0)
        return -1;
    if (!d->block_size || (d->block_size % BLK_SECTOR_SIZE))
        return -1;
    dlba   = (lba * (u64)BLK_SECTOR_SIZE) / d->block_size;
    dcount = (count * BLK_SECTOR_SIZE) / d->block_size;
    return scsi_write_blocks(d, dlba, dcount, buf);
}

static int scsi_blk_flush(driver_block_device_t *dev)
{
    u8 cdb[10];
    scsi_dev_t *d = (scsi_dev_t *)dev->priv;
    if (!d || !d->present || d->blk_idx < 0)
        return -1;
    scsi_cdb_sync_cache10(cdb);
    return scsi_exec(d, cdb, 10, SCSI_DIR_NONE, NULL, 0, NULL) == 0 ? 0 : -1;
}

static const driver_block_ops_t scsi_blk_ops = {
    .read  = scsi_blk_read,
    .write = scsi_blk_write,
    .flush = scsi_blk_flush,
};

/* ================================================================== */
/* target attach (INQUIRY -> capacity -> registry -> blk)              */
/* ================================================================== */

int scsi_attach_target(scsi_hba_t *hba, u8 target, u8 lun)
{
    scsi_dev_t *d;
    scsi_inq_t inq;
    int idx;

    if (!hba || scsi_get_device(target, lun))
        return -1;
    if (dev_n >= SCSI_MAX_DEVS)
        return -1;

    d = (scsi_dev_t *)kmalloc(sizeof(*d));
    if (!d)
        return -1;
    memset(d, 0, sizeof(*d));
    d->hba    = hba;
    d->target = target;
    d->lun    = lun;
    scsi_dev_arm(d);                  /* probe commands need submit   */

    /* INQUIRY first: an absent target answers with SELECTION TIMEOUT
       (transport error), present ones with standard data           */
    /* INQUIRY first: an absent target answers with SELECTION TIMEOUT
       (transport error), present ones with standard data           */
    if (scsi_inquiry(d, &inq) != 0) {
        kfree(d);
        return -1;
    }
    d->pdt        = inq.pdt;
    d->scsi_level = inq.scsi_level;
    memcpy(d->vendor,  inq.vendor,  sizeof(d->vendor));
    memcpy(d->product, inq.product, sizeof(d->product));
    memcpy(d->rev,     inq.rev,     sizeof(d->rev));

    /* capacity only makes sense for direct-access-like LUs         */
    if (d->pdt == SCSI_PDT_DIRECT_ACCESS ||
        d->pdt == SCSI_PDT_OPTICAL ||
        d->pdt == SCSI_PDT_CDROM) {
        if (scsi_read_capacity(d, &d->sectors, &d->block_size) != 0) {
            d->sectors = 0;
            d->block_size = 0;
        }
    }

    idx = scsi_register(d);
    if (idx < 0) {
        kfree(d);
        return -1;
    }

    /* direct-access targets become sda..sdh in the block layer     */
    if (d->pdt == SCSI_PDT_DIRECT_ACCESS && d->sectors) {
        if (!d->block_size)
            d->block_size = 512;
        d->blk_idx = blk_dev_add(
            d->name, BLK_TYPE_SCSI,
            (d->sectors * d->block_size) / BLK_SECTOR_SIZE,
            BLK_SECTOR_SIZE, &scsi_blk_ops, d);
    }
    return idx;
}

/* ---- v2.0.0 file-local driver identity (collected at boot) ---- */
static int blk_probe_scsi(void) { driver_scsi_init(); return 0; }
static const blk_drv_ops_t g_blk_probe_scsi = { blk_probe_scsi };
OC_DRIVER_DECL("block", "scsi", &g_blk_probe_scsi, "SCSI disk (virtio-scsi passthrough)");
