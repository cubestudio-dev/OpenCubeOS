/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_main.c
 * Purpose: OCIRE kernel entry (called by ocire_boot.S after the
 *          switch to 64-bit long mode).
 *
 * WP-02 boot roles (selected by the GRUB menu command line, see
 * ocire_mb2.h):
 *
 *   ocire.mode=sup  - BOOT SUPERVISOR.  The default disk-menu entry.
 *                     Arms the hardware watchdog (60 s), records
 *                     WAIT_BOOT_SIGNAL in the OCIRE mailbox, injects
 *                     the log agent as a multiboot2 module and hands
 *                     the machine over to OCOS (kexec).  When the
 *                     previous boot never signalled (watchdog reset),
 *                     it shows the crash notice and reboots into the
 *                     full recovery environment.
 *
 *   ocire.mode=full - FULL RECOVERY SHELL.  The manual menu entry:
 *                     logread / syscheck / repair / return.
 *
 *   (no cmdline)    - standalone media (the ISO): no disks to
 *                     supervise, degrade into the full shell.
 */

#include "ocire_console.h"
#include "ocire_idt.h"
#include "ocire_input.h"
#include "ocire_acpi.h"
#include "ocire_shell.h"
#include "ocire_mb2.h"
#include "ocire_version_gen.h"

void ocire_main(u64 magic, u64 mbi) {
    /* WP-02: the multiboot2 info must be parsed BEFORE the console
     * inits - the console picks its video backend (LFB vs VGA text)
     * from the framebuffer tag. */
    int mb_ok = (ocire_mb2_parse(magic, mbi) == 0);

    ocire_console_init();

    ocire_puts("\nOCIRE - OpenCubeOS Independent Recovery Environment\n");
    ocire_puts(OCIRE_STAMP);
    ocire_puts("\n");
    ocire_puts("Independent recovery kernel (L0 peer of Open Cube OS).\n");

    if (mb_ok) {
        const ocire_mb2_info_t *mb = ocire_mb2_get();
        ocire_puts("Video: ");
        if (ocire_console_is_lfb()) {
            ocire_putdec("linear framebuffer ", (u64)mb->fb->width);
            ocire_putdec("x", (u64)mb->fb->height);
            ocire_putdec("x", (u64)mb->fb->bpp);
            ocire_puts("\n");
        } else if (mb->fb) {
            ocire_puts("framebuffer tag present but not RGB32 (type=");
            ocire_putdec("", (u64)mb->fb->fb_type);
            ocire_putdec(" bpp=", (u64)mb->fb->bpp);
            ocire_puts(") - VGA text fallback\n");
        } else {
            ocire_puts("VGA text 80x25 (no framebuffer tag)\n");
        }
        if (mb->cmdline) {
            ocire_puts("Cmdline: ");
            ocire_puts(mb->cmdline);
            ocire_puts("\n");
        }
    } else {
        ocire_puts("Video: VGA text 80x25 (no multiboot2 info)\n");
    }

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

    /* ---- role dispatch (WP-02) ------------------------------------
     * ocire.mode=sup  -> the boot supervisor (watchdog closed loop);
     * ocire.mode=full -> straight into the recovery shell;
     * no cmdline      -> standalone media (ISO): full shell. */
    if (ocire_mb2_cmdline_kv("ocire.mode", "sup")) {
        extern void ocire_supervisor_run(void);
        ocire_supervisor_run();       /* falls through on degrade */
    }

    ocire_puts("Ready. Type 'help' for the command list.\n\n");

    ocire_shell_run();   /* never returns */
}
