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
    "kernel/aead.c": "src/L0/services/crypto/crypto_aead.c",
    "kernel/crypto.c": "src/L0/services/crypto/crypto_core.c",
    "kernel/bn.c": "src/L0/services/crypto/crypto_bn.c",
    "kernel/rsa.c": "src/L0/services/crypto/crypto_rsa.c",
    "kernel/x509.c": "src/L0/services/crypto/crypto_x509.c",
    "kernel/curve25519.c": "src/L0/services/crypto/crypto_curve25519.c",
    "kernel/sha512.c": "src/L0/services/crypto/crypto_sha512.c",
    "kernel/gcm.c": "src/L0/services/crypto/crypto_aead.c",
    "kernel/tar.c": "src/L0/core/lib/lib_tar.c",
    "kernel/gzip.c": "src/L0/core/lib/lib_gzip.c",
    "kernel/string.c": "src/L0/core/lib/lib_string.c",
    "kernel/net_tcp_cc.c": "src/L0/services/net/net_tcp_cc.c",
    "kernel/scsi.c": "src/L0/drivers/scsi/driver_scsi.c",
    "kernel/lsi.c": "src/L0/drivers/scsi/driver_scsi_lsi.c",
    "kernel/i2chid.c": "src/L0/drivers/input/driver_input_i2chid.c",
    "kernel/i2c.c": "src/L0/drivers/i2c/driver_i2c.c",
    "kernel/smbus.c": "src/L0/drivers/i2c/driver_smbus.c",
    "kernel/parallel.c": "src/L0/drivers/misc/driver_parallel.c",
    "kernel/gameport.c": "src/L0/drivers/misc/driver_gameport.c",
    "kernel/ir.c": "src/L0/drivers/misc/driver_ir.c",
    "kernel/wireless.c": "src/L0/drivers/wireless/driver_wireless.c",
    "kernel/rtlwifi.c": "src/L0/drivers/wireless/driver_rtlwifi.c",
    "kernel/iwlwifi.c": "src/L0/drivers/wireless/driver_iwlwifi.c",
    "kernel/ath.c": "src/L0/drivers/wireless/driver_ath.c",
    "kernel/brcm.c": "src/L0/drivers/wireless/driver_brcm.c",
    "kernel/camera.c": "src/L0/drivers/camera/driver_camera.c",
    "kernel/mjpeg.c": "src/L0/drivers/camera/lib_mjpeg.c",
    "kernel/uvc.c": "src/L0/drivers/camera/driver_camera_uvc.c",
    "kernel/fb.c": "src/L0/drivers/display/screen_fb.c",
    "kernel/font.c": "src/L0/drivers/display/screen_font.c",
    "kernel/font_data.c": "src/L0/drivers/display/screen_font_data.c",
    "kernel/console.c": "src/L0/drivers/display/screen_console.c",
    "kernel/keyboard.c": "src/L0/drivers/input/driver_input_keyboard.c",
    "kernel/nic.c": "src/L0/drivers/nic/driver_nic.c",
    "kernel/e1000.c": "src/L0/drivers/nic/driver_nic_e1000e.c",
    "kernel/snd.c": "src/L0/services/audio/snd_svc.c",
    "drivers/block/driver_block_blk.c": "src/L0/drivers/block/block_svc.c",
    "net/net_tcp_cc.c": "src/L0/services/net/net_tcp_cc.c",
    "kernel/arch/x86_64/arch_multiboot2.c": "src/L0/core/arch/x86_64/arch_multiboot2.c",
    "kernel/core/core_rtc.c": "src/L0/services/time/core_rtc.c",
    "core/core_rtc.c": "src/L0/services/time/core_rtc.c",
    "kernel/lib/lib_string.c": "src/L0/core/lib/lib_string.c",
    "kernel/crypto/crypto_x509.c": "src/L0/services/crypto/crypto_x509.c",
    "kernel/crypto/crypto_rsa.c": "src/L0/services/crypto/crypto_rsa.c",
    "kernel/crypto/crypto_bn.c": "src/L0/services/crypto/crypto_bn.c",
    "kernel/crypto/crypto_ec_nist.c": "src/L0/services/crypto/crypto_ec_nist.c",
    "kernel/crypto/crypto_core.c": "src/L0/services/crypto/crypto_core.c",
    "kernel/crypto/crypto_sha512.c": "src/L0/services/crypto/crypto_sha512.c",
    "kernel/crypto/crypto_curve25519.c": "src/L0/services/crypto/crypto_curve25519.c",
    "kernel/hda.c": "src/L0/drivers/snd/driver_snd_hda.c",
    "kernel/usb.c": "src/L0/drivers/usb/driver_usb.c",
    "kernel/hid.c": "src/L0/drivers/usb/driver_usb_hid.c",
    "kernel/ata.c": "src/L0/drivers/block/driver_block_ata.c",
    "kernel/blk.c": "src/L0/drivers/block/driver_blk.c" if os.path.exists(os.path.join(ROOT, "drivers/block/driver_block_blk.c")) is False else "drivers/block/driver_block_blk.c",
    "kernel/cache.c": "src/L0/drivers/block/driver_block_cache.c",
    "kernel/part.c": "src/L0/drivers/block/driver_block_part.c",
    "kernel/disk_setup.c": "src/L0/drivers/block/driver_block_disk_setup.c",
    "kernel/acpi.c": "src/L0/drivers/acpi/driver_acpi.c",
    "kernel/aml.c": "src/L0/drivers/acpi/driver_acpi_aml.c",
    "kernel/hpet.c": "src/L0/drivers/time/driver_hpet.c",
    "kernel/pmtimer.c": "src/L0/drivers/time/driver_pmtimer.c",
    "kernel/tsc.c": "src/L0/drivers/time/driver_tsc.c",
    "kernel/cmos_rtc.c": "src/L0/drivers/time/driver_cmos_rtc.c",
    "kernel/ec_nist.c": "src/L0/services/crypto/crypto_ec_nist.c",
    "kernel/crypto_ec_nist.c": "src/L0/services/crypto/crypto_ec_nist.c",
    "kernel/rtc.c": "src/L0/services/time/core_rtc.c",
    "kernel/sdhci.c": "src/L0/drivers/sd/driver_sdhci.c",
    "kernel/sched.c": "src/L0/core/core_sched.c",
    "kernel/usermode.c": "src/L0/core/core_usermode.c",
    "kernel/syscall.c": "src/L0/services/sysapi/core_syscall.c",
    "kernel/ota_ab.c": "src/L0/services/selfhost/ota_ab.c",
    "kernel/ota_update.c": "src/L0/services/selfhost/ota_update.c",
}
INC_MAP = {
    "-Ikernel/net": "-Isrc/L0/services/net",
    "-Ikernel/drivers": "-Isrc/L0/drivers",
    "-Ikernel": "-Isrc/L0/core",
    "-Ikernel/lib": "-Isrc/L0/core/lib",
    "-Ikernel/crypto": "-Isrc/L0/services/crypto",
    "-Ikernel/mem": "-Isrc/L0/core/mem",
    "-Ikernel/core": "-Isrc/L0/core",
    "-Ikernel/arch/x86_64": "-Isrc/L0/core/arch/x86_64",
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
                                   "src/L0/core/core_registry.c"],
             # v2.0.0: block_svc.c probe path walks the driver registry
             "host_blk512_gate_test.c": ["tests/host_registry_stub.c",
                                          "src/L0/core/core_registry.c"]}


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


# 3.0.0-WP03 G2: src/L0 layout.  "-Isrc/L0" lets the tests keep their
# historical path-shaped includes ("core/types.h", "drivers/input/x.c").
SAFE_INCS = ["-Isrc/L0", "-Isrc/L1", "-Isrc/L0/core", "-Isrc/L0/core/lib",
             "-Isrc/L0/core/mem", "-Isrc/L0/core/generated",
             "-Isrc/L0/core/arch/x86_64", "-Isrc/L0/services/crypto",
             "-Isrc/L0/services/selfhost", "-Isrc/L0/services/klog",
             "-Isrc/L0/services/health", "-Isrc/L0/services/crash",
             "-Isrc/L0/services/power", "-Isrc/L0/services/time",
             "-Isrc/L0/services/fs", "-Isrc/L0/services/net",
             "-Isrc/L0/services/sysapi", "-Isrc/L0/services/block",
             "-Isrc/L0/services/audio", "-Isrc/L0/services/display",
             "-Isrc/L0/services/font", "-Isrc/L0/services/encoding",
             "-Isrc/L0/services/ipc", "-Isrc/L0/services/loader",
             "-Isrc/L0/services/camera", "-Isrc/L0/services/usb",
             "-Isrc/L0/drivers", "-Itests",
             "-Isrc/L0/drivers/block", "-Isrc/L0/drivers/nic",
             "-Isrc/L0/drivers/snd", "-Isrc/L0/drivers/usb",
             "-Isrc/L0/drivers/input", "-Isrc/L0/drivers/display",
             "-Isrc/L0/drivers/pci", "-Isrc/L0/drivers/i2c",
             "-Isrc/L0/drivers/acpi", "-Isrc/L0/drivers/time",
             "-Isrc/L0/drivers/wireless", "-Isrc/L0/drivers/bluetooth",
             "-Isrc/L0/drivers/camera", "-Isrc/L0/drivers/misc",
             "-Isrc/L0/drivers/scsi", "-Isrc/L0/drivers/sd",
             "-Isrc/L0/boot"]


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
