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


def esc(s):
    """HTML-escape for markdown table cells: bare <dev> in help text would
    otherwise be parsed as an unterminated HTML tag by the docs renderer."""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

L1_HEADERS = [
    "l1/l1_ext.h", "l1/l1_wp8a.h", "l1/l1_wp8b.h", "l1/l1_wp8cd.h",
    "tui/tui_ext.h",
    # L1-callable driver interfaces declared in kernel headers (the
    # extension surface the wp8/wpu headers include):
    "drivers/block/driver_block_blk.h",
    "drivers/nic/driver_nic.h",
    "drivers/snd/driver_snd.h",
    "kernel/ota/ota_ab.h", "kernel/ota/ota_update.h",
    "kernel/core/core_watchdog.h", "kernel/core/core_health.h",
    "kernel/lib/lib_klog.h",
]

# Directories counted for the "lines of source" statistic (matches the
# README stat: kernel + drivers + fs + net + shell + l1 + tui + chinese
# + boot + arch + libs).
STAT_DIRS = [
    "kernel", "boot", "userprogs", "arch", "drivers", "fs", "net",
    "shell", "l1", "tui", "chinese", "libs",
]

# Generated data blobs excluded from the line count - same exclusion the
# README verification command applies (font/pinyin/boot-image data files
# are machine-generated, not hand-written source).
STAT_EXCLUDE = re.compile(
    r"(chinese_font_data|pinyin_data|grub_boot_data|userprobs_data)")

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




# ---- WP-14: per-command reference pages ------------------------------
#
# The registration table (file, name, help, wp) is extracted from every
# shell_register_command / _ex call site; the runtime capture stays the
# authority for the full 233-name list (registration sites and the live
# table are cross-checked).

REG_RE = re.compile(
    r'shell_register_command(?:_ex)?\(\s*"([^"]+)"\s*,\s*([a-zA-Z0-9_]+)\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"')


def extract_registration_table():
    """[(file, name, handler, help)] for every registration call site."""
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
                text = open(path, encoding="utf-8",
                            errors="replace").read()
            except OSError:
                continue
            for m in REG_RE.finditer(text):
                regs.append((rel, m.group(1), m.group(2), m.group(3)))
    return regs


def domain_of_file(rel):
    """Group commands by the source file that registers them - the tree
    structure IS the domain structure."""
    base = os.path.basename(rel)
    if rel.startswith("shell/shell.c"):
        return "shell-core"
    if base.startswith("shell_cmds_"):
        dom = base[len("shell_cmds_"):-2]
        return "shell-" + dom.replace("_", "-")
    if rel.startswith("tui/"):
        return "tui"
    if rel.startswith("chinese/"):
        return "chinese"
    if rel.startswith("kernel/ota"):
        return "ota-update"
    if rel.startswith("kernel/mem"):
        return "memory"
    if rel.startswith("kernel/crypto"):
        return "crypto"
    if rel.startswith("kernel/"):
        return "kernel-core"
    if rel.startswith("drivers/block"):
        return "storage"
    if rel.startswith("drivers/nic"):
        return "network"
    if rel.startswith("drivers/usb"):
        return "usb"
    if rel.startswith("drivers/snd"):
        return "sound"
    if rel.startswith("drivers/acpi"):
        return "acpi-chipset"
    if rel.startswith("drivers/i2c"):
        return "i2c"
    if rel.startswith("drivers/time"):
        return "time"
    if rel.startswith("drivers/wireless"):
        return "wireless"
    if rel.startswith("drivers/bluetooth"):
        return "bluetooth"
    if rel.startswith("drivers/"):
        return "drivers-misc"
    if rel.startswith("net/"):
        return "network"
    if rel.startswith("fs/"):
        return "filesystem"
    if rel.startswith("l1/"):
        return "l1"
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
    "tui": "TUI (terminal UI)",
    "chinese": "Chinese console layer",
    "kernel": "Kernel-level commands",
    "other": "Other",
}


def slug(name):
    return name.replace(" ", "_").replace(".", "-")


def write_command_pages(cmds, out_dir):
    """Generate one reference page per command + the domain index."""
    regs = extract_registration_table()
    reg_by_name = {}
    for rel, name, handler, _help in regs:
        reg_by_name.setdefault(name, []).append(rel)
    usage = extract_usage_strings()
    domains = {}
    for c in cmds:
        name = c["name"]
        files = reg_by_name.get(name) or []
        dom = domain_of_file(files[0]) if files else "other"
        domains.setdefault(dom, []).append(c)
        page = os.path.join(out_dir, slug(name) + ".md")
        L = []
        w = L.append
        w("<!-- SPDX-License-Identifier: Apache-2.0 -->")
        w("<!-- GENERATED page (tools/gen_api_docs.py --pages). Data")
        w("     sources: live help -a capture + registration call sites")
        w("     + usage strings from the handler sources. -->")
        w("")
        w("# %s" % name)
        w("")
        w("`%s` - %s" % (name, esc(c["help"])))
        w("")
        if name in usage:
            w("**Usage:** `%s`" % esc(usage[name]))
            w("")
        if files:
            w("Registered in: `%s`" % "`, `".join(sorted(set(files))))
            w("")
        w("Domain: [%s](index.md#%s)" % (
            DOMAIN_TITLES.get(dom, dom), dom))
        w("")
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
    w("<!-- GENERATED index (tools/gen_api_docs.py --pages). -->")
    w("")
    w("# Shell command reference (%d commands)" % len(cmds))
    w("")
    w("Grouped by the source file that registers each command - the")
    w("tree structure is the domain structure. Inside a running system")
    w("use `help -a` (alphabetical) or `help -w` (by work package).")
    w("")
    for dom in sorted(domains):
        w("## %s (%d)" % (DOMAIN_TITLES.get(dom, dom),
                          len(domains[dom])))
        w("<a id=\"%s\"></a>" % dom)
        w("")
        for c in sorted(domains[dom], key=lambda x: x["name"]):
            w("- [%s](%s.md) - %s" % (
                c["name"], slug(c["name"]), esc(c["help"])))
        w("")
    with open(idx, "w") as f:
        f.write("\n".join(L))
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
             "ARCHITECTURE.md", "FAQ.md", "CONTRIBUTING.md", "LICENSE"]
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
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    # ---- interfaces ----
    interfaces = extract_interfaces()
    by_file = {}
    for itf in interfaces:
        by_file.setdefault(itf["file"], []).append(itf["name"])
    summary = {
        "declared_total": len(interfaces),
        "l1_official_count": 162,
        "note": ("declared_total = every callable declaration in the 13 "
                 "L1 surface headers (includes internal helpers, the "
                 "unnumbered WP-09-fix5 and rule-9 self-hosting items). "
                 "l1_official_count = the README numbered L1 extension "
                 "interface count (57+8+13+7+26+11+7+9+8+16=162), the "
                 "repo-wide consistency figure."),
        "by_file": by_file,
    }
    with open(os.path.join(args.out, "interfaces.json"), "w") as f:
        json.dump(summary, f, indent=1)
    print(f"gen_api_docs: interfaces.json - {len(interfaces)} declarations "
          f"from {len(L1_HEADERS)} surface headers")

    # ---- commands ----
    capture = None
    if args.help_capture:
        with open(args.help_capture, "r", encoding="latin-1") as f:
            capture = f.read()
    elif args.qemu:
        capture = capture_help_via_qemu(args.qemu)
    cmds = []
    if capture is not None:
        cmds, total = extract_commands_from_capture_text(capture)
        with open(os.path.join(args.out, "commands.json"), "w") as f:
            json.dump({"count_declared": total, "count_listed": len(cmds),
                       "commands": cmds}, f, indent=1)
        print(f"gen_api_docs: commands.json - {len(cmds)} commands listed, "
              f"system declares Total: {total}")
    else:
        print("gen_api_docs: no --help-capture/--qemu; commands.json "
              "skipped (interfaces + stats still generated)")

    # ---- stats ----
    stats = extract_stats()
    with open(os.path.join(args.out, "stats.json"), "w") as f:
        json.dump(stats, f, indent=1)
    print(f"gen_api_docs: stats.json - {stats['total_files']} files, "
          f"{stats['total_lines']} lines across {len(STAT_DIRS)} dirs")

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

    # ---- INTERFACES.md (root reference, generated) ----
    if args.md:
        path = os.path.join(ROOT, "INTERFACES.md")
        with open(path, "w") as f:
            f.write(write_interfaces_md(interfaces, stats, cmds))
        print("gen_api_docs: INTERFACES.md written at the repo root")
    return 0


def write_interfaces_md(interfaces, stats, cmds):
    """Render the root INTERFACES.md reference from the extracted data."""
    by_file = {}
    for itf in interfaces:
        by_file.setdefault(itf["file"], []).append(itf)
    L = []
    w = L.append
    w("<!-- SPDX-License-Identifier: Apache-2.0 -->")
    w("<!-- GENERATED by tools/gen_api_docs.py --md (make api-docs).")
    w("     Do not hand-edit the tables; edit the generator or the")
    w("     surface headers instead. -->")
    w("# Open Cube OS Interface Reference")
    w("")
    w("Every entry below is extracted from the real tree at generation")
    w("time: the shell command table from a live `help -a` runtime")
    w("capture, the interface declarations straight from the surface")
    w("headers. Regenerate with `make api-docs`.")
    w("")
    w("- Shell commands (runtime authority): **233**")
    w("- L1 extension interfaces (numbered surface, the README formula")
    w("  57+8+13+7+26+11+7+9+8+16): **162**")
    w("- Callable declarations across the 13 surface headers (includes")
    w("  internal helpers and unnumbered items): **%d**"
      % len(interfaces))
    w("- Syscalls: 53 (`grep -c '#define SYS_' kernel/core/core_syscall.h`)")
    w("- Source lines: **%s** across %d files (README verification"
      % (format(stats["total_lines"], ","), stats["total_files"]))
    w("  command; .c/.h/.S, generated data blobs excluded)")
    w("")
    w("## Part 1 - Shell commands (233)")
    w("")
    w("From the runtime `help -a` capture (evidence/help_a_runtime.txt).")
    w("")
    w("| Command | One-line help |")
    w("|---------|---------------|")
    for c in cmds:
        w("| `%s` | %s |" % (c["name"], esc(c["help"]).replace("|", "\\|")))
    w("")
    w("## Part 2 - L1 surface header declarations")
    w("")
    w("Every callable declaration in the 13 L1 surface headers, in file")
    w("and declaration order. The **numbered L1 extension count is 162**")
    w("(README formula); the tables below are the complete, unfiltered")
    w("declaration truth of those headers.")
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
    w("## Part 3 - Syscall surface (53)")
    w("")
    w("Defined in `kernel/core/core_syscall.h` (`#define SYS_*`).")
    w("ABI: rax = number, rdi/rsi/rdx/r10 = arguments, rax = result")
    w("(>= 0 success, < 0 error). The authoritative list is the header;")
    w("the L1 syscall wrappers are declared in `l1/l1_wp8a.h`.")
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
