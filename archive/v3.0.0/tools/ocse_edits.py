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
    # WP06b: generic idempotency pre-check. Appending edits (new contains
    # old, e.g. E2/E11) keep the anchor present, so the n==0 guard below
    # never fires on a re-run and the block got duplicated. If the final
    # text is already in the file the edit is done - skip unconditionally.
    if new and new in t:
        return
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
    if replacement and replacement in t:
        return                         # already applied
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
    if replacement and replacement in t:
        return                         # already applied
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

# ---------------------------------------------------------------------------
# E13: ext4 write path (WP-06b) - the on-disk structures and the
# ctx/inode typedefs move VERBATIM from fs_ext4.c to fs_ext4.h so
# ocse_ext4w.c (the new write-path module: write/mkdir/rmdir/create/
# unlink/rename + block+inode allocation) can share them, and
# fs_ext4_init() attaches the write ops onto the same op structures
# the VFS registry points at (registry stores POINTERS, so one patch
# covers both the mount root and every looked-up node).
# ---------------------------------------------------------------------------
def e13_ext4w():
    global applied
    c_rel = "ocse/fs/fs_ext4.c"
    h_rel = "ocse/fs/fs_ext4.h"
    c = _rd(c_rel)
    h = _rd(h_rel)
    if "ocse_ext4w_attach" in c and "fs_ext4_sb_t;" in h:
        return                          # already applied
    # 1. extract the packed on-disk struct block. The anchor CANNOT be
    # the bare "#pragma pack(push, 1)" string: the fs_ext4.c file-header
    # COMMENT contains that exact text ("...use #pragma pack(push, 1).
    # All functions are static..."), and .index() on the bare string
    # matched the comment, swallowing the comment tail + the whole
    # include block into the move (live-caught batch 3). Anchor on the
    # pragma PLUS the first struct's first member instead.
    pragma_real = ("#pragma pack(push, 1)\ntypedef struct {\n"
                   "    u32 s_inodes_count;")
    a0 = c.index(pragma_real)
    a1 = c.index("#pragma pack(pop)", a0) + len("#pragma pack(pop)")
    struct_block = c[a0:a1]
    # 2. extract the ctx + per-node typedef block
    b0 = c.index("/* FS-wide state.")
    b1 = c.index("} fs_ext4_inode_t;") + len("} fs_ext4_inode_t;")
    ctx_block = c[b0:b1]
    # 3. cut both from the .c
    c2 = c.replace(struct_block,
        "/* WP06b-E13: on-disk structures moved to fs_ext4.h "
        "(shared with ocse_ext4w.c). */")
    c2 = c2.replace(ctx_block,
        "/* WP06b-E13: ctx/inode typedefs moved to fs_ext4.h. */")
    # 4. attach the write ops after registration
    anchor = ('fs_vfs_register_fs("ext4", &g_ext4_fs_ops, '
              '&g_ext4_file_ops,\n                    &g_ext4_dir_ops);')
    if anchor not in c2:
        sys.exit("[edits] E13 register anchor not found")
    c2 = c2.replace(anchor,
        anchor + "\n    ocse_ext4w_attach(&g_ext4_file_ops, &g_ext4_dir_ops);")
    _wr(c_rel, c2)
    # 5. append the shared section inside the header guard
    guard = "#endif /* OC_EXT4_H */"
    if guard not in h:
        sys.exit("[edits] E13 header guard not found")
    shared = ("\n/* =========================================================\n"
              " * WP-06b-E13: shared ext4 structures (moved verbatim from\n"
              " * fs_ext4.c so ocse_ext4w.c - the write path - sees the same\n"
              " * layouts; the read driver keeps its kmalloc'd copies).\n"
              " * ========================================================= */\n"
              "#include \"fs_vfs.h\"\n"
              + struct_block + "\n\n" + ctx_block + "\n\n"
              "/* WP-06b: the ext4 write path (ocse_ext4w.c). */\n"
              "void ocse_ext4w_attach(fs_vfs_file_ops_t *file_ops,\n"
              "                      fs_vfs_dir_ops_t *dir_ops);\n\n")
    _wr(h_rel, h.replace(guard, shared + guard))
    applied += 1


e13_ext4w()

# ---------------------------------------------------------------------------
# E14: write-back cache coherence for the raw I/O paths (WP-06b,
# live-caught in the batch-3 battery). The write-back cache had NO
# invalidation hook: a raw WRITE (every mkfs.* path) updated the device
# while any cache line for the same sector survived. A later CACHED
# read (filesystem signature probing, fs metadata reads) then served
# the PRE-format bytes - reproducible: mkfs.fat32 + mount + umount +
# mkfs.exfat, then a cached sniff reported "fat32" and the exFAT mount
# was never even attempted ("fat32: invalid BPB"), and a late eviction
# could have written the stale sector back OVER the fresh raw write.
# Fix: raw WRITE drops the line for every sector it wrote (a dirty
# line's data is OLDER than the raw write that triggered the drop -
# discarding it is correct); raw READ copies any dirty line over the
# freshly read sector (write-back semantics: the line is newer than
# the device until flushed).
# ---------------------------------------------------------------------------
def e14_cache_cohere():
    global applied
    changed = False
    # 1. blk.h: declare the no-coherence write used by the cache's own
    # write-back paths (it holds g_cache_lock - the plain raw write
    # would re-enter the cache lock through the E14 drop hook and
    # deadlock; live-caught batch 3).
    bh_rel = "ocse/drivers/block/driver_block_blk.h"
    bh = _rd(bh_rel)
    if "raw_noco" not in bh:
        anchor = ("int driver_block_write_sectors_raw(int dev_idx, u64 lba, "
                  "u32 count, const void *buf);")
        if anchor not in bh:
            sys.exit("[edits] E14 blk.h anchor not found")
        decl = ("\n/* WP06b-E14: device write WITHOUT any cache interaction.\n"
                " * For the write-back cache's own eviction/flush paths, which\n"
                " * run holding g_cache_lock (the public raw write would take\n"
                " * it again through the drop hook). The caller drops the line\n"
                " * itself after a successful write-back. */\n"
                "int driver_block_write_sectors_raw_noco(int dev_idx, u64 lba,\n"
                "                                        u32 count,\n"
                "                                        const void *buf);")
        _wr(bh_rel, bh.replace(anchor, anchor + decl))
        changed = True
    # 2. cache.h: declare the two public coherence helpers
    h_rel = "ocse/drivers/block/driver_block_cache.h"
    h = _rd(h_rel)
    if "driver_block_cache_drop" not in h:
        anchor = "/* Flush dirty blocks for a specific device. */\nint driver_block_cache_flush_dev(int dev_idx);"
        if anchor not in h:
            sys.exit("[edits] E14 cache.h anchor not found")
        decl = ("\n/* WP06b-E14: raw-path write-back coherence (implemented in\n"
                " * driver_block_cache.c; used by the raw I/O dispatch in\n"
                " * driver_block_blk.c). */\n"
                "void driver_block_cache_drop(int dev_idx, u64 lba);\n"
                "void driver_block_cache_dirty_fill(int dev_idx, u64 lba,\n"
                "                                   void *buf);")
        _wr(h_rel, h.replace(anchor, anchor + decl))
        changed = True
    # 3. cache.c: nolock drop + public helpers + write-back swap
    c_rel = "ocse/drivers/block/driver_block_cache.c"
    c = _rd(c_rel)
    if "cache_drop_nolock" not in c:
        # 3a. the nolock helper + the public pair, in front of get_stats
        anchor = ("/* BUG-0116 FIX: pick a victim slot. The old code picked the LRU slot,\n"
                    " * fired one write-back for a dirty page and IGNORED the return value -")
        if anchor not in c:
            sys.exit("[edits] E14 cache.c get_stats anchor not found")
        impl = (
            "/* WP06b-E14: write-back cache coherence for the raw I/O paths.\n"
            " * cache_drop_nolock: the device was just written through the\n"
            " * raw path - any line for this sector is stale by definition\n"
            " * and must go (a dirty line's data is OLDER than the raw write\n"
            " * that triggered the drop; discarding it is correct). Callers\n"
            " * hold g_cache_lock; the public wrapper below takes it. */\n"
            "static void cache_drop_nolock(int dev_idx, u64 lba) {\n"
            "    int s = find_slot(dev_idx, lba);\n"
            "    if (s >= 0) {\n"
            "        g_cache[s].dev_idx = -1;\n"
            "        g_cache[s].dirty = 0;\n"
            "        g_cache_stats.evicts++;\n"
            "    }\n"
            "}\n"
            "\n"
            "void driver_block_cache_drop(int dev_idx, u64 lba) {\n"
            "    if (!g_cache) return;\n"
            "    spin_lock(&g_cache_lock);\n"
            "    cache_drop_nolock(dev_idx, lba);\n"
            "    spin_unlock(&g_cache_lock);\n"
            "}\n"
            "\n"
            "/* A raw READ must not serve pre-dirty device content: copy the\n"
            " * newer dirty line over the sector just read (the line stays\n"
            " * dirty; flush still owns the write-back). Never called while\n"
            " * g_cache_lock is held (only from the raw READ dispatch). */\n"
            "void driver_block_cache_dirty_fill(int dev_idx, u64 lba, void *buf) {\n"
            "    if (!g_cache) return;\n"
            "    spin_lock(&g_cache_lock);\n"
            "    int s = find_slot(dev_idx, lba);\n"
            "    if (s >= 0 && g_cache[s].dirty)\n"
            "        memcpy(buf, g_cache[s].data, 512);\n"
            "    spin_unlock(&g_cache_lock);\n"
            "}\n"
            "\n")
        c = c.replace(anchor, impl + anchor)
        # 3b. pick_victim write-back: noco write + nolock drop
        old_pv = ("        if (!g_cache[vi].dirty) return vi;\n"
                  "        if (driver_block_write_sectors_raw(g_cache[vi].dev_idx, g_cache[vi].lba, 1,\n"
                  "                                           g_cache[vi].data) == 0) {\n"
                  "            g_cache[vi].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "            return vi;\n"
                  "        }")
        new_pv = ("        if (!g_cache[vi].dirty) return vi;\n"
                  "        /* WP06b-E14: the write-back runs under g_cache_lock -\n"
                  "         * use the no-coherence write and drop the line here\n"
                  "         * (the plain raw write would re-enter the lock through\n"
                  "         * the drop hook and deadlock the eviction). */\n"
                  "        if (driver_block_write_sectors_raw_noco(g_cache[vi].dev_idx,\n"
                  "                                                g_cache[vi].lba, 1,\n"
                  "                                                g_cache[vi].data) == 0) {\n"
                  "            cache_drop_nolock(g_cache[vi].dev_idx, g_cache[vi].lba);\n"
                  "            g_cache[vi].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "            return vi;\n"
                  "        }")
        if old_pv not in c:
            sys.exit("[edits] E14 cache.c pick_victim anchor not found")
        c = c.replace(old_pv, new_pv)
        # 3c. both flush loops: same swap
        old_fl = ("        if (g_cache[i].dev_idx == dev_idx && g_cache[i].dirty) {\n"
                  "            if (driver_block_write_sectors_raw(dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0) {\n"
                  "                spin_unlock(&g_cache_lock);\n"
                  "                return -1;\n"
                  "            }\n"
                  "            g_cache[i].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "        }")
        new_fl = ("        if (g_cache[i].dev_idx == dev_idx && g_cache[i].dirty) {\n"
                  "            /* WP06b-E14: flush holds g_cache_lock - noco write\n"
                  "             * + local drop (see pick_victim above). */\n"
                  "            if (driver_block_write_sectors_raw_noco(dev_idx,\n"
                  "                                                    g_cache[i].lba, 1,\n"
                  "                                                    g_cache[i].data) != 0) {\n"
                  "                spin_unlock(&g_cache_lock);\n"
                  "                return -1;\n"
                  "            }\n"
                  "            cache_drop_nolock(dev_idx, g_cache[i].lba);\n"
                  "            g_cache[i].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "        }")
        if old_fl not in c:
            sys.exit("[edits] E14 cache.c flush_dev anchor not found")
        c = c.replace(old_fl, new_fl)
        old_f2 = ("        if (g_cache[i].dev_idx != -1 && g_cache[i].dirty) {\n"
                  "            if (driver_block_write_sectors_raw(g_cache[i].dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0) {\n"
                  "                spin_unlock(&g_cache_lock);\n"
                  "                return -1;\n"
                  "            }\n"
                  "            g_cache[i].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "        }")
        new_f2 = ("        if (g_cache[i].dev_idx != -1 && g_cache[i].dirty) {\n"
                  "            /* WP06b-E14: flush holds g_cache_lock - noco write\n"
                  "             * + local drop (see pick_victim above). */\n"
                  "            if (driver_block_write_sectors_raw_noco(g_cache[i].dev_idx,\n"
                  "                                                    g_cache[i].lba, 1,\n"
                  "                                                    g_cache[i].data) != 0) {\n"
                  "                spin_unlock(&g_cache_lock);\n"
                  "                return -1;\n"
                  "            }\n"
                  "            cache_drop_nolock(g_cache[i].dev_idx, g_cache[i].lba);\n"
                  "            g_cache[i].dirty = 0;\n"
                  "            g_cache_stats.writebacks++;\n"
                  "        }")
        if old_f2 not in c:
            sys.exit("[edits] E14 cache.c flush anchor not found")
        c = c.replace(old_f2, new_f2)
        _wr(c_rel, c)
        changed = True
    # 4. blk.c: wire the raw dispatch + add the noco write
    b_rel = "ocse/drivers/block/driver_block_blk.c"
    b = _rd(b_rel)
    if "cache_dirty_fill" not in b:
        old_rd = (
            "int driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf) {\n"
            "    driver_block_device_t *d = driver_block_get_device(dev_idx);\n"
            "    if (!d || !d->ops || !d->ops->read) return -1;\n"
            "    int rc = d->ops->read(d, lba, count, buf);\n"
            "    if (rc == 0) {\n"
            "        g_blk_stats[dev_idx].reads++;\n"
            "        g_blk_stats[dev_idx].read_sectors += count;\n"
            "    }\n"
            "    return rc;\n"
            "}")
        new_rd = (
            "int driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf) {\n"
            "    driver_block_device_t *d = driver_block_get_device(dev_idx);\n"
            "    if (!d || !d->ops || !d->ops->read) return -1;\n"
            "    int rc = d->ops->read(d, lba, count, buf);\n"
            "    if (rc == 0) {\n"
            "        /* WP06b-E14: a DIRTY write-back line for this sector is\n"
            "         * newer than anything the device can return - fill it\n"
            "         * over the raw data (write-back semantics). The read path\n"
            "         * never runs under g_cache_lock, so the helper may lock. */\n"
            "        u8 *out = (u8 *)buf;\n"
            "        for (u32 i = 0; i < count; i++)\n"
            "            driver_block_cache_dirty_fill(dev_idx, lba + i,\n"
            "                                          out + (u64)i * BLK_SECTOR_SIZE);\n"
            "        g_blk_stats[dev_idx].reads++;\n"
            "        g_blk_stats[dev_idx].read_sectors += count;\n"
            "    }\n"
            "    return rc;\n"
            "}")
        if old_rd not in b:
            sys.exit("[edits] E14 blk.c read anchor not found")
        b = b.replace(old_rd, new_rd)
        old_wr = (
            "int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf) {\n"
            "    driver_block_device_t *d = driver_block_get_device(dev_idx);\n"
            "    if (!d || !d->ops || !d->ops->write) return -1;\n"
            "    int rc = d->ops->write(d, lba, count, buf);\n"
            "    if (rc == 0) {\n"
            "        g_blk_stats[dev_idx].writes++;\n"
            "        g_blk_stats[dev_idx].write_sectors += count;\n"
            "    }\n"
            "    return rc;\n"
            "}")
        new_wr = (
            "int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf) {\n"
            "    driver_block_device_t *d = driver_block_get_device(dev_idx);\n"
            "    if (!d || !d->ops || !d->ops->write) return -1;\n"
            "    int rc = d->ops->write(d, lba, count, buf);\n"
            "    if (rc == 0) {\n"
            "        /* WP06b-E14: the device now holds NEWER data than any\n"
            "         * cache line for these sectors (the raw path bypasses\n"
            "         * the write-back cache). Drop the lines - keeping them\n"
            "         * would serve stale bytes to later cached reads and a\n"
            "         * late eviction would write the stale data back OVER\n"
            "         * the fresh raw write. */\n"
            "        for (u32 i = 0; i < count; i++)\n"
            "            driver_block_cache_drop(dev_idx, lba + i);\n"
            "        g_blk_stats[dev_idx].writes++;\n"
            "        g_blk_stats[dev_idx].write_sectors += count;\n"
            "    }\n"
            "    return rc;\n"
            "}\n"
            "\n"
            "/* WP06b-E14: device write only - NO cache interaction. The\n"
            " * write-back cache's eviction/flush paths call this while\n"
            " * HOLDING g_cache_lock and drop the line themselves (a plain\n"
            " * raw write would re-enter the lock and deadlock). */\n"
            "int driver_block_write_sectors_raw_noco(int dev_idx, u64 lba, u32 count, const void *buf) {\n"
            "    driver_block_device_t *d = driver_block_get_device(dev_idx);\n"
            "    if (!d || !d->ops || !d->ops->write) return -1;\n"
            "    int rc = d->ops->write(d, lba, count, buf);\n"
            "    if (rc == 0) {\n"
            "        g_blk_stats[dev_idx].writes++;\n"
            "        g_blk_stats[dev_idx].write_sectors += count;\n"
            "    }\n"
            "    return rc;\n"
            "}")
        if old_wr not in b:
            sys.exit("[edits] E14 blk.c write anchor not found")
        b = b.replace(old_wr, new_wr)
        _wr(b_rel, b)
        changed = True
    if changed:
        applied += 1


e14_cache_cohere()

# ---------------------------------------------------------------------------
# E15: BMDMA completion detection rewritten (WP-06b, live-caught in the
# batch-3 battery). Per the PCI IDE controller spec - and QEMU's
# implementation - BMIS bit0 is "Bus Master ACTIVE" (1 WHILE the
# transfer runs, cleared on completion) and bit2 is the IDE interrupt.
# The vendored poll broke on Active==1 - the IN-FLIGHT state - and read
# success from it; when the transfer finished before the FIRST poll
# (the normal case for a 1-sector transfer: QEMU completes the DMA
# inside the START write) the loop saw Active=0 forever and burned its
# whole 50M-iteration budget, 8 attempts per sector - every short DMA
# hung the shell for minutes (mkfs.fat32 = ~1000 single-sector writes
# = effectively forever; the "atadma: xfer FAIL rc=1 bmis=4" evidence:
# bmis=4 = IDE INT set = transfer COMPLETE while the poll wanted
# Active). Success is now: no error AND no longer active, confirmed by
# the IDE interrupt or an idle device; the fix3 dropped-command
# signature (idle device, no BM status) is preserved as the retry path.
# ---------------------------------------------------------------------------
def e15_dma_completion():
    global applied
    f_rel = "ocse/drivers/block/driver_block_ata_dma.c"
    f = _rd(f_rel)
    if "WP06b-E15" in f:
        return
    old_poll = (
        "        rc = -1;\n"
        "        s_last = 0;\n"
        "        for (u64 i = 0; i < 50000000u; i++) {\n"
        "            s_last = inb(bmis);\n"
        "            if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }\n"
        "            if (s_last & BMIS_DMA_INT)   { rc = 0;  break; }\n"
        "            if ((i & 0xFFFu) == 0xFFFu && s_last == 0) {\n"
        "                u8 stq = inb(g_chan_base[drive] + ATA_REG_STATUS);\n"
        "                if (!(stq & ATA_SR_BSY) && !(stq & ATA_SR_DRQ)) {\n"
        "                    /* re-read once: a completing DMA may race the idle check */\n"
        "                    s_last = inb(bmis);\n"
        "                    if (s_last & BMIS_DMA_INT) { rc = 0; break; }\n"
        "                    if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }\n"
        "                    rc = -5;            /* dropped: retry the sequence */\n"
        "                    break;\n"
        "                }\n"
        "            }\n"
        "        }")
    new_poll = (
        "        rc = -1;\n"
        "        s_last = 0;\n"
        "        for (u64 i = 0; i < 50000000u; i++) {\n"
        "            s_last = inb(bmis);\n"
        "            if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }\n"
        "            /* WP06b-E15: BMIS bit0 = ACTIVE (in flight), bit2 =\n"
        "             * IDE interrupt. Done = Active CLEARED (with the IDE\n"
        "             * interrupt or an idle device confirming); the old\n"
        "             * poll broke on Active SET - the in-flight window -\n"
        "             * and spun forever on transfers that completed before\n"
        "             * the first poll (every 1-sector DMA). */\n"
        "            if (!(s_last & BMIS_DMA_INT)) {\n"
        "                if (s_last & BMIS_IDE_INT) { rc = 0; break; }\n"
        "                u8 stq = inb(g_chan_base[drive] + ATA_REG_STATUS);\n"
        "                if (!(stq & ATA_SR_BSY) && !(stq & ATA_SR_DRQ)) {\n"
        "                    /* re-read once: a completing DMA may race\n"
        "                     * the idle check */\n"
        "                    s_last = inb(bmis);\n"
        "                    if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }\n"
        "                    if (s_last & BMIS_IDE_INT) { rc = 0; break; }\n"
        "                    if (!(s_last & BMIS_DMA_INT)) {\n"
        "                        rc = -5;        /* dropped: retry the sequence */\n"
        "                        break;\n"
        "                    }\n"
        "                    /* Active again (race): keep polling */\n"
        "                }\n"
        "                /* device busy, BM idle: launch latency - poll on */\n"
        "            }\n"
        "        }")
    if old_poll not in f:
        sys.exit("[edits] E15 poll anchor not found")
    f = f.replace(old_poll, new_poll)
    _wr(f_rel, f)
    applied += 1


def e16_rdonly_masks():
    """WP06b b4: the read-only opens of fs_ext4.c rejected EVERY open.
    VFS_O_RDWR == VFS_O_RDONLY|VFS_O_WRONLY (0x3), so a mask that ORs
    RDWR in also matches a pure RDONLY open (bit 0). The WRONLY bit
    alone is the write-intent indicator for both write modes. The
    iso9660 twin of this bug (ocse_iso.c, our own file) broke every
    /install file open - live-caught by the b4 tar battery; the ext4
    copy is dead code while ocse_ext4w_attach() overrides .open but
    is fixed here for hygiene (same class, same fix)."""
    edit("ocse/fs/fs_ext4.c",
         "    /* ext4 is read-only; reject write/create flags. */\n"
         "    if (flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT)) return -3;",
         "    /* ext4 is read-only; reject write/create flags. WP06b-E16:\n"
         "     * VFS_O_RDWR == RDONLY|WRONLY, so the old mask (which ORed\n"
         "     * RDWR in) matched a pure O_RDONLY open too and refused\n"
         "     * EVERY read; the WRONLY bit alone covers both write modes. */\n"
         "    if (flags & (VFS_O_WRONLY | VFS_O_CREAT)) return -3;")


def e17_flush_guard():
    """WP06b b4: flush write-back vs partition-child reentrancy.
    A dirty line's write-back runs under g_cache_lock via the noco
    write - but a PARTITION child device's ops->write remaps to the
    parent through the PUBLIC raw API (d->ops->write -> the partition
    mapper -> driver_block_write_sectors_raw on the parent), whose
    E14 hooks (drop / dirty_fill) take g_cache_lock AGAIN: single-core
    spin deadlock. Live-caught: `sync` after dd'ing to hdap2 spun
    forever (RIP=spin_lock, stack flush_dev -> noco -> child write ->
    raw write -> cache_drop). While the guard is up the hooks run
    lockless-inline - the exact semantics the E14 eviction path
    already uses via cache_drop_nolock."""
    c = "ocse/drivers/block/driver_block_cache.c"
    # 1. the guard variable
    edit(c,
         "static spinlock_t g_cache_lock;",
         "static spinlock_t g_cache_lock;\n"
         "/* WP06b-E17: flush-path reentrancy guard - see the E17 note at\n"
         " * driver_block_cache_drop below. */\n"
         "static volatile int g_cache_flush_guard = 0;")
    # 2. cache_drop: guarded entry
    edit(c,
         "void driver_block_cache_drop(int dev_idx, u64 lba) {\n"
         "    if (!g_cache) return;\n"
         "    spin_lock(&g_cache_lock);\n"
         "    cache_drop_nolock(dev_idx, lba);\n"
         "    spin_unlock(&g_cache_lock);\n"
         "}",
         "void driver_block_cache_drop(int dev_idx, u64 lba) {\n"
         "    if (!g_cache) return;\n"
         "    /* WP06b-E17: a flush write-back through a partition child\n"
         "     * re-enters here via the parent raw-write hook while the\n"
         "     * caller already holds g_cache_lock - run inline instead. */\n"
         "    if (g_cache_flush_guard) { cache_drop_nolock(dev_idx, lba); return; }\n"
         "    spin_lock(&g_cache_lock);\n"
         "    cache_drop_nolock(dev_idx, lba);\n"
         "    spin_unlock(&g_cache_lock);\n"
         "}")
    # 3. dirty_fill: guarded entry
    edit(c,
         "void driver_block_cache_dirty_fill(int dev_idx, u64 lba, void *buf) {\n"
         "    if (!g_cache) return;\n"
         "    spin_lock(&g_cache_lock);\n"
         "    int s = find_slot(dev_idx, lba);\n"
         "    if (s >= 0 && g_cache[s].dirty)\n"
         "        memcpy(buf, g_cache[s].data, 512);\n"
         "    spin_unlock(&g_cache_lock);\n"
         "}",
         "void driver_block_cache_dirty_fill(int dev_idx, u64 lba, void *buf) {\n"
         "    if (!g_cache) return;\n"
         "    /* WP06b-E17: same reentrancy class as cache_drop (the raw\n"
         "     * READ dispatch can be reached from a flush write-back\n"
         "     * through a partition child). Inline, lockless. */\n"
         "    if (g_cache_flush_guard) {\n"
         "        int s = find_slot(dev_idx, lba);\n"
         "        if (s >= 0 && g_cache[s].dirty)\n"
         "            memcpy(buf, g_cache[s].data, 512);\n"
         "        return;\n"
         "    }\n"
         "    spin_lock(&g_cache_lock);\n"
         "    int s = find_slot(dev_idx, lba);\n"
         "    if (s >= 0 && g_cache[s].dirty)\n"
         "        memcpy(buf, g_cache[s].data, 512);\n"
         "    spin_unlock(&g_cache_lock);\n"
         "}")
    # 4. pick_victim write-back: raise the guard around the noco write
    edit(c,
         "        if (driver_block_write_sectors_raw_noco(g_cache[vi].dev_idx,\n"
         "                                                g_cache[vi].lba, 1,\n"
         "                                                g_cache[vi].data) == 0) {",
         "        g_cache_flush_guard = 1;      /* WP06b-E17 */\n"
         "        int wbrc = driver_block_write_sectors_raw_noco(g_cache[vi].dev_idx,\n"
         "                                                g_cache[vi].lba, 1,\n"
         "                                                g_cache[vi].data);\n"
         "        g_cache_flush_guard = 0;\n"
         "        if (wbrc == 0) {")
    # 5. flush_dev write-back
    edit(c,
         "            if (driver_block_write_sectors_raw_noco(dev_idx,\n"
         "                                                    g_cache[i].lba, 1,\n"
         "                                                    g_cache[i].data) != 0) {\n"
         "                spin_unlock(&g_cache_lock);\n"
         "                return -1;\n"
         "            }",
         "            g_cache_flush_guard = 1;  /* WP06b-E17 */\n"
         "            int wbrc = driver_block_write_sectors_raw_noco(dev_idx,\n"
         "                                                    g_cache[i].lba, 1,\n"
         "                                                    g_cache[i].data);\n"
         "            g_cache_flush_guard = 0;\n"
         "            if (wbrc != 0) {\n"
         "                spin_unlock(&g_cache_lock);\n"
         "                return -1;\n"
         "            }")
    # 6. flush-all write-back
    edit(c,
         "            if (driver_block_write_sectors_raw_noco(g_cache[i].dev_idx,\n"
         "                                                    g_cache[i].lba, 1,\n"
         "                                                    g_cache[i].data) != 0) {\n"
         "                spin_unlock(&g_cache_lock);\n"
         "                return -1;\n"
         "            }",
         "            g_cache_flush_guard = 1;  /* WP06b-E17 */\n"
         "            int wbrc = driver_block_write_sectors_raw_noco(g_cache[i].dev_idx,\n"
         "                                                    g_cache[i].lba, 1,\n"
         "                                                    g_cache[i].data);\n"
         "            g_cache_flush_guard = 0;\n"
         "            if (wbrc != 0) {\n"
         "                spin_unlock(&g_cache_lock);\n"
         "                return -1;\n"
         "            }")


e17_flush_guard()
e16_rdonly_masks()
e15_dma_completion()

# ---------------------------------------------------------------------------
# E18 (3.0.0-WP06f): the GUI double buffer in screen_fb (flicker fix).
# The GUI used to render straight onto the live framebuffer - every
# repaint blanked the whole screen with a background fill before
# redrawing each widget, so every state update (progress bar, pointer
# move) flashed.  screen_fb gains an optional RAM back buffer: while
# attached, ALL draw primitives render into it and screen_fb_present()
# publishes only the rows that differ (the local refresh).  Includes
# the standard arrow cursor sprite (the old pointer was three stacked
# fill_rects).
# ---------------------------------------------------------------------------
def e18_double_buffer():
    # 18a. header: the new API surface
    edit("ocse/drivers/display/screen_fb.h",
         "/* Convenience: clear entire framebuffer to a single pixel value. */\n"
         "void screen_fb_clear(u32 pixel);\n"
         "\n"
         "#endif /* OC_FB_H */",
         "/* Convenience: clear entire framebuffer to a single pixel value. */\n"
         "void screen_fb_clear(u32 pixel);\n"
         "\n"
         "/* ---- 3.0.0-WP06f: the double-buffer (back buffer) -------------------\n"
         " * The GUI used to render straight onto the live framebuffer, blanking\n"
         " * the whole screen with a background fill before repainting every\n"
         " * widget - every state update (progress bar, countdown, pointer move)\n"
         " * was a visible full-screen flash.  With a shadow attached, ALL draw\n"
         " * primitives (put_pixel / fill_rect / blit / clear / write_px / the\n"
         " * glyph blitters) render into the RAM back buffer instead; a finished\n"
         " * frame is published with screen_fb_present(), which copies only the\n"
         " * rows that differ (the local refresh: a pointer move touches ~20 of\n"
         " * 600 rows).  Each visible pixel is written exactly once per frame,\n"
         " * so no intermediate state is ever displayed - no flicker.\n"
         " *\n"
         " * Attach before entering the GUI panel/wizard, detach when the CLI\n"
         " * console takes the screen over (the text console draws cells in\n"
         " * place and needs direct framebuffer writes). */\n"
         "int  screen_fb_shadow_attach(void);   /* allocate + route draws to RAM */\n"
         "void screen_fb_shadow_detach(void);   /* route draws back to the FB    */\n"
         "int  screen_fb_shadow_active(void);\n"
         "void screen_fb_present(void);         /* publish changed rows to the FB */\n"
         "\n"
         "/* Unconditional pixel write used by the glyph blitters and the cursor\n"
         " * sprite (fg AND bg pixels - unlike put_pixel callers that layer on an\n"
         " * existing background).  Routes to the back buffer when attached. */\n"
         "void screen_fb_write_px(u32 x, u32 y, u32 pixel);\n"
         "\n"
         "/* 3.0.0-WP06f: the standard arrow pointer sprite (11x16 classic arrow,\n"
         " * white outline + black fill, transparent background - the transparent\n"
         " * pixels leave the back-buffer content untouched).  Clipped at the\n"
         " * screen edges.  Routes to the back buffer when attached. */\n"
         "void screen_fb_draw_cursor(u32 x, u32 y);\n"
         "\n"
         "#endif /* OC_FB_H */")

    # 18b. screen_fb.c: include + shadow state + routed primitives
    edit("ocse/drivers/display/screen_fb.c",
         '#include "screen_fb.h"\n'
         '#include "arch_multiboot2.h"\n'
         '#include "lib_string.h"\n'
         '\n'
         'static screen_fb_info_t g_fb;',
         '#include "screen_fb.h"\n'
         '#include "arch_multiboot2.h"\n'
         '#include "lib_string.h"\n'
         '#include "mem_heap.h"     /* WP06f: the double-buffer allocation */\n'
         '\n'
         'static screen_fb_info_t g_fb;\n'
         '\n'
         '/* ---- 3.0.0-WP06f: the RAM back buffer (double buffering) --------------\n'
         ' * While attached, every draw primitive renders into g_shadow (a\n'
         ' * width*4-pitch mirror of the screen) instead of the live framebuffer;\n'
         ' * screen_fb_present() publishes only the rows that differ.  A frame\n'
         ' * reaches the visible surface as one final write per pixel - the\n'
         ' * blank-then-repaint intermediate states that flickered are gone. */\n'
         'static u8* g_shadow;          /* kmalloc\'d W*H*4 back buffer (or NULL) */\n'
         'static int g_shadow_on;       /* 1 = draws route to the back buffer   */\n'
         '\n'
         'static u8* draw_base(void)\n'
         '{\n'
         '    return g_shadow_on && g_shadow ? g_shadow : g_fb.addr;\n'
         '}\n'
         '\n'
         'static u32 draw_pitch(void)\n'
         '{\n'
         '    /* the back buffer is always tightly packed */\n'
         '    return (g_shadow_on && g_shadow) ? g_fb.width * 4u : g_fb.pitch;\n'
         '}')

    edit("ocse/drivers/display/screen_fb.c",
         "void screen_fb_put_pixel(u32 x, u32 y, u32 pixel) {\n"
         "    if (x >= g_fb.width || y >= g_fb.height) return;\n"
         "    u8* row = g_fb.addr + (u64)y * g_fb.pitch;\n"
         "    u32* px = (u32*)(row + (u64)x * 4);\n"
         "    *px = pixel;\n"
         "}",
         "void screen_fb_put_pixel(u32 x, u32 y, u32 pixel) {\n"
         "    if (x >= g_fb.width || y >= g_fb.height) return;\n"
         "    u8* row = draw_base() + (u64)y * draw_pitch();\n"
         "    u32* px = (u32*)(row + (u64)x * 4);\n"
         "    *px = pixel;\n"
         "}\n"
         "\n"
         "void screen_fb_write_px(u32 x, u32 y, u32 pixel) {\n"
         "    /* WP06f: unconditional write for glyph blitters / the cursor sprite -\n"
         "     * identical routing to put_pixel (kept separate so the call sites\n"
         "     * document that fg AND bg pixels land, never a layered overlay). */\n"
         "    if (x >= g_fb.width || y >= g_fb.height) return;\n"
         "    u8* row = draw_base() + (u64)y * draw_pitch();\n"
         "    u32* px = (u32*)(row + (u64)x * 4);\n"
         "    *px = pixel;\n"
         "}")

    edit("ocse/drivers/display/screen_fb.c",
         "    for (u32 j = 0; j < h; ++j) {\n"
         "        u8*  row = g_fb.addr + (u64)(y + j) * g_fb.pitch;\n"
         "        u32* px  = (u32*)(row + (u64)x * 4);\n"
         "        for (u32 i = 0; i < w; ++i) px[i] = pixel;\n"
         "    }",
         "    u8* base = draw_base();\n"
         "    u32 pitch = draw_pitch();\n"
         "    for (u32 j = 0; j < h; ++j) {\n"
         "        u8*  row = base + (u64)(y + j) * pitch;\n"
         "        u32* px  = (u32*)(row + (u64)x * 4);\n"
         "        for (u32 i = 0; i < w; ++i) px[i] = pixel;\n"
         "    }")

    edit("ocse/drivers/display/screen_fb.c",
         "    for (u32 j = 0; j < h; ++j) {\n"
         "        u8*        dst_row = g_fb.addr + (u64)(y + j) * g_fb.pitch;\n"
         "        const u32* src_row = (const u32*)((const u8*)src + (u64)j * src_pitch_bytes);\n"
         "        u32*       dpx     = (u32*)(dst_row + (u64)x * 4);\n"
         "        for (u32 i = 0; i < w; ++i) dpx[i] = src_row[i];\n"
         "    }",
         "    u8* base = draw_base();\n"
         "    u32 pitch = draw_pitch();\n"
         "    for (u32 j = 0; j < h; ++j) {\n"
         "        u8*        dst_row = base + (u64)(y + j) * pitch;\n"
         "        const u32* src_row = (const u32*)((const u8*)src + (u64)j * src_pitch_bytes);\n"
         "        u32*       dpx     = (u32*)(dst_row + (u64)x * 4);\n"
         "        for (u32 i = 0; i < w; ++i) dpx[i] = src_row[i];\n"
         "    }")

    # 18c. screen_fb.c: the shadow API + present + the cursor sprite
    edit("ocse/drivers/display/screen_fb.c",
         "void screen_fb_clear(u32 pixel) {\n"
         "    screen_fb_fill_rect(0, 0, g_fb.width, g_fb.height, pixel);\n"
         "}",
         "void screen_fb_clear(u32 pixel) {\n"
         "    screen_fb_fill_rect(0, 0, g_fb.width, g_fb.height, pixel);\n"
         "}\n"
         "\n"
         "int screen_fb_shadow_attach(void) {\n"
         "    if (g_shadow) { g_shadow_on = 1; return 0; }\n"
         "    if (g_fb.width == 0 || g_fb.height == 0) return -1;\n"
         "    g_shadow = kmalloc((u64)g_fb.width * g_fb.height * 4u);\n"
         "    if (!g_shadow) return -1;\n"
         "    /* start as a faithful copy of what is on screen so a partial\n"
         "     * repaint + present never flashes stale content */\n"
         "    for (u32 y = 0; y < g_fb.height; ++y)\n"
         "        memcpy(g_shadow + (u64)y * g_fb.width * 4u,\n"
         "               g_fb.addr + (u64)y * g_fb.pitch,\n"
         "               (usize)g_fb.width * 4u);\n"
         "    g_shadow_on = 1;\n"
         "    return 0;\n"
         "}\n"
         "\n"
         "void screen_fb_shadow_detach(void) {\n"
         "    g_shadow_on = 0;\n"
         "}\n"
         "\n"
         "int screen_fb_shadow_active(void) {\n"
         "    return g_shadow_on && g_shadow ? 1 : 0;\n"
         "}\n"
         "\n"
         "void screen_fb_present(void) {\n"
         "    /* Publish the finished frame: copy ONLY the rows that differ\n"
         "     * (the local refresh - a pointer move rewrites ~20 of 600 rows,\n"
         "     * a progress tick ~2).  With no back buffer this is a no-op: the\n"
         "     * primitives already wrote the live surface directly. */\n"
         "    if (!g_shadow_on || !g_shadow) return;\n"
         "    u32 pitch = g_fb.width * 4u;\n"
         "    for (u32 y = 0; y < g_fb.height; ++y) {\n"
         "        u8* srow = g_shadow + (u64)y * pitch;\n"
         "        u8* frow = g_fb.addr + (u64)y * g_fb.pitch;\n"
         "        if (memcmp(srow, frow, pitch) != 0)\n"
         "            memcpy(frow, srow, pitch);\n"
         "    }\n"
         "}\n"
         "\n"
         "/* ---- 3.0.0-WP06f: the standard arrow pointer (13x16) -------------------\n"
         " * The classic arrow silhouette: a solid black triangular head with its\n"
         " * left edge vertical, the hypotenuse down to the tip at row 10, and a\n"
         " * 3px stem descending to row 15.  One-pixel white outline all around\n"
         " * (so the pointer stays visible on dark backgrounds); everything else\n"
         " * transparent - the underlying frame shows through untouched.\n"
         " *\n"
         " *   bit=1 in k_cursor_mask : this pixel belongs to the pointer\n"
         " *   bit=1 in k_cursor_white: outline pixel (white); 0 = fill (black)\n"
         " *\n"
         " * (The old pointer was three stacked fill_rects - a chunky block\n"
         " * \"arrow\" with no outline that read as a random black blob.) */\n"
         "#define CURSOR_W 13u\n"
         "#define CURSOR_H 16u\n"
         "static const u16 k_cursor_mask[CURSOR_H] = {\n"
         "    0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80, 0xFFC0,\n"
         "    0xFFE0, 0xFFF0, 0xFFF8, 0xFFF0, 0xF800, 0xF800, 0xF800, 0xF800,\n"
         "};\n"
         "static const u16 k_cursor_white[CURSOR_H] = {\n"
         "    0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080, 0x8040,\n"
         "    0x8020, 0x8010, 0x8008, 0x87F0, 0x8800, 0x8800, 0x8800, 0x8800,\n"
         "};\n"
         "\n"
         "void screen_fb_draw_cursor(u32 x, u32 y) {\n"
         "    u32 white = screen_fb_rgb(0xFF, 0xFF, 0xFF);\n"
         "    u32 black = screen_fb_rgb(0x10, 0x10, 0x10);\n"
         "    for (u32 r = 0; r < CURSOR_H; r++) {\n"
         "        u32 py = y + r;\n"
         "        if (py >= g_fb.height) break;\n"
         "        u16 m = k_cursor_mask[r];\n"
         "        u16 w = k_cursor_white[r];\n"
         "        for (u32 c = 0; c < CURSOR_W; c++) {\n"
         "            if (!(m & (0x8000u >> c))) continue;   /* transparent */\n"
         "            u32 px = x + c;\n"
         "            if (px >= g_fb.width) break;\n"
         "            screen_fb_write_px(px, py, (w & (0x8000u >> c)) ? white : black);\n"
         "        }\n"
         "    }\n"
         "}")


# ---------------------------------------------------------------------------
# E19 (3.0.0-WP06f): the glyph blitters route through screen_fb_write_px
# so console/CJK glyphs follow the active draw target (RAM back buffer
# while the GUI owns the screen).  Behavior with no shadow attached is
# identical to v1.0.0 (direct framebuffer writes).
# ---------------------------------------------------------------------------
def e19_glyph_routing():
    edit("ocse/drivers/display/screen_font.c",
         "void screen_renderer_default_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel) {\n"
         "    const u8* glyph = screen_font8x16[cp & 0xFF];\n"
         "    /* For each of 16 rows: for each of 8 columns, pick fg or bg pixel. */\n"
         "    for (u32 row = 0; row < OC_FONT_CELL_H; ++row) {\n"
         "        u8  bits   = glyph[row];\n"
         "        u32 ypix   = y + row;\n"
         "        u8* rowptr = screen_fb_get_info()->addr + (u64)ypix * screen_fb_get_info()->pitch;\n"
         "        for (u32 col = 0; col < OC_FONT_CELL_W; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= screen_fb_get_info()->width || ypix >= screen_fb_get_info()->height) continue;\n"
         "            u32* px = (u32*)(rowptr + (u64)xpix * 4);\n"
         "            *px = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;\n"
         "        }\n"
         "    }\n"
         "}",
         "void screen_renderer_default_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel) {\n"
         "    const u8* glyph = screen_font8x16[cp & 0xFF];\n"
         "    /* For each of 16 rows: for each of 8 columns, pick fg or bg pixel.\n"
         "     * WP06f: writes route through screen_fb_write_px so the glyph\n"
         "     * lands in the RAM back buffer while the GUI owns the screen\n"
         "     * (the double-buffer path - see screen_fb.h). */\n"
         "    for (u32 row = 0; row < OC_FONT_CELL_H; ++row) {\n"
         "        u8  bits   = glyph[row];\n"
         "        u32 ypix   = y + row;\n"
         "        for (u32 col = 0; col < OC_FONT_CELL_W; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= screen_fb_get_info()->width || ypix >= screen_fb_get_info()->height) continue;\n"
         "            screen_fb_write_px(xpix, ypix,\n"
         "                               (bits & (0x80 >> col)) ? fg_pixel : bg_pixel);\n"
         "        }\n"
         "    }\n"
         "}")

    edit("ocse/chinese/render_cn.c",
         "static void blit_8x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {\n"
         "    const screen_fb_info_t* fb = screen_fb_get_info();\n"
         "    for (u32 row = 0; row < 16; ++row) {\n"
         "        u8 bits = glyph[row];\n"
         "        u32 ypix = y + row;\n"
         "        if (ypix >= fb->height) break;\n"
         "        u8* rowptr = fb->addr + (u64)ypix * fb->pitch;\n"
         "        for (u32 col = 0; col < 8; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= fb->width) break;\n"
         "            *(u32*)(rowptr + (u64)xpix * 4) =\n"
         "                (bits & (0x80 >> col)) ? fg : bg;\n"
         "        }\n"
         "    }\n"
         "}",
         "static void blit_8x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {\n"
         "    /* WP06f: unconditional writes route through screen_fb_write_px so\n"
         "     * glyphs follow the active draw target (RAM back buffer while the\n"
         "     * GUI owns the screen, live framebuffer otherwise). */\n"
         "    const screen_fb_info_t* fb = screen_fb_get_info();\n"
         "    for (u32 row = 0; row < 16; ++row) {\n"
         "        u8 bits = glyph[row];\n"
         "        u32 ypix = y + row;\n"
         "        if (ypix >= fb->height) break;\n"
         "        for (u32 col = 0; col < 8; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= fb->width) break;\n"
         "            screen_fb_write_px(xpix, ypix,\n"
         "                               (bits & (0x80 >> col)) ? fg : bg);\n"
         "        }\n"
         "    }\n"
         "}")

    edit("ocse/chinese/render_cn.c",
         "static void blit_16x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {\n"
         "    const screen_fb_info_t* fb = screen_fb_get_info();\n"
         "    for (u32 row = 0; row < 16; ++row) {\n"
         "        u16 bits = (u16)((glyph[row * 2] << 8) | glyph[row * 2 + 1]);\n"
         "        u32 ypix = y + row;\n"
         "        if (ypix >= fb->height) break;\n"
         "        u8* rowptr = fb->addr + (u64)ypix * fb->pitch;\n"
         "        for (u32 col = 0; col < 16; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= fb->width) break;\n"
         "            *(u32*)(rowptr + (u64)xpix * 4) =\n"
         "                (bits & (0x8000 >> col)) ? fg : bg;\n"
         "        }\n"
         "    }\n"
         "}",
         "static void blit_16x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {\n"
         "    const screen_fb_info_t* fb = screen_fb_get_info();\n"
         "    for (u32 row = 0; row < 16; ++row) {\n"
         "        u16 bits = (u16)((glyph[row * 2] << 8) | glyph[row * 2 + 1]);\n"
         "        u32 ypix = y + row;\n"
         "        if (ypix >= fb->height) break;\n"
         "        for (u32 col = 0; col < 16; ++col) {\n"
         "            u32 xpix = x + col;\n"
         "            if (xpix >= fb->width) break;\n"
         "            screen_fb_write_px(xpix, ypix,\n"
         "                               (bits & (0x8000 >> col)) ? fg : bg);\n"
         "        }\n"
         "    }\n"
         "}")


# ---------------------------------------------------------------------------
# E20 (3.0.0-WP06f): regenerated font data freshness guard.
# tools/gen_chinese_font.py rasterize() was mapping the 16px cell to the
# em box [0,64] while Unifont's glyph box is baseline-relative
# [descent..ascent] = [-8,+56] - every hanzi sat 2 rows too low and the
# bottom 2 rows of strokes were clipped (missing strokes).  The fixed
# generator's output is committed; this guard aborts loudly if a
# re-vendor restored the v1.0.0 (pre-fix) data, so the operator
# regenerates instead of shipping clipped glyphs.
# ---------------------------------------------------------------------------
def e20_font_freshness():
    t = _rd("ocse/chinese/chinese_font_data.c")
    # WP06f 啊 (hanzi idx 0): content begins at byte 2 (row 1) and the
    # last row carries strokes.  The v1.0.0 data began 0x00,0x00,0x00,
    # 0x00,0x00,0x00,0x0E,0xFC (content at row 3).
    if "{0x00,0x00,0x0E,0xFC,0xEA,0x08" in t:
        return
    sys.exit("[ocse_edits] E20: ocse/chinese/chinese_font_data.c carries the "
             "pre-WP06f (clipped) glyph data - regenerate it:\n"
             "  python3 tools/gen_chinese_font.py   # fixed rasterizer\n"
             "  cp chinese/chinese_font_data.c ocse/chinese/chinese_font_data.c\n"
             "  sed -i 's|#include \"../core/types.h\"|#include \"../kernel/types.h\"|' "
             "ocse/chinese/chinese_font_data.c\n"
             "  rm -rf chinese/")


# ---------------------------------------------------------------------------
# E21 (3.0.0-WP06g3-ocse_enc_fix, task 4.1): disk-probe diagnostics reach
# the serial line + RAID/RST-mode SATA visibility + NVMe real-hardware
# tolerance.  Postmortem disaster 1: the OCSE AHCI/NVMe/ATA probe logs go
# to screen_console_puts ONLY - a serial user saw the boot ladder and
# then "no usable disk" with the whole probe walk in between invisible;
# and the probe surface only matched PCI class 010601/010802 exactly, so
# a factory-default RAID/RST-mode SATA controller (class 0x0104xx, the
# OEM default on many brand-name machines) was never even enumerated.
# ---------------------------------------------------------------------------
def e21_disk_probe_diag():
    # ---- (a) AHCI: log helper -> screen + serial dual path ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         '/* ---- logging helpers ---- */\n'
         'static void driver_block_ahci_log(const char *s) { screen_console_puts(s); }\n',
         '/* ---- logging helpers ---- */\n'
         '/* WP06g3-4.1: probe diagnostics must reach the serial line too - the\n'
         ' * postmortem caught probe output painted on the screen only, leaving\n'
         ' * serial users with a black box between the boot ladder and the\n'
         ' * "no usable disk" stop. */\n'
         'extern void ocse_serial_puts_raw(const char *s);\n'
         'static void driver_block_ahci_log(const char *s) {\n'
         '    screen_console_puts(s);\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n')

    # ---- (b) AHCI: version register (RAID-card guard) ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         '#define AHCI_REG_CAP2       0x24u\n',
         '#define AHCI_REG_CAP2       0x24u\n'
         '#define AHCI_REG_VS         0x10u   /* WP06g3: version reg (RAID-card guard) */\n')

    # ---- (c) AHCI: 2 -> 4 controllers ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         'static driver_block_ahci_ctrl_t  g_ctrls[2];\n',
         'static driver_block_ahci_ctrl_t  g_ctrls[4];   /* WP06g3: was 2 - real machines routinely carry more */\n')

    # ---- (d) AHCI: CAP/VS sanity guard before the port walk ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         '    c->mmio_base = bar;\n'
         '\n'
         '    /* Enable AHCI mode, keep host interrupts off. */\n',
         '    c->mmio_base = bar;\n'
         '\n'
         '    /* WP06g3-4.1: RAID/RST-mode SATA controllers (PCI class 0x0104xx,\n'
         '     * the factory default on many OEM machines) expose the same AHCI\n'
         '     * register block at BAR5 - but a hardware RAID card does not.\n'
         '     * Gate on a sane CAP (never zero: at least NP+SSC bits are set on\n'
         '     * every real HBA) and a sane VS (major byte 1..2 per AHCI 1.x-\n'
         '     * 2.x) BEFORE the port walk, so a non-AHCI block is refused with\n'
         '     * a clear log line instead of a blind probe that hangs. */\n'
         '    {\n'
         '        u32 cap_probe = mmio_read32((volatile void *)(bar + AHCI_REG_CAP));\n'
         '        u32 vs_probe  = mmio_read32((volatile void *)(bar + AHCI_REG_VS));\n'
         '        u32 vs_major  = (vs_probe >> 16) & 0xFFu;\n'
         '        if (cap_probe == 0u || vs_probe == 0u || vs_major == 0u || vs_major > 2u) {\n'
         '            driver_block_ahci_log_hex("ahci: BAR5 is not an AHCI register block (CAP=0x",\n'
         '                                      (u64)cap_probe, " VS=0x");\n'
         '            driver_block_ahci_log_hex("", (u64)vs_probe, ") - controller skipped\\n");\n'
         '            return -1;\n'
         '        }\n'
         '    }\n'
         '\n'
         '    /* Enable AHCI mode, keep host interrupts off. */\n')

    # ---- (e) AHCI: extension path accepts RAID/RST class too ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         '        if (cls != 0x010601u) {\n'
         '            driver_block_ahci_log("ahci: requested PCI function is not class 010601\\n");\n'
         '            return -1;\n'
         '        }\n',
         '        /* WP06g3-4.1: also accept RAID/RST-mode SATA (class 0x0104xx) -\n'
         '         * the OEM default on many brand-name machines. */\n'
         '        if (cls != 0x010601u && (cls >> 8) != 0x0104u) {\n'
         '            driver_block_ahci_log("ahci: requested PCI function is not class 010601/0104xx\\n");\n'
         '            return -1;\n'
         '        }\n')

    # ---- (f) AHCI: probe RAID/RST-class controllers too ----
    edit("ocse/drivers/block/driver_block_ahci.c",
         '    /* Find up to 2 AHCI controllers. */\n'
         '    for (int nth = 0; nth < 2; nth++) {\n'
         '        u8 bus = 0, dev = 0, func = 0;\n'
         '        if (driver_pci_find_class_exact(0x010601u, nth, &bus, &dev, &func) != 0) break;\n'
         '        driver_block_ahci_init_ctrl(bus, dev, func);\n'
         '    }\n'
         '    if (g_ctrl_count == 0) {\n',
         '    /* Find up to 2 standard AHCI controllers (class 010601). */\n'
         '    for (int nth = 0; nth < 2; nth++) {\n'
         '        u8 bus = 0, dev = 0, func = 0;\n'
         '        if (driver_pci_find_class_exact(0x010601u, nth, &bus, &dev, &func) != 0) break;\n'
         '        driver_block_ahci_init_ctrl(bus, dev, func);\n'
         '    }\n'
         '    /* WP06g3-4.1: then RAID/RST-mode SATA controllers (subclass 0x0104,\n'
         '     * any programming interface).  Intel RST and friends expose the\n'
         '     * same AHCI register block at BAR5 in RAID mode; init_ctrl\'s\n'
         '     * CAP/VS guard refuses a real hardware RAID card with a log line\n'
         '     * instead of a blind probe.  Without this loop a factory-default\n'
         '     * RAID-mode SATA controller is invisible and the machine reports\n'
         '     * no usable disk. */\n'
         '    for (int nth = 0; nth < 2; nth++) {\n'
         '        u8 bus = 0, dev = 0, func = 0;\n'
         '        if (driver_pci_find_class_mask(0x010400u, 0xFFFF00u, nth, &bus, &dev, &func) != 0) break;\n'
         '        driver_block_ahci_log("ahci: RAID/RST-class SATA controller (0104xx) - probing as AHCI\\n");\n'
         '        driver_block_ahci_init_ctrl(bus, dev, func);\n'
         '    }\n'
         '    if (g_ctrl_count == 0) {\n')

    # ---- (g) NVMe: log helper -> screen + serial dual path ----
    edit("ocse/drivers/block/driver_block_nvme.c",
         'static void driver_block_nvme_log(const char *s) { screen_console_puts(s); }\n',
         '/* WP06g3-4.1: probe diagnostics must reach the serial line too. */\n'
         'extern void ocse_serial_puts_raw(const char *s);\n'
         'static void driver_block_nvme_log(const char *s) {\n'
         '    screen_console_puts(s);\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n')

    # ---- (h) NVMe: TO-scaled RDY poll budget ----
    edit("ocse/drivers/block/driver_block_nvme.c",
         '#define NVME_POLL_RESET_ITERS   50000000u\n'
         '#define NVME_POLL_CMD_ITERS     50000000u\n',
         '#define NVME_POLL_RESET_ITERS   50000000u\n'
         '#define NVME_POLL_CMD_ITERS     50000000u\n'
         '\n'
         '/* WP06g3-4.1: CSTS.RDY poll budget, scaled from CAP.TO at init.  The\n'
         ' * fixed 50M-iteration budget (~5s) is shorter than the worst-case\n'
         ' * readiness window real controllers may take (CAP.TO is in units of\n'
         ' * 500ms and the spec allows up to 0xF = 7.5s) - a slow-but-healthy\n'
         ' * controller was declared dead on real machines.  ~100ns per\n'
         ' * iteration => 5M iterations per 500ms unit; never below 50M. */\n'
         'static u64 g_rdy_poll_iters = NVME_POLL_RESET_ITERS;\n')
    edit("ocse/drivers/block/driver_block_nvme.c",
         'static int driver_block_nvme_wait_rdy(driver_block_nvme_dev_t *d, int target) {\n'
         '    for (u64 i = 0; i < NVME_POLL_RESET_ITERS; i++) {\n',
         'static int driver_block_nvme_wait_rdy(driver_block_nvme_dev_t *d, int target) {\n'
         '    for (u64 i = 0; i < g_rdy_poll_iters; i++) {\n')
    edit("ocse/drivers/block/driver_block_nvme.c",
         '    g_nvme.dstrd = (u32)NVME_CAP_DSTRD(cap);\n'
         '    driver_block_nvme_log_hex("nvme: CAP=0x", cap, "\\n");\n',
         '    g_nvme.dstrd = (u32)NVME_CAP_DSTRD(cap);\n'
         '    /* WP06g3-4.1: scale the RDY poll budget with CAP.TO (units of\n'
         '     * 500ms, ~5M iterations per unit at ~100ns/iter) so slow-but-\n'
         '     * healthy real controllers are not declared dead. */\n'
         '    {\n'
         '        u64 to_units = (cap >> 48) & 0xFFu;\n'
         '        u64 want = to_units * 5000000u;\n'
         '        if (want > g_rdy_poll_iters) g_rdy_poll_iters = want;\n'
         '    }\n'
         '    driver_block_nvme_log_hex("nvme: CAP=0x", cap, "\\n");\n')

    # ---- (i) NVMe: disable timeout tolerance (rewrite CC=0 + retry) ----
    edit("ocse/drivers/block/driver_block_nvme.c",
         '    /* Disable controller first; wait for RDY=0. */\n'
         '    driver_block_nvme_write(&g_nvme, NVME_REG_CC, 0);\n'
         '    if (driver_block_nvme_wait_rdy(&g_nvme, 0) != 0) {\n'
         '        driver_block_nvme_log("nvme: timeout waiting for disable\\n");\n'
         '        return -1;\n'
         '    }\n',
         '    /* Disable controller first; wait for RDY=0.\n'
         '     * WP06g3-4.1 (real-hardware tolerance): on a genuine disable\n'
         '     * timeout rewrite CC=0 once and wait again with the TO-scaled\n'
         '     * budget before giving up - some real chips miss the first\n'
         '     * transition window (firmware still settling after power-on). */\n'
         '    driver_block_nvme_write(&g_nvme, NVME_REG_CC, 0);\n'
         '    if (driver_block_nvme_wait_rdy(&g_nvme, 0) != 0) {\n'
         '        driver_block_nvme_log("nvme: disable timeout, rewriting CC=0 and retrying\\n");\n'
         '        driver_block_nvme_write(&g_nvme, NVME_REG_CC, 0);\n'
         '        if (driver_block_nvme_wait_rdy(&g_nvme, 0) != 0) {\n'
         '            driver_block_nvme_log("nvme: timeout waiting for disable\\n");\n'
         '            return -1;\n'
         '        }\n'
         '    }\n')

    # ---- (j) NVMe: enable timeout tolerance (full reset + retry round) ----
    edit("ocse/drivers/block/driver_block_nvme.c",
         '        driver_block_nvme_write(&g_nvme, NVME_REG_CC, cc);\n'
         '    }\n'
         '    if (driver_block_nvme_wait_rdy(&g_nvme, 1) != 0) {\n'
         '        driver_block_nvme_log("nvme: timeout waiting for ready\\n");\n'
         '        return -1;\n'
         '    }\n'
         '    driver_block_nvme_log("nvme: controller enabled\\n");\n',
         '        driver_block_nvme_write(&g_nvme, NVME_REG_CC, cc);\n'
         '    }\n'
         '    if (driver_block_nvme_wait_rdy(&g_nvme, 1) != 0) {\n'
         '        /* WP06g3-4.1 (real-hardware tolerance): full reset-and-retry\n'
         '         * round before declaring the controller dead - some real\n'
         '         * chips miss the first CC.EN=1 ready window (firmware still\n'
         '         * booting after power-on).  Disable, reprogram the admin\n'
         '         * queues (the buffers are still allocated; only the registers\n'
         '         * are rewired), and enable once more with the TO-scaled\n'
         '         * budget. */\n'
         '        driver_block_nvme_log("nvme: ready timeout, resetting and retrying\\n");\n'
         '        driver_block_nvme_write(&g_nvme, NVME_REG_CC, 0);\n'
         '        (void)driver_block_nvme_wait_rdy(&g_nvme, 0);\n'
         '        {\n'
         '            u64 asq_phys = (u64)(uintptr_t)g_nvme.asq;\n'
         '            u64 acq_phys = (u64)(uintptr_t)g_nvme.acq;\n'
         '            u32 aqa_r = ((g_nvme.admin_qsize - 1u) << 16) | (g_nvme.admin_qsize - 1u);\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_AQA, aqa_r);\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_ASQ,     (u32)(asq_phys & 0xFFFFFFFFu));\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_ASQ + 4, (u32)(asq_phys >> 32));\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_ACQ,     (u32)(acq_phys & 0xFFFFFFFFu));\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_ACQ + 4, (u32)(acq_phys >> 32));\n'
         '            driver_block_nvme_write(&g_nvme, NVME_REG_CC,\n'
         '                NVME_CC_EN\n'
         '                | (0u << NVME_CC_CSS_SHIFT)\n'
         '                | (0u << NVME_CC_MPS_SHIFT)\n'
         '                | (0u << NVME_CC_AMS_SHIFT)\n'
         '                | (6u << 16)\n'
         '                | (4u << 20));\n'
         '        }\n'
         '        if (driver_block_nvme_wait_rdy(&g_nvme, 1) != 0) {\n'
         '            driver_block_nvme_log("nvme: timeout waiting for ready\\n");\n'
         '            return -1;\n'
         '        }\n'
         '    }\n'
         '    driver_block_nvme_log("nvme: controller enabled\\n");\n')

    # ---- (k) ATA: probe report -> screen + serial dual path ----
    edit("ocse/drivers/block/driver_block_ata.c",
         '#include "driver_block_ata.h"\n'
         '#include "screen_console.h"\n'
         '#include "lib_string.h"\n',
         '#include "driver_block_ata.h"\n'
         '#include "screen_console.h"\n'
         '#include "lib_string.h"\n'
         '\n'
         '/* WP06g3-4.1: probe diagnostics must reach the serial line too. */\n'
         'extern void ocse_serial_puts_raw(const char *s);\n')
    edit("ocse/drivers/block/driver_block_ata.c",
         '            strcpy(msg + strlen(msg), " present\\n");\n'
         '            screen_console_puts(msg);\n',
         '            strcpy(msg + strlen(msg), " present\\n");\n'
         '            screen_console_puts(msg);\n'
         '            ocse_serial_puts_raw(msg);\n')

    # ---- (l) ATA DMA: dlog -> screen + serial dual path ----
    edit("ocse/drivers/block/driver_block_ata_dma.c",
         '/* ---- logging ---- */\n'
         'static void dlog(const char *s) { screen_console_puts(s); }\n',
         '/* ---- logging ---- */\n'
         '/* WP06g3-4.1: probe diagnostics must reach the serial line too. */\n'
         'extern void ocse_serial_puts_raw(const char *s);\n'
         'static void dlog(const char *s) {\n'
         '    screen_console_puts(s);\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n')

    # ---- (m) virtio-blk: log helper -> screen + serial dual path ----
    edit("ocse/drivers/block/driver_block_virtio_blk.c",
         'static void driver_block_virtio_blk_log(const char *s) { screen_console_puts(s); }\n',
         '/* WP06g3-4.1: probe diagnostics must reach the serial line too. */\n'
         'extern void ocse_serial_puts_raw(const char *s);\n'
         'static void driver_block_virtio_blk_log(const char *s) {\n'
         '    screen_console_puts(s);\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n')


# ---------------------------------------------------------------------------
# E22 (3.0.0-WP06g3-ocse_enc_fix, tasks 4.2+4.8): one serial encoding
# standard + the OCSE screen mojibake root fix.
# Postmortem disasters 2/3: ocse_out()'s zh path converted UTF-8 to
# GB2312 and fed the GB2312 bytes BOTH to the stock ASCII console
# (screen mojibake) and to the serial line (UTF-8 terminal mojibake) -
# while the vendored console_cn wide renderer sat initialized but
# never called.  The serial line now emits in a CONFIGURED encoding
# (serial_encoding utf8|gbk, default utf8 - one standard with the main
# kernel), and the zh screen path renders UTF-8 through console_cn.
# ---------------------------------------------------------------------------
def e22_ocse_out_enc():
    # ---- (a) ocse_i18n.c: console_cn include ----
    edit("ocse/ocse_i18n.c",
         '#include "ocse_i18n.h"\n'
         '#include "encoding.h"\n'
         '#include "screen_console.h"\n'
         '#include "lib_string.h"\n',
         '#include "ocse_i18n.h"\n'
         '#include "encoding.h"\n'
         '#include "screen_console.h"\n'
         '#include "lib_string.h"\n'
         '#include "console_cn.h"   /* WP06g3: zh screen path renders wide */\n')

    # ---- (b) ocse_i18n.c: rewrite ocse_out/ocse_outln ----
    edit("ocse/ocse_i18n.c",
         'void ocse_out(const char* s) {\n'
         '    if (g_cap_buf) { cap_puts(s); ocse_serial_puts_raw(s); return; }\n'
         '    if (g_lang == OCSE_LANG_ZH) {\n'
         '        static char conv[512];\n'
         '        u32 n = 0;\n'
         '        int rc = encoding_convert((const u8*)s, (u32)strlen(s),\n'
         '                                  ENC_UTF8, ENC_GB2312,\n'
         '                                  (u8*)conv, sizeof(conv) - 1, &n);\n'
         '        if (rc == 0 && n > 0) {\n'
         '            conv[n] = 0;\n'
         '            screen_console_puts(conv);\n'
         '            ocse_serial_puts_raw(conv);\n'
         '            return;\n'
         '        }\n'
         '        /* fall through on conversion failure: raw bytes are better\n'
         '         * than silence. */\n'
         '    }\n'
         '    screen_console_puts(s);\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n'
         '\n'
         'void ocse_outln(const char* s) {\n'
         '    if (g_cap_buf) {\n'
         '        cap_puts(s);\n'
         '        cap_puts("\\n");\n'
         '        ocse_serial_puts_raw(s);\n'
         '        ocse_serial_puts_raw("\\r\\n");\n'
         '        return;\n'
         '    }\n'
         '    ocse_out(s);\n'
         '    screen_console_putc(\'\\n\');\n'
         '    ocse_serial_puts_raw("\\r\\n");\n'
         '}\n',
         '/* WP06g3-4.2: the serial line output encoding - ONE standard\n'
         ' * shared with the main kernel\'s serial_encoding setting\n'
         ' * (utf8 default, gbk for the GBK-default Chinese serial-terminal\n'
         ' * ecosystem).  The old zh path put GB2312 bytes on the serial\n'
         ' * line - mojibake on every UTF-8 terminal (postmortem disaster 2). */\n'
         'static int g_serial_gbk = 0;\n'
         '\n'
         'void ocse_serial_encoding_set_str(const char* s) {\n'
         '    g_serial_gbk = (s && s[0]==\'g\' && s[1]==\'b\' && s[2]==\'k\'\n'
         '                   && s[3]==0) ? 1 : 0;\n'
         '}\n'
         '\n'
         'const char* ocse_serial_encoding_name(void) {\n'
         '    return g_serial_gbk ? "gbk" : "utf8";\n'
         '}\n'
         '\n'
         '/* Emit to the serial line in the CONFIGURED encoding.  GBK mode\n'
         ' * converts UTF-8 through the vendored codec; a conversion failure\n'
         ' * degrades to the raw UTF-8 bytes (better than silence). */\n'
         'static void ocse_serial_emit(const char* s) {\n'
         '    if (!g_serial_gbk) { ocse_serial_puts_raw(s); return; }\n'
         '    static char conv[512];\n'
         '    u32 n = 0;\n'
         '    if (encoding_convert((const u8*)s, (u32)strlen(s),\n'
         '                          ENC_UTF8, ENC_GB2312,\n'
         '                          (u8*)conv, sizeof(conv) - 1, &n) == 0 && n > 0) {\n'
         '        conv[n] = 0;\n'
         '        ocse_serial_puts_raw(conv);\n'
         '        return;\n'
         '    }\n'
         '    ocse_serial_puts_raw(s);\n'
         '}\n'
         '\n'
         '/* WP06g3-4.8: the zh SCREEN path renders through the vendored\n'
         ' * console_cn wide renderer (UTF-8, codepoint-indexed glyphs)\n'
         ' * instead of feeding GB2312 bytes to the stock ASCII console -\n'
         ' * the exact screen mojibake the postmortem photographed.  The\n'
         ' * GUI capture path keeps the raw UTF-8 (the GUI canvas decodes\n'
         ' * UTF-8 itself, gui_text()). */\n'
         'void ocse_out(const char* s) {\n'
         '    if (g_cap_buf) { cap_puts(s); ocse_serial_emit(s); return; }\n'
         '    if (g_lang == OCSE_LANG_ZH) {\n'
         '        console_cn_puts(s);\n'
         '    } else {\n'
         '        screen_console_puts(s);\n'
         '    }\n'
         '    ocse_serial_emit(s);\n'
         '}\n'
         '\n'
         'void ocse_outln(const char* s) {\n'
         '    if (g_cap_buf) {\n'
         '        cap_puts(s);\n'
         '        cap_puts("\\n");\n'
         '        ocse_serial_emit(s);\n'
         '        ocse_serial_puts_raw("\\r\\n");\n'
         '        return;\n'
         '    }\n'
         '    ocse_out(s);\n'
         '    screen_console_putc(\'\\n\');\n'
         '    ocse_serial_puts_raw("\\r\\n");\n'
         '}\n')

    # ---- (c) ocse_i18n.h: public API for the setting ----
    edit("ocse/ocse_i18n.h",
         'void ocse_out(const char* s);\n'
         'void ocse_outln(const char* s);\n',
         '/* WP06g3-4.2: serial line output encoding (utf8|gbk) - one\n'
         ' * standard with the main kernel\'s serial_encoding command. */\n'
         'void         ocse_serial_encoding_set_str(const char* s);\n'
         'const char*  ocse_serial_encoding_name(void);\n'
         '\n'
         'void ocse_out(const char* s);\n'
         'void ocse_outln(const char* s);\n')

    # ---- (d) OCSECL: the serial_encoding command ----
    edit("ocse/ocse_cl.c",
         'static int cmd_clear(int argc, char** argv) {',
         '/* WP06g3-4.2: serial line output encoding - the same setting the\n'
         ' * main kernel exposes (serial_encoding utf8|gbk).  System\n'
         ' * strings stay English; the encoding names are the terminology. */\n'
         'static int cmd_serial_encoding(int argc, char** argv) {\n'
         '    if (argc == 1) {\n'
         '        ocse_out("serial encoding: ");\n'
         '        ocse_outln(ocse_serial_encoding_name());\n'
         '        return 0;\n'
         '    }\n'
         '    if (argc == 2 && (strcmp(argv[1], "utf8") == 0 ||\n'
         '                      strcmp(argv[1], "gbk") == 0)) {\n'
         '        ocse_serial_encoding_set_str(argv[1]);\n'
         '        ocse_out("serial encoding: ");\n'
         '        ocse_outln(ocse_serial_encoding_name());\n'
         '        return 0;\n'
         '    }\n'
         '    ocse_outln("usage: serial_encoding [utf8|gbk]");\n'
         '    return 1;\n'
         '}\n'
         '\n'
         'static int cmd_clear(int argc, char** argv) {')
    edit("ocse/ocse_cl.c",
         '    { "lang",     "show or set the language: lang [en|zh]", cmd_lang },\n',
         '    { "lang",     "show or set the language: lang [en|zh]", cmd_lang },\n'
         '    { "serial_encoding", "show/set the serial line encoding: serial_encoding [utf8|gbk]", cmd_serial_encoding },\n')



# ---------------------------------------------------------------------------
# E24 (3.0.0-WP06g3-ocse_enc_fix, task 4.8): the em-dash mapping fix.
# GB2312 0xA1AA is the em dash, but the font generator's extraction
# codec (Python gb2312) mapped that slot to U+2015 (HORIZONTAL BAR)
# while real-world source strings carry U+2014 (EM DASH) - the lookup
# missed, and a zh string containing an em dash failed the WHOLE-STRING
# GB2312 conversion (and missed the glyph on screen).  U+2014 now folds
# onto the table's U+2015 - same fix as the main kernel's
# services/font/chinese_font.c.
# ---------------------------------------------------------------------------
def e24_emdash():
    edit("ocse/chinese/chinese_font.c",
         'int chinese_font_lookup_unicode(chinese_font_handle_t slot, u32 cp) {\n'
         '    const chinese_font_slot_t* s = slot_ok(slot);\n'
         '    if (!s) return -3;\n'
         '    g_lookups++;\n',
         'int chinese_font_lookup_unicode(chinese_font_handle_t slot, u32 cp) {\n'
         '    const chinese_font_slot_t* s = slot_ok(slot);\n'
         '    if (!s) return -3;\n'
         '    g_lookups++;\n'
         '    /* WP06g3-4.8 (em-dash fix): fold U+2014 (EM DASH, what\n'
         '     * real-world text carries) onto the table\'s U+2015 (the\n'
         '     * Python gb2312 codec mapped 0xA1AA there) - an em dash in\n'
         '     * a zh string neither kills the whole-string GB2312\n'
         '     * conversion nor misses its glyph on screen. */\n'
         '    if (cp == 0x2014) cp = 0x2015;\n')


e18_double_buffer()
e19_glyph_routing()
e20_font_freshness()
e21_disk_probe_diag()
e22_ocse_out_enc()
e23_placeholder_unused = None
e24_emdash()

print("[ocse_edits] %d edit groups applied - OCSE tree is build-ready" % applied)
