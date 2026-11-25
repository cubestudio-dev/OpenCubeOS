#!/usr/bin/env python3
"""Capture the live `help -a` output from the built ISO into
evidence/help_a_runtime.txt (the gen_api_docs.py capture input)."""
import os, socket, subprocess, sys, time

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ISO = sys.argv[1]
OUT = sys.argv[2]
SOCK = "/tmp/cap_ha.sock"

if os.path.exists(SOCK):
    os.unlink(SOCK)
env = dict(os.environ)
env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                          f"{OC_TOOLS}/lib/x86_64-linux-gnu")
args = ["-m", "512M", "-cdrom", ISO, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu",
        "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-display", "none", "-monitor", "none", "-no-reboot",
        "-serial", f"unix:{SOCK},server=on,wait=on"]
child = subprocess.Popen([QEMU] + args, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, env=env)
sock = None
for _ in range(120):
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCK)
        break
    except OSError:
        time.sleep(0.5)
if sock is None:
    child.kill(); sys.exit(1)
sock.settimeout(1.0)
buf = b""
def pump(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        try:
            d = sock.recv(65536)
            if not d: break
            buf += d
        except socket.timeout:
            pass
pump(1.0)
end = time.time() + 120
while b"ush>" not in buf and time.time() < end:
    pump(1.0)
# WP-compat B9: stream mode for the capture (the ush pager off)
sock.sendall(b"export PAGER=off\n")
pump(1.0)
sock.sendall(b"help -a\n")
end = time.time() + 40
while time.time() < end:
    before = len(buf)
    pump(2.0)
    if b"write  [" in buf or (len(buf) == before and b"registry" in buf):
        break
pump(2.0)
sock.close(); child.kill(); child.wait()

text = buf.decode("utf-8", "replace")
# strip serial CR echo; keep the capture from the built-in list
# through the registry listing (the shape gen_api_docs parses)
text = text.replace("\r\n", "\n")
open(OUT, "w").write(text)
n = text.count("\n")
print(f"captured {n} lines -> {OUT}")
print("title:", [l for l in text.split("\n") if "registry (A-Z" in l][:1])
