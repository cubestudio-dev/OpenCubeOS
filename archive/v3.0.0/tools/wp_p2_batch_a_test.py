#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-fix-p2-p3-cleanup batch A live probes (issues #8 #9 #12 #14 #131
+ the pipe-slot leak) through the real QEMU serial console.

Probes (the app is src/L1/p2test, registered in gen_userprogs.py):

  read0  - #14: read(0, buf, 0) must return 0 immediately.  Pre-fix
           the stdin loop blocked until a key arrived (the host sends
           NO keys: a pre-fix kernel simply times out here).

  pipe   - #9 + slot leak: 100 rounds of pipe + two forked readers +
           a 4-byte cookie; a round only passes when exactly one
           reader decodes the cookie (exit 0) and the other sees a
           clean EOF (exit 5).  Duplicated delivery (the #9 race)
           fails the round with exit 2; 100 rounds > MAX_PIPES(32)
           proves the slot table is being reused.

  utf8   - #12: 174 hanzi x 3 bytes = 522 bytes per write, through
           BOTH console write paths (SYS_WRITE and SYS_WRITE2).  The
           host counts intact U+6D4B sequences in the serial log:
           348 expected (174 x 2 paths).  A frame-buffer screendump
           is taken afterwards for the VLM mojibake comparison
           (optional, --screendump).

Usage: tools/wp_p2_batch_a_test.py [iso] [--screendump DIR]
Exit code 0 = all probes passed.
"""
import os
import re
import signal
import sys
import time

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ENV = os.environ.copy()
ENV["LD_LIBRARY_PATH"] = ":".join([
    f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib"])

PROMPT = r"ush>\s"
HANZI = "\u6d4b"          # U+6D4B, the payload glyph


def hmp(child, hcmd, settle=1.0):
    child.send("\x01c")
    child.expect(r"\(qemu\)", timeout=20)
    child.sendline(hcmd)
    child.expect(r"\(qemu\)", timeout=20)
    child.send("\x01c")
    time.sleep(settle)


def main():
    args = sys.argv[1:]
    iso = "build/opencube.iso"
    dump_dir = None
    while args:
        a = args.pop(0)
        if a == "--screendump":
            dump_dir = args.pop(0)
        else:
            iso = a

    results = {}

    cmd = [QEMU, "-m", "512M", "-cdrom", iso, "-boot", "d", "-no-reboot",
           "-L", f"{OC_TOOLS}/usr/share/qemu",
           "-L", f"{OC_TOOLS}/usr/share/seabios",
           "-vga", "std", "-display", "none", "-serial", "mon:stdio"]
    # latin-1 keeps every byte of the UTF-8 payload decodable 1:1 so
    # the hanzi count works on the captured stream (text-mode log to
    # match pexpect's encoding-aware logfile writes).
    child = pexpect.spawn(cmd[0], cmd[1:], env=ENV, timeout=180,
                          encoding="latin-1", codec_errors="replace")
    log_path = "/tmp/wp_p2_batch_a_serial.log"
    logf = open(log_path, "w", encoding="latin-1")
    child.logfile_read = logf
    try:
        child.expect(PROMPT, timeout=120)
        print("[boot] ok")

        # ---------- read0 (#14) ----------
        print("[read0] running (no keys will be sent)...")
        child.sendline("p2test read0")
        idx = child.expect([r"READ0 ret=(\d+) dt=(\d+)ms\r*\n", PROMPT],
                           timeout=45)
        if idx == 0:
            ret, dt = child.match.group(1), child.match.group(2)
            child.expect(PROMPT, timeout=15)
            ok = (ret == "0" and int(dt) <= 900)
            print(f"[read0] ret={ret} dt={dt}ms -> "
                  + ("PASS" if ok else "FAIL"))
            results["read0"] = ok
        else:
            print("[read0] FAIL: no READ0 line (blocked on read?)")
            results["read0"] = False

        # ---------- pipe (#9 + slot leak) ----------
        print("[pipe] running 100 rounds (this takes a while)...")
        child.sendline("p2test pipe")
        idx = child.expect([r"PIPE: (\d+)/100 data \+ (\d+)/100 eof[^\n]*",
                            r"PIPE-FAIL[^\n]*",
                            r"PIPE-PASS",
                            PROMPT], timeout=420)
        if idx == 0:
            data, eof = child.match.group(1), child.match.group(2)
            child.expect(r"PIPE-PASS", timeout=30)
            child.expect(PROMPT, timeout=30)
            ok = (data == "100" and eof == "100")
            print(f"[pipe] data {data}/100 eof {eof}/100 -> "
                  + ("PASS" if ok else "FAIL"))
            results["pipe"] = ok
        elif idx == 1:
            line = child.match.group(0)
            child.expect(PROMPT, timeout=30)
            print(f"[pipe] FAIL: {line}")
            results["pipe"] = False
        elif idx == 2:
            child.expect(PROMPT, timeout=30)
            print("[pipe] PASS (summary line missing but PIPE-PASS)")
            results["pipe"] = True
        else:
            print("[pipe] FAIL: no output")
            results["pipe"] = False

        # ---------- utf8 (#12) ----------
        print("[utf8] running 522-byte hanzi writes through both paths...")
        child.sendline("p2test utf8")
        child.expect(r"UTF8-BEGIN", timeout=60)
        # child.after/before carry the payload between the markers; the
        # 522 hanzi bytes sit between UTF8-BEGIN and the W1 summary.
        i1 = child.expect([r"UTF8-W1 bytes=(\d+)\r*\n", r"UTF8-FAIL"],
                           timeout=60)
        w1 = child.match.group(1) if i1 == 0 else "?"
        w1_payload = child.before if i1 == 0 else ""
        i2 = child.expect([r"UTF8-W2 bytes=(\d+)\r*\n", r"UTF8-FAIL"],
                          timeout=60)
        w2 = child.match.group(1) if i2 == 0 else "?"
        w2_payload = child.before if i2 == 0 else ""
        child.expect(r"UTF8-END", timeout=30)
        child.expect(PROMPT, timeout=30)
        seq = HANZI.encode("utf-8").decode("latin-1")
        n1 = w1_payload.count(seq)
        n2 = w2_payload.count(seq)
        n_seq = n1 + n2
        ok = (w1 == "522" and w2 == "522" and n_seq == 348)
        print(f"[utf8] w1={w1} w2={w2} intact-sequences={n_seq}/348 "
              + f"(w1={n1} w2={n2}) -> "
              + ("PASS" if ok else "FAIL"))
        results["utf8"] = ok

        if dump_dir:
            os.makedirs(dump_dir, exist_ok=True)
            shot = os.path.join(dump_dir, "utf8_after.ppm")
            hmp(child, f"screendump {shot}", settle=1.5)
            print(f"[utf8] screendump -> {shot}")

        child.sendline("poweroff")
        try:
            child.expect(pexpect.EOF, timeout=30)
        except pexpect.TIMEOUT:
            pass
    except pexpect.TIMEOUT as e:
        print(f"[probe] TIMEOUT: {e}")
        return 2
    except pexpect.EOF:
        print("[probe] EOF (guest died?)")
        return 3
    finally:
        logf.close()
        try:
            child.close(force=True)
        except Exception:
            pass
        if child.status is not None and child.signalstatus is None:
            pass

    print("=" * 60)
    for k in ("read0", "pipe", "utf8"):
        print(f"  {k:6s} : {'PASS' if results.get(k) else 'FAIL'}")
    all_ok = all(results.get(k) for k in ("read0", "pipe", "utf8"))
    print(f"[wp_p2_batch_a] {'ALL PASS' if all_ok else 'FAILURES PRESENT'}")
    print(f"[wp_p2_batch_a] serial log: {log_path}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
