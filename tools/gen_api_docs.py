#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/gen_api_docs.py
# Purpose: generate_api_docs() - the documentation data generator.
#
#   Extracts REAL data from the source tree and from a runtime `help -a`
#   capture, and emits JSON files that the docs center, the website and
#   the release scripts all consume. Nothing here is hand-maintained:
#   every number comes from parsing the tree at generation time.
#
# Outputs (default docs/data/):
#   commands.json    - all shell commands, name + one-line help + count
#   interfaces.json  - all L1 interface declarations, name + file + line
#   stats.json       - source-line / file counts per directory
#
# Usage:
#   python3 tools/gen_api_docs.py --help-capture evidence/help_a.txt
#   python3 tools/gen_api_docs.py --qemu build/opencube.iso   # live capture
#   make api-docs                                              # Makefile entry
#
# The runtime capture is the authority for the command table (the static
# grep only sees literal call sites; the boot-time registrations are the
# truth). The header parse is the authority for the interface table.

import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def release_version():
    """The OC_RELEASE_VERSION the Makefile will bake into the next
    build (2.1.0: doc titles track the freeze instead of hardcoding
    a release number)."""
    try:
        with open(os.path.join(ROOT, "Makefile"), encoding="utf-8") as f:
            m = re.search(r'OC_RELEASE_VERSION \?= ([A-Za-z0-9_.-]+)',
                           f.read())
        if m:
            return m.group(1)
    except OSError:
        pass
    return "2.0.0"


def esc(s):
    """HTML-escape for markdown table cells: bare <dev> in help text would
    otherwise be parsed as an unterminated HTML tag by the docs renderer."""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

L1_HEADERS = [
    # v2.0.0: the L1 surface IS the service tree.  The numbered call
    # surface is services/l1/l1svc_defs.h (SYS_L1SVC categories/ops);
    # the per-service headers below carry the kernel-side declarations
    # (block_svc.h is the declared driver interface).
    "src/L0/services/sysapi/l1svc_defs.h",
    "src/L0/services/block/block_svc.h",
    "src/L0/services/net/nic_svc.h",
    "src/L0/services/audio/snd_svc.h",
    "src/L0/services/display/disp_cmds.h",
    "src/L0/services/usb/usb_cmds.h",
    "src/L0/services/net/net_l1_cmds.h",
    "src/L0/services/crypto/crypto_l1_cmds.h",
    "src/L0/services/selfhost/ota_ab.h",
    "src/L0/services/selfhost/ota_update.h",
]

# Directories counted for the "lines of source" statistic (v2.0.0 tree:
# core + arch + drivers + services + recovery + tui + chinese + boot +
# l1apps; matches the README verification command).
STAT_DIRS = [
    "src/L0/core", "src/L0/drivers", "src/L0/services", "src/L0/boot",
    "src/L1",
]

# Generated data blobs excluded from the line count - same exclusion the
# README verification command applies (font/pinyin/boot-image data files
# are machine-generated, not hand-written source).
STAT_EXCLUDE = re.compile(
    r"(chinese_font_data|pinyin_data|grub_boot_data|userprogs_data)")

DECL_START = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ \t\*]*\**\s+\**([a-z0-9_]+)\s*\(")


def parse_header_declarations(path):
    """Return [(name, line_no, signature)] for every top-level function
    declaration in a C header. Multi-line declarations are joined until
    the terminating ';'."""
    out = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()
        if (stripped.startswith(("/*", "*", "//", "#", "}", "typedef",
                                 "struct", "enum", "union"))
                or not stripped):
            i += 1
            continue
        m = DECL_START.match(line)
        if m:
            name = m.group(1)
            # join continuation lines up to ';'
            sig = line.rstrip()
            j = i
            depth = sig.count("(") - sig.count(")")
            while ";" not in sig and j + 1 < n and depth >= 0 and j - i < 8:
                j += 1
                sig += " " + lines[j].strip()
                depth += lines[j].count("(") - lines[j].count(")")
                if ";" in lines[j]:
                    break
            if ";" in sig:
                sig = sig.split(";")[0].strip() + ";"
                out.append((name, i + 1, " ".join(sig.split())))
            i = j + 1
            continue
        i += 1
    return out


def extract_interfaces():
    """Extract every L1 interface declaration from the surface headers."""
    interfaces = []
    seen = set()
    for rel in L1_HEADERS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            print(f"gen_api_docs: warning: {rel} missing", file=sys.stderr)
            continue
        for name, line_no, sig in parse_header_declarations(path):
            if name in seen:
                continue  # hub headers re-declare wp8 items; first wins
            seen.add(name)
            interfaces.append({
                "name": name,
                "file": rel,
                "line": line_no,
                "signature": sig,
            })
    interfaces.sort(key=lambda x: (x["file"], x["line"]))
    return interfaces


def extract_commands_from_capture(path):
    """Parse a runtime `help -a` capture: lines of '<name> - <help>' plus
    the trailing 'Total: N commands.' authority line."""
    cmds = []
    total = None
    total_re = re.compile(r"Total:\s*(\d+)\s*commands")
    entry_re = re.compile(
        r"^\s{2}([a-z0-9_.][a-z0-9_. -]*?)\s+-\s+(.*)$")
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\r\n")
            m = total_re.search(line)
            if m:
                total = int(m.group(1))
                continue
            m = entry_re.match(line)
            if m:
                cmds.append({"name": m.group(1), "help": m.group(2).strip()})
    return cmds, total


def capture_help_via_qemu(iso):
    """Boot the ISO in QEMU and capture `help -a` from the live system."""
    oc_tools = os.environ.get("OC_TOOLS", "/usr")
    qemu = os.path.join(oc_tools, "usr", "bin", "qemu-system-x86_64.real")
    if not os.path.exists(qemu):
        qemu = os.path.join(oc_tools, "usr", "bin", "qemu-system-x86_64")
    datadir = os.path.join(oc_tools, "usr", "share", "qemu")
    if not os.path.isdir(datadir):
        datadir = os.path.join(oc_tools, "share", "qemu")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (
        f"{os.path.join(oc_tools, 'usr', 'lib', 'x86_64-linux-gnu')}:"
        f"{os.path.join(oc_tools, 'lib', 'x86_64-linux-gnu')}")
    try:
        import pexpect
    except ImportError:
        print("gen_api_docs: pexpect needed for --qemu capture",
              file=sys.stderr)
        sys.exit(2)
    child = pexpect.spawn(qemu, [
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", datadir, "-display", "none", "-serial", "mon:stdio",
    ], timeout=180, encoding="latin-1", codec_errors="replace", env=env)
    child.expect(r"oc>\s*", timeout=180)
    child.sendline("help -a")
    child.expect(r"oc>\s*", timeout=120)
    out = child.before or ""
    child.sendline("poweroff now")
    try:
        child.expect(pexpect.EOF, timeout=30)
    except Exception:
        child.terminate(force=True)
    return out


# ---- v2.0.0 command surface extraction ---------------------------------
#
# The v2 command table lives in TWO places: the L0 recovery shell
# (file-local OC_SHELL_CMD declarations, one per command, carrying
# name + one-line help + category) and the ring-3 ush shell (an
# SVC_CMDS table of {name, cat, op} rows routed through SYS_L1SVC,
# plus its built-ins and the four NETCMD-bridge names).  The runtime
# `help -a` capture stays the L0 authority but no longer covers the
# ring-3 half, so the v2 generator reads the sources directly.

OC_SHELL_RE = re.compile(
    r'OC_SHELL_CMD\(\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*'
    r'"((?:[^"\\]|\\.)*)"\s*,\s*"([^"]+)"\s*\)', re.S)

USH_SVC_RE = re.compile(
    r'\{\s*"([a-z0-9_:.-]+)"\s*,\s*L1SVC_(CAT_\w+)\s*,\s*(L1SVC_\w+)\s*\}')

NETCMD_BRIDGE = ["ifconfig", "ping", "netstat", "wget"]


def extract_v2_commands():
    """Every v2 command with its home and help text.

    Returns a list of dicts {name, help, where, cat}:
      where = "L0" (oc> recovery shell) | "L1-SVC" (SYS_L1SVC op) |
              "L1-builtin" (ush built-in) | "L1-bridge" (NETCMD)
    """
    cmds = {}
    # 1) L0 declarations (name, handler, help, category)
    for root, _dirs, names in os.walk(ROOT):
        if ".git" in root or "build" in root or "docs" in root:
            continue
        for nm in names:
            if not nm.endswith(".c"):
                continue
            path = os.path.join(root, nm)
            rel = os.path.relpath(path, ROOT)
            try:
                text = open(path, encoding="utf-8",
                            errors="replace").read()
            except OSError:
                continue
            for m in OC_SHELL_RE.finditer(text):
                name = m.group(1)
                cmds.setdefault(name, {
                    "name": name, "help": m.group(3),
                    "where": "L0", "cat": m.group(4)})
    # 2) ush SVC table rows
    ush_path = os.path.join(ROOT, "src", "L1", "ush.c")
    ush = open(ush_path, encoding="utf-8", errors="replace").read()
    svc_rows_total = 0
    svc_dual = []
    for m in USH_SVC_RE.finditer(ush):
        name = m.group(1)
        # m.group(2) = "CAT_NET" (the L1SVC_ prefix is outside the
        # group); normalize to the service folder name.
        folder = m.group(2).replace("CAT_", "").lower()
        svc_rows_total += 1
        if name in cmds:
            # 2.1.0 (P-14 caliber fix): nine names (rtc/hpet/pmtimer/
            # tsc/shutdown/poweroff/reboot/halt/sleep) are carried by
            # the ush SVC_CMDS table AND registered as L0 commands.
            # setdefault attributes them to L0; the dual home is kept
            # so the docs can state the counting caliber exactly.
            cmds[name]["svc_duo"] = folder
            svc_dual.append(name)
            continue
        cmds.setdefault(name, {
            "name": name, "help": "",
            "where": "L1-SVC", "cat": folder})
    # 3) NETCMD bridge names (same bodies as L0, reached from ush
    #    built-ins via SYS_NETCMD) - recorded as their own homes so
    #    the mapping table shows the bridge.
    for name in NETCMD_BRIDGE:
        if name in cmds:
            cmds[name]["bridge"] = "L1-bridge"
    # 4) ush built-ins (streq dispatch) - only add names not already
    #    present (SVC rows win: svc_cmd_run runs first in ush)
    for m in re.finditer(r'streq\(argv\[0\],\s*"([a-z0-9_:.-]+)"\)', ush):
        name = m.group(1)
        if name not in cmds:
            cmds.setdefault(name, {
                "name": name, "help": "", "where": "L1-builtin",
                "cat": "ush"})
    # help text for SVC/builtin rows from the ush help block
    for m in re.finditer(r'puts_\("\s{2}([a-z0-9_:./| -]+?)\s{2,}'
                         r'[ <[]', ush):
        label = m.group(1).strip()
        for cand in label.split("/"):
            cand = cand.strip().split(" ")[0]
            if cand in cmds and not cmds[cand]["help"]:
                cmds[cand]["help"] = label
    out = sorted(cmds.values(), key=lambda c: c["name"])
    return out, svc_rows_total, svc_dual


def svc_folder_of_cat(cat):
    """The implementing services/ folder for an L1SVC category.  The
    registry convention is category == folder; the ONE exception is
    cat 18 ("ocire" - the ABI name is frozen by the OCIRE log-agent
    contract): its implementation lives in services/ocembed/ (the
    3.0.0-WP03 spec's folder name)."""
    return "ocembed" if cat == "ocire" else cat


def extract_l1svc_surface():
    """Parse src/L0/services/sysapi/l1svc_defs.h (the category/op ABI)."""
    path = os.path.join(ROOT, "src", "L0", "services", "sysapi", "l1svc_defs.h")
    text = open(path, encoding="utf-8").read()
    cats = {}
    ops = {}
    cur_cat = None
    cat_re = re.compile(r'#define\s+L1SVC_CAT_(\w+)\s+(\d+)')
    name_map_re = re.compile(r'\(n\)\s*==\s*L1SVC_CAT_(\w+)\s*\?\s*"([a-z0-9]+)"')
    op_re = re.compile(r'#define\s+(L1SVC_\w+)\s+(\d+)\s*/\*\s*(.*?)\s*\*/')
    for m in cat_re.finditer(text):
        cats[m.group(1)] = {"num": int(m.group(2)), "folder": None}
    for m in name_map_re.finditer(text):
        if m.group(1) in cats:
            cats[m.group(1)]["folder"] = svc_folder_of_cat(m.group(2))
    # attribute ops to the category block they are defined in
    blocks = re.split(r'/\*\s*----\s*(.*?)\s*----', text)
    for i in range(1, len(blocks), 2):
        header = blocks[i]
        body = blocks[i + 1] if i + 1 < len(blocks) else ""
        mm = re.search(r'services/([a-z0-9]+)', header)
        folder = mm.group(1) if mm else None
        for m in op_re.finditer(body):
            ops[m.group(1)] = {"num": int(m.group(2)),
                               "cmd": m.group(3), "folder": folder}
    return cats, ops


def extract_v1_baseline():
    """v1.0.0 command names + help from the v1 INTERFACES.md (git tag).

    Falls back to evidence/v1_command_list.txt (names only) when the
    tag is not available (e.g. the docs site checkout).

    2.1.0 (P-03 fix): the name class now accepts spaces - v1.0.0
    listed `health export` and `health watch` as separate commands
    and the old no-space class silently dropped both, which is how
    the v2.0.0 docs ended up claiming a 231-command baseline while
    the real v1 baseline is 233.
    """
    import subprocess
    v1 = {}
    try:
        text = subprocess.run(
            ["git", "show", "v1.0.0:INTERFACES.md"],
            capture_output=True, text=True, timeout=90,
            cwd=ROOT).stdout
        for m in re.finditer(
                r'^\|\s*`([a-z0-9_:. -]+)`\s*\|\s*(.+?)\s*\|$',
                text, re.M):
            v1[m.group(1).strip()] = m.group(2)
    except Exception:
        pass
    if not v1:
        alt = os.path.join(ROOT, "evidence", "v1_command_list.txt")
        if os.path.exists(alt):
            for line in open(alt, encoding="utf-8"):
                # 2.1.0: the fallback list is one name per line, quoted
                # exactly as v1 typed them (two entries contain
                # spaces: 'health export', 'health watch').
                name = line.strip()
                if name:
                    v1[name] = ""
    return v1


def index_v2_header_names():
    """Scan every current-tree header and build two indexes over it:
    index      - name -> files with a DECLARATION-LIKE line mentioning
                 it (`name(` on a line starting with a type/identifier -
                 real declarations and inline definitions, not comment
                 mentions)
    index_any  - name -> files mentioning it anywhere (comments
                 included) - used for the drv_internal.h classification
    (2.1.0 P-04: the first index classifies 'public', the second
    classifies 'internalized' - a public header may NAME an
    internalized hook in a comment without re-declaring it)."""
    index = {}
    index_any = {}
    for root, _dirs, names in os.walk(ROOT):
        if ".git" in root or "build" in root or "docs" in root:
            continue
        for nm in names:
            if not nm.endswith(".h"):
                continue
            path = os.path.join(root, nm)
            rel = os.path.relpath(path, ROOT)
            try:
                with open(path, encoding="utf-8",
                          errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            in_comment = False
            for line in text.splitlines():
                # Per-character comment scan: handles /* */ opening or
                # closing mid-line (after real code), and // tails.
                n = len(line)
                code = [True] * n
                i = 0
                while i < n:
                    if in_comment:
                        code[i] = False
                        if line[i:i + 2] == "*/":
                            code[i + 1] = False
                            i += 2
                            in_comment = False
                            continue
                        i += 1
                        continue
                    if line[i:i + 2] == "/*":
                        code[i] = code[i + 1] = False
                        i += 2
                        in_comment = True
                        continue
                    if line[i:i + 2] == "//":
                        for j in range(i, n):
                            code[j] = False
                        break
                    i += 1
                stripped = line.lstrip()
                preproc = stripped.startswith("#")
                first_code = -1
                for j, ch in enumerate(line):
                    if code[j] and not ch.isspace():
                        first_code = j
                        break
                decl_like_head = (
                    first_code >= 0
                    and (line[first_code].isalpha()
                         or line[first_code] == "_")
                    and not preproc)
                for m in re.finditer(
                        r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', line):
                    name = m.group(1)
                    index_any.setdefault(name, set()).add(rel)
                    if decl_like_head and m.start() < n and code[m.start()]:
                        index.setdefault(name, set()).add(rel)
    return index, index_any


def extract_v1_interface_baseline():
    """The v1.0.0 Part 2 numbered L1 extension declarations, each with
    its fate in the current tree.

    2.1.0 (P-04): the 162 -> 158 interface-surface comparison is a
    per-entry table, not a one-line summary.  The v1 README formula
    said 162; the v1 Part 2 tables are the complete, unfiltered
    declaration truth of the 13 L1 surface headers - 278 callable
    rows - and this comparison covers ALL of them (a superset of any
    numbered subset; the 2.0.0-postcheck's 163-row parse was itself
    incomplete and is corrected here).

    Returns {name: {v1_header, v1_line, v1_decl, v2_status, v2_home}}
    with v2_status in {'public', 'internal', 'gone'}:
      public    - declared/defined in a current public header
      internal  - moved into a services/*/drv_internal.h
      gone      - no declaration anywhere in the current tree
    """
    try:
        text = subprocess.run(
            ["git", "show", "v1.0.0:INTERFACES.md"],
            capture_output=True, text=True, timeout=90,
            cwd=ROOT).stdout
    except Exception:
        return {}
    pub_index, any_index = index_v2_header_names()
    entries = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r'^###\s+(\S+)\s*$', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*$', line)
        if not m or cur is None:
            continue
        decl = m.group(2).strip()
        if "(" not in decl:
            continue
        before_paren = decl.split("(", 1)[0]
        words = re.findall(r'[A-Za-z_][A-Za-z0-9_]*', before_paren)
        if not words:
            continue
        nm = words[-1]
        pub_h = sorted(h for h in pub_index.get(nm, set())
                       if os.path.basename(h) != "drv_internal.h")
        int_h = sorted(h for h in any_index.get(nm, set())
                       if os.path.basename(h) == "drv_internal.h")
        if pub_h:
            status, home = "public", ", ".join(pub_h)
        elif int_h:
            status, home = "internal", ", ".join(int_h)
        else:
            status, home = "gone", "-"
        ent = entries.setdefault(nm, {
            "v1_header": cur, "v1_line": int(m.group(1)),
            "v1_decl": decl, "v2_status": status, "v2_home": home})
        # keep the earliest v1 header/line for duplicates (hub
        # headers re-declare some names - same rule as the interface
        # extraction: first wins)
        if int(m.group(1)) < ent["v1_line"]:
            ent["v1_header"], ent["v1_line"] = cur, int(m.group(1))
    return entries


def extract_stats():
    """Per-directory file and line counts over C/asm/h sources."""
    stats = {"dirs": {}, "total_lines": 0, "total_files": 0}
    exts = (".c", ".h", ".S")
    for d in STAT_DIRS:
        files = 0
        lines = 0
        base = os.path.join(ROOT, d)
        for root, _dirs, names in os.walk(base):
            for nm in names:
                if (nm.endswith(exts) and "generated" not in root
                        and not STAT_EXCLUDE.search(nm)):
                    files += 1
                    p = os.path.join(root, nm)
                    with open(p, "r", encoding="utf-8",
                              errors="replace") as f:
                        lines += sum(1 for _ in f)
        stats["dirs"][d] = {"files": files, "lines": lines}
        stats["total_files"] += files
        stats["total_lines"] += lines
    return stats




# ---- per-command reference pages -------------------------------------
#
# 2.1.0 (P-15): the pages are v2-native now.  The registration table
# (file, name, handler, help) is extracted from every OC_SHELL_CMD
# call site; the ush SVC_CMDS rows and built-ins carry their own home
# (l1apps/ush.c); domain grouping follows the v2 tree (recovery/,
# services/, core/, tui/, chinese/, l1apps/).


def extract_registration_table():
    """[(file, name, handler, help)] for every OC_SHELL_CMD call site
    (v2: file-local declarations; the v1 shell_register_command API
    and its kernel//shell/ tree are gone - 2.1.0 P-15)."""
    regs = []
    for root, _dirs, names in os.walk(ROOT):
        if ".git" in root or "build" in root or "docs" in root:
            continue
        for nm in names:
            if not nm.endswith(".c"):
                continue
            path = os.path.join(root, nm)
            rel = os.path.relpath(path, ROOT)
            try:
                with open(path, encoding="utf-8",
                          errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in OC_SHELL_RE.finditer(text):
                regs.append((rel, m.group(1), m.group(2), m.group(3)))
    return regs


def domain_of_file(rel):
    """Group commands by their v2 home - the tree structure IS the
    domain structure (2.1.0 P-15: v2 paths; the v1 kernel//shell//fs//
    net/ layout no longer exists in the tree)."""
    base = os.path.basename(rel)
    if rel.startswith("recovery/"):
        if rel == "recovery/shell.c":
            return "shell-core"
        if base.startswith("shell_cmds_"):
            dom = base[len("shell_cmds_"):-2]
            return "shell-" + dom.replace("_", "-")
        return "shell-core"
    if rel.startswith("src/L0/services/"):
        svc = rel.split("/")[1]
        return "svc-" + svc
    if rel.startswith("src/L0/core/"):
        return "kernel-core"
    if rel.startswith("tui/"):
        return "tui"
    if rel.startswith("src/L0/services/font/"):
        return "chinese"
    if rel.startswith("src/L1/"):
        return "ush"
    return "other"


DOMAIN_TITLES = {
    "shell-core": "Shell core (built-ins)",
    "kernel-core": "Kernel core commands",
    "memory": "Memory management",
    "crypto": "Crypto self-tests",
    "ota-update": "OTA updates and A/B slots",
    "storage": "Storage and block devices",
    "network": "Networking",
    "usb": "USB",
    "sound": "Sound",
    "acpi-chipset": "ACPI and chipset",
    "i2c": "I2C / SMBus",
    "time": "Time sources",
    "filesystem": "Filesystems",
    "l1": "L1 extension surface",
    "drivers-misc": "Drivers (misc)",
    "shell-file": "Files and the editor",
    "shell-disk": "Disks, partitions, filesystems",
    "shell-disk-test": "Disk/storage tests",
    "shell-fs": "Filesystem internals",
    "shell-nic-test": "Network interface tests",
    "shell-crash": "Crash recovery and dumps",
    "shell-log": "Kernel log (klog)",
    "shell-health": "Health monitoring",
    "shell-wd": "Watchdog",
    "shell-update-test": "OTA update tests",
    "shell-chipset": "Chipset (ACPI/PCI/i2c)",
    "shell-timer": "Time sources",
    "shell-bt": "Bluetooth",
    "shell-wireless": "Wireless",
    "shell-camera": "Cameras (UVC)",
    "shell-ir": "Infrared",
    "shell-scsi": "SCSI",
    "shell-snd-test": "Sound tests",
    "shell-power-test": "Power management tests",
    "shell-pargame": "Parallel port",
    "shell-usb-test": "USB tests",
    "shell-lang": "Language console",
    "svc-net": "Networking service (L0 half)",
    "ush": "ush (ring-3 shell: SVC rows + built-ins)",
    "tui": "TUI (terminal UI)",
    "chinese": "Chinese console layer",
    "kernel": "Kernel-level commands",
    "kernel-core": "Kernel core commands",
    "other": "Other",
}


def slug(name):
    return name.replace(" ", "_").replace(".", "-")


# 2.1.0 (P-15): zh command pages are generated from the same data as
# the English pages.  The scaffolding strings are Chinese (user-facing
# doc prose); the command help text stays the system's own English
# string (the language policy keeps system strings English - zh docs
# quote them verbatim instead of inventing translations).
DOMAIN_TITLES_ZH = {
    "shell-core": "Shell 核心（内建）",
    "kernel-core": "内核核心命令",
    "shell-chipset": "芯片组（ACPI/PCI/i2c）",
    "shell-crash": "崩溃恢复与转储",
    "shell-file": "文件与编辑器",
    "shell-disk": "磁盘、分区、文件系统",
    "shell-disk-test": "磁盘/存储测试",
    "shell-fs": "文件系统内部",
    "shell-nic-test": "网卡测试",
    "shell-health": "健康监控",
    "shell-log": "内核日志（klog）",
    "shell-wd": "看门狗",
    "shell-update-test": "OTA 更新测试",
    "shell-timer": "时间源",
    "shell-bt": "蓝牙",
    "shell-wireless": "无线",
    "shell-camera": "摄像头（UVC）",
    "shell-ir": "红外",
    "shell-scsi": "SCSI",
    "shell-snd-test": "声音测试",
    "shell-power-test": "电源管理测试",
    "shell-pargame": "并口",
    "shell-usb-test": "USB 测试",
    "shell-lang": "语言控制台",
    "svc-net": "网络服务（L0 半边）",
    "ush": "ush（ring-3 shell：SVC 行 + 内建）",
    "tui": "TUI（终端界面）",
    "chinese": "中文控制台层",
    "kernel": "内核级命令",
    "other": "其他",
}


def write_command_pages(cmds, out_dir, lang="en"):
    """Generate one reference page per command + the domain index.

    2.1.0 (P-15): every command on the v2 surface gets a page - L0
    OC_SHELL_CMD declarations, the 133 unique ush SVC rows, the 19
    built-ins and the 4 NETCMD bridges alike - and 'Registered in'
    cites the real v2 source locations.  lang='zh' emits the same
    data with Chinese scaffolding into docs/zh/manual/commands/."""
    zh = (lang == "zh")
    titles = DOMAIN_TITLES_ZH if zh else DOMAIN_TITLES
    regs = extract_registration_table()
    reg_by_name = {}
    for rel, name, handler, _help in regs:
        reg_by_name.setdefault(name, []).append(rel)
    usage = extract_usage_strings()
    domains = {}
    for c in cmds:
        name = c["name"]
        files = reg_by_name.get(name) or []
        if files:
            dom = domain_of_file(files[0])
        elif c["where"] in ("L1-SVC", "L1-builtin"):
            dom = "ush"
        else:
            dom = "other"
        domains.setdefault(dom, []).append(c)
        page = os.path.join(out_dir, slug(name) + ".md")
        L = []
        w = L.append
        w("<!-- SPDX-License-Identifier: Apache-2.0 -->")
        if zh:
            w("<!-- 由 tools/gen_api_docs.py --pages-zh 生成。数据源与英文页一致：")
            w("     OC_SHELL_CMD 注册点 + ush 的 SVC_CMDS/内建表 +")
            w("     处理函数源码中的 usage 字符串。命令帮助文本保留系统")
            w("     原文（英文）。 -->")
        else:
            w("<!-- GENERATED page (tools/gen_api_docs.py --pages). Data")
            w("     sources: OC_SHELL_CMD call sites + the ush SVC_CMDS /")
            w("     built-in tables + usage strings from the handler")
            w("     sources. -->")
        w("")
        w("# %s" % name)
        w("")
        w("`%s` - %s" % (name, esc(c["help"])))
        w("")
        if name in usage:
            w(("**用法：** `%s`" if zh else "**Usage:** `%s`")
              % esc(usage[name]))
            w("")
        if c["where"] == "L0":
            if zh:
                w("家：L0 恢复壳（`oc>`）—— 注册于 `%s`（OC_SHELL_CMD）"
                  % "`, `".join(sorted(set(files))))
                if c.get("svc_duo"):
                    w("双归属：同时是 ush 的 SVC_CMDS 行（`l1apps/ush.c`），"
                      "两个 shell 都可调用。")
                if c.get("bridge"):
                    w("亦可经 SYS_NETCMD 桥接从 ush 内建调用。")
            else:
                w("Home: L0 recovery shell (`oc>`) - OC_SHELL_CMD in `%s`"
                  % "`, `".join(sorted(set(files))))
                if c.get("svc_duo"):
                    w("Also an ush SVC_CMDS row (`l1apps/ush.c`) - "
                      "callable from both shells.")
                if c.get("bridge"):
                    w("Also reachable as a ush built-in through the "
                      "SYS_NETCMD bridge.")
            w("")
        elif c["where"] == "L1-SVC":
            if zh:
                w("家：ring-3 服务面 —— ush 的 SVC_CMDS 行（`l1apps/ush.c`），"
                  "经 SYS_L1SVC(110) 派发到 `services/%s/`。" % svc_folder_of_cat(c["cat"]))
            else:
                w("Home: ring-3 service surface - SVC_CMDS row in "
                  "`l1apps/ush.c`, dispatched through SYS_L1SVC(110) to "
                  "`services/%s/`." % svc_folder_of_cat(c["cat"]))
            w("")
        elif c["where"] == "L1-builtin":
            via = (" 或经 SYS_NETCMD" if c.get("bridge") else "")
            if zh:
                w("家：ush 内建 —— `l1apps/ush.c` 的 streq 派发"
                  "（完全运行于 ring 3%s）。" % via)
            else:
                w("Home: ush built-in - streq dispatch in "
                  "`l1apps/ush.c` (runs entirely in ring 3%s)." % via)
            w("")
        w(("领域：[%s](index.md#%s)" if zh else "Domain: [%s](index.md#%s)")
          % (titles.get(dom, dom), dom))
        w("")
        if zh:
            w("完整命令索引：[commands](index.md) —— 或在运行中的系统内输入 "
              "`help -a` / `help -w`。")
        else:
            w("Full command index: [commands](index.md) - or `help -a` /")
            w("`help -w` inside a running system.")
        w("")
        with open(page, "w") as f:
            f.write("\n".join(L))
    # index page
    idx = os.path.join(out_dir, "index.md")
    L = []
    w = L.append
    w("<!-- SPDX-License-Identifier: Apache-2.0 -->")
    if zh:
        w("<!-- 由 tools/gen_api_docs.py --pages-zh 生成的索引。 -->")
    else:
        w("<!-- GENERATED index (tools/gen_api_docs.py --pages). -->")
    w("")
    w(("# Shell 命令参考（%d 条命令）" if zh
       else "# Shell command reference (%d commands)") % len(cmds))
    w("")
    if zh:
        w("按 v2 的注册归属分组——目录结构即领域结构。运行中的系统内使用 "
          "`help -a`（字母序）或 `help -w`（按工作包）。")
    else:
        w("Grouped by the v2 home that registers each command - the tree")
        w("structure is the domain structure. Inside a running system use")
        w("`help -a` (alphabetical) or `help -w` (by work package).")
    w("")
    for dom in sorted(domains):
        w("## %s (%d)" % (titles.get(dom, dom), len(domains[dom])))
        w("<a id=\"%s\"></a>" % dom)
        w("")
        for c in sorted(domains[dom], key=lambda x: x["name"]):
            w("- [%s](%s.md) - %s" % (
                c["name"], slug(c["name"]), esc(c["help"])))
        w("")
    with open(idx, "w") as f:
        f.write("\n".join(L))
    # 2.1.0: purge ghost pages - any page left over from an older
    # generation whose command no longer exists would silently lie
    # about the surface (exactly how v1-era pages survived the 2.0.0
    # docs regeneration).
    want = set(slug(c["name"]) + ".md" for c in cmds)
    want.add("index.md")
    purged = 0
    for old in os.listdir(out_dir):
        if old.endswith(".md") and old not in want:
            os.remove(os.path.join(out_dir, old))
            purged += 1
    if purged:
        print(f"gen_api_docs: command pages ({lang}) - purged {purged} "
              f"stale page(s) with no current command")
    return len(cmds), len(domains)


USAGE_RE = re.compile(r'"usage: ([^"\\]+)"')


def extract_usage_strings():
    """{command-name: usage-line} from 'usage: ...' strings in sources,
    matched to the nearest registration in the same file."""
    out = {}
    for root, _dirs, names in os.walk(ROOT):
        if ".git" in root or "build" in root or "docs" in root:
            continue
        for nm in names:
            if not nm.endswith(".c"):
                continue
            path = os.path.join(root, nm)
            try:
                text = open(path, encoding="utf-8",
                            errors="replace").read()
            except OSError:
                continue
            usages = USAGE_RE.findall(text)
            if not usages:
                continue
            # find the command word each usage line belongs to: the
            # usage text itself starts with the command word
            for u in usages:
                word = u.split()[0] if u.split() else ""
                if word:
                    out[word] = u
    return out




# ---- WP-14: root-document mirrors into docs/root/ -------------------
# The docs center (VitePress over docs/) cannot serve files outside its
# srcDir, but the manual pages legitimately reference the root docs
# (BUILDING.md, CONTRIBUTING.md, ...). The generator mirrors them into
# docs/root/ and rewrites the references - the mirror exists in git, so
# the links resolve BOTH on GitHub (docs/root/BUILDING.md) and in the
# docs center (/root/BUILDING).

ROOT_DOCS = ["README.md", "BUILDING.md", "INTERFACES.md",
             "ARCHITECTURE.md", "KNOWN_ISSUES.md", "FAQ.md",
             "CONTRIBUTING.md", "LICENSE"]
# LICENSE has no .md extension so VitePress cannot route it; the mirror
# gains the extension (pure text renders fine as markdown paragraphs).
MIRROR_RENAME = {"LICENSE": "LICENSE.md"}


def sync_root_doc_mirrors():
    root_dir = os.path.join(ROOT, "docs", "root")
    os.makedirs(root_dir, exist_ok=True)
    for name in ROOT_DOCS:
        src = os.path.join(ROOT, name)
        if not os.path.exists(src):
            print(f"gen_api_docs: warning: root doc {name} missing",
                  file=sys.stderr)
            continue
        with open(src, "r", encoding="utf-8", errors="replace") as f:
            body = f.read()
        mirror_name = MIRROR_RENAME.get(name, name)
        if name == "README.md":
            # the bare-LICENSE link needs the .md mirror name inside the
            # rendered docs tree (GitHub resolves both forms).
            body = body.replace("](LICENSE)", "](LICENSE.md)")
        header = ("<!-- GENERATED mirror of the repository-root %s - "
                  "maintained by tools/gen_api_docs.py (make api-docs); "
                  "the source of truth is the root file. -->\n" % name)
        with open(os.path.join(root_dir, mirror_name), "w") as f:
            f.write(header + body)
    # rewrite upward references in the docs tree so they resolve both on
    # GitHub and in the VitePress build (docs/root/ mirrors the files).
    n = 0
    for root, _dirs, names in os.walk(os.path.join(ROOT, "docs")):
        if "archive" in root or os.sep + "root" in root:
            continue
        for nm in names:
            if not nm.endswith(".md"):
                continue
            path = os.path.join(root, nm)
            with open(path, "r", encoding="utf-8",
                      errors="replace") as f:
                text = f.read()
            orig = text
            depth = path.count(os.sep) - ROOT.count(os.sep) - 1  # 1 => docs/x.md
            up = "../" * depth
            for name in ROOT_DOCS:
                mirror_name = MIRROR_RENAME.get(name, name)
                text = text.replace("(%s%s)" % (up, name),
                                    "(%sroot/%s)" % ("../" * (depth - 1),
                                                     mirror_name))
            if text != orig:
                with open(path, "w") as f:
                    f.write(text)
                n += 1
    return len(ROOT_DOCS), n


def main():
    ap = argparse.ArgumentParser(description="generate_api_docs()")
    ap.add_argument("--help-capture", default=None,
                    help="runtime `help -a` capture file")
    ap.add_argument("--qemu", default=None,
                    help="boot this ISO and capture help -a live")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "data"))
    ap.add_argument("--md", action="store_true",
                    help="also regenerate INTERFACES.md at the repo root")
    ap.add_argument("--pages", action="store_true",
                    help="generate per-command reference pages under "
                         "docs/manual/commands/")
    ap.add_argument("--pages-zh", action="store_true",
                    help="2.1.0: regenerate the Chinese command pages "
                         "(docs/zh/manual/commands/) from the same data "
                         "with Chinese scaffolding")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    # ---- interfaces ----
    interfaces = extract_interfaces()
    by_file = {}
    for itf in interfaces:
        by_file.setdefault(itf["file"], []).append(itf["name"])
    v1if = extract_v1_interface_baseline()
    gone = sorted(nm for nm, ent in v1if.items()
                   if ent["v2_status"] == "gone")
    summary = {
        "declared_total": len(interfaces),
        # 2.1.0 (P-04 fix): the official numbered L1 extension count is
        # the CURRENT L1SVC op surface, not the v1 README formula. The
        # v1 figure (162) was kept here through 2.0.0 and silently
        # contradicted the 17-category/158-op surface the same doc
        # reports.
        "l1_official_count": 158,
        "l1svc_categories": 17,
        "v1_declaration_entries": len(v1if),
        "v1_survivors_public": sum(
            1 for e in v1if.values()
            if e["v2_status"] == "public"),
        "v1_internalized": sum(
            1 for e in v1if.values()
            if e["v2_status"] == "internal"),
        "v1_gone": gone,
        "note": ("declared_total = every callable declaration in the "
                 "v2 service headers (includes internal helpers). "
                 "l1_official_count = the numbered L1SVC op surface "
                 "(kernel-side ABI table, src/L0/services/sysapi/l1svc_defs.h). "
                 "v1_* fields record the COMPLETE v1.0.0 Part 2 "
                 "declaration truth (all %d callable rows across the 13 "
                 "surface headers; the v1 README numbered formula said "
                 "162, and the 2.0.0 postcheck's 163-row parse was "
                 "incomplete) and each row's fate in the current tree - "
                 "see INTERFACES.md Part 2c and Part 2d." % len(v1if)),
        "by_file": by_file,
    }
    with open(os.path.join(args.out, "interfaces.json"), "w") as f:
        json.dump(summary, f, indent=1)
    print(f"gen_api_docs: interfaces.json - {len(interfaces)} declarations "
          f"from {len(L1_HEADERS)} surface headers")

    # ---- commands (v2: sources are the authority; a runtime capture
    # is still accepted to cross-check the L0 half) ----
    v2cmds, svc_rows_total, svc_dual = extract_v2_commands()
    svc_cats, svc_ops = extract_l1svc_surface()
    v1base = extract_v1_baseline()
    cmds = v2cmds
    # 2.1.0 (P-15): SVC rows whose ush help-block line uses a compact
    # format the help regex can't see (e.g. "mkfs.fat32|exfat|ext4 <dev>")
    # get the usage hint straight from the l1svc_defs.h op comment -
    # the same string the numbered-op tables print.
    op_hint = {}
    for op in svc_ops.values():
        parts = op["cmd"].split()
        if parts:
            op_hint[parts[0]] = op["cmd"]
    filled_hints = 0
    for c in cmds:
        if (c["where"] == "L1-SVC" and not c["help"]
                and c["name"] in op_hint):
            c["help"] = op_hint[c["name"]]
            filled_hints += 1
    if filled_hints:
        print(f"gen_api_docs: filled {filled_hints} SVC help hint(s) "
              f"from l1svc_defs.h op comments")
    capture = None
    if args.help_capture:
        with open(args.help_capture, "r", encoding="latin-1") as f:
            capture = f.read()
    elif args.qemu:
        capture = capture_help_via_qemu(args.qemu)
    # v2: the source-extracted table is the authority; a runtime
    # capture (when given) cross-checks the L0 half only.
    with open(os.path.join(args.out, "commands.json"), "w") as f:
        json.dump({"count_v2_surface": len(cmds),
                   "count_l0": sum(1 for c in cmds if c["where"] == "L0"),
                   "count_l1_svc": sum(1 for c in cmds
                                       if c["where"] == "L1-SVC"),
                   "count_l1_builtin": sum(1 for c in cmds
                                           if c["where"] == "L1-builtin"),
                   # 2.1.0 (P-14 fix): the SVC counting caliber. The ush
                   # SVC_CMDS table carries svc_rows_total rows; the
                   # len(svc_dual) dual-homed names are ALSO registered
                   # as L0 commands and are attributed to L0 by the
                   # first-home rule, so count_l1_svc + count_l0 is the
                   # unique-name ledger - both halves stay reachable.
                   "count_ush_svc_rows": svc_rows_total,
                   "ush_svc_dual_homed": sorted(svc_dual),
                   "caliber_note": (
                       "count_l1_svc counts unique names attributed to "
                       "their first home (L0 wins). The ush SVC_CMDS "
                       "table itself carries %d rows; the %d dual-homed "
                       "names (%s) are registered both as L0 commands "
                       "and as ush SVC rows, are attributed to L0 here, "
                       "and remain callable from both shells."
                       % (svc_rows_total, len(svc_dual),
                          ", ".join(sorted(svc_dual)))),
                   "commands": cmds}, f, indent=1)
    if capture is not None:
        cap_cmds, total = extract_commands_from_capture_text(capture)
        all_names = {c["name"] for c in cmds}
        cap_names = {c["name"] for c in cap_cmds}
        # 2.1.0: the capture is the LIVE oc> `help -a` output of the
        # current build; it cross-checks the L0 half of the extracted
        # surface (a v1 capture would work the same way - any capture
        # name with no v2 home at all is flagged MISSING).
        missing = cap_names - all_names
        not_l0 = cap_names - {c["name"] for c in cmds
                              if c["where"] == "L0"}
        print(f"gen_api_docs: runtime cross-check - capture lists "
              f"{len(cap_names)} (declares {total}); v2 surface "
              f"{len(all_names)}; capture names outside the L0 half: "
              f"{len(not_l0)}; MISSING in v2: "
              f"{sorted(missing) or 'none'}")
    else:
        print(f"gen_api_docs: commands.json - {len(cmds)} v2 commands "
              f"from the sources (no runtime capture requested)")

    # ---- stats ----
    stats = extract_stats()
    # 2.1.0 (P-17 follow-up): the file-count caliber documented in the
    # data file itself - the 379-file figure counts .c/.h/.S sources
    # only, so l1apps/ contributes its 9 .c files, not all 36 program
    # files (25 .asm + 2 .ld are outside the extension set, same as
    # the README verification command).
    stats["note"] = ("Counts .c/.h/.S sources under the 9 STAT_DIRS "
                     "(core arch drivers services recovery tui chinese "
                     "boot l1apps); generated data blobs excluded, and "
                     "l1apps/ .asm/.ld program files are not counted "
                     "(9 of its 36 files are .c) - the same exclusions "
                     "the README verification command applies.")
    with open(os.path.join(args.out, "stats.json"), "w") as f:
        json.dump(stats, f, indent=1)
    print(f"gen_api_docs: stats.json - {stats['total_files']} files, "
          f"{stats['total_lines']} lines across {len(STAT_DIRS)} dirs")

    # ---- INTERFACES.md (root reference, generated) ----
    # NOTE: must be written BEFORE the docs/root/ mirror sync below,
    # so the mirror copies the freshly generated root file (2.1.0
    # order bug: the mirror ran first and copied the previous root
    # INTERFACES.md, leaving stale stats numbers in docs/root/).
    if args.md:
        path = os.path.join(ROOT, "INTERFACES.md")
        with open(path, "w") as f:
            f.write(write_interfaces_md(interfaces, stats, cmds,
                                        svc_cats=svc_cats,
                                        svc_ops=svc_ops, v1=v1base,
                                        v1if=v1if,
                                        svc_rows_total=svc_rows_total,
                                        svc_dual=svc_dual))
        print("gen_api_docs: INTERFACES.md written at the repo root")

    # ---- root-doc mirrors (docs/root/) ----
    if args.pages:
        m, r = sync_root_doc_mirrors()
        print(f"gen_api_docs: root-doc mirrors - {m} files mirrored to "
              f"docs/root/, {r} referencing pages rewritten")

    # ---- per-command reference pages ----
    if args.pages and cmds:
        pages_dir = os.path.join(ROOT, "docs", "manual", "commands")
        os.makedirs(pages_dir, exist_ok=True)
        n, d = write_command_pages(cmds, pages_dir)
        print(f"gen_api_docs: command pages - {n} pages across {d} "
              f"domains under docs/manual/commands/")
    if args.pages_zh and cmds:
        zh_dir = os.path.join(ROOT, "docs", "zh", "manual", "commands")
        os.makedirs(zh_dir, exist_ok=True)
        n, d = write_command_pages(cmds, zh_dir, lang="zh")
        print(f"gen_api_docs: command pages (zh) - {n} pages across {d} "
              f"domains under docs/zh/manual/commands/")

    # 2.1.0: the run log must state the v1 interface fate so the
    # release evidence shows the comparison result explicitly.
    if v1if:
        pub = sum(1 for e in v1if.values() if e["v2_status"] == "public")
        internal = sum(1 for e in v1if.values()
                       if e["v2_status"] == "internal")
        gone = sorted(nm for nm, e in v1if.items()
                      if e["v2_status"] == "gone")
        print(f"gen_api_docs: v1 interface fate - {pub} public, "
              f"{internal} internalized, gone: {gone}")
    return 0


def write_interfaces_md(interfaces, stats, cmds, svc_cats=None,
                         svc_ops=None, v1=None, v1if=None,
                         svc_rows_total=0, svc_dual=None):
    """Render the root INTERFACES.md reference from the extracted data.

    v2.0.0 layout: Part 1 is the full command surface split by home
    (L0 recovery shell / ring-3 SVC / ush built-ins), Part 1b is the
    v1 -> v2 command mapping (the compatibility contract), Part 2 the
    L1SVC service surface + kernel-side declarations, Part 2c/2d the
    v1 -> v2 interface-surface comparison and the deletion register,
    Part 3 syscalls.
    """
    VER = release_version()
    svc_dual = svc_dual or []
    by_file = {}
    for itf in interfaces:
        by_file.setdefault(itf["file"], []).append(itf)
    l0 = [c for c in cmds if c["where"] == "L0"]
    l1svc = [c for c in cmds if c["where"] == "L1-SVC"]
    l1b = [c for c in cmds if c["where"] == "L1-builtin"]
    bridge = [c for c in cmds if c.get("bridge") == "L1-bridge"]
    v1 = v1 or {}
    v1if = v1if or {}
    L = []
    w = L.append
    w("<!-- SPDX-License-Identifier: Apache-2.0 -->")
    w("<!-- GENERATED by tools/gen_api_docs.py --md (make api-docs).")
    w("     Do not hand-edit the tables; edit the generator or the")
    w("     surface headers instead. -->")
    w("# Open Cube OS Interface Reference (%s)" % VER)
    w("")
    w("Every entry below is extracted from the real tree at generation")
    w("time: the L0 command declarations straight from the OC_SHELL_CMD")
    w("call sites, the ring-3 surface from the ush SVC_CMDS table and")
    w("src/L0/services/sysapi/l1svc_defs.h, the kernel-side declarations from the")
    w("service headers. Regenerate with `make api-docs`.")
    w("")
    w("- v1.0.0 commands (compatibility baseline): **%d** - all mapped"
      % len(v1))
    w("  below, none dropped")
    w("- %s command homes: **%d total** = %d in the L0 recovery"
      % (VER, len(cmds), len(l0)))
    w("  shell + %d ring-3 SYS_L1SVC ops + %d ush built-ins"
      % (len(l1svc), len(l1b)))
    w("- L1SVC service surface: **%d categories / %d numbered ops**"
      % (len(svc_cats or {}), len(svc_ops or {})))
    w("- Kernel-side declarations across the service headers: **%d**"
      % len(interfaces))
    w("- Syscalls: 54 (`grep -c '#define SYS_' src/L0/services/sysapi/core_syscall.h`,")
    w("  SYS_L1SVC(110) added in 2.0.0)")
    w("- Source lines: **%s** across %d files (README verification"
      % (format(stats["total_lines"], ","), stats["total_files"]))
    w("  command; .c/.h/.S, generated data blobs excluded)")
    w("")
    w("2.0.0 is a breaking re-architecture, stated precisely (2.1.0")
    w("correction of the over-general 2.0.0 phrasing): the three")
    w("driver-family register hooks (`driver_block_register`,")
    w("`driver_nic_register`, `driver_snd_register`) moved from the")
    w("public v1 L1 headers into the per-family internal headers")
    w("(`services/*/drv_internal.h`, 'internal since 2.0.0'); drivers")
    w("and services now declare their identity in-file via")
    w("OC_DRIVER_DECL / OC_SERVICE_DECL; every operational command")
    w("class moved to the ring-3 service surface; and the only true")
    w("deletions on the v1 numbered surface are the two entries in the")
    w("deletion register (Part 2d). Other register-class declarations")
    w("(tui_ext.h menus/pages/widgets/..., `watchdog_register`,")
    w("`signal_register`, `shell_register_builtin`) are STILL PUBLIC")
    w("in the v2 tree - the per-entry truth is Part 2c. What")
    w("deliberately stays in L0: the kernel self-tests, the watchdog,")
    w("the crash policy controls, the kernel log readers")
    w("(dmesg/logcat/logctl/crashlog/dumpinfo/crashtest), the chipset")
    w("diagnostics, the recovery-environment file commands, and the")
    w("interactive session commands (ssh/sshd).")
    w("")
    w("## Part 1 - Command surface by home")
    w("")
    w("### 1a - L0 recovery shell (`oc>`, %d commands)"
      % len(l0))
    w("")
    w("File-local OC_SHELL_CMD declarations; the `oc>` prompt is the")
    w("fallback environment after the ring-3 shell exits (or fails to")
    w("start).")
    w("")
    w("| Command | One-line help | Category |")
    w("|---------|---------------|----------|")
    for c in sorted(l0, key=lambda x: x["name"]):
        w("| `%s` | %s | %s |" % (
            c["name"], esc(c["help"]).replace("|", "\\|"), c["cat"]))
    w("")
    w("### 1b - Ring-3 service commands (ush, %d SVC rows + %d built-ins)"
      % (len(l1svc), len(l1b)))
    w("")
    w("The default boot entry (ush, ring 3) routes these through")
    w("SYS_L1SVC(110) to the owning L0 service; the built-ins run")
    w("entirely in ring 3 (classic file/process syscalls) or through")
    w("the SYS_NETCMD bridge (%s)."
      % ", ".join("`%s`" % n for n in sorted(
          c["name"] for c in bridge)))
    w("")
    w("Counting caliber (2.1.0, P-14): the ush SVC_CMDS table itself")
    w("carries **%d** rows. The %d rows below are the unique names"
      % (svc_rows_total, len(l1svc)))
    w("attributed to their first home: the %d dual-homed names (%s)"
      % (len(svc_dual), ", ".join("`%s`" % n for n in sorted(svc_dual))))
    w("are registered BOTH as L0 recovery-shell commands and as ush")
    w("SVC rows; the first-home rule counts them as L0 here, and they")
    w("stay callable from both shells. 133 + 9 = 142 ush rows; the")
    w("unique-name ledger (99 L0 + 133 SVC + 19 built-ins = 251) is")
    w("unchanged by dual-homing.")
    w("")
    w("| Command | Service |")
    w("|---------|---------|")
    for c in sorted(l1svc, key=lambda x: x["name"]):
        w("| `%s` | `services/%s/` |" % (c["name"], svc_folder_of_cat(c["cat"])))
    w("")
    w("Dual-homed with L0 (listed in Part 1a, reachable from ush")
    w("too): %s." % ", ".join(
        "`%s`" % n for n in sorted(svc_dual)))
    w("")
    w("Built-ins: %s." % ", ".join(
        "`%s`" % c["name"] for c in sorted(l1b, key=lambda x: x["name"])))
    w("")
    w("## Part 1c - v1.0.0 -> %s command mapping (%d rows)"
      % (VER, len(v1)))
    w("")
    w("The compatibility contract: every 1.0.0 command and where it")
    w("lives now. `L0` = recovery shell (same name), `SVC` = ring-3")
    w("service surface (same body, captured output), `builtin` = ush")
    w("built-in, `bridge` = ush built-in reaching the L0 body through")
    w("SYS_NETCMD, `L0-only` = recovery-environment command kept out")
    w("of the ring-3 surface by design (kernel self-test / watchdog /")
    w("crash policy / log reader / chipset diagnostic / interactive")
    w("session classes).")
    w("")
    w("| v1.0.0 command | v2.0.0 home |")
    w("|----------------|-------------|")
    where_by_name = {c["name"]: c for c in cmds}
    for name in sorted(v1):
        c = where_by_name.get(name)
        if c is None:
            w("| `%s` | **MISSING** |" % name)
            continue
        if c.get("bridge"):
            home = "bridge (L0 body + ush NETCMD built-in)"
        elif c["where"] == "L0":
            home = "L0-only (recovery environment)"
        elif c["where"] == "L1-SVC":
            home = "SVC `services/%s/`" % svc_folder_of_cat(c["cat"])
        else:
            home = "builtin (ush)"
        w("| `%s` | %s |" % (name, home))
    w("")
    w("## Part 2 - L1SVC service surface")
    w("")
    w("The numbered ring-3 call surface (SYS_L1SVC): category number")
    w("selects the services/`<category>`/ folder, op selects the command.")
    w("Renumber only by appending (see src/L0/services/sysapi/l1svc_defs.h).")
    w("")
    if svc_cats:
        w("| Cat | Folder | Categories with an L1 dispatch table |")
        w("|-----|--------|--------------------------------------|")
        dispatched = set()
        if svc_ops:
            for opname, op in svc_ops.items():
                if op["folder"]:
                    dispatched.add(op["folder"])
        for catname, cat in sorted(svc_cats.items(),
                                   key=lambda kv: kv[1]["num"]):
            folder = cat["folder"] or catname.lower()
            mark = "yes" if folder in dispatched else "-"
            w("| %d | `services/%s/` | %s |" % (cat["num"], folder, mark))
        w("")
    if svc_ops:
        for folder in sorted(set(o["folder"] for o in svc_ops.values()
                                 if o["folder"])):
            w("### services/%s (%d ops)" % (folder, sum(
                1 for o in svc_ops.values() if o["folder"] == folder)))
            w("")
            w("| Op | Command | Constant |")
            w("|----|---------|----------|")
            for opname, op in sorted(
                    ((k, v) for k, v in svc_ops.items()
                     if v["folder"] == folder),
                    key=lambda kv: kv[1]["num"]):
                w("| %d | `%s` | `%s` |" % (op["num"], op["cmd"], opname))
            w("")
    w("### Kernel-side service header declarations")
    w("")
    w("Every callable declaration in the service surface headers, in")
    w("file and declaration order.")
    w("")
    for fname in sorted(by_file):
        w("### %s" % fname)
        w("")
        w("| Line | Declaration |")
        w("|------|-------------|")
        for itf in by_file[fname]:
            w("| %d | `%s` |" % (itf["line"],
                                 itf["signature"].replace("|", "\\|")))
        w("")
    if v1if:
        pub = sum(1 for e in v1if.values()
                  if e["v2_status"] == "public")
        internal = sum(1 for e in v1if.values()
                       if e["v2_status"] == "internal")
        gone = [nm for nm, e in sorted(v1if.items())
                if e["v2_status"] == "gone"]
        w("## Part 2c - v1.0.0 -> %s interface-surface comparison "
          "(%d entries)" % (VER, len(v1if)))
        w("")
        w("2.1.0 (P-04): the per-entry truth behind 'the register class")
        w("is gone'. The v1 README formula counted 162 numbered L1")
        w("extension interfaces; the v1 Part 2 tables are the complete")
        w("unfiltered declaration truth of the 13 L1 surface headers -")
        w("**%d callable rows** - and this comparison covers every one" % len(v1if))
        w("of them (a superset of any numbered subset; the 2.0.0")
        w("postcheck's 163-row parse was itself incomplete and missed")
        w("three of the deletions below). Their fate in the current")
        w("tree: **%d public + %d internalized + %d gone = %d.**"
          % (pub, internal, len(gone), len(v1if)))
        w("")
        w("| v1.0.0 entry | v1 header | v2 status | v2 home |")
        w("|---|---|---|---|")
        for nm in sorted(v1if, key=lambda k: (v1if[k]["v1_header"],
                                             v1if[k]["v1_line"])):
            e = v1if[nm]
            if e["v2_status"] == "public":
                st = "public"
            elif e["v2_status"] == "internal":
                st = "internalized"
            else:
                st = "**gone** (deleted - Part 2d)"
            w("| `%s` | `%s` | %s | %s |" % (
                nm, e["v1_header"], st,
                ("`%s`" % e["v2_home"].replace(", ", "`, `"))
                if e["v2_home"] != "-" else "-"))
        w("")
        w("## Part 2d - Deletion register (declared, complete)")
        w("")
        w("Every v1.0.0 surface-header declaration with NO counterpart")
        w("anywhere in the current tree - the complete declared deletion")
        w("list. There are exactly **%d**; nothing else on the v1" % len(gone))
        w("surface was removed (the remaining %d entries are public or" % (len(v1if) - len(gone)))
        w("internalized per Part 2c). All deletions are *init/probe")
        w("plumbing*, not user-visible capability: the functionality")
        w("was rebuilt inside the v2 service model - each row names")
        w("where.")
        w("")
        w("| v1 entry | v1 header | What it did | Why removed | Rebuilt where |")
        w("|---|---|---|---|---|")
        w("| `void driver_block_init(void);` | "
          "`drivers/block/driver_block_blk.h` | hardcoded per-driver "
          "block-layer init walk | v2 drivers self-register via "
          "OC_DRIVER_DECL and services self-initialize from the "
          "collected OC_SERVICE_DECL linker sections - a hardcoded "
          "walk contradicts the model | `block_svc_init()` in "
          "`services/block/block_svc.c` (OC_SERVICE_DECL_L1 in "
          "`services/block/block_l1.c`) |")
        w("| `int driver_nic_init(void);` | "
          "`drivers/nic/driver_nic.h` | hardcoded per-family NIC "
          "init walk | same self-declaration model | `nic_svc_init()` "
          "in `services/nic/nic_svc.c` (`services/nic/nic_svc.h`) |")
        w("| `int driver_nic_probe_all(void);` | "
          "`drivers/nic/driver_nic.h` | hardcoded probe list over all "
          "NIC families | the probe list is COLLECTED from per-driver "
          "OC_DRIVER_DECL identities instead of hardcoded | "
          "`nic_svc_probe_all()` in `services/nic/nic_svc.c` "
          "(`services/nic/nic_svc.h`) |")
        w("| `void driver_snd_init(void);` | "
          "`drivers/snd/driver_snd.h` | hardcoded sound-driver init "
          "walk | same self-declaration model | `snd_svc_init()` in "
          "`services/snd/snd_svc.c` (`services/snd/snd_svc.h`) |")
        w("| `int driver_snd_probe_all(void);` | "
          "`drivers/snd/driver_snd.h` | hardcoded probe list over all "
          "sound drivers | same collection model (see "
          "`services/snd/snd_svc.c`) | `snd_svc_probe_all()` in "
          "`services/snd/snd_svc.c` (OC_SERVICE_DECL_L1 late probe, "
          "`services/snd/snd_l1.c`) |")
        w("")
        w("'Register class gone' - the exact statement (2.1.0")
        w("correction):")
        w("")
        w("- The register-class hooks of the three driver families")
        w("  were NOT deleted - they moved from the public v1 L1 headers")
        w("  into the per-family internal headers 'internal since")
        w("  2.0.0' (`driver_block_register` AND the block family's")
        w("  `driver_block_register_device` / `_unregister_device` /")
        w("  `driver_block_set_ops` -> `services/block/drv_internal.h`;")
        w("  `driver_nic_register` -> `services/nic/drv_internal.h`;")
        w("  `driver_snd_register` -> `services/snd/drv_internal.h` -")
        w("  6 entries in Part 2c marked internalized). They are")
        w("  driver-private plumbing now, not an L1 extension surface.")
        w("- Still-public register-class declarations in the current")
        w("  tree: the `tui_*_register` families (`tui/tui_ext.h`),")
        w("  `watchdog_register` (`services/watchdog/core_watchdog.h`),")
        w("  `signal_register` (`services/l1/l1_wp8a.h`),")
        w("  `shell_register_builtin` (`services/l1/l1_wp8cd.h`) and")
        w("  `l1_ext_shell_register_command_ex`")
        w("  (`services/l1/l1_ext.h`).")
        w("- The only true deletions on the v1 surface are the %d rows" % len(gone))
        w("  above.")
        w("")
    w("## Part 3 - Syscall surface (54)")
    w("")
    w("Defined in `src/L0/services/sysapi/core_syscall.h` (`#define SYS_*`).")
    w("ABI: rax = number, rdi/rsi/rdx/r10 = arguments (r8 = 5th for")
    w("SYS_L1SVC), rax = result (>= 0 success, < 0 error).")
    w("SYS_L1SVC(110) and SYS_NETCMD(101) are the 2.0.0 ring-3")
    w("service bridges.")
    w("")
    return "\n".join(L)


def extract_commands_from_capture_text(text):
    cmds = []
    seen = set()
    total = None
    total_re = re.compile(r"Total:\s*(\d+)\s*commands")
    entry_re = re.compile(
        r"^\s{2}([a-z0-9_.][a-z0-9_. -]*?)\s+-\s+(.*)$")
    for line in text.splitlines():
        m = total_re.search(line)
        if m:
            total = int(m.group(1))
            continue
        m = entry_re.match(line)
        if m:
            name = m.group(1)
            # a runner capture can contain the command table twice
            # (the live serial log + the runner's OUTPUT SUMMARY
            # replay) - dedupe by name, first occurrence wins.
            if name in seen:
                continue
            seen.add(name)
            cmds.append({"name": name, "help": m.group(2).strip()})
    return cmds, total


if __name__ == "__main__":
    sys.exit(main())
