<!-- SPDX-License-Identifier: Apache-2.0 -->
# Build system reference (maintainer guide)

The complete, mechanic-level reference to the Open Cube OS build: every
phony target, every internal file target, every variable, every `-D`
macro, every per-file optimization override, and the parallelism /
compile-cache machinery. Companion pages: [guard-scripts](guard-scripts.md)
(the checker scripts), [ci-gates](ci-gates.md) (CI and the release
chain), [tool-inventory](tool-inventory.md) (all 117 `tools/` files).

Build files covered here: `Makefile` (top level), `ocire/ocire.mk`,
`ocbl/ocbl.mk`, `ocse/ocse.mk`, `ocos/kernel/arch/{arm64,riscv}/build.mk`,
plus the entry wrappers `tools/build.sh` and `build.ps1`.

## 1. Architecture dispatch and entry points

`ARCH ?= x86_64`. Any value outside `x86_64 arm64 riscv` fails
immediately with `$(error unsupported ARCH '<ARCH>' - supported: x86_64 arm64 riscv)` — the make run aborts before a single command runs.

- `ARCH=x86_64` (default): the full body of the `Makefile` — kernel,
  ISO, src zip, OTA, QEMU, config, cache, tests — plus the three
  component includes (`ocire.mk`, `ocbl.mk`, `ocse.mk`).
- `ARCH=arm64` / `ARCH=riscv`: the entire x86_64 body is skipped and
  `ocos/kernel/arch/$(ARCH)/build.mk` is included instead (bring-up
  shells only; see §7).

Three equal entries build the same thing:

| Entry | What it adds over raw make |
|-------|----------------------------|
| `make <target>` | nothing — the source of truth |
| `tools/build.sh <target>` | dependency detection with the exact Debian package per command, `--install` apt fallback, no-root `install-toolchain.sh` hint, `OC_TOOLS` autodiscovery (`/usr`, `$HOME/tools/grub`), `JOBS` parallelism (default `nproc`) |
| `build.ps1` (Windows) | WSL2 (`wsl.exe` + `wslpath`) or MSYS2/Git-Bash (`cygpath`) translation into `tools/build.sh`; `-Target` (default `all`), `-Install` |

`tools/build.sh` also exposes the scriptable API (WP-13 spec 8):
`build_all`, `build_iso`, `build_src`, `build_ota`, `config_load`,
`config_save`, `config_get`, `cache_lookup`, `cross_compile`,
`cross_toolchain_setup` — source the script to call them.

`.DEFAULT_GOAL := kernel`: a bare `make` builds only
`build/opencube.elf`. The full WP-13 pipeline is `make all`.

## 2. Top-level phony targets (26)

Format: **trigger → recipe logic → failure consequence.**

| # | Target | Trigger / prerequisites | Logic | On failure |
|---|--------|------------------------|-------|------------|
| 1 | `all` | `userprogs $(KERNEL_ISO) src-zip ota` | WP-13 unified pipeline; rebuilds embedded programs FIRST (WP06g1 catch-2: `make all` once shipped a stale `userprogs_data.h`), then kernel + ISO, deterministic src zip, OTA package; prints the four product paths | any sub-gate failure stops the pipeline |
| 2 | `kernel` | `$(KERNEL)` = `build/opencube.elf` | pre-WP-13 semantics: two-stage link of the ELF only (see §3.2) | link/compile error aborts |
| 3 | `iso` | `$(KERNEL_ISO)` | packs the dual-firmware OCBL ISO via `tools/build_iso.sh`; prerequisites include `$(USERPROGS_HDR)` so a standalone `make iso` can never pack stale embedded programs (WP06g2) | packer script exits non-zero → make fails |
| 4 | `src-zip` | none (phony) | `git ls-files` filtered by `SRC_EXCLUDE`, intersected with the tag `v$(OC_RELEASE_VERSION)` tree when the tag exists (`ls-tree -r` + `grep -Fx -f`), then `git archive --format=zip`; HEAD fallback prints a loud "development cut, NOT a release asset" warning; empty `.srclist` (not a git checkout) → `exit 1` telling you to use `make dist` | hard fail on non-git trees; warning-only on HEAD cut |
| 5 | `ota` | `$(KERNEL) $(KERNEL_ISO)` | `tools/make_update_pkg.sh` → `dist/OpenCubeOS-<ver>-update.tar.gz`; the ISO is a prerequisite so a stale-ISO + fresh-OTA set is not producible (v2.0.0 fix) | script failure aborts |
| 6 | `reproducible-check` | none | clean→kernel→sha256, twice; `cmp -s /tmp/.oc_repro_1.elf $(KERNEL)`; PASS message + both hashes, or FAIL + first 3 `cmp` diff bytes + `exit 1` | exit 1 on any byte difference |
| 7–12 | `run-bios`, `run-uefi`, `run-bios-gui`, `run-uefi-gui`, `run-bios-persist`, `run-uefi-persist` | `$(KERNEL_ISO) $(ETC_IMG)` | QEMU boots: 256M RAM, IDE `/etc` disk, `-no-reboot`, serial stdio. `-display none` (headless), `-display gtk` (GUI — the PS/2 keyboard needs a window to deliver scancodes), `-snapshot` off (persist: `/etc/opencube.conf` edits are written back into `build/etc.img`, WP-09-fix5). UEFI variants copy `$(OVMF_FD)` to `build/OVMF_WORK.fd` and attach it as pflash | QEMU exit code propagates |
| 13–14 | `shot-bios`, `shot-uefi` | `$(KERNEL_ISO)` | `tools/qemu_shot_vnc.py` boots via VNC, monitor-screendumps, converts PPM→PNG with PIL → `build/shot-{bios,uefi}.png` | tool failure aborts |
| 15 | `clean` | none | `rm -rf $(BUILD) $(DIST) $(ISO_DIR)/boot/opencube.elf $(GEN_DIR)/grub_boot_data.c` — removes objects, ELFs, ISOs, dist products, the transient stage-2 GRUB data | — |
| 16 | `dist` | `userprogs-check $(KERNEL_ISO)` | staleness gate first (below), then a timestamped `zip -qr` (NOT git archive) with inline exclusions (`build/`, `dist/`, `.git/`, `tools/push_to_git.py`, `releases/`, website build trees, `ocos/kernel/generated/*`), ISO copy, `sha256sum` sidecar | gate failure aborts before anything is packed |
| 17 | `defconfig` | none | heredoc writes `.config` with `CONFIG_NVME=y CONFIG_AHCI=y CONFIG_VIRTIO_BLK=y` | — |
| 18 | `menuconfig` | none | `tools/oc_menuconfig.sh` — dependency-free interactive editor (number=toggle, s=save, q=quit, r=reset) | — |
| 19 | `cache-stats` | none | awk over `$(OC_CACHE_DIR)/STATS`: HIT/MISS counts, hit rate %, entry count; prints "no STATS yet" on a cold cache | never fails |
| 20 | `cache-clear` | none | `rm -rf $(OC_CACHE_DIR)` | — |
| 21 | `test` | none | `python3 tools/run_host_tests.py` compiles and runs every `tests/host_*.c` harness | **exit code = number of failed tests** |
| 22 | `l0-i18n` | `$(L0_I18N_GEN)` | `tools/gen_l0_i18n.py` regenerates `ocos/kernel/core_lib_i18n_data.c` from `etc/i18n/l0/{en,zh}.json` (49 keys each); idempotent | generator failure aborts |
| 23 | `userprogs` | `$(USERPROGS_HDR)` | `tools/gen_userprogs.py` full regeneration of the embedded-program header (discovery + registry + orphan purge, see [guard-scripts](guard-scripts.md)) | any per-program compile/link failure aborts |
| 24 | `userprogs-check` | none | staleness gate: `ls -t $(USERPROG_SRCS)` newest vs `stat -c %Y` of the header; newest source more than 2 s newer → `ERROR: <src> is newer than userprogs_data.h` + `exit 1` (2 s slack absorbs fresh-clone mtime jitter) | exit 1; wired into `make dist` |
| 25 | `api-docs` | none | `tools/gen_api_docs.py --help-capture evidence/help_a_runtime.txt --l1-registry evidence/help_a_runtime.txt` → `docs/data/*.json` | generator failure aborts |
| 26 | `FORCE` | `.PHONY` | always-out-of-date helper driving the cmp-guarded generators | — |

## 3. Internal file targets

### 3.1 Order-only and generated headers

| Target | Rule essence | Why it exists |
|--------|--------------|---------------|
| `$(BUILD)` | `mkdir -p build` | order-only prerequisite of every object |
| `$(GEN_DIR)/version_gen.h` | `FORCE` + emit to `.tmp`, `cmp -s` guard, mv only on change | emits `#define OC_VERSION_STAMP "<ver> build <build>"`. `-D` flags are invisible to `-MMD`, so incremental builds used to keep a stale banner (WP-10-tui-qa: an ISO booted a `-ga4d5342` banner over `-ga29ac53` code). The header makes version changes visible to the dependency graph |
| `$(GEN_DIR)/config_gen.h` | same cmp-guard pattern | emits `OC_CONFIG_NVME/AHCI/VIRTIO_BLK` 0/1 plus `driver_block_<x>_init()` no-op stubs when off; order-only prerequisite of `$(KERNEL_OBJ) $(STUB_OBJ) $(GRUB_DATA_OBJ)` — must exist before ANY object compiles, but only the `.d` files trigger rebuilds |

### 3.2 Two-stage kernel link

```
KERNEL_OBJ + grub_boot_stub.o --ld--> build/opencube_base.elf (+strip)
embed_grub.py "$(OC_TOOLS)" base.elf --> ocos/kernel/generated/grub_boot_data.c
grub_boot_data.c --compile--> grub_boot_data.o
KERNEL_OBJ + grub_boot_data.o --ld--> build/opencube.elf (+strip)
```

- `$(STUB_OBJ)`: `core_boot_grubstub.c` compiled separately (stage-1
  link only; filtered out of `KERNEL_C`).
- `$(GRUB_DATA_C)`: `tools/embed_grub.py` embeds the GRUB i386-pc
  `boot.img` (512 B) + `core.img` (grub-mkimage with the modules the
  on-disk cfg needs) as C arrays — the in-kernel installer payload.
  The kernel ELF itself is NOT embedded (GRUB attaches it as a
  multiboot2 module via grub.cfg).
- `$(KERNEL)`: stage-2 link also carries `$(L0_I18N_GEN)` and
  `$(IME_PHRASE_GEN)` as prerequisites so the i18n/IME tables are
  fresh at link time.
- `-include $(KERNEL_OBJ:.o=.d)`: BUG-025 FIX — `-MMD -MP` header
  tracking. OCIRE and OCSE carry their own `-include` lines for the
  same reason.

### 3.3 Other file targets

| Target | Logic |
|--------|-------|
| `$(ETC_IMG)` = `build/etc.img` | `truncate -s 32M`, mtoolsrc with `drive e:`, `mformat -F -v OCS_ETC e:` (FAT32), `mcopy etc/opencube.conf` — the persistent `/etc` volume (WP-09-fix5) |
| `$(USERPROGS_HDR)` = `ocos/kernel/userprogs_data.h` | real file target: any `USERPROG_SRCS` entry (or the generator scripts) newer → `gen_userprogs.py`; idempotent byte-compare emit so an up-to-date tree rebuilds nothing |
| `$(L0_I18N_GEN)` | `etc/i18n/l0/{en,zh}.json` or the generator newer → regenerate |
| `$(IME_PHRASE_GEN)` + its `.h` | `tools/ime_phrases.txt` or `gen_pinyin_phrase.py` newer → regenerate |
| `build/ocos/**/*..o` | generic `$(OC_CC) $(CFLAGS) -c` (i.e. `-O2`) |
| `build/boot_%.o` | NASM `-f elf64 -F dwarf -g` over `ocos/kernel/core_boot_*.S` |
| `build/core/arch/%.o` | NASM over `ocos/kernel/arch/x86_64/*.S` |
| `$(KERNEL_ISO)` | `bash tools/build_iso.sh`; prerequisites: kernel, `userprogs_data.h`, `ocbl_bios.bin`, `ocbl_x64.efi`, `ocire.elf`, the script itself |
| `$(SRC_ZIP)` / `.srclist` / `.taglist` | see `src-zip` above |

## 4. Variables (complete list)

### 4.1 Paths, tools, architecture

| Variable | Default / derivation | Meaning |
|----------|----------------------|---------|
| `OC_ROOT` | `abspath` of the Makefile dir | repo root |
| `BUILD` / `ISO_DIR` / `DIST` | `$(OC_ROOT)/build`, `/iso`, `/dist` | output dirs |
| `ARCH` | `x86_64` | target-machine dispatch (`x86_64 arm64 riscv`, else `$(error)`) |
| `CC` / `LD` / `NASM` / `QEMU` | `gcc` / `ld` / `nasm` / `qemu-system-x86_64` | PATH-resolved tools |
| `OC_TOOLS` | `/usr` | relocated toolchain root (dpkg -x trees); `build.sh` autodetects |
| `OVMF_FD` | `$(OC_TOOLS)/share/ovmf/OVMF.fd` | UEFI firmware |
| `QEMU_DATADIR` | `$(OC_TOOLS)/share/qemu` | QEMU `-L` path |
| `SEABIOS_DIR` | `$(OC_TOOLS)/share/seabios` | BIOS firmware |
| `CROSS` (arch builds) | `aarch64-linux-gnu-` / `riscv64-linux-gnu-` | cross prefix |

### 4.2 Version and identity

| Variable | Default | Meaning |
|----------|---------|---------|
| `OC_RELEASE_VERSION` | `3.0.0-NAK` (fixed literal; `?=` override wins) | kernel banner / `uname` / update-check string; override per the E2E update test, e.g. `make OC_RELEASE_VERSION=WP-10c-test1` |
| `OC_KERNEL_BUILD_OFFSET` | `21` | alignment over `git rev-list --count HEAD`; keeps numeric OTA comparison monotonic above the published ids 278/321/414/416 (BUG-0347) |
| `OC_KERNEL_BUILD` | `expr git rev-list --count HEAD + 21` | monotonic build id; override `make OC_KERNEL_BUILD=42` |

### 4.3 Configuration

| Variable | Default | Effect |
|----------|---------|--------|
| `CONFIG_NVME` / `CONFIG_AHCI` / `CONFIG_VIRTIO_BLK` | `y` (from `.config` if present) | `=n` filters the driver `.c` out of `KERNEL_C` AND stubs its init call via `config_gen.h` |
| `.config` | (optional) | make fragment written by `defconfig`/`menuconfig`; command-line overrides win |

### 4.4 Sources and objects

| Variable | Content |
|----------|---------|
| `KERNEL_DIRS` | `ocos/kernel`, `ocos/kernel/arch/x86_64`, `ocos/services`, `ocos/drivers` + wildcards of `ocos/services/*/src`, `ocos/drivers/*/` |
| `KERNEL_INC` | `-I` for every `KERNEL_DIRS` + `tools` + `ocos/services/*/interface` + `ocos/kernel/generated` + `include` (multi-`-I` plain-basename include strategy) |
| `KERNEL_C` | wildcard `*.c` over `KERNEL_DIRS`, minus `core_boot_grubstub.c`, minus config-filtered drivers |
| `KERNEL_ASM_B` / `KERNEL_ASM_K` | `ocos/kernel/core_boot_*.S` / `ocos/kernel/arch/x86_64/*.S` |
| `KERNEL_OBJ` | pattern-subst `.c→.o` (build-mirrored), boot `.S→boot_%.o`, arch `.S→core/arch/%.o` |
| `KERNEL` / `KERNEL_ISO` | `build/opencube.elf` / `build/opencube.iso` |
| `GEN_DIR` / `GRUB_DATA_C` / `GRUB_DATA_OBJ` / `STUB_OBJ` / `BASE_ELF` | generated dir + two-stage link set |
| `VERSION_GEN` / `CONFIG_GEN` | cmp-guarded generated headers |
| `EMBED_PY` / `BUILD_C_PY` / `BUILD_SOLIB_PY` | `tools/embed_userprog.py` / `build_c_userprog.py` / `build_solib.py` |

### 4.5 Embedded programs, packaging, cache

| Variable | Content |
|----------|---------|
| `USERPROG_SRCS` | wildcards over `ocos/l1apps/*.{asm,c}`, `ld_so.c`, `*/*.c`, `*/*.asm`, `ocos/interface/*.h`, `*/i18n/*.json`, `_i18n/*/*.json` (json added WP06g2 after the 548-file hint sweep live-catch) |
| `USERPROGS_HDR` | `ocos/kernel/userprogs_data.h` |
| `ETC_IMG` | `build/etc.img` |
| `SRC_ZIP` / `SRC_EXCLUDE` / `SRC_REV` | `dist/OpenCubeOS-src-<ver>.zip`; exclusions `website/public/downloads website/out website/.next website/node_modules build dist releases .zscripts public/deliverables evidence archive trash`; rev = tag `v<ver>` else `HEAD` |
| `OTA_PKG` | `dist/OpenCubeOS-<ver>-update.tar.gz` |
| `OC_CACHE_DIR` | `$(HOME)/.cache/opencube-cc` |
| `L0_I18N_GEN` / `IME_PHRASE_GEN` | generated i18n/IME sources |

### 4.6 Flags

| Variable | Value |
|----------|-------|
| `CFLAGS` | `-ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g -std=gnu11 $(KERNEL_INC) -DOC_RELEASE_VERSION=\"…\" -DOC_KERNEL_BUILD=N -MMD -MP` |
| `ASFLAGS` | `-f elf64 -F dwarf -g` |
| `OC_CC` | `bash tools/oc_cc.sh $(CC)` — the compile-cache wrapper |
| `LDFLAGS` | `-n -nostdlib -T ocos/kernel/arch/x86_64/linker.ld -z max-page-size=0x1000 -z noexecstack` |

The `-Wall -Wextra -Werror` triple appears in every component's flags
(kernel, OCIRE, OCBL, OCSE, agent) — this is the project's "lint"; CI
runs it on every push (see [ci-gates](ci-gates.md)).

## 5. The .mk includes

### 5.1 ocire.mk (OCIRE recovery kernel — 11 phony targets)

Independent kernel: own flags (`OCIRE_CFLAGS`, same freestanding set,
only `-I ocire -I ocire/generated -I include`), own linker
`ocire/linker_ocire.ld`, static-pattern rules that win over the generic
`$(BUILD)/%.o` rule, own `-include $(OCIRE_OBJ_C:.o=.d)` (added after
the WP-02 live-catch where stale OCIRE objects wrote garbage start LBAs
into both MBRs). `OCIRE_VERSION ?= $(OC_RELEASE_VERSION)` (BUG #92 fix
— the banner used to lag four milestones). Internal generators:
`ocire/generated/ocire_cn_font_data.c` (sed rewrite of the OCSE font
data, cmp-guarded) and `ocire_version_gen.h` (`OCIRE_STAMP`,
cmp-guarded).

| Target | Product / logic |
|--------|-----------------|
| `ocire-agent` | ring-3 log agent compiled like an l1apps C program (user.ld @ 0x400000, int 0x80) but shipped standalone → `build/ocire/agent.elf` |
| `ocire-kernel` | `build/ocire/ocire.elf` |
| `ocire-iso` | `tools/build_ocire_iso.sh` → `build/ocire/ocire.iso` |
| `ocire-disk` | `tools/make_ocire_disk.sh` dual-boot + recovery test disks |
| `ocire-run-bios` / `-uefi` / `-bios-gui` / `-uefi-gui` | QEMU runs (128M, own `OVMF_OCIRE.fd` pflash copy) |
| `ocire-test` | `tools/run_ocire_tests.py` (boot/help/unknown/backspace/shutdown × BIOS+UEFI) |
| `ocire-dual-test` | dual-boot + partition isolation battery |
| `ocire-shell-test` | WP-02 group-4 recovery shell battery (help/logread/syscheck/repair quick/full/return incl. digest damage + nuked BPB) |

### 5.2 ocbl.mk (OCBL boot loader — dual flavor)

One source tree, two flavors: `-DOCBL_FLAVOR_BIOS` with
`-mcmodel=small`, and `-DOCBL_FLAVOR_UEFI` with `-mcmodel=large` (every
address a 64-bit `movabs` the PE converter can relocate). Baked-in
assertions (build fails on violation): both stage1 flavors exactly
512 B; `tramp16.bin` ≤ 0x300 B; `handoff_blob.bin` ≤ 0x1000 B; the EFI
PE header check (MZ / PE\0\0 / machine 0x8664 / subsystem 10).
`include/ocbl_state.h` is a prerequisite of EVERY OCBL object (WP07a
stale-address live-catch). The EFI flavor links an intermediate ELF
with `--emit-relocs` and converts via `ocbl/elf_to_efi.py` (complete
`.reloc` directory; the plain `ld -m i386pep` route mis-relocated
under OVMF).

Phony targets: `ocbl-bios` (stage1 El Torito + MBR flavors + core →
`ocbl_bios.bin`, `stage1_mbr.bin`), `ocbl-efi` (`ocbl_x64.efi`),
`ocbl-iso` (standalone dual-firmware test ISO via
`ocbl/build_test_iso.py` — the script is a hard prerequisite, B1
live-catch), `ocbl-run-bios`, `ocbl-run-uefi`, `ocbl-scan-disks`
(11-image fleet via `ocbl/make_scan_disks.py`; the stamp covers ALL
sibling images — B5-7 live-catch), `ocbl-run-scan` (boots the fleet:
2 IDE disks, 3 virtio disks, FAT + bare images, 1 CD + the test ISO).

### 5.3 ocse.mk (OCSE install environment)

- `ocse-vendor-check` — fails if `ocse/kernel/types.h` is missing:
  "run `tools/vendor_ocse.sh` then `tools/ocse_edits.py`". Runs as a
  prerequisite of every `make ocse-elf`.
- `ocse-elf` → `build/ocse/ocse.elf` (single-stage link, `-Map=`
  emitted, `filter-out driver_block_disk_setup.c` — GRUB-era tooling;
  the filter line is repeated 10× verbatim, a known cosmetic artifact).
- `ocse-iso` → `bash tools/build_ocse_iso.sh`, which runs
  `tools/check_bss.py` on the ELF FIRST (see
  [guard-scripts](guard-scripts.md)).
- `ocse-clean`; own `-include $(OCSE_OBJ:.o=.d)`.
- `OCSE_CFLAGS` mirrors the kernel set + `-DOCSE_RELEASE=\"<ver>-ocse\"`
  + `-DOCSE_BUILD`.

### 5.4 arm64 / riscv build.mk (4 targets each)

`make ARCH=arm64|riscv` replaces the whole x86_64 body. Toolchain
`CROSS ?= aarch64-linux-gnu-` / `riscv64-linux-gnu-` (Debian
`gcc-aarch64-linux-gnu` / `gcc-riscv64-linux-gnu`, GCC ≥ 12). Sources:
`src/L0/core/arch/<arch>/{boot.S,kernel_main.c}` (bare-metal entry +
UART + heartbeat). `XCFLAGS`: freestanding +
`-mgeneral-regs-only` (arm64) / `-march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany` (riscv — no FP: M-mode has no `mstatus.FS`
programmed on raw `-kernel` boot), both with `-Wall -Wextra -Werror -O2 -g -std=gnu11 -DOC_RELEASE_VERSION -DOC_KERNEL_BUILD`. Targets:
`all`/`kernel` → `build/opencube-<arch>.elf` (+ readelf
Class/Machine/Entry echo), `run-bios` → `qemu-system-<arch> -machine virt [-cpu cortex-a57] -nographic -kernel`, `clean-arch`. Link:
`$(XCC) -T linker.ld -Wl,--build-id=none -Wl,--hash-style=sysv`.

## 6. -D macro table (all build-time definitions)

| Macro | Defined where | Enables |
|-------|---------------|---------|
| `OC_RELEASE_VERSION` | kernel/OCSE/arch CFLAGS (string) | kernel banner, `uname`, update-check version string |
| `OC_KERNEL_BUILD` | kernel/OCSE/arch CFLAGS (number) | monotonic build id compared numerically by the OTA check (BUG-0347) |
| `OCBL_RELEASE`, `OCBL_BUILD` | ocbl.mk, both flavors | OCBL banner/version |
| `OCBL_FLAVOR_BIOS` | ocbl.mk BIOS objects | selects the int-13h/VGA/E820/VBE backend implementations (`#ifdef` sites in `ocbl/common/ocbl_main.c`) |
| `OCBL_FLAVOR_UEFI` | ocbl.mk EFI objects | EFI backend of the same contracts |
| `OCSE_RELEASE`, `OCSE_BUILD` | ocse.mk | OCSE banner (release literal + `-ocse` suffix) |
| `OCBL_ELTORITO` / `OCBL_MBR` | NASM `-D` on `ocbl_bios_stage1.asm` | which 512-byte stage1 flavor to assemble |
| `OC_CONFIG_NVME` / `OC_CONFIG_AHCI` / `OC_CONFIG_VIRTIO_BLK` | generated `config_gen.h` (0/1) | hardware profile; `0` also `#define`s `driver_block_<x>_init(...)` to a no-op stub |
| `OC_VERSION_STAMP` | generated `version_gen.h` | recompile-trigger string |
| `OCIRE_STAMP` | generated `ocire_version_gen.h` | OCIRE recompile trigger |
| `RSA_DEBUG` | **not in any build file** — source-local `#ifdef` (6 sites in `ocos/services/crypto/src/svc_crypto_rsa.c`) | debug prints in the RSA path; enable by editing the `#define` at the top of that file |

There is **no** `NAP_SANITIZE`, `DEBUG`, or `OC_DEBUG` build macro
anywhere in the build files: the freestanding flags are identical for
all build types; host-side tests compile with their own gcc lines
(several use ASAN locally, documented in each `tests/host_*.c` header).

## 7. Per-file optimization overrides (9 × -Os, 1 × -O0)

All overrides use the same pattern:
`$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os|-O0 -c $< -o $@`.

| File | Level | Reason (from the Makefile notes) |
|------|-------|----------------------------------|
| `ocos/drivers/net/drv_net_wireless.c` | `-Os` | wireless/sd driver set — kernel image must stay inside the 0x400000 identity window; the linker ASSERT is byte-exact and the measured over-run at `-O2` was 8 KiB |
| `ocos/drivers/net/drv_net_iwlwifi.c` | `-Os` | same window discipline |
| `ocos/drivers/net/drv_net_rtlwifi.c` | `-Os` | same |
| `ocos/drivers/net/drv_net_ath.c` | `-Os` | same |
| `ocos/drivers/net/drv_net_brcm.c` | `-Os` | same |
| `ocos/drivers/storage/drv_storage_sdhci.c` | `-Os` | same |
| `ocos/drivers/misc/drv_misc_mjpeg.c` | `-Os` | codegen-sensitivity: `-O2` miscompiles a working path (segfault caught by the 4:4:4 host fixture); `-Os` is clean and the JPEG decoder is not performance-critical |
| `ocos/drivers/misc/drv_misc_camera.c` | `-Os` | same camera-set reason |
| `ocos/drivers/misc/drv_misc_camera_uvc.c` | `-Os` | same |
| `ocos/drivers/bus/drv_bus_acpi_aml.c` | `-O0` | AML interpreter runs once at boot; `-O2` codegen interacted badly with the pointer-heavy value model (boot crash in kfree with a wild pointer) while `-O0` parses the live QEMU DSDT cleanly |

Historical note (NAK cleanup): two `-Os` rules for a root-level `tui/`
directory never matched a real object and were deleted, not
path-updated — activating them now would change the codegen of files
that have always shipped as `-O2`.

## 8. Parallelism and the compile cache

- **Parallelism**: `make -j$(nproc)` (build.sh's `JOBS`, default
  `nproc`). Safety comes from per-object `@mkdir -p $(dir $@)` recipes,
  order-only prerequisites (`| $(BUILD)`, `| $(VERSION_GEN) $(CONFIG_GEN)`), and cmp-guarded generators that never thrash mtimes.
- **Compile cache** (`OC_CC := bash tools/oc_cc.sh $(CC)`): cache key =
  SHA-256 over (1) exact compiler identity (path + version string),
  (2) the full argument list (flags, `-D` values, include paths),
  (3) the source file hash, (4) the hash of every header the source
  depends on (from the compiler's own `-MM` output — content-based, so
  a touched-but-unchanged header still hits and an
  unchanged-mtime-but-changed header still misses). Hits restore the
  `.o` AND the `.d`; misses compile and store both. Hits are
  byte-identical, so `reproducible-check` stays valid with the cache
  on. Controls: `OC_CACHE_DIR` (relocate), `OC_CACHE_OFF` (bypass);
  counters land in `$(OC_CACHE_DIR)/STATS` (read by `cache-stats`).
- **Incremental integrity**: `-include` of all `.d` files (kernel,
  OCIRE, OCSE each include their own), the `version_gen.h` recompile
  trigger, and the `userprogs_data.h` staleness gate together close the
  three historical stale-artifact classes (BUG-025, WP-10-tui-qa,
  WP06g1 catch-2).

## 9. Generated-but-committed inputs

These files are regenerated by make rules AND committed, so a fresh
wildcard build always sees them: `ocos/kernel/userprogs_data.h`,
`ocos/kernel/core_lib_i18n_data.c`,
`ocos/kernel/generated/version_gen.h` / `config_gen.h`,
`ocos/kernel/generated/grub_boot_data.c` (stage-2 only, not in
`KERNEL_DIRS`, transient), `ocire/generated/ocire_cn_font_data.c` /
`ocire_version_gen.h`,
`ocos/services/display/src/svc_display_pinyin_phrase_data.c` (+ `.h`),
`ocse/chinese/chinese_font_data.c`. Never hand-edit them; see
[tool-inventory](tool-inventory.md) for the generator per file.

## 10. Quick reference

```
make all                     # userprogs + kernel + ISO + src zip + OTA
make kernel | iso | src-zip | ota
make test                    # host unit harnesses
make run-bios | run-uefi | run-bios-gui | run-uefi-gui | run-*-persist
make shot-bios | shot-uefi
make reproducible-check
make defconfig | menuconfig
make cache-stats | cache-clear
make userprogs | userprogs-check
make l0-i18n | api-docs
make ocire-kernel | ocire-iso | ocire-test | ocire-disk | ocire-shell-test
make ocbl-bios | ocbl-efi | ocbl-iso | ocbl-run-scan
make ocse-elf | ocse-iso
make ARCH=arm64 | ARCH=riscv
```
