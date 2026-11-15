#!/usr/bin/env python3
"""quick interactive probe: boot to OCIRE, run a few commands"""
import os
import sys
import pexpect

OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
root = os.getcwd()
cmd = ("%s/usr/bin/qemu-system-x86_64 -m 512 -L %s/usr/share/qemu -L %s/usr/share/seabios "
       "-drive file=%s/build/ocire/ocire-hd.img,if=ide,index=0,media=disk,format=raw "
       "-drive file=%s/build/ocire/ocire-boot.img,if=ide,index=1,media=disk,format=raw "
       "-drive file=%s/build/ocire/esp.img,if=ide,index=2,media=disk,format=raw "
       "-net none -serial stdio -display none -no-reboot"
       % (OC, OC, OC, root, root, root))
env = dict(os.environ)
env["LD_LIBRARY_PATH"] = "%s/usr/lib/x86_64-linux-gnu:%s/lib/x86_64-linux-gnu" % (OC, OC)
c = pexpect.spawn(cmd, env=env, timeout=120, encoding="utf-8", codec_errors="replace")
c.logfile = sys.stdout
try:
    c.expect("Booting Open Cube OS")
    c.send("\x1b[A")
    c.expect(r"OCIRE recovery \(slot A")
    c.send("\r")
    c.expect("Ready. Type 'help'")
    for command in sys.argv[1:]:
        c.sendline(command)
        c.expect("ocire>")
except Exception as e:
    print("ERR", e)
c.terminate(force=True)
