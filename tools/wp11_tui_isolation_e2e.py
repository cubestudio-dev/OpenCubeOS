#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-11-crash_recovery E2E: TUI crash isolation (timing-driven).

The TUI renders on the VGA framebuffer - the serial console only
carries the shell and the kernel notices, so the navigation is sent
as a timed key sequence and the ASSERTIONS are serial-side:
  1. CONTAINED notice after pressing C (the page raised a real ring-0
     fault inside TUI code; the guard longjmp'ed back)
  2. NO kernel panic banner
  3. F10 still exits the desktop to the shell prompt (the desktop
     loop is alive after the containment)
  4. `crashlog` lists the contained fault afterwards
"""
import sys, os, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
ISO = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"

cmd = [QEMU_BIN, "-m", "512M", "-cdrom", ISO, "-boot", "d",
       "-L", QEMU_DATADIR, "-L", SEABIOS_DIR,
       "-vga", "std", "-display", "none", "-serial", "mon:stdio",
       "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]

c = pexpect.spawn(cmd[0], cmd[1:], timeout=200, encoding="latin-1",
                  dimensions=(30, 100))
out = []
def drain(t):
    """collect serial output for t seconds without failing"""
    end = time.time() + t
    while time.time() < end:
        try:
            i = c.expect([r".+"], timeout=max(0.2, end - time.time()))
            if i == 0:
                out.append(c.match.group(0))
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass
    return "".join(out)

def wait(pat, t=120, what=""):
    i = c.expect([pat, pexpect.TIMEOUT, pexpect.EOF], timeout=t)
    if i != 0:
        print(f"\n[E2E-FAIL] waiting for {what or pat}")
        print("---- last serial output ----")
        print("".join(out)[-2000:])
        c.terminate(force=True)
        sys.exit(1)
    out.append(c.match.group(0))

DOWN = "\x1b[B"
ENTER = "\r"
ESC = "\x1b"
F10 = "\x1b[26~"

ok = True
try:
    wait(r"Type 'help' for commands", 150, "boot banner")
    c.sendline("tui")
    drain(5)                       # desktop renders (VGA-side)
    # root order: Files(5) Processes(8) ControlPanel(10)
    c.send(DOWN); time.sleep(0.3)
    c.send(DOWN); time.sleep(0.3)
    c.send(ENTER); time.sleep(1.5)  # enter Control Panel
    # panel order: Hardware Storage Network Time CrashTest
    for _ in range(4):
        c.send(DOWN); time.sleep(0.3)
    c.send(ENTER); time.sleep(1.5)  # open the crash-test page
    c.send("c")                    # raise the ring-0 fault
    wait(r"CONTAINED", 30, "CONTAINED notice on serial")
    drain(2)
    # the desktop loop must still be alive: Esc pops the page, F10 exits
    c.send(ESC); time.sleep(1.0)
    c.send(F10)
    wait(r"oc>", 40, "shell prompt back (desktop alive after containment)")
    c.sendline("crashlog")
    wait(r"#UD Invalid Opcode|entries", 30, "crashlog entry")
    drain(2)
    full = "".join(out)
    if "KERNEL PANIC" in full:
        print("\n[E2E-FAIL] kernel panicked - containment broken")
        ok = False
    else:
        print("\n[E2E-PASS] TUI crash contained; desktop answered F10; "
              "shell back; crashlog recorded")
except SystemExit:
    raise
except Exception as e:
    print(f"\n[E2E-FAIL] {e}")
    ok = False
finally:
    try: c.terminate(force=True)
    except Exception: pass

sys.exit(0 if ok else 1)
