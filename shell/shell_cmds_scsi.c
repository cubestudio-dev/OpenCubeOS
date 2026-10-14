/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_scsi.c
 *
 * `scsi` (status) and `scsitest` (in-kernel self test) commands.
 *
 * The status command lists every registered HBA and target with its
 * SCSI address, INQUIRY strings, capacity and block-layer name.
 *
 * The self test runs the EXACT production code paths (the CDB
 * builders, the INQUIRY/capacity/sense parsers, the SAM submit/
 * complete queue and the block-layer hookup) against the live LSI
 * HBA when the machine models one (QEMU "-device lsi"):
 *
 *   1. INQUIRY      - re-run INQUIRY and validate the parsed view
 *   2. READ CAPACITY - device capacity vs the blk-layer view
 *   3. READ/WRITE   - a magic-pattern round trip on the LAST sector
 *                     of the first direct-access disk (the original
 *                     content is saved and restored; on a real
 *                     machine this touches one sector of sda)
 *   4. blk hookup   - read LBA 0 through the blk layer and through
 *                     the raw SCSI path and compare
 *
 * Without an HBA the test honestly reports the zero-device state
 * and passes only the pure-function checks (CDB bytes + parsers),
 * which run on the real code paths regardless of hardware.
 */
#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "shell.h"
#include "driver_scsi.h"
#include "driver_block_blk.h"
#include "shell_cmds_scsi.h"

static void put(const char *s)
{
    screen_console_puts(s);
}

static void put_u64(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}

static int step(const char *name, int ok)
{
    put("  ");
    put(name);
    put(": ");
    put(ok ? "PASS" : "FAIL");
    put("\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* scsi (status)                                                       */
/* ------------------------------------------------------------------ */

static const char *pdt_name(u8 pdt)
{
    switch (pdt) {
    case SCSI_PDT_DIRECT_ACCESS: return "disk";
    case SCSI_PDT_SEQ_ACCESS:    return "tape";
    case SCSI_PDT_CDROM:         return "cdrom";
    case SCSI_PDT_OPTICAL:       return "optical";
    case SCSI_PDT_RAID:          return "array";
    default:                     return "other";
    }
}

int shell_cmd_scsi(const char *args)
{
    (void)args;
    int h = scsi_hba_count();
    int n = scsi_count();

    put("scsi HBAs: ");
    put_u64((u64)h);
    put("\n");
    for (int i = 0; i < h; i++) {
        scsi_hba_t *hb = scsi_hba_get(i);
        if (!hb) continue;
        put("  ");
        put(hb->name);
        put(" (initiator id ");
        put_u64(hb->initiator_id);
        put(")\n");
    }

    put("scsi devices: ");
    put_u64((u64)n);
    put("\n");
    for (int i = 0; i < n; i++) {
        scsi_dev_t *d = scsi_get(i);
        if (!d) continue;
        put("  ");
        put(d->name);
        put(": target ");
        put_u64(d->target);
        put(" lun ");
        put_u64(d->lun);
        put(" ");
        put(pdt_name(d->pdt));
        put(" \"");
        put(d->vendor);
        put("\", \"");
        put(d->product);
        put("\" \"");
        put(d->rev);
        put("\" spc");
        put_u64(d->scsi_level >= 3 ? (u64)(d->scsi_level - 2) : 0);
        put(" ");
        put_u64(d->sectors);
        put("x");
        put_u64(d->block_size);
        put(" blk=");
        if (d->blk_idx >= 0)
            put_u64((u64)d->blk_idx);
        else
            put("-");
        put(" q ");
        put_u64(d->n_completed);
        put("/");
        put_u64(d->n_submitted);
        put("\n");
    }
    if (n == 0)
        put("  (no SCSI targets present; QEMU models one via"
            " -device lsi)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* scsitest                                                            */
/* ------------------------------------------------------------------ */

/* pure-function checks: CDB bytes are spec'd golden values */
static int cdb_golden_checks(void)
{
    int fails = 0;
    u8 cdb[16];

    /* READ(10) lba 0x12345678, 4 blocks -> 28 00 12 34 56 78 00 00 04 00 */
    scsi_cdb_read10(cdb, 0x12345678u, 4);
    {
        static const u8 gold[10] =
            {0x28,0x00,0x12,0x34,0x56,0x78,0x00,0x00,0x04,0x00};
        fails += step("read10 golden", memcmp(cdb, gold, 10) == 0);
    }

    /* WRITE(10) lba 0, 1 block -> 2A 00 00 00 00 00 00 00 01 00 */
    scsi_cdb_write10(cdb, 0, 1);
    {
        static const u8 gold[10] =
            {0x2A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00};
        fails += step("write10 golden", memcmp(cdb, gold, 10) == 0);
    }

    /* INQUIRY alloc 36 -> 12 00 00 00 24 00 */
    scsi_cdb_inquiry(cdb, 36);
    {
        static const u8 gold[6] = {0x12,0x00,0x00,0x00,0x24,0x00};
        fails += step("inquiry golden", memcmp(cdb, gold, 6) == 0);
    }

    /* TEST UNIT READY -> 6 zero bytes */
    scsi_cdb_test_unit_ready(cdb);
    {
        static const u8 gold[6] = {0,0,0,0,0,0};
        fails += step("tur golden", memcmp(cdb, gold, 6) == 0);
    }

    /* READ CAPACITY(16) service action byte */
    scsi_cdb_read_capacity16(cdb, 32);
    fails += step("rc16 action", cdb[0] == 0x9E && cdb[1] == 0x10);

    return fails;
}

/* parser checks against hand-built response bytes */
static int parser_golden_checks(void)
{
    int fails = 0;
    u8 r[36];
    scsi_inq_t inq;
    scsi_sense_t sense;

    /* QEMU-style INQUIRY: PDT 0, level 0x05 (SPC-3), "QEMU  " /
       "QEMU HARDDISK   " / "2.5+"                        */
    memset(r, 0, sizeof(r));
    r[0] = 0x00;
    r[1] = 0x00;                       /* not removable             */
    r[2] = 0x05;
    r[3] = 0x02;
    memcpy(r + 8,  "QEMU    ", 8);
    memcpy(r + 16, "QEMU HARDDISK  ", 16);
    memcpy(r + 32, "2.5+", 4);
    scsi_parse_inquiry(r, sizeof(r), &inq);
    fails += step("inq parse", inq.pdt == 0 && inq.scsi_level == 0x05 &&
                  inq.removable == 0 &&
                  memcmp(inq.vendor, "QEMU", 4) == 0 &&
                  memcmp(inq.product, "QEMU HARDDISK", 13) == 0);

    /* capacity(10): last_lba 0x0000FFFF (65535), block 512 */
    {
        u64 last;
        u32 bs;
        u8 cap[8] = {0x00,0x00,0xFF,0xFF, 0x00,0x00,0x02,0x00};
        scsi_parse_capacity10(cap, &last, &bs);
        fails += step("cap10 parse", last == 65535 && bs == 512);
    }

    /* capacity(16): 64-bit last_lba + block size at offset 8 */
    {
        u64 last;
        u32 bs;
        u8 cap[16] = {
            0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,   /* 4 GiB     */
            0x00,0x00,0x02,0x00, 0,0,0,0               /* 512       */
        };
        scsi_parse_capacity16(cap, &last, &bs);
        fails += step("cap16 parse", last == 0x100000000ull &&
                      bs == 512);
    }

    /* fixed sense: ILLEGAL REQUEST / ASC 0x20 0x00 */
    {
        u8 s[18] = {0xF0,0x00,0x05,0,0,0,0,18,0,0,0,0,0x20,0x00,
                    0,0,0,0};
        scsi_parse_sense(s, 18, &sense);
        fails += step("sense parse", sense.valid == 1 &&
                      sense.key == SCSI_SENSE_ILLEGAL_REQ &&
                      sense.asc == 0x20 && sense.ascq == 0x00);
    }

    return fails;
}

int shell_cmd_scsitest(const char *args)
{
    (void)args;
    int fails = 0;
    int n = scsi_count();

    put("scsitest: CDB goldens, parsers, live target checks\n");

    fails += cdb_golden_checks();
    fails += parser_golden_checks();

    /* live checks only when a target was discovered                */
    {
        scsi_dev_t *d = NULL;
        for (int i = 0; i < n; i++) {
            scsi_dev_t *x = scsi_get(i);
            if (x && x->pdt == SCSI_PDT_DIRECT_ACCESS && x->present) {
                d = x;
                break;
            }
        }
        if (d) {
            scsi_inq_t inq;
            u64 sect = 0;
            u32 bs = 0;
            u8 buf0[512], buf1[512], magic[512], save[512];
            u32 lba512_hi;

            put("  live target: ");
            put(d->name);
            put("\n");

            /* 1. INQUIRY on the live path                            */
            fails += step("live inquiry", scsi_inquiry(d, &inq) == 0 &&
                          inq.pdt == SCSI_PDT_DIRECT_ACCESS &&
                          inq.vendor[0] != '\0');

            /* 2. capacity vs the blk view                            */
            fails += step("live capacity",
                          scsi_read_capacity(d, &sect, &bs) == 0 &&
                          sect > 0 && bs > 0 &&
                          (bs % 512) == 0);
            if (d->blk_idx >= 0) {
                driver_block_device_t *bd =
                    driver_block_get_device(d->blk_idx);
                fails += step("capacity vs blk",
                              bd != NULL && bd->sectors == sect &&
                              bd->type == BLK_TYPE_SCSI);
            }

            /* 3. READ/WRITE round trip on the LAST 512B sector.
                 the original content is restored afterwards.       */
            if (sect >= 1 && d->block_size >= 512) {
                u64 last512 = (sect * (u64)bs) / 512 - 1;
                memset(magic, 0, sizeof(magic));
                memcpy(magic, "OCS-SCSI-TEST", 13);
                for (int k = 13; k < 512; k++)
                    magic[k] = (u8)(k * 7 + 11);

                lba512_hi = (u32)(last512 >> 32);
                (void)lba512_hi;

                if (scsi_read_blocks(d, last512, 1, save) == 0 &&
                    scsi_write_blocks(d, last512, 1, magic) == 0 &&
                    scsi_read_blocks(d, last512, 1, buf0) == 0 &&
                    memcmp(buf0, magic, 512) == 0) {
                    fails += step("rw round trip", 1);
                    scsi_write_blocks(d, last512, 1, save);
                    if (scsi_read_blocks(d, last512, 1, buf1) == 0)
                        fails += step("content restored",
                                      memcmp(buf1, save, 512) == 0);
                    else
                        fails += step("content restored", 0);
                } else {
                    fails += step("rw round trip", 0);
                    fails += step("content restored", 0);
                }
            }

            /* 4. blk-layer hookup: LBA 0 through both paths          */
            if (d->blk_idx >= 0) {
                int r_blk = driver_block_read_sectors_raw(d->blk_idx,
                                                          0, 1, buf0);
                int r_scs = scsi_read_blocks(d, 0, 1, buf1);
                fails += step("blk vs scsi lba0",
                              r_blk == 0 && r_scs == 0 &&
                              memcmp(buf0, buf1, 512) == 0);
            }
        } else {
            put("  (no live SCSI target: pure-function checks above"
                " ran on the real code paths)\n");
        }
    }

    put("scsitest: ");
    put_u64((u64)fails);
    put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    return fails == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */

void shell_cmds_scsi_register(void)
{
    shell_register_command_ex("scsi", shell_cmd_scsi,
        "SCSI status (HBAs, targets, capacity, blk names)",
        "WP-10-camera_misc");
    shell_register_command_ex("scsitest", shell_cmd_scsitest,
        "SCSI self test (CDB goldens, parsers, live target)",
        "WP-10-camera_misc");
}
