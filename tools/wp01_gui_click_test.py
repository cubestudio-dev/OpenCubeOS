#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-fix-critical-security: BUG #113 acceptance - REAL pointer clicks.

The T4 battery proves button geometry via the OCSECL `click` probe
(which bypasses the event rings).  This test drives the REAL pointer
path: QMP input-send-event injects PS/2 relative moves + button
press/release into the guest, the OCSE input rings batch them, and the
press-edge LATCH (the #113 fix) must fire the button action.  Pre-fix,
a press+release pair landing in one poll drain was silently dropped.
(HMP mouse_move is dead in this QEMU 10/-display none combo - verified
identically on the unmodified g3 release ISO - so QMP is the injector.)
Exit 0 = click worked; 1 = click lost (BUG #113 regression)."""
import os, socket, sys, time, json
import pexpect

BUILD = "/home/z/oc-public/build"
DISK = f"{BUILD}/ocse-test-disk64.raw"
QMP = f"{BUILD}/qmp113.sock"
OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")

if not os.path.exists(DISK) or os.path.getsize(DISK) != 64 * 1024 * 1024:
    with open(DISK, "wb") as f:
        f.truncate(64 * 1024 * 1024)
if os.path.exists(QMP):
    os.unlink(QMP)

CMD = (f"{OC_TOOLS}/usr/bin/qemu-system-x86_64 "
       f"-L {OC_TOOLS}/usr/share/qemu "
       f"-L {OC_TOOLS}/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-qmp unix:{QMP},server,nowait -vga std")

class Qmp:
    def __init__(self, path, wait=30):
        for _ in range(wait * 2):
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                self.s.settimeout(0.4)
                self._rd()
                self.s.sendall(b'{ "execute": "qmp_capabilities" }\n')
                self._rd()
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("qmp socket never came up")
    def _rd(self):
        buf = b""
        try:
            while True:
                c = self.s.recv(65536)
                if not c:
                    break
                buf += c
        except socket.timeout:
            pass
        return buf.decode()
    def ev(self, dx=0, dy=0, btn=None, down=None):
        evs = [{"type": "rel", "data": {"axis": "x", "value": dx}},
               {"type": "rel", "data": {"axis": "y", "value": dy}}]
        if btn is not None:
            evs.append({"type": "btn", "data": {"down": down, "button": btn}})
        self.s.sendall(json.dumps({"execute": "input-send-event",
                                   "arguments": {"events": evs}}).encode() + b"\n")
        self._rd()
        time.sleep(0.2)

def click_at(qmp, x, y):
    """PS/2 is RELATIVE: rush to the top-left corner, then the deltas
    land the pointer exactly on (x, y) (OCSE clamps)."""
    for _ in range(6):
        qmp.ev(dx=-500, dy=-500)
    for _ in range(2):
        qmp.ev(dx=x // 2, dy=y // 2)
    qmp.ev(btn="left", down=True)    # press
    qmp.ev(btn="left", down=False)   # release (same drain = the #113 race)

def main():
    child = pexpect.spawn(CMD, timeout=180, encoding="utf-8",
                          codec_errors="replace")
    child.logfile = sys.stdout
    qmp = None
    try:
        child.expect("environment up: GUI mode", timeout=150)
        print("\n[#113] GUI up; REAL PS/2 click on 'Command-line install'")
        qmp = Qmp(QMP)
        click_at(qmp, 190, 542)   # T4-proven coordinates for goto-cl
        idx = child.expect([r"switched: CLI mode", pexpect.TIMEOUT], timeout=25)
        if idx == 0:
            print("[#113 PASS] real pointer click fired the button action")
            return 0
        print("[#113 FAIL] click lost - button action never fired (BUG #113)")
        return 1
    except Exception as e:
        print(f"[#113 FAIL] {e}")
        return 1
    finally:
        if qmp:
            try:
                qmp.s.close()
            except Exception:
                pass
        child.terminate(force=True)

if __name__ == "__main__":
    sys.exit(main())
