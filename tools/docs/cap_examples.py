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

# mega apps: the example is the SERIAL-VISIBLE classic dispatch
# (byte-identical to the single-command app); 'nope' for those
# whose usage renders on VGA only
MEGA_USAGE = {
    "disk-tool": ["disk-tool lsblk"],
    "update": ["update status"],
    "files": ["files nope"],
    "net-tool": ["net-tool nope"],
    "sound-tool": ["sound-tool nope"],
    "sysdiag": ["sysdiag nope"],
    "hwinfo": ["hwinfo nope"],
    "power": ["power nope"],
}

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
    if name == "sshd":
        return "long_running"
    if name in NONTERM:
        return "nonterm"
    if name in INTERACTIVE:
        return "interactive"
    if name in CRASH:
        return "crash"
    if name in USAGE_ONLY:
        return "usage_only"
    return "plain"


# per-command completion timeouts (seconds) - the prompt return is
# the completion signal; stress batteries and the DH test need long
# windows under TCG
LONG_RUNNERS = {
    "dhtest": 500, "pmmrace": 300, "memtest": 300, "vmtest": 300,
    "heaptest": 240, "fstest": 240, "cryptotest": 120,
    "checkupdate": 150, "checkupdate_test": 150, "update": 90,
    "real_hw_test": 90, "sshd": 25, "loop": 15,
}
DEFAULT_TIMEOUT = 45


STDIN_READERS = {"cat", "sort", "tail", "uniq", "wc", "head", "grep",
                 "awk", "sed"}


def run_to_prompt(child, cmdline, timeout=None):
    """Send one command line; capture everything until the NEXT ush
    prompt (the reliable completion signal - a fixed lull breaks too
    early for slow-starting TCG spawns).  Commands that read standard
    input with no arguments get a Ctrl+D to close it.

    The prompt regexes are LINE-ANCHORED (``(?m)^ush>\\s``): command
    OUTPUT can embed prompt-lookalike text mid-line (``env`` prints
    ``PS1=ush> ``), and an unanchored match there consumes a fake
    prompt, leaves the real one stale in the buffer and desyncs every
    later capture window by one command (each entry then records the
    PREVIOUS command's transcript).  verify_docs_counts.sh gates this
    by checking every block opens with its own command echo."""
    t = timeout or LONG_RUNNERS.get(
        cmdline.split()[0], DEFAULT_TIMEOUT)
    child.sendline(cmdline)
    if cmdline.split()[0] in STDIN_READERS and len(cmdline.split()) == 1:
        time.sleep(1.0)
        child.send("\x04")       # Ctrl+D: close stdin
    try:
        i = child.expect([r"(?m)^ush>\s", pexpect.TIMEOUT, pexpect.EOF],
                         timeout=t)
        if i == 0:
            return child.before + "ush> ", True
        return child.before, False
    except (pexpect.EOF, pexpect.TIMEOUT):
        return "", False


class VM:
    MON = "/tmp/oc-dict-cap-mon.sock"

    def __init__(self, iso):
        self.iso = iso
        self.child = None

    def boot(self, timeout=180):
        if self.child and self.child.isalive():
            self.child.terminate(force=True)
            time.sleep(1)
        if os.path.exists(self.MON):
            os.unlink(self.MON)
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = LIB
        cmd = ("{q} -m 512 -L {qd} -L {sb} -cdrom {iso} -boot d "
               "-net none -serial stdio -display none -no-reboot "
               "-monitor unix:{mon},server,nowait"
               ).format(q=QEMU, qd=QDATA, sb=SEABIOS, iso=self.iso,
                        mon=self.MON)
        self.child = pexpect.spawn(cmd, env=env, timeout=timeout,
                                   encoding="latin-1",
                                   codec_errors="replace")
        self.child.expect(r"(?m)^ush>\s", timeout=timeout)
        for _ in range(40):
            if os.path.exists(self.MON):
                break
            time.sleep(0.5)
        return self.child

    def mon_key(self, key, settle=1.0):
        """Drive a real keystroke through the QEMU monitor."""
        import socket
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(8)
            s.connect(self.MON)
            s.sendall(("sendkey %s\n" % key).encode())
            time.sleep(settle)
            try:
                s.settimeout(1.5)
                while True:
                    b = s.recv(4096)
                    if not b:
                        break
            except Exception:
                pass
            s.close()
        except Exception:
            pass

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
            child.expect(r"(?m)^ush>\s", timeout=5)
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
                try:
                    child.expect(r"(?m)^oc>\s", timeout=15)
                    out = child.before + child.after
                    note = "exit drops to the L0 recovery shell"
                except (pexpect.EOF, pexpect.TIMEOUT):
                    out, note = "", ""
                # come back: relaunch the L1 shell from oc>
                child.sendline("run ush")
                try:
                    child.expect(r"(?m)^ush>\s", timeout=60)
                    back = True
                except (pexpect.EOF, pexpect.TIMEOUT):
                    back = False
                if back:
                    record(name, "exit", strip_ansi(out), note=note)
                    print("[cap] %-16s exit OK" % name)
                else:
                    raise RuntimeError("could not return to ush")
            elif cls == "long_running":
                # sshd: the daemon serves one session and stays up -
                # capture the startup banner, then reset for the rest
                child = vm.child
                child.sendline(name)
                time.sleep(20)
                out = ""
                dl = time.time() + 6
                while time.time() < dl:
                    try:
                        i = child.expect([".+", pexpect.TIMEOUT],
                                         timeout=2)
                        if i == 0:
                            out += child.after
                            continue
                        break
                    except (pexpect.EOF, pexpect.TIMEOUT):
                        break
                record(name, name, strip_ansi(out),
                        note="long-running service (the SSH daemon); "
                             "the transcript shows the startup output, "
                             "then the session was reset")
                print("[cap] %-16s long-running captured" % name)
                vm.kill()
                vm.boot()
            elif cls == "interactive":
                child = vm.child
                # for mega apps: the serial-visible classic dispatch
                if name in MEGA:
                    ln = MEGA_USAGE[name][0]
                    out, ok = run_to_prompt(child, ln)
                    if ok:
                        record(name, ln, strip_ansi(out),
                                note="classic dispatch (verbatim op "
                                     "passthrough)")
                        print("[cap] %-16s mega-classic OK" % name)
                        continue
                    raise RuntimeError("prompt did not return after mega")
                # real interactive: capture opening screen then quit
                # through the QEMU monitor (full-screen apps read the
                # KEYBOARD, not the serial line)
                child.sendline(name)
                time.sleep(6)          # let the page paint (TCG)
                out = quiesce(child, lull=2, cap=10)
                ok = False
                for attempt in range(2):
                    vm.mon_key("esc", settle=1.0)
                    try:
                        child.expect(r"(?m)^ush>\s", timeout=45)
                        ok = True
                        break
                    except (pexpect.EOF, pexpect.TIMEOUT):
                        continue
                if ok:
                    out += "\n(returned to the ush prompt after Esc)"
                    record(name, name, strip_ansi(out),
                            note="full-screen page; Esc exits (driven "
                                 "via the QEMU monitor sendkey)")
                    print("[cap] %-16s interactive OK" % name)
                else:
                    # keep what we saw; reset for the next entry
                    record(name, name, strip_ansi(out),
                            note="full-screen page; the quit key was "
                                 "not honored in this session - output "
                                 "as captured, session reset")
                    print("[cap] %-16s interactive captured (reset)" % name)
                    vm.kill()
                    vm.boot()
            else:
                child = vm.child
                lines = RICH_ARGS.get(name) or [cmdline]
                out_all = ""
                ok_all = True
                for ln in lines:
                    out, ok = run_to_prompt(child, ln)
                    out_all += out
                    if not ok:
                        ok_all = False
                        break
                if ok_all:
                    record(name, "; ".join(lines), strip_ansi(out_all))
                    print("[cap] %-16s %-10s OK" % (name, cls))
                else:
                    raise RuntimeError("prompt did not return")
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
