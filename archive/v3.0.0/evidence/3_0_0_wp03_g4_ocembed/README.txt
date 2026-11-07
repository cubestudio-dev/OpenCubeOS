3.0.0-WP03 G4 - the OCOS side of the OCIRE embed (services/ocembed)
=====================================================================

WHAT THIS CLOSES (the WP-02 honest boundary, verbatim from its
declaration: "OCOS 侧闭环缺失——不解析 oc.* 命令行事实、不写
SIGNAL_OK、不停 i6300esb、agent 的 L1SVC_CAT_OCIRE 通道不存在"):

1. oc.* FACT PARSING        kmain 3b: oc.mb / oc.log / oc.logsecs /
                             oc.wdt=ib700 / oc.agent=1 parsed from the
                             kexec cmdline; the agent module ("ocire-
                             agent" mb2 tag) captured (both the spec
                             offset-16 and the OCIRE offset-20 string
                             spellings - ocire_kexec.c writes the
                             marker 4 bytes past the multiboot2 spec
                             position; accommodated, ocire/ untouched)
2. BOOT SIGNAL               kmain 16d: mailbox WAIT_BOOT_SIGNAL ->
                             SIGNAL_OK (+boot_count), through the
                             trusted block channel
3. WATCHDOG DISARM           the i6300esb the supervisor armed is
                             stopped inside the 60 s window: unlock
                             (0x80/0x86) + reload (0x100) + a TRUE
                             8-BIT write to PCI 0x68 clearing the
                             enable bit (the QEMU model handles the
                             LOCK/ENABLE register ONLY on len==1
                             writes; the generic 32-bit config write
                             never reached the enable logic - found
                             live: the disarmed wdt still fired its
                             stage-1 expiry)
4. AGENT SPAWN + PROTECTION  the injected ELF spawns as "ocire_guard"
                             (user_proc_t.ocire_guard); sys_kill
                             refuses EVERY signal to it (kill tested:
                             "kill: no such process" + ps still lists
                             it).  Layer 2 is its binary living on the
                             OCIRE volume.  No nice syscall exists in
                             the ABI (nothing to refuse); job control
                             cannot target it (jobs are created only
                             via job_create; the agent never is)
5. L1SVC_CAT_OCIRE=18        ops 4 (LOG_APPEND) + 5 (KLOG_SNAPSHOT),
                             guard-only: the dispatch checks the
                             caller's ocire_guard flag; any other
                             caller is refused and klog-logged
6. WHOLE-DISK HARD PROTECT   every write through a block device whose
                             sector at oc.mb carries the OCIREMB2 magic
                             is REFUSED (cached + raw + the MBR-write
                             helper; reads stay open).  The embed's own
                             writes go through the kernel-internal
                             trusted channel.  disk_rw_test hdb (the
                             recovery disk in the dual topology):
                             4x REFUSED + honest FAILs
7. LOG RING CHANNEL          the agent appends through the kernel
                             channel: header (magic OCIRELOG1 + u32
                             cursor + u24 wrap-sequence), read-modify-
                             write per sector, header updated LAST
                             (crash-safe), wrap at capacity

FIVE SPEC REGRESSION ITEMS -> where they are proven:
  partition isolation ....... supervised-bios.log (parted hdb: "no
                              partition table found", disk_rw_test
                              hdbp1: "not found") + the WP-01 battery
                              (11/11, partscan zero children for the
                              0xEE disk)
  boot signal ............... supervised-bios.log ("boot signal written
                              (SIGNAL_OK, boot_count+1)") +
                              supervised-verify.log (OCIRE syscheck:
                              "state SIGNAL_OK, boots 1")
  bidirectional (3) ......... mailbox write (above) + LOG_APPEND +
                              KLOG_SNAPSHOT (supervised-wdt-verify.log:
                              the ring holds the agent's kernel-log
                              snapshots + heartbeats, 12+ cycles)
  process protection ........ supervised-bios.log (kill 1 refused, ps
                              still lists ocire_guard)
  whole-disk hard protect ... supervised-bios.log (disk_rw_test hdb
                              REFUSED) + unsupervised-open.log (the
                              same disk WITHOUT supervision stays
                              writable - the protection is a property
                              of the supervised relationship, the WP-01
                              rawdecl honest declaration unchanged)

THE SPEC'S "disk_rw_test hda": in the supervised dual-boot topology
the recovery disk is hdb (GRUB must live on hd0 to boot); the spec's
"hda" follows the WP-01 isolation topology where the OCIRE disk sits
at index 0.  The protection follows the MAILBOX FACT, not the disk
index - it refuses the covering device whatever its name (verified on
hdb here; the unsupervised case runs the hda-named variant open).

LIVE BUGS FOUND AND FIXED ON THE WAY (all in OCOS, ocire/ untouched):
  F1  sys_l1svc rejected the agent's out=0/cap=0 sleep spelling ->
      fire-and-forget calls now render into a kernel scratch buffer
      (the sleep used to return -1 instantly -> the agent busy-looped)
  F2  the power sleep ran under the kcap console capture: a 5-second
      park with the capture hook installed swallowed every console
      line printed meanwhile -> the sleep op now bypasses kcap
  F3  the ATA DMA driver staged through GLOBAL pointers with zero
      locking ("single-threaded kernel", 2.0.0 era): concurrent disk
      I/O (the agent's appends + any other user) corrupted transfers
      -> buffers now passed through the call chain + per-channel
      irqsave spinlocks (transfers + flush)
  F4  the v2.0.0 sti;hlt sleep park freezes when the parked task is
      preempted mid-halt (the resume lands on the hlt with IF=0 -
      the exact failure the v2.0.0 launcher fix documented): the sleep
      now BLOCKS on the scheduler with a one-shot timer callback as
      the waker (the pipe-read primitive; no halt, no CPU burn).  The
      remaining hlt parks (sys_read keyboard wait, wp8cd fg wait,
      sdhci timed wait, OTA settle) are reverted to their proven
      v2.0.0 form - the freeze was never observed on those paths;
      declared as a known hazard class with the sleep as the fixed
      instance
  F5  QEMU's writeback disk cache: a SIGKILLed QEMU loses the agent's
      ring appends -> the battery now shuts the guest down cleanly
      before verifying the disk state

SEMANTIC CHANGE (declared in WP-02 already): the shell battery's wdt
case is re-pointed at the closed loop (armed -> SIGNAL_OK + DISARM ->
alive past the 60 s window; shell-wdt-g4.log).  The degrade flow for
a genuinely-hung OCOS (reset -> crash notice -> full recovery shell)
stays proven by the frozen WP-02 evidence.

ONE ocire/ TOUCH (declared): ocire.mk line 105, the agent link script
path l1apps/user.ld -> src/L1/user.ld - a mechanical consequence of
the WP-03 G2 tree move (the same class as every other path update in
the main Makefile); zero OCIRE behavior change, the OCIRE kernel
binary is byte-identical (60224 bytes, build 324).

BATTERY RESULTS (this directory):
  supervised-bios ........ PASS (facts+signal+protection+agent+
                               kill-refused+disk_rw-refused+isolation+
                               on-disk verify: SIGNAL_OK boots 1,
                               ring holds the agent appends)
  supervised-wdt ......... PASS (armed -> DISARMED -> alive t>75s +
                               on-disk ring shows live agent cycles)
  unsupervised-open ...... PASS (embed dormant, whole-disk surface
                               open, partition isolation holds)
  WP-01 dual battery ..... 11/11 PASS
  WP-02 shell battery .... 4/4 PASS (wdt case re-pointed, declared)
  host tests ............. 17 passed / 0 failed / 2 fixture-skip
  normal boot ............ BIOS + UEFI green (normal_uefi_boot.log)
  clean build ............ 0 errors 0 warnings (build_clean.log)
