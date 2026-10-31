<!-- GENERATED mirror of the repository-root ARCHITECTURE.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS Architecture

This document describes the tree as it exists in 3.0.0-WP03 - every
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
  table and the classic syscall ABI). The default boot entry is the
  ring-3 `ush` shell (`OC_L1_APP_DECL("ush", ..., dflt=1)` in
  `src/L0/boot/l1_launcher.c`).

```
                  GRUB 2.12 (hybrid BIOS + UEFI ISO)
                                  |  multiboot2 + kernel-as-module
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
        |    SD/BT/camera/misc driver families             |
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
src/L1/     the ring-3 applications: ush (the only
            OC_L1_APP_DECL default entry) + the embedded test
            programs (built by `make l1apps`).
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

- the classic syscall ABI (`src/L0/services/sysapi/core_syscall.h`),
  including `SYS_L1SVC(110)`: `(category, op, arg, out, cap)` routed
  by the registry to the owning service's `l1_dispatch`.
- the categories/ops are the frozen ABI table in
  `src/L0/services/sysapi/l1svc_defs.h` (category number -> the
  owning services/ folder; category 18 "ocire" is the declared
  exception - the OCIRE log-agent contract's ABI name, implemented
  by `services/ocembed/`).

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
On a normal GRUB boot no fact is present and the service stays
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
