3.0.0-WP04 G9 - ring-0 self-test family migrated to L1 (diag ops 9-26)
=====================================================================

18 commands (v2.2.0 core/main.c bodies + msitest from
recovery/shell_cmds_chipset.c) moved verbatim into services/diag;
each is one L1 app folder (src/L1/<name>/main.c) calling
SYS_L1SVC(19, op).  Op numbers were frozen in l1svc_defs.h by G8.

  op  command      app folder            notes
  --  -----------  ---------------------  -----------------------------
   9  cr3test      src/L1/cr3test/        kernel-AS wrap
  10  exc          src/L1/exc/            shares g_exc_test_pass_count
                                           with kmain boot step 11 (the
                                           block moved to
                                           services/diag/diag_exc_test.c)
  11  pftest       src/L1/pftest/         kernel-AS wrap
  12  pmmrace      src/L1/pmmrace/        verbatim; runtime-bound by
                                           design (see below)
  13  vmtest       src/L1/vmtest/
  14  vmkernelpt   src/L1/vmkernelpt/     kernel-AS wrap
  15  vmmap        src/L1/vmmap/          kernel-AS wrap
  16  memtest      src/L1/memtest/
  17  heaptest     src/L1/heaptest/
  18  heapbounds   src/L1/heapbounds/
  19  irqabitest   src/L1/irqabitest/     nested int-0x80 probe verified
                                           safe (rax=-1 -> safe reject)
  20  timer        src/L1/timer/          oneshot cb bumps the SAME
                                           counter the stats op prints
                                           (g_disp_oneshot_fires)
  21  msitest      src/L1/msitest/        from recovery/shell_cmds_chipset.c
  22  spawn        src/L1/spawn/          async kthread prints to console
                                           (v2.2.0 behavior)
  23  multi        src/L1/multi/          idem
  24  nice         src/L1/nice/           arg-passing via 256B line
  25  synctest     src/L1/synctest/
  26  fstest       src/L1/fstest/

kernel-AS wrap: vmmap/pftest/cr3test/vmkernelpt semantics in v2.2.0
depended on current CR3 being the KERNEL address space (they walk/map
through it); the L1 syscall runs with the CALLER's AS, so these four
switch to the kernel AS for the body and restore the spawner's AS
before returning (recreating the exact v2.2.0 execution context).

Tests
-----
bios_selftest_battery.log : 20-command BIOS battery, all outputs
                            v2.2.0-identical (exc/timer/memtest/
                            heapbounds/vmmap/vmtest/cr3test/pftest/
                            heaptest/nice/irqabitest/spawn/multi/
                            synctest/msitest/vmkernelpt/fstest + sleep
                            gaps for the async commands).
uefi_smoke.log            : OVMF pflash boot, 5-command smoke ALL PASS
                            (exc/memtest/heapbounds/vmmap/syncstat).
post_fix_regression.log   : post-F6 spot regression (exc/vmmap/pftest/
                            heaptest/nice/sleep/echo).

pmmrace: runtime-bound stress test BY DESIGN (the body reprograms the
PIT to ~7.9 kHz so the IRQ-context allocator intersects the caller's
test-then-set window ~every 126us - the v2.2.0 comment says exactly
this).  20000 rounds at that IRQ cadence exceed any practical
single-window e2e budget on QEMU TCG; v2.2.0 never ran it in any e2e
suite either.  Verified instead: kernel stays alive for 500+ s inside
the race window, and the window itself is demonstrably live
(f6_race_window_concurrent_ok.log: ticks 243 -> 13704 while a
background pmmrace, sleep 8 and two stats calls run concurrently,
no exception, prompt responsive).

F6 - live kernel bug found and fixed (self-found self-fixed)
------------------------------------------------------------
Symptom  : `pmmrace &` + a concurrent syscall (stats) -> #DF Double
           Fault within seconds; panic "KERNEL STACK OVERFLOW
           (stack exhausted -> #DF)" (f6_crash_before_fix.log).
Probe    : temporary burned-stack scan in the #DF branch dumped the
           recursion cycle  arch_isr_common -> arch_irq_dispatch ->
           core_timer_irq_handler -> health_tick -> health_take ->
           [sti-leak] -> arch_isr_common -> ...  (~4.5 KiB per level,
           ~8 levels -> 36 KiB kernel stack exhausted).
Root cause: THREE inverted IF-restore patterns, inherited verbatim
           from v2.2.0 (same files, same lines):
             core_sync.c:19  sync_get_stats        if (!(flags&0x200)) sti
             mem_heap.c:428 mem_heap_get_stats     if (!(flags&0x200)) sti
             mem_vmm.c:424 mem_vmm_get_fault_stats if (!(flags&0x200)) sti
           In IRQ context (IF=0) they ENABLED interrupts inside the
           handler; during pmmrace's 7.9 kHz window every tick nested a
           full IRQ entry + health sample chain -> runaway recursion ->
           stack exhaustion.  In task context (IF=1) they left
           interrupts DISABLED after the call (latent interrupt loss).
           Never seen pre-WP-04 because nothing reprogrammed the PIT
           at 7.9 kHz in any e2e suite and normal 100 Hz gives the
           handler time to finish between ticks.
Fix      : the three restores corrected to `if (flags & 0x200) sti`
           (restore IF only when it was set) - the same pattern their
           sibling functions in the same files already use.
Verified : the exact crash repro now runs clean with the race window
           active (f6_race_window_concurrent_ok.log), zero exceptions,
           and a temporary post-callback IF-probe reported zero
           violations across the whole battery (probe removed after
           diagnosis; binary strings confirm it is gone).

Build: 0 errors 0 warnings (make kernel + make iso), BIOS + UEFI green.
