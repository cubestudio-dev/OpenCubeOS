/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_menu.c
 * Purpose: the WP-07 takeover menus - see ocire_menu.h.
 *
 *          Two menus share one blocking key loop:
 *
 *          BOOT-FAILED takeover (two consecutive judged boot
 *          failures routed the machine here):
 *            [R] Rollback to the previous OCOS version
 *            [C] Continue and try booting OCOS again
 *
 *          WAKE-FAILED takeover (SLEEP_ARMED survived a reboot):
 *            [W] Wake from sleep failed.   (diagnostics)
 *            [R] Rollback to the previous OCOS version
 *            [C] Try to boot OCOS normally
 *
 *          The loop REALLY blocks: every other key is ignored and
 *          there is no timeout - the machine stays in recovery
 *          until a human decides (the spec's explicit requirement).
 *
 *          [R] flips the OCOS A/B slot flags (the same next_B /
 *          ok_B / bootfail_B protocol the GRUB config and OCBL's
 *          scanner implement) and reboots; [C] clears the failure
 *          record and reboots into OCOS.
 */
#include "ocire_menu.h"
#include "ocire_console.h"
#include "ocire_input.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_acpi.h"
#include "ocire_ata.h"
#include "ocire_fat.h"
#include "ocire_disk.h"
#include "ocbl_state.h"        /* the shared three-way contract */

/* ---- the boot record accessors (the OCIRE side) -------------------- */

static volatile ocst_record_t* rec(void)
{
    return (volatile ocst_record_t *)OCST_RESERVED_ADDR;
}

static volatile ocst_state_t* state_area(void)
{
    return (volatile ocst_state_t *)OCST_STATE_ADDR;
}

/* Invalidate the boot record (any kind): a NONE-kind record never
 * validates, so OCBL's next run starts from a clean slate. */
static void record_clear(void)
{
    volatile ocst_record_t* r = rec();
    r->kind = OCST_KIND_NONE;
    r->checksum = ocst_record_checksum(r);
}

/* Clear a kernel flag in the live state area (SLEEP_ARMED residue). */
static void state_clear_flag(ocst_u8 mask)
{
    volatile ocst_state_t* s = state_area();
    if (ocst_state_valid(s))
        s->kflags = (ocst_u8)(s->kflags & (ocst_u8)~mask);
}

/* ---- machine reset (the same ladder the shell uses) --------------- */

static void reboot_machine(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(oc_inb(0x64) & 2)) break;
    }
    oc_outb(0x64, 0xFE);
    ocire_acpi_reset();
    oc_outb(0xCF9, 0x02);
    oc_outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 2000000; i++) { }
    ocire_puts("Reset failed on this machine; halting.\n");
    oc_halt();
}

/* ---- the OCOS A/B slot rollback -------------------------------------
 * The WP-10u A/B disk: p1 = FAT32 flag volume (/boot/next_B, ok_B,
 * bootfail_B), p2 = slot A, p3 = slot B (both carry the kernel).
 * Selection semantics (grub-ab.cfg / ocbl_scan.c, mirrored here):
 *   next_B && !bootfail_B -> B ; ok_B -> B ; else -> A            */

#pragma pack(push, 1)
typedef struct {
    u8  boot;
    u8  start_chs[3];
    u8  type;
    u8  end_chs[3];
    u32 start_lba;
    u32 sectors;
} m_part_t;

typedef struct {
    u8       code[446];
    m_part_t parts[4];
    u16      sig;
} m_mbr_t;
#pragma pack(pop)

/* Probe the MBR partition `idx` on `ata` for a FAT32 volume; mount
 * it into *v.  Returns 0. */
static int mount_part(int ata, int idx, ocire_fat_vol_t *v)
{
    u8 sec[512];
    if (ocire_ata_read(ata, 0, 1, sec) != 0) return -1;
    m_mbr_t *mbr = (m_mbr_t *)sec;
    if (mbr->sig != 0xAA55) return -1;
    u64 start = mbr->parts[idx].start_lba;
    u64 n = mbr->parts[idx].sectors;
    if (start == 0 || n < 2048) return -1;
    if (ocire_fat_mount(v, ata, start) != 0 || v->fat_type != 32)
        return -1;
    return 0;
}

/* Returns the ACTIVE OCOS slot on the A/B disk ('A'/'B'), or 0 when
 * no A/B layout was found.  Also fills ata_out/flag_vol with the
 * mounted flag volume so the caller can flip it. */
static int ocos_ab_active(int *ata_out, ocire_fat_vol_t *flag_vol)
{
    int n = ocire_ata_scan();
    for (int i = 0; i < n; i++) {
        const ocire_ata_dev_t *d = ocire_ata_dev(i);
        if (!d->present) continue;
        ocire_fat_vol_t v;
        if (mount_part(i, 0, &v) != 0) continue;      /* p1 */
        ocire_fat_file_t f;
        if (ocire_fat_lookup(&v, "BOOT/GRUB/GRUB.CFG", &f) != 0)
            continue;                                 /* not the flag part */
        /* p2/p3 must both carry kernels for a real A/B pair */
        ocire_fat_vol_t s2, s3;
        ocire_fat_file_t k2, k3;
        if (mount_part(i, 1, &s2) != 0 ||
            ocire_fat_lookup(&s2, "BOOT/OPENCUBE.ELF", &k2) != 0)
            continue;
        if (mount_part(i, 2, &s3) != 0 ||
            ocire_fat_lookup(&s3, "BOOT/OPENCUBE.ELF", &k3) != 0)
            continue;
        *ata_out = i;
        *flag_vol = v;
        int next_b = (ocire_fat_lookup(&v, "BOOT/NEXT_B", &f) == 0);
        int ok_b = (ocire_fat_lookup(&v, "BOOT/OK_B", &f) == 0);
        int bootfail_b = (ocire_fat_lookup(&v, "BOOT/BOOTFAIL_B", &f) == 0);
        if ((next_b && !bootfail_b) || (!next_b && ok_b))
            return 'B';
        return 'A';
    }
    return 0;
}

/* Flip the OCOS A/B flags to the OTHER slot.  Returns 0 on success:
 *   0  flags flipped + written
 *  -1  no A/B layout found
 *  -2  I/O error while writing
 *  -3  the other slot has no kernel (refused, nothing written)     */
static int ocos_ab_rollback(void)
{
    int ata;
    ocire_fat_vol_t v;
    int active = ocos_ab_active(&ata, &v);
    if (!active) return -1;

    if (active == 'B') {
        /* active B -> roll back to A: drop both B-selecting flags */
        ocire_fat_file_t f;
        int had = 0;
        if (ocire_fat_lookup(&v, "BOOT/NEXT_B", &f) == 0) {
            if (ocire_fat_delete(&v, "BOOT/NEXT_B") == 0) had = 1;
            else return -2;
        }
        if (ocire_fat_lookup(&v, "BOOT/OK_B", &f) == 0) {
            if (ocire_fat_delete(&v, "BOOT/OK_B") == 0) had = 1;
            else return -2;
        }
        return had ? 0 : 0;    /* already A: idempotent success */
    }
    /* active A -> roll back to B: require a kernel there, then ok_B */
    {
        ocire_fat_vol_t s3;
        ocire_fat_file_t k3;
        if (mount_part(ata, 2, &s3) != 0 ||
            ocire_fat_lookup(&s3, "BOOT/OPENCUBE.ELF", &k3) != 0)
            return -3;
    }
    static const u8 one = '1';
    if (ocire_fat_create_file(&v, "BOOT/OK_B", &one, 1) != 0)
        return -2;
    return 0;
}

/* ---- the menus ------------------------------------------------------ */

static char wait_key(const char *keys)
{
    for (;;) {
        char c;
        while (!ocire_getchar(&c)) { }     /* hard block: no timeout */
        if (c >= 'a' && c <= 'z') c -= 32;
        for (const char *k = keys; *k; k++)
            if (c == *k) {
                ocire_putc(c);
                ocire_putc('\n');
                return c;
            }
        /* anything else is ignored - the menu truly waits */
    }
}

/* The shared [R]/[C] tail: R = rollback + clean reboot,
 * C = clear + normal reboot. */
static void menu_tail(int allow_wake_details)
{
    const char *keys = allow_wake_details ? "WRC" : "RC";
    for (;;) {
        char k = wait_key(keys);
        if (k == 'W') {
            const volatile ocst_record_t* r = rec();
            ocire_puts("\nWake-failure diagnostics:\n");
            ocire_putdec("  judgment unix time ", (u64)r->ts_unix);
            ocire_putdec(", silence ", (u64)r->silence_sec);
            ocire_putdec("s, consecutive boot failures ",
                         (u64)r->boot_fails);
            ocire_puts("\n  SLEEP_ARMED was still set when the machine "
                       "came back: the suspend-to-RAM\n"
                       "  resume vector crashed or the machine died "
                       "mid-sleep before the flag was cleared.\n");
            const volatile ocst_state_t* s = state_area();
            if (ocst_state_valid(s)) {
                ocire_puts("  state area: stage ");
                ocire_puts(ocst_stage_name(s->stage));
                ocire_putdec(", heartbeat ", (u64)s->heartbeat);
                ocire_puts(" ticks\n");
            }
            ocire_puts("\nChoose R or C:\n");
            continue;
        }
        if (k == 'R') {
            ocire_puts("Rolling back to the previous OCOS version...\n");
            int rc = ocos_ab_rollback();
            if (rc == -1) {
                ocire_puts("No OCOS A/B slot layout found on any disk.\n"
                           "The system volume is single-slot; there is no "
                           "previous version to return to.\n"
                           "Clearing the failure record and rebooting "
                           "normally.\n");
            } else if (rc == -2) {
                ocire_puts("I/O error while flipping the slot flags - "
                           "the rollback was NOT applied.\n"
                           "Clearing the failure record and rebooting "
                           "normally.\n");
            } else if (rc == -3) {
                ocire_puts("The other slot carries no OCOS kernel - "
                           "refusing to switch.\n"
                           "Clearing the failure record and rebooting "
                           "normally.\n");
            } else {
                ocire_puts("Slot flags flipped. The next boot uses the "
                           "previous OCOS version.\n");
            }
            record_clear();
            state_clear_flag(OCST_KFLAG_SLEEP_ARMED);
            ocire_puts("Rebooting...\n");
            reboot_machine();              /* never returns */
        }
        if (k == 'C') {
            ocire_puts("Clearing the failure record and rebooting into "
                       "Open Cube OS...\n");
            record_clear();
            state_clear_flag(OCST_KFLAG_SLEEP_ARMED);
            reboot_machine();              /* never returns */
        }
    }
}

void ocire_menu_takeover(void)
{
    const volatile ocst_record_t* r = rec();
    if (!ocst_record_valid(r))
        return;                             /* normal boot: no menu */

    if (r->kind == OCST_KIND_WAKE_FAILED) {
        ocire_puts("\n==================================================\n");
        ocire_puts(" S3/S0ix WAKE FAILURE DETECTED (OCIRE takeover)\n");
        ocire_puts("==================================================\n");
        ocire_puts("The system armed suspend-to-RAM but the wake flag "
                   "was still set\nwhen the machine rebooted: the resume "
                   "path crashed or the machine\n died mid-sleep.\n");
        ocire_putdec("Consecutive boot failures on record: ",
                     (u64)r->boot_fails);
        ocire_puts("\n\n");
        ocire_puts(" [W] Wake from sleep failed.\n");
        ocire_puts(" [R] Rollback to the previous OCOS version\n");
        ocire_puts(" [C] Try to boot OCOS normally\n");
        ocire_puts("\nSelect an option (the menu waits for a key): ");
        menu_tail(1);
        return;                             /* unreachable */
    }

    if (r->kind == OCST_KIND_BOOT_FAILED) {
        ocire_puts("\n==================================================\n");
        ocire_puts(" OPEN CUBE OS FAILED TO BOOT (OCIRE takeover)\n");
        ocire_puts("==================================================\n");
        ocire_putdec("Consecutive boot failures: ", (u64)r->boot_fails);
        ocire_puts(" - the last one stalled at stage ");
        ocire_puts(ocst_stage_name(r->stall));
        ocire_putdec(" (no progress for ", (u64)r->silence_sec);
        ocire_puts("s).\n\n");
        ocire_puts(" [R] Rollback to the previous OCOS version\n");
        ocire_puts(" [C] Continue and try booting OCOS again\n");
        ocire_puts("\nSelect an option (the menu waits for a key): ");
        menu_tail(0);
        return;                             /* unreachable */
    }

    /* OCST_KIND_USER_REQUEST: the plain recovery session, no menu */
}
