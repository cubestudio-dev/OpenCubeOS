#!/usr/bin/env python3
"""WP-04 G11a ssh/sshd e2e (3.0.0 adaptation of the v2.2.0 rigs).

A: guest `ssh 10.0.2.2 2222 oc oc` -> host paramiko_sshd.py -> expect
   "[ssh] authenticated (USERAUTH_SUCCESS)" + "[ssh] exec output: hello-from-OpenCubeOS-kernel-ssh".
B: guest `sshd 22 oc oc` -> host paramiko client through the runner's
   hostfwd (tcp::2223-:22) -> exec uname -> expect "Open Cube OS".

K-09 root fix (WP-severe-fix-and-closure): the guest is driven over a
unix-socket serial line now (was `-serial stdio` + pexpect). The stdio
chardev measurably starves the QEMU main loop in this sandbox (see the
HTTPS E2E's b-k09 evidence: a mon:stdio boot answers ping 2/4 where the
same ISO answers 4/4 over a unix-socket serial) and the SSH handshakes
then missed their windows - the same kernel, the same ISO, a healthy
network stack, an unhealthy harness channel.
"""
import os, sys, time, subprocess, socket
import paramiko

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
def _oc_tools_layout(root):
    """Resolve the toolchain layout.

    The historical default (~/opt/extract) is a dpkg -x EXTRACTION ROOT,
    so the tools live at <root>/usr/bin and the firmware at
    <root>/usr/share.  A system install has OC_TOOLS=/usr and the tools
    live at /usr/bin directly - joining "/usr" + "/usr/bin" produced the
    nonsense path "/usr/usr/bin/qemu-system-x86_64" (live-caught running
    the gate on the permanent host, which does not use the extract
    layout).  Detect which one this is and return (bindir, sharedir,
    libdir) accordingly, falling back to PATH for the binary.
    """
    import shutil
    bindir = os.path.join(root, "usr", "bin")
    sharedir = os.path.join(root, "usr", "share")
    libdir = os.path.join(root, "usr", "lib", "x86_64-linux-gnu")
    if not os.path.isdir(bindir):
        # system layout: <root>/bin
        cand = os.path.join(root, "bin")
        if os.path.isdir(cand):
            bindir = cand
            sharedir = os.path.join(root, "share")
            libdir = os.path.join(root, "lib", "x86_64-linux-gnu")
    return bindir, sharedir, libdir

_OC_BINDIR, _OC_SHAREDIR, _OC_LIBDIR = _oc_tools_layout(OC_TOOLS)
import shutil
QEMU = os.path.join(_OC_BINDIR, "qemu-system-x86_64")
if not os.path.exists(QEMU):
    QEMU = shutil.which("qemu-system-x86_64") or QEMU

def main():
    iso = sys.argv[1]
    # host ssh server for test A
    server = subprocess.Popen(
        ["python3", "tools/paramiko_sshd.py", "2222"],
        cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{_OC_LIBDIR}:"
                                  f"{os.path.join(OC_TOOLS, 'lib', 'x86_64-linux-gnu')}")
    ser = "/tmp/g11a_serial.sock"
    if os.path.exists(ser):
        os.unlink(ser)
    child = subprocess.Popen([QEMU,
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", os.path.join(_OC_SHAREDIR, "qemu"),
        "-L", os.path.join(_OC_SHAREDIR, "seabios"),
        "-display", "none", "-monitor", "none", "-no-reboot", "-snapshot",
        "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
        "-object", "filter-dump,id=f0,netdev=n1,file=/tmp/g11a_net.pcap",
        "-device", "e1000,netdev=n1",
        "-drive", "if=ide,format=raw,file=build/etc.img",
        "-serial", f"unix:{ser},server=on,wait=on",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    sock = None
    for _ in range(120):
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(ser)
            break
        except OSError:
            sock = None
            time.sleep(0.5)
    if sock is None:
        child.kill(); server.terminate()
        print("[g11a] FAIL harness: serial socket never came up")
        return 1
    sock.settimeout(0.3)
    buf = b""

    def pump(t):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            try:
                d = sock.recv(4096)
                if d:
                    buf += d
                    sys.stdout.write(d.decode("utf-8", "replace"))
                    sys.stdout.flush()
            except socket.timeout:
                pass

    def wait_for(pat, timeout):
        nonlocal buf
        end = time.time() + timeout
        while time.time() < end:
            if pat in buf:
                return True
            pump(0.5)
        return pat in buf

    def wait_new(pat, timeout):
        # wait for `pat` appearing AFTER the last prompt already in buf
        nonlocal buf
        pre = len(buf)
        end = time.time() + timeout
        while time.time() < end:
            pump(0.5)
            if buf.find(pat, pre) != -1:
                return True
        return buf.find(pat, pre) != -1

    def sc(cmd, t=120):
        sock.sendall(cmd.encode() + b"\n")
        wait_new(b"ush>", t)

    ok = True
    try:
        wait_for(b"ush>", 240)
        pump(1.0)
        # ---- A: guest ssh client -> host paramiko server ----
        sc("ssh 10.0.2.2 2222 oc oc", 60)
        if wait_for(b"[ssh] authenticated (USERAUTH_SUCCESS)", 180):
            print("\n[g11a] PASS ssh-auth")
        else:
            print("\n[g11a] FAIL ssh-auth"); ok = False
        if wait_for(b"[ssh] exec output: hello-from-OpenCubeOS-kernel-ssh", 120):
            print("\n[g11a] PASS ssh-exec")
        else:
            print("\n[g11a] FAIL ssh-exec"); ok = False
        wait_for(b"ush>", 120)
        # ---- B: guest sshd <- host paramiko client ----
        sc("dhcp", 60)
        sc("sshd 22 oc oc", 60)
        if wait_for(b"listening on port 22", 180):
            print("\n[g11a] PASS sshd-listen")
        else:
            print("\n[g11a] FAIL sshd-listen"); ok = False
        # host client through the hostfwd
        cli_out = None
        # TCG: the guest's SSH handshake (KEX + AES + RSA) takes tens of
        # seconds per stage; a short banner/kex timeout aborts a session
        # the guest is still working on.  One attempt, generous windows.
        try:
            c = paramiko.SSHClient()
            c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            c.connect("127.0.0.1", port=2223, username="oc", password="oc",
                      timeout=90, banner_timeout=420, auth_timeout=420,
                      allow_agent=False, look_for_keys=False)
            _, out, _ = c.exec_command("uname", timeout=420)
            cli_out = out.read().decode("latin-1")
            c.close()
        except Exception as e:
            print(f"\n[g11a] sshd connect failed: {e}")
        if cli_out is not None and "Open Cube OS" in cli_out:
            print(f"\n[g11a] PASS sshd-exec (client saw: {cli_out.strip()!r})")
        else:
            print(f"\n[g11a] FAIL sshd-exec (client saw: {cli_out!r})"); ok = False
        if wait_for(b"exec: captured ", 420):
            print("\n[g11a] PASS sshd-capture-log")
        else:
            print("\n[g11a] FAIL sshd-capture-log"); ok = False
        wait_for(b"ush>", 120)
        sock.sendall(b"poweroff\n")
    finally:
        pump(5.0)
        try:
            child.wait(timeout=30)
        except Exception:
            child.kill()
        sock.close()
        server.terminate()
    print("\n[g11a] " + ("ALL PASS" if ok else "FAILURES"))
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
