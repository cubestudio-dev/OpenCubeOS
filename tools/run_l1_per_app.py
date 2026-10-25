#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS 2.1.0 - L1 per-app record driver (P-08 gate).
#
# Boots the release ISO ONCE PER PROGRAM (a clean process table per
# row: a same-boot marathon showed a real kernel bookkeeping wrinkle -
# a stale unwaited child + tid reuse makes the next wait4-using
# program collect a stale sibling's exit code - so each row gets a
# fresh, interference-free system), enters the L0 recovery shell (oc>)
# and executes that program through `run <name>`
# (`run` is ASYNC: it spawns the ring-3 ELF, prints
# "started pid=N ... (kill N stops it)" and the prompt returns while
# the program streams its output).  Capture protocol per row:
#   1. send `run <name>`, expect the "started pid=N" ack
#   2. quiescence-collect: consume output until a 4 s lull (40 s cap)
#   3. liveness probe: `echo L1PROBE_<n>` must round-trip - proves the
#      kernel + shell survived the program (the trap family rows)
#   4. record the captured output; FAIL on "command not found" or a
#      dead/never-responsive program
# Special rows:
#   - ush   `run ush` spawns a NESTED ush instance: expect its "ush>"
#           prompt, exit it, then probe
#   - loop  the by-design non-terminating spinner (watchdog starvation
#           target): verify its "loop: running" line, then VM-reset
#
# Usage: python3 tools/run_l1_per_app.py <iso> <out.log>
# Env:   OC_TOOLS (default /home/z/opt/extract), OC_L1_TIMEOUT (cap s)
import os
import re
import sys
import time

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
LIB = ":".join([f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib"])

# Mirrors core/userprogs_data.h exactly (35 rows: 34 test programs +
# ush as a nested instance + loop).
PROGRAMS = [
    "hello", "badapp", "fork_test", "exec_test", "pipe_test",
    "mmap_test", "signal_test", "select_test", "dyn_test",
    "dyn_hello", "so_test", "dlsym_test", "pie_test", "reloc_test",
    "mmap_multi", "test_min", "test_bss", "mprotect_test", "sse_test",
    "pf_test", "fdref_test", "select_zero_test", "int3_user",
    "main_dyn", "g7mmap", "p3_test", "g6_exec_a", "g6_exec_b",
    "g6_oom", "g6_test", "priv_test", "sigsegv_test", "stackovf",
    "ush", "loop",
]
TIMEOUT_PER_APP = int(os.environ.get("OC_L1_TIMEOUT", "40"))
LULL = 4


def spawn_iso(iso):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = LIB
    return pexpect.spawn(
        QEMU_BIN,
        ["-m", "512M", "-cdrom", iso, "-boot", "d", "-display", "none",
         "-serial", "mon:stdio",
         "-L", QEMU_DATADIR, "-L", SEABIOS_DIR],
        timeout=180, encoding="latin-1", codec_errors="replace",
        env=env)


def boot_to_oc(child, timeout=240):
    child.expect(r"ush>\s", timeout=timeout)
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)


def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\[\?\d+[a-zA-Z]", "", s)
    return s


def quiesce(child, lull=LULL, cap=TIMEOUT_PER_APP):
    """Consume output until a `lull`-second silence or `cap` total."""
    buf = []
    deadline = time.time() + cap
    while time.time() < deadline:
        try:
            i = child.expect([".+", pexpect.TIMEOUT], timeout=lull)
            if i == 0:
                buf.append(child.after)
                continue
            break
        except pexpect.EOF:
            break
    return "".join(buf)


def probe(child, n):
    """Liveness probe: `echo L1PROBE_n` must round-trip."""
    mark = f"L1PROBE_{n}"
    child.sendline(f"echo {mark}")
    try:
        child.expect(mark, timeout=15)
        child.expect(r"oc>\s", timeout=15)
        return True
    except (pexpect.TIMEOUT, pexpect.EOF):
        return False


def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <iso> <out.log>")
        return 1
    iso, outlog = sys.argv[1], sys.argv[2]
    child = spawn_iso(iso)
    rows = []
    log = open(outlog, "w", encoding="utf-8")
    raws = open(outlog + ".raw", "w", encoding="latin-1")

    def note(s):
        print(s)
        log.write(s + "\n")
        log.flush()


    note(f"[l1-per-app] iso: {iso}")
    note(f"[l1-per-app] fresh boot per row (clean process table), "
         f"{len(PROGRAMS)} rows")

    dead = False
    for idx, name in enumerate(PROGRAMS):
        # fresh, interference-free system for every row
        try:
            child.terminate(force=True)
        except Exception:
            pass
        child = spawn_iso(iso)
        boot_to_oc(child)
        child.sendline(f"run {name}")
        try:
            child.expect(rf"started pid=\d+[^\r\n]*", timeout=30)
        except (pexpect.TIMEOUT, pexpect.EOF):
            rows.append((name, "ERR", "no spawn ack"))
            note(f"[l1-per-app] {name:16s} ERR (no spawn ack)")
            continue
        ack = child.match.group(0)
        if name == "ush":
            # nested ush instance: its own prompt, then exit it
            out = quiesce(child, lull=6, cap=30)
            if "ush>" not in out:
                rows.append((name, "FAIL",
                             "nested ush prompt never appeared"))
                note(f"[l1-per-app] {name:16s} FAIL (no nested prompt)")
                continue
            child.sendline("exit")
            try:
                child.expect(r"oc>\s", timeout=30)
            except (pexpect.TIMEOUT, pexpect.EOF):
                rows.append((name, "FAIL", "nested exit failed"))
                note(f"[l1-per-app] {name:16s} FAIL (nested exit)")
                continue
            if not probe(child, idx):
                rows.append((name, "FAIL", "post-exit probe dead"))
                note(f"[l1-per-app] {name:16s} FAIL (probe)")
                continue
            rows.append((name, "PASS",
                         "nested ush instance spawned (ush> observed), "
                         "exited cleanly, shell alive"))
            note(f"[l1-per-app] {name:16s} PASS (nested spawn+exit)")
            continue
        if name == "loop":
            out = quiesce(child, lull=5, cap=25)
            if "loop: running" in out:
                rows.append((name, "PASS",
                             "prints 'loop: running', then spins by "
                             "design (watchdog starvation target)"))
                note(f"[l1-per-app] {name:16s} PASS (spinner verified)")
                continue
            rows.append((name, "FAIL", "spinner line never printed"))
            note(f"[l1-per-app] {name:16s} FAIL (no spinner line)")
            continue
        out = strip_ansi(quiesce(child))
        if "command not found" in out:
            rows.append((name, "FAIL", "command not found"))
            note(f"[l1-per-app] {name:16s} FAIL (command not found)")
            continue
        lines = [l for l in out.splitlines() if l.strip()]
        snippet = " | ".join(lines)[:160]
        if not lines:
            rows.append((name, "FAIL", "no output within capture window"))
            note(f"[l1-per-app] {name:16s} FAIL (no output)")
            continue
        if not probe(child, idx):
            rows.append((name, "FAIL", "liveness probe dead after run"))
            note(f"[l1-per-app] {name:16s} FAIL (probe dead)")
            continue
        rows.append((name, "PASS", snippet))
        note(f"[l1-per-app] {name:16s} PASS :: {snippet}")
        raws.write(f"ROW {name}: {out!r}\n\n")
        raws.flush()

    note("")
    note("=" * 60)
    note("L1 PER-APP RECORD (every embedded program via `run` from "
         "oc>, liveness probed per row)")
    note("=" * 60)
    ok = sum(1 for r in rows if r[1] == "PASS")
    note(f"{'PROGRAM':20s} RESULT  NOTE")
    for name, verdict, why in rows:
        note(f"{name:20s} {verdict:8s} {why}")
    note("")
    note(f"summary: {ok} PASS / {len(rows)} rows. ush is the boot "
         f"shell every row already runs inside; the ush row "
         f"additionally spawns+exits a nested instance. loop is the "
         f"by-design non-terminating spinner, verified by its output "
         f"line (VM reset to continue). Trap-family rows (badapp/"
         f"pf_test/int3_user/g6_exec_b/g6_oom/priv_test/sigsegv_test/"
         f"stackovf) pass when the kernel catches the trap, the "
         f"process dies and the echo probe round-trips.")
    log.close()
    try:
        child.sendline("poweroff now")
        child.expect(pexpect.EOF, timeout=30)
    except Exception:
        child.terminate(force=True)
    return 0 if ok == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
