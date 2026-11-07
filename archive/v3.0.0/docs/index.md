<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS documentation

Welcome to the documentation center for Open Cube OS - the
from-scratch x86_64 kernel that boots into ring 3 (L0 is a pure
service kernel; the whole user world - the ush shell, the TUI
desktop, the Chinese console - lives in L1 under `src/L1/`).

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

## Quick facts (3.0.0-WP06c)

- 285 shell commands - 274 ring-3 registry apps + 11 ush built-ins,
  zero commands in L0 (`help -a` in the guest)
- 20 SYS_L1SVC service categories / 190 numbered entries
  (INTERFACES.md, l1svc_defs.h)
- 54 syscalls (`src/L0/services/sysapi/core_syscall.h`)
- 128,890 lines of original source across 624 files
- Apache 2.0, every file SPDX-tagged
