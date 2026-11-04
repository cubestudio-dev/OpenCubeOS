# 3.0.0-WP07a-ocbl_state — Evidence Bundle

All artifacts below were produced live in the WP07a sandbox batch
(2026-11-04, QEMU 10.0.13, SeaBIOS + OVMF, gcc 14.2, build 377).
The scenario transcripts are the complete, unedited serial logs of
`tools/wp07a_state_e2e.py` runs (one fresh QEMU per scenario, the
real shipped OCBL/OCOS/OCIRE images, `-device i6300esb`, no
`-no-reboot`: the warm resets are real).

## Scenario matrix (13/13 PASS)

| log                     | scenario                                                        |
|-------------------------|-----------------------------------------------------------------|
| s1_normal.log           | normal boot: stages 1..6 advance, ush prompt, L1 isolation walk, 45 s without a reset (the heartbeat holds the relay) |
| s2_user_request.log     | menu -> OCIRE: USER_REQUEST record written, OCIRE reports "requested by the user", no failure judged |
| s3_hang_boot.log        | pre-kmain hang (hang0.elf payload): the OCBL-armed 30 s budget fires, BOOT_FAILED at stage BOOT, screen notice, 8042 pulse, auto-route, OCIRE report + advice (silence 30 s) |
| s4_hang1.log            | /HANGAT=1: halt after KM_AIN_ENTERED, 10 s phase budget (silence 10 s), full chain |
| s4_hang2.log            | /HANGAT=2: SCHED_READY, 10 s budget (silence 11 s)                |
| s4_hang3.log            | /HANGAT=3: DRIVERS_READY, 10 s budget (silence 10 s)             |
| s4_hang4.log            | /HANGAT=4: SERVICES_READY, 10 s budget (silence 10 s)            |
| s4_hang5.log            | /HANGAT=5: L1_LAUNCHED, 30 s budget (silence 30 s)               |
| s4_hang6.log            | /HANGAT=6: L1_READY, 15 s runtime budget (silence 15 s)          |
| s5_runtime.log          | /HANGAT=7: runtime hard hang after L1_READY + heartbeat running; 15 s budget, logread advice |
| s6_manual_reset.log     | a warm reset (QEMU monitor system_reset) while running is NOT judged a failure (the RTC-silence discriminator) |
| s7_permissions.log      | badapp (ring-3 touching supervisor space) killed by the page fault, ush survives; the state-page CR3 walk asserted live in s1 |
| s8_uefi_chain.log       | the UEFI-flavor monitor path under OVMF (no-device smoke; see the declaration below) |

## Regression gates

| log                 | result                                                  |
|---------------------|----------------------------------------------------------|
| host_tests.log      | host suite: 17 passed, 0 failed (2 fixture skips)        |
| ocire_battery.log   | OCIRE battery (the five-regression set x BIOS+UEFI): 14/14 PASS |
| reproducible.log    | two clean builds byte-identical (kernel ELF sha256 match) |
| artifact_hashes.txt | sha256 of every WP07a artifact                           |

## Honest declarations

1. **The OCBL FAT12 latent bug (live-caught, NOT fixed in WP07a).**
   Building a test volume whose cluster count lands in the FAT12
   range exposed a defect in OCBL's FAT12 multi-hop FILE-chain reads
   (directory chains work; file data chains hit a bogus EOC - the
   payload load fails). All WP07a test volumes are built FAT16
   (`mformat -c 1`, see tools/wp07a_state_e2e.py). The WP05 scan
   fleet never loaded real payloads from its FAT12 volume, which is
   why this never fired before. Fix belongs to the OCBL/OCIRE repair
   batch (WP07b candidate), NOT this package (the no-drive-by-fixes
   rule).

2. **The UEFI watchdog chain is blocked by a PRE-EXISTING defect.**
   With the i6300ESB PCI device attached, the OCBL EFI payload path
   dies after the scan under OVMF - and a controlled experiment (a
   git-worktree build of the UNMODIFIED WP06e OCBL) fails
   byte-identically (e1/e2 probes in the WP07a dossier): the defect
   predates WP07a. Without the device, the ENTIRE UEFI monitor code
   path is verified live (s8: honest no-watchdog arm declaration,
   stages 1..6, the isolation walk, full boot to ush). The BIOS
   flavor carries the complete verified mechanism (s1..s7).

3. **The QEMU/SeaBIOS reset wipes most low memory.** Pattern-probe
   evidence (three independent runs): after any system reset, all
   probed pages in [0x70000..0x9FC00) read back zero EXCEPT the two
   stable islands at 0x98000 and 0x9B000 (the wipe ends at the
   EBDA). The state/reserved areas live on those two proven-surviving
   islands; cold boots and power loss clear them (magic + checksum
   validated on every read - no record found = a normal boot).

4. **Instant-crash payloads (triple fault inside boot.S) reset the
   machine within seconds of handoff; the measured silence stays
   below the 8 s judgment threshold, so no BOOT_FAILED is recorded
   (the boot simply restarts).** The monitor's contract covers HANGS
   (the watchdog-caught class); crash-resets are a different failure
   mode, declared here.

5. **The old OCBL payload handoff armed a blind 60 s watchdog window
   for EVERY payload (including OCIRE/OCSE) with nothing in a normal
   OCOS boot ever feeding it** - a latent 60-second-reset bug
   (WP05-era). WP07a replaces it: monitored OCOS handoffs arm the
   boot budget and the kernel takes the relay over at stage 1;
   OCIRE/OCSE handoffs disarm it (they own their watchdog story).
