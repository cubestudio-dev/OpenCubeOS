<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 extension guide - overview

L0 ships without any L1. The whole point of the architecture is the
surface L0 exposes so L1 *can* exist: 162 numbered extension
interfaces plus the driver / OTA / watchdog / health / klog header
families, and a shared-object loader so modules can arrive at
runtime.

## The layers, once more

```
L1  your module: .so loaded by the in-tree ld.so, or a built-in
    compiled against the surface headers
     |
    162 numbered interfaces + driver/OTA/watchdog/health/klog
    families (INTERFACES.md = the full generated tables)
     |
L0  Open Cube OS kernel + oc> shell (this repo)
```

## Where the surface lives

| Header | What it covers |
|--------|----------------|
| `l1/l1_ext.h` | fb info, font engines, console hook, config read/write, update chain, power, shell bridge, self-test |
| `l1/l1_wp8a.h` | process/fd/signal/mmap/chdir - the Unix-shaped user API |
| `l1/l1_wp8b.h` | the dynamic loader (elf_load_dynamic, so_load, dlsym_impl, reloc_apply...) |
| `l1/l1_wp8cd.h` | shell built-ins, tool embedding, jobs |
| `tui/tui_ext.h` | 37 TUI declarations (windows/menus/forms/panels/text) |
| `drivers/block/driver_block_blk.h` | block registration + I/O |
| `drivers/nic/driver_nic.h` | NIC registration + I/O |
| `drivers/snd/driver_snd.h` | sound registration + playback |
| `kernel/ota/ota_ab.h`, `ota_update.h` | slots + the update chain |
| `kernel/core/core_watchdog.h`, `core_health.h` | watchdog slots + health metrics |
| `kernel/lib/lib_klog.h` | the kernel log |

The complete declaration tables (file, line, signature for all 278
declarations) are generated into [INTERFACES.md](../root/INTERFACES.md)
by `make api-docs`.

## The count that matters

- **162 numbered L1 interfaces** - the README formula
  57+8+13+7+26+11+7+9+8+16, the repo-wide consistency number.
- 278 total callable declarations across the 13 surface headers
  (includes internal helpers and documented unnumbered groups).

## How modules load

Three paths, all real:

1. **Compiled-in L1** - your module compiles into the kernel and
   calls the surface directly (how the TUI command bridge works).
2. **Shared object at runtime** - `libs/ld_so.c` loads an ELF .so,
   resolves symbols, applies relocations:
   [solib-loader.md](solib-loader.md).
3. **Embedded tools** - `tool_register` embeds an ELF payload the
   shell can `run`.

## Walk-through order

1. This page.
2. [surface-map.md](surface-map.md) - the 162 by functional group.
3. The family pages: [driver-block.md](driver-block.md),
   [driver-nic.md](driver-nic.md), [driver-snd.md](driver-snd.md),
   [ota.md](ota.md), [watchdog-health.md](watchdog-health.md),
   [shell-bridge.md](shell-bridge.md).
4. [tui-ext.md](tui-ext.md) - building UI from L1.
5. [writing-modules.md](writing-modules.md) - a complete module,
   from source to `run`.
