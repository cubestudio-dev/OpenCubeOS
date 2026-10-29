<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS documentation

Welcome to the documentation center for Open Cube OS - the
from-scratch x86_64 kernel with the `oc>` recovery shell.

Everything here is written against the real tree: command counts,
interface tables and line statistics are machine-generated
(`make api-docs` -> `docs/data/*.json`), and the manual pages cite
the file that implements what they describe.

## Start here

- New to the system: [Getting started](manual/getting-started.md)
- Want to build it: [Development environment](dev/environment.md)
    then [BUILDING.md](root/BUILDING.md)
- Want to extend it: [L1 extension overview](l1/overview.md)
- Architecture questions: [ARCHITECTURE.md](root/ARCHITECTURE.md) then
    the deep dives under [arch/](arch/)
- Something broke: [Troubleshooting](troubleshooting/common-errors.md)
- The 3.0.0 story: [WP-04 L1 migration record](release/wp04-l1-migration.md)

## The library

| Section | Contents |
|---------|----------|
| [manual/](manual/) | User manual: boot, shell, files, network, TUI, OTA, recovery + all 233 command pages |
| [dev/](dev/) | Developer handbook: environment, build, QEMU, debugging, tests, PR bar, portability, style |
| [arch/](arch/) | Architecture deep dives: boot flow, memory, scheduler, interrupts, ring 3, VFS, network, drivers, TUI |
| [l1/](l1/) | L1 extension guide: the surface, driver families, writing modules, the solib loader |
| [drivers/](drivers/) | Driver bring-up manuals: 16 families |
| [troubleshooting/](troubleshooting/) | Error catalogues and QEMU debugging |
| [internals/](internals/) | Subsystem internals: watchdog, klog, health, crash dump, crypto seeding, OTA/A-B |
| [release/](release/) | Release engineering: process SOP, versioning, website + wiki sync |

## Quick facts

- 233 shell commands (`help -a` in the guest)
- 162 numbered L1 extension interfaces (INTERFACES.md)
- 53 syscalls (`kernel/core/core_syscall.h`)
- 140,672 lines of original source
- Apache 2.0, every file SPDX-tagged
