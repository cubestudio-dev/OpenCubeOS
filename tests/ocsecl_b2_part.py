#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocsecl_b2_part.py (3.0.0-WP06b-ocsecl)
# Batch-2 acceptance: the OCSECL shell body (prompt, tokenizer, line
# editor: arrows/Home/End/Del, Ctrl combos, history, Tab completion)
# plus the partition command group on a blank 64 MiB disk:
#   help / help <cmd> / lsdisk / lspart / parted / mkpart / rmpart /
#   setflag, the no-overlap guarantee and the full-table notice.
import os
import socket
import time
import pexpect
import sys

BUILD = "/home/z/oc-public/build"
DISK = f"{BUILD}/ocse-b2-disk64.raw"
MON = f"{BUILD}/ocse-b2-mon.sock"
if not os.path.exists(DISK) or os.path.getsize(DISK) != 64 * 1024 * 1024:
    with open(DISK, "wb") as f:
        f.write(b"\0" * (64 * 1024 * 1024))   # blank, zeroed
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")


class Mon:
    def __init__(self, path, wait=20):
        for _ in range(wait * 2):
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                self.s.settimeout(0.2)
                try:
                    while self.s.recv(4096):
                        pass
                except socket.timeout:
                    pass
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("monitor socket never came up")

    def cmd(self, c, settle=0.35):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)

    def type_line(self, text, settle=0.25):
        """Send one char at a time (the serial path is a keyboard)."""
        for ch in text:
            self.cmd(f"sendkey {self.keyname(ch)}", 0.02)
        self.cmd("sendkey ret", settle)

    def type_raw(self, text, settle=0.3):
        """Type WITHOUT Enter (for line-editor tests)."""
        for ch in text:
            self.cmd(f"sendkey {self.keyname(ch)}", 0.02)
        time.sleep(settle)

    @staticmethod
    def keyname(ch):
        special = {" ": "spc", ".": "dot", "-": "minus", "=": "equal",
                   "/": "slash", ";": "semicolon"}
        if ch in special:
            return special[ch]
        return ch


child = pexpect.spawn(CMD, timeout=180, encoding="utf-8", codec_errors="replace")
child.logfile = sys.stdout
mon = None
fails = []


def check(name, pattern, timeout=20):
    global fails
    try:
        child.expect(pattern, timeout=timeout)
        print(f"\n[b2 OK] {name}")
    except Exception as e:
        print(f"\n[b2 FAIL] {name}: {e}")
        fails.append(name)


try:
    child.expect("environment up: GUI mode", timeout=120)
    mon = Mon(MON)
    mon.cmd("sendkey ctrl-alt-f2", 1.0)

    # A: banner + prompt
    check("cli banner", "OCSECL")
    check("cli prompt", r"ocsecl>")

    # B: help lists the command table
    mon.type_line("help")
    check("help lists lsdisk", "lsdisk")
    check("help lists mkpart", "mkpart")
    check("help lists reboot", "reboot")

    # C: help <cmd>
    mon.type_line("help mkpart")
    check("help mkpart text", r"mkpart <disk> <startMiB> <endMiB>")

    # D: lsdisk on the blank disk
    mon.type_line("lsdisk")
    check("lsdisk hda", r"hda \[no table\] 64 MiB, 0 part")
    mon.type_line("lspart hda")
    check("lspart empty", "no partitions on this disk")

    # E: mkpart #1 [1,20) MiB
    mon.type_line("mkpart hda 1 20")
    check("mkpart 1 created", "partition created: hdap1")
    mon.type_line("lspart hda")
    check("lspart shows #1", r"#1 .* start=1MiB end=20MiB size=19MiB")

    # F: overlap refusal (must NOT touch existing partitions)
    mon.type_line("mkpart hda 5 10")
    check("overlap refused", "overlaps an existing partition")

    # G: second partition + setflag + rmpart
    mon.type_line("mkpart hda 20 40")
    check("mkpart 2 created", "partition created: hdap2")
    mon.type_line("setflag hda 1 boot on")
    check("setflag boot", "flag updated")
    mon.type_line("parted hda")
    check("parted boot=y", r"boot=y")
    mon.type_line("rmpart hda 2")
    check("rmpart 2", "partition removed")

    # H: fill the table. Slot reuse after rmpart is mainstream
    # behaviour (first free slot); the test expects exactly that.
    mon.type_line("mkpart hda 40 52")
    check("mkpart reuses freed slot", "partition created: hdap2")
    mon.type_line("mkpart hda 52 60")
    check("mkpart 3", "partition created: hdap3")
    mon.type_line("mkpart hda 60 64")
    check("mkpart 4", "partition created: hdap4")
    mon.type_line("mkpart hda 30 35")
    check("table full", "partition table is full")

    # I: line editor - history (Up recalls the last command; consume
    # the first output, then assert the re-EXECUTION)
    mon.type_line("lsdisk", settle=0.4)
    check("first lsdisk", r"hda \[MBR\] 64 MiB, 4 part")
    mon.cmd("sendkey up", 0.4)          # recalls "lsdisk"
    mon.cmd("sendkey ret", 0.6)
    check("history recall + re-run", r"hda \[MBR\] 64 MiB, 4 part")

    # J: Tab completion of a command prefix (typed WITHOUT Enter)
    mon.type_raw("lsd", settle=0.4)
    mon.cmd("sendkey tab", 0.6)        # completes to "lsdisk "
    check("tab completion", "lsdisk ")
    mon.cmd("sendkey ctrl-u", 0.3)     # kill line, clean slate
    mon.cmd("sendkey ret", 0.4)
    child.expect(r"ocsecl>", timeout=10)

    # K: Ctrl+C cancels a half-typed line
    mon.type_raw("xyzzy", settle=0.3)
    mon.cmd("sendkey ctrl-c", 0.4)
    check("ctrl-c marker", r"\^C")
    mon.type_line("clear")

    # L: lang switch round-trip (zh comes back as GB2312 -> U+FFFD)
    mon.type_line("lang zh")
    # WP06g3+: OCSE serial is UTF-8 - assert the zh glyphs (the old
    # GB2312/U+FFFD expectation is stale; updated in WP-fix-critical-security).
    check("lang zh output", "\u8bed\u8a00", timeout=20)
    mon.type_line("lang en")
    check("lang en output", "language: English")

    # M: back to the GUI, then hotkey back to CLI
    mon.type_line("gui")
    check("gui switched", "switched: GUI mode", timeout=30)
    mon.cmd("sendkey ctrl-alt-f2", 0.8)
    check("cli again", r"ocsecl>")

    print("\n[b2 summary] fails:", fails if fails else "none")
    sys.exit(1 if fails else 0)
except Exception as e:
    print(f"\n[b2 CRASH] {e}")
    sys.exit(2)
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
