Open Cube OS 3.0.0-WP04 G15 - release-chain evidence
====================================================

The WP-04 close batch.  The freeze commit 2ae6a2b ("version literal
3.0.0-WP04, build 348") is the release commit of the milestone: the
tag v3.0.0-WP-04 points at it and the artifacts below are built
from exactly that tree.

1. Determinism (double_clean_sha256.txt)
   Two full clean builds of 2ae6a2b, byte-identical:
     elf  31c7c8844ae33d91a4e8dba5653aeb21024f4a626244eddcbab984b7c75125bf  (2,077,472 B)
     iso  b2697ab8617ed21ec20a541f5f658d62eb6a095a40ca69bf3e7796b96aa153c3  (12,351,488 B, dual El Torito)
   0 errors 0 warnings both builds.  The G13 six-layer pinning stack
   (gzip mtime=0, xorriso date/GUID pinning, touch @0, FAT date
   scrubbing, iso_zero_ctime.py) is unchanged and still sufficient.

2. Boot gates (bios_smoke.txt + uefi_boot.log)
   BIOS (SeaBIOS El Torito) via tools/qemu_runner.py:
     [oc] Open Cube OS 3.0.0-WP04 kmain entered
     [..] L0 ready, waiting for L1.......... OK
     l1 launcher: booting 'ush' (ring 3, 29064 bytes)
     ush>  -> uname -a: "Open Cube OS 3.0.0-WP04 x86_64"
     echo det-iso-boots-ok OK; stats OK; tui-test 93/93 all green.
   UEFI (OVMF pflash CODE+VARS): same banner, same launcher line,
   auto-entry to ush> (exit code 124 = the shell was still waiting
   for input at the harness timeout - the boot itself completed).

3. Build-id correction (recorded in double_clean_sha256.txt)
   The G13-era artifact had baked "build 24" from the shallow-clone
   count (3 + 21), below the published 316 - the post-reset recovery
   clone was the cause.  History was restored (count 326) before the
   freeze commit; build 348 (327 + 21) is monotonic above 316 and
   matches what CI (fetch-depth: 0) and any full-clone builder
   produce from the same commit.  Verified in the binary: the crash
   header's build field is the immediate 0x15C (core_crash.o
   `movq $0x15c,0x10(%rbx)`), and the sysinfo/crash string prefix
   "Open Cube OS 3.0.0-WP04 build " is baked with the -D flags.

4. Asset hashes (asset_hashes.txt)
   The milestone assets (canonical upload names):
     opencube-3.0.0-WP-04.iso  b2697ab8617ed21ec20a541f5f658d62eb6a095a40ca69bf3e7796b96aa153c3
     opencube-3.0.0-WP-04.elf  31c7c8844ae33d91a4e8dba5653aeb21024f4a626244eddcbab984b7c75125bf
   Three-way identity (in-tree website/public/downloads/ == GitHub
   Release API digest == live gh-pages download) is verified in the
   delivery dossier (the private audit record of this batch).
