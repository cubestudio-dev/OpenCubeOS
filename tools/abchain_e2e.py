#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-severe-fix-and-closure: the UPDATE FULL-CHAIN E2E.

The spec: "从 L1 update 程序 → 网络下载 OTA → A/B 槽位识别 → 刷写，全链路
排查（拉取失败/解包错误/槽位死锁），提供完整成功串口日志".

The chain, every link proven on the serial transcript:

  [1] the A/B disk boots through GRUB -> slot A (BOOT_SLOT=A, the
      current tree kernel's banner)
  [2] dhcp brings the NIC up (slirp 10.0.2.15)
  [3] config set update_url http://10.0.2.2:8008/update.json
  [4] update --status prints the A/B state (current boot: A / next boot: A /
      A/B disk: present)
  [5] `update` (the L1 intent wizard, 5 steps, driven over the serial line):
        1/5 network up
        2/5 A/B disk present
        3/5 DNS: the IP-literal host resolves directly (the K-fix: an IP
            was never a name - the old path fed 10.0.2.2 to the recursive
            resolver and timed out)
        4/5 manifest fetched over HTTP -> New version available
        5/5 confirmed install: package download (Content-Length enforced),
            SHA256 verified, extract to the B partition, set boot to B,
            "reboot required", then a confirmed reboot
  [6] the reboot boots slot B: BOOT_SLOT=B, the TEST_VERSION package
      kernel banner (proves the flash landed), uname -r, and
      update --status shows current boot: B / next boot: B (confirm_boot
      promoted B to the confirmed default)
  [7] host-side byte proof: the B partition's /boot/opencube.elf extracted
      from the disk image equals the package's kernel/opencube.elf member
      (sha256), and slot A still equals the seeded 3.0.0 kernel.

Assets are self-prepared (idempotent, needs the toolchain on PATH):
  - a 3.0.0-WP-severe-fix-test test kernel (build 999) -> the package
  - the current-tree 3.0.0 kernel -> the slot-A seed
  - tools/make_ab_disk.sh builds the GRUB self-booting A/B disk
  - build/update.json is regenerated from the package's real sha256/size
    (field names byte-match the production manifest: package_url /
    package_sha256 / package_size - the field-name drift between a
    hand-rolled test manifest and the production format was exactly the
    "manifest has no package_url" chain break this battery exists to catch)

Usage: python3 tools/abchain_e2e.py [transcript-out]
       (default transcript-out: build/abchain-transcript.log)
Exit code: 0 all links pass, 1 any link fails.
"""
import http.server
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
TOOLBIN = f"{OC_TOOLS}/usr/bin"
QEMU = f"{TOOLBIN}/qemu-system-x86_64"
ABDISK = f"{ROOT}/build/abdisk.img"
PKG = f"{ROOT}/build/update-pkg.tar.gz"
MANIFEST = f"{ROOT}/build/update.json"
SEED_KERNEL = "/tmp/abchain-seed-kernel.elf"
PKG_KERNEL = "/tmp/abchain-pkg-kernel.elf"
PORT = 8008
TEST_VERSION = "3.0.0-WP-severe-fix-test"


def kernel_banner(path):
    """The 'Open Cube OS <version> kmain entered' string from a kernel
    ELF (the banner the B-slot boot proves the flash with)."""
    r = subprocess.run(
        f"strings {path} | grep -m1 'Open Cube OS .* kmain entered'",
        shell=True, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def die(msg):
    print(f"[chain] FAIL: {msg}", flush=True)
    sys.exit(1)


def run(cmd, **kw):
    print(f"[chain] $ {cmd}", flush=True)
    r = subprocess.run(cmd, shell=True, cwd=ROOT, **kw)
    if r.returncode != 0:
        die(f"command failed ({r.returncode}): {cmd}")
    return r


# ------------------------------------------------------------------ #
# asset preparation (idempotent)                                     #
# ------------------------------------------------------------------ #

def prepare_assets():
    # B. the test package from the TEST_VERSION kernel (a different
    # banner than the tree kernel - asserted in prepare, so the B-slot
    # uname and the byte-proof stay discriminating)
    run(f"make OC_RELEASE_VERSION={TEST_VERSION} OC_KERNEL_BUILD=999 kernel")
    shutil.copyfile(f"{ROOT}/build/opencube.elf", PKG_KERNEL)

    run(f"bash tools/make_update_pkg.sh WP-severe-fix build/opencube.elf "
        f"{PKG}")

    # C. restore the current-tree kernel (the slot-A seed)
    run("make kernel")
    shutil.copyfile(f"{ROOT}/build/opencube.elf", SEED_KERNEL)
    seed_banner = kernel_banner(SEED_KERNEL)
    pkg_banner = kernel_banner(PKG_KERNEL)
    # the seed must be a real tree kernel (a banner exists) and must be
    # DISTINGUISHABLE from the package kernel - the whole byte-proof and
    # the B-slot uname check rest on the two banners being different
    # (the tree version moves with every release; the TEST_VERSION
    # literal above is the one that must never collide with it).
    if not seed_banner:
        die("slot-A seed kernel carries no 'Open Cube OS ... kmain entered' "
            "banner")
    if seed_banner == pkg_banner:
        die(f"the tree kernel and the test package kernel carry the SAME "
            f"banner ({seed_banner}) - bump TEST_VERSION so the B-slot "
            f"boot proves the flash")
    print(f"[chain] seed banner : {seed_banner}", flush=True)
    print(f"[chain] pkg banner  : {pkg_banner}", flush=True)

    # D. the GRUB self-booting A/B disk (slot A seeded with 3.0.0)
    run(f"bash tools/make_ab_disk.sh {SEED_KERNEL}")

    # E. the manifest, byte-matching the PRODUCTION field names
    #    (package_url / package_sha256 / package_size)
    pkg_sha = subprocess.run(f"sha256sum {PKG}", shell=True,
                             capture_output=True, text=True
                             ).stdout.split()[0]
    pkg_size = os.path.getsize(PKG)
    with open(MANIFEST, "w") as f:
        f.write('{\n'
                f'  "version": "{TEST_VERSION}",\n'
                '  "time": "2026-11-26",\n'
                '  "changes": "WP-severe-fix-and-closure full-chain test '
                'payload",\n'
                f'  "package_url": "http://10.0.2.2:{PORT}/pkg.tar.gz",\n'
                f'  "package_sha256": "{pkg_sha}",\n'
                f'  "package_size": {pkg_size}\n'
                '}\n')
    print(f"[chain] assets ready: pkg {pkg_size}B sha {pkg_sha[:16]}...",
          flush=True)


# ------------------------------------------------------------------ #
# the HTTP fixture (manifest + package)                              #
# ------------------------------------------------------------------ #

# Loaded AFTER prepare_assets() in main() - a module-level read would
# serve the PREVIOUS run's package (live-caught by the byte-proof:
# the guest downloaded and flashed the old package while every
# version-string assertion stayed green).
MANIFEST_BODY = None
PKG_BODY = None


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = MANIFEST_BODY if self.path.endswith(".json") else PKG_BODY
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        print(f"[httpd] {self.path} -> {len(body)} bytes", flush=True)

    def log_message(self, *a):
        pass


# ------------------------------------------------------------------ #
# the serial driver (unix-socket serial line - see the K-09 lesson:  #
# a mon:stdio channel measurably starves the QEMU main loop here)    #
# ------------------------------------------------------------------ #

class Serial:
    def __init__(self, sock_path):
        self.buf = b""
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(sock_path)
        self.sock.settimeout(0.2)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.sock.recv(4096)
                if d:
                    self.buf += d
            except socket.timeout:
                pass

    def wait_for(self, pattern, timeout, what):
        end = time.time() + timeout
        while time.time() < end:
            if pattern in self.buf:
                print(f"[chain] ok: {what}", flush=True)
                return True
            self.pump(0.5)
        if pattern in self.buf:
            print(f"[chain] ok: {what}", flush=True)
            return True
        print(f"[chain] TIMEOUT ({timeout}s) waiting for: {what}", flush=True)
        return False

    def wait_new(self, pattern, timeout, what):
        """Wait for pattern to appear AFTER the current buffer position."""
        pre = len(self.buf)
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.5)
            if self.buf.find(pattern, pre) != -1:
                print(f"[chain] ok: {what}", flush=True)
                return True
        return self.buf.find(pattern, pre) != -1

    def send(self, line):
        self.sock.sendall(line.encode() + b"\r")

    def cmd(self, line, timeout=90):
        """Send a command and wait for the next prompt.  The command's
        own output lands in buf BEFORE the prompt, so callers assert on
        buf (was_seen) - NOT on wait_new, which would wait for a second
        occurrence that never comes."""
        self.send(line)
        ok = self.wait_new(b"ush>", timeout, f"prompt after `{line}`")
        self.pump(0.5)
        return ok

    def was_seen(self, pattern, what):
        """Assert a pattern the last command already printed."""
        if pattern in self.buf:
            print(f"[chain] ok: {what}", flush=True)
            return True
        print(f"[chain] MISSING in transcript: {what}", flush=True)
        return False


# ------------------------------------------------------------------ #
# host-side byte proof: the flashed partition equals the package     #
# ------------------------------------------------------------------ #

def pull_partition(img, part_no, start_s, sectors, out_file):
    with open(img, "rb") as f, open(out_file, "wb") as o:
        f.seek(start_s * 512)
        left = sectors * 512
        while left:
            chunk = f.read(min(1 << 20, left))
            if not chunk:
                die("short read pulling the partition image")
            o.write(chunk)
            left -= len(chunk)


def mcopy_out(part_img, member, out_file):
    r = subprocess.run(
        f"PATH={TOOLBIN}:$PATH mcopy -i {part_img} ::/{member} {out_file}",
        shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        die(f"mcopy {member} failed: {r.stderr.strip()}")


def sha256_of(path):
    return subprocess.run(f"sha256sum {path}", shell=True,
                          capture_output=True, text=True).stdout.split()[0]


def verify_flash():
    # layout (tools/make_ab_disk.sh): p1 @2048 (64M), p2 @133120 (128M),
    # p3 @395264 (128M), p4 @657408 (128M)
    checks = []
    for part, start, label, expect in (
            (2, 133120, "the slot-A kernel (the tree seed)", SEED_KERNEL),
            (3, 395264, "the slot-B kernel (the flashed package)",
             PKG_KERNEL)):
        pimg = f"/tmp/abchain-p{part}.img"
        pull_partition(ABDISK, part, start, 262144, pimg)
        kfile = f"/tmp/abchain-p{part}-opencube.elf"
        mcopy_out(pimg, "boot/opencube.elf", kfile)
        got, want = sha256_of(kfile), sha256_of(expect)
        same = got == want
        print(f"[chain] p{part} {label}: {got[:16]}... vs {want[:16]}... "
              f"-> {'MATCH' if same else 'MISMATCH'}", flush=True)
        checks.append((f"p{part} == {'seed' if part == 2 else 'package'}"
                        f" (byte-identical flash)", same))
        os.unlink(pimg)
        os.unlink(kfile)
    return checks


# ------------------------------------------------------------------ #
# the chain                                                          #
# ------------------------------------------------------------------ #

def main():
    if "--prepare-only" in sys.argv:
        prepare_assets()
        print("[chain] prepare-only: done")
        return 0
    if "--no-prepare" not in sys.argv:
        prepare_assets()

    global MANIFEST_BODY, PKG_BODY
    MANIFEST_BODY = open(MANIFEST, "rb").read()
    PKG_BODY = open(PKG, "rb").read()

    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 and
                          not sys.argv[1].startswith("-")
                          else f"{ROOT}/build/abchain-transcript.log")

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"[chain] httpd on 127.0.0.1:{PORT} "
          f"(manifest {len(MANIFEST_BODY)}B + pkg {len(PKG_BODY)}B)",
          flush=True)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (
        f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
        f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    S = "/tmp/abchain.sock"
    if os.path.exists(S):
        os.unlink(S)
    child = subprocess.Popen(
        [QEMU, "-m", "512M",
         "-L", f"{OC_TOOLS}/usr/share/qemu",
         "-L", f"{OC_TOOLS}/usr/share/seabios",
         "-drive", f"file={ABDISK},format=raw,if=ide,index=0,media=disk",
         "-display", "none", "-monitor", "none",
         "-netdev", "user,id=n1", "-device", "e1000,netdev=n1",
         "-serial", f"unix:{S},server=on,wait=on"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)

    sock = None
    for _ in range(90):
        try:
            sock = Serial(S)
            break
        except OSError:
            time.sleep(0.5)
    if sock is None:
        child.kill()
        die("serial socket never came up")

    ok = []

    # [1] the slot-A boot (GRUB menu -> 5 s countdown -> kernel)
    if not sock.wait_for(b"ush>", 240, "the slot-A boot reaches the prompt"):
        ok.append(("slot-A boot", False))
        return finish(child, sock, out, ok, srv)
    ok.append(("slot-A boot (GRUB, BOOT_SLOT=A -> prompt)", True))

    # [2] dhcp
    ok.append(("dhcp brings the NIC up", sock.cmd("dhcp", 60) and
               sock.was_seen(b"DHCP success", "DHCP success")))

    # [3] the local update server URL
    ok.append(("config set update_url", sock.cmd(
        f"config set update_url http://10.0.2.2:{PORT}/update.json", 30) and
        sock.was_seen(b"config: saved", "config saved")))

    # [4] the A/B state
    ok.append(("update --status (A/B state)", sock.cmd(
        "update --status", 60) and
        sock.was_seen(b"current boot: A", "current boot: A") and
        sock.was_seen(b"A/B disk: present", "A/B disk: present")))

    # [5] the wizard - the full online chain
    print("[chain] running `update` (the intent wizard)...", flush=True)
    sock.send("update")
    ok_w = sock.wait_for(b"Press ENTER to start the checks...", 30,
                         "the wizard banner")
    sock.send("")
    ok_w = ok_w and sock.wait_for(b"[OK] Network is up.", 60,
                                  "wizard 1/5 network")
    ok_w = ok_w and sock.wait_for(b"[OK] A/B disk is present.", 60,
                                  "wizard 2/5 A/B disk")
    ok_w = ok_w and sock.wait_for(
        f"Resolved 10.0.2.2:{PORT} -> 10.0.2.2 (literal)".encode(),
        60, "wizard 3/5 IP-literal DNS (the authority incl. the port)")
    ok_w = ok_w and sock.wait_for(b"New version available.", 180,
                                  "wizard 4/5 manifest fetched")
    ok_w = ok_w and sock.wait_for(
        b"Download and install the new version now?", 30,
        "wizard 5/5 install confirm")
    sock.send("y")
    ok_w = ok_w and sock.wait_for(b"update: downloading...", 60,
                                  "package download started")
    ok_w = ok_w and sock.wait_for(b"update: verifying SHA256... OK", 300,
                                  "package SHA256 verified")
    ok_w = ok_w and sock.wait_for(b"update: extracting to B partition... OK",
                                  300, "extract to the B partition")
    ok_w = ok_w and sock.wait_for(b"update: setting boot to B... OK", 60,
                                  "set boot to B")
    ok_w = ok_w and sock.wait_for(b"update: reboot required", 30,
                                  "reboot required")
    ok_w = ok_w and sock.wait_for(b"[OK] Update installed.", 30,
                                  "wizard install complete")
    ok.append(("wizard 5/5: download -> SHA256 -> flash B -> set boot B",
               ok_w))
    if not ok_w:
        return finish(child, sock, out, ok, srv)

    # confirmed reboot into the new version
    ok_w = sock.wait_for(b"Reboot into the new version now?", 30,
                         "the reboot offer")
    sock.send("y")

    # [6] the slot-B boot: BOOT_SLOT=B + the TEST_VERSION package banner
    ok_b = sock.wait_for(b"BOOT_SLOT=B", 240, "GRUB selects slot B")
    ok_b = ok_b and sock.wait_for(
        f"Open Cube OS {TEST_VERSION} kmain entered".encode(), 240,
        "the package kernel boots (the flash landed)")
    ok_b = ok_b and sock.wait_for(b"ush>", 240, "the slot-B prompt")
    ok.append(("reboot boots slot B (BOOT_SLOT=B, the package banner)",
               ok_b))
    if not ok_b:
        return finish(child, sock, out, ok, srv)

    ok.append(("uname -r on slot B (the package kernel version)", sock.cmd(
        "uname -r", 60) and
        sock.was_seen(TEST_VERSION.encode(), "uname -r " + TEST_VERSION)))
    ok.append(("update --status on slot B (current B, confirmed)", sock.cmd(
        "update --status", 60) and
        sock.was_seen(b"current boot: B", "current boot: B") and
        sock.was_seen(b"next boot: B", "next boot: B (confirmed)")))

    sock.send("poweroff")
    try:
        child.wait(timeout=30)
    except subprocess.TimeoutExpired:
        child.kill()

    # [7] host-side byte proof
    ok.extend(verify_flash())

    return finish(child, sock, out, ok, srv, qemu_dead=True)


def finish(child, sock, out, ok, srv, qemu_dead=False):
    if not qemu_dead:
        child.kill()
    sock.sock.close()
    srv.shutdown()
    with open(out, "wb") as f:
        f.write(sock.buf)
    print("=== results ===", flush=True)
    allok = True
    for name, good in ok:
        print(("PASS " if good else "FAIL ") + name, flush=True)
        allok = allok and good
    print(f"=== update full-chain: {'ALL PASS' if allok else 'FAILURES'} ===",
          flush=True)
    print(f"transcript: {out} ({len(sock.buf)} bytes)", flush=True)
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
