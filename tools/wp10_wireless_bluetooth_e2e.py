#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-wireless_bluetooth end-to-end test driver.

Boots the Open Cube OS ISO under QEMU (BIOS or UEFI), runs the wireless
+ bluetooth command surface and captures the raw serial output of every
command.  The Bluetooth path is REAL end-to-end: the second QEMU serial
(COM2) carries H4 bytes into tools/bt_test_peer.py, which plays the
controller half plus the remote device (L2CAP/SDP/RFCOMM/SPP/HID).

Usage: python3 tools/wp10_wireless_bluetooth_e2e.py <iso> <mode: bios|uefi> <outlog>
Exit code: number of failed checks.
"""
import os, re, socket, subprocess, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
BOOT_COUNT = 198

# (name, command, expected-regex on that command's output) - None regex
# means "record only" (informational).
CMDS = [
    ("wifi families",  "wifi",       r"wireless families: 7  devices: 7"),
    ("wifi dev list",  "wifi",       r"dev0 iwlwifi bus=auto.*?state=down"),
    ("wifi honest",    "wifi",       r"QEMU has no 802\.11 models; down = device not present"),
    ("wifi test core", "wifi test",  r"beacon parse PASS|core checks done"),
    ("wifi test core2","wifi test",  r"core selftest 0 fails PASS|core checks done"),
    ("wifi test iwl",  "wifi test",  r"iwl ucode legacy parse PASS"),
    ("wifi test rtl",  "wifi test",  r"rtl txdesc own/fsl PASS"),
    ("wifi test ath",  "wifi test",  r"ath9k rxd parse PASS"),
    ("wifi test brcm", "wifi test",  r"brcmf sb window order PASS"),
    ("wifi test verdict","wifi test", r"wifi test: 0 failures - PASS"),
    ("wifi scan",      "wifi scan",  r"wifi scan: 0 networks seen \(no beacons - no radio present\)"),
    ("wifi sdio",      "wifi sdio",  r"sdio bus: state=(empty|no-host|sdio) hosts=\d+"),
    ("wifi sdio test", "wifi sdio",  r"sdio cmd52 write enc PASS"),
    ("bt status",      "bt",         r"bluetooth: transport=h4-uart2 controller=[0-9A-F:]{17} conns=\d+"),
    ("bt uart",        "bt uart",    r"h4: uart2 ok"),
    ("bt usb honest",  "bt usb",     r"usb-bt: no dongle attached"),
    ("bt test reset",  "bt test",    r"hci reset: PASS"),
    ("bt test version","bt test",    r"read local version: PASS"),
    ("bt test bdaddr", "bt test",    r"read bd addr: PASS"),
    ("bt test addr",   "bt test",    r"bd_addr: [0-9A-F:]{17}"),
    ("bt test inquiry","bt test",    r"inquiry sees test peer: PASS"),
    ("bt test conn",   "bt test",    r"acl connection: PASS"),
    ("bt test l2cap",  "bt test",    r"l2cap echo: PASS"),
    ("bt test sdp",    "bt test",    r"sdp spp channel: PASS"),
    ("bt test rfcomm", "bt test",    r"rfcomm dlc: PASS"),
    ("bt test spp",    "bt test",    r"spp echo: PASS"),
    ("bt test hid",    "bt test",    r"hid get report: PASS"),
    ("bt test verdict","bt test",    r"bt test: 0 failures - PASS"),
    ("help wp group",  "help -w",    r"WP-10-wireless_bluetooth \(wireless \+ bluetooth \(802\.11/HCI/L2CAP/RFCOMM\)\)"),
    ("help az wifi",   "help -a",    r"wifi"),
    ("uname",          "uname -a",   r"Open Cube OS WP-10-(wireless_bluetooth|timer_rtc-\d+-g)"),
]


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main():
    iso, mode, outlog = sys.argv[1], sys.argv[2], sys.argv[3]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    bt_port = free_port()
    args = [
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu", "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        # COM2 carries the H4 Bluetooth controller bytes
        "-chardev", f"socket,id=btchr,host=127.0.0.1,port={bt_port},server=on,wait=off",
        "-serial", "chardev:btchr",
        "-drive", "if=ide,format=raw,file=build/etc.img",
        "-netdev", "user,id=n1", "-device", "e1000,netdev=n1",
    ]
    if mode == "uefi":
        os.makedirs("build", exist_ok=True)
        import shutil
        shutil.copyfile(f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd",
                        "build/OVMF_WORK.fd")
        args += ["-drive", "if=pflash,format=raw,file=build/OVMF_WORK.fd"]

    child = pexpect.spawn(QEMU, args, timeout=240, encoding="latin-1",
                          codec_errors="replace", env=env)
    child.expect(r"oc>\s*", timeout=240)
    boot_out = child.before or ""
    boot_count_ok = re.search(
        rf"shell command system \({BOOT_COUNT} commands\)", boot_out
    ) is not None

    # hook the host-side Bluetooth peer onto COM2
    peer = subprocess.Popen(
        [sys.executable, "tools/bt_test_peer.py", str(bt_port)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(f"[e2e {mode}] bt_test_peer.py on 127.0.0.1:{bt_port}")
    # wait until the peer is connected to the QEMU chardev
    time.sleep(1.5)
    if peer.poll() is not None:
        print("[e2e] bt_test_peer died:", peer.stdout.read().decode()[:400])
        sys.exit(1)

    outputs = {}
    for name, cmd, pat in CMDS:
        child.sendline(cmd)
        idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=150)
        out = (child.before or "") + ("\n<<TIMEOUT>>" if idx == 1 else "")
        outputs[name] = out
        ok = "?" if pat is None else ("PASS" if re.search(pat, out, re.I | re.S) else "FAIL")
        print(f"[e2e {mode}] {ok}  {name} ({len(out)} bytes)")
        child.sendline("")   # resync
        try:
            child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=10)
        except Exception:
            pass

    child.close(force=True)
    peer.terminate()

    parts = [boot_out]
    for (name, cmd, _pat) in CMDS:
        parts.append(f"=== [{name}] $ {cmd}\n{outputs.get(name, '')}")
    combined = "\n".join(parts).replace("\x00", "")
    os.makedirs(os.path.dirname(outlog) or ".", exist_ok=True)
    with open(outlog, "w") as f:
        f.write(combined)

    passed = failed = 0
    print(f"\n=== {mode} checks ===")
    for name, cmd, pat in CMDS:
        if pat is None:
            continue
        ok = re.search(pat, outputs[name], re.I | re.S) is not None
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
        failed += (not ok)
    print(f"  {'PASS' if boot_count_ok else 'FAIL'}  boot log: shell command system ({BOOT_COUNT} commands)")
    passed += boot_count_ok
    failed += (not boot_count_ok)
    print(f"=== {mode}: {passed}/{passed + failed} checks passed, {failed} failed ===")
    sys.exit(failed)


if __name__ == "__main__":
    main()
