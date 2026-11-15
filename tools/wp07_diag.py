#!/usr/bin/env python3
"""standalone: gdb-inject the takeover record, reset, watch OCBL"""
import os
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from run_wp07_tests import (ENV, GdbStub, HD, OC, BOOT, ocst_record,
                            ocst_state)

sock = "/tmp/wp07-diag.sock"
if os.path.exists(sock):
    os.unlink(sock)
cmd = ["%s/usr/bin/qemu-system-x86_64" % OC, "-m", "512",
       "-L", "%s/usr/share/qemu" % OC, "-L", "%s/usr/share/seabios" % OC,
       "-drive", "file=%s,if=ide,index=0,media=disk,format=raw" % HD,
       "-drive", "file=%s,if=ide,index=1,media=disk,format=raw" % BOOT,
       "-net", "none", "-serial", "stdio", "-display", "none",
       "-monitor", "unix:%s,server,nowait" % sock,
       "-gdb", "tcp::1235"]
p = subprocess.Popen(cmd, env=ENV, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT)

import fcntl
fl = fcntl.fcntl(p.stdout, fcntl.F_GETFL)
fcntl.fcntl(p.stdout, fcntl.F_SETFL, fl | os.O_NONBLOCK)
buf = b""


def pump(t=1.0):
    global buf
    end = time.time() + t
    while time.time() < end:
        try:
            d = p.stdout.read(65536)
            if d:
                buf += d
        except Exception:
            pass
        time.sleep(0.05)


print("[diag] booting OCOS ...")
pump(30)
while b"ush> " not in buf:
    pump(3)
print("[diag] ush reached")

print("[diag] gdb inject ...")
stub = GdbStub(port=1235)
rec = ocst_record(kind=1, boot_fails=2, routed=0, stall=6, seq=9)
state = ocst_state(seq=10, stage=6, armed=1, kflags=0x02)
stub.write_mem(0x9B000, rec)
stub.write_mem(0x98000, state)
back = stub.read_mem(0x9B000, 28)
print("[diag] readback 0x9B000:", back.hex())
print("[diag] expected       :", rec.hex())
print("[diag] match:", back == rec)
stub.close()
print("[diag] gdb detached")

print("[diag] monitor reset ...")
mon = socket.socket(socket.AF_UNIX)
mon.connect(sock)
mon.settimeout(3)
time.sleep(0.3)
try:
    mon.recv(4096)
except Exception:
    pass
mon.sendall(b"system_reset\n")
time.sleep(0.5)
try:
    mon.recv(4096)
except Exception:
    pass
mon.close()

print("[diag] watching serial ...")
pump(20)
text = buf.decode("utf-8", "replace")
for needle in ("fresh boot", "already judged", "BOOT_FAILED record pending",
               "auto-booting OCIRE", "FAILED TO BOOT", "Rollback",
               "OCIRE - OpenCubeOS"):
    print("[diag] %-28s %s" % (needle, "FOUND" if needle in text else "-"))
p.terminate(force=True)
time.sleep(0.5)
print("[diag] tail:")
print(text[-1500:])
