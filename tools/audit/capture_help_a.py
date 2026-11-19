#!/usr/bin/env python3
"""Capture `help -a` from the running system (QEMU) into a file.
Usage: python3 tools/audit/capture_help_a.py [--out FILE] [--iso ISO]"""
import argparse, os, sys, pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="evidence/presentation-audit/help-a-output.txt")
    ap.add_argument("--iso", default="build/opencube.iso")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = ":".join(filter(None, [
        f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib",
        env.get("LD_LIBRARY_PATH", "")]))
    c = pexpect.spawn(QEMU, ["-m", "512", "-cdrom", a.iso, "-nographic",
                             "-no-reboot",
                             "-L", f"{OC_TOOLS}/usr/share/qemu",
                             "-L", f"{OC_TOOLS}/usr/share/seabios"],
                      env=env, timeout=120, encoding="utf-8", codec_errors="replace")
    try:
        c.expect(r"ush>", timeout=120)
        c.sendline("help -a")
        c.expect(r"ush>", timeout=60)
        out = c.before or ""
        # strip leading echo
        if "\r\n" in out:
            out = out.split("\r\n", 1)[1]
        with open(a.out, "w") as f:
            f.write(out)
        print(f"captured {a.out}: {len(out)} bytes")
        return 0
    finally:
        c.close(force=True)

if __name__ == "__main__":
    sys.exit(main())
