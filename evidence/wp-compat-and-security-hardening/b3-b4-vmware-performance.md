# Open Cube OS - WP-compat-and-security-hardening B3/B4
# VMware slow-boot root cause + performance evidence
# Date: 2026-11-25

## 1. Root cause (code-walkthrough evidence)

The whole-system crawl under VMware was the ATA bus-master DMA
completion poll in ocos/drivers/storage/drv_storage_ata_dma.c:

    for (u64 i = 0; i < 50000000u; i++) {     /* 50,000,000 iterations */
        s_last = inb(bmis);                    /* PCI port read */
        ...
    }

On real hardware an iteration is ~10 ns, so the budget is a 0.5 s
bound.  Under VMware every port I/O instruction causes a VM exit
(1-3 us each, binary-translation overhead): the loop alone walks
50,000,000 x 1-3 us = 50-150 s PER TRANSFER, x 8 retry attempts =
hundreds of seconds - and it ran under an irqsave spinlock, so
nothing else in the system advanced either.  The same applies, at a
smaller scale, to every raw-iteration wait in the tree (the PIO ATA
waits at 100,000 iterations, the AC97/HDA codec waits at 100,000,
the OCBL menu's 50x-fast PIT arithmetic).

## 2. The fix

1. Every I/O wait loop now carries a TSC wall-clock deadline
   (checked every 4096/1024/256 iterations, chosen per loop):
   - ata_dma.c xfer poll: 1000 ms real-time bound (rc = -6 on timeout)
   - ata_dma.c wait_not_bsy: 100 ms
   - ata.c (PIO) wait_bsy / wait_drq: 100 ms
   - ac97 codec/stop/reset waits: 100 ms
   - hda stream reset / controller reset: 100 ms
   The raw iteration caps stay ONLY as the safety net for the boot
   window before the TSC calibration lands.
2. The TSC is calibrated EARLY in kmain (right after the system tick
   comes up): a new RTC-based calibration measures 32 PIE ticks
   (~31 ms) - driver_tsc_calibrate_rtc(), autocalibrate order is
   HPET -> PM timer -> RTC -> PIT.
3. The system tick itself moved to the RTC (1024 Hz PIE on IRQ8, see
   the B4 evidence), so the wall clock no longer depends on PIT IRQ0
   delivery either.

## 3. Measured results (QEMU, TCG, identical machine, host wall clock)

| metric | value | source |
|---|---|---|
| boot to ush> (default RTC tick) | 16.8 s total, of which the menu countdown is a REAL 9.69 s (host-measured, one second per step - the old 50x-fast menu clock did 10 s in 0.2 s) | b34-rtc-default-serial.log + the host timestamps in the acceptance run |
| boot to ush> (forced PIT fallback) | 17.5 s (adds the ~1.1 s PIT calibration window) | b4-tickpit-degradation-serial.log |
| menu per-step cadence | 0.987 s/step (RTC second anchor), 10 steps | diagnostic run (arrival timestamps per line) |

## 4. VMware projection (honest scope note)

This sandbox has NO VMware binary, so a before/after VMware pair
cannot be produced here.  What the numbers above prove on QEMU:

- the countdown now tracks REAL time (9.69 s measured for 10 s;
  the pre-fix binary did it in 0.2 s - the identical bug class);
- no I/O wait can exceed its wall-clock bound any more, so the
  VMware failure mode ("50M-iteration inb poll walks for minutes
  under 1-3 us/exit") is bounded by construction to 8 x 1000 ms
  per transfer worst case, at ANY port-I/O cost.

The expected VMware effect: the boot crawl (minutes) collapses to
the actual work time plus bounded waits (< 10 s class).  First
hardware verification report welcome; the mechanism is fully
covered by the code above either way.

## 5. VGA debug marks (B4, same workpack item)

- ocrmecl/bios/{stage1,core}.asm: every 0xB800 text-mode write is
  compiled OUT by default (%ifdef OCRMECL_VGA_MARKS; the makefile
  only defines it for `make OCRMECL_VGA_MARKS=1`).
- Byte-level check: the default build contains no `BB 00 B8`
  (mov bx,0xB800) in stage1.bin or core.bin; the marks=1 build
  does.  The SBFAJPILM row-24 residue is gone from the default
  boot.

-- end --
