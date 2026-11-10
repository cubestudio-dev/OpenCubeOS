# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#!/usr/bin/env python3
"""Run QEMU ISO boot and execute shell commands, capturing output.

Usage: python3 qemu_runner.py <iso_path> <command1> [command2 ...]

Commands prefixed with "@" are executed in the QEMU human monitor
(HMP) instead of the guest shell - the serial console is multiplexed
with the monitor via -serial mon:stdio, so Ctrl-A c switches to it.
This drives WP-10d tests: device_add/device_del (USB hot-plug),
sendkey (USB keyboard input), mouse_move (USB mouse input).

Env:
  OC_USB_SERIAL_ECHO=1  start a TCP echo server and attach QEMU as a
                        socket chardev client, so the guest's
                        usb-serial port loops back (TX -> RX)
  OC_CMD_TIMEOUT=300    per-command timeout

Screendump API (fake-green #66 - the screen was never machine-asserted
because every run_* / wp10_* / wp12_* E2E uses -display none):
  hmp_screendump(child, path)
        take a VGA screendump via the existing mon:stdio monitor
        channel (Ctrl-A c, 'screendump <path>', Ctrl-A c) and VERIFY
        the PPM actually landed on disk;
  screen_nonblank(path) / assert_screen_not_blank(path)
        coarse machine assertion on the dumped PPM: the frame must
        contain a minimum number of non-background pixels (i.e. not a
        blank/all-background screen);
  screen_diff(a, b) / assert_screen_changed(a, b)
        coarse frame-to-frame assertion: count pixels that changed
        between two screendumps, proving "the screen really moved
        between event A and event B" (e.g. an IME commit, a redraw).
  NOTE: all of this is deliberately coarse - it proves "the screen
  shows SOMETHING / CHANGED", it cannot read characters; a text-level
  assert_screen_contains(text) needs a font-render/OCR comparison and
  is follow-up work.
"""
import sys, os, re, time, signal, socket, threading
from collections import Counter
import pexpect

# Setup LD_LIBRARY_PATH + QEMU paths
# Toolchain layout (no-root sandbox install): OC_TOOLS points at the
# extracted toolchain root (usr/bin, usr/lib, usr/share). Override with
# the OC_TOOLS environment variable when your layout differs.
OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract/root")
LIB_PATHS = [f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib"]
ENV_LD = ":".join(LIB_PATHS)

QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"

# v2.0.0 boot flow: the kernel launcher boots straight into the ring-3
# ush shell, so the serial prompt is 'ush>' after boot and 'oc>' only
# after the user exits ush (or the launcher fails and falls back to the
# recovery shell). Both prompts are valid command-completion markers.
# OC_PROMPT overrides the pattern for special cases (e.g. waiting for a
# sub-shell or a login banner).
PROMPT_RE = os.environ.get("OC_PROMPT", r"(?:ush>|oc>)\s")

def strip_ansi(s):
    s = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', s)
    s = re.sub(r'\x1b\[\?\d+[a-zA-Z]', '', s)
    s = re.sub(r'c\x1b', '', s)
    return s

# ------------------------------------------------------------------
# v2.0.0 boot-flow adapter
# ------------------------------------------------------------------

def boot_to_recovery(child, boot_timeout=150, exit_timeout=60):
    """Drive a freshly spawned QEMU child from power-on to the oc>
    recovery-shell prompt.

    v2.0.0 boots straight into the ring-3 ush shell, so legacy tests
    that need the L0 recovery prompt must first leave ush.  On v1 ISOs
    (already at oc>) this is a no-op.  Returns the concatenated pre-
    prompt output, or raises pexpect.TIMEOUT/EOF.
    """
    import pexpect as _px
    i = child.expect([r"ush>\s", r"Type 'help' for commands",
                      _px.TIMEOUT, _px.EOF], timeout=boot_timeout)
    pre = child.before
    if i == 0:
        # v2 flow: leave the ring-3 shell, fall through to oc>
        child.sendline("exit")
        j = child.expect([r"Type 'help' for commands",
                          _px.TIMEOUT, _px.EOF], timeout=exit_timeout)
        pre += child.before + (child.match.group(0) if j == 0 else "")
        if j != 0:
            raise _px.TIMEOUT("recovery shell did not come up after exit")
    elif i == 1:
        pre += child.match.group(0)   # keep the banner text in the stream
    elif i != 1:
        raise _px.TIMEOUT("no shell prompt (ush> or recovery banner) within "
                          f"{boot_timeout}s")
    # v2.0.0: the banner match above fires mid-banner (the READY line),
    # BEFORE interactive_loop() has run shell_lineedit_init() - bytes sent
    # in that window can be dropped by the editor's queue reset.  Wait for
    # the actual oc> prompt so the line editor is armed before we return.
    k = child.expect([r"oc>\s", _px.TIMEOUT, _px.EOF], timeout=exit_timeout)
    if k != 0:
        raise _px.TIMEOUT("recovery prompt (oc>) did not appear within "
                          f"{exit_timeout}s")
    return pre + child.before

# ------------------------------------------------------------------
# OC_USB_SERIAL_ECHO: loopback server for the guest usb-serial port
# ------------------------------------------------------------------

class EchoServer(threading.Thread):
    """Accepts one TCP connection and echoes every byte back."""
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.sock = socket.socket(socket.AF_INET,
                                  socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET,
                             socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(1)

    def run(self):
        try:
            conn, _ = self.sock.accept()
            conn.settimeout(0.2)
            while True:
                try:
                    data = conn.recv(256)
                    if not data:
                        break
                    conn.sendall(data)
                except socket.timeout:
                    continue
                except OSError:
                    break
        except OSError:
            pass


# ------------------------------------------------------------------
# QEMU monitor (HMP) helpers - shared by run_qemu_commands and the
# screendump API (fake-green #66)
# ------------------------------------------------------------------

def hmp_cmd(child, hcmd):
    """Switch to the QEMU monitor, run one HMP command, switch back.
    After switching back to serial the guest shell does NOT reprint
    its prompt (it never left it), so just let the switch settle."""
    child.send("\x01c")                     # Ctrl-A c -> monitor
    child.expect(r"\(qemu\)", timeout=10)
    child.sendline(hcmd)
    child.expect(r"\(qemu\)", timeout=20)
    out = strip_ansi(child.before)
    child.send("\x01c")                     # Ctrl-A c -> serial
    time.sleep(0.5)
    return out


def hmp_screendump(child, path):
    """Take a VGA screendump through the QEMU monitor and verify it.

    Uses the existing mon:stdio multiplex (hmp_cmd).  Returns
    (ok, detail); ok is True only when the PPM exists AND is non-empty
    afterwards - a silent HMP error must never be mistaken for
    evidence."""
    try:
        out = hmp_cmd(child, "screendump " + path)
    except pexpect.TIMEOUT:
        return False, "monitor TIMEOUT during screendump"
    except pexpect.EOF:
        return False, "monitor EOF during screendump"
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return False, ("screendump produced no file (monitor said: "
                       f"{out.strip()[:120]!r})")
    return True, f"{os.path.getsize(path)} bytes written"


def read_ppm(path):
    """Minimal binary-PPM (P6) reader, stdlib only - no PIL required.
    Returns (width, height, raster_bytes).  Raises ValueError/OSError
    on anything that is not a complete P6 image."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: not a binary PPM (P6)")
    fields = []
    i = 2
    n = len(data)
    # i starts PAST the "P6" magic, so fields[0..2] = width, height, maxval
    while len(fields) < 3 and i < n:
        while i < n and data[i:i+1].isspace():
            i += 1
        if i < n and data[i:i+1] == b"#":      # comment to end of line
            while i < n and data[i:i+1] not in (b"\n", b"\r"):
                i += 1
            continue
        j = i
        while j < n and not data[j:j+1].isspace() and data[j:j+1] != b"#":
            j += 1
        if j == i:
            break
        fields.append(data[i:j])
        i = j
    if len(fields) < 3:
        raise ValueError(f"{path}: truncated PPM header")
    try:
        w, h, maxval = int(fields[0]), int(fields[1]), int(fields[2])
    except ValueError:
        raise ValueError(f"{path}: malformed PPM header {fields!r}")
    if maxval != 255:
        raise ValueError(f"{path}: unsupported maxval {maxval}")
    if not data[i:i+1].isspace():
        raise ValueError(f"{path}: no whitespace after maxval token")
    need = w * h * 3
    raster = data[i + 1:i + 1 + need]   # one whitespace byte after maxval
    if len(raster) < need:
        raise ValueError(f"{path}: truncated raster "
                         f"({len(raster)}/{need} bytes)")
    return w, h, raster


def screen_nonblank(path, min_fg_pixels=64, bg_tolerance=40):
    """COARSE machine assertion on a QEMU screendump (P6 PPM).

    Estimates the background colour as the dominant pixel value, then
    counts pixels differing from it by more than bg_tolerance on any
    channel.  ok = (foreground pixels >= min_fg_pixels), i.e. the frame
    is not just a blank background (one 8x16 VGA glyph is up to ~128
    lit pixels, a text screen has thousands).

    Known granularity limits (reported under #66): this proves "the
    screen shows non-background content"; it canNOT read characters,
    detect tofu blocks vs real hanzi, or compare layouts.  A
    text-level assert_screen_contains(text) needs a font-render/OCR
    comparison against the fb font and is follow-up work.
    Returns (ok, detail_string)."""
    try:
        w, h, raster = read_ppm(path)
    except (OSError, ValueError) as e:
        return False, f"screendump unreadable: {e}"
    total = w * h
    cnt = Counter()
    for p in range(0, total, 7):            # sample for the dominant bg
        cnt[raster[p*3:p*3+3]] += 1
    if not cnt:
        return False, "screendump has zero pixels"
    bg = cnt.most_common(1)[0][0]
    fg = 0
    for p in range(total):
        px = raster[p*3:p*3+3]
        if (abs(px[0] - bg[0]) > bg_tolerance
                or abs(px[1] - bg[1]) > bg_tolerance
                or abs(px[2] - bg[2]) > bg_tolerance):
            fg += 1
    ok = fg >= min_fg_pixels
    return ok, (f"{w}x{h} bg={tuple(bg)} fg={fg}/{total} "
                f"(min {min_fg_pixels})")


def assert_screen_not_blank(path, **kw):
    """screen_nonblank() as a raising assertion (for try/except-style
    E2E batteries).  AssertionError carries the measured detail."""
    ok, detail = screen_nonblank(path, **kw)
    if not ok:
        raise AssertionError(f"screen blank/unreadable ({path}): {detail}")
    return detail


def screen_diff(path_a, path_b, min_changed_pixels=64, pixel_tolerance=8):
    """COARSE frame-to-frame machine assertion between two screendumps.

    Counts pixels whose channel deltas exceed pixel_tolerance in any
    channel; ok = (changed >= min_changed_pixels).  This proves "the
    visible frame really moved between the two dumps" - catching the
    fake-green shape where the serial stream claims an event (commit,
    redraw, mode switch) but the screen never changed, or vice versa.

    Frames with different dimensions are simply not comparable ->
    (False, detail), never an exception, so callers can drop this in
    between existing screendump pairs.

    Granularity limits (same as screen_nonblank, #66): it cannot say
    WHAT changed, only that >= min_changed_pixels did.
    Returns (ok, detail_string)."""
    try:
        wa, ha, ra = read_ppm(path_a)
        wb, hb, rb = read_ppm(path_b)
    except (OSError, ValueError) as e:
        return False, f"screendump unreadable: {e}"
    if (wa, ha) != (wb, hb):
        return False, (f"frames not comparable: {path_a} is {wa}x{ha}, "
                       f"{path_b} is {wb}x{hb}")
    total = wa * ha
    changed = 0
    for p in range(total):
        pa = ra[p*3:p*3+3]
        pb = rb[p*3:p*3+3]
        if (abs(pa[0] - pb[0]) > pixel_tolerance
                or abs(pa[1] - pb[1]) > pixel_tolerance
                or abs(pa[2] - pb[2]) > pixel_tolerance):
            changed += 1
    ok = changed >= min_changed_pixels
    return ok, (f"{wa}x{ha} changed={changed}/{total} "
                f"(min {min_changed_pixels}, tol {pixel_tolerance})")


def assert_screen_changed(path_a, path_b, **kw):
    """screen_diff() as a raising assertion.  AssertionError carries
    the measured detail (including the both-frames-identical case)."""
    ok, detail = screen_diff(path_a, path_b, **kw)
    if not ok:
        raise AssertionError(f"screen did not change between dumps "
                             f"({path_a} -> {path_b}): {detail}")
    return detail


def run_qemu_commands(iso_path, commands, timeout_per_cmd=30):
    """Boot ISO, run commands, return all output."""
    cmd = [
        QEMU_BIN,
        "-m", "512M",
        "-cdrom", iso_path,
        "-boot", "d",
        "-L", QEMU_DATADIR,
        "-L", SEABIOS_DIR,
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        # User-mode networking + e1000 NIC (so the kernel e1000 driver finds it)
        "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
        "-object", "filter-dump,id=f0,netdev=n1,file=/tmp/guest_net.pcap",
        "-device", "e1000,netdev=n1",
    ]
    # WP-10d-pre: OC_BOOT_DISK=1 boots the first hard disk (-boot c) with
    # NO cdrom attached - used to prove a disk made with the in-system
    # abdisk / install commands boots on its own (rule-9 self-sufficiency).
    if os.environ.get("OC_BOOT_DISK", "").strip():
        cmd = [c for i, c in enumerate(cmd) if not (
            c == "-cdrom" or (c.startswith("-") is False and cmd[i-1] == "-cdrom"))]
        bi = cmd.index("-boot")
        cmd[bi + 1] = "c"
        print("[runner] OC_BOOT_DISK set: cdrom dropped, -boot c")
    # WP-10u: allow the guest to reboot (end-to-end update test drops
    # -no-reboot so the 8042 reset actually restarts the machine).
    if os.environ.get("OC_ALLOW_REBOOT", "").strip():
        print("[runner] OC_ALLOW_REBOOT set: -no-reboot dropped")
    else:
        cmd.insert(cmd.index("-boot"), "-no-reboot")
    # WP-10b: replace the default NIC model to exercise the WP-10b
    # drivers one at a time (QEMU models only e1000/e1000e/igb/rtl8139
    # from the WP-10b list):
    #   OC_NETDEV=e1000e|igb|rtl8139 python3.13 tools/qemu_runner.py ...
    netdev_model = os.environ.get("OC_NETDEV", "").strip()
    if netdev_model:
        cmd[cmd.index("e1000,netdev=n1")] = f"{netdev_model},netdev=n1"
        print(f"[runner] netdev model: {netdev_model}")
    # WP-09-fix5: optional extra QEMU args (e.g. attach the /etc disk):
    #   OC_EXTRA_QEMU_ARGS="-drive if=ide,format=raw,file=build/etc.img"
    extra = os.environ.get("OC_EXTRA_QEMU_ARGS", "").split()
    if extra:
        cmd.extend(extra)
        print(f"[runner] extra args: {extra}")
    # WP-10d debug: optional QEMU tracing (OC_TRACE=/path/events.txt)
    trace_ev = os.environ.get("OC_TRACE", "").strip()
    if trace_ev:
        cmd.extend(["-trace", f"events={trace_ev}",
                    "-trace", "file=/tmp/oc-trace.log"])
        print("[runner] QEMU trace enabled")
    # WP-10d: USB serial loopback (guest TX -> echo server -> guest RX)
    echo = None
    if os.environ.get("OC_USB_SERIAL_ECHO", "").strip():
        echo = EchoServer(4450)
        echo.start()
        cmd.extend(["-chardev",
                    "socket,id=ser0,host=127.0.0.1,port=4450",
                    "-device", "usb-serial,chardev=ser0"])
        print("[runner] usb-serial loopback: echo server on :4450")
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = ENV_LD

    print(f"[runner] starting QEMU: {os.path.basename(iso_path)}")
    print(f"[runner] will run {len(commands)} command(s): {commands}")
    
    cmd_timeout = int(os.environ.get("OC_CMD_TIMEOUT", "300"))
    child = pexpect.spawn(cmd[0], cmd[1:], env=env, timeout=cmd_timeout,
                          encoding='utf-8', codec_errors='replace')
    # real-time passthrough of all serial output to our stdout
    child.logfile_read = sys.stdout
    
    all_output = []

    try:
        # Wait for boot prompt (v2: ring-3 ush by default, oc> on fallback)
        child.expect(PROMPT_RE, timeout=30)
        boot_output = child.before
        all_output.append(("BOOT", strip_ansi(boot_output)))
        print(f"[runner] boot complete ({len(strip_ansi(boot_output))} chars)")

        for c in commands:
            if c.startswith("@"):
                hcmd = c[1:]
                print(f"[runner] HMP: {hcmd}")
                try:
                    out = hmp_cmd(child, hcmd)
                    all_output.append((c, out))
                except pexpect.TIMEOUT:
                    print(f"[runner] HMP TIMEOUT for '{hcmd}'")
                    all_output.append((c, "<<HMP TIMEOUT>>"))
                except pexpect.EOF:
                    print(f"[runner] HMP EOF for '{hcmd}'")
                    all_output.append((c, "<<HMP EOF>>"))
                continue
            print(f"[runner] sending: {c}")
            child.sendline(c)
            try:
                # 300s: 2048-bit DH modexp takes ~60-120s per op in QEMU (dhtest does 2)
                child.expect(PROMPT_RE, timeout=cmd_timeout)
                out = child.before
                # Strip the echoed command line at start
                lines = strip_ansi(out).split('\n', 1)
                if len(lines) > 1 and lines[0].strip() == c.strip():
                    out = lines[1]
                else:
                    out = strip_ansi(out)
                all_output.append((c, out))
                print(f"[runner] got {len(out)} chars for '{c}'")
            except pexpect.TIMEOUT:
                print(f"[runner] TIMEOUT for '{c}'")
                all_output.append((c, f"<<TIMEOUT after {cmd_timeout}s>>"))
            except pexpect.EOF:
                print(f"[runner] EOF for '{c}'")
                all_output.append((c, "<<EOF>>"))
                break
    finally:
        try:
            child.kill(signal.SIGKILL)
        except:
            pass
        child.close(force=True)
    
    return all_output

def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <iso> <cmd1> [cmd2 ...]")
        sys.exit(1)
    iso = sys.argv[1]
    cmds = sys.argv[2:]
    
    out = run_qemu_commands(iso, cmds, timeout_per_cmd=120)
    
    print("\n" + "=" * 60)
    print("OUTPUT SUMMARY")
    print("=" * 60)
    for label, content in out:
        print(f"\n--- [{label}] ---")
        # Strip the last prompt line ('ush>' in v2, 'oc>' after exit/fallback)
        print(content.rstrip())

if __name__ == "__main__":
    main()
