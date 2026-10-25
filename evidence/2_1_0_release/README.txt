== v2.1.0 release verification record ==

Commit: a7f8859 (2.1.0 freeze/docs tree; the v2.1.0 tag target).
Kernel build id: 309 (commit count 288 + 21). CI on the commit: GREEN
(run 40223104...-era; check-run "kernel + ISO + packages (0-warning
gate)" = success: kernel -Werror 0E0W, ISO, src-zip, OTA, host tests
17 passed + 2 skipped, reproducible-check PASS).

1. Reproducible build: PASS - ELF byte-identical across two clean
   builds (local: reproducible_check.log; CI: same gate green).
   sha256 887c0587c6d07b29b6421fa3960cee57766a682dce809bbe3d429c95bd8ede2c

2. host tests: 17 passed, 2 skipped (openssl fixtures) - host_tests.log
   (CI shows the same 17+2 on the runner toolchain.)
3. wp12 watchdog/logging/crash e2e (BIOS): 14/14 - wp12_e2e.log
   (S12 Intel TCO real reset + S13 ACPI WDAT included)
4. wp10_tui suite (BIOS): 37/37 - wp10_tui_bios.log
   DECLARED: after the last check the kernel #DF'd (kernel stack
   exhausted in the ush task, task=ush) and HALTed; the same
   behavior is present on the 2.0.0 ISO (verified live:
   /tmp/wp10_tui_200.log run on opencube-2.0.0.iso shows the same
   37/37 + tail #DF) - a pre-existing 2.0.0-era defect, declared in
   KNOWN_ISSUES.md, kernel fix deferred to the next batch.
5. chinese e2e (BIOS): 21/21 - chinese_bios.log
6. tuiqa desktop apps: BIOS 32/32 + UEFI 32/32
   (tuiqa_e2e.log, tuiqa_e2e_uefi.log)
7. native TUI e2e: BIOS 36 PASS / 0 FAIL + UEFI 36 PASS / 0 FAIL
   (native_e2e.log/ + native_e2e_stdout.log, native_e2e_uefi.log/ +
   native_e2e_uefi_stdout.log). DECLARED: the runner-side 10-minute
   host cap cut each run after the final teardown checks (all 36
   PASS lines are in the stdout logs; the suite's own summary line
   landed after the cap). 0 FAIL anywhere.
8. L1 per-app record (2.1.0 gate, tools/run_l1_per_app.py): 35/35
   PASS - l1_per_app.log (+ l1_per_app_raw_notes.txt, raw row bytes).
   Every embedded l1apps/ program executed via `run` from oc>, one
   fresh boot per row; ush exercised as a nested spawn+exit; loop
   verified as the by-design spinner. DECLARED: a rare wait4-vs-fork
   timing flake was observed in marathon-mode logs (declared in
   KNOWN_ISSUES.md); the fresh-boot record shows the deterministic
   138=128+SIGUSR1 PASS for g6_exec_a.
9. Boot gates, BIOS + UEFI: PASS - boot_gates.log (banner + ring-3
   ush> + uname "Open Cube OS 2.1.0 x86_64" + oc> fallback +
   poweroff; both on the 18:23 test container and re-verified on the
   FINAL shipped ISO bytes).
10. help -a runtime capture (oc>, L0 authority): 99 commands, 2.1.0
    banner - ../help_a_runtime.txt.

Assets (build 309, sha256):
808b5423127fa6ee802f84d9edd2db2639052a57946ff554512787638cdcb9de  build/opencube.iso
21b52c98d9224574674f4fbea31fbe71b411e2e5618281cf545376abe4bc480b  dist/OpenCubeOS-2.1.0-update.tar.gz
8dba53d3fe6bd3d3c0615aa025a406616b4a72d23877e9eb3429eb6c4588aa14  dist/OpenCubeOS-src-2.1.0.zip

Note on container vs kernel bytes: the kernel ELF inside every
asset is byte-identical (reproducible-check). The ISO/OTA container
bytes embed build timestamps (xorriso volume dates, gzip header
mtime); the deep suites ran on the ISO built from the same commit
(kernel-identical), and the final shipped container was re-verified
with the BIOS+UEFI boot gates above.
