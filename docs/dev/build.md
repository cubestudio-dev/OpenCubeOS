<!-- SPDX-License-Identifier: Apache-2.0 -->
# Building and packaging

The developer view of the build system. For the user-level guide see
[BUILDING.md](../root/BUILDING.md) at the repository root - this page
adds the developer-workflow details.

## Targets you will actually use

```
make -j$(nproc) kernel     # fast inner loop: ELF only
make iso                   # + bootable ISO
make userprogs             # regenerate the embedded program registry
make test                  # host unit tests (no QEMU needed)
make run-bios              # boot the ISO
make all                   # userprogs + kernel + ISO + src zip + OTA
make clean                 # start fresh
make reproducible-check    # two clean builds -> byte-identical ELF
```

## The app embed chain

`make userprogs` (part of `make all`, and of the staleness gate)
runs `tools/gen_userprogs.py`: it discovers every
`ocos/l1apps/<name>/` folder + flat legacy program, compiles each
freestanding (`-I ocos/interface`), links per image mode, emits the
per-app i18n tables, and writes `ocos/kernel/userprogs_data.h`
(the blobs + the `OC_USERPROGS[]` registry). Nothing is
hand-registered; a new folder IS a new command.

The staleness gate: any source, interface header or i18n json newer
than the generated header forces regeneration - `make dist` fails
on a stale embed.

## The compile cache makes the inner loop fast

`tools/oc_cc.sh` wraps every compile rule with a content-addressed
cache: key = compiler identity + full flag set + source content hash
+ the content hashes of every header dependency.

- Warm objects are byte-identical to cold ones.
- `make cache-stats` / `make cache-clear` manage it.
- A stale cache entry is impossible: any header edit changes the key.

## Version and build number

- `OC_RELEASE_VERSION` defaults to the version literal in the
  Makefile; override on the command line
  (`make OC_RELEASE_VERSION=3.0.0-test1 iso`) and it ends up in the
  banner, `uname -a`, the OTA manifest and the ISO volume id.
- `OC_KERNEL_BUILD` = `git rev-list --count HEAD` + the permanent
  +21 alignment offset (`OC_KERNEL_BUILD_OFFSET := 21`) - it feeds
  `uname` and the update.json "build" field at release time. The
  numeric comparison (BUG-0347) makes the OTA check
  build-number-aware.
- The version recompile trigger (`kernel/generated/version_gen.h`,
  cmp-guarded) makes -D flag changes visible to the dependency
  graph - a stale banner over new code can no longer happen.

## Packaging determinism

- **SRC zip** (`make src-zip`): a `git archive` - tree order, commit
  mtimes, index permissions; byte-identical for the same commit.
- **OTA** (`make ota`): kernel ELF + manifest; the manifest carries
  the build day (UTC) so the package is byte-reproducible within a
  day.
- **ELF**: no `__DATE__`/`__TIME__`/`__TIMESTAMP__` anywhere in the
  tree (a make gate greps for them), fixed link order,
  `strip --strip-debug` removes path-bearing sections.

## Multi-config and cross builds (developer notes)

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_NVME=n iso  # hardware profile: NVME off
make CONFIG_AHCI=n CONFIG_VIRTIO_BLK=n iso
make ARCH=arm64         # PL011 bring-up shell ELF
make ARCH=riscv         # NS16550A bring-up shell ELF
```

The configurator generates `kernel/generated/config_gen.h` with
compare-guarded macros (the 3.0.0 knobs are the hardware profile
options NVME / AHCI / VIRTIO_BLK; the TUI and CJK pipeline are
always compiled in). Disabled drivers route to stubs, never to
deleted code.

## Scriptable API

`tools/build.sh` doubles as a function library:

```
source tools/build.sh
build_all; build_iso; config_load; cache_lookup; cross_compile ...
tools/build.sh api-selftest     # verifies the spec'd interfaces
```

Release automation (see [../release/process.md](../release/process.md))
composes these instead of parsing make output.
