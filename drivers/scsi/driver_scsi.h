/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/scsi/driver_scsi.h
 *
 * SCSI subsystem core: a SAM-style command layer (CDB builders,
 * response parsers, per-device command queues with submit/complete)
 * shared by every SCSI HBA backend.  The in-tree backend is the LSI
 * 53C895A (drivers/scsi/driver_scsi_lsi.c, the QEMU -device lsi
 * model); an L1 extension can register its own HBA with
 * scsi_hba_register() and attach targets via scsi_attach_target().
 *
 * Command set (SPC / SBC as used by SCSI-2 through SPC-4 disks):
 *   TEST UNIT READY, INQUIRY, REQUEST SENSE, READ CAPACITY(10) with
 *   automatic READ CAPACITY(16) escalation above 2 TiB, READ(10)/
 *   WRITE(10), READ(16)/WRITE(16), MODE SENSE(6) and the
 *   SYNCHRONIZE CACHE(10) flush path.
 *
 * Discovered direct-access (PDT 0x00) targets land in the block
 * layer as sda / sdb / sdc ... via blk_dev_add(),
 * then MBR/GPT partition parsing runs on them exactly like on the
 * other block backends.
 *
 * L1 extension surface (7 interfaces, documented in
 * docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   registration        scsi_register() / scsi_unregister()
 *   enumeration         scsi_count() / scsi_get()
 *   command submission  scsi_submit()      (queue + pump)
 *   completion          scsi_complete()    (HBA -> core callback)
 *   lookup              scsi_get_device()  (by target/lun address)
 *
 * All functions return 0 on success and -1 on error unless stated
 * otherwise; they are honest about missing hardware (no HBA -> no
 * devices, count() == 0, commands fail with an error, never a fake
 * success).
 */
#ifndef OC_DRIVER_SCSI_H
#define OC_DRIVER_SCSI_H

#include "types.h"
#include "../../services/block/drv_internal.h"

/* ---- limits ---- */
#define SCSI_MAX_HBAS    4
#define SCSI_MAX_DEVS    8    /* sda..sdh */
#define SCSI_NAME_LEN    8
#define SCSI_CDB_MAX     16
#define SCSI_SENSE_MAX   32
#define SCSI_INQ_STD     36   /* standard INQUIRY data length   */

/* ---- peripheral device types (INQUIRY byte 0 low nibble) ---- */
#define SCSI_PDT_DIRECT_ACCESS  0x00
#define SCSI_PDT_SEQ_ACCESS     0x01
#define SCSI_PDT_CDROM          0x05
#define SCSI_PDT_OPTICAL        0x07
#define SCSI_PDT_RAID           0x0C

/* ---- SCSI status codes ---- */
#define SCSI_STATUS_GOOD          0x00
#define SCSI_STATUS_CHECK_COND    0x02
#define SCSI_STATUS_COND_MET      0x04
#define SCSI_STATUS_BUSY          0x08
#define SCSI_STATUS_TASK_FULL     0x28

/* ---- sense keys ---- */
#define SCSI_SENSE_NO_SENSE       0x0
#define SCSI_SENSE_RECOVERED      0x1
#define SCSI_SENSE_NOT_READY      0x2
#define SCSI_SENSE_MEDIUM_ERROR   0x3
#define SCSI_SENSE_HARDWARE_ERR   0x4
#define SCSI_SENSE_ILLEGAL_REQ    0x5
#define SCSI_SENSE_UNIT_ATTENTION 0x6
#define SCSI_SENSE_DATA_PROTECT   0x7
#define SCSI_SENSE_ABORTED_CMD    0xB

/* ---- command data direction ---- */
#define SCSI_DIR_NONE  0
#define SCSI_DIR_IN    1
#define SCSI_DIR_OUT   2

/* ---- INQUIRY parsed view ---- */
typedef struct scsi_inq {
    u8  pdt;                          /* peripheral device type        */
    u8  qualifier;                    /* peripheral qualifier          */
    u8  removable;                    /* RMB bit                       */
    u8  scsi_level;                   /* byte 2: version               */
    u8  data_format;                  /* byte 3: response data format  */
    char vendor[9];                   /* 8 bytes + NUL                 */
    char product[17];                 /* 16 bytes + NUL                */
    char rev[5];                      /* 4 bytes + NUL                 */
} scsi_inq_t;

/* ---- sense parsed view ---- */
typedef struct scsi_sense {
    u8  valid;                        /* error code 0x70/0x71          */
    u8  filemark, eom, ili;
    u8  key;                          /* sense key                     */
    u64 info;                         /* INFORMATION field             */
    u16 asc, ascq;                    /* additional sense codes        */
} scsi_sense_t;

/* ---- command block (SAM-style) ---- */
typedef struct scsi_cmd scsi_cmd_t;
struct scsi_cmd {
    u8  cdb[SCSI_CDB_MAX];            /* command descriptor block      */
    u8  cdb_len;                      /* 6/10/12/16                    */
    u8  dir;                          /* SCSI_DIR_*                    */
    u8 *data;                         /* data buffer (kernel virtual,
                                         identity-mapped physical)     */
    u32 data_len;                     /* bytes to move                 */
    u32 xfered;                       /* bytes actually moved          */
    u8  status;                       /* SCSI_STATUS_*                 */
    u8  sense[SCSI_SENSE_MAX];        /* REQUEST SENSE payload         */
    u8  sense_len;
    u8  done;                         /* 1 = completed                 */
    int error;                        /* 0 ok, negative transport err  */
    /* completion callback (async model): invoked by scsi_complete()
       with the command already dequeued.  NULL = fire and forget.   */
    void (*on_complete)(scsi_cmd_t *c, void *arg);
    void *arg;
    scsi_cmd_t *q_next;               /* internal queue link           */
};

/* ---- HBA (host bus adapter) abstraction ---- */
typedef struct scsi_hba scsi_hba_t;
typedef struct scsi_hba_ops {
    /* Execute one complete transaction to (target,lun): arbitration +
       selection + IDENTIFY + CDB + optional data phase + STATUS +
       MSG IN.  Polling model, like every other in-tree block driver.
       Fills c->status / c->sense / c->data / c->xfered and returns 0
       on transport success (the command status itself may still be
       CHECK CONDITION); negative on transport failure (selection
       timeout, bus error, ...). */
    int (*exec)(scsi_hba_t *hba, u8 target, u8 lun, scsi_cmd_t *c);
} scsi_hba_ops_t;

struct scsi_hba {
    char name[16];                    /* e.g. "lsi0"                   */
    u8   initiator_id;                /* our SCSI ID (LSI default 7)   */
    u8   present;
    void *priv;                       /* backend private state         */
    const scsi_hba_ops_t *ops;
};

/* ---- target / logical unit ---- */
typedef struct scsi_dev {
    scsi_hba_t *hba;                  /* owning adapter                */
    u8   target, lun;                 /* SCSI address                  */
    u8   present;
    u8   pdt;                         /* peripheral device type        */
    u8   scsi_level;                  /* SPC version from INQUIRY      */
    char vendor[9];
    char product[17];
    char rev[5];
    u64  sectors;                     /* total 512B sectors            */
    u32  block_size;                  /* device logical block size     */
    char name[SCSI_NAME_LEN];         /* "sda" .. "sdh" (PDT 0 only)   */
    int  blk_idx;                     /* block-layer index, -1 if none */
    /* SAM-style per-device pending queue (single outstanding command
       per device in the polled core; the queue preserves ordering
       for asynchronous submit). */
    scsi_cmd_t *q_head, *q_tail;
    u32  q_len, q_max;
    u64  n_submitted, n_completed;
} scsi_dev_t;

/* ---- core lifecycle (called from kernel/main.c) ---- */
void driver_scsi_init(void);

/* ---- HBA registration (backend side) ---- */
int  scsi_hba_register(scsi_hba_t *hba);
int  scsi_hba_count(void);
scsi_hba_t *scsi_hba_get(int idx);

/* ---- attach a discovered target: INQUIRY (+ capacity for PDT 0),
 *      registry insert and blk-layer hookup for direct-access LUs.
 *      Returns the device slot (>=0) or -1. */
int  scsi_attach_target(scsi_hba_t *hba, u8 target, u8 lun);

/* ==================================================================
 * L1 extension interface (7)
 * ================================================================ */

/* Register a device that the caller prepared (hba/target/lun/...
 * filled in).  Assigns the sda..sdh name for direct-access devices,
 * inserts it into the registry and returns the slot index (>=0),
 * -1 on full/invalid input. */
int  scsi_register(scsi_dev_t *d);

/* Remove a device from the registry (and, if it was a direct-access
 * device, from the block layer).  Returns 0 or -1. */
int  scsi_unregister(scsi_dev_t *d);

/* Number of registered devices (all PDTs). */
int  scsi_count(void);

/* Device by registry index, NULL when out of range. */
scsi_dev_t *scsi_get(int idx);

/* SAM-style submit: queue the command on the device and pump the
 * HBA until it completes (polled model), then fire the completion
 * callback exactly like the interrupt path would.  Returns 0 when
 * the command ran (check c->status / c->error), -1 on rejection
 * (queue full, device gone, bad CDB length). */
int  scsi_submit(scsi_dev_t *d, scsi_cmd_t *c);

/* Completion path: dequeue c from d, mark it done and invoke the
 * callback.  Called by the HBA backend when the transport finishes
 * a command; safe to call from an IRQ handler in a future backend.
 * Returns 0 if the command was on this device's queue, else -1. */
int  scsi_complete(scsi_dev_t *d, scsi_cmd_t *c);

/* Look a device up by its SCSI address (target/lun).  NULL when no
 * such device is registered. */
scsi_dev_t *scsi_get_device(u8 target, u8 lun);

/* ==================================================================
 * CDB builders + response parsers (exported for scsitest and host
 * unit tests; every builder is a pure function of its inputs).
 * ================================================================ */

void scsi_cdb_test_unit_ready(u8 cdb[6]);
void scsi_cdb_inquiry(u8 cdb[6], u8 alloc_len);
void scsi_cdb_request_sense(u8 cdb[6], u8 alloc_len);
void scsi_cdb_read_capacity10(u8 cdb[10]);
void scsi_cdb_read_capacity16(u8 cdb[16], u32 alloc_len);
void scsi_cdb_read10(u8 cdb[10], u32 lba, u16 blocks);
void scsi_cdb_write10(u8 cdb[10], u32 lba, u16 blocks);
void scsi_cdb_read16(u8 cdb[16], u64 lba, u32 blocks);
void scsi_cdb_write16(u8 cdb[16], u64 lba, u32 blocks);
void scsi_cdb_mode_sense6(u8 cdb[6], u8 page_code, u8 alloc_len);
void scsi_cdb_sync_cache10(u8 cdb[10]);

void scsi_parse_inquiry(const u8 *r, u32 len, scsi_inq_t *out);
void scsi_parse_capacity10(const u8 *r, u64 *last_lba, u32 *block_size);
void scsi_parse_capacity16(const u8 *r, u64 *last_lba, u32 *block_size);
void scsi_parse_sense(const u8 *s, u8 len, scsi_sense_t *out);

/* ---- convenience: blocking command helpers ----------------------- */

/* Run one CDB synchronously through the device (submit + wait).
 * data may be NULL when data_len is 0.  out may be NULL. */
int  scsi_exec(scsi_dev_t *d, u8 *cdb, u8 cdb_len, u8 dir,
               u8 *data, u32 data_len, scsi_cmd_t *out);

/* INQUIRY wrapper (36-byte standard data). */
int  scsi_inquiry(scsi_dev_t *d, scsi_inq_t *out);

/* TEST UNIT READY + REQUEST SENSE on CHECK CONDITION.  Returns the
 * final SCSI status (0 = ready). */
int  scsi_test_unit_ready(scsi_dev_t *d);

/* READ CAPACITY(10), escalating to READ CAPACITY(16) when the
 * device reports 0xFFFFFFFF last-LBA (> 2 TiB). */
int  scsi_read_capacity(scsi_dev_t *d, u64 *sectors, u32 *block_size);

/* Block helpers through READ(10)/WRITE(10) (auto-READ(16)/WRITE(16)
 * beyond the 32-bit LBA range), sectors counted in d->block_size. */
int  scsi_read_blocks(scsi_dev_t *d, u64 lba, u32 count, void *buf);
int  scsi_write_blocks(scsi_dev_t *d, u64 lba, u32 count, const void *buf);

/* LSI 53C895A backend (driver_scsi_lsi.c): PCI probe + target scan. */
int  driver_scsi_lsi_init(void);

#endif /* OC_DRIVER_SCSI_H */
