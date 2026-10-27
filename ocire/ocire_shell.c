/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_shell.c
 * Purpose: the OCIRE full recovery shell.
 *
 * WP-02 command set:
 *   help         - list available commands
 *   logread      - dump the boot-log ring (the OCOS agent's record)
 *   syscheck     - ten-point integrity self-check
 *   repair quick - fix metadata + restore damaged system files
 *   repair full  - reformat + full golden rewrite (asks YES)
 *   return       - reboot into OpenCubeOS (normal-boot mailbox)
 *   shutdown     - power off (ACPI S5, with legacy port fallbacks)
 *   reboot       - reset the machine (8042, ACPI RESET_REG, 0xCF9)
 *
 * Line editing supports backspace.  Anything unrecognised prints an
 * honest "unknown command" hint - never a silent drop.
 */

#include "ocire_shell.h"
#include "ocire_console.h"
#include "ocire_input.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_acpi.h"
#include "ocire_recover.h"

#define OCIRE_PROMPT "ocire> "
#define LINE_MAX 96

static void cmd_help(void) {
    ocire_puts("OCIRE commands:\n");
    ocire_puts("  help         - list available commands\n");
    ocire_puts("  logread      - dump the boot-log ring (OCOS agent record)\n");
    ocire_puts("  syscheck     - ten-point integrity self-check\n");
    ocire_puts("  repair quick - fix metadata + restore damaged system files\n");
    ocire_puts("  repair full  - reformat the system volume + golden rewrite\n");
    ocire_puts("  return       - reboot into OpenCubeOS (normal-boot mailbox)\n");
    ocire_puts("  shutdown     - power off the machine (ACPI S5)\n");
    ocire_puts("  reboot       - reset the machine\n");
}

static void cmd_shutdown(void) {
    ocire_puts("Shutting down...\n");
    /* Mainstream ACPI S5 first. */
    if (ocire_acpi_enter_s5() != 0) {
        /* Legacy port fallbacks (QEMU/PIIX4, Bochs, APM). */
        oc_outw(0x604, 0x2000);    /* QEMU PIIX4 PM1a_CNT SLP_EN|SLP_TYP */
        oc_outw(0xB004, 0x2000);   /* Bochs */
        oc_outw(0xF000, 0x0000);   /* legacy APM */
        for (volatile int i = 0; i < 2000000; i++) { }
        ocire_puts("Power off not supported on this machine; halting.\n");
        oc_halt();
    }
    /* unreachable when power off works */
    oc_halt();
}

static void cmd_reboot(void) {
    ocire_puts("Rebooting...\n");
    /* 8042 keyboard-controller pulse first (mainstream PC reset). */
    for (int i = 0; i < 100000; i++) {
        if (!(oc_inb(0x64) & 2)) break;
    }
    oc_outb(0x64, 0xFE);
    /* ACPI RESET_REG next (QEMU/edk2: port 0xCF9). */
    ocire_acpi_reset();
    /* Raw 0xCF9 "hard reset" sequence as the final fallback. */
    oc_outb(0xCF9, 0x02);
    oc_outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 2000000; i++) { }
    ocire_puts("Reset failed on this machine; halting.\n");
    oc_halt();
}

static void read_line(char *buf, int cap) {
    int len = 0;
    for (;;) {
        char c;
        while (!ocire_getchar(&c)) { }
        if (c == '\n') {
            ocire_putc('\n');
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                ocire_putc('\b');
                ocire_putc(' ');
                ocire_putc('\b');
            }
            continue;
        }
        if (c == '\r') continue;      /* ignore stray CR */
        if (len < cap - 1) {
            buf[len++] = c;
            ocire_putc(c);             /* echo */
        }
    }
    buf[len] = 0;
}

void ocire_shell_run(void) {
    static char line[LINE_MAX];
    for (;;) {
        ocire_puts(OCIRE_PROMPT);
        read_line(line, LINE_MAX);
        /* Trim leading spaces. */
        char *s = line;
        while (*s == ' ') s++;
        if (!s[0]) continue;
        if (oc_strcmp(s, "help") == 0) {
            cmd_help();
        } else if (oc_strcmp(s, "logread") == 0) {
            ocire_cmd_logread();
        } else if (oc_strcmp(s, "syscheck") == 0) {
            ocire_cmd_syscheck();
        } else if (oc_strcmp(s, "repair quick") == 0) {
            ocire_cmd_repair_quick();
        } else if (oc_strcmp(s, "repair full") == 0) {
            ocire_cmd_repair_full();
        } else if (oc_strcmp(s, "return") == 0) {
            ocire_cmd_return();            /* does not return */
        } else if (oc_strcmp(s, "repair") == 0) {
            ocire_puts("usage: repair quick | repair full\n");
        } else if (oc_strcmp(s, "shutdown") == 0) {
            cmd_shutdown();            /* does not return on success */
        } else if (oc_strcmp(s, "reboot") == 0) {
            cmd_reboot();              /* does not return on success */
        } else {
            ocire_puts("Unknown command: ");
            ocire_puts(s);
            ocire_puts("  (try 'help')\n");
        }
    }
}
