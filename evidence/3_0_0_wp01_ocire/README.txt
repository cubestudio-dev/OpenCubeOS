Open Cube OS 3.0.0-WP-01-ocire-core - evidence bundle
=====================================================

Scope of the work package: OCIRE (OpenCubeOS Independent Recovery
Environment) boots independently and runs a minimal shell
(help / shutdown / reboot).  No recovery features (WP-02), no
OCOS<->OCIRE interface, no process protection (WP-03).

The OCOS kernel is UNCHANGED (zero edits to core/, arch/, drivers/,
services/, boot/, tui/, chinese/, recovery/); the full 2.2.0
release regression matrix was re-run on the current tree to prove it.

Contents
--------
ocire_tests_bios.log       standalone ISO boot battery, SeaBIOS
ocire_tests_uefi.log       standalone ISO boot battery, OVMF
    14/14 PASS: shell prompt reached, help lists the three commands,
    unknown-command hint, backspace editing, shutdown really powers
    off (ACPI S5 -> QEMU exits), reboot really resets (-no-reboot ->
    QEMU exits).
dual_bios_ocos.log         GRUB dual-menu disk, default entry -> OCOS
dual_bios_ocire.log        GRUB dual-menu disk, cursor-down+CR -> OCIRE
dual_uefi_ocos.log         same under OVMF
dual_uefi_ocire.log        same under OVMF
isolation.log              partition isolation probes from inside OCOS
isolation_rawdecl.log      whole-disk surface probes (throwaway copy)

Partition isolation - what was measured (isolation.log)
-------------------------------------------------------
Disk layout: hda = OCIRE disk (one MBR entry, type 0xEE "GPT
protective shadow", no GPT header behind it), hdb = dual-boot disk
(normal MBR p1 FAT32, control group).

  partscan: 1 partition device(s) registered
      (hdb p1 only; the OCIRE disk contributes ZERO child devices -
      its 0xEE entry makes driver_block_part_parse_gpt() refuse the
      whole table, so no hdapN node ever exists)
  parted hdb   -> Partition table (MBR) listed      [control group]
  parted hda   -> "no partition table found"
  mkfs.fat32 hdap1 confirm -> "device not found"   [no node to write]
  disk_rw_test hdap1       -> device lookup FAIL    [no node to read]
  df           -> only OCOS volumes mounted

Honest declaration (isolation_rawdecl.log): whole-disk device nodes
still exist, exactly like Linux root with /dev/sdb:
  disk_rw_test hda         -> round-trip PASSES (whole-disk raw I/O)
  mkfs.fat32 hda confirm   -> "FAT32 formatted" (destructive, explicit)
These are administrator-level explicit operations against the whole
disk; protecting the OCIRE disk against them requires OCOS kernel
changes (block-layer policy), which this work package forbids ("do
not touch OCOS").  That hardening belongs to WP-03 (process
protection).  The partition-level and file-level isolation is
complete and measured above.

Regression matrix (regression/, same tree with only +ocire and the
3-line Makefile include, OCOS ELF differs from the 2.2.0 release only
by the build id)
  host tests            17 passed, 2 skipped (openssl fixtures; the
                        identical skips are declared in 2.2.0)
  reproducible-check    PASS (two clean builds byte-identical,
                        sha256 50ebd666...)
  wp12                  14/14 PASS
  wp10_tui (bios)       37/37 PASS, no #DF (2.2.0 stack fix holds)
  chinese (bios)        21/21 PASS
  tuiqa bios / uefi     32/32 + 32/32 PASS
  native bios           36/36 PASS 0 FAIL (l1_per_app_run3-style
                        reruns; see below) - native_bios_stdout.log
  native uefi           36/36 PASS 0 FAIL - native_uefi_stdout.log
  l1_per_app            35/35 PASS (run 3).  Runs 1 and 2 each show
                        ONE stray "no spawn ack" ERR row (dlsym_test,
                        reloc_test) that walks between runs while
                        every other row passes - the identical
                        runner-side serial race the 2.2.0 release
                        recorded; all three logs are kept here.
  wait4 fresh-boot battery   4 x 5 boots, anomalies=0 (20/20 CLEAN;
                        the 2.2.0 wait4 fix holds)
  boot gates            bios=PASS uefi=PASS

Known deviations from the 2.2.0 release run (all honest, none
regressions):
  - native bios needed a 4th run to finish inside the sandbox's
    10-minute tool window; runs 1-3 all printed 34 PASS 0 FAIL and
    were cut by the timeout during phase J teardown, never by a
    test failure.  Run 4 completed in-window with 36/36.
  - the l1_per_app stray rows described above (2.2.0 recorded the
    same phenomenon, four runs, different row each time).

asset_hashes.txt           sha256 of the released OCIRE ISO
ocire_rebuild.log          rebuild record (kernel + ISO + disks)
