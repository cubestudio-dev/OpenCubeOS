3.0.0-WP03 G6 - the full acceptance matrix
===========================================

[1]  clean build, twice:        174 compile lines, 0 errors, 0 warnings
                               (build_clean.log; -Werror throughout)
[2]  reproducible:              two clean builds -> byte-identical ELF
                               sha256 55d930aa97d1cd93803f1ff6ac845ea2
                               93f06fe10f822db3025c368db95a332 (both)
[3]  bootable ISO:              dual El Torito (BIOS + UEFI), built clean
[4]  BIOS boot:                 boot_bios.log - all four "boot chain"
                               markers in order, "L0 ready, waiting for
                               L1", ush (ring 3) boots, stats + wifi
                               (7 wireless families) answer
[5]  UEFI boot:                 boot_uefi.log - all four markers, L0
                               ready (OVMF)
[6]  L0 user plane deleted:     greps: "oc> " strings in the ELF = 0;
                               recovery/ tui/ chinese/ dirs = 0;
                               interactive_loop/tui_/pinyin/lineedit/
                               IME code hits = 0 (rename-history
                               comments only)
[7]  directory structure:       src/L0/{boot,core,drivers,services} +
                               src/L1 (the spec's layout, G2)
[8]  dependency direction:      tools/check_deps.py = 0 violations
                               (181 files scanned; core -> nothing
                               above it, drivers -> no services; the
                               G5 surgery took it from 23 to 0)
[9]  registry mechanism:        every module identity self-declared
                               (.oc_drvs/.oc_svcs/.oc_apps; G3), zero
                               register-class boot calls in kmain
[10] extension live test:       G3 evidence - one file + one
                               OC_DRIVER_DECL in its own folder ->
                               collected at runtime (7->8 wireless
                               families; usb exttest-cls), then
                               removed; the stale-.o relink hazard
                               documented
[11] boot order:                the five-phase dependency chain (G5):
                               four "boot chain N/4" markers asserted
                               in order on both firmwares
[12] terminal state:            "L0 ready, waiting for L1" then the
                               launcher hands the machine to ush - the
                               expected pure-service-kernel state
[13] kernel abilities kept:     klog ring (core mechanism + the
                               services/klog on-disk sink), health
                               monitor (1 s samples), crash capture
                               (dump + crashlog + panic slots), soft +
                               hardware watchdog (TCO/WDAT) - all in
                               the boot logs
[14] OCIRE five regression items: battery_supervised.log (3/3):
                               partition isolation (parted/disk_rw on
                               the 0xEE disk), boot signal (SIGNAL_OK,
                               boots 1, verified on-disk from OCIRE),
                               bidirectional interfaces (mailbox write
                               + LOG_APPEND + KLOG_SNAPSHOT, the ring
                               holds the agent's snapshots), process
                               protection (kill refused, ocire_guard
                               stays), whole-disk hard protection
                               (disk_rw_test on the recovery disk
                               REFUSED; unsupervised = open, declared)
[15] OCOS regression:           battery_wp01_dual.log 11/11 +
                               battery_wp02_shell.log 4/4 (the wdt
                               case re-pointed at the closed loop per
                               the WP-02 declared semantic change)
[16] host tests:                17 passed / 0 failed / 2 fixture-skip
                               (the baseline)

REMAINING for G7 (docs + release chain): the interface-surface old/new
mapping table (gen_api_docs.py regeneration against the new tree),
ARCHITECTURE.md / docs updates, website data, the WP-03 milestone
artifact + the three-way SHA record, CI green, the delivery report.
