/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_bootsvc.c
 * Purpose: the OCIRE boot supervisor - the WP-02 closed loop.
 *
 * Flow (ocire.mode=sup, the default GRUB menu entry):
 *
 *   scan disks   -> OCIRE volume (0xEE shadow + FAT12 + mailbox) and
 *                   the OCOS volume (FAT32 with BOOT/OPENCUBE.ELF);
 *                   either missing -> degrade to the full shell.
 *
 *   mailbox      -> decide what this boot is about:
 *     WAIT_BOOT_SIGNAL  The previous hand-over never got its signal:
 *                       the watchdog reset landed here.  Show the
 *                       crash notice, mark ENTER_FULL, wait 3 s so
 *                       the notice is readable, reboot -> the next
 *                       supervisor pass drops into the full shell.
 *     ENTER_FULL        One-shot flag from the OCOS-side "boot into
 *                       recovery" interface: clear it, run the full
 *                       shell (no reboot loop).
 *     NONE / SIGNAL_OK  Normal boot: arm the 60 s watchdog, record
 *                       WAIT_BOOT_SIGNAL, verify + stage the log
 *                       agent, kexec OCOS.  A healthy OCOS sends its
 *                       boot signal (mailbox = SIGNAL_OK + watchdog
 *                       disarmed) within the window; a hung one never
 *                       does and the watchdog pulls the machine back
 *                       here.
 *
 *   Every failure path DEGRADES to the full recovery shell with an
 *   honest message - the supervisor never leaves the machine unusable.
 */

#include "ocire_bootsvc.h"
#include "ocire_console.h"
#include "ocire_util.h"
#include "ocire_acpi.h"
#include "ocire_io.h"
#include "ocire_wdt.h"
#include "ocire_disk.h"
#include "ocire_fat.h"
#include "ocire_ata.h"
#include "ocire_mb2.h"
#include "ocire_kexec.h"
#include "ocire_sha256.h"

/* The OCOS kernel ELF is copied to this scratch address before its
 * segments are placed (ocire_kexec.h).  It is ~1.4 MiB; 4 MiB of
 * headroom covers every build we ship. */
#define OCOS_ELF_CAP (4 * 1024 * 1024)

static const char *state_name(u32 st) {
    switch (st) {
    case OCIRE_MB_STATE_NONE:        return "NONE";
    case OCIRE_MB_STATE_WAIT_SIGNAL: return "WAIT_BOOT_SIGNAL";
    case OCIRE_MB_STATE_SIGNAL_OK:   return "SIGNAL_OK";
    case OCIRE_MB_STATE_ENTER_FULL:  return "ENTER_FULL";
    default:                         return "UNKNOWN";
    }
}

/* Real machine reset: 8042 pulse, ACPI RESET_REG, then 0xCF9. */
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

/* Append a decimal to a growing string; returns the new end. */
static char *put_dec(char *p, u64 v) {
    char tmp[24];
    int k = 0;
    if (v == 0) { *p++ = '0'; return p; }
    while (v && k < 24) { tmp[k++] = (char)('0' + (v % 10)); v /= 10; }
    while (k) *p++ = tmp[--k];
    return p;
}

static char *put_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

void ocire_supervisor_run(void) {
    ocire_puts("\nOCIRE boot supervisor: watching the Open Cube OS boot.\n");

    /* ---- disk discovery ---- */
    int nd = ocire_ata_scan();
    ocire_putdec("ATA: ", (u64)nd);
    ocire_puts(" disk(s)\n");

    int src = ocire_disk_scan();
    if (src == -1) {
        ocire_puts("Supervisor: no OCIRE recovery volume found "
                   "(0xEE shadow + FAT12). Entering the full shell.\n");
        return;
    }
    if (src == -2) {
        ocire_puts("Supervisor: no OCOS system volume found "
                   "(FAT32 with /BOOT/OPENCUBE.ELF). Entering the full shell.\n");
        return;
    }
    if (src == -3) {
        ocire_puts("Supervisor: recovery volume malformed "
                   "(mailbox/log missing). Entering the full shell.\n");
        return;
    }
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    ocire_puts("Recovery volume: disk ");
    ocire_putdec("", (u64)L->ocire_idx);
    ocire_puts(", partition LBA ");
    ocire_putdec("", L->ocire_part_lba);
    ocire_puts(" (FAT12, 0xEE shadow)\n");
    ocire_puts("OCOS volume: disk ");
    ocire_putdec("", (u64)L->ocos_idx);
    ocire_puts(", partition LBA ");
    ocire_putdec("", L->ocos_part_lba);
    ocire_puts(" (FAT32)\n");

    /* ---- mailbox state machine ---- */
    ocire_mailbox_t *mb = &L->mb;
    ocire_puts("Mailbox: state ");
    ocire_puts(state_name(mb->state));
    ocire_putdec(", boots ", (u64)mb->boot_count);
    ocire_putdec(", timeouts ", (u64)mb->fail_count);
    ocire_puts("\n");

    if (mb->state == OCIRE_MB_STATE_WAIT_SIGNAL) {
        /* Previous boot never signalled: the watchdog reset landed
         * here (or the mailbox survived a hard power cut mid-boot).
         * WP-02: show the notice, then reboot into the full shell. */
        ocire_puts("\n");
        ocire_puts("OpenCubeOS seems to have crashed, getting ready to reboot to OCIRE\n");
        ocire_puts("\n");
        mb->fail_count++;
        mb->state = OCIRE_MB_STATE_ENTER_FULL;
        if (ocire_mailbox_write() != 0) {
            ocire_puts("Supervisor: mailbox write failed; continuing anyway.\n");
        }
        ocire_puts("Rebooting into the full recovery environment in 3 seconds...\n");
        ocire_sleep_sec(3);
        reboot_machine();                        /* never returns */
    }

    if (mb->state == OCIRE_MB_STATE_ENTER_FULL) {
        /* One-shot flag (set by the crash path above or the OCOS-side
         * recovery interface): clear it and run the full shell. */
        mb->state = OCIRE_MB_STATE_NONE;
        if (ocire_mailbox_write() == 0) {
            ocire_puts("Supervisor: recovery entry requested - "
                       "starting the full shell.\n");
        } else {
            ocire_puts("Supervisor: recovery entry requested "
                       "(mailbox write failed; continuing).\n");
        }
        return;                                   /* -> full shell */
    }

    /* ---- normal boot: stage, arm, hand over ---- */
    ocire_puts("Handing the machine to Open Cube OS...\n");

    /* Stage the agent FIRST (its digest is part of the mailbox the
     * OCOS side verifies before spawning it - process protection
     * layer 2). */
    static u8 agent[64 * 1024];
    u64 agent_len = 0;
    ocire_fat_file_t af;
    if (ocire_fat_lookup(&L->ocire_vol, "AGENT/AGENT.ELF", &af) == 0 &&
        af.size <= sizeof(agent)) {
        s64 got = ocire_fat_read_file(&L->ocire_vol, &af,
                                      agent, sizeof(agent));
        if (got > 0) {
            agent_len = (u64)got;
            u8 digest[32];
            ocire_sha256(agent, agent_len, digest);
            if (ocire_sha256_eq(digest, mb->agent_sha256) == 0) {
                ocire_puts("Log agent staged and digest verified (");
                ocire_putdec("", agent_len);
                ocire_puts(" bytes)\n");
            } else {
                char hex[65];
                ocire_sha256_hex(digest, hex);
                ocire_puts("WARNING: log agent digest mismatch -\n  computed ");
                ocire_puts(hex);
                ocire_puts("\n  mailbox  (recorded at build time)\n");
                ocire_puts("The agent will NOT be injected (tamper guard).\n");
                agent_len = 0;
            }
        }
    }
    if (agent_len == 0) {
        ocire_puts("Log agent unavailable - booting without it "
                   "(the OCOS log channel stays unused).\n");
    }

    /* Read the OCOS kernel. */
    static u8 elf_img[OCOS_ELF_CAP];
    ocire_fat_file_t kf;
    if (ocire_fat_lookup(&L->ocos_vol, "BOOT/OPENCUBE.ELF", &kf) != 0) {
        ocire_puts("Supervisor: /BOOT/OPENCUBE.ELF not found on the "
                   "OCOS volume. Entering the full shell.\n");
        return;
    }
    if (kf.size > sizeof(elf_img)) {
        ocire_putdec("Supervisor: OCOS kernel too large (", (u64)kf.size);
        ocire_puts(" bytes). Entering the full shell.\n");
        return;
    }
    s64 n = ocire_fat_read_file(&L->ocos_vol, &kf, elf_img, sizeof(elf_img));
    if (n != (s64)kf.size) {
        ocire_puts("Supervisor: short read on the OCOS kernel. "
                   "Entering the full shell.\n");
        return;
    }
    ocire_putdec("OCOS kernel: ", (u64)n);
    ocire_puts(" bytes loaded\n");

    /* The hand-over command line: where the mailbox / log ring live
     * (absolute LBAs, so the OCOS side needs no FAT12 code) and that
     * the watchdog must be stopped by the boot signal. */
    char extra[192];
    {
        u64 mb_lba = ocire_fat_file_first_lba(&L->ocire_vol, &L->mb_file);
        char *p = put_str(extra, "oc.log=");
        p = put_dec(p, (u64)mb->log_start_lba);
        p = put_str(p, " oc.logsecs=");
        p = put_dec(p, (u64)mb->log_sectors);
        p = put_str(p, " oc.mb=");
        p = put_dec(p, mb_lba);
        p = put_str(p, " oc.wdt=ib700");
        if (agent_len) p = put_str(p, " oc.agent=1");
        *p = 0;
    }

    /* Record WAIT_BOOT_SIGNAL, then arm the watchdog (order matters:
     * if we armed first and then died before the mailbox write, a
     * watchdog reset would look like an OCOS failure). */
    mb->state = OCIRE_MB_STATE_WAIT_SIGNAL;
    if (ocire_mailbox_write() != 0) {
        ocire_puts("Supervisor: FATAL - cannot write the mailbox. "
                   "Entering the full shell (watchdog left disarmed).\n");
        return;
    }
    if (ocire_wdt_present() || ocire_wdt_probe() == 0) {
        if (ocire_wdt_arm(60) == 0) {
            ocire_puts("Watchdog armed (i6300esb): 60 s boot window.\n");
        } else {
            ocire_puts("Watchdog arm FAILED - continuing without it "
                       "(mailbox-only signalling).\n");
        }
    } else {
        ocire_puts("No hardware watchdog (i6300esb) - continuing "
                   "without it (mailbox-only signalling).\n");
    }
    ocire_puts("Handing over to Open Cube OS...\n");

    int rc = ocire_kexec_exec(elf_img, (u64)n,
                              ocire_mb2_get()->mbi_phys, extra,
                              agent_len ? agent : (const u8 *)0, agent_len);
    /* kexec returned: something in the image was wrong.  Disarm the
     * watchdog so the machine does not reset-loop, degrade to full. */
    if (ocire_wdt_present()) ocire_wdt_disarm();
    ocire_putdec("Supervisor: kexec refused the image (rc ", (u64)(-rc));
    ocire_puts("). Watchdog disarmed; entering the full shell.\n");
}
