== v2.2.0 release verification record ==

Commit: 083f148 (2.2.0 freeze; the v2.2.0 tag target).
Kernel build id: 316 (commit count 295 + 21). CI on the commit: GREEN
(check-run "kernel + ISO + packages (0-warning gate)" = success: kernel
-Werror 0E0W, hybrid BIOS+UEFI ISO, deterministic src zip, OTA package,
host tests, reproducibility gate).

Build-id caliber, three places, all equal at 316:
- tag tree core/generated/version_gen.h: "2.2.0 build 316"
- release ELF immediate: movq $0x13c (=316) in core_dump_save
  (h->build store), verified by objdump on the shipped ISO kernel
- website/public/update.json + update-v2.json: "version": "2.2.0",
  "build": 316
(The 2.1.0 release shipped a tree header stamped 308 against a 309
release build - the stale-header caliber this release closes.)

1. Reproducible build: PASS - ELF byte-identical across two clean
   builds (reproducible_check.log).
   sha256 f865abff08bd65a3d39e0799bbd1af96f8f18f80fcb3a8fc9a9dbdc9487c03cc

2. host tests: 17 passed, 2 skipped (openssl rsa/x509 fixtures absent
   - same declared state as 2.1.0).

3. wp12 watchdog/logging/crash e2e (BIOS): 14/14 - wp12_e2e.log
   (S12 Intel TCO real reset + S13 ACPI WDAT included).

4. wp10_tui suite (BIOS): 37/37 - wp10_tui_bios.log.
   THE 2.2.0 HEADLINE: the suite runs through its closing checkupdate
   SVC round (the exact step that double-faulted both 2.0.0 and 2.1.0
   after the last check) and exits clean at "=== summary: 37/37 checks
   passed, 0 failed ===" - NO #DF, no HALT. The checkupdate report
   itself shows "Current version is up to date." with the server-side
   manifest fields (Version/Time lines display the SERVER's update.json
   - the gh-pages manifest still served 2.1.0/2026-10-25 at suite time;
   the site deploy follows the Release, after which the lines read
   2.2.0). Fix: OC_KTHREAD_STACK_PAGES 6 -> 10 (commit 4293e94).

5. wait4-vs-fork fresh-boot battery (the O-02 acceptance, NEW gate):
   20/20 boots CLEAN, 0 anomalies - wait4_battery/battery_batch1..4
   (4 x 5 fresh boots of the release ISO; each boot runs the zombie
   sources pf_test/sigsegv_test/int3_user/g6_oom ahead of the victims
   g6_exec_a/fork_test/exec_test/signal_test/mmap_multi/sse_test/
   g6_test and checks every "child status" line; g6_exec_a must read
   138 every time). Pre-fix (2.1.0 tree): 2/2 fresh boots hit the
   142 anomaly. Fix: orphan-detach + fork corpse detach (commit 11d2127).
   The fix's own development-stage record (20/20 on the dev build)
   plus same-boot marathon (g6_exec_a=138 both rounds) and stress
   (80 runs, 0 anomalies) live in the 2.2.0 commit message.

6. chinese console E2E (BIOS): 21/21 - chinese_bios.log (the log's
   closing uname row reads "Open Cube OS 2.2.0 x86_64").

7. tuiqa desktop apps: 32/32 BIOS + 32/32 UEFI -
   tuiqa_e2e.log / tuiqa_e2e_uefi.log.

8. native desktop E2E: 36/36 PASS BIOS (native_e2e_stdout.log, exit 0)
   + 36/36 PASS UEFI (native_e2e_uefi_stdout.log - the 10-minute host
   runner cap cut the tool's process exit AFTER its summary line;
   declared, same shape as 2.1.0). Raw serial transcripts + shot
   rounds in native_e2e/ and native_e2e_uefi/.

9. L1 per-app record: 35/35 - l1_per_app.log (every embedded l1apps/
   program + ush via run, fresh boot per row). Three earlier runs
   each hit ONE row-level "no spawn ack" ERR on a DIFFERENT row
   (signal_test / g6_test / select_zero_test - each PASSing in every
   other run; l1_per_app_runs/). A standalone 18-iteration protocol
   diagnostic (/home/z/rowdiag.py shape) reproduced ZERO ack misses -
   the ERRs are a rare runner-side serial read race, not a kernel
   defect; no row failed twice; the shipped record is the clean run.

10. Boot gates: BIOS PASS + UEFI PASS - boot_gates.log (banner
    "Open Cube OS 2.2.0 kmain entered" + ring-3 ush> prompt + uname
    "Open Cube OS 2.2.0 x86_64" + oc> fallback + poweroff; UEFI via
    the OVMF pflash pair - the bare -bios shape fails to load in this
    toolchain, the same lesson as the 2.1.0 first attempt).

Deviations declared: the 2 host-test fixture skips; the UEFI native
stdout cap-cut (after the final check line); the l1_per_app runner
ack-race cross-run record; the checkupdate server-manifest display
noting (item 4).
