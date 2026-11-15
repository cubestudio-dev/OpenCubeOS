<!-- SPDX-License-Identifier: Apache-2.0 -->
# Architecture guard scripts (maintainer guide)

The checker scripts that turn structural invariants into hard failures.
Every guard below is documented as **input → check logic → failure
output → where it is enforced**. Related pages:
[build-reference](build-reference.md) for the rules that host these
guards, [ci-gates](ci-gates.md) for the CI-side gates,
[tool-inventory](tool-inventory.md) for the full tools/ listing.

The guard philosophy of this repo: an invariant that once failed live
(the incident is cited in each section) becomes a script that fails
the build/release, so the same class of bug can never ship silently
again.

## 1. tools/check_deps.py — dependency-direction gate

**What it guards**: the layering of the `ocos/` tree.

| Layer | May NOT depend on |
|-------|-------------------|
| `ocos/kernel/` | `ocos/drivers/`, `ocos/services/` (nothing above the mechanism layer) |
| `ocos/drivers/` | `ocos/services/` |
| `ocos/l1apps/` | `ocos/kernel/`, `ocos/drivers/`, `ocos/services/` — only `ocos/interface/`-owned headers |

**Input**: nothing (walks the working tree from the repo root).

**Check logic**:
1. Build a basename → owning-layer map by walking
   `ocos/{kernel,drivers,services,interface}` for `.h`/`.c` files.
   Includes are plain basenames resolved against this real inventory —
   exactly how the build's multi-`-I` strategy resolves them.
2. For every `.c`/`.h` under the guarded layers, read each
   `#include "x.h"` line (quoted includes only, no path separators) and
   resolve `x.h` against the inventory.
3. Skip the composition root: `kernel/core_boot_*.c` may depend on
   every layer by design (it is the top of the chain); the two
   registration frameworks include each other's owning headers by
   design.

**Failure output** (exit 1):

```
check_deps: N DEPENDENCY-DIRECTION VIOLATIONS:
  kernel/foo.c:12: includes 'bar.h' -> drivers/baz/bar.h (forbidden direction)
```

Success prints the scanned-file count and
`0 violations (kernel -> nothing above it; drivers -> no services; l1apps -> interface only)`.

**Enforced where**: manual / pre-release hygiene runs
(`python3 tools/check_deps.py`). It is not wired into CI and not into
`make` — run it before structural changes and before a release. Note
that the l1apps constraint is already half-enforced by the build's
`-I` set (the compile fails when an app reaches outside
`ocos/interface/`); this gate double-checks it statically.

## 2. tools/check_bss.py — boot-zone .bss gate

**What it guards**: no regular `.bss` variable may land in the boot
structure zone. `core_boot_entry.S` reserves the page tables
(PML4/PDPT/PD) and the boot stack INSIDE `.bss`; the linker happily
places other objects below them. The WP-06b b4 session live-caught the
consequence: a 64 KiB CLI buffer landed on the live PML4 and the first
>448-byte ISO read corrupted the page tables into a triple fault.

**Input**: one argument — the built ELF (`check_bss.py <elf>`).

**Check logic**:
1. Run `nm -S -n <elf>`; collect every `b`/`B` symbol with address and
   size (3-column lines get size 0).
2. Locate the layout markers `__boot_pml4` (boot zone start),
   `__ocse_work_start`, `__end_of_kernel` (the `.ocsework` NOLOAD work
   zone added in WP-06c for the GUI wizard state).
3. Any marker missing → `FATAL: layout markers not found`, exit 1
   (a mis-linked ELF is itself a failure).
4. A symbol is a violation when it overlaps
   `[__boot_pml4, __ocse_work_start)` and is not itself a
   `__boot_*`/`__ocse_work_start`/`__end_of_kernel` marker. Symbols
   fully inside the work zone are LEGAL (the loader zeroes it via
   `p_memsz`).

**Failure output** (exit 1):

```
[check_bss] FATAL: variables overlap the boot zone (0x...0..0x...1):
  cli_buffer @0x...0+0x10000
```

Success prints the headroom in bytes between the last regular variable
and the boot zone — the number to watch when `.bss` grows.

**Enforced where**: `tools/build_ocse_iso.sh` runs it on the ELF
BEFORE packing — every `make ocse-iso` is gated. Any future overflow
is a build error instead of a boot-time heisenbug.

## 3. tools/v2_registry_gate.py — v2 command-surface freeze

**What it guards**: the v2.0.0 group-B interface freeze — the
declaration-collected command registry must not change shape when the
collection mechanism itself changes.

**Input**: one argument — the ISO to boot
(`python3 tools/v2_registry_gate.py <iso>`).

**Check logic** (boots QEMU BIOS 512M via pexpect, `OC_TOOLS` env for
the relocated QEMU, then sends `help -a`, `help -w`, `help`):
1. Boot reached the `oc>` prompt (TIMEOUT → `FAIL: no shell prompt`).
2. The registry collection log line
   `registry: file-local declarations collected` must be present.
3. `help -a` parsed between the anchors `Available commands (A-Z):`
   and `Commands by work package:` — exactly 231 command lines
   (233 commands; health export/watch count as subcommands). Wrong
   count → `FAIL: expected 231 lines, got N`.
4. `help -w` footer must print `Total: 233 commands.`
5. The service-group view must render (recovery, watchdog, `=== l1 (`).

**Failure output**: one `FAIL:` line per missed check; the full serial
log is dumped to `/tmp/vcheck/v2_gate_out.txt` for triage. Non-zero
exit on any failure.

**Enforced where**: release-time gate of the v2 era. The 3.x successor
of the count invariant is the dynamic floor in the 31-check
regression (see [ci-gates](ci-gates.md) §5): commands may be ADDED,
never removed — the floor starts at 276 (the g3 baseline 275 + p0leak)
and the current tree measures 284.

## 4. tools/gen_userprogs.py — registration constraints

**What it guards**: the embedded ring-3 program registry
(`ocos/kernel/userprogs_data.h`, consumed by
`core_userprog_lookup()`). No name is hand-registered in the kernel.

**Input**: none (discovers from the tree; needs `nasm` + `gcc` on
PATH). Called by `make userprogs`, by `make all` (first step), and by
the `$(USERPROGS_HDR)` file rule.

**Check logic / constraints**:
1. **Discovery is mechanical**: folder apps are
   `ocos/l1apps/<name>/` containing `main.c` or `main.asm` (program
   name = folder name; every `*.c` in the folder links together);
   legacy flat programs are `ocos/l1apps/*.{c,asm}` (name = basename,
   with their v2.x build mode). `_shared/` is a header-only library,
   never an app. `libfoo.c` routes to `tools/build_solib.py`
   (`solib_data.h`, not this header). `ld_so.c` builds with
   `ld_so.ld` @ 0x10000000.
2. **First action**: regenerate `ocos/interface/l1svc_ops.h` via
   `gen_l1svc_ops.py` so wrapper apps always compile against the
   current L1SVC numbering (append-only contract; a failure there
   aborts with `gen_l1svc_ops.py failed`).
3. **Duplicate names are fatal**: after a global A-Z sort of folder +
   flat programs, `fail("duplicate program names (folder vs flat?): [...]")` — a folder app may not shadow a flat program.
4. **Strict i18n gate**: every app must carry `i18n/{en,zh}.json`
   resources (loaded via `i18n_load`); the generated per-app tables
   ride the build.
5. **Mode verification**: `verify_elf()` checks `e_type`/`PT_INTERP`
   per build mode (exec/pie/pie-foo/ld_so) — a mis-linked program
   aborts the generation.
6. **Orphan purge**: a full regeneration drops blobs whose sources no
   longer exist (deleting an app removes it from the registry).
7. **Idempotent emit**: the header is written byte-compare guarded, so
   an unchanged tree regenerates nothing.

**Failure output**: `gen_userprogs: ERROR: <reason>` + exit 1
(duplicate names, missing resources, compile/link failures).

**Enforced where**: `make all` (rebuilds the header first — the WP06g1
catch-2 gap), `make userprogs`, and the staleness gate
`make userprogs-check` wired into `make dist` (any source > 2 s newer
than the header fails the dist with the "STALE embedded user programs
(fix1 int3_user incident class)" message).

## 5. make reproducible-check — byte-identical rebuild gate

**What it guards**: clean-build determinism of the kernel ELF (the G13
gate; the ISO-level determinism stack is below).

**Input**: the current tree; runs two full sub-makes.

**Check logic**: `clean` → `kernel` → `sha256sum` → copy ELF to
`/tmp/.oc_repro_1.elf` → `clean` again → `kernel` again → `cmp -s`.
Determinism sources verified by the target's comments: no
`__DATE__`/`__TIME__`/`__TIMESTAMP__` in the tree, the OCBL ISO packer
pins every timestamp/GUID/volume-serial, gcc/ld are deterministic for
identical inputs+flags, link order is fixed by the wildcard expansion,
`strip --strip-debug` removes path-bearing sections, and
`SOURCE_DATE_EPOCH` is honored when the caller exports it.

**Failure output**: `reproducible-check: FAIL - ELF differs:`, both
SHA256s, the first three `cmp` difference bytes, and `exit 1`.

**Enforced where**: `tools/build_release.sh` step 2 (a release cannot
proceed past a FAIL), and the release SOP step 2 (see
[process](process.md)).

## 6. tools/fat_zero_dates.py — FAT date scrubber

**What it guards**: byte-identical FAT payloads across builds minutes
apart. `mformat`/`mmd`/`mcopy` stamp the current wall clock into
directory entries, which would leak build time into the ISO.

**Input**: one argument — the mtools FAT12/16 image
(`fat_zero_dates.py <img>`; rewrites in place).

**Check logic**: parse the BPB (bytes-per-sector, sectors-per-cluster,
FAT start/size, root dir location); walk every directory entry and
zero ONLY the date/time fields — creation tenth (byte 13), creation
time (14–15), creation date (16–17), last-access date (18–19), write
time (22–23), write date (24–25) — leaving names, attributes, cluster
chains and file sizes untouched. FAT12 chains are followed entry-by-
entry to walk subdirectories.

**Failure output**: none by design (a malformed image raises); the
gate that consumes the image (the ISO packer's determinism seals)
catches downstream drift.

**Enforced where**: the FAT-image build path used by the install/update
tooling before images are embedded or packed. Its ISO-side sibling is
`tools/iso_zero_ctime.py` (rewrites Rock Ridge TF ctime/atime stamps
to 1970 epoch in every ISO9660 directory record — xorriso hardcodes
ctime to assembly time and `-alter_date` offers no ctime knob; the
directory-atime leak was fixed in WP04-fix1). Together with the
packer's pinned volume dates, node mtime/atime, GPT disk GUID and ESP
volume serial, they form the G13 determinism stack that
`reproducible-check` + the release SHA256 three-way check verify.

## 7. tools/fix_file_headers.py — stale `* File:` comment rewriter

**What it guards**: header-comment truth after the WP-10
project_restructure — files moved (e.g. `kernel/snd.c` →
`drivers/snd/driver_snd.c`) but their `* File:` comments still pointed
at the pre-restructure paths.

**Input**: none (walks the repo).

**Check logic**: walk every `.c`/`.h`/`.S`/`.asm` (skipping `.git`,
`build`, `website`, `archive`, `iso`, and
`tools/embed_grub.py`, whose matched line is a template header it
WRITES, not its own header); in the first 15 lines, match
`^(\s*\*\s*File:\s*)(\S+)\s*$`; rewrite the path to the file's actual
repo-relative location. Comment-only — no semantics.

**Failure output**: none (a hygiene tool); prints the lists of changed
/ already-correct / pathless files.

**Enforced where**: one-shot after restructures; re-run it after any
future tree-wide move so `grep '* File:'` stays a reliable index.

## 8. Gate summary table

| Guard | Input | Enforced by | Failure mode |
|-------|-------|-------------|--------------|
| `check_deps.py` | tree | manual / pre-release | exit 1 + file:line list |
| `check_bss.py` | ELF | `build_ocse_iso.sh` (every OCSE ISO) | exit 1 + symbol/address list |
| `v2_registry_gate.py` | ISO | release-time (v2 era) | FAIL lines + non-zero exit |
| `gen_userprogs.py` constraints | tree | `make all` / `make userprogs` / `make dist` | abort with reason |
| `userprogs-check` (make) | tree | `make dist` | exit 1, STALE message |
| `reproducible-check` (make) | tree | `build_release.sh` step 2 / SOP step 2 | exit 1 + diff bytes |
| `fat_zero_dates.py` | FAT image | determinism stack | downstream hash mismatch |
| `iso_zero_ctime.py` | ISO | determinism stack | downstream hash mismatch |
| `fix_file_headers.py` | tree | one-shot hygiene | report only |
| `ocse-vendor-check` (make) | tree | `make ocse-elf` | exit 1 + re-vendor instructions |

Vendor-side integrity (not a script but a rule): `ocse/` is
vendor+edits output only — `ocse-vendor-check` fails when
`ocse/kernel/types.h` is missing, and `tools/ocse_edits.py` is the
single source of truth for every vendored-source edit (E1 OTA API
stripped, E2 GRUB wrapper out, E3 net stack not vendored, E4 crash
plumbing stubbed, E5 ring-3 kill removed). Never hand-edit
`ocse/{kernel,drivers,fs,l1,boot,chinese}/*.c` — a re-vendor erases it.
