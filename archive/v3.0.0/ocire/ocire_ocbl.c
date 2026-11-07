/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocire_state
 * File: ocire/ocire_ocbl.c
 * Purpose: the OCIRE reader for the OCBL boot record + state area -
 *          see ocire_ocbl.h and include/ocbl_state.h.
 *
 *          This is the "reserved area read interface" the WP07a spec
 *          asks for: a validated reader that explains WHY the machine
 *          is in recovery (a judged boot failure at a known stage, or
 *          an explicit user request) and maps the stall point to a
 *          concrete repair suggestion built from OCIRE's real command
 *          set (syscheck / repair / logread).
 */
#include "ocire_ocbl.h"
#include "ocire_console.h"
#include "ocbl_state.h"        /* the shared contract (include/) */

static const volatile ocst_record_t* boot_record(void)
{
    return (const volatile ocst_record_t *)OCST_RESERVED_ADDR;
}

static const volatile ocst_state_t* state_area(void)
{
    return (const volatile ocst_state_t *)OCST_STATE_ADDR;
}

/* the per-stage repair advice (real OCIRE commands only) */
static const char* stall_advice(u8 stall)
{
    switch (stall) {
    case OCST_STAGE_RESET:
    case OCST_STAGE_KM_AIN_ENTERED:
    case OCST_STAGE_SCHED_READY:
        return "The kernel failed to start or stalled in its earliest "
               "bring-up. Run syscheck to verify the boot image, then "
               "retry the boot.";
    case OCST_STAGE_DRIVERS_READY:
        return "A driver likely hung during initialization. Run "
               "syscheck, review recently installed drivers on the "
               "boot volume, remove or replace the suspect one, then "
               "retry the boot.";
    case OCST_STAGE_SERVICES_READY:
        return "A root service hung while coming up. Run repair to "
               "restore the system volume from the golden image, then "
               "retry the boot.";
    case OCST_STAGE_L1_LAUNCHED:
        return "The default L1 application (ush) failed to come up. "
               "Run syscheck to verify the system image, then retry "
               "the boot.";
    case OCST_STAGE_L1_READY:
        return "The system hung while running. Run logread to inspect "
               "the last kernel log entries; run repair if the image "
               "turns out to be damaged.";
    default:
        return "Unknown stall point. Run syscheck to verify the "
               "system image.";
    }
}

int ocire_ocbl_report(void)
{
    const volatile ocst_record_t* r = boot_record();

    if (!ocst_record_valid(r)) {
        /* no record - a normal/cold boot; stay silent (the operator
         * asked for recovery by hand, the menu already said so) */
        return 0;
    }

    ocire_puts("OCBL boot record found (boot-state monitor).\n");

    if (r->kind == OCST_KIND_USER_REQUEST) {
        ocire_putdec("  Recovery session requested by the user (seq ",
                     (u64)r->seq);
        ocire_puts(", unix ts ");
        ocire_putdec("", (u64)r->ts_unix);
        ocire_puts(").\n");
        ocire_puts("  No boot failure was judged - this is a manual "
                   "recovery session.\n");
        return 1;
    }

    /* OCST_KIND_BOOT_FAILED */
    ocire_puts("  Previous boot FAILED at stage ");
    ocire_puts(ocst_stage_name(r->stall));
    ocire_puts(" (no progress for ");
    ocire_putdec("", (u64)r->silence_sec);
    ocire_puts("s, seq ");
    ocire_putdec("", (u64)r->seq);
    ocire_puts(", unix ts ");
    ocire_putdec("", (u64)r->ts_unix);
    ocire_puts(").\n");
    ocire_puts("  Advice: ");
    ocire_puts(stall_advice(r->stall));
    ocire_puts("\n");

    /* the live state area (this boot's OCBL-initialized copy) - the
     * read side of OCIRE's state-area access */
    {
        const volatile ocst_state_t* s = state_area();
        if (ocst_state_valid(s)) {
            ocire_puts("  State area: boot attempt seq ");
            ocire_putdec("", (u64)s->seq);
            ocire_puts(", stage ");
            ocire_putdec("", (u64)s->stage);
            ocire_puts(" (");
            ocire_puts(ocst_stage_name(s->stage));
            ocire_puts(").\n");
        }
    }
    return 1;
}
