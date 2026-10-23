<!-- SPDX-License-Identifier: Apache-2.0 -->
# Building and packaging

The developer view of the build system. For the user-level guide see
[BUILDING.md](../root/BUILDING.md) at the repository root - this page
adds the developer-workflow details.

## Targets you will actually use

```
make -j$(nproc) kernel     # fast inner loop: ELF only
make iso                   # + bootable ISO
make test                  # host unit tests (no QEMU needed)
make run-bios              # boot the ISO
make clean                 # start fresh
make reproducible-check    # two clean builds -> byte-identical ELF
```

## The compile cache makes the inner loop fast

`tools/oc_cc.sh` wraps every compile rule with a content-addressed
cache: key = compiler identity + full flag set + source content hash
+ the content hashes of every header dependency (the dependency probe
is a `gcc -MM -MG` pass with the -MMD/-MP flags stripped so the two
never fight).

- Cold build of the release tree: 46.6 s. Warm: 14.4 s (186/186 hits,
  3.2x). Warm objects are byte-identical to cold ones.
- `make cache-stats` / `make cache-clear` manage it.
- A stale cache entry is impossible: any header edit changes the key.

## Version and build number

- `OC_RELEASE_VERSION` defaults to `git describe --tags`; override on
  the command line (`make OC_RELEASE_VERSION=WP-14-test1 iso`) or it
  ends up in the banner, `uname -a`, the OTA manifest and the ISO
  volume id.
- The build number is the commit count (`git rev-list count`) - it
  feeds `uname` and the update.json "build" field at release time.

## Packaging determinism

- **SRC zip** (`make src-zip`): a `git archive` - tree order, commit
  mtimes, index permissions; byte-identical for the same commit.
  Excludes the website/out/build/deliverables trees via
  `SRC_EXCLUDE`.
- **OTA** (`make ota`): kernel ELF + manifest; the manifest carries
  the build day (UTC) so the package is byte-reproducible within a
  day.
- **ELF**: no __DATE__/__TIME__/__TIMESTAMP__ anywhere in the tree
  (a make gate greps for them), fixed link order, `strip
  --strip-debug` removes path-bearing sections.

## Multi-config and cross builds (developer notes)

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_TUI=n CONFIG_CHINESE=n iso    # 216-command image
make ARCH=arm64         # PL011 bring-up shell ELF
make ARCH=riscv         # NS16550A bring-up shell ELF
```

The configurator generates `kernel/generated/config_gen.h` with
compare-guarded macros; disabled features route to real ASCII
equivalents (console_cn -> screen_console), never to deleted code.
The GNU make `%` wildcard limitation (only the first `%` in a pattern
matches) is why the TUI/Chinese source filters look the way they do -
do not "simplify" them.

## Scriptable API

`tools/build.sh` doubles as a function library:

```
source tools/build.sh
build_all; build_iso; config_load; cache_lookup; cross_compile ...
tools/build.sh api-selftest     # verifies the 8 spec'd interfaces
```

Release automation (see [../release/process.md](../release/process.md))
composes these instead of parsing make output.
