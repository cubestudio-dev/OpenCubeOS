#!/usr/bin/env python3
"""WP-04 G11a ssh/sshd e2e (3.0.0 adaptation of the v2.2.0 rigs).

A: guest `ssh 10.0.2.2 2222 oc oc` -> host paramiko_sshd.py -> expect
   "[ssh] authenticated (USERAUTH_SUCCESS)" + "[ssh] exec output: hello-from-OpenCubeOS-kernel-ssh".
B: guest `sshd 22 oc oc` -> host paramiko client through the runner's
   hostfwd (tcp::2223-:22) -> exec uname -> expect "Open Cube OS".
"""
import os, sys, time, subprocess, socket
import pexpect
import paramiko

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    iso = sys.argv[1]
    # host ssh server for test A
    server = subprocess.Popen(
        ["python3", "tools/paramiko_sshd.py", "2222"],
        cwd="/home/z/oc-public",
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio",
            "-no-reboot", "-snapshot",
            "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
            "-object", "filter-dump,id=f0,netdev=n1,file=/tmp/g11a_net.pcap",
            "-device", "e1000,netdev=n1",
            "-drive", "if=ide,format=raw,file=build/etc.img"]
    child = pexpect.spawn(QEMU, args, timeout=240, encoding="latin-1",
                          codec_errors="replace", env=env)
    child.logfile_read = sys.stdout
    ok = True
    try:
        child.expect(r"ush>\s", timeout=240)
        # ---- A: guest ssh client -> host paramiko server ----
        child.sendline("ssh 10.0.2.2 2222 oc oc")
        i = child.expect([r"\[ssh\] authenticated \(USERAUTH_SUCCESS\)",
                         pexpect.TIMEOUT, pexpect.EOF], timeout=180)
        if i == 0:
            print("\n[g11a] PASS ssh-auth")
        else:
            print("\n[g11a] FAIL ssh-auth"); ok = False
        j = child.expect([r"\[ssh\] exec output: hello-from-OpenCubeOS-kernel-ssh",
                         pexpect.TIMEOUT, pexpect.EOF], timeout=120)
        if j == 0:
            print("\n[g11a] PASS ssh-exec")
        else:
            print("\n[g11a] FAIL ssh-exec"); ok = False
        child.expect(r"ush>\s", timeout=120)
        # ---- B: guest sshd <- host paramiko client ----
        child.sendline("dhcp")
        child.expect(r"ush>\s", timeout=60)
        child.sendline("sshd 22 oc oc")
        k = child.expect([r"listening on port 22", pexpect.TIMEOUT, pexpect.EOF],
                         timeout=180)
        if k == 0:
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
        m = child.expect([r"exec: captured \d+ bytes of output",
                         pexpect.TIMEOUT, pexpect.EOF], timeout=420)
        if m == 0:
            print("\n[g11a] PASS sshd-capture-log")
        else:
            print("\n[g11a] FAIL sshd-capture-log"); ok = False
        child.expect(r"ush>\s", timeout=120)
        child.sendline("poweroff")
    finally:
        try:
            child.expect(pexpect.EOF, timeout=60)
        except Exception:
            child.terminate(force=True)
        server.terminate()
    print("\n[g11a] " + ("ALL PASS" if ok else "FAILURES"))
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
