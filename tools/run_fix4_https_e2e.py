#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""HTTPS E2E gate for WP-10-AUDIT_P2-fix4 (restores the lost 7/7 session
coverage).  Runs the real OpenSSL-backed tools/https_test_server.py and
drives the kernel TLS 1.2 client (wget https) inside QEMU:

  [1] TLS handshake succeeds (kernel logs "Connecting ... (TLS)")
  [2] server negotiated TLS1.2 with cipher DHE-RSA-AES128-SHA256 (0x0067)
  [3] server received the encrypted HTTP GET for /hello.txt
  [4] server sent the canonical body (hello-from-opencube-tls)
  [5] kernel downloaded the body (wget save OK + cat content matches)
  [6] clean close: server received the kernel's close_notify
  [7] truncation guard: a hard-close server (no close_notify) makes the
      kernel fail PROMPTLY instead of hanging

Usage: python3 tools/run_fix4_https_e2e.py [outlog]
Exit code: number of failed checks."""
import os, re, socket, subprocess, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVD = "/home/z/work/fix4-evidence/g8"

BODY = "hello-from-opencube-tls\n"

def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, **kw)

def make_certs():
    os.makedirs(f"{EVD}/certs", exist_ok=True)
    c, k, d = f"{EVD}/certs/cert.pem", f"{EVD}/certs/key.pem", f"{EVD}/certs/dhparam.pem"
    if not (os.path.exists(c) and os.path.exists(k)):
        r = sh(f"openssl req -x509 -newkey rsa:2048 -keyout {k} -out {c} "
               f"-days 2 -nodes -subj '/CN=opencube-test' 2>/dev/null")
        if r.returncode != 0:
            raise RuntimeError("openssl cert gen failed: " + r.stderr.decode())
    # DH params pinned to RFC 3526 1024-bit MODP group (Oakley Group 1),
    # matching https_test_server.py's P_HEX; ASN.1 SEQUENCE { p, g=2 }.
    if not os.path.exists(d):
        sys.path.insert(0, f"{ROOT}/tools")
        from https_test_server import P_HEX  # the exact prime the server checks
        pbytes = bytes.fromhex(P_HEX)
        assert pbytes[0] & 0x80, "prime must be full 1024-bit"
        pint = b"\x00" + pbytes              # positive INTEGER encoding
        def tlv(tag, body):
            if len(body) < 128:
                return bytes([tag, len(body)]) + body
            if len(body) < 256:
                return bytes([tag, 0x81, len(body)]) + body
            return bytes([tag, 0x82]) + len(body).to_bytes(2, "big") + body
        der = tlv(0x30, tlv(0x02, pint) + tlv(0x02, b"\x02"))
        import base64
        b64 = base64.b64encode(der).decode()
        lines = [b64[i:i+64] for i in range(0, len(b64), 64)]
        with open(d, "w") as f:
            f.write("-----BEGIN DH PARAMETERS-----\n"
                    + "\n".join(lines)
                    + "\n-----END DH PARAMETERS-----\n")
    return c, k, d

def wait_port(port, t=4):
    # https_test_server.py serves exactly ONE connection then exits - a
    # TCP probe would consume it.  The server binds in its first lines,
    # so a plain sleep is the safe readiness wait.
    time.sleep(t)
    return True

def boot_guest(port, path_out):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    child = pexpect.spawn(QEMU, [
        "-m","512M","-cdrom",f"{ROOT}/build/opencube.iso","-boot","d",
        "-L",f"{OC_TOOLS}/usr/share/qemu","-L",f"{OC_TOOLS}/usr/share/seabios",
        "-vga","std","-display","none","-serial","mon:stdio",
        "-netdev","user,id=n1","-device","e1000,netdev=n1",
    ], timeout=150, encoding="latin-1", codec_errors="replace", env=env)
    child.expect(r"oc>\s*", timeout=150)
    def sc(cmd, t=90):
        child.sendline(cmd)
        child.expect(r"oc>\s*", timeout=t)
        return child.before or ""
    out = sc(f"wget https://10.0.2.2:{port}/hello.txt", 120)
    out += "\n" + sc("cat /wget_https.html", 60)
    child.close(force=True)
    open(path_out, "w").write(out)
    return out

def server_log_wait(p, deadline=120):
    buf = b""
    end = time.time() + deadline
    while time.time() < end:
        try:
            buf += os.read(p.stdout.fileno(), 65536)
        except Exception:
            break
        if b"close_notify" in buf or b"client closed" in buf or b"FAIL" in buf:
            time.sleep(0.4)
            try: buf += os.read(p.stdout.fileno(), 65536)
            except Exception: pass
            break
    return buf.decode("latin-1", "replace")

def main():
    cert, key, dh = make_certs()
    results = []

    # ---- normal server (checks 1-6) ----
    srv = subprocess.Popen(
        [sys.executable, f"{ROOT}/tools/https_test_server.py", cert, key, dh, "8443"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        if not wait_port(8443):
            results.append(("server started", False)); 
        else:
            kout = boot_guest(8443, f"{EVD}/https_kernel.log")
            slog = server_log_wait(srv)
            open(f"{EVD}/https_server.log", "w").write(slog)
            results.append(("1 TLS handshake (kernel connected)", "Connecting" in kout and "(TLS)" in kout))
            results.append(("2 TLS1.2 + 0x0067 negotiated", bool(re.search(r"TLS1\.2|TLS 1\.2", slog)) and bool(re.search(r"DHE-RSA-AES128-SHA256|0x0067|0x00,0x67", slog, re.I))))
            results.append(("3 encrypted GET received", "GET /hello.txt" in slog))
            results.append(("4 canonical body served (body=24)", "body=24" in slog))
            results.append(("5 kernel saved the body", "hello-from-opencube-tls" in kout))
            results.append(("6 close_notify check (see observe run)", True))  # covered by the observe pass below
    finally:
        srv.terminate()

    # ---- observe server (check 6: raw-byte close_notify assertion) ----
    srv_obs = subprocess.Popen(
        [sys.executable, f"{ROOT}/tools/https_test_server.py", cert, key, dh, "8451", "observe"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        time.sleep(4)
        kout6 = boot_guest(8451, f"{EVD}/https_kernel_observe.log")
        olog = server_log_wait(srv_obs)
        open(f"{EVD}/https_observe.log", "w").write(olog)
        for name, ok in results:
            pass
        results = [(n, v) for n, v in results if not n.startswith("6 ")]
        results.append(("6 kernel close_notify observed (raw recorder)",
                        "CLOSE_NOTIFY_ASSERT: PASS" in olog))
    finally:
        srv_obs.terminate()

    # ---- hard-close server (check 7) ----
    srv2 = subprocess.Popen(
        [sys.executable, f"{ROOT}/tools/https_test_server.py", cert, key, dh, "8444", "eof"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        if not wait_port(8444):
            results.append(("7 eof server started", False))
        else:
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                                      f"{OC_TOOLS}/lib/x86_64-linux-gnu")
            child = pexpect.spawn(QEMU, [
                "-m","512M","-cdrom",f"{ROOT}/build/opencube.iso","-boot","d",
                "-L",f"{OC_TOOLS}/usr/share/qemu","-L",f"{OC_TOOLS}/usr/share/seabios",
                "-vga","std","-display","none","-serial","mon:stdio",
                "-netdev","user,id=n1","-device","e1000,netdev=n1",
            ], timeout=150, encoding="latin-1", codec_errors="replace", env=env)
            child.expect(r"oc>\s*", timeout=150)
            t0 = time.time()
            child.sendline("wget https://10.0.2.2:8444/hello.txt")
            idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=45)
            dt = time.time() - t0
            out = (child.before or "") + (f"\n<<TIMEOUT after {dt:.0f}s>>" if idx == 1 else "")
            child.close(force=True)
            open(f"{EVD}/https_kernel_eof.log", "w").write(out)
            # kernel must fail promptly (error text) and never hang past 45 s
            results.append(("7 truncation detected promptly",
                            idx == 0 and dt < 45 and re.search(r"fail|error|truncat", out, re.I) is not None))
    finally:
        srv2.terminate()

    passed = 0
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
    print(f"=== {passed}/{len(results)} HTTPS E2E checks passed ===")
    sys.exit(len(results) - passed)

if __name__ == "__main__":
    main()
