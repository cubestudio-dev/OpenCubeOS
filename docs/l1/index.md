<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 app development guide

How to write, build and ship the ring-3 programs of Open Cube OS -
from a one-line service wrapper to a full merged mega-app.

The app model in one paragraph: **one command = one app = one
folder** under `ocos/l1apps/<name>/` holding `main.c` (+ any extra
`.c`) and `i18n/{en,zh}.json`. The build discovers folders
mechanically (no hand-written tables), compiles each app freestanding
against `ocos/interface/`, and embeds every image into the kernel's
`OC_USERPROGS[]` registry (284 programs today). At runtime `ush`
forks + execves registry programs by name; apps reach every kernel
service through the `SYS_L1SVC` syscall (category + op + argument
line + output buffer).

| Page | What it covers |
|------|----------------|
| [overview](overview.md) | the app model, the layers, the counts |
| [surface-map](surface-map.md) | every L1SVC category and op, the syscall bridge |
| [app-anatomy](app-anatomy.md) | main.c / crt0 / i18n / wizard_lib.h - the full anatomy |
| [writing-apps](writing-apps.md) | a complete app from source to `run` - tutorial |
| [wizard-library](wizard-library.md) | the shared wizard primitives behind the 8 mega-apps |
| [driver-block](driver-block.md) | extending the block service |
| [driver-nic](driver-nic.md) | extending the NIC service |
| [driver-snd](driver-snd.md) | extending the sound service |
| [ota](ota.md) | the OTA/A-B service from the app side |
| [solib-loader](solib-loader.md) | the in-tree ld.so + ET_DYN programs |
| [tui-ext](tui-ext.md) | the TUI service surface |
| [watchdog-health](watchdog-health.md) | watchdog + health from apps |

## The three doors every app offers

The 8 merged apps (`update`, `disk-tool`, `net-tool`, `sound-tool`,
`files`, `sysdiag`, `hwinfo`, `power`) established the three-door
contract - no-arg wizard, `direct` passthrough, classic op dispatch.
New apps are free to adopt it; the wizard primitives are one
`#include "wizard_lib.h"` away.
