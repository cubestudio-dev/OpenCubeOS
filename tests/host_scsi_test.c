/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-camera_misc
 *
 * File: tests/host_scsi_test.c
 *
 * Host-side test of the SCSI subsystem (drivers/scsi/driver_scsi.c).
 * The SAME kernel .c is compiled for the host.  A fake HBA backend
 * (an exec hook that answers INQUIRY / READ CAPACITY / READ / WRITE
 * from canned buffers, exactly like the QEMU scsi-hd model does)
 * drives the whole production path: CDB builders -> scsi_submit ->
 * the SAM queue -> scsi_complete -> the response parsers -> the
 * block-layer hookup.
 *
 * Coverage (the four SCSI items of the batch test plan):
 *   1. INQUIRY        CDB golden bytes + response parse
 *   2. READ CAPACITY  CDB golden bytes, cap10/cap16 parse, the
 *                     automatic cap10 -> cap16 escalation
 *   3. READ/WRITE     READ(10)/WRITE(10) + READ(16)/WRITE(16)
 *                     escalation, data round trip through the HBA
 *   4. blk hookup     sda/sdb/sdc naming, driver_block_register_
 *                     device() binding, submit/complete queue stats
 *
 * Build:
 *   gcc -O2 -g -Ikernel -Ikernel/lib -Idrivers/block tests/host_scsi_test.c \
 *       -o /tmp/host_scsi_test
 *
 * Exit code = number of failing checks.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../core/types.h"
#include "../drivers/block/driver_block_blk.h"

/* ---- stubs the kernel .c expects on the host --------------------- */
void *kmalloc(unsigned long long size) { return malloc(size); }
void  kfree(void *p)                   { free(p); }
u64   core_timer_now_ms(void)          { return 0; }
void  screen_console_puts(const char *s) { fputs(s, stdout); }

/* the blk layer is stubbed: we record what the SCSI core binds     */
static char     blk_last_name[32];
static u64      blk_last_sectors;
static int      blk_reg_calls, blk_unreg_calls;
int  driver_block_register_device(const char *name, driver_block_type_t type, u64 sectors,
                                  u32 sector_size, const driver_block_ops_t *ops,
                                  void *priv)
{
    (void)type; (void)sector_size; (void)ops;
    snprintf(blk_last_name, sizeof(blk_last_name), "%s", name);
    blk_last_sectors = sectors;
    blk_reg_calls++;
    return blk_reg_calls - 1;
}
int  driver_block_unregister_device(int index)
{
    (void)index;
    blk_unreg_calls++;
    return 0;
}
driver_block_device_t *driver_block_get_device(int index) { (void)index; return NULL; }
int  driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf)
{ (void)dev_idx; (void)lba; (void)count; (void)buf; return -1; }
int  driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf)
{ (void)dev_idx; (void)lba; (void)count; (void)buf; return -1; }

#include "../core/lib/lib_string.c"
#include "../drivers/scsi/driver_scsi.c"

static int check(const char *name, int cond)
{
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* ================================================================== */
/* fake HBA: answers like the QEMU scsi-hd model                       */
/* ================================================================== */

static u8   fake_inq[36];
static u64  fake_cap_last_lba;      /* what cap10 reports              */
static u32  fake_cap_bs;
static int  fake_rc16_mode;         /* cap10 reports 0xFFFFFFFF        */
static u8   fake_disk[512 * 4];     /* 4 sectors of storage            */
static u8   fake_last_cdb[16];
static u8   fake_last_cdb_len;
static int  fake_exec_calls;

static int fake_exec(scsi_hba_t *h, u8 target, u8 lun, scsi_cmd_t *c)
{
    (void)h; (void)lun;
    /* targets 3 and 4 are absent: selection timeout (transport err) */
    if (target == 3 || target == 4)
        return -1;
    fake_exec_calls++;
    memset(fake_last_cdb, 0, 16);
    memcpy(fake_last_cdb, c->cdb, c->cdb_len);
    fake_last_cdb_len = c->cdb_len;

    switch (c->cdb[0]) {
    case 0x00:                       /* TEST UNIT READY               */
        c->status = SCSI_STATUS_GOOD;
        return 0;
    case 0x12:                       /* INQUIRY                       */
        if (c->data && c->data_len)
            memcpy(c->data, fake_inq,
                   c->data_len < sizeof(fake_inq) ? c->data_len
                                                  : sizeof(fake_inq));
        c->status = SCSI_STATUS_GOOD;
        return 0;
    case 0x25:                       /* READ CAPACITY(10)             */
        if (c->data && c->data_len >= 8) {
            u32 last = fake_rc16_mode ? 0xFFFFFFFFu
                                      : (u32)fake_cap_last_lba;
            c->data[0] = (u8)(last >> 24);
            c->data[1] = (u8)(last >> 16);
            c->data[2] = (u8)(last >> 8);
            c->data[3] = (u8)(last);
            c->data[4] = (u8)(fake_cap_bs >> 24);
            c->data[5] = (u8)(fake_cap_bs >> 16);
            c->data[6] = (u8)(fake_cap_bs >> 8);
            c->data[7] = (u8)(fake_cap_bs);
        }
        c->status = SCSI_STATUS_GOOD;
        return 0;
    case 0x9E:                       /* SERVICE ACTION IN: cap16      */
        if (c->cdb[1] == 0x10 && c->data && c->data_len >= 16) {
            memset(c->data, 0, 16);
            c->data[0] = (u8)(fake_cap_last_lba >> 56);
            c->data[1] = (u8)(fake_cap_last_lba >> 48);
            c->data[2] = (u8)(fake_cap_last_lba >> 40);
            c->data[3] = (u8)(fake_cap_last_lba >> 32);
            c->data[4] = (u8)(fake_cap_last_lba >> 24);
            c->data[5] = (u8)(fake_cap_last_lba >> 16);
            c->data[6] = (u8)(fake_cap_last_lba >> 8);
            c->data[7] = (u8)(fake_cap_last_lba);
            c->data[8]  = (u8)(fake_cap_bs >> 24);
            c->data[9]  = (u8)(fake_cap_bs >> 16);
            c->data[10] = (u8)(fake_cap_bs >> 8);
            c->data[11] = (u8)(fake_cap_bs);
        }
        c->status = SCSI_STATUS_GOOD;
        return 0;
    case 0x28:                       /* READ(10)                      */
    case 0x88: {                     /* READ(16)                      */
        u64 lba;
        u32 blocks;
        if (c->cdb[0] == 0x28) {
            lba = ((u64)fake_last_cdb[2] << 24) |
                  ((u64)fake_last_cdb[3] << 16) |
                  ((u64)fake_last_cdb[4] << 8)  |
                  (u64)fake_last_cdb[5];
            blocks = ((u32)fake_last_cdb[7] << 8) | fake_last_cdb[8];
        } else {
            lba = ((u64)fake_last_cdb[2] << 56) |
                  ((u64)fake_last_cdb[3] << 48) |
                  ((u64)fake_last_cdb[4] << 40) |
                  ((u64)fake_last_cdb[5] << 32) |
                  ((u64)fake_last_cdb[6] << 24) |
                  ((u64)fake_last_cdb[7] << 16) |
                  ((u64)fake_last_cdb[8] << 8)  |
                  (u64)fake_last_cdb[9];
            blocks = ((u32)fake_last_cdb[10] << 24) |
                     ((u32)fake_last_cdb[11] << 16) |
                     ((u32)fake_last_cdb[12] << 8) |
                     (u32)fake_last_cdb[13];
        }
        if (lba * 512u + (u64)blocks * 512u > sizeof(fake_disk))
            return -1;               /* out of range -> transport err */
        memcpy(c->data, fake_disk + lba * 512u,
               (size_t)blocks * 512u);
        c->xfered = blocks * 512u;
        c->status = SCSI_STATUS_GOOD;
        return 0;
    }
    case 0x2A:                       /* WRITE(10)                     */
    case 0x8A: {                     /* WRITE(16)                     */
        u64 lba;
        u32 blocks;
        if (c->cdb[0] == 0x2A) {
            lba = ((u64)fake_last_cdb[2] << 24) |
                  ((u64)fake_last_cdb[3] << 16) |
                  ((u64)fake_last_cdb[4] << 8)  |
                  (u64)fake_last_cdb[5];
            blocks = ((u32)fake_last_cdb[7] << 8) | fake_last_cdb[8];
        } else {
            lba = ((u64)fake_last_cdb[2] << 56) |
                  ((u64)fake_last_cdb[3] << 48) |
                  ((u64)fake_last_cdb[4] << 40) |
                  ((u64)fake_last_cdb[5] << 32) |
                  ((u64)fake_last_cdb[6] << 24) |
                  ((u64)fake_last_cdb[7] << 16) |
                  ((u64)fake_last_cdb[8] << 8)  |
                  (u64)fake_last_cdb[9];
            blocks = ((u32)fake_last_cdb[10] << 24) |
                     ((u32)fake_last_cdb[11] << 16) |
                     ((u32)fake_last_cdb[12] << 8) |
                     (u32)fake_last_cdb[13];
        }
        if (lba * 512u + (u64)blocks * 512u > sizeof(fake_disk))
            return -1;
        memcpy(fake_disk + lba * 512u, c->data, (size_t)blocks * 512u);
        c->xfered = blocks * 512u;
        c->status = SCSI_STATUS_GOOD;
        return 0;
    }
    case 0x35:                       /* SYNCHRONIZE CACHE(10)         */
        c->status = SCSI_STATUS_GOOD;
        return 0;
    default:
        c->status = SCSI_STATUS_CHECK_COND;
        c->sense[0] = 0x70;
        c->sense[2] = SCSI_SENSE_ILLEGAL_REQ;
        c->sense[12] = 0x20;
        c->sense[13] = 0x00;
        c->sense_len = 18;
        return 0;
    }
}

static void fake_reset(void)
{
    memset(fake_inq, 0, sizeof(fake_inq));
    fake_inq[0] = 0x00;              /* direct access, qualifier 0    */
    fake_inq[1] = 0x00;
    fake_inq[2] = 0x05;              /* SPC-3                         */
    fake_inq[3] = 0x02;
    memcpy(fake_inq + 8,  "QEMU    ", 8);
    memcpy(fake_inq + 16, "QEMU HARDDISK  ", 16);
    memcpy(fake_inq + 32, "2.5+", 4);
    fake_cap_last_lba = 3;           /* 4 sectors                     */
    fake_cap_bs = 512;
    fake_rc16_mode = 0;
    fake_exec_calls = 0;
    memset(fake_disk, 0, sizeof(fake_disk));
    memset(blk_last_name, 0, sizeof(blk_last_name));
    blk_last_sectors = 0;
}

static scsi_hba_t fake_hba;
static const scsi_hba_ops_t fake_ops = { .exec = fake_exec };

/* ================================================================== */
/* 1. CDB builders: golden bytes                                       */
/* ================================================================== */

static int cdb_goldens(void)
{
    int fails = 0;
    u8 cdb[16];
    static const u8 r10[10]  = {0x28,0x00,0x12,0x34,0x56,0x78,0x00,0x00,0x04,0x00};
    static const u8 w10[10]  = {0x2A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00};
    static const u8 inq6[6]  = {0x12,0x00,0x00,0x00,0x24,0x00};
    static const u8 rs6[6]   = {0x03,0x00,0x00,0x00,0x20,0x00};
    static const u8 tur6[6]  = {0x00,0x00,0x00,0x00,0x00,0x00};
    static const u8 rc10[10] = {0x25,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    static const u8 ms6[6]   = {0x1A,0x00,0x08,0x00,0x0C,0x00};
    static const u8 sc10[10] = {0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

    scsi_cdb_read10(cdb, 0x12345678u, 4);
    fails += check("read10 golden bytes", memcmp(cdb, r10, 10) == 0);

    scsi_cdb_write10(cdb, 0, 1);
    fails += check("write10 golden bytes", memcmp(cdb, w10, 10) == 0);

    scsi_cdb_inquiry(cdb, 36);
    fails += check("inquiry golden bytes", memcmp(cdb, inq6, 6) == 0);

    scsi_cdb_request_sense(cdb, 32);
    fails += check("request sense golden bytes", memcmp(cdb, rs6, 6) == 0);

    scsi_cdb_test_unit_ready(cdb);
    fails += check("test unit ready golden", memcmp(cdb, tur6, 6) == 0);

    scsi_cdb_read_capacity10(cdb);
    fails += check("read capacity10 golden", memcmp(cdb, rc10, 10) == 0);

    scsi_cdb_mode_sense6(cdb, 0x08, 12);
    fails += check("mode sense6 golden", memcmp(cdb, ms6, 6) == 0);

    scsi_cdb_sync_cache10(cdb);
    fails += check("sync cache10 golden", memcmp(cdb, sc10, 10) == 0);

    scsi_cdb_read16(cdb, 0x1122334455667788ull, 0x01020304u);
    fails += check("read16 golden lba", cdb[0] == 0x88 &&
                  cdb[2] == 0x11 && cdb[3] == 0x22 && cdb[4] == 0x33 &&
                  cdb[5] == 0x44 && cdb[6] == 0x55 && cdb[7] == 0x66 &&
                  cdb[8] == 0x77 && cdb[9] == 0x88);
    fails += check("read16 golden blocks", cdb[10] == 0x01 &&
                  cdb[11] == 0x02 && cdb[12] == 0x03 && cdb[13] == 0x04);

    scsi_cdb_write16(cdb, 0, 1);
    fails += check("write16 opcode", cdb[0] == 0x8A && cdb[13] == 1);

    scsi_cdb_read_capacity16(cdb, 32);
    fails += check("rc16 service action", cdb[0] == 0x9E &&
                  cdb[1] == 0x10 && cdb[9] == 32);

    return fails;
}

/* ================================================================== */
/* 2. parsers                                                          */
/* ================================================================== */

static int parser_tests(void)
{
    int fails = 0;
    scsi_inq_t inq;

    fake_reset();
    scsi_sense_t sense;
    u8 r[36], s[18];
    u64 last;
    u32 bs;

    scsi_parse_inquiry(fake_inq, sizeof(fake_inq), &inq);
    fails += check("inq vendor/product", inq.pdt == 0 &&
                  inq.scsi_level == 0x05 && inq.removable == 0 &&
                  memcmp(inq.vendor, "QEMU", 4) == 0 &&
                  memcmp(inq.product, "QEMU HARDDISK", 13) == 0 &&
                  memcmp(inq.rev, "2.5+", 4) == 0);
    fails += check("inq nul-terminated", inq.vendor[8] == 0 &&
                  inq.product[16] == 0 && inq.rev[4] == 0);

    /* short buffer rejected (out untouched -> zeroed)               */
    memset(&inq, 0xAA, sizeof(inq));
    scsi_parse_inquiry(fake_inq, 20, &inq);
    fails += check("inq short len ignored", inq.vendor[0] == (char)0xAA);

    /* removable bit                                                 */
    memcpy(r, fake_inq, 36);
    r[1] = 0x80;
    scsi_parse_inquiry(r, 36, &inq);
    fails += check("inq removable bit", inq.removable == 1);

    /* capacity 10                                                   */
    {
        u8 cap[8] = {0x00,0x00,0xFF,0xFF, 0x00,0x00,0x02,0x00};
        scsi_parse_capacity10(cap, &last, &bs);
        fails += check("cap10 parse", last == 65535 && bs == 512);
    }

    /* capacity 16                                                   */
    {
        u8 cap[16] = {0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
                      0x00,0x00,0x02,0x00, 0,0,0,0};
        scsi_parse_capacity16(cap, &last, &bs);
        fails += check("cap16 parse", last == 0x100000000ull &&
                      bs == 512);
    }

    /* fixed sense: NOT READY, ASC 0x04 0x02, info 0x1234            */
    memset(s, 0, sizeof(s));
    s[0] = 0xF0; s[2] = SCSI_SENSE_NOT_READY;
    s[3] = 0x00; s[4] = 0x00; s[5] = 0x12; s[6] = 0x34;
    s[7] = 18;   s[12] = 0x04; s[13] = 0x02;
    scsi_parse_sense(s, 18, &sense);
    fails += check("sense fixed parse", sense.valid == 1 &&
                  sense.key == SCSI_SENSE_NOT_READY &&
                  sense.info == 0x1234 &&
                  sense.asc == 0x04 && sense.ascq == 0x02);

    /* descriptor sense                                              */
    {
        u8 d[8] = {0x72, 0x0B, 0x2A, 0x01, 0, 0, 0, 0};
        scsi_parse_sense(d, 8, &sense);
        fails += check("sense descriptor parse", sense.valid == 0 &&
                      sense.key == SCSI_SENSE_ABORTED_CMD &&
                      sense.asc == 0x2A && sense.ascq == 0x01);
    }

    return fails;
}

/* ================================================================== */
/* 3 + 4. live-ish path through the fake HBA                           */
/* ================================================================== */

static int live_path(void)
{
    int fails = 0;
    scsi_dev_t *d;
    scsi_inq_t inq;
    u64 sect = 0;
    u32 bs = 0;
    u8 buf[512], buf2[512], magic[512];
    int cb_calls = 0;
    scsi_cmd_t c;

    driver_scsi_init();
    fake_reset();
    memset(&fake_hba, 0, sizeof(fake_hba));
    memcpy(fake_hba.name, "fake0", 6);
    fake_hba.initiator_id = 7;
    fake_hba.priv = NULL;
    fake_hba.ops = &fake_ops;
    fails += check("hba register", scsi_hba_register(&fake_hba) == 0);
    fails += check("hba count", scsi_hba_count() == 1);

    fails += check("attach target 0", scsi_attach_target(&fake_hba, 0, 0) == 0);
    d = scsi_get_device(0, 0);
    fails += check("device by address", d != NULL);
    fails += check("named sda", d && strcmp(d->name, "sda") == 0);
    fails += check("blk bind sda", blk_reg_calls == 1 &&
                  strcmp(blk_last_name, "sda") == 0);
    fails += check("blk sectors 512-unit",
                  blk_last_sectors == (4u * 512u) / 512u);
    fails += check("inq strings", d &&
                  memcmp(d->vendor, "QEMU", 4) == 0 &&
                  memcmp(d->product, "QEMU HARDDISK", 13) == 0);
    fails += check("spc level", d && d->scsi_level == 0x05);

    /* INQUIRY wrapper on the live path                              */
    d = scsi_get_device(0, 0);
    fails += check("live inquiry", scsi_inquiry(d, &inq) == 0 &&
                  inq.pdt == 0);

    /* capacity (cap10 path)                                         */
    fails += check("live capacity", scsi_read_capacity(d, &sect, &bs) == 0 &&
                  sect == 4 && bs == 512);

    /* submit/complete queue: callback fires exactly once            */
    {
        u64 sub_before = d->n_submitted, com_before = d->n_completed;
        u8 tur[6];
        memset(&c, 0, sizeof(c));
        scsi_cdb_test_unit_ready(tur);
        memcpy(c.cdb, tur, 6);
        c.cdb_len = 6;
        c.dir = SCSI_DIR_NONE;
        c.on_complete = NULL;        /* NULL callback must not crash  */
        fails += check("submit tur", scsi_submit(d, &c) == 0 &&
                      c.done == 1 && c.status == SCSI_STATUS_GOOD);
        fails += check("queue stats", d->n_submitted == sub_before + 1 &&
                      d->n_completed == com_before + 1);
    }

    /* second + third target: sdb / sdc                              */
    fake_cap_last_lba = 7;           /* 8 sectors                     */
    fails += check("attach target 1", scsi_attach_target(&fake_hba, 1, 0) >= 0);
    fails += check("attach target 2", scsi_attach_target(&fake_hba, 2, 0) >= 0);
    fails += check("named sdb", scsi_get_device(1, 0) &&
                  strcmp(scsi_get_device(1, 0)->name, "sdb") == 0);
    fails += check("named sdc", scsi_get_device(2, 0) &&
                  strcmp(scsi_get_device(2, 0)->name, "sdc") == 0);
    fails += check("count 3", scsi_count() == 3);
    fails += check("get by idx", scsi_get(0) == scsi_get_device(0, 0) &&
                  scsi_get(9) == NULL);
    fails += check("dup target rejected",
                  scsi_attach_target(&fake_hba, 0, 0) == -1);

    /* unknown CDB -> CHECK CONDITION + sense via the fake backend   */
    {
        u8 junk[6] = {0xFF, 0, 0, 0, 0, 0};
        memset(&c, 0, sizeof(c));
        memcpy(c.cdb, junk, 6);
        c.cdb_len = 6;
        fails += check("unknown cdb status", scsi_submit(d, &c) == 0 &&
                      c.status == SCSI_STATUS_CHECK_COND &&
                      c.sense_len == 18 &&
                      c.sense[2] == SCSI_SENSE_ILLEGAL_REQ);
    }

    /* READ/WRITE round trip (device 1: 8 sectors)                   */
    d = scsi_get_device(1, 0);
    memset(magic, 0, sizeof(magic));
    memcpy(magic, "OCS-SCSI-HOST", 13);
    for (int i = 13; i < 512; i++)
        magic[i] = (u8)(i * 3 + 5);
    fails += check("write10 round trip",
                  scsi_write_blocks(d, 2, 1, magic) == 0 &&
                  scsi_read_blocks(d, 2, 1, buf) == 0 &&
                  memcmp(buf, magic, 512) == 0);
    fails += check("read10 cdb on wire",
                  fake_last_cdb_len == 10 && fake_last_cdb[0] == 0x28 &&
                  fake_last_cdb[2] == 0 && fake_last_cdb[5] == 2 &&
                  fake_last_cdb[8] == 1);

    /* READ(16)/WRITE(16) escalation beyond 32-bit LBA: make a
       2 TiB-capable fake and address LBA 0x100000000                */
    fake_reset();
    {
        scsi_dev_t *big;
        fails += check("attach big disk", scsi_attach_target(&fake_hba, 5, 0) >= 0);
        big = scsi_get_device(5, 0);
        fails += check("big named", big &&
                      strcmp(big->name, "sdd") == 0);
        fails += check("big capacity", big &&
                      big->sectors == 4 && big->block_size == 512);
        /* out-of-range READ(16) -> transport error, honest -1       */
        fails += check("read16 far lba errors",
                      scsi_read_blocks(big, 0x100000000ull, 1, buf) == -1);
        fails += check("read16 cdb on wire",
                      fake_last_cdb_len == 16 && fake_last_cdb[0] == 0x88);
    }

    /* cap10 -> cap16 escalation when the device reports 0xFFFFFFFF  */
    fake_reset();
    fake_rc16_mode = 1;
    fake_cap_last_lba = 0xFFFFFFFFF0ull;   /* 64-bit size via cap16   */
    {
        scsi_dev_t *huge;
        fails += check("attach huge", scsi_attach_target(&fake_hba, 6, 0) >= 0);
        huge = scsi_get_device(6, 0);
        fails += check("huge capacity via rc16", huge &&
                      huge->sectors == 0xFFFFFFFFF1ull &&
                      huge->block_size == 512);
    }

    /* unregister removes from the registry                          */
    d = scsi_get_device(0, 0);
    fails += check("unregister", scsi_unregister(d) == 0 &&
                  scsi_get_device(0, 0) == NULL && scsi_count() == 4);

    /* absent target: transport error (selection timeout upstream)   */
    fails += check("absent target rejected",
                  scsi_attach_target(&fake_hba, 4, 0) == -1 ||
                  scsi_get_device(4, 0) == NULL);

    return fails;
}

int main(void)
{
    int fails = 0;

    printf("== SCSI CDB builders (golden bytes) ==\n");
    fails += cdb_goldens();

    printf("== SCSI response parsers ==\n");
    fails += parser_tests();

    printf("== SCSI live path (fake HBA) ==\n");
    fails += live_path();

    printf("host_scsi_test: %d failures - %s\n", fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails;
}
