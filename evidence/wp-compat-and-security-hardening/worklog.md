# Open Cube OS WP-compat-and-security-hardening worklog
# 2026-11-25, cubestudio-dev <cubestudio@qq.com>

Environment (fresh sandbox):
- clone: public main @d1a059e, unshallowed to 497 commits (build = commits + 21)
- toolchain: mirror-dl.sh 108 debs + 9 version-refreshed debs from TUNA,
  extracted to ~/opt/extract (QEMU 10.0.13, NASM 2.16.03, xorriso 1.5.6,
  grub-mkrescue, OVMF, gcc 14.2.0) - baseline make all 0e/0w
- tokens: tokenA (JGAL) and tokenB (FyqA) BOTH verified HTTP 200 against
  api.github.com (the earlier 401 was, as the user said, a local network
  condition; GnuTLS hiccups on push were retried through per the red line)

Functionality points (each: code -> make all 0e/0w -> acceptance ->
commit -> push public + mirror private -> disaster export to
/home/z/my-project/download/):

1. B2 OCIRE unconditional write-arm (fd4906d / mirror 8cbc61ff)
   - ocembed_early_protect() scans every block device for OCIREAB1
     (LBA 1024) / OCIREMB2 (LBA 2048) right before partscan, arms
     read-only at the driver layer on EVERY boot
   - both L0 write entries return -OC_EPERM (new include/oc_errno.h);
     the kernel trusted channel stays open
   - mkfs.fat (new L1 alias app) + /dev/ prefix + command-layer EPERM
     reports; partscan re-arms (defense in depth); disk_rw_test honest
   - L1 boundary audit: evidence/.../security-audit-l1-boundary.md
   - QEMU acceptance: 3/3 (arm log, mkfs refused, image sha256
     unchanged with write-through hdb)

2. B3+B4 RTC-first timing + wall-clock-bounded I/O (e7794bc6 / 2a9c461e)
   - core_timer_init(cmdline): RTC 1024 Hz PIE on IRQ8 (single
     RegC-owning handler + reference-counted install + consumer
     fan-out; half-ns accumulator, zero drift); PIT 100 Hz is the
     fallback (RTC absent or oc.tick=pit)
   - OCBL both flavors: the 50x-fast PIT arithmetic fixed (delta*50
     vs PIT_DIV); RTC seconds anchor (UIP-safe) primary
   - TSC wall-clock deadlines on every I/O wait (ATA DMA 1000 ms;
     PIO/AC97/HDA 100 ms); early kmain TSC calibration via a new
     RTC-window measurement (~31 ms); HPET -> PMT -> RTC -> PIT order
   - OCRMECL VGA marks compiled out by default (%ifdef; byte-verified)
   - [vfs] mounting/mounted tracing with elapsed ms
   - Acceptance: RTC tick + TSC cal + 9.69 s countdown + vfs trace;
     /TICKPIT.TXT degrades to "system tick: pit"; B2 re-green
   - live-caught: ocbl_fs ISO9660 extension-less marker names
     ("TICKPIT.;1" -> trailing-dot strip -> reverted after it broke
     the boot; the 8.3-friendly /TICKPIT.TXT naming is the fix)

3. B5+B6+B7 boot splash / pager / one-shot mount (3ef2e4b3 / 8416619d)
   - core_boot_splash.c: centered ASCII logo + phase progress bar; the
     whole kernel console stream captured to klog; L1_READY one-shot
     (ush's FIRST syscall, before the prompt byte) restores the hook
     chain and clears the screen
   - live-caught #1: the console hook is a STACK (kcap sits on it with
     the serial tee as base) - clearing it on finish killed the serial
     transcript for the whole session; save/restore is the fix
   - live-caught #2: the ush banner's uname fetch rides the kcap
     capture; the frozen "3.0.0-WP04" fallback literal was replaced
     with an honest hint (KNOWN_ISSUES K-08)
   - panic tears the splash down first (full-screen diagnostics back)
   - ush pager: help -a captured into 480 lines; PgDn/PgUp (0x87/0x86,
     ANSI [6~/[5~) scroll by a screen; q/Enter leave; PAGER=off env
     switch for drivers; sys_read(0) blocking key wait (no new kernel
     interface)
   - one-shot mount: 2-arg form auto-probes fat32/exfat/ext4 (each
     fs's own mount validates), OCIRE-protected device refuses EPERM,
     /dev/ prefix accepted, 3-arg form unchanged
   - Acceptance: 4/4 (splash rows + bar / clean 3-row handoff / PgDn
     24-46 + PgUp 1-23 / "mounted fat32 (auto-detected)")
   - /NOSPLASH.TXT diagnostics hook (classic boot for the batteries)

4. B8 four-asset website (feda0c5b / 1c216c1f / gh-pages 10e716d0)
   - the dev channel carries ALL FOUR assets: opencube ISO + the
     REBUILT OCSE medium + OTA + source zip
   - live-caught: the OCSE tree had not linked since WP-07 (the
     vendored power path's boot_state_sleep_* references) - the dev
     channel's ocse asset was frozen at WP-l1apps-merge;
     ocse/kernel/core/core_state_stub.c fixes the link, ocse rebuilt
   - tools/update_website.sh generates the dev quad; download.html
     four cards (bilingual); app.js renders the quad
   - release line: 3.0.0-WP-compat-and-security-hardening build 521
   - DOM assertion (agent-browser): all four buttons populated; local
     asset 200s; gh-pages deployed and API-verified
   - data refresh (65a986f8): regenerated fact source 313 commands /
     166,643 lines / 293 programs

5. Full regression + docs (this batch)
   - live-caught: tools/run_host_tests.py's include remaps still
     targeted the dead src/L0 layout (NAK) - the 31/31 battery had
     been silently build-failing; INC_MAP/SAFE_INCS/SRC_MAP moved to
     ocos/, the core/ symlink serves the historical core/types.h
     includes, host_blk512's renamed header + framework links fixed
   - RSA fixture regenerated locally (openssl) - the RSA test runs
   - strict matrix verdict chain OCRMECL -> ush> (the "boot complete"
     banner is console output the splash legitimately backgrounds)
   - README current-release paragraph rewritten; KNOWN_ISSUES K-08/
     K-09/K-10 recorded; docs/data regenerated; gh-pages data refresh
   - battery results: see evidence/.../regression-summary.txt

Push ledger (all verified on the remotes after push):
  public main : fd4906d -> e7794bc6 -> 3ef2e4b3 -> feda0c5b -> (final)
  private main: 8cbc61ff -> 2a9c461e -> 8416619d -> 1c216c1f -> (final)
  gh-pages    : 10e716d0 -> 65a986f8
  CI          : success on every push (latest run on the final head)
EOF marker: WP-compat-and-security-hardening executed end-to-end.
