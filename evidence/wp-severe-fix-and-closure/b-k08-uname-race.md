# B-k08: the ush banner race - root-caused to the pid-registration
# window, fixed, 20/20

Work package: WP-severe-fix-and-closure, item K-08.

## The symptom (KNOWN_ISSUES K-08 as shipped)

The first-boot banner sometimes showed "(run 'uname -r' for the
version)" instead of the fetched release string; on this tree the
same family of failure reproduced FAR harder: ~50% of boots showed
NO BANNER AND NO PROMPT AT ALL (the battery verdict "no-prompt"),
with the screen fully black and the serial transcript silent after
"[ocst] L1 isolation verified".

## The hunt (three instruments, all transcripts archived)

1. The 20-boot battery (k08_uname_test.py): 5/9 boots "no-prompt"
   on the intermediate tree (uname op already de-kcapped).

2. The QEMU-monitor hang hunter (k08_hang_hunter.py): at the
   "hang", RIP=0x4037b3 CPL=3 IF=1 HLT=0 - the CPU was alive in
   ring 3, inside ush's main loop, at the SYS_GETCH int 0x80.
   A live key injection then answered

       x\r\nush: command not found: x\r\nush>

   on the serial line: the system was FULLY alive - keyboard,
   line editor, command dispatch, echo, everything. The "hang" was
   the INVISIBLE STARTUP OUTPUT, not a dead machine.

3. The -d int interrupt trace (k08_hang_hunter_int.py, 2.7 GB
   transcript tail): the boot window's exact syscall sequence

       2160: v=80 RAX=0x6e (SYS_L1SVC) pc=0x403618   <- uname fetch
       2162: v=28 (timer IRQ)                          <- preemption
       2164-2167: v=80 RAX=0x48 (SYS_WRITE2) x4        <- banner writes
       2168: v=80 RAX=0x48 (SYS_WRITE2)                <- prompt write
       2169: v=80 RAX=0x4a (SYS_GETCH) -> EAX=-1       <- busy key poll
       2170..2M+: SYS_GETCH storm (ush's spin-wait design)

   The banner writes EXECUTED - and vanished anyway.

## The root cause (two stacked defects)

The banner bytes were swallowed by the BOOT SPLASH hook still
being installed: screen_console's hook was still splash_hook
(return 1 = suppress framebuffer AND serial) while ush printed
its whole banner + first prompt. The splash is released by
boot_splash_finish(), which runs in the L1_READY one-shot - and
the one-shot never fired:

    core_syscall_dispatch():
        if (boot_state_l1_pending()) {
            bs_p = user_process_current();
            if (bs_p->pid == g_l1_entry_pid)   /* FAILED */
                boot_state_l1_syscall();      /* never ran */

g_l1_entry_pid was still 0: l1_launcher_boot_default() registered
it only AFTER user_process_create() returned AND after
boot_state_advance(L1_LAUNCHED) printed its lines - while
user_process_create() had already queued the launcher kthread
READY. A timer preemption (RTC 1024 Hz + PIT) inside that window
ran ush all the way to its FIRST syscall before the pid was ever
registered; the compare failed, the splash stayed up, and the
banner + prompt were captured into the splash's line buffer
(klog) instead of the screen/serial. ush then sat in its getch
spin-wait (by design), the battery saw no prompt, and the boot
was verdicted dead. (~50% here because the registration window
sits right in the RTC-dense post-driver phase.)

The K-08 banner-fallback symptom in the shipped release is the
SAME window in a milder outfit: with the uname fetch still riding
the kcap console capture, the race landed inside
core_kcap_run_fn's capture hook juggling instead, and the fetch
returned empty.

## The fix (three layers)

1. THE ROOT: l1_launcher_boot_default() now performs
   user_process_create() + the g_l1_entry_pid registration inside
   ONE interrupts-off window (pushfq/popq/cli ... sti). The timer
   IRQ is the only preemption source on this single-CPU design
   and user_process_create() never yields - the window is closed
   BY CONSTRUCTION. (core_boot_l1launch.c)

2. SECOND LINE OF DEFENSE: while g_l1_entry_pid is still 0, the
   first syscall from ANY pid triggers the one-shot - during
   boot the default entry is the only ring-3 process that exists.
   (core_usermode.c, core_syscall_dispatch)

3. THE SYMPTOM LAYER: the uname op (L1SVC_DISP_UNAME) is now a
   pure buffer fill in svc_display_l1.c (like LANG_GET) - the
   banner fetch no longer rides the kcap console capture at all,
   so the capture-hook race window cannot lose the version
   string. disp_cmd_uname (the console-printing body) is removed.
   The wrapper apps print `out` themselves, so the interactive
   `uname` / `uname -r` / `uname -a` commands are byte-identical.

   Plus the failed-start path now tears the splash down
   (boot_splash_abort) - a process that was never created has no
   first syscall to release the console, and the failure
   diagnostics must not be swallowed either.

## The acceptance (B1-B4, 20 consecutive DEFAULT-splash boots)

    K-08 battery: 20/20 PASS - no banner fallback, no uname miss,
    no panic

  B1 the banner carries the REAL release string (20/20)
  B2 `uname -r` answers the exact release string (20/20)
  B3 `uname -a` answers the full line (20/20)
  B4 no kernel panic anywhere in the transcript (20/20)

Transcripts: /tmp/k08b_boot_{0..19}.log (the battery run); the
hunt evidence (monitor dump, screen dump, LIVE-TEST, the int
trace tail) under b-k08-*.log alongside this file.

## What this incident says about the vmx1 live-catch

The B-vmx1 "kernel stack bounds violated (rsp=boot-stack)"
panic-in-the-first-syscall observation was the same registration
window in its most violent outfit (the instrumented build's
timing); the pure release ISO never showed the panic - it showed
THIS instead. The window is now closed for all of them.
