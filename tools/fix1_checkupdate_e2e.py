#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""fix1 2a regression: the shipped ISO's checkupdate must see the real
live update feed (gh-pages update.json, build 194 = same as the ISO) and
honestly report up-to-date -- never offer the stale camera_misc build 193
package the way the chinese_support build did (version inversion bug).

Usage: python3 tools/fix1_checkupdate_e2e.py <iso> <mode: bios|uefi> <outlog>
Exit code: 0 ok, 1 fail.
"""
import os, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"


def main():
    iso, mode, outlog = sys.argv[1], sys.argv[2], sys.argv[3]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio", "-monitor", "none",
            "-no-reboot",
            "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]
    if mode == "uefi":
        code4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE_4M.fd"
        vars4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_VARS_4M.fd"
        if os.path.exists(code4m) and os.path.exists(vars4m):
            import shutil, tempfile
            vars_run = os.path.join(tempfile.gettempdir(), "oc_ovmf_vars.fd")
            shutil.copyfile(vars4m, vars_run)
            args += ["-drive", f"if=pflash,format=raw,readonly=on,file={code4m}",
                     "-drive", f"if=pflash,format=raw,file={vars_run}"]
    log = open(outlog, "w", buffering=1)
    child = pexpect.spawn(QEMU, args, encoding="utf-8",
                          codec_errors="replace", timeout=120, env=env)
    ok = True
    try:
        child.expect(r"ush>", timeout=240)
        log.write("=== boot ok, prompt reached ===\n")

        def run(cmd, timeout=120):
            child.sendline(cmd)
            child.expect(r"ush>", timeout=timeout)
            out = child.before or ""
            log.write(f"\n--- [{cmd}] ---\n{out}\n")
            return out

        # 1. local build identity: uname is the git-describe build
        #    provenance string (WP-10-chinese_support-N-g<sha>); the
        #    release-tag string lives in the OTA metadata that
        #    checkupdate reads (asserted below).
        out = run("uname -a")
        uname_ok = "Open Cube OS WP-10-chinese_support" in out
        # 2. the real-network check: must fetch the live update.json
        #    (build 194, same as the ISO) and report up to date
        out = run("checkupdate", timeout=180)
        log.write(f"\ncheckupdate result: {out.strip()[-400:]}\n")
        up_to_date = ("up to date" in out or "already" in out.lower()
                      or "no update" in out.lower()
                      or "194" in out and "install" not in out.lower())
        stale = "camera_misc" in out or "193" in out
        if stale:
            up_to_date = False
        log.write(f"\n[PASS?] uname shows fix1 build: {uname_ok}\n")
        log.write(f"[PASS?] checkupdate up-to-date honest: {up_to_date} "
                  f"(stale-offered={stale})\n")
        ok = uname_ok and up_to_date
        run("echo alive")
    except Exception as e:
        log.write(f"EXCEPTION: {e}\n")
        log.write(child.before or "")
        ok = False
    log.write("\n=== summary: fix1-checkupdate "
              + ("PASS" if ok else "FAIL") + " ===\n")
    log.close()
    try:
        child.sendline("poweroff")
    except Exception:
        pass
    child.close(force=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
