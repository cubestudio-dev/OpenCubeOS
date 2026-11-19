<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS documentation

Welcome to the documentation center for Open Cube OS - the
from-scratch x86_64 kernel that boots into ring 3 (L0 is a pure
service kernel; the whole user world - the ush shell, the TUI
desktop, the Chinese console, the 8 merged mega-apps - lives in L1
under `ocos/l1apps/`).

Everything here is written against the real tree: command counts,
interface tables and line statistics are machine-generated
(`make api-docs` -> `docs/data/*.json`), and the manual pages cite
the file that implements what they describe.

## Start here

- New to the system: [Getting started](manual/getting-started.md)
- The command surface: [The 8 merged mega-apps](manual/mega-apps.md)
- Want to build it: [Development environment](dev/environment.md)
    then [BUILDING.md](root/BUILDING.md)
- Want to extend it: [Ring-3 app development](l1/overview.md)
- Architecture questions: [ARCHITECTURE.md](root/ARCHITECTURE.md) then
    the deep dives under [arch/](arch/)
- Something broke: [Troubleshooting](troubleshooting/common-errors.md)
- Every command, including the hidden ones:
    [Function dictionary](manual/function-dictionary.md) then
    [Hidden commands & test stubs](manual/hidden-commands.md)
- Every error the system can emit: [Error codes](troubleshooting/error-codes.md)
    then the [full error catalog](troubleshooting/error-catalog.md)
- The whole picture, hidden corners included:
    [System map](arch/system-map.md), [tree map](arch/tree-map.md),
    [hidden mechanisms](arch/hidden-mechanisms.md)
- Every interface: [L1SVC dictionary (221 ops)](dev/interface-dictionary.md),
    [syscall reference (54)](dev/syscall-reference.md),
    [kernel data structures](dev/data-structures.md)
- The merged-app story: [3.0.0-WP-l1apps-merge](release/3.0.0-WP-l1apps-merge.md); the driver framework: [3.0.0-WP08-driver-framework](release/3.0.0-WP08-driver-framework.md)

## The library

| Section | Contents |
|---------|----------|
| [manual/](manual/) | User manual: the mega-apps, boot, shell, files, network, TUI, OTA, recovery, the 311-command function dictionary, hidden commands & 93 test stubs + all command pages |
| [dev/](dev/) | Developer handbook: environment, build, the L1SVC interface dictionary (221 ops), syscall reference, data structures, call chains, debug handbook, hidden debug interfaces, writing services, QEMU, tests, PR bar |
| [arch/](arch/) | System anatomy: the system map, the full repository tree map, hidden mechanisms, private headers + deep dives: boot flow, memory, scheduler, interrupts, ring 3, VFS, network, drivers, TUI |
| [l1/](l1/) | Ring-3 app development: the surface map, app anatomy, the wizard library, writing apps, service families |
| [drivers/](drivers/) | Driver bring-up manuals: 15 hardware topics |
| [troubleshooting/](troubleshooting/) | Error library: 61 errno codes + panics, the all-subsystem error catalog, silent failures, boot/network/storage/QEMU guides |
| [internals/](internals/) | Subsystem internals: watchdog, klog, health, crash dump, crypto seeding, OTA/A-B, the OCIRE guard channel |
| [release/](release/) | Maintainer guide: build system reference, guard scripts, CI gates, tool inventory (117), process SOP, versioning, website + wiki sync |

## Quick facts (3.0.0-WP08-driver-framework)

- 311 shell commands - 300 ring-3 registry apps (the 8 merged
  mega-apps included) + 11 ush built-ins, zero commands in L0
  (`help -a` in the guest)
- 20 SYS_L1SVC service categories / 221 numbered ops
  (INTERFACES.md, svc_sysapi_l1svc.h)
- 54 syscalls (`ocos/services/sysapi/interface/svc_sysapi_syscall.h`)
- 38 drivers across 7 declared families plus the WP-08 driver
  framework (metadata, device tree, power hooks, hotplug, failure
  isolation, deterministic init order)
- 163,935 lines of original source across 807 files
- Apache 2.0, every file SPDX-tagged
