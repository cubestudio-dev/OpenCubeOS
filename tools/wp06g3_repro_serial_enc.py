#!/usr/bin/env python3
# Investigation tool (no fix code): boot the main ISO, drive `lang zh`
# over serial, capture the RAW BYTES that come back, then decode them
# three ways (utf-8 / gbk / gb2312) to establish what a real terminal
# in each encoding would have shown on the user's real machine.
import sys, time
import pexpect

QEMU = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL = "/home/z/opt/extract/usr/share/qemu"
SEA = "/home/z/opt/extract/usr/share/seabios"
ISO = "/home/z/oc-os/build/opencube.iso"
OUT = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix/repro_serial_lang_zh.bin"

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 512 -cdrom {ISO} -boot d "
       f"-no-reboot -display none -serial stdio -monitor none -vga std")

child = pexpect.spawn(cmd, timeout=150, encoding=None, codec_errors="replace")
raw = bytearray()
failures = []   # Fake-green #65: collected verdicts; any entry => exit 1
try:
    child.expect(b"ush>", timeout=140)
    raw += child.before + b"ush>"
    time.sleep(0.5)
    child.send(b"lang zh\r")
    child.expect(b"ush>", timeout=30)
    raw += child.before + b"ush>"
    time.sleep(0.3)
    child.send(b"help\r")
    child.expect(b"ush>", timeout=30)
    raw += child.before + b"ush>"
except Exception as e:
    # Fake-green #65: a broken session is a verdict, not a traceback -
    # record it so the evidence dump + failure list still run below.
    print(f"SESSION FAILED: {e}", flush=True)
    failures.append(f"session exception: {e}")
finally:
    child.terminate(force=True)

open(OUT, "wb").write(bytes(raw))
print("captured", len(raw), "bytes ->", OUT)
if not raw:
    failures.append("capture is empty - session produced no bytes")

# isolate the lang zh reply segment
data = bytes(raw)
i = data.find(b"lang zh")
if i < 0:
    failures.append("no 'lang zh' reply found in capture")
seg = data[i:i+120]
print("raw bytes after 'lang zh':")
print(" ", seg[:80])
# decode attempts
for enc in ("utf-8", "gbk", "gb18030"):
    try:
        txt = seg.decode(enc, errors="replace")
        print(f"as {enc}: {txt.splitlines()[0][:60] if txt.splitlines() else txt[:60]!r}")
    except Exception as e:
        print(f"as {enc}: ERROR {e}")
        failures.append(f"decode as {enc} errored: {e}")
# authoritative: what does the UTF-8 payload look like when a GBK terminal renders it
u = "控制台输出语言：中文"
print("expected utf-8 bytes :", u.encode("utf-8").hex(" "))
print("utf-8 bytes as-gbk   :", u.encode("utf-8").decode("gbk", errors="replace"))

# Fake-green #65: propagate the verdict - any failed step => exit 1.
if failures:
    print(f"\n== wp06g3_repro_serial_enc: {len(failures)} step(s) FAILED ==")
    for f in failures:
        print("  FAIL:", f)
    sys.exit(1)
print("\n== wp06g3_repro_serial_enc: capture session completed OK ==")
sys.exit(0)
