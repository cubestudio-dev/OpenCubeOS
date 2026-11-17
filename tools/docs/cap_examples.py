#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Open Cube OS 3.0.0 - dictionary example capturer.

Boots the release ISO and executes EVERY enumerated command once,
capturing the real serial-console output verbatim (the dictionary
rule: examples are executed, never invented).

Risk classes (explicit, auditable):
  plain      - run bare, quiesce-collect, liveness probe
  usage_only - destructive ops run with NO arguments: the captured
               usage/refusal IS the honest example
  interactive- full-screen/wizard apps: capture the opening screen,
               then quit (q / ESC / editor exit chords); VM-reset if
               wedged (checkpoint resumes after it)
  crash      - fault-by-design programs: the trap report is the
               example; the shell must survive (probe)
  nonterm    - `run loop` + kill (the by-design spinner)
  power_real - reboot/halt/poweroff/shutdown: one dedicated boot
               each, shutdown LAST (final boot ends the run)

Robustness: every command is checkpointed to a JSONL file before the
next one starts; a wedge (probe timeout) kills the VM, reboots, and
resumes after the offender - nothing is lost, the offender is marked
"wedged" in the record.

Usage:
  python3 tools/docs/cap_examples.py --iso build/opencube.iso \
      --facts /tmp/enum-facts.json --out build/dictionary/examples.jsonl
Env: OC_TOOLS (default ~/opt/extract)
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time

import pexpect

OC = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = os.path.join(OC, "usr", "bin", "qemu-system-x86_64")
QDATA = os.path.join(OC, "usr", "share", "qemu")
SEABIOS = os.path.join(OC, "usr", "share", "seabios")
LIB = ":".join([os.path.join(OC, "usr", "lib", "x86_64-linux-gnu"),
                os.path.join(OC, "lib", "x86_64-linux-gnu")])

MEGA = ["update", "disk-tool", "net-tool", "sound-tool",
        "files", "sysdiag", "hwinfo", "power"]

INTERACTIVE = {
    "tui", "tui-drv", "tui-ext", "tui-hw", "tui-net", "tui-sys",
    "tui-sysinfo", "tui-test", "tui-time",
    "edit", "nano", "vi", "chinese", "chinesetest", "top",
} | set(MEGA)

USAGE_ONLY = {
    "mkfs.fat32", "mkfs.exfat", "mkfs.ext4", "abdisk", "install",
    "grub-install", "rm", "rmdir", "umount", "mount", "fatmount",
    "dd", "kill", "run", "sleep", "nice",
}

CRASH = {
    "badapp", "int3_user", "sigsegv_test", "stackovf", "pf_test",
    "g6_oom", "crashtest", "priv_test", "p0leak", "p3_test",
}

NONTERM = {"loop"}

POWER_REAL = ["reboot", "halt", "poweroff", "shutdown"]

# commands whose example is the wizard-less usage table (deterministic)
MEGA_USAGE = {m: [m + " nope"] for m in MEGA}

# a few commands get explicit safe args for a richer example;
# a LIST means a small session - all lines are sent in order and the
# concatenated output is the example (verbatim session capture)
RICH_ARGS = {
    "echo": ["echo hello dictionary"],
    "sleep": ["sleep 1"],
    "help": ["help"],
    "grep": ["grep"],
    "cat": ["cat"],
    "ls": ["ls"],
    "lsblk": ["lsblk"],
    "dmesg": ["dmesg"],
    "alias": ["alias ll=ls", "alias"],
    "unalias": ["alias ll=ls", "unalias ll", "alias"],
    "cd": ["cd /", "pwd"],
    "export": ["export DICTVAR=1", "env"],
    "env": ["env"],
    "history": ["history"],
    "set": ["set"],
    "pwd": ["pwd"],
}


def classify(name):
    if name == "exit":
        return "exit_special"
    if name in NONTERM:
        return "nonterm"
    if name in INTERACTIVE:
        return "interactive"
    if name in CRASH:
        return "crash"
    if name in USAGE_ONLY:
        return "usage_only"
    return "plain"


class VM:
    def __init__(self, iso):
        self.iso = iso
        self.child = None

    def boot(self, timeout=180):
        if self.child and self.child.isalive():
            self.child.terminate(force=True)
            time.sleep(1)
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = LIB
        cmd = ("{q} -m 512 -L {qd} -L {sb} -cdrom {iso} -boot d "
               "-net none -serial stdio -display none -no-reboot"
               ).format(q=QEMU, qd=QDATA, sb=SEABIOS, iso=self.iso)
        self.child = pexpect.spawn(cmd, env=env, timeout=timeout,
                                   encoding="latin-1",
                                   codec_errors="replace")
        self.child.expect(r"ush>\s", timeout=timeout)
        return self.child

    def kill(self):
        if self.child and self.child.isalive():
            self.child.terminate(force=True)
        try:
            subprocess.run(["pkill", "-f", "qemu-system-x86_64.*opencube"],
                           capture_output=True)
        except Exception:
            pass
        time.sleep(1)


def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b[()][B0]", "", s)
    s = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", s)
    return s


def quiesce(child, lull=1.6, cap=25):
    """Consume output until a lull-second silence or cap total."""
    buf = []
    deadline = time.time() + cap
    while time.time() < deadline:
        try:
            i = child.expect([".+", pexpect.TIMEOUT], timeout=lull)
            if i == 0:
                buf.append(child.after)
                continue
            break
        except (pexpect.EOF, pexpect.TIMEOUT):
            break
    return "".join(buf)


def probe(child, n, timeout=8):
    """Liveness probe: echo DICHECK_n must round-trip."""
    mark = "DICHECK_%d" % n
    try:
        child.sendline("echo " + mark)
        child.expect(mark, timeout=timeout)
        # swallow the trailing prompt
        try:
            child.expect(r"ush>\s", timeout=5)
        except (pexpect.EOF, pexpect.TIMEOUT):
            pass
        return True
    except (pexpect.EOF, pexpect.TIMEOUT):
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--facts", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-cmds", type=int, default=0,
                    help="cap (0 = all); debugging aid")
    args = ap.parse_args()

    facts = json.load(open(args.facts))
    names = [c["name"] for c in facts["commands"]]
    names = [n for n in names if n not in POWER_REAL]

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    done = set()
    if os.path.exists(args.out):
        for line in open(args.out):
            try:
                done.add(json.loads(line)["name"])
            except Exception:
                pass
    todo = [n for n in names if n not in done]
    if args.max_cmds:
        todo = todo[:args.max_cmds]
    print("[cap] %d commands total, %d already captured, %d to go"
          % (len(names), len(done), len(todo)))

    vm = VM(args.iso)
    vm.boot()
    fh = open(args.out, "a")

    def record(name, cmdline, output, note=""):
        fh.write(json.dumps({"name": name, "cmd": cmdline,
                             "output": output, "note": note},
                            ensure_ascii=False) + "\n")
        fh.flush()
        done.add(name)

    idx = 0
    while idx < len(todo):
        name = todo[idx]
        idx += 1
        cls = classify(name)
        cmdline = (RICH_ARGS.get(name, [None])[0] or
                   (MEGA_USAGE.get(name, [name + " nope"])[0]
                    if name in MEGA else name))
        try:
            if cls == "nonterm":
                child = vm.child
                child.sendline("run loop")
                out = quiesce(child, lull=2, cap=8)
                m = re.search(r"started pid=(\d+)", out)
                if m:
                    child.sendline("kill " + m.group(1))
                    out += quiesce(child, lull=1.5, cap=6)
                out += quiesce(child, lull=1, cap=3)
                if probe(child, idx):
                    record(name, "run loop", strip_ansi(out))
                    print("[cap] %-16s nonterm OK" % name)
                else:
                    raise RuntimeError("wedged after loop")
            elif cls == "exit_special":
                child = vm.child
                child.sendline("exit")
                out = quiesce(child)
                # exit drops to the L0 recovery shell (oc>)
                try:
                    child.expect(r"oc>\s", timeout=10)
                    out += child.before + child.after
                    note = "exit drops to the L0 recovery shell"
                except (pexpect.EOF, pexpect.TIMEOUT):
                    note = ""
                # come back: relaunch the L1 shell from oc>
                child.sendline("ush")
                try:
                    child.expect(r"ush>\s", timeout=30)
                    back = True
                except (pexpect.EOF, pexpect.TIMEOUT):
                    back = False
                if back and probe(child, idx):
                    record(name, "exit", strip_ansi(out), note=note)
                    print("[cap] %-16s exit OK" % name)
                else:
                    raise RuntimeError("could not return to ush")
            elif cls == "interactive":
                child = vm.child
                # for mega apps: the deterministic usage table first
                if name in MEGA:
                    child.sendline(name + " nope")
                    out = quiesce(child)
                    if probe(child, idx):
                        record(name, name + " nope", strip_ansi(out))
                        print("[cap] %-16s mega-usage OK" % name)
                        continue
                    raise RuntimeError("wedged after mega usage")
                # real interactive: capture opening screen then quit
                child.sendline(name)
                out = quiesce(child, lull=2.5, cap=20)
                for key in ["q", "\x1b", "\x1b", "\x18", "\r"]:
                    if probe(child, idx):
                        break
                    try:
                        child.send(key)
                        out += quiesce(child, lull=1.5, cap=5)
                    except (pexpect.EOF, pexpect.TIMEOUT):
                        break
                if probe(child, idx):
                    record(name, name, strip_ansi(out))
                    print("[cap] %-16s interactive OK" % name)
                else:
                    raise RuntimeError("wedged interactive")
            else:
                child = vm.child
                lines = RICH_ARGS.get(name) or [cmdline]
                out_all = ""
                for ln in lines:
                    child.sendline(ln)
                    out_all += quiesce(child)
                out = out_all
                if probe(child, idx):
                    record(name, "; ".join(lines), strip_ansi(out))
                    print("[cap] %-16s %-10s OK" % (name, cls))
                else:
                    raise RuntimeError("wedged after plain run")
        except RuntimeError as e:
            print("[cap] %-16s WEDGED (%s) - reset+resume" % (name, e))
            record(name, cmdline or name, "", note="wedged:" + str(e))
            vm.kill()
            try:
                vm.boot()
            except Exception as e2:
                print("[cap] reboot failed: %s" % e2)
                vm.kill()
                time.sleep(3)
                vm.boot()
        except Exception as e:
            print("[cap] %-16s ERROR %s" % (name, e))
            record(name, cmdline or name, "", note="error:" + str(e))
            vm.kill()
            try:
                vm.boot()
            except Exception as e2:
                print("[cap] reboot failed: %s" % e2)
                vm.kill()
                time.sleep(3)
                vm.boot()

    fh.close()

    # ---------------- final phase: real power actions, one boot each
    print("[cap] main pass complete - power-real phase")
    for i, act in enumerate(POWER_REAL):
        try:
            vm.kill()
            vm.boot()
            child = vm.child
            child.sendline(act)
            out = quiesce(child, lull=2, cap=20)
            try:
                child.expect(pexpect.EOF, timeout=25)
                out += child.before
                eof = True
            except (pexpect.EOF, pexpect.TIMEOUT):
                eof = False
            fh = open(args.out, "a")
            fh.write(json.dumps(
                {"name": act, "cmd": act, "output": strip_ansi(out),
                 "note": "power-real; qemu-eof=%s" % eof},
                ensure_ascii=False) + "\n")
            fh.close()
            print("[cap] %-16s power-real captured (eof=%s)" % (act, eof))
        except Exception as e:
            print("[cap] %-16s power-real ERROR %s" % (act, e))
            fh = open(args.out, "a")
            fh.write(json.dumps(
                {"name": act, "cmd": act, "output": "",
                 "note": "error:%s" % e}, ensure_ascii=False) + "\n")
            fh.close()

    vm.kill()
    print("[cap] DONE - %d records in %s" % (len(done) + len(POWER_REAL),
                                             args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
