<!-- SPDX-License-Identifier: Apache-2.0 -->
# Ring-3 apps - overview

L0 is a pure service kernel: it finishes boot by launching `ush`,
the ring-3 shell, and every user-visible command is itself a ring-3
program. The whole point of the architecture is that the surface
L0 exposes is small, numbered and complete - so apps can exist.

## The layers

```
L1  ush (the boot shell) + 284 registry programs
    in ocos/l1apps/<name>/main.c - freestanding C against
    ocos/interface/*.h
     |
    SYS_L1SVC(cat, op, arg, out, cap)   20 categories, ~245 ops
    SYS_* (open/read/write/fork/execve/wait4/readline/getch/...)
     |
L0  Open Cube OS service kernel
    (ocos/kernel + ocos/services + ocos/drivers)
```

## Where the surface lives

| Header (ocos/interface/) | What it covers |
|---------------------------|----------------|
| `l1_api_syscall.h` | syscall wrappers, string/IO helpers, `l1svc_main()` - the verbatim wrapper body |
| `l1_svc_ops.h` | the category/op numbering contract (auto-mirrored from the L0 header) |
| `l1_i18n_api.h` | `tr_()` + the per-app bilingual tables |
| `l1_wizard_api.h` | the shared wizard primitives (menus, asks, confirms, anchor probes) |
| `l1_fs_api.h` | path helpers shared by the file tools |
| `l1_editor_core.h` | the editor engine behind `edit` and the TUI editor |
| `l1_i18n_res.h` | the shared `MSG_ID` system-message numbering (L0/L1 common) |
| `l1_diag_cmds.h` | the shared `dev`/`drv` diag command layer |
| `l1_api_surface.h` | the umbrella: the audit point for "what L1 can see" |
| `l1_crt_entry.asm` | the linked (not included) `_start` trampoline |

The service-side declarations live in
`ocos/services/*/interface/` (one folder per facet); the numbering
mirror regenerates with `tools/gen_l1svc_ops.py`.

## The counts that matter

- **284 registry programs** - 276 historical commands (the
  interface-freeze floor; commands are added, never removed) plus
  the 8 merged mega-apps.
- **20 L1SVC categories** (time/block/nic/snd/power/ota/fs/net/
  health/klog/watchdog/crypto/l1/display/input/camera/usb/ocire/
  diag/tui), ~245 numbered ops.
- Every program ships `i18n/{en,zh}.json` - the build fails on a
  missing resource (a program without translations is a delivery
  violation, not a warning).

## How a command runs

```
ush> disk-tool lsblk
  ush forks; child execve("disk-tool", ["disk-tool","lsblk"])
  app_main(argc, argv) sees argv[1] = "lsblk"
  -> classic dispatch: table lookup -> sys_l1svc(BLOCK, LSBLK, ...)
  -> service output printed verbatim
  ush wait4()s the child, prints the next prompt
```

The historical single commands are the same mechanism one level
simpler: `lsblk/main.c` is `l1svc_main(argc, argv, CAT, OP)` - the
v2.2.0 ush SVC runner semantics, byte for byte.

## Walk-through order

1. This page.
2. [surface-map.md](surface-map.md) - the categories/ops by group.
3. [app-anatomy](app-anatomy.md) - what a main.c is made of.
4. [writing-apps](writing-apps.md) - a complete app, source to `run`.
5. The family pages: drivers, OTA, loader, TUI, watchdog/health.
