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
- The merged-app story: [3.0.0-WP-l1apps-merge](release/3.0.0-WP-l1apps-merge.md)

## The library

| Section | Contents |
|---------|----------|
| [manual/](manual/) | User manual: the mega-apps, boot, shell, files, network, TUI, OTA, recovery + all 292 command pages |
| [dev/](dev/) | Developer handbook: environment, build, QEMU, debugging, tests, PR bar, portability, style |
| [arch/](arch/) | Architecture deep dives: boot flow, memory, scheduler, interrupts, ring 3, VFS, network, drivers, TUI |
| [l1/](l1/) | Ring-3 app development: the surface map, app anatomy, the wizard library, writing apps, service families |
| [drivers/](drivers/) | Driver bring-up manuals: 16 families |
| [troubleshooting/](troubleshooting/) | Error catalogues and QEMU debugging |
| [internals/](internals/) | Subsystem internals: watchdog, klog, health, crash dump, crypto seeding, OTA/A-B |
| [release/](release/) | Release engineering: process SOP, versioning, website + wiki sync |

## Quick facts (3.0.0-WP-l1apps-merge)

- 296 shell commands - 284 ring-3 registry apps (the 8 merged
  mega-apps included) + 12 ush built-ins, zero commands in L0
  (`help -a` in the guest)
- 20 SYS_L1SVC service categories / 208 numbered ops
  (INTERFACES.md, l1svc_defs.h)
- 54 syscalls (`ocos/services/sysapi/interface/svc_sysapi_syscall.h`)
- 158,474 lines of original source across 764 files
- Apache 2.0, every file SPDX-tagged
