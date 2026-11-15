<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the kernel log (klog)

The unified kernel log (WP-12): five levels, four channels, a
1024-entry RAM ring, live filters, and a rotating on-disk file.
Mechanism in `ocos/kernel/core_lib_klog.{c,h}`; the ring-3 face
(`dmesg`/`logcat`/`logctl`) in `ocos/services/klog/`; the on-disk
sink in `services/klog/src/svc_klog_file.c`. Usage-side guidance
is in [../dev/debug-handbook.md](../dev/debug-handbook.md)
section 2.

## 1. The ring (verbatim)

```c
#define KLOG_RING_EARLY 24                 /* static BSS bootstrap ring (4 KiB) */
#define KLOG_RING_MAX   1024               /* after klog_grow() (heap)   */
#define KLOG_MOD_MAX  12
#define KLOG_MSG_MAX  144

typedef struct {
    u64  ts_ms;
    u8   level;
    char module[KLOG_MOD_MAX];
    char msg[KLOG_MSG_MAX];
} klog_entry_t;

static klog_entry_t g_ring_early[KLOG_RING_EARLY];
static klog_entry_t *g_ring = g_ring_early;
static u32 g_ring_cap = KLOG_RING_EARLY;
static volatile u32 g_ring_head = 0;   /* next write slot               */
static volatile u32 g_ring_count = 0;  /* valid entries (<= cap)        */
static volatile u64 g_total = 0;       /* absolute entries ever written */
static volatile u64 g_dropped = 0;     /* slots overwritten since boot  */
```

- **Trigger**: any kernel code calls
  `klog_write(level, module, fmt, ...)` - every subsystem's error
  paths, the health alarms, the watchdog policy kills, the OCIRE
  refusals; `klog_init()` runs before anything logs (kmain step
  3), `klog_grow()` right after `mem_heap_init`.
- **Processing**: the 24-entry BSS bootstrap ring carries the
  earliest boot lines (the kernel image is pinned below 0x400000,
  so the big ring lives on the heap); `klog_grow()` kmallocs the
  1024-entry ring (168 KiB) and copies the boot entries over -
  nothing is lost. Slot claim is a short cli/sti critical section
  (IRQ-safe). Slot math:
  `klog_slot_of(seq)` -> `off = g_total - seq (>=1)`;
  `slot = (g_ring_head + g_ring_cap - off) % g_ring_cap`.
- **Impact**: the ring always records, even when live channels are
  muted or filtered - it is the post-mortem source of truth;
  `klog_dropped()` counts overwritten slots (shown in the dmesg
  footer).

## 2. Levels and channels

- Levels: `KLOG_DEBUG 0 ... KLOG_FATAL 4`; per-level independent
  switches (`g_level_mask = 0x1F` default, all on); live gate
  `g_live_level = KLOG_INFO` for console+serial (-1 mutes live
  channels; the ring still records).
- Four channels: serial COM1 (polled `arch_exc_ser_puts`), console
  VGA, ring RAM (always), file VFS `/var/log/kernel.log`. The FILE
  channel is skipped when interrupts are disabled at call time
  (deferred to the next task-context writer/flush/timer kick).
- Line format, identical in ring replay, file, and live channels:
  `[   12.345] WARN  net: link up (e1000)` - timestamp =
  seconds.milliseconds since `klog_init` (`core_timer_now_ms`;
  0.000 pre-PIT).

## 3. The emit hook and the on-disk channel

`klog_set_emit_hook(fn)` installs one observer; `klog_on_mount()`
(called by kmain after the first filesystem mounts) installs
`klog_file_hook` from `svc_klog_file.c` and dumps the boot log to
`/var/log/kernel.log`. State: `g_fd` (open O_APPEND fd, -1 =
down), `g_bytes`, `g_synced`, `g_dirty`, `g_rotate_bytes`,
`g_rotate_keep`.

- `kf_sync()` appends every not-yet-synced entry via
  `klog_entry_render`, then auto-rotates at the threshold.
- Rotation: `KLOG_ROTATE_BYTES` = **1 MiB** (1048576; the live
  `logctl threshold` accepts 512..16777216), `KLOG_ROTATE_KEEP 3`:
  `kernel.log` -> `.1` -> `.2` -> `.3`; flush-first, then shift;
  last generation dropped. `kf_shift()` unlinks `.keep`, renames
  `.gen-1 -> .gen` down to 2 (guard against u32 wrap), then
  `kernel.log -> kernel.log.1`.
- `klog_snapshot_text(out, cap)` renders the whole ring
  oldest-to-newest - the OCIRE agent's KLOG_SNAPSHOT consumer.
- `klog_flush_panic()` serves the crash path (ring only, no VFS at
  depth).

## 4. The active filter

`klog_filter_set` (level (-1 any / 0..4 that level AND above),
module exact match, keyword substring; only one filter at a time,
last call wins). **Trigger**: `logctl filter ...`.
**Processing**: applies to LIVE channels AND replay
(`dmesg`/`logcat`). **Impact**: a filter set for a quiet session
also changes what later `dmesg` shows - `logctl filter clear` when
surprised.

## 5. The ring-3 face (3 ops)

| Op | Command | Behavior |
|---|---|---|
| `L1SVC_KLOG_DMESG` (1) | `dmesg [N]` | N = lines (default 30, 1..1024); replay tail through the active filter; footer `-- <shown> lines (ring <count>/1024, dropped <n>) --` |
| `L1SVC_KLOG_LOGCAT` (2) | `logcat -l lvl -m mod -k key -t sec -n N [-f]` | filtered replay; follow mode is the v2.2.0 console-side loop INSIDE the op: 10 Hz poll (100 ms hlt stretches checking for ENTER); prints `-- following (press ENTER to exit) --` / `-- follow stopped --` |
| `L1SVC_KLOG_LOGCTL` (3) | `logctl ...` | 11 subcommands: `status`, `level [-1..4]`, `enable\|disable <level>`, `clear`, `flush`, `rotate`, `threshold <bytes>`, `export [path]`, `test`, `filter clear\|[level] [module] [keyword]` |

`logctl test` emits one line per level (module `klogtest`) - the
filter verification tool. Error anchors:
`klog: flush FAILED (no filesystem mounted?)`, `klog: flushed N bytes to /var/log/kernel.log`, `klog: rotated (kernel.log -> kernel.log.1, keep 3)`, `logctl: level needs -1..4`.

## 6. Who writes, who reads

- Writers: every subsystem's failure branches; `health` alarm
  crossings (module "health"); the watchdog policy kills; the
  crash path (`klog_record(KLOG_FATAL, "kernel", reason)` FIRST,
  ring only); the OCIRE guard refusals (module "ocire"); boot
  bring-up notices.
- Readers: `dmesg`/`logcat`/`logctl` (ring + state), the file
  channel (after mount), the OCIRE agent (snapshot), the panic
  path (flush), the E2E suites (S1-S6 of
  `wp12_watchdog_logging_e2e.py`: levels, switches, filters,
  replay, rotation chains, persistence).
- The log IS the system narrative; `logctl` is how you turn the
  noise down without losing the errors.

## 7. Interaction notes

- The klog family declaration:
  `OC_SERVICE_DECL_L1("klog", "klog service (ring + on-disk channel + filters + ring-3 surface)", 0, 0, klog_l1_dispatch);`
  - no init slots, the mechanism starts in kmain.
- `log.level` config key quiet mode (from `/etc/opencube.conf`)
  suppresses non-failure boot stage lines (the `core_lib_log`
  stage logger, a separate mechanism that prints the
  `[HH:MM:SS.mmm] stage ......... OK` boot ladder).
- `crashlog` (32-slot crash ring) is a *different* ring in the
  crash family - see [crash-dump.md](crash-dump.md).
