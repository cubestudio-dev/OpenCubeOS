#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
nak_migrate.py: WP-refactor-to-nak-architecture physical migration engine.

Generates the FULL old->new file map for the NAK (New Architecture
Kernel) restructure, verifies its uniqueness invariants, then (with
--apply) performs the `git mv` moves and (with --rewrite) rewrites
every `#include "old_name.h"` occurrence across the tree.

Layout produced (NAK spec):
  ocos/kernel/            core_*.c/.h (flat, prefix-grouped) + arch/ + generated/
  ocos/drivers/<class>/   drv_*.c/.h (net storage usb input display bus audio time misc)
  ocos/services/<svc>/    interface/svc_*_api.h + src/svc_*.c
  ocos/interface/         the ring-3 API surface (was src/L1/_shared)
  ocos/l1apps/            ring-3 application sources (was src/L1)
  ocos/apps/              ring-3 build products (pure ELF; gitignored)

Usage:
  python3 tools/nak_migrate.py            # print the map + verify
  python3 tools/nak_migrate.py --apply    # do the git mv moves
  python3 tools/nak_migrate.py --rewrite  # rewrite includes/strings
  python3 tools/nak_migrate.py --apply --rewrite
The map is also written to tools/nak_map.tsv (the deliverable
"full rename manifest" required by the WP acceptance report).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_TSV = os.path.join(ROOT, "tools", "nak_map.tsv")


# --------------------------------------------------------------------------
# Rule tables
# --------------------------------------------------------------------------

# driver family dir -> NAK class dir
DRV_CLASS = {
    "nic": "net",
    "wireless": "net",
    "block": "storage",
    "sd": "storage",
    "scsi": "storage",
    "usb": "usb",
    "input": "input",
    "pci": "bus",
    "acpi": "bus",
    "i2c": "bus",
    "snd": "audio",
    "time": "time",
    "bluetooth": "misc",
    "camera": "misc",
    "misc": "misc",
}

# per-driver-family name normalization: (family dir, old basename w/o ext)
# -> new basename w/o ext.  Applied before the generic rule.
DRV_NAME_SPECIAL = {
    # net
    ("nic", "driver_nic_other"): "drv_net_other",
    # storage
    ("sd", "driver_sdhci"): "drv_storage_sdhci",
    ("scsi", "driver_scsi"): "drv_storage_scsi",
    ("scsi", "driver_scsi_lsi"): "drv_storage_scsi_lsi",
    # input (the two console input paths live under drivers/input)
    ("input", "screen_console_in"): "drv_input_console",
    ("input", "screen_serial_in"): "drv_input_serial",
    # camera helpers
    ("camera", "lib_mjpeg"): "drv_misc_mjpeg",
    ("camera", "driver_camera_uvc"): "drv_misc_camera_uvc",
    # semantic preservation (avoid lossy role names)
    ("acpi", "driver_acpi_aml"): "drv_bus_acpi_aml",
    ("acpi", "driver_acpi_power"): "drv_bus_acpi_power",
    ("i2c", "driver_i2c_dw"): "drv_bus_i2c_dw",
    ("usb", "driver_usb"): "drv_usb_core",
    ("bluetooth", "driver_bt_h4"): "drv_misc_bt_h4",
    ("bluetooth", "driver_bt_hci"): "drv_misc_bt_hci",
    ("bluetooth", "driver_bt_hid"): "drv_misc_bt_hid",
    ("bluetooth", "driver_bt_l2cap"): "drv_misc_bt_l2cap",
    ("bluetooth", "driver_bt_rfcomm"): "drv_misc_bt_rfcomm",
    ("bluetooth", "driver_bt_sdp"): "drv_misc_bt_sdp",
    ("bluetooth", "driver_bt_spp"): "drv_misc_bt_spp",
    ("bluetooth", "driver_bt_usb"): "drv_misc_bt_usb",
    # driver-family manager headers move to the driver class as the
    # driver-facing API contract (services consume, drivers implement)
    ("block", "blk_drv_internal"): "drv_storage_api",
    ("nic", "nic_drv_internal"): "drv_net_api",
    ("snd", "snd_drv_internal"): "drv_audio_api",
}

# driver-family managers: the .c moves into the OWNING service
DRV_MGR_TO_SERVICE = {
    ("block", "block_svc"): ("block", "svc_block_drivers"),
    ("nic", "nic_svc"): ("net", "svc_net_drivers"),
    ("snd", "snd_svc"): ("audio", "svc_audio_drivers"),
}

# service dir -> (service name, list of stripped filename prefixes).
# The remainder becomes the "role" part of svc_<name>_<role>.{c,h}.
SVC_PREFIX = {
    "audio": ("audio", ["snd_"]),
    "block": ("block", ["blk_", "block_"]),
    "camera": ("camera", ["cam_"]),
    "crash": ("crash", ["core_"]),
    "crypto": ("crypto", ["crypto_"]),
    "diag": ("diag", ["diag_"]),
    "display": ("display", ["disp_", "console_", "ime_", "inp_", "pinyin_",
                            "serial_enc"]),
    "encoding": ("encoding", ["encoding"]),
    "font": ("font", ["chinese_", "font_", "render_"]),
    "fs": ("fs", ["fs_", "lib_fs"]),
    "health": ("health", ["core_", "hlth_"]),
    "ipc": ("ipc", ["l1_"]),
    "klog": ("klog", ["klog_"]),
    "l1": ("l1", ["l1_"]),
    "loader": ("loader", ["l1_"]),
    "net": ("net", ["net_", "nic_", "wl_"]),
    "ocembed": ("ocembed", ["ocembed"]),
    "power": ("power", ["core_", "pwr_"]),
    "selfhost": ("selfhost", ["ota_"]),
    "sysapi": ("sysapi", ["core_", "l1svc_"]),
    "time": ("time", ["core_", "time_"]),
    "tui": ("tui", ["tui_"]),
    "usb": ("usb", ["usb_"]),
    "watchdog": ("watchdog", ["wdt_"]),
}

# explicit per-file overrides for service files whose generic derivation
# would be wrong.  (svc dir, old basename w/o ext) -> role name
SVC_NAME_SPECIAL = {
    ("fs", "lib_fspath"): "path",          # svc_fs_path.c  -> svc_fs_path.c
    ("encoding", "encoding"): "core",      # svc_encoding_core.c    -> svc_encoding_core.c
    ("l1", "l1_l1"): "core",               # svc_l1_core.c       -> svc_l1_core.c
    ("ocembed", "ocembed"): "core",        # svc_ocembed_core.c     -> svc_ocembed_core.c
    ("sysapi", "core_syscall"): "syscall",
    ("sysapi", "l1svc_defs"): "l1svc",     # svc_sysapi_l1svc.h  -> svc_sysapi_l1svc.h
    ("time", "core_rtc"): "rtc",
    # collision breaks (same role derived from different prefixes)
    ("net", "nic_cmds"): "nic_cmds",
    ("net", "wl_cmds"): "wl_cmds",
    ("net", "wl_cmds_bt"): "wl_cmds_bt",
    ("net", "wl_fw"): "wl_fw",
    ("net", "nic_l1"): "nic_l1",
    ("display", "inp_cmds"): "input_cmds",
    ("display", "inp_l1"): "input_l1",
    # semantic preservation (avoid doubled / lossy names)
    ("crash", "core_crash"): "core",       # -> svc_crash_core.c
    ("health", "core_health"): "core",     # -> svc_health_core.c
    ("power", "core_power"): "core",       # -> svc_power_core.c
    ("font", "chinese_font"): "chinese",   # -> svc_font_chinese.c
    ("font", "chinese_font_data"): "chinese_data",
    ("font", "render_cn"): "render_cn",    # -> svc_font_render_cn.c
    ("display", "ime_engine"): "ime_engine",
    ("display", "ime_session"): "ime_session",
    ("display", "pinyin_data"): "pinyin_data",
    ("display", "pinyin_phrase_data"): "pinyin_phrase_data",
    ("display", "pinyin_phrase"): "pinyin_phrase",
    ("tui", "tui"): "api",                 # svc_tui_api.h -> svc_tui_api.h
}

# kernel flat renames (src/L0/boot + src/L0/core top level + mem/lib/console)
KERNEL_RENAME = {
    # boot composition root
    "core_boot_entry.S": "core_boot_entry.S",
    "core_boot_longmode.S": "core_boot_longmode.S",
    "core_boot_mb2header.S": "core_boot_mb2header.S",
    "boot_state": "core_boot_state",
    "grub_boot_stub": "core_boot_grubstub",
    "kmain": "core_boot_main",
    "l1_launcher": "core_boot_l1launch",
    "l1_selftest": "core_boot_selftest",
    "core_boot_grubdata.h": "core_boot_grubdata.h",
    # core top level keeps its core_ names already
    # mem/
    "mem_pmm": "core_mm_pmm",
    "mem_vmm": "core_mm_vmm",
    "mem_heap": "core_mm_heap",
    # lib/
    "lib_string": "core_lib_string",
    "lib_log": "core_lib_log",
    "lib_klog": "core_lib_klog",
    "lib_config": "core_lib_config",
    "lib_gzip": "core_lib_gzip",
    "lib_tar": "core_lib_tar",
    "l0_i18n": "core_lib_i18n",
    "l0_i18n_data": "core_lib_i18n_data",
    # console/
    "screen_console": "core_console_screen",
    "screen_fb": "core_console_fb",
    "screen_font": "core_console_font",
    "screen_font_data": "core_console_font_data",
}

# service sources that are INTERNAL (stay in src/, never interface/).
# Keyed by (service dir, basename WITHOUT extension).
SVC_HEADER_INTERNAL = {
    ("display", "pinyin_data"),
    ("display", "pinyin_phrase_data"),
    ("font", "chinese_font_data"),
}

# service headers forced PUBLIC regardless of the reference analysis
# (the service's umbrella API header).
SVC_HEADER_FORCE_PUBLIC = {
    ("tui", "svc_tui_api.h"),
}


def q(*args):
    return os.path.join(ROOT, *args)


def list_files(base):
    out = []
    for dirpath, _dirnames, filenames in os.walk(q(base)):
        for f in filenames:
            out.append(os.path.relpath(os.path.join(dirpath, f), ROOT))
    return out


def drv_new_basename(fam, stem):
    if (fam, stem) in DRV_NAME_SPECIAL:
        return DRV_NAME_SPECIAL[(fam, stem)]
    cls = DRV_CLASS[fam]
    # strip the old family prefix: driver_nic_e1000e -> e1000e,
    # driver_block_ata -> ata, driver_usb_xhci -> xhci,
    # driver_input_keyboard -> keyboard, driver_snd_hda -> hda,
    # driver_cmos_rtc -> cmos_rtc (time family has no uniform prefix)
    for pre in ("driver_nic_", "driver_block_", "driver_usb_",
                "driver_input_", "driver_snd_", "driver_acpi_",
                "driver_i2c_", "driver_bt_", "driver_camera_",
                "driver_", "driver"):
        if stem.startswith(pre):
            role = stem[len(pre):]
            break
    else:
        role = stem
    return "drv_%s_%s" % (cls, role)


def svc_new_basename(svc_dir, stem):
    if (svc_dir, stem) in SVC_NAME_SPECIAL:
        role = SVC_NAME_SPECIAL[(svc_dir, stem)]
    else:
        name, prefixes = SVC_PREFIX[svc_dir]
        role = stem
        for pre in prefixes:
            if stem.startswith(pre) and stem != pre:
                role = stem[len(pre):]
                break
        if not role:
            role = "core"
    name = SVC_PREFIX[svc_dir][0]
    return "svc_%s_%s" % (name, role)


def scan_include_users():
    """Map header basename -> set of includer relative paths (old tree)."""
    import re
    inc_re = re.compile(r'#include\s+"([^"]+)"')
    users = {}
    for base in ("src/L0", "src/L1", "tests", "ocire", "ocse", "tools"):
        for dirpath, _dn, fns in os.walk(q(base)):
            for fn in fns:
                if not fn.endswith((".c", ".h", ".S", ".py", ".sh")):
                    continue
                p = os.path.join(dirpath, fn)
                try:
                    text = open(p, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
                for m in inc_re.finditer(text):
                    users.setdefault(os.path.basename(m.group(1)),
                                     set()).add(rel)
    return users


def build_map():
    """-> list[(old_rel, new_rel)] for every tracked file under src/."""
    inc_users = scan_include_users()
    pairs = []
    # ---- L0 -------------------------------------------------------------
    for rel in list_files("src/L0"):
        parts = rel.split(os.sep)           # src L0 <group> [<sub>] file
        group = parts[2]
        fname = parts[-1]
        stem, ext = os.path.splitext(fname)
        if group == "boot":
            if fname in KERNEL_RENAME:
                new = KERNEL_RENAME[fname]
            elif stem in KERNEL_RENAME:
                new = KERNEL_RENAME[stem] + ext
            else:
                new = fname
            pairs.append((rel, "ocos/kernel/" + new))
        elif group == "core":
            sub = parts[3] if len(parts) > 4 else ""
            if sub == "arch":
                # arch/<arch>/<file> -> ocos/kernel/arch/<arch>/<file>
                pairs.append((rel, "ocos/kernel/" + "/".join(parts[3:])))
            elif sub == "generated":
                pairs.append((rel, "ocos/kernel/generated/" + fname))
            elif sub in ("mem", "lib", "console"):
                new = KERNEL_RENAME.get(stem, KERNEL_RENAME.get(fname, fname))
                if not new.endswith(ext) and "." in new:
                    new = new + ext
                elif not new.endswith(ext):
                    new = new + ext
                pairs.append((rel, "ocos/kernel/" + new))
            else:
                # core top level: core_*.c/.h keep their names;
                # userprogs_data.h (generated) keeps its name.
                pairs.append((rel, "ocos/kernel/" + fname))
        elif group == "drivers":
            fam = parts[3]
            if (fam, stem) in DRV_MGR_TO_SERVICE:
                svc, newstem = DRV_MGR_TO_SERVICE[(fam, stem)]
                # newstem is already the final svc_<name>_<role> base
                if ext == ".h":
                    pairs.append((rel, "ocos/services/%s/interface/%s.h"
                                  % (svc, newstem)))
                else:
                    pairs.append((rel, "ocos/services/%s/src/%s%s"
                                  % (svc, newstem, ext)))
            elif (fam, fname) in SVC_HEADER_INTERNAL:
                pass
            elif stem in ("blk_drv_internal", "nic_drv_internal",
                          "snd_drv_internal"):
                # handled via DRV_NAME_SPECIAL for the rename itself
                pairs.append((rel, "ocos/drivers/%s/%s%s"
                              % (DRV_CLASS[fam], DRV_NAME_SPECIAL[(fam, stem)],
                                 ext)))
            else:
                pairs.append((rel, "ocos/drivers/%s/%s%s"
                              % (DRV_CLASS[fam], drv_new_basename(fam, stem),
                                 ext)))
        elif group == "services":
            svc = parts[3]
            if ext == ".h":
                if (svc, stem) in SVC_HEADER_INTERNAL:
                    pairs.append((rel, "ocos/services/%s/src/%s%s"
                                  % (svc, svc_new_basename(svc, stem), ext)))
                else:
                    # Data-driven public/private split: a header is
                    # PUBLIC (interface/) when anything outside its own
                    # service folder includes it (or it is forced
                    # public as the umbrella API), PRIVATE (src/) when
                    # only the owning service (or nothing) does.
                    svc_old = "src/L0/services/%s/" % svc
                    users = inc_users.get(fname, set())
                    public = (svc, fname) in SVC_HEADER_FORCE_PUBLIC \
                        or any(not u.startswith(svc_old) for u in users)
                    if public:
                        pairs.append((rel, "ocos/services/%s/interface/%s%s"
                                      % (svc, svc_new_basename(svc, stem),
                                         ext)))
                    else:
                        pairs.append((rel, "ocos/services/%s/src/%s%s"
                                      % (svc, svc_new_basename(svc, stem),
                                         ext)))
            else:
                pairs.append((rel, "ocos/services/%s/src/%s%s"
                              % (svc, svc_new_basename(svc, stem), ext)))
        else:
            raise SystemExit("unhandled L0 group: " + rel)
    # ---- L1 -------------------------------------------------------------
    for rel in list_files("src/L1"):
        parts = rel.split(os.sep)           # src L1 [<entry>] [<file>]
        entry = parts[2] if len(parts) > 3 else ""
        fname = parts[-1]
        if entry == "_shared":
            pairs.append((rel, "ocos/interface/" + fname))
        elif entry == "_i18n":
            pairs.append((rel, "ocos/l1apps/_i18n/" + "/".join(parts[3:])))
        elif entry:
            pairs.append((rel, "ocos/l1apps/" + "/".join(parts[2:])))
        else:
            pairs.append((rel, "ocos/l1apps/" + fname))
    return pairs


def verify(pairs):
    errs = []
    seen_dst = {}
    seen_base = {}
    for old, new in pairs:
        if new in seen_dst:
            errs.append("duplicate dst: %s <- %s and %s"
                        % (new, old, seen_dst[new]))
        seen_dst[new] = old
        # Basename uniqueness matters only across the multi -I set
        # (every kernel build directory).  l1apps app folders and the
        # non-x86_64 arch shells are never on a shared -I path.
        norm = new.replace(os.sep, "/")
        skip = norm.startswith("ocos/l1apps/") \
            or norm.startswith("ocos/kernel/arch/arm64/") \
            or norm.startswith("ocos/kernel/arch/riscv/")
        if skip:
            continue
        b = os.path.basename(new)
        if b in seen_base:
            errs.append("duplicate dst basename: %s <- %s and %s"
                        % (b, old, seen_base[b]))
        seen_base[b] = old
    return errs


def main():
    args = set(sys.argv[1:])
    pairs = build_map()
    errs = verify(pairs)
    if errs:
        for e in errs:
            print("ERROR:", e, file=sys.stderr)
        sys.exit(1)
    with open(MAP_TSV, "w") as f:
        for old, new in pairs:
            f.write("%s\t%s\n" % (old, new))
    print("nak_migrate: %d files mapped -> %s" % (len(pairs), MAP_TSV))

    if "--apply" in args:
        import subprocess
        n = 0
        for old, new in pairs:
            dst = q(new)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            r = subprocess.run(["git", "mv", old, new], cwd=ROOT)
            if r.returncode != 0:
                print("git mv FAILED: %s -> %s" % (old, new), file=sys.stderr)
                sys.exit(1)
            n += 1
        print("nak_migrate: %d git mv done" % n)

    if "--rewrite" in args:
        # basename map for include rewriting
        bmap = {}
        for old, new in pairs:
            ob = os.path.basename(old)
            nb = os.path.basename(new)
            if ob != nb:
                bmap[ob] = nb
        # also the _shared -> interface flattening for L1 sources
        # (#include "x.h" -> #include "x.h")
        import re
        targets = []
        for base in ("ocos", "tests", "ocire", "ocse", "ocbl", "tools"):
            for dirpath, _dn, fns in os.walk(q(base)):
                if os.sep + "node_modules" in dirpath:
                    continue
                for fn in fns:
                    if fn.endswith((".c", ".h", ".S", ".ld", ".py", ".sh",
                                    ".mk", ".asm")):
                        targets.append(os.path.join(dirpath, fn))
        inc_re = re.compile(r'#include\s+"([^"]+)"')
        changed = 0
        for path in targets:
            try:
                src = open(path, encoding="utf-8").read()
            except (UnicodeDecodeError, IsADirectoryError):
                continue
            out = src
            def repl(m):
                inc = m.group(1)
                # _shared flattening
                if inc.startswith("_shared/"):
                    inc = inc[len("_shared/"):]
                # basename rename
                b = os.path.basename(inc)
                if b in bmap:
                    inc = bmap[b]
                return '#include "%s"' % inc
            out2 = inc_re.sub(repl, out)
            if out2 != out:
                open(path, "w", encoding="utf-8").write(out2)
                changed += 1
        print("nak_migrate: include rewrite touched %d files" % changed)


if __name__ == "__main__":
    main()
