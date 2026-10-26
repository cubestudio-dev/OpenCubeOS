/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_main.c
 * Purpose: OCIRE kernel entry (called by ocire_boot.S after the
 *          switch to 64-bit long mode).
 *
 * WP-01 scope: prove that OCIRE boots independently and runs a minimal
 * shell (help / shutdown / reboot).  Recovery features (WP-02), the
 * OCOS<->OCIRE interface and process protection (WP-03) are NOT here.
 */

#include "ocire_console.h"
#include "ocire_idt.h"
#include "ocire_input.h"
#include "ocire_acpi.h"
#include "ocire_shell.h"
#include "ocire_version_gen.h"

void ocire_main(void) {
    ocire_console_init();

    ocire_puts("\nOCIRE - OpenCubeOS Independent Recovery Environment\n");
    ocire_puts(OCIRE_STAMP);
    ocire_puts("\n");
    ocire_puts("Independent recovery kernel (L0 peer of Open Cube OS).\n");

    ocire_idt_init();
    ocire_puts("CPU exception handlers armed.\n");

    ocire_input_init();

    if (ocire_acpi_init() == 0) {
        ocire_puts("ACPI: FADT found");
        if (ocire_acpi_slp_typ_s5() >= 0) {
            ocire_putdec(" (S5 SLP_TYP=", (u64)ocire_acpi_slp_typ_s5());
            ocire_puts(")\n");
        } else {
            ocire_puts(" (no \\_S5 package - legacy ports will be used)\n");
        }
    } else {
        ocire_puts("ACPI: not found - legacy power ports will be used\n");
    }

    ocire_puts("Ready. Type 'help' for the command list.\n\n");

    ocire_shell_run();   /* never returns */
}
