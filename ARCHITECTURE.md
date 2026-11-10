<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS Architecture

This document describes the tree as it exists in 3.0.0-WP06g3 - every
component named below is a real file you can open. Line counts and
per-directory detail live in `docs/data/stats.json` (regenerate:
`make api-docs`).

## The two rings

Open Cube OS is a two-ring system:

- **L0 - provides services.** A pure service kernel: the core
  mechanisms, the drivers, the root services and the interface
  surface. 3.0.0-WP03 deleted the L0 user plane entirely - the `oc>`
  shell, every shell command, the TUI framework and the input method
  engine are gone; L0 finishes boot at `L0 ready, waiting for L1`
  and hands the machine to the default ring-3 entry.
- **L1 - uses services.** Ring-3 user applications that reach L0
  capability through the extension surface (the `SYS_L1SVC` op
  table and the classic syscall ABI). 3.0.0-WP04 moved every command
  into L1: **275 programs under `src/L1/`** (242 app folders + 8 flat
  `.c` + 25 flat `.asm`) - one command = one app = one folder. The
  default boot entry is the ring-3 `ush` shell (`OC_L1_APP_DECL("ush",
  ..., dflt=1)` in `src/L0/boot/l1_launcher.c`); **285 commands = 274
  registry apps + 11 ush built-ins, zero in L0**.

```
              OCBL - the in-tree bootloader (hybrid BIOS + UEFI ISO)
              512-byte real-mode stage 1 (El Torito / MBR) or the EFI
              entry -> 16 -> 32 -> 64-bit long mode, dynamic disk scan,
              boot menu with a 10 s countdown (no GRUB in the chain)
        +-------------------------+-------------------------+
        |              src/L0/boot/boot.S                  |
        |  32-bit entry -> GDT -> 2 MiB identity map of    |
        |  the first 4 GiB -> PAE+LME+PGE -> long mode     |
        +---------------------------------------------------+
                               |  kmain (src/L0/boot/kmain.c)
        +-------------------------+-------------------------+
        |  PHASE 1  kernel core mechanisms                  |
        |    core/console (serial/fb platform text layer),  |
        |    IDT/PIC/exceptions, PIT, PMM/VMM/heap,         |
        |    scheduler/sync, userspace, registry,           |
        |    ACPI/PCIe/watchdog-hw/chipset, RTC/HPET        |
        |  PHASE 2  drivers + driver-free services          |
        |    VFS + ramfs (driver-free); block/USB/wireless/ |
        |    SD/BT/camera/misc driver families              |
        |  PHASE 3  driver-dependent services               |
        |    FAT/exFAT/ext4 + config + A/B (after block),   |
        |    wireless (after fs-mounted firmware),          |
        |    net (after NIC), snd (after the snd family)    |
        |  PHASE 4  interfaces ready                        |
        |    crash dump/crashlog/soft wdt, health monitor,  |
        |    update check, the OCIRE embed confirmation     |
        +---------------------------------------------------+
                               |
                     "L0 ready, waiting for L1"
                               |
                    l1 launcher -> ush (ring 3)
                               |
          exit / failure -> L0 halts honestly (reboot or the
          OCBL menu; the 2.x oc> recovery fallback is gone)
```

(The boot prints `boot chain N/4` markers for the middle phases;
`tools/check_deps.py` gates the layer directions below.)

## The L0 tree (3.0.0-WP03 layout)

```
src/L0/
  boot/     the composition root: boot.S, long mode entry, the
            multiboot2 header/stub, kmain.c, the L1 launcher and the
            boot self-tests. May depend on every layer.
  core/     kernel mechanisms: scheduling, sync, timers, usermode,
            the panic slots (arch_exceptions), the declaration
            registry, the captured-output primitive, mem/, lib/
            (string/log/klog-ring/config-kv), console/ (the mb2
            framebuffer platform text layer), arch/{x86_64,arm64,
            riscv} and generated/.  Depends on nothing above it.
  drivers/  hardware drivers, one folder per family (block, nic,
            snd, usb, input, pci, acpi, i2c, time, wireless,
            bluetooth, sd, camera, misc, scsi).  No services
            dependencies.
  services/ root services - the folder is the interface facet:
            fs block net audio font encoding display loader
            selfhost ocembed | sysapi ipc klog health crash
            (+ time power crypto camera usb bodies).  May depend on
            core/ and drivers/ (and, where a real dependency
            exists, on other services - fs before selfhost, for
            example).
src/L1/     the ring-3 world: 275 programs (242 app folders + 8 flat
            .c + 25 flat .asm) - ush (the only OC_L1_APP_DECL default
            entry), the TUI desktop family, the editor, the Chinese
            console + IME, and one command per app (built by
            `make l1apps`).
ocbl/       OCBL, the in-tree bootloader (BIOS stage 1 + EFI entry,
            the boot menu, the ocbl-install target) - a separate
            freestanding build (ocbl/ocbl.mk). NOT part of L0.
ocire/      the independent recovery kernel (a separate freestanding
            binary, own build in ocire/ocire.mk).  NOT part of L0.
```

**The dependency rule** (enforced by `tools/check_deps.py`, run in
the test matrix): `core/` must not depend on `drivers/` or
`services/`; `drivers/` must not depend on `services/`.  The inversions
this required in WP-03 G5: the platform console stack lives in
`core/console`; the klog ring is a core mechanism with the on-disk
sink in `services/klog` installed as an emit hook; crash capture
lives in `services/crash` and installs its handlers into the
core-owned panic slots; the config store keeps the key-value
mechanism in core with the fs backend installed from `services/fs`.

## Extension by adding a file

Nothing registers into global tables through runtime calls.  A
driver, a service or an L1 app declares its identity IN THE FILE
THAT IMPLEMENTS IT via the `OC_*_DECL` macros (`core_registry.h`);
each macro places a const descriptor into a dedicated linker section
(`.oc_drvs`, `.oc_svcs`, `.oc_apps`) and `core_registry_collect()`
walks the section boundaries at boot.  Adding a new driver or
service = adding one `.c` file in the right folder plus one
declaration line - no boot-file edit, no registration call site
(live-tested in the G3 evidence: one new file with one
`OC_DRIVER_DECL` was collected at runtime, then removed).

The register-class L1 interfaces of v1 (`driver_block_register`,
`driver_nic_register`, ...) died with the section mechanism; their
operation-class siblings (read/write/send/recv/...) remain the
surface (see INTERFACES.md for the full table and the v1->v2->v3
fate record).

## The interface surface

Ring 3 reaches L0 through two doors:

- the classic syscall ABI (`src/L0/services/sysapi/core_syscall.h`,
  54 syscalls), including `SYS_L1SVC(110)`: `(category, op, arg,
  out, cap)` routed by the registry to the owning service's
  `l1_dispatch`.
- the categories/ops are the frozen ABI table in
  `src/L0/services/sysapi/l1svc_defs.h`: **20 categories / 190
  numbered entries** (category number -> the owning services/
  folder; category 18 "ocire" is the declared exception - the OCIRE
  log-agent contract's ABI name, implemented by `services/ocembed/`).
  Ops are plain ASCII text renderers - the ring-3 side formats
  nothing.

## The boot-state monitor (3.0.0-WP07a)

OCBL cannot execute while the kernel runs, so the WP07a boot-state
monitor enforces the boot timeouts with a hardware watchdog relay
(the 6300ESB, the same PCI device OCIRE's supervisor loop uses):

```
OCBL arm(30s) -> handoff -> kernel stage N: write stage + arm(next
budget) -> ... -> L1_READY: 1 Hz tick pets the 15 s runtime budget
                |
                v (a hang stops the feeding)
6300ESB resets the machine (warm reset, DRAM kept)
                |
OCBL boots again: reads the state area -> silence >= 8 s and no
CLEAN marker -> JUDGED: writes the BOOT_FAILED record (stall point,
timestamp, measured silence) -> screen notice -> 8042 pulse
                |
OCBL boots again: pending unrouted record -> auto-boots OCIRE
                |
OCIRE: reads the record -> prints the stall point + repair advice
```

The contract lives in `include/ocbl_state.h` (shared byte-for-byte
by OCBL, OCOS and OCIRE):

- **State area** at 0x98000 - what the kernel is doing (stage,
  heartbeat, the RTC clock of the last progress).  The kernel only
  WRITES it (blind, never reads back); OCBL reads/writes; OCIRE
  reads.  L1 applications cannot reach it: the pages sit in the
  supervisor-only half of every user address space (verified live by
  a CR3 walk at L1_READY + the badapp canary class).
- **Reserved area** at 0x9B000 - the post-mortem boot record
  (BOOT_FAILED vs USER_REQUEST, the stall stage, the judgment
  timestamp, the measured silence, a ones-complement checksum).
  It survives warm resets (the two addresses are the empirically
  proven surviving islands of the QEMU/SeaBIOS reset wipe - see
  `evidence/3_0_0_WP07a/`); cold boots clear it and every reader
  validates the magic first.
- **Six state points**: KM_AIN_ENTERED (kmain entry), SCHED_READY
  (core mechanisms up), DRIVERS_READY (driver families probed),
  SERVICES_READY (root services up), L1_LAUNCHED (ush started),
  L1_READY (ush serving - its first syscall).
- **Phase budgets**: 30 s (boot start -> stage 1), 10 s per middle
  phase, 30 s (L1_LAUNCHED -> L1_READY), 15 s runtime heartbeat.
- **Judgment**: OCBL computes the silence (RTC now minus the last
  kernel progress); >= 8 s without the CLEAN marker = a judged
  failure (a healthy boot progresses every second, so a manual warm
  reset measures < ~4 s and is NOT a failure).
- **Clean reboots**: the kernel marks the state area CLEAN before
  its deliberate 8042 reboot, so a normal `reboot` never produces a
  failure record.
- **User requests**: picking the OCIRE entry in the OCBL menu writes
  a USER_REQUEST record - the exact distinction OCIRE reports
  ("requested by the user" vs a judged failure with repair advice).

The whole chain is driven live (real watchdog resets, real 8042
pulses, both record kinds, every phase budget, the runtime hang, the
manual-reset discrimination) by `tools/wp07a_state_e2e.py`; the
transcripts are archived under `evidence/3_0_0_WP07a/`.

## The OCIRE embed (3.0.0-WP03 G4)

When the OCIRE recovery supervisor (see `ocire/`) hands the machine
to OCOS via kexec, it passes boot facts on the command line
(`oc.mb= oc.log= oc.logsecs= oc.wdt= oc.agent=1`) and attaches its
log agent as a multiboot2 module.  `services/ocembed/` closes the
loop on the OCOS side: it parses the facts, writes SIGNAL_OK into
the recovery mailbox, disarms the i6300esb watchdog inside the
supervised 60 s window, spawns the agent as the protected
`ocire_guard` process (kill is refused by sys_kill), serves the
guard-only `L1SVC_CAT_OCIRE` channel (boot-log append + kernel-log
snapshot) and arms whole-disk write protection on the recovery disk.
On a normal OCBL boot no fact is present and the service stays
dormant.  The five regression properties (partition isolation, boot
signal, bidirectional interfaces, process protection, whole-disk
hard protection) are tested end-to-end by
`tools/run_ocire_supervised_tests.py`.

## Kernel abilities that stayed

The kernel log ring (+ the `/var/log/kernel.log` sink), the health
monitor (1 s sampling + alarms), crash capture (core dump +
crashlog + the panic path), the soft watchdog slots and the hardware
watchdog (TCO/WDAT) are all L0-side mechanisms - the L0 COMMAND
entries that used to drive them died with the user plane; the
mechanisms themselves boot-armed and testable (the batteries in
`evidence/3_0_0_wp03_*`).
