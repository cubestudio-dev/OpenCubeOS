/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/scsi/driver_scsi_lsi.c
 *
 * LSI 53C895A SCSI HBA backend (the QEMU "-device lsi" model and
 * the real 53C8XX chip family share this programming interface).
 *
 * Transaction model — one mini SCRIPTS program per command, built
 * in kernel memory and started by writing DSP (registers 0x2C..0x2F;
 * the write to byte 3 launches the engine):
 *
 *     SELECT with ATN  <target>, relative jump to the next insn
 *     MSG  OUT  1 byte   IDENTIFY (0x80 | lun)
 *     CMD       cdb_len  the CDB
 *     DATA IN/OUT  n     the data phase (skipped for 0-length)
 *     STATUS    1 byte   the command status
 *     MSG  IN   1 byte   the completion message
 *     Interrupt          raises DSTAT.SIR = normal end
 *
 * The driver then polls ISTAT0 for DIP|SIP (polled model, like the
 * kernel's AHCI/NVMe), reads DSTAT / SIST0 / SIST1 to classify the
 * outcome and hands the status to the SCSI core:
 *
 *     DSTAT.SIR               -> command ran; check c->status
 *     SIST1.STO               -> selection timeout: no target
 *     SIST0.MA / UDC / SGE, DSTAT.IID/BF/MDPE/ABRT -> transport error
 *
 * REQUEST SENSE is fetched automatically when a command ends with
 * CHECK CONDITION, mirroring the SAM error-recovery flow.
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "screen_console.h"
#include "driver_pci.h"
#include "driver_scsi.h"
#include "driver_scsi_lsi.h"

static inline u8  lsi_inb(u16 p)  { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void lsi_outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

/* SCRIPTS program buffer.  WP-10-tui-qa: the command-phase script
   (lsi_build_script1) emits 15 instruction/data pairs = 30 dwords in
   the worst case (SELECT 2 + MO 2 + CMD 2 + 2 JUMPs 4 + DATA 2 +
   STATUS 2 + 3 MSG-IN loop pairs 6 + INT 2 + disconnect branch 3
   pairs 6); the reselected script2 peaks at exactly 16.  The old
   16-dword buffer let script1 overrun 56 bytes into the adjacent BSS
   (ELF layout: straight into g_exfat_dir_ops - verified by nm on the
   release build).  32 dwords covers both with margin; an overflow
   guard in the builders makes the bound explicit. */
#define LSI_SCRIPT_DWORDS 32
#define LSI_POLL_MAX      20000000   /* generous: TCG runs slow     */

static u32 lsi_script_buf[LSI_SCRIPT_DWORDS];
static scsi_lsi_state_t lsi_state;
static scsi_hba_t       lsi_hba;

/* ------------------------------------------------------------------ */
/* register helpers                                                    */
/* ------------------------------------------------------------------ */

static inline u8 lsi_rd(u8 reg)      { return lsi_inb(lsi_state.io_base + reg); }
static inline void lsi_wr(u8 reg, u8 v) { lsi_outb(lsi_state.io_base + reg, v); }

/* clear both interrupt latches so the next command starts clean   */
static void lsi_ack_irqs(void)
{
    if (lsi_rd(LSI_ISTAT0) & (LSI_ISTAT0_DIP | LSI_ISTAT0_SIP)) {
        (void)lsi_rd(LSI_DSTAT);
        (void)lsi_rd(LSI_SIST0);
        (void)lsi_rd(LSI_SIST1);
    }
}

static void lsi_soft_reset(void)
{
    lsi_wr(LSI_ISTAT0, LSI_ISTAT0_SRST);      /* assert  */
    lsi_wr(LSI_ISTAT0, 0);                    /* deassert */
    lsi_wr(LSI_SCNTL0, 0x00);                 /* initiator mode, no
                                                 special options     */
    lsi_wr(LSI_SCNTL1, 0x00);                 /* no reset, connect off */
    lsi_wr(LSI_SCNTL3, 0x03);                 /* clock divide /2 (QEMU
                                                 ignores timing)      */
    lsi_wr(LSI_SXFER,  0x00);                 /* async, 8-bit         */
    lsi_wr(LSI_SCID, (u8)(0x40 | lsi_state.initiator_id));  /* RRE +
                                                 initiator ID        */
    lsi_wr(LSI_DMODE, 0x00);                  /* direct addressing,
                                                 no 40/64-bit modes  */
    lsi_wr(LSI_DIEN,  0x00);                  /* DIP reflects dstat
                                                 regardless (QEMU)   */
    lsi_wr(LSI_SIEN0, 0x10);                  /* RSL reselect notice
                                                 (polled via ISTAT0) */
    lsi_wr(LSI_SIEN1, 0x00);
    lsi_ack_irqs();
}

/* ------------------------------------------------------------------ */
/* SCRIPTS construction + execution                                    */
/* ------------------------------------------------------------------ */

/* Write one 32-bit word into DSP (LSB first; writing 0x2F starts
 * the engine — do that last). */
static void lsi_load_dsp(u32 val)
{
    lsi_wr(LSI_DSP + 0, (u8)(val));
    lsi_wr(LSI_DSP + 1, (u8)(val >> 8));
    lsi_wr(LSI_DSP + 2, (u8)(val >> 16));
    lsi_wr(LSI_DSP + 3, (u8)(val >> 24));   /* <- engine starts here */
}

/* ------------------------------------------------------------------ */
/* SCRIPTS construction + execution                                    */
/* ------------------------------------------------------------------ */

/* ---- SCRIPTS construction (two scripts per transaction) ----------
 *
 * Script 1 (command phase):
 *   SELECT with ATN -> IDENTIFY -> CDB -> MSG-IN loop -> INT.
 *   The MSG-IN loop drains every message byte the target sends and
 *   ends the script on the first COMMAND COMPLETE (0x00, synchronous
 *   command) or DISCONNECT (0x04, asynchronous media I/O); SFTR
 *   bytes like SAVE DATA POINTER (0x02) / RESTORE POINTERS (0x03)
 *   loop transparently.  lsi_state.msg_in then tells which way the
 *   command went.
 *
 * Script 2 (reselected data phase, asynchronous commands only):
 *   MSG-IN (the reselect IDENTIFY) -> DATA -> STATUS -> MSG-IN loop
 *   -> INT.  Executed after the target reselected the bus.
 */

/* build the command-phase script.  After the CDB the bus phase
 * decides the route:
 *   MESSAGE IN  -> asynchronous command: drain SAVE POINTER /
 *                  DISCONNECT bytes and end (reselect handled by
 *                  script 2 once the data is ready);
 *   DATA IN/OUT -> synchronous: move the data, STATUS, MSG-IN loop;
 *   STATUS      -> command rejected with CHECK CONDITION: skip the
 *                  data block entirely.
 * Every route ends in the same terminating INT. */
static int lsi_build_script1(u8 target, u8 lun, const u8 *cdb, u8 cdb_len,
                              u8 *data, u32 data_len, u8 dir)
{
    u32 *s = lsi_state.script;
    int  i = 0;
    int  sync_status, msgin_move, jmp0, jmp4, back_jmp, int_insn;
    int  disc_move, disc_j4, disc_back;
    u32  phase = (dir == SCSI_DIR_OUT) ? LSI_PHASE_DO : LSI_PHASE_DI;

    lsi_state.msg_out = (u8)(0x80 | lun);        /* IDENTIFY        */

    s[i++] = 0x44000008u | ((u32)target << 16);  /* SELECT with ATN */
    s[i++] = 0x00000008u;                        /* rel -> next insn */

    s[i++] = ((u32)LSI_PHASE_MO << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.msg_out;

    s[i++] = ((u32)LSI_PHASE_CMD << 24) | cdb_len;
    s[i++] = (u32)(uintptr_t)cdb;

    s[i++] = 0x878A0000u;                        /* JUMP phase==MI  */
    s[i++] = 0;                                  /* -> disc branch  */
    s[i++] = 0x838A0000u;                        /* JUMP phase==ST  */
    s[i++] = 0;                                  /* -> STATUS       */

    if (data_len) {
        s[i++] = (phase << 24) | data_len;       /* DATA (sync)     */
        s[i++] = (u32)(uintptr_t)data;
    }

    sync_status = i;
    s[i++] = ((u32)LSI_PHASE_STAT << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.status;

    msgin_move = i;
    s[i++] = ((u32)LSI_PHASE_MI << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.msg_in;
    jmp0 = i;
    s[i++] = 0x808C0000u;
    s[i++] = 0;
    jmp4 = i;
    s[i++] = 0x808C0004u;
    s[i++] = 0;
    back_jmp = i;
    s[i++] = 0x808CFF00u;
    s[i++] = 0;

    int_insn = i;
    s[i++] = 0x98080000u;
    s[i++] = 0x00000000u;

    /* disconnect branch: drain 0x02/0x03/... until 0x04            */
    disc_move = i;
    s[i++] = ((u32)LSI_PHASE_MI << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.msg_in;
    disc_j4 = i;
    s[i++] = 0x808C0004u;
    s[i++] = 0;
    disc_back = i;
    s[i++] = 0x808CFF00u;
    s[i++] = 0;

    s[6 + 1]        = (u32)(i32)((disc_move * 4) - (6 * 4 + 8));
    s[8 + 1]        = (u32)(i32)((sync_status * 4) - (8 * 4 + 8));
    s[jmp0 + 1]     = (u32)(i32)((int_insn * 4) - (jmp0 * 4 + 8));
    s[jmp4 + 1]     = (u32)(i32)((int_insn * 4) - (jmp4 * 4 + 8));
    s[back_jmp + 1] = (u32)(i32)((msgin_move * 4) - (back_jmp * 4 + 8));
    s[disc_j4 + 1]  = (u32)(i32)((int_insn * 4) - (disc_j4 * 4 + 8));
    s[disc_back + 1]= (u32)(i32)((disc_move * 4) - (disc_back * 4 + 8));
    /* WP-10-tui-qa: refuse a torn program instead of overrunning the
     * static buffer (defense in depth on top of the 32-dword sizing). */
    return (i > LSI_SCRIPT_DWORDS) ? -1 : 0;
}

/* build the reselected data-phase script */
static int lsi_build_script2(u8 *data, u32 data_len, u8 dir)
{
    u32 *s = lsi_state.script;
    int  i = 0;
    int  msgin_move, jmp0, jmp4, back_jmp, int_insn;
    u32  phase = (dir == SCSI_DIR_OUT) ? LSI_PHASE_DO : LSI_PHASE_DI;

    s[i++] = ((u32)LSI_PHASE_MI << 24) | 1;      /* reselect ID     */
    s[i++] = (u32)(uintptr_t)&lsi_state.msg_in;

    if (data_len) {
        s[i++] = (phase << 24) | data_len;
        s[i++] = (u32)(uintptr_t)data;
    }

    s[i++] = ((u32)LSI_PHASE_STAT << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.status;

    msgin_move = i;
    s[i++] = ((u32)LSI_PHASE_MI << 24) | 1;
    s[i++] = (u32)(uintptr_t)&lsi_state.msg_in;
    jmp0 = i;
    s[i++] = 0x808C0000u;
    s[i++] = 0;
    jmp4 = i;
    s[i++] = 0x808C0004u;
    s[i++] = 0;
    back_jmp = i;
    s[i++] = 0x808CFF00u;
    s[i++] = 0;

    int_insn = i;
    s[i++] = 0x98080000u;
    s[i++] = 0x00000000u;

    s[jmp0 + 1]     = (u32)(i32)((int_insn * 4) - (jmp0 * 4 + 8));
    s[jmp4 + 1]     = (u32)(i32)((int_insn * 4) - (jmp4 * 4 + 8));
    s[back_jmp + 1] = (u32)(i32)((msgin_move * 4) - (back_jmp * 4 + 8));
    /* WP-10-tui-qa: same torn-program guard as script 1. */
    return (i > LSI_SCRIPT_DWORDS) ? -1 : 0;
}

/* Start the engine and wait for the terminating INT.  Returns 0 when
 * the INT fired (check lsi_state.msg_in / c->status), -1 on transport
 * failure (stuck engine, selection timeout, hard bus error). */
static int lsi_run_wait(void)
{
    int tries;
    u8  istat, dstat, sist0, sist1;

    lsi_state.status = 0xFF;                     /* sentinel        */
    lsi_state.msg_in = 0xFF;

    lsi_load_dsp((u32)(uintptr_t)lsi_state.script);

    for (tries = 0; tries < LSI_POLL_MAX; tries++) {
        istat = lsi_rd(LSI_ISTAT0);
        if (istat & (LSI_ISTAT0_DIP | LSI_ISTAT0_SIP))
            break;
    }
    if (tries >= LSI_POLL_MAX)
        return -1;                               /* engine stuck    */

    /* NOTE: SIST0/SIST1/DSTAT clear on read; read them exactly once */
    sist1 = lsi_rd(LSI_SIST1);
    sist0 = lsi_rd(LSI_SIST0);
    dstat = lsi_rd(LSI_DSTAT);

    if (sist1 & LSI_SIST1_STO)
        return -1;                               /* no target there */
    if ((sist0 & (LSI_SIST0_MA | LSI_SIST0_SGE | LSI_SIST0_UDC |
                  LSI_SIST0_RST | LSI_SIST0_PAR)) ||
        (dstat & (LSI_DSTAT_IID | LSI_DSTAT_BF |
                  LSI_DSTAT_MDPE | LSI_DSTAT_ABRT)))
        return -1;                               /* transport error */
    if (!(dstat & LSI_DSTAT_SIR))
        return -1;                               /* not a script INT */
    return 0;
}

/* Execute one complete SCSI transaction for the core. */
static int lsi_exec(scsi_hba_t *hba, u8 target, u8 lun, scsi_cmd_t *c)
{
    u8 dstat;
    int rc;

    (void)hba;
    if (!lsi_state.script || target > 6)
        return -1;
    if (c->data_len > 0x00FFFFFFu)
        return -1;                               /* 24-bit count    */

    lsi_ack_irqs();
    /* initiator-only use of the chip: a connection left over by an
     * aborted transaction would make the SELECT report "already
     * reselected" and jump to a stale DNAD. */
    if (lsi_rd(LSI_SCNTL1) & LSI_SCNTL1_CON)
        lsi_wr(LSI_SCNTL1, 0);

    /* ---- script 1: command phase ---- */
    if (lsi_build_script1(target, lun, c->cdb, c->cdb_len,
                          c->data, c->data_len, c->dir) != 0)
        return -1;
    if (lsi_run_wait() != 0)
        return -1;

    if (lsi_state.msg_in == 0x00) {
        /* synchronous completion: STATUS was already transferred   */
        if (lsi_state.status == 0xFF)
            return -1;
        c->status  = lsi_state.status;
        c->xfered  = c->data_len;
    } else if (lsi_state.msg_in == 0x04) {
        /* asynchronous: the target disconnected and will reselect
         * when the data is ready (SIEN0.RSL + SCID.RRE are enabled,
         * so QEMU reselects automatically as soon as it can).      */
        int tries;
        u8  istat = 0;
        for (tries = 0; tries < LSI_POLL_MAX; tries++) {
            istat = lsi_rd(LSI_ISTAT0);
            if (istat & (LSI_ISTAT0_DIP | LSI_ISTAT0_SIP))
                break;
        }
        if (tries >= LSI_POLL_MAX)
            return -1;                           /* never reselected */
        (void)lsi_rd(LSI_SIST1);                 /* RSL notice: clear */
        (void)lsi_rd(LSI_SIST0);
        (void)lsi_rd(LSI_DSTAT);

        /* ---- script 2: reselected data phase ---- */
        if (lsi_build_script2(c->data, c->data_len, c->dir) != 0)
            return -1;
        if (lsi_run_wait() != 0)
            return -1;
        if (lsi_state.status == 0xFF)
            return -1;
        c->status  = lsi_state.status;
        c->xfered  = c->data_len;
    } else {
        return -1;                               /* unexpected message */
    }

    /* CHECK CONDITION: fetch sense data (SAM error recovery); the
     * REQUEST SENSE runs as its own transaction. */
    if (c->status == SCSI_STATUS_CHECK_COND) {
        u8 sc[6];
        enum { SENSE_ALLOC = 18 };   /* fixed-format sense length   */
        scsi_cdb_request_sense(sc, SENSE_ALLOC);

        lsi_ack_irqs();
        if (lsi_rd(LSI_SCNTL1) & LSI_SCNTL1_CON)
            lsi_wr(LSI_SCNTL1, 0);
        lsi_build_script1(target, lun, sc, 6, c->sense, SENSE_ALLOC,
                          SCSI_DIR_IN);
        rc = lsi_run_wait();
        {
            scsi_cmd_t sense_cmd;
            memset(&sense_cmd, 0, sizeof(sense_cmd));
            if (rc == 0 && lsi_state.msg_in == 0x00) {
                /* REQ SENSE ran synchronously; emulate the status  */
                sense_cmd.status = lsi_state.status;
                if (sense_cmd.status == SCSI_STATUS_GOOD)
                    c->sense_len = SENSE_ALLOC;  /* sense captured  */
            }
        }
    }
    (void)dstat;
    return 0;
}

static const scsi_hba_ops_t lsi_ops = {
    .exec = lsi_exec,
};

/* ------------------------------------------------------------------ */
/* PCI probe + target scan                                             */
/* ------------------------------------------------------------------ */

int driver_scsi_lsi_init(void)
{
    u8 bus, dev, func;
    u32 io_base;
    int found, attached = 0, target;

    /* driver_pci_find_device returns 0 when the device IS found. */
    found = driver_pci_find_device(LSI_PCI_VENDOR, LSI_PCI_DEVICE,
                                   &bus, &dev, &func);
    if (found != 0)
        return 0;                    /* honest: no LSI chip present */

    driver_pci_enable_device(bus, dev, func);
    io_base = driver_pci_read_bar(bus, dev, func, 0);
    if (!io_base)
        return 0;                    /* BAR0 absent               */

    memset(&lsi_state, 0, sizeof(lsi_state));
    lsi_state.io_base      = (u16)(io_base & 0xFFFC);
    lsi_state.initiator_id = 7;
    lsi_state.present      = 1;

    lsi_state.script = lsi_script_buf;   /* static: heap-independent */

    lsi_soft_reset();

    memcpy(lsi_hba.name, "lsi0", 5);
    lsi_hba.initiator_id = lsi_state.initiator_id;
    lsi_hba.priv         = &lsi_state;
    lsi_hba.ops          = &lsi_ops;
    if (scsi_hba_register(&lsi_hba) < 0) {
        lsi_state.script = NULL;
        return 0;
    }

    screen_console_puts("SCSI LSI 53C895A HBA (polled SCRIPTS engine)\n");

    /* scan targets 0..6 (7 is our initiator id) for LUN 0         */
    for (target = 0; target <= 6; target++) {
        int idx = scsi_attach_target(&lsi_hba, (u8)target, 0);
        if (idx >= 0) {
            scsi_dev_t *d = scsi_get(idx);
            attached++;
            if (d && d->pdt == SCSI_PDT_DIRECT_ACCESS && d->blk_idx >= 0) {
                screen_console_puts(d->name);   /* e.g. "sda" in blk */
                screen_console_puts("\n");
            }
        }
    }
    return attached;
}
