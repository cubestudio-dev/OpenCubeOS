#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - host test runner (make test backend).
#
# Extracts the gcc command documented in each tests/host_*.c header,
# remaps source/-I paths broken by the WP-10-project_restructure move,
# compiles and runs each harness, and exits with the total number of
# failed tests. Tests that require external fixtures (openssl-generated
# keys) are compiled and reported as SKIP-FIXTURE, not counted as runs.
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "tests")
OUT = "/tmp/oc_hosttest"
os.makedirs(OUT, exist_ok=True)

# pre-restructure path (as written in test headers) -> current path
SRC_MAP = {
    "kernel/aead.c": "ocos/services/crypto/src/svc_crypto_aead.c",
    "kernel/crypto.c": "ocos/services/crypto/src/svc_crypto_core.c",
    "kernel/bn.c": "ocos/services/crypto/src/svc_crypto_bn.c",
    "kernel/rsa.c": "ocos/services/crypto/src/svc_crypto_rsa.c",
    "kernel/x509.c": "ocos/services/crypto/src/svc_crypto_x509.c",
    "kernel/curve25519.c": "ocos/services/crypto/src/svc_crypto_curve25519.c",
    "kernel/sha512.c": "ocos/services/crypto/src/svc_crypto_sha512.c",
    "kernel/gcm.c": "ocos/services/crypto/src/svc_crypto_aead.c",
    "kernel/tar.c": "ocos/kernel/core_lib_tar.c",
    "kernel/gzip.c": "ocos/kernel/core_lib_gzip.c",
    "kernel/string.c": "ocos/kernel/core_lib_string.c",
    "kernel/net_tcp_cc.c": "ocos/services/net/src/svc_net_tcp_cc.c",
    "kernel/scsi.c": "ocos/drivers/storage/drv_storage_scsi.c",
    "kernel/lsi.c": "ocos/drivers/storage/drv_storage_scsi_lsi.c",
    "kernel/i2chid.c": "ocos/drivers/input/drv_input_i2chid.c",
    "kernel/i2c.c": "ocos/drivers/bus/drv_bus_i2c.c",
    "kernel/smbus.c": "ocos/drivers/bus/drv_bus_smbus.c",
    "kernel/parallel.c": "ocos/drivers/misc/drv_misc_parallel.c",
    "kernel/gameport.c": "ocos/drivers/misc/drv_misc_gameport.c",
    "kernel/ir.c": "ocos/drivers/misc/drv_misc_ir.c",
    "kernel/wireless.c": "ocos/drivers/net/drv_net_wireless.c",
    "kernel/rtlwifi.c": "ocos/drivers/net/drv_net_rtlwifi.c",
    "kernel/iwlwifi.c": "ocos/drivers/net/drv_net_iwlwifi.c",
    "kernel/ath.c": "ocos/drivers/net/drv_net_ath.c",
    "kernel/brcm.c": "ocos/drivers/net/drv_net_brcm.c",
    "kernel/camera.c": "ocos/drivers/misc/drv_misc_camera.c",
    "kernel/mjpeg.c": "ocos/drivers/misc/drv_misc_mjpeg.c",
    "kernel/uvc.c": "ocos/drivers/misc/drv_misc_camera_uvc.c",
    "kernel/fb.c": "ocos/kernel/core_console_fb.c",
    "kernel/font.c": "ocos/kernel/core_console_font.c",
    "kernel/font_data.c": "ocos/kernel/core_console_font_data.c",
    "kernel/console.c": "ocos/kernel/core_console_screen.c",
    "kernel/keyboard.c": "ocos/drivers/input/drv_input_keyboard.c",
    "kernel/nic.c": "ocos/drivers/net/drv_net_e1000e.c",
    "kernel/e1000.c": "ocos/drivers/net/drv_net_e1000e.c",
    "kernel/snd.c": "ocos/services/audio/src/svc_audio_drivers.c",
    "drivers/block/driver_block_blk.c": "ocos/services/block/src/svc_block_drivers.c",
    "net/net_tcp_cc.c": "ocos/services/net/src/svc_net_tcp_cc.c",
    "kernel/arch/x86_64/arch_multiboot2.c": "ocos/kernel/arch/x86_64/arch_multiboot2.c",
    "kernel/core/core_rtc.c": "ocos/services/time/src/svc_time_rtc.c",
    "core/core_rtc.c": "ocos/services/time/src/svc_time_rtc.c",
    "kernel/lib/lib_string.c": "ocos/kernel/core_lib_string.c",
    "kernel/crypto/crypto_x509.c": "ocos/services/crypto/src/svc_crypto_x509.c",
    "kernel/crypto/crypto_rsa.c": "ocos/services/crypto/src/svc_crypto_rsa.c",
    "kernel/crypto/crypto_bn.c": "ocos/services/crypto/src/svc_crypto_bn.c",
    "kernel/crypto/crypto_ec_nist.c": "ocos/services/crypto/src/svc_crypto_ec_nist.c",
    "kernel/crypto/crypto_core.c": "ocos/services/crypto/src/svc_crypto_core.c",
    "kernel/crypto/crypto_sha512.c": "ocos/services/crypto/src/svc_crypto_sha512.c",
    "kernel/crypto/crypto_curve25519.c": "ocos/services/crypto/src/svc_crypto_curve25519.c",
    "kernel/hda.c": "ocos/drivers/audio/drv_audio_hda.c",
    "kernel/usb.c": "ocos/drivers/usb/drv_usb_core.c",
    "kernel/hid.c": "ocos/drivers/usb/drv_usb_hid.c",
    "kernel/ata.c": "ocos/drivers/storage/drv_storage_ata.c",
    "kernel/blk.c": "ocos/services/block/src/svc_block_drivers.c",
    "kernel/cache.c": "ocos/drivers/storage/drv_storage_cache.c",
    "kernel/part.c": "ocos/drivers/storage/drv_storage_part.c",
    "kernel/disk_setup.c": "ocos/drivers/storage/drv_storage_disk_setup.h",
    "kernel/acpi.c": "ocos/drivers/bus/drv_bus_acpi.c",
    "kernel/aml.c": "ocos/drivers/bus/drv_bus_acpi_aml.c",
    "kernel/hpet.c": "ocos/drivers/time/drv_time_hpet.c",
    "kernel/pmtimer.c": "ocos/drivers/time/drv_time_pmtimer.c",
    "kernel/tsc.c": "ocos/drivers/time/drv_time_tsc.c",
    "kernel/cmos_rtc.c": "ocos/drivers/time/drv_time_cmos_rtc.c",
    "kernel/ec_nist.c": "ocos/services/crypto/src/svc_crypto_ec_nist.c",
    "kernel/crypto_ec_nist.c": "ocos/services/crypto/src/svc_crypto_ec_nist.c",
    "kernel/rtc.c": "ocos/services/time/src/svc_time_rtc.c",
    "kernel/sdhci.c": "ocos/drivers/storage/drv_storage_sdhci.c",
    "kernel/sched.c": "ocos/kernel/core_sched.c",
    "kernel/usermode.c": "ocos/kernel/core_usermode.c",
    "kernel/syscall.c": "ocos/services/sysapi/src/svc_sysapi_syscall.c",
    "kernel/ota_ab.c": "ocos/services/selfhost/src/svc_selfhost_ab.c",
    "kernel/ota_update.c": "ocos/services/selfhost/src/svc_selfhost_update.c",
}
# WP-compat-and-security-hardening (regression batch): the remap now
# targets the LIVE NAK tree (ocos/) - the src/L0 targets died with the
# WP-refactor-to-nak-architecture move and the 31/31 battery had been
# silently build-failing since (live-caught by this release's full
# regression run).
INC_MAP = {
    "-Ikernel/net": "-Iocos/services/net/src",
    "-Ikernel/drivers": "-Iocos/drivers",
    "-Ikernel": "-Iocos/kernel",
    "-Ikernel/lib": "-Iocos/kernel",
    "-Ikernel/crypto": "-Iocos/services/crypto/src",
    "-Ikernel/mem": "-Iocos/kernel",
    "-Ikernel/core": "-Iocos/kernel",
    "-Ikernel/arch/x86_64": "-Iocos/kernel/arch/x86_64",
}
# tests with external-fixture requirements (compile but skip run).
# WP-10-tui-qa: the skip is now CONDITIONAL - when the fixture tree is
# present the test runs for real.  RSA fixtures are openssl-generated
# locally (/tmp/rsa_fix); the X509 chains are live-captured from
# github.io/google/cloudflare (/tmp/chain_fix/<site>/cert{0,1,2}.der,
# all three sites required - this sandbox cannot reach www.google.com
# so x509 still skips here, honestly).
SKIP_FIXTURE = {"host_rsa_test.c", "host_x509_test.c"}


def fixtures_ready(t):
    if t == "host_rsa_test.c":
        return os.path.exists("/tmp/rsa_fix/n.bin")
    if t == "host_x509_test.c":
        return all(os.path.exists(f"/tmp/chain_fix/{s}/cert0.der")
                   for s in ("cubestudio-dev.github.io", "www.google.com",
                             "www.cloudflare.com"))
    return True
# camera harness miscompiles at -O2 (see Makefile note): force -Os
FORCE_FLAGS = {"host_camera_test.c": (("-O2",), ("-Os",))}
EXTRA_SRC = {"host_tls_p0_test.c": ["tests/host_tls_p0_stub.c"],
             "host_a401_entry_test.c": ["tests/host_pmm_stub.c"],
             "host_rsa_test.c": ["tests/host_pmm_stub.c"],
             "host_x509_test.c": ["tests/host_pmm_stub.c"],
             # WP-10-tui: the wireless bridge calls nic_dev_add (v2.0.0);
             # WP-03 G3: driver_wireless_init collects families through the
             # real registry (empty section boundaries via the stub)
             "host_wifi_test.c": ["tests/host_wifi_nic_stub.c",
                                   "tests/host_registry_stub.c",
                                   "ocos/kernel/core_registry.c", "ocos/drivers/drv_framework.c"],
             # v2.0.0: svc_block_drivers.c probe path walks the driver
             # registry (and the framework section walker it needs)
             "host_blk512_gate_test.c": ["tests/host_registry_stub.c",
                                          "ocos/kernel/core_registry.c",
                                          "ocos/drivers/drv_framework.c"]}


def _clean(seg):
    seg = re.sub(r"^[\*\s]+", "", seg)   # strip comment prefix "*   gcc ..."
    seg = re.sub(r"^Build:\s*", "", seg)  # strip "Build: gcc ..." prefix
    return seg


def extract_gcc(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    for i, ln in enumerate(lines[:40]):
        s = _clean(ln)
        if re.match(r"^(gcc|cc)\s", s):
            cmd = [s]
            j = i
            while cmd[-1].endswith("\\"):
                cmd[-1] = cmd[-1][:-1].strip()
                j += 1
                cmd.append(_clean(lines[j]))
            return " ".join(cmd)
    return None


def normalize(toks):
    out, i = [], 0
    while i < len(toks):
        t = toks[i]
        if t == "*":                                   # stray comment star
            i += 1
            continue
        if t == "-I" and i + 1 < len(toks):            # "-I dir" -> "-Idir"
            out.append("-I" + toks[i + 1])
            i += 2
            continue
        if t == "-o" and i + 1 < len(toks):            # drop original -o
            i += 2
            continue
        out.append(t)
        i += 1
    return out


# WP-compat regression: the ocos/ NAK tree.  The historical src/L0
# historical path-shaped includes ("core/types.h", "drivers/input/x.c").
SAFE_INCS = ["-Iocos", "-Iocos/kernel", "-Iocos/kernel/lib",
             "-Iocos/kernel/generated",
             "-Iocos/kernel/arch/x86_64",
             "-Iocos/services/crypto/src",
             "-Iocos/services/crypto/interface",
             "-Iocos/services/selfhost/src",
             "-Iocos/services/selfhost/interface",
             "-Iocos/services/klog/interface",
             "-Iocos/services/health/interface",
             "-Iocos/services/crash/interface",
             "-Iocos/services/power/interface",
             "-Iocos/services/time/interface",
             "-Iocos/services/fs/interface", "-Iocos/services/fs/src",
             "-Iocos/services/net/interface", "-Iocos/services/net/src",
             "-Iocos/services/sysapi/interface",
             "-Iocos/services/block/interface",
             "-Iocos/services/audio/interface",
             "-Iocos/services/display/interface", "-Iocos/services/display/src",
             "-Iocos/services/font/interface",
             "-Iocos/services/encoding/interface",
             "-Iocos/services/ipc/interface",
             "-Iocos/services/loader/interface",
             "-Iocos/services/camera/interface",
             "-Iocos/services/usb/interface",
             "-Iocos/services/tui/interface",
             "-Iocos/services/diag/src",
             "-Iocos/drivers", "-Itests", "-Iinclude",
             "-Iocos/drivers/storage", "-Iocos/drivers/net",
             "-Iocos/drivers/audio", "-Iocos/drivers/usb",
             "-Iocos/drivers/input", "-Iocos/drivers/bus",
             "-Iocos/drivers/i2c", "-Iocos/drivers/misc",
             "-Iocos/drivers/time", "-Iocos/drivers/display",
             "-Iocos/services",
             "-I."]     # the tree root: core/ (a symlink to ocos/kernel)
                        # serves the historical "core/types.h" and
                        # "../core/types.h" includes of the older tests


def remap(cmd):
    out = []
    for tok in cmd.split():
        if tok in SRC_MAP:
            out.append(SRC_MAP[tok])
        elif tok in INC_MAP:
            out.append(INC_MAP[tok])
        else:
            out.append(tok)
    return out


def main():
    selected = sys.argv[1:] or sorted(
        f for f in os.listdir(TESTS)
        if f.startswith("host_") and f.endswith("_test.c"))
    results, failed = [], 0
    for t in selected:
        tp = os.path.join(TESTS, t)
        gcc_line = extract_gcc(tp)
        if not gcc_line:
            results.append((t, "NO-GCC-CMD"))
            failed += 1
            continue
        toks = normalize(remap(gcc_line))
        if t in FORCE_FLAGS:
            for old, new in zip(*FORCE_FLAGS[t]):
                toks = [new if x == old else x for x in toks]
        exe = os.path.join(OUT, t[:-2])
        toks = [toks[0], "-fno-builtin", "-w"] + toks[1:] + SAFE_INCS + ["-o", exe]
        for extra in EXTRA_SRC.get(t, []):
            toks.insert(-2, os.path.join(ROOT, extra))
        # normalize relative source refs against repo root
        toks = [os.path.join(ROOT, x) if (x.endswith((".c", ".h")) and not os.path.isabs(x) and not x.startswith("-")) else x for x in toks]
        toks = [x.replace("-I" + ROOT, "-I" + ROOT) for x in toks]
        cp = subprocess.run(toks, cwd=ROOT, capture_output=True, text=True)
        if cp.returncode != 0:
            results.append((t, f"BUILD-FAIL (see {OUT}/{t}.build.log)"))
            with open(f"{OUT}/{t}.build.log", "w") as f:
                f.write(" ".join(toks) + "\n\n" + cp.stderr[-4000:])
            # 2.1.0: echo the first error lines to stdout as well - CI
            # (and any headless run) cannot read the /tmp log file, and
            # a bare "BUILD-FAIL" line hides the actual compiler error.
            err_lines = [ln for ln in cp.stderr.splitlines()
                         if "error" in ln.lower()][:8]
            if err_lines:
                print(f"--- {t} build errors (first {len(err_lines)}):")
                for ln in err_lines:
                    print(f"    {ln[:200]}")
            failed += 1
            continue
        if t in SKIP_FIXTURE and not fixtures_ready(t):
            results.append((t, "SKIP-FIXTURE (needs openssl fixtures)"))
            continue
        rp = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True, timeout=120)
        if rp.returncode == 0:
            results.append((t, "PASS"))
        else:
            results.append((t, f"FAIL exit={rp.returncode} (log {OUT}/{t}.run.log)"))
            with open(f"{OUT}/{t}.run.log", "w") as f:
                f.write(rp.stdout[-4000:] + rp.stderr[-2000:])
            failed += 1

    print(f"{'TEST':38s} RESULT")
    for t, r in results:
        print(f"{t:38s} {r}")
    ran = [r for _, r in results if r == "PASS"]
    print(f"\nsummary: {len(ran)} passed, {failed} failed/build-failed, "
          f"{sum(1 for _, r in results if r.startswith('SKIP'))} skipped (fixtures)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
