#!/usr/bin/env python3
# Investigation tool (no fix code): boot the main ISO, open nano,
# toggle the IME with the serial F2 sequence (ESC[12~), type "nihao"
# one letter at a time, and capture what a REAL serial terminal would
# have shown. Counts the full status-bar reprints per keystroke.
import sys, time
import pexpect

QEMU = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL = "/home/z/opt/extract/usr/share/qemu"
SEA = "/home/z/opt/extract/usr/share/seabios"
ISO = "/home/z/oc-os/build/opencube.iso"
OUT = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix/repro_nano_ime_scroll.bin"

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 512 -cdrom {ISO} -boot d "
       f"-no-reboot -display none -serial stdio -monitor none -vga std")

child = pexpect.spawn(cmd, timeout=150, encoding=None, codec_errors="replace")
raw = bytearray()
failures = []   # Fake-green #65: collected verdicts; any entry => exit 1
def eat(pat, tmo=30, keep=20000):
    child.expect(pat, timeout=tmo)
    raw.extend(child.before[-keep:])
    raw.extend(child.match.group(0) if child.match else b"")
try:
    eat(b"ush>", 140)
    time.sleep(0.4)
    child.send(b"nano t.txt\r")
    eat(b"save", 30)          # the nano header bar
    time.sleep(0.5)
    # F2 = ESC[12~ (the serial CSI table)
    child.send(b"\x1b[12~")
    time.sleep(0.6)
    eat(b"IME", 10)
    # type n i h a o slowly, draining output after each letter
    for ch in b"nihao":
        child.send(bytes([ch]))
        time.sleep(0.4)
        try:
            i = child.expect([b"ime", pexpect.TIMEOUT], timeout=1.2)
            raw.extend(child.before[-4000:])
            if i == 0:
                raw.extend(b"ime")
        except Exception as e:
            failures.append(f"drain after typing {chr(ch)!r} errored: {e}")
    time.sleep(0.6)
    try:
        child.expect(pexpect.TIMEOUT, timeout=1.0)
        raw.extend(child.before[-4000:])
    except Exception as e:
        failures.append(f"final drain before quit errored: {e}")
    # count what came in the last seconds
    child.send(b"\x03")       # ctrl-c-ish; then quit without saving
    time.sleep(0.4)
    child.send(b"\x18")       # ^X
    time.sleep(0.6)
    try:
        eat(b"ush>", 8)
    except Exception as e:
        failures.append(f"no ush> prompt after quit: {e}")
except Exception as e:
    # Fake-green #65: a broken session is a verdict, not a traceback -
    # record it so the evidence dump + failure list still run below.
    print(f"SESSION FAILED: {e}", flush=True)
    failures.append(f"session exception: {e}")
finally:
    child.terminate(force=True)

data = bytes(raw)
open(OUT, "wb").write(data)
print("captured", len(data), "bytes ->", OUT)
if not data:
    failures.append("capture is empty - session produced no bytes")

BAR = b"^O save ^X exit ^K cut ^R copy ^U paste ^G help F2 ime"
n_bar = data.count(BAR)
print(f"status-bar reprints during the session: {n_bar}")
# strip lines (composition updates)
strips = [l for l in data.split(b"\n") if b"IME:ON]" in l or b"ni" in l and b":" in l]
print("---- sample of what the terminal showed (tail) ----")
tail = data[-1600:].decode("utf-8", errors="replace")
for line in tail.splitlines()[-30:]:
    print("  |", line[:110])

# Fake-green #65: propagate the verdict - any failed step => exit 1.
if failures:
    print(f"\n== wp06g3_repro_nano_scroll: {len(failures)} step(s) FAILED ==")
    for f in failures:
        print("  FAIL:", f)
    sys.exit(1)
print("\n== wp06g3_repro_nano_scroll: capture session completed OK ==")
sys.exit(0)
