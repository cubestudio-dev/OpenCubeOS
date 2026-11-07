WP-02 ocire_recovery - evidence bundle (2026-10-27)
====================================================

All logs in this directory were produced on 2026-10-27 from the
WP-02 recovery tree (OCIRE 3.0.0-WP-02, kernel 60224 bytes, build
stamp 324) with the toolchain at /home/z/opt/extract
(qemu-system-x86_64 10.x, GRUB 2.12-9+deb13u2 via the extract tree,
mtools, OVMF).

Batteries and their logs
------------------------

1. OCIRE standalone battery - 14/14 PASS (BIOS + UEFI)
   make ocire-test  ->  tools/run_ocire_tests.py
   ocire_tests_shell_bios.log / ocire_tests_shell_uefi.log
   (boot/help/unknown/backspace; shutdown + reboot logs from the
   same run live in build/ocire/test-*.log - the two shell logs here
   show the boot + banner + help interactions on both firmwares)

2. Dual-boot + isolation battery - 11/11 PASS
   make ocire-dual-test  ->  tools/run_ocire_dual_tests.py
   dual-dual_bios_ocos.log   menu default (supervisor) -> kexec -> ush>
   dual-dual_bios_ocire.log  menu pick entry 2 -> full recovery shell
   dual-dual_uefi_ocos.log   same under OVMF
   dual-dual_uefi_ocire.log  same under OVMF
   isolation.log             OCOS from CD: partscan/party/mkfs/disk_rw
                             probes - the OCIRE disk contributes zero
                             child devices
   isolation_rawdecl.log     whole-disk surface declaration (throwaway)
   WP-02 note: the dual cases boot THROWAWAY disk copies because the
   supervisor is stateful (WAIT_BOOT_SIGNAL persists until WP-03's
   OCOS-side signal); the isolation probes are unchanged from WP-01.

3. WP-02 recovery shell battery - 4/4 PASS
   make ocire-shell-test  ->  tools/run_ocire_shell_tests.py
   shell-healthy.log      pristine disk: help/logread/syscheck 10/10,
                          repair quick idempotent (0 restored,
                          5 unchanged, 0 failed), syscheck 10/10 again
   shell-damaged.log      one byte flipped in hd0's OPENCUBE.ELF magic:
                          syscheck reports DIGEST MISMATCH (9 OK,
                          1 problem), repair quick restores + verifies
                          ("1 restored, 4 unchanged, 0 failed"),
                          syscheck back to 10/10 System healthy
   shell-fullrepair.log   hd0 p1 BPB destroyed (0xDEADBEEF x128):
                          hd0's own GRUB cannot read its config any
                          more, so the machine boots the STANDALONE
                          OCIRE ISO (-cdrom ocire.iso); syscheck shows
                          the system volume FAIL; repair full (typed
                          YES) locates the partition by its MBR entry,
                          reformats FAT32, restores all 5 golden files
                          with digest verification, resets the log
                          ring + mailbox; final syscheck 10/10 healthy
   shell-wdt-timeout.log  the REAL i6300esb watchdog attached
                          (-device i6300esb, no -no-reboot): supervisor
                          arms the 60 s window -> kexec -> OCOS boots
                          (ush>) and never signals (WP-03 boundary) ->
                          watchdog RESET at the 60 s mark -> GRUB ->
                          supervisor sees the stale WAIT_BOOT_SIGNAL ->
                          crash notice + fail_count++ -> ENTER_FULL ->
                          reboot -> supervisor runs the FULL SHELL
                          (ocire>).  This is the WP-02 boot-failure
                          detection feature proven end to end.

4. Host-side FAT write-set harness - 33/33 PASS
   fat_host_harness.log   tools/ocire_fat_host_test.c (in-tree):
                          the REAL ocire_fat.c/ocire_sha256.c/
                          ocire_util.c compiled on the host against a
                          copy of the real hd0 p1 volume:
                          2070-cluster create + digest
                          round-trip, overwrite (1465 clusters),
                          copy_stream into a new path AND over an
                          existing path, edge sizes (exact cluster
                          multiple / 1 byte / 1.5 clusters), over-long
                          names rejected (create/lookup/mkdir/copy),
                          mkdir + subdir file, read_range offsets.
                          This harness caught four real FAT bugs
                          during development (see WORK_LOG).

OCOS regression note
--------------------
Zero OCOS source changes in WP-02 (the only tracked OCOS-side file
touched is the generated build stamp, core/generated/version_gen.h,
which the Makefile regenerates).  OCOS is nonetheless booted and
exercised by this bundle: the dual_ocos cases kexec it (both
firmwares) and the isolation cases boot it from CD and run the
partition/filesystem probes.  The full OCOS regression matrix for a
zero-OCOS-edit tree was recorded at WP-01
(evidence/3_0_0_wp01_ocire/regression/) and applies unchanged.

Bugs found and fixed during this batch (all verified by the logs
above; details in docs/archive/WORK_LOG.md)
---------------------------------------------------------------------
F1. ocire_fat.c copy_stream: the chain terminator was written on the
    second-to-last cluster when a file ended mid-cluster -> every
    restored file was one cluster short (digest mismatch, syscheck
    I/O ERROR).
F2. ocire_fat.c alloc_cluster did not mark the allocated cluster ->
    two consecutive allocations returned the SAME cluster and each
    absorbed two data blocks (a 2070-cluster file became 1035
    clusters holding every even 1 KiB block).  Fixed with the
    0x0FFFFFF7 reservation marker (+ free_chain stopping at it).
F3. ocire_fat.c name_to_83 silently truncated >8-char bases and
    dropped their extension; the path splitters silently truncated
    >12-char components and treated the remainder as a NEW path
    component (observed live: "EDGE_EXACT.BIN" split into
    "EDGE_EXACT.B" + "N", the "N" looked up inside a garbage
    directory cluster).  All five call sites now reject non-8.3
    names.
F4. ocire_fat.c alloc_cluster rescanned the FAT from cluster 2 for
    every allocation (O(n^2): a quick repair took 9 minutes).
    A free_hint cursor made it 13.7 s.
F5. ocire.mk never included the OCIRE .d files -> header changes
    linked STALE objects (found as MBR corruption: the mixed build
    read ocire_layout_t fields at shifted offsets and wrote garbage
    start LBAs into both MBRs).  CI was never affected (fresh clone
    every run); local incremental builds were.
F6. ocire_disk.c scan returned -2 before locating the mailbox/log
    files -> repair full on an unmountable system volume rebuilt the
    mailbox without log LBA or agent digest.
F7. ocire_recover.c repair full required a MOUNTABLE system volume -
    the exact condition it exists to fix.  Now falls back to
    locating the partition by its MBR entry.
F8. ocire_recover.c repair full step 1 reported "REBUILT" for an
    already-correct MBR (rc>=0 instead of rc>0).
