#!/usr/bin/env python3
"""Regenerate the website boot screenshots (shot-bios.png / shot-uefi.png)
from the RELEASED ISO (the published asset, SHA-verified before boot).

BIOS path: default SeaBIOS boot; UEFI path: OVMF pflash (CODE + a
writable VARS copy).  Each boots to the ush prompt, then the QEMU
monitor takes a screendump (PPM) which is converted to PNG via PIL.

Usage: python3 tools/audit/reshoot_boots.py --iso FILE --outdir DIR
"""
import argparse, os, shutil, socket, subprocess, sys, time
from PIL import Image

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = ":".join(filter(None, [
    f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib",
    ENV.get("LD_LIBRARY_PATH", "")]))

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]; s.close(); return p

def monitor_cmd(port, cmd, wait=0.4):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    time.sleep(wait)
    s.sendall((cmd + "\n").encode())
    time.sleep(1.2)
    try:
        data = s.recv(65536)
    except socket.timeout:
        data = b""
    s.close()
    return data

def shoot(tag, extra_args, iso, outdir):
    mon = free_port()
    ppm = os.path.join(outdir, f"shot-{tag}.ppm")
    args = [QEMU, "-m", "512", "-cdrom", iso, "-boot", "d",
            "-vga", "std", "-display", "none",
            "-monitor", f"tcp:127.0.0.1:{mon},server,nowait",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios"] + extra_args
    p = subprocess.Popen(args, env=ENV,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # wait for the ush prompt on the (serial) boot path - the VGA
        # screen shows the boot log + prompt; ~25s covers OVMF too
        time.sleep(40 if tag == "uefi" else 22)
        monitor_cmd(mon, f"screendump {ppm}")
        time.sleep(1.5)
    finally:
        p.terminate()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
    img = Image.open(ppm)
    img = img.convert("RGB").resize((800, 600), Image.LANCZOS)
    out = os.path.join(outdir, f"shot-{tag}.png")
    img.save(out, "PNG", optimize=True)
    os.remove(ppm)
    print(f"shot-{tag}.png: {img.size} -> {out}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--outdir", default="website/assets")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    # BIOS (default)
    shoot("bios", [], a.iso, a.outdir)
    # UEFI (OVMF): CODE rom + writable VARS copy
    ovmf_code = f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"
    if not os.path.exists(ovmf_code):
        for cand in ("OVMF_CODE_4M.fd", "OVMF.fd"):
            p = os.path.join(f"{OC_TOOLS}/usr/share/ovmf", cand)
            if os.path.exists(p):
                ovmf_code = p; break
    vars_copy = "/tmp/ovmf_vars.fd"
    shutil.copy(ovmf_code if "CODE" not in ovmf_code
                else ovmf_code.replace("CODE", "VARS"), vars_copy)
    if "CODE" in ovmf_code:
        shutil.copy(ovmf_code.replace("CODE", "VARS"), vars_copy)
    shoot("uefi", ["-drive", f"file={vars_copy},if=pflash,format=raw"],
          a.iso, a.outdir)
    return 0

if __name__ == "__main__":
    sys.exit(main())
