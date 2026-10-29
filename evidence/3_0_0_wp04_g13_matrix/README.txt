Open Cube OS 3.0.0-WP04 G13 - full acceptance matrix evidence
=============================================================

1. Clean-build determinism (double_clean_sha256.txt)
   Two FULL clean builds of the final tree (make clean; make userprogs;
   make kernel; make iso), ~90 s apart:
     elf  a61d7fc1ea58b1f568c40d96545a5c2ac94f39b96339389839ca00c9d3b24c8b
     iso  92096640f42f39d77f15a39d1ed0f3467a77f61aaae5cdef9a1d6f5df71240de
   BYTE-IDENTICAL across both builds.  The nondeterminism sources found
   and pinned in this batch (each verified by forced rebuilds):
     - gzip streams in gen_userprogs.py / gen_font_gz.py: mtime=0
     - xorriso: --modification-date + --set_all_file_dates
       (YYYYMMDDhhmmsscc form - a bare '0' is REJECTED and silently
       leaves dates unpinned; caught via a decoded root record)
     - xorriso --gpt_disk_guid (auto mode randomizes per run)
     - staged tree mtimes: find -exec touch -d @0 (directory records;
       --set_all_file_dates pins files only)
     - ESP FAT image: mformat -N 1 (volume serial was wall-clock
       derived) + tools/fat_zero_dates.py (every dir-entry date/time
       field zeroed; LFN fragments preserved)
     - THE LAST MILE: xorriso hardcodes the Rock Ridge TF ctime stamp
       to the image-assembly moment and -alter_date has no ctime type;
       tools/iso_zero_ctime.py walks all ISO9660 records and zeroes
       every TF ctime stamp post-build (593 stamps; ISO records carry
       no checksums, the patch is structurally safe; boot re-verified).

2. Zero L0 command-name residue (grep proof)
   All 251 baseline command names grepped against src/L0/services/
   sysapi + src/L0/boot: 6 hits, ALL comments (l1svc_defs.h category
   folder notes; a klog_write tag in core_syscall.c; a POSIX execv
   note).  OC_SHELL_CMD appears 3 times - all in comments ("is gone",
   migration notes).  The L1SVC ABI is purely numeric (cat, op); no
   name-based command dispatch surface exists in L0.

3. Parity matrix vs the real v2.2.0 (parity_*.txt)
   Reference: v2.2.0 tag built with the same toolchain
   (elf 2,118,992 B / ISO 12,392,448 B), booted BIOS side by side.
     - 21 ush-level commands on both: stats, uname -a, health,
       watchdog status, mem, frag, heap, sched, syncstat, crashlog,
       acpitest, i2ctest, smbustest, rtc, tsc, memtest, vmmap,
       chinesetest, ime-test, encoding-test, font.
     - 13 of those are v2.2.0 oc> (L0) commands - re-run on v2.2.0
       after `exit` from ush for the true comparison.
   Results: EVERY command structurally identical; the only expected
   deltas are volatile state (ticks, counters, free bytes, heap block
   counts - the gz caches changed allocation history) and version
   strings (2.2.0 vs 3.0.0-WP03).
   REAL divergence caught and FIXED (F8): the G6 health port had
   rewritten v2.2.0's hc_line() (label space-padded to column 22, no
   colon) into a "    label: value" style and mislabeled it
   v2.2.0-verbatim.  Restored to the true form; health now matches
   line-for-line (health_hc_line_fix.txt).
   Documented rescoping (not divergence): chinesetest swaps the
   v2.2.0 [11] ush line-editing section for the WP-03 [12] font
   engine registry section (same 77/77 total) - declared in the
   WP-03 report.
   Section-gluing artifacts in the v2.2.0 capture (late rtc bytes
   landing in the tsc section) are harness timing, verified against
   the raw outputs.

4. BIOS/UEFI dual boot on the final deterministic ISO
   (bios_smoke.txt + G12 folder): boot-to-ush both firmwares,
   echo/tui-test 93/93/stats green on BIOS; UEFI ime-test PASS.

5. 0 errors 0 warnings on both final clean builds.
