# B-vmx1: the VMware boot-crawl root cause and the measured fix

Work package: WP-severe-fix-and-closure, function point 4.1.

## The claim this replaces

WP-compat-and-security-hardening B3 claimed the VMware crawl was the
ATA DMA completion poll ("50,000,000 inb iterations x 8 retries") and
fixed it with TSC wall-clock deadlines.  User-side retest: the load
was STILL slow.  B3 bounded the wait TIME but changed neither the
poll RATE nor the drop-verdict gate - the two things that actually
generate load under VMware.

## The real mechanism (two independent drivers)

Under VMware every port I/O and every unaccelerated MMIO access is a
VM exit (~1-3 us of host time each; QEMU TCG executes the same inb in
~ tens of ns).  The boot path contained:

1. UN-PACED POLL LOOPS.  The ATA BMDMA completion poll, wait_not_bsy,
   the PIO status waits, the AC97 codec-access semaphore, the HDA
   stream reset, the e1000 EEPROM read and the OCBL menu loop all
   issued one port read PER LOOP ITERATION with no idle gap.  At QEMU
   speed that is harmless; at VMware exit cost it is an un-throttled
   exit storm (millions of exits per second of wall clock) that also
   saturates one host core.

2. AN ITERATION-COUNTED DROP DETECTOR.  The QEMU-command-drop
   heuristic in the BMDMA poll gated its "command was dropped" verdict
   on 4096 POLL ITERATIONS.  On QEMU 4096 inb is a few MICROseconds -
   the gate never fired on a healthy transfer.  On VMware 4096 inb is
   10+ ms of exit time - LONGER than a healthy virtual disk needs to
   start a transfer - so every slow-to-start transfer drew a FALSE
   "dropped" verdict, aborted, stalled (20000 x io_wait = 20000 MORE
   port writes) and retried, up to 8 times, inside a wall-clock
   deadline of 1000 ms per attempt.  One unlucky transfer: 8 s of
   deadline + 160k stall port ops.  The boot walks dozens of
   transfers: minutes.  THIS is the "extremely slow" the user measured
   - and why B3's deadline change did not help: the deadline only
   capped each retry, the false verdicts and the exit storm kept
   coming.

## The fix

- driver_tsc_pause_us() (drivers/time): a microsecond pause with ZERO
  port I/O - a pure rdtsc/pause spin.  The pacing primitive.
- Every hot poll is now PACED: one port/MMIO read per 25-50 us of wall
  clock (ATA DMA poll 25 us, status polls 50 us, AC97/HDA/e1000 50 us).
  Completion latency is unchanged (device transfer time dominates);
  the port-I/O (exit) rate drops ~1000x.
- The drop verdict is now TIME-gated: 10 ms of wall-clock idle
  (BMIS==0 AND the task file not BSY/DRQ) before "dropped" is
  declared.  A QEMU-dropped command idles the full 1000 ms deadline
  (the verdict still fires); a healthy VMware transfer starts within
  a couple of ms (the verdict never fires).
- The 20000 x io_wait stall loops are replaced by 2 ms TSC stalls
  (zero port I/O).
- The OCBL menu loop (both BIOS and UEFI flavors) idles on a pure-CPU
  pause between polls: the int 16h + 16550 LSR + 8 CMOS port ops per
  iteration drop from ~130k iterations to ~117 over the 10 s
  countdown.  Key latency stays human-invisible.
- The early-console COM1 THRE wait is bounded (a broken UART used to
  spin on inb forever).

## The measured evidence (QEMU TCG, 16 MiB FAT32 data disk as the
first IDE drive - the /etc mount path exercises partscan + the OCIRE
anchor scan + the FAT32 mount + the config read: 45 DMA transfers)

Identical counters in both builds; the ONLY difference is the loop
behavior.  The [perf] line is printed by the kernel at L1_READY on
the serial transcript of EVERY boot (this capability ships - it is
the standing verification surface).

BEFORE (release d3560c3f + counters-only instrumentation; the
drop-case boot, see b-vmx1-before-boot.log):

    [perf] ata_dma: port_io=17608313 polls=17588313 xfers=45 retries=1 drops=0 wait_ms=1000
    [menu] poll iterations: 130589
    [00:00:01.614] boot complete (kernel-side)

A no-drop boot (see b-vmx1-before-boot-nodrop.log) shows port_io=88:
the storm only appears when the emulated disk takes longer than a few
poll iterations to complete - which is exactly the VMware regime.

AFTER (the fix; three consecutive boots, b-vmx1-after-boot*.log):

    [perf] ata_dma: port_io=39452 polls=39452 xfers=45 retries=1 drops=0 wait_ms=1000
    [perf] ata_dma: port_io=39417 polls=39417 xfers=45 retries=1 drops=0 wait_ms=1000
    [perf] ata_dma: port_io=39368 polls=39368 xfers=45 retries=1 drops=0 wait_ms=1000
    [menu] poll iterations: 117  (all runs)
    [00:00:01.5-1.7] boot complete

Same 45 transfers, same emulated-disk behavior (retries=1, the same
1000 ms deadline burn on the same dropped first command), but:

    port-I/O in the wait paths : 17,608,313 -> 39,452   (446x fewer)
    menu poll iterations       :  130,589   ->    117   (1116x fewer)

## The VMware projection (declared as a projection - the sandbox
cannot run VMware; the counters are the exit count)

On VMware every counted port op is one VM exit at 1-3 us of host
time:

    BEFORE: 17.6M exits x 1-3 us  = 18-53 SECONDS of exit processing
            during boot (the drop-detector false-fire path adds up to
            8 x 1000 ms of deadline burn + 160k stall exits per
            unlucky transfer - the minutes-long crawl)
    AFTER:   ~40k exits x 1-3 us   = 0.04-0.12 seconds

User-side verification (one boot of the release ISO in VMware):
the serial transcript now carries [menu] poll iterations, the
[00:00:xx] milestone grid, and the [perf] ata_dma counters - read
them directly; the numbers above are what a healthy paced boot
shows.  A serial port (or the VM's serial-over-file) is required;
the same lines also appear on the VGA console log in the classic
(oc.splash=0) boot.

## Live-caught along the way (honest disclosure)

The counters-only BEFORE build (release code + a ~10 ms serial print
inside ush's first syscall) panicked in 2 of 4 boots: "kernel stack
bounds violated on switch (task=ush, rsp=<boot-stack address>)" -
ush's saved task->rsp was a BOOT-STACK address, i.e. at some
switch-away ush's kernel context was executing on the boot stack
rather than its own 40 KiB window.  The PURE release ISO boots the
same disk 10/10 clean; the fixed tree (which carries the same print)
boots 3/3 clean here and 20/20 across the K-08 battery later in this
package.  The race window lives in the first-syscall region (the
same region K-08's banner race lives in); the K-08 fix removes the
kcap-capture dependency from that path entirely.  The panic was NOT
reproduced on the release build and is recorded here as a
timing-window observation with the full transcript archived
(b-vmx1-before-boot.log, tail).
