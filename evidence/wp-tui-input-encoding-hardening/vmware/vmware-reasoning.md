WP-tui-input-encoding-hardening 4.5: the VMware triple-fault reasoning chain
===============================================================================
Reporter: OCBL on VMware Workstation 17.0.0 (build-20800274) starts and
CPU-shutdown-reboots in <0.1 s per guest attempt.  Delivered materials
(kept out of the repo, referenced by digest):

  vmware.log   355398 B  sha256 3bc6b4952df1a35504f089e7029b6996c211f816977da6261185924427c95fbc
  vmware-0.log 200701 B  sha256 91fc95c80860f22b3a7c4070ef01213a2341a656b5eaa5d4c86fe2876f8bd870
  vmware-1.log 260115 B  sha256 e12452eb91a204053ccd014f18ecc612b92fcfea328d3d6a9b3b95337e2904d2
  vmware-2.log 319929 B  sha256 71e891ed915732fa417bcfffab9f16973f1691d546d9293945ade91f3c367722
  serial.log     5975 B  sha256 8b27739d381d3c55983d25bc9510cb26ce825a1b347fac1f906490e24ef93b2d

--- 1. vmware.log verbatim evidence (line numbers from vmware.log) ---

Line 1:   2026-11-14T05:02:47.396Z In(05) vmx Log for VMware Workstation
          pid=35324 version=17.0.0 build=build-20800274 option=Release
Line 3:   2026-11-14T05:02:47.396Z In(05) vmx Host codepage=windows-936
          encoding=GBK
Line 328: 2026-11-14T05:02:47.854Z In(05) vmx DICT memsize = "512"
Line 294: 2026-11-14T05:02:47.853Z In(05) vmx DICT cpuid.coresPerSocket = "1"

The reset loop (15 "Triple fault." events total; every one followed by
"CPU reset: hard" and "The guest has requested that the virtual
machine be hard reset"):

Line 1297: 2026-11-14T05:02:50.099Z In(05) vcpu-0 Triple fault.
Line 1298: 2026-11-14T05:02:50.099Z In(05) vcpu-0 MsgHint: msg.monitorEvent.tripleFault
Line 1299: 2026-11-14T05:02:50.099Z In(05)+ vcpu-0 A fault has occurred
            causing a virtual CPU to enter the shutdown state. If this
            fault had occurred outside of a virtual machine, it would
            have caused the physical machine to restart. [...]
Line 1300: 2026-11-14T05:02:50.104Z In(05) vcpu-0 CPU reset: hard (mode Emulation)
Line 1336: 2026-11-14T05:02:50.123Z In(05) vcpu-0 Chipset: The guest has
            requested that the virtual machine be hard reset.

Cadence: 05:02:50.099 -> 51.394 -> 52.632 -> 53.872 -> ... -> 05:03:06.692
(15 events over ~16.6 s; ~1.24 s per cycle = firmware POST + the OCBL
attempt; the guest-side lifetime is well under a second, matching the
"<0.1 s" report).

Pre-crash context (the guest was mid-POST handoff, no register dump is
emitted by VMware for guest triple faults):
Line 1295: 2026-11-14T05:02:49.499Z In(05) svga SWBScreen: Screen 0
            Resized: xywh(0, 0, 720, 400) flags=0x3
        (720x400 = 80x25 VGA text - the firmware's mode set right
        before the El Torito handoff; the first Triple fault follows
        600 ms later, i.e. inside the boot loader's run.)

--- 2. serial.log verbatim evidence (the guest side) ---

Six OCBL attempts are visible (line numbers from serial.log):

Attempt 1 (lines 1-24):  banner -> B1 chain -> B2A probe (trampoline,
  E820 x11, hd 0x80 read, drive scan) -> "[state] fresh boot - state
  area initialized" -> "[state] boot attempt seq 1" -> "[scan] entries:
  0" -> RESET (no menu ever drawn).
Attempt 2 (lines 25-49): reaches the menu render, line 49:
  "[2;1HBooting Shutdown in 10s" - the ONLY menu entry is Shutdown
  (the scan found ZERO boot entries) - reset mid-countdown.
Attempt 3 (lines 50-73): same as 2, reset mid-countdown.
Attempt 4 (lines 74-93): dies EARLIER - right after "drive 0x80:
  16777216 sectors x 512 B", before ANY [state] line.
Attempt 5 (lines 94-113): dies MID-PRINTF - line 113 shows
  "[b2a] scanning BIOS drives (OCBL stage1 ok" - the next boot's
  stage1 banner printed INTO the middle of this line; the crash cut
  the line between "(" and "boot=0x9F)".
Attempt 6 (lines 114-136): full path again to the menu, reset
  mid-countdown.

Machine facts read off the guest output:
  - "flavor: el-torito  boot drive: 0x9F"   (firmware-chosen CD number)
  - E820 entry [0]: base=0x0 len=0x9F800 usable; [1]: 0x9F800..0xA0000
    reserved (VMware's low-memory layout; SeaBIOS/QEMU uses 0x9FC00)
  - "[scan] entries: 0" on every attempt that got there
  - "[state] fresh boot ... seq 1" on EVERY attempt (the state pages
    never survive the reset cycle)
  - "Booting Shutdown in 10s" (the fallback entry - H2's smoking gun)

--- 3. crash phase codes ---

The crash points distribute over the OCBL phases (all pre-kernel; the
kernel never starts, so the state stage stays OCST_STAGE_RESET):
  - B2A BIOS probe (E820/disk geometry/scan): attempts 4 and 5
  - state-monitor init + FS scan: attempt 1
  - B3 menu countdown (int16/int10 polling): attempts 2, 3, 6
The VARIANCE of the cut points (including a mid-printf cut with no
INT call in sight) rules out a deterministic fault at one instruction
and indicates an asynchronous event landing at random points - while
the clustering after state-init points at a second, stateful
mechanism.

--- 4. candidate hypotheses, each confirmed or excluded by evidence ---

H1 (CONFIRMED as a defect; part of the causal chain): the OCBL 64-bit
  core NEVER loaded an IDT - the IDTR inherited the firmware's
  real-mode IVT state ({0, 0x3FF}), which in long mode misdecodes any
  exception/interrupt vectoring (gate bytes are IVT far pointers:
  selector 0xF000 -> beyond the core GDT -> #GP during vectoring ->
  #DF -> triple fault).  Evidence: no lidt anywhere in the pre-fix
  tree (code inspection); the pre-fix QEMU register dump shows
  IDT=00000000/000003ff while CS/EIP sit in the OCBL context; the
  mid-printf cut (attempt 5) has no INT boundary and only an
  asynchronous event explains it.  Fixed by ocbl_idt.c/.S/.h: our own
  256-gate table, exceptions report on serial and halt, IRQs/NMIs
  iretq; the trampoline now swaps the vectoring discipline at every
  mode boundary (ENTER32 lidt's the firmware IVT for the real-mode
  window, BACK32 lidt's our table back on the way up - facts
  published by tramp_init at the fixed scratch pair 0x404F4/0x404F6).
  Known accepted residual (GRUB-class): the microsecond 32-bit compat
  windows and the real-mode IVT window still escalate on an NMI that
  lands inside them (measured: a 300-NMI/15 s storm kills both the
  fixed and pre-fix builds through those windows - and even kills
  OVMF's own firmware on the UEFI side; realistic single-event
  exposure is covered by the fix, see the differential below).

H2 (CONFIRMED directly by serial.log): the El Torito boot drive is
  FIRMWARE-CHOSEN - the reporter's VMware/Phoenix passes 0x9F, which
  is in neither the 0xE0+ (SeaBIOS CD) nor the plain hard-disk range
  the scan trusted, so "[scan] entries: 0" and the menu fell back to
  the Shutdown entry - even without the crash the OS would never
  boot.  Fixed by gating on the El Torito flavor (0 = el-torito), the
  boot drive is always included (deduped, geometry-checked) and
  classified as a CD whatever number the firmware chose.

H3 (CONFIRMED as a defect; consistent with the stateful part of the
  chain): the state monitor's EBDA guard read I/O PORTS 0x40E/0x40F
  instead of the BDA MEMORY at linear 0x40E, so it always saw
  0xFFFF ("no EBDA") and NEVER disabled itself.  The state pages sit
  at 0x98000/0x9B000; VMware's E820 calls low memory usable to
  0x9F800 while (per the WP06f audit of VMware layouts) the real EBDA
  can reach below 0x9F000 - exactly the "state writes land in
  BIOS-private memory, the next INT call triple-faults" class the
  WP06f comment documents, and it also explains "[state] fresh boot"
  every cycle (POST reinitializes the EBDA the pages landed in).
  Fixed by reading the BDA word in memory (ocbl_peek16), with the
  same window-check contract as before.
  (Harness note: an RSP-injected BDA poke at the first trampoline
  stop persisted in RAM but did not change the running decision - a
  QEMU 10.0.13 TCG translation-cache artifact of the gdbstub 'M'
  write path, not reproducible against the product: the forced-build
  proof below exercises the real code path end-to-end.)

H4 (CONFIRMED as a defect of the same low-memory class, not the
  reporter's primary trigger): the page-table relocation for low-EBDA
  machines moved the PML4 to 0x40000 - ON TOP of the real-mode
  trampoline (tramp_init's copy then stomped the tables) - and the
  trampoline's return path hardcoded CR3=0x8F000, so any machine that
  actually took the branch died on the first BIOS call.  Fixed by
  relocating to 0x41000 (clear of the trampoline footprint, below the
  mb2 info at 0x50000) and making ocbl_pt_base the single source of
  truth that tramp_init publishes to the fixed scratch word 0x404F0
  and BACK32 loads CR3 from.

NMI arming (fixed alongside): every CMOS register select wrote the
  bare register number to port 0x70, CLEARING the NMI mask bit on
  every RTC read - arming NMIs for the whole OCBL stay.  The standard
  discipline (keep bit 7 set while selecting) is restored.

H5 (EXCLUDED - host terminal decoding, not a guest defect): the
  reported "COM1 garbled output" - settled in 4.4 by the raw-byte
  captures: the kernel side is uniformly UTF-8, OCBL emits pure
  ASCII+ANSI, and the reporter's host runs windows-936/GBK (vmware.log
  line 3).  No code change; the built-in serial_encoding=gbk tee
  serves GBK-terminal users.

--- 5. reproduction / falsification WITHOUT VMware ---

(a) NMI-at-64-bit-phase differential (QEMU + gdbstub, deterministic):
    a hardware breakpoint stops the guest at ocbl_plat_getkey() entry
    - pure 64-bit OCBL C code, spinning for the whole menu countdown -
    an NMI is latched via the HMP monitor, then execution continues:
      pre-fix  (rsp_old.ser):  post-NMI stop = W00 - the VM exited:
                   the NMI vectored through the inherited IVT state
                   and triple-faulted (QEMU -no-reboot turns the guest
                   reset request into exit).
      post-fix (rsp_new2.ser): post-NMI stop = T05 - the SAME
                   breakpoint hits again (the NMI was absorbed by the
                   iretq stub) and the boot completes to ush>.
(b) Exception-stub live proof (cc2.ser): a 0xCC byte written at the
    getkey entry fires "#BP" through the new IDT and the guest prints
    "[ocbl] CPU exception 3 (#BP breakpoint) at rip=0x...FFF1 - the
    IDT caught it; halting (no reset loop)" - the exact
    diagnose-and-stop behavior the fix promises (vs the silent reset
    loop of the report).
(c) H3 forced build (h3_forced.ser): with ebda_base() forced to
    0x98000 (the overlapping case) the fixed build prints
    "[state] EBDA at 0x00098000 overlaps the monitor window - state
    monitoring DISABLED this boot", propagates "monitor window
    unavailable" through boot_check, the kernel honors
    "oc.stmon=off - no low-memory writes", and the boot still
    completes to ush>.
(d) H4 forced-build matrix (deterministic, three throwaway test
    builds, patches never committed):
      fixed build, PML4 forced to 0x41000 (h4a.ser): FULL boot to
        ush> - the relocation target, the ocbl_pt_base publish, the
        0x404F0 scratch handoff and BACK32's CR3 load all work.
      fixed build, PML4 forced to 0x40000 (h4b.ser): dies right
        after "[b2a] trampoline installed" - the trampoline copy
        stomps the tables at 0x40000; the old target was intrinsically
        fatal.
      pre-fix build, PML4 forced to 0x40000 (h4c.ser): same death -
        the original bug reproduced without VMware.
(e) Normal-boot regressions: the fixed tree boots BIOS and UEFI
    flavors to ush> (final_smoke.ser / the 4.5 smoke logs), with the
    64-bit phase register dump showing IDT=0000000000014610/00000fff
    (our table) and the state monitor active as before.

--- 6. honest limits ---

- The reporter's exact primary trigger cannot be pinned to a single
  mechanism from the logs alone: H1 explains the mid-printf cut, H3
  explains the post-state-init clustering and the "fresh boot every
  cycle", and both defects are real by inspection.  Both are fixed by
  standard-discipline changes (no VMware special-casing anywhere).
- The delivered logs contain no guest register dump (VMware does not
  emit one for guest triple faults), so the cascade is reconstructed
  from the CPU-visible behavior + code inspection + the injected
  reproductions above.
- The microsecond mode-boundary windows remain NMI-fragile (GRUB-class
  accepted risk, documented in ocbl_idt.S).
