<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL: troubleshooting and status codes

What OCBL and the recovery chain report, and what each report means.

## The reserved-area record (what OCBL writes)

| Field | Meaning |
|---|---|
| `magic` / `ver` / `size` | `"OCB1"`, version 1, 28-byte layout |
| `kind` | the record kind (below) |
| `stall` | the stage where the boot hung |
| `seq` | the state area's boot-attempt sequence at judgment time |
| `ts_unix` | judgment time, unix seconds (CMOS) |
| `silence_sec` | the measured no-progress seconds |
| `routed` | 1 = OCBL already auto-booted OCIRE |
| `boot_fails` | consecutive judged failures |
| `checksum` | ones-complement sum of the preceding payload words - the check that separates a real record from reset garbage |

### Record kinds

| Kind | Meaning |
|---|---|
| NONE | no record (a normal boot) |
| BOOT_FAILED | a watchdog-caught, judged boot failure |
| USER_REQUEST | the user chose the OCIRE entry in the menu |
| WAKE_FAILED | SLEEP_ARMED survived a reboot (wake vector crash or death mid-sleep) |

## The takeover menus (in OCIRE, after routing)

Two blocking menus share one key loop.  **Every other key is ignored
and there is no timeout** - the machine stays in recovery until a
human decides.

### Boot-failed takeover (two consecutive judged failures)

```
[R] Rollback to the previous OCOS version
[C] Continue and try booting OCOS again
```

### Wake-failed takeover (SLEEP_ARMED survived a reboot)

```
[W] Wake from sleep failed.   (diagnostics)
[R] Rollback to the previous OCOS version
[C] Try to boot OCOS normally
```

- `[R]` flips the OCOS A/B slot flags (the `next_B` / `ok_B` /
  `bootfail_B` protocol - the same semantics the GRUB config and
  OCBL's scanner implement) and reboots.
- `[C]` clears the failure record and reboots into OCOS.
- `[W]` shows the wake-failure diagnostics.

## Reading a boot failure

OCIRE prints the record's fields as repair advice: the stall stage
(0 BOOT / 1 KMAIN / 2 SCHED / 3 DRIVERS / 4 SERVICES / 5 L1_LAUNCHED
/ 6 L1_READY / 255 RUNTIME), the measured silence, the boot-failure
count and the boot sequence.  A stall in the low stages with a large
`silence_sec` points at hardware or early kernel trouble; a stall at
255 (RUNTIME) means the system came up and hung later.

## The /HANGAT diagnostic hook

When a one-byte file `/HANGAT` containing `1`..`7` exists on the
payload volume, OCBL appends `oc.hang=N` to the kernel command line
and the kernel deliberately halts right after recording stage N -
with the phase watchdog armed, so the real watchdog -> reset ->
diagnosis chain fires exactly as it would in production.  `7` halts
after L1_READY with the runtime heartbeat running (the runtime-hang
scenario).  The hook exists so **shipped images** can be
failure-tested without test-built kernels; with no file present it
is a complete no-op.

## When the monitor disables itself

If the fixed monitor pages are not free for OCBL this boot (BIOS: no
E820 usable region covers them; UEFI: the AllocatePages reserve
failed), the whole monitor **disables for that boot** - no
low-memory writes at all - and the kernel learns it from the
`oc.stmon=off` command-line flag.

## The declared UEFI watchdog limitation

With the i6300ESB PCI watchdog device attached, the UEFI (OVMF) OCBL
payload path dies after the slot scan; a controlled rebuild of the
unmodified pre-WP07 loader fails byte-identically, so the defect
predates WP-07.  Without the device the entire UEFI monitor path is
verified live.  The **BIOS flavor carries the complete verified
watchdog mechanism** and must pass fully (it does).  See
[KNOWN_ISSUES](../../root/KNOWN_ISSUES.md) K-02.

## Consistency with the OCIRE recovery flow

The recovery chain the menus guard is the WP-07 closed loop: the
strictly **manual** `update` command (no network, no timers) reads
`\EFI\OpenCube\ocire-update.bin` and its `.sig` from the real GPT
ESP, verifies the signature with the built-in **Ed25519** public key,
flashes the **inactive** slot of the two-partition recovery disk,
hashes the read-back, and arms it via the A/B flags sector at LBA
1024.  A bad image never replaces the running slot - the takeover
menus exist for the case where the *running system* fails, not the
update path.

See also: [dual-firmware boot and A/B slot decisions](ocbl-ab.md)
for the flags-sector and state-area semantics.
