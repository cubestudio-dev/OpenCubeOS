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
"""
import sys, os, re, time, signal, socket, threading
import pexpect

# Setup LD_LIBRARY_PATH + QEMU paths
# Toolchain layout (no-root sandbox install): OC_TOOLS points at the
# extracted toolchain root (usr/bin, usr/lib, usr/share). Override with
# the OC_TOOLS environment variable when your layout differs.
OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
LIB_PATHS = [f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib"]
ENV_LD = ":".join(LIB_PATHS)

QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"

def strip_ansi(s):
    s = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', s)
    s = re.sub(r'\x1b\[\?\d+[a-zA-Z]', '', s)
    s = re.sub(r'c\x1b', '', s)
    return s

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

    try:
        # Wait for boot prompt
        child.expect(r"oc>\s*", timeout=30)
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
                child.expect(r"oc>\s*", timeout=cmd_timeout)
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
        # Strip the last 'oc>' prompt line
        print(content.rstrip())

if __name__ == "__main__":
    main()
