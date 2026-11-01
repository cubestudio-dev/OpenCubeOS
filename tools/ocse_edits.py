#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP06a-ocse
# File: tools/ocse_edits.py
#
# OCSE vendored-source edit groups - the single source of truth for every
# modification on top of the pristine v1.0.0 sources.
#
#   tools/vendor_ocse.sh   -> rebuilds ocse/{kernel,drivers,fs,boot,l1,
#                             chinese} from tag v1.0.0 (143 files)
#   tools/ocse_edits.py   -> applies the edit groups below (this file)
#
# Never edit a vendored file by hand: a re-vendor would erase it. Extend
# this script instead.
#
# Edit groups (as delivered for WP-06a):
#   E1  l1_ext hook surface reduced to what OCSE uses (OTA API stripped)
#   E2  GRUB wrapper stripped (disk_setup tooling out of the build)
#   E3  net stack not vendored (health counters stay zero)
#   E4  ring-3 crash plumbing stubbed (core_crash)
#   E5  ring-3 kill path removed (arch_exceptions)
#   E6  int 0x80 dropped (ring-0 gate + count-and-drop dispatch)
#   E7  USB MSC/serial pollers removed
#   E8  shell language coupling removed (lib_config)
#   E9  NIC/net resume hooks removed (acpi_power)
#   E10 base bug 1: aml_store_value uninitialized acpi_value_t copy
#   E11 base bug 2: aml_invoke_method early-returns leave *ret_out unwritten
#   E12 kernel/main.c rewritten into the 24-stage OCSE boot ladder
#       (template: tools/ocse_kmain_c)
#
# Idempotent: every edit detects "already applied" and skips. An anchor
# mismatch aborts loudly - that means the v1.0.0 upstream drifted and the
# edit needs re-verification, never a silent fuzzy match.

import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

applied = 0


def _rd(rel):
    return (ROOT / rel).read_text()


def _wr(rel, text):
    (ROOT / rel).write_text(text)


def edit(rel, old, new, expect=1):
    """Replace exactly `expect` occurrences of `old` with `new`."""
    global applied
    t = _rd(rel)
    n = t.count(old)
    if n == 0:
        if new and t.count(new) >= 1:      # already applied
            return
        sys.exit("[edits] anchor not found in %s:\n  %r" % (rel, old[:120]))
    if n != expect:
        sys.exit("[edits] anchor count %d != %d in %s:\n  %r"
                 % (n, expect, rel, old[:120]))
    _wr(rel, t.replace(old, new))
    applied += 1


def cut_block(rel, start_marker, replacement):
    """Delete the balanced-brace block that begins at the first '{' at or
    after `start_marker` (the marker text itself is deleted too)."""
    global applied
    t = _rd(rel)
    i = t.find(start_marker)
    if i < 0:
        if replacement in t:
            return                         # already applied
        sys.exit("[edits] cut_block marker not found in %s:\n  %r"
                 % (rel, start_marker[:120]))
    j = t.find("{", i)
    if j < 0:
        sys.exit("[edits] no '{' after marker in %s" % rel)
    depth = 0
    k = j
    while k < len(t):
        c = t[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        k += 1
    if depth != 0:
        sys.exit("[edits] unbalanced braces after marker in %s" % rel)
    end = k + 1
    while end < len(t) and t[end] in "\r\n":
        end += 1
        if t[end - 1] == "\n" and t[end:end + 1] == "\n":
            break
    _wr(rel, t[:i] + replacement + t[end:])
    applied += 1


def cut_span(rel, start_marker, end_marker, replacement):
    """Delete text from start_marker up to AND INCLUDING end_marker."""
    global applied
    t = _rd(rel)
    i = t.find(start_marker)
    if i < 0:
        if replacement in t:
            return                         # already applied
        sys.exit("[edits] cut_span start not found in %s" % rel)
    j = t.find(end_marker, i)
    if j < 0:
        sys.exit("[edits] cut_span end not found in %s" % rel)
    end = j + len(end_marker)
    _wr(rel, t[:i] + replacement + t[end:])
    applied += 1


# ---------------------------------------------------------------------------
# E1: l1_ext.h - strip the OTA/update API surface. The four hook families
# OCSE uses (fb access, renderer, font engine, console hook) stay; the
# update-check + A/B-slot API goes (no net stack, no slots in OCSE).
# ---------------------------------------------------------------------------
E1 = "ocse/l1/l1_ext.h"

edit(E1,
     '#include "ota_ab.h"   /* WP-10u: ota_update_status_t + pkg info */\n',
     '/* WP06a-E1: ota_ab.h not vendored - the update/A-B API below is\n'
     ' * stripped; OCSE keeps hooks 1-4 (fb / renderer / font / console). */\n')

cut_span(E1,
         "/* ------------------------------------------------------------------ *\n"
         " * 6. UPDATE CHECK (WP-09-fix5)",
         "int  l1_ext_update_get_status(ota_update_status_t *out);\n",
         "/* WP06a-E1: sections 6 (update check) + 7 (A/B update) removed -\n"
         " * OCSE has no network stack and no slot layout. */\n")

# ---------------------------------------------------------------------------
# E2: GRUB wrapper stripped - driver_block_disk_setup.c writes grub
# boot.img/core.img to disks (GRUB-era install tooling). GRUB left the
# release chain in B5-7; OCSE never writes GRUB, so the unit leaves the
# OCSE build entirely (patch into ocse/ocse.mk).
# ---------------------------------------------------------------------------
edit("ocse/ocse.mk",
     "                 $(wildcard $(OCSE_DIR)/drivers/usb/*.c)\n",
     "                 $(wildcard $(OCSE_DIR)/drivers/usb/*.c)\n"
     "# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling\n"
     "# (writes grub boot.img/core.img; pulls grub_boot_data.h + shell.h).\n"
     "OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))\n")

# ---------------------------------------------------------------------------
# E3: net stack not vendored - core_health samples net counters; they stay
# zero (the fields keep their meaning: "no network in this environment").
# ---------------------------------------------------------------------------
E3 = "ocse/kernel/core/core_health.c"

edit(E3,
     '#include "../net/net_core.h"\n',
     '/* WP06a-E3: net stack not vendored into OCSE; net counters stay 0. */\n')

edit(E3,
     "        net_stats_t ns;\n"
     "        net_get_stats(&ns);\n"
     "        s->net_tx_packets = ns.tx_packets;\n"
     "        s->net_rx_packets = ns.rx_packets;\n"
     "        s->net_tx_bytes = ns.tx_bytes;\n"
     "        s->net_rx_bytes = ns.rx_bytes;\n",
     "        /* WP06a-E3: no net stack - counters stay zero. */\n"
     "        s->net_tx_packets = 0;\n"
     "        s->net_rx_packets = 0;\n"
     "        s->net_tx_bytes = 0;\n"
     "        s->net_rx_bytes = 0;\n")

edit(E3,
     "    net_stats_t ns;\n"
     "    net_get_stats(&ns);\n"
     "    *net_tx_bytes = ns.tx_bytes;\n"
     "    *net_rx_bytes = ns.rx_bytes;\n",
     "    /* WP06a-E3: no net stack - byte counters stay zero. */\n"
     "    *net_tx_bytes = 0;\n"
     "    *net_rx_bytes = 0;\n")

# ---------------------------------------------------------------------------
# E4: ring-3 crash plumbing (core_crash) - keep the dump/panic machinery,
# stub the user-plane pointer. With no ring 3 every "user" field reports
# "none", exactly like a kernel-only crash.
# ---------------------------------------------------------------------------
E4 = "ocse/kernel/core/core_crash.c"

edit(E4,
     '#include "core_usermode.h"     /* user_process_current               */\n',
     '/* WP06a-E4: ring-3 user plane is not vendored into OCSE. A minimal\n'
     ' * local type + a stubbed user_process_current() keep the crash-report\n'
     ' * plumbing compiling; the stub returns 0 so every user field reads\n'
     ' * as "none" (kernel-only crash). */\n'
     'typedef struct { int pid; int tid; int alive; } user_proc_t;\n'
     'static user_proc_t *user_process_current(void) { return 0; }\n')

# ---------------------------------------------------------------------------
# E5: ring-3 kill path (arch_exceptions) - with no user plane, (cs & 3)
# can never be 3 in OCSE: the whole user-mode exception branch and the
# IST2 kill continuation are unreachable and removed.
# ---------------------------------------------------------------------------
E5 = "ocse/kernel/arch/x86_64/arch_exceptions.c"

edit(E5,
     '#include "core_usermode.h"\n',
     '/* WP06a-E5: no ring 3 in OCSE - the user-mode kill path below is\n'
     ' * removed; every fault is a kernel fault. A minimal local type\n'
     ' * keeps the parked arch_exc_kill_and_yield() signature compiling. */\n'
     'typedef struct { int _pad; } user_proc_t;\n')

edit(E5,
     '#include "config_gen.h"   /* WP-13 multi-config: OC_CONFIG_TUI */\n',
     '/* WP06a-E5: config_gen.h not vendored; the OC_CONFIG_TUI window below\n'
     ' * is compiled out (macro undefined = 0). */\n')

edit(E5,
     "/* BUG-0139 FIX (A1-6): reap-and-yield continuation for kills taken on\n"
     " * IST2-backed exceptions (#DB=1, NMI=2, #BP=3). Runs on the dedicated exit\n"
     " * stack (arch_exc_exit.S), NEVER on IST2, so the scheduler records an\n"
     " * address on this private stack and the global IST2 region is abandoned\n"
     " * instead of carrying a suspended frame that the next IST2 exception would\n"
     " * overwrite. Never returns. */\n",
     "/* WP06a-E5: arch_exc_kill_and_yield() (the BUG-0139 IST2 exit\n"
     " * continuation) is parked - it reaped a RING-3 victim and is\n"
     " * unreachable without a user plane; the asm trampoline still\n"
     " * references the symbol, so the signature stays. */\n")

cut_block(E5,
          "void arch_exc_kill_and_yield(user_proc_t *proc, u64 vec) {",
          "void arch_exc_kill_and_yield(user_proc_t *proc, u64 vec) {\n"
          "    /* WP06a-E5: the ring-3 kill continuation is unreachable in\n"
          "     * OCSE (no user plane), but the IST2 exit trampoline in\n"
          "     * arch_exc_exit.S still references the symbol. Keep the\n"
          "     * signature and park. */\n"
          "    (void)proc; (void)vec;\n"
          "    for (;;) __asm__ volatile(\"hlt\");\n"
          "}\n")

cut_block(E5,
          "    if ((f->cs & 3) == 3) {",
          "    /* WP06a-E5: the ring-3 exception branch (kill the user process\n"
          "     * and continue) is gone - (f->cs & 3) is always 0 in OCSE, so\n"
          "     * every fault falls through to the kernel panic path below. */\n")

# ---------------------------------------------------------------------------
# E6: int 0x80 dropped - the gate stays ring-0 (a ring-3 attempt faults
# #GP) and the dispatch branch is gone (a stray 0x80 is counted+dropped
# with the other unregistered vectors).
# ---------------------------------------------------------------------------
E6 = "ocse/kernel/arch/x86_64/arch_idt.c"

edit(E6,
     "        /* Syscall entry (vector 128 = 0x80) uses DPL=3 so ring 3 can call it. */\n"
     "        if (i == 0x80) flags = 0xEE;  /* P|ring3|interrupt gate */\n",
     "        /* WP06a-E6: int 0x80 dropped - no ring 3 in OCSE, every gate\n"
     "         * stays ring-0 (0x8E); a ring-3 attempt faults with #GP like\n"
     "         * any unregistered vector. */\n")

edit(E6,
     "    } else if (v == 0x80) {\n"
     "        /* Syscall (int 0x80 from ring 3). */\n"
     "        arch_irq_counts[v]++;\n"
     "        extern void core_syscall_dispatch(u64 *regs);\n"
     "        /* Pass the register array from the frame. The frame starts with\n"
     "         * r15 at offset 0, so f itself is the register array. */\n"
     "        core_syscall_dispatch((u64*)f);\n"
     "    }\n",
     "    }\n"
     "    /* WP06a-E6: the v == 0x80 syscall branch is dropped (no ring 3);\n"
     "     * a stray vector joins the count-and-drop tail below. */\n")

# ---------------------------------------------------------------------------
# E7: USB MSC + serial class drivers are not vendored; the resident poll
# thread drops their pollers.
# ---------------------------------------------------------------------------
E7 = "ocse/drivers/usb/driver_usb.c"

edit(E7,
     '#include "driver_usb_msc.h"\n'
     '#include "driver_usb_serial.h"\n',
     '/* WP06a-E7: MSC/serial (and audio) class drivers are not vendored\n'
     ' * into OCSE - the installer reads disks via the ATA block layer,\n'
     ' * not USB storage; HID (keyboard + mouse) is what OCSE uses. */\n')

edit(E7,
     "        driver_usb_serial_poll();\n"
     "        if ((round & 0x0f) == 0) driver_usb_msc_poll();\n",
     "        /* WP06a-E7: no USB serial / MSC pollers in OCSE. */\n")

# ---------------------------------------------------------------------------
# E8: shell_lang coupling removed from lib_config (OCSE language is an
# in-memory setting of the environment, never persisted by the installer).
# ---------------------------------------------------------------------------
E8 = "ocse/kernel/lib/lib_config.c"

edit(E8,
     '#include "shell_lang.h"   /* fix3: config set shell.lang syncs the live state */\n',
     '/* WP06a-E8: shell_lang not vendored; the OCSE language state is\n'
     ' * in-memory only (ocse_i18n.c), nothing to sync on config writes. */\n')

edit(E8,
     "            if (strcmp(key, OC_CONFIG_KEY_SHELL_LANG) == 0)\n"
     "                shell_lang_set_str(rest + k);\n",
     "            /* WP06a-E8: no live shell language to sync in OCSE. */\n")

# ---------------------------------------------------------------------------
# E9: NIC/net resume hooks removed from the ACPI power path (S3 resume in
# OCSE would have no NIC to reprogram; the serial-input re-init stays).
# ---------------------------------------------------------------------------
E9 = "ocse/drivers/acpi/driver_acpi_power.c"

edit(E9,
     '#include "../nic/driver_nic.h"\n'
     '#include "../../net/net_core.h"\n',
     '/* WP06a-E9: NIC + net stack are not vendored into OCSE. */\n')

edit(E9,
     "        driver_nic_resume();\n"
     "        net_e1000_resume();\n",
     "        /* WP06a-E9: no NIC state to resume in OCSE. */\n")

# ---------------------------------------------------------------------------
# E10: base bug 1 (latent since v1.0.0) - aml_store_value() kept an
# uninitialized `acpi_value_t copy` on the success path's tail: fields the
# copy helper did not write still held stack garbage that later
# aml_val_clear()/store paths treated as live pointers.
# ---------------------------------------------------------------------------
edit("ocse/drivers/acpi/driver_acpi_aml.c",
     "    acpi_value_t copy;\n"
     "    if (aml_val_copy(&copy, v) != 0) return;\n",
     "    acpi_value_t copy;\n"
     "    memset(&copy, 0, sizeof(copy));   /* WP06a-E10 (base bug 1): the\n"
     "     * union tail behind .kind/.integer carried stack garbage into the\n"
     "     * later clear()/store paths; zero-init first, always. */\n"
     "    if (aml_val_copy(&copy, v) != 0) return;\n")

# ---------------------------------------------------------------------------
# E11: base bug 2 (latent since v1.0.0) - aml_invoke_method() early-return
# paths (bad node / recursion limit) never wrote *ret_out, so the caller
# read an uninitialized value on failure.
# ---------------------------------------------------------------------------
edit("ocse/drivers/acpi/driver_acpi_aml.c",
     "    if (!m || m->kind != AO_METHOD || !m->aml) return -1;\n"
     "    if (depth >= AML_MAX_DEPTH) {\n"
     "        aml_note(\"method recursion limit\");\n"
     "        return -1;\n"
     "    }\n",
     "    if (ret_out) {\n"
     "        ret_out->kind = AO_INTEGER;   /* WP06a-E11 (base bug 2): the\n"
     "         * early returns below used to leave *ret_out unwritten (the\n"
     "         * caller read garbage on failure); a failed call now reads\n"
     "         * as a plain integer 0. The success path overwrites it. */\n"
     "        ret_out->integer = 0;\n"
     "    }\n"
     "    if (!m || m->kind != AO_METHOD || !m->aml) return -1;\n"
     "    if (depth >= AML_MAX_DEPTH) {\n"
     "        aml_note(\"method recursion limit\");\n"
     "        return -1;\n"
     "    }\n")

# ---------------------------------------------------------------------------
# E12: kernel/main.c rewritten into the 24-stage OCSE boot ladder
# (vendored kmain is the full v1.0.0 system bring-up - shell command
# registration, net init, TUI, OTA - none of which exists in OCSE).
# Template: tools/ocse_kmain_c.
# ---------------------------------------------------------------------------
def e12_main():
    global applied
    template = (ROOT / "tools/ocse_kmain_c").read_text()
    target = "ocse/kernel/main.c"
    if _rd(target) == template:
        return                          # already applied
    _wr(target, template)
    applied += 1


e12_main()

print("[ocse_edits] %d edit groups applied - OCSE tree is build-ready" % applied)
