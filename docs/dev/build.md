<!-- SPDX-License-Identifier: Apache-2.0 -->
# Building and packaging (developer reference)

The developer view of the build system. For the user-level guide
see [BUILDING.md](../root/BUILDING.md); for toolchain setup see
[environment.md](environment.md). Facts below are from the
top-level `Makefile` (740 lines), `tools/build.sh` and the
component `.mk` files.

## 1. Targets you will actually use

```
make -j$(nproc) kernel     # fast inner loop: ELF only (default goal)
make iso                   # + bootable dual-firmware ISO
make all                   # userprogs + kernel + ISO + src zip + OTA
make userprogs             # regenerate the embedded program registry
make userprogs-check       # staleness gate (wired into make dist)
make test                  # host unit tests (no QEMU needed)
make run-bios | run-uefi | run-bios-gui | run-uefi-gui
make run-bios-persist      # no -snapshot: /etc edits persist in etc.img
make shot-bios | shot-uefi # VNC screendump -> PNG
make reproducible-check    # two clean builds -> byte-identical ELF
make defconfig | menuconfig
make cache-stats | cache-clear
make l0-i18n | api-docs
make ocire-kernel | ocbl-bios | ocbl-efi | ocse-elf   # sibling kernels
make ARCH=arm64 | ARCH=riscv                            # bring-up shells
make clean
```

## 2. Makefile variables (including the hidden ones)

| Variable | Default / derivation | Meaning |
|---|---|---|
| `OC_TOOLS` | `/usr` | toolchain root for relocated (dpkg -x) trees; autodetected by `tools/build.sh` probing `/usr`, `/home/z/tools/grub`, `$HOME/tools/grub` |
| `OVMF_FD` | `$(OC_TOOLS)/share/ovmf/OVMF.fd` | UEFI firmware for the `run-uefi*` targets |
| `QEMU_DATADIR` | `$(OC_TOOLS)/share/qemu` | QEMU `-L` path |
| `SEABIOS_DIR` | `$(OC_TOOLS)/share/seabios` | BIOS firmware |
| `OC_RELEASE_VERSION` | fixed literal (e.g. `3.0.0-NAK`) | kernel banner / uname / update-check string; `?=` so a command-line override wins - the E2E update test builds with e.g. `make OC_RELEASE_VERSION=WP-10c-test1` |
| `OC_KERNEL_BUILD_OFFSET` | `21` | alignment offset over `git rev-list --count HEAD` (keeps the numeric OTA comparison monotonic above published ids 278/321/414/416) |
| `OC_KERNEL_BUILD` | `expr git rev-list --count HEAD + 21` | monotonic build id (BUG-0347); override `make OC_KERNEL_BUILD=42` |
| `OC_CACHE_DIR` | `$(HOME)/.cache/opencube-cc` | compile-cache root (`oc_cc.sh`); `OC_CACHE_OFF` bypasses |
| `CONFIG_NVME` / `CONFIG_AHCI` / `CONFIG_VIRTIO_BLK` | `y` (from `.config` if present) | hardware profile knobs; `=n` filters the driver `.c` out of `KERNEL_C` and stubs its init |
| `ARCH` | `x86_64` (also `arm64`, `riscv`) | dispatches to the arch `build.mk` instead of the whole x86_64 body |
| `CC` / `LD` / `NASM` / `QEMU` | `gcc` / `ld` / `nasm` / `qemu-system-x86_64` | tool selection (PATH-resolved) |
| `KERNEL_DIRS` | kernel + arch + services + drivers wildcards | source wildcard roots |
| `KERNEL_INC` | `-I` for every `KERNEL_DIRS` + `tools` + `services/*/interface` + `kernel/generated` + `include` | multi-`-I` plain-basename include strategy |
| `LDFLAGS` | `-n -nostdlib -T ocos/kernel/arch/x86_64/linker.ld -z max-page-size=0x1000 -z noexecstack` | kernel link |
| `ETC_IMG` | `build/etc.img` | 32 MiB FAT32 `/etc` volume (mformat + mcopy of `etc/opencube.conf`) |
| `SRC_ZIP` / `OTA_PKG` | `dist/OpenCubeOS-src-<ver>.zip` / `dist/OpenCubeOS-<ver>-update.tar.gz` | release artifacts |

Environment variables honored by tools: `OC_TOOLS`,
`OC_RELEASE_VERSION`/`OC_KERNEL_BUILD` (make override),
`OC_CACHE_DIR`/`OC_CACHE_OFF`, `ARCH`/`CROSS`, `JOBS` (build.sh,
default nproc), `OVMF_FD`/`QEMU_DATADIR`/`SEABIOS_DIR`,
`OC_PROMPT` (qemu_runner prompt regex, default `(?:ush>|oc>)\s`),
`OC_CMD_TIMEOUT` (default 300), `OC_USB_SERIAL_ECHO`,
`OC_ETC_IMG` (pre-seeded /etc for the host-key fast path),
`LD_LIBRARY_PATH` (relocated QEMU libs), `SOURCE_DATE_EPOCH`.

## 3. CFLAGS and the -D macro table

Kernel CFLAGS (x86_64):

```
-ffreestanding -fno-stack-protector -fno-pie -fno-pic
-mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel
-fno-asynchronous-unwind-tables -Wall -Wextra -Werror
-O2 -g -std=gnu11 $(KERNEL_INC)
-DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\"
-DOC_KERNEL_BUILD=$(OC_KERNEL_BUILD)
-MMD -MP
```

Zero-warning gate: `-Wall -Wextra -Werror` on every component
(kernel, OCIRE, OCBL, OCSE, agent) - this is the project's "lint".

Build-time `-D` macros:

| Macro | Defined where | Enables |
|---|---|---|
| `OC_RELEASE_VERSION` | kernel/OCSE/arch CFLAGS (string) | banner, uname, update-check version |
| `OC_KERNEL_BUILD` | kernel/OCSE/arch CFLAGS (number) | the numerically-compared build id |
| `OCBL_RELEASE`, `OCBL_BUILD` | ocbl.mk both flavors | OCBL banner/version |
| `OCBL_FLAVOR_BIOS` | ocbl.mk BIOS objects | selects int-13h/VGA/E820/VBE backends |
| `OCBL_FLAVOR_UEFI` | ocbl.mk EFI objects | EFI backends of the same contracts |
| `OCSE_RELEASE`, `OCSE_BUILD` | ocse.mk | OCSE banner (`<ver>-ocse`) |
| `OCBL_ELTORITO` / `OCBL_MBR` | NASM `-D` on `stage1.asm` | which 512-byte stage1 flavor to assemble |
| `OC_CONFIG_NVME` / `OC_CONFIG_AHCI` / `OC_CONFIG_VIRTIO_BLK` | generated `config_gen.h` (0/1) | hardware profile; `0` also stubs `driver_block_<x>_init()` |
| `OC_VERSION_STAMP` | generated `version_gen.h` | recompile-trigger string |
| `OCIRE_STAMP` | generated `ocire_version_gen.h` | OCIRE recompile trigger |
| `RSA_DEBUG` | source-local `#ifdef` in `svc_crypto_rsa.c` | debug prints in the RSA path - enable by editing the `#define` at the top of that file |

There is **no** `NAP_SANITIZE`, `DEBUG` or `OC_DEBUG` build macro
anywhere in the build files - debug behavior is per-tool (host
tests compile with their own gcc lines; several use ASAN locally).

## 4. The 0x400000 window and the per-file optimization overrides

The kernel image must end below **0x400000** - enforced by a
link-time ASSERT in `linker.ld`:

```
ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the 0x400000 identity-window boundary - move large buffers to kmalloc")
```

The reason: user address spaces remap the *virtual* window
0x400000-0x600000 to an empty PT (P3-1 security fix), so the
kernel can never dereference PMM frames handed out in that
*physical* range while running on a user CR3; PMM reserves those
512 frames forever, and the image end below 0x400000 keeps the
identity window unambiguous. Everything large (the 1024-entry klog
ring, the 32-conn TCP table, the pipes, the crash-dump buffer)
lives on the kernel heap for exactly this reason.

**9 per-file `-Os` overrides** (the generic rule is `-O2`; these
files filter `-O2` out and take `-Os`):

- `drv_net_wireless.c`, `drv_net_iwlwifi.c`, `drv_net_rtlwifi.c`,
  `drv_net_ath.c`, `drv_net_brcm.c` (wireless families)
- `drv_storage_sdhci.c`
- `drv_misc_mjpeg.c`, `drv_misc_camera.c`, `drv_misc_camera_uvc.c`

Why: measured overrun ~8 KiB at `-O2` - the overrides keep the
image inside the window (the linker ASSERT is byte-exact); the
camera set is additionally codegen-sensitive.

**1 per-file `-O0` override**:

- `drv_bus_acpi_aml.c` - the AML interpreter: `-O2` codegen
  crashed `kfree` with a wild pointer; `-O0` parses live QEMU DSDT
  cleanly. (AML is generated-code interpretation - optimization
  changes lifetimes the interpreter relies on.)

Historical note: two dead `-Os` rules for a root-level `tui/`
directory were deliberately deleted during the NAK cleanup - the
pattern to follow when path-moving per-file override rules.

## 5. The two-stage link and the cmp-guarded generators

```
KERNEL_OBJ + grub_boot_stub.o --ld--> build/opencube_base.elf (+strip)
embed_grub.py --> generated/grub_boot_data.c --> grub_boot_data.o
KERNEL_OBJ + grub_boot_data.o --ld--> build/opencube.elf (+strip)
```

- **Stage 1** links with strong zero-length `grub_boot_img` /
  `grub_core_img` symbols from `core_boot_grubstub.c`, so the
  kernel links before the real boot data exists.
- **Stage 2**: `tools/embed_grub.py` runs `grub-mkimage` with the
  modules the on-disk config needs (biosdisk, part_msdos/gpt, fat,
  multiboot2, search_fs_file, normal, configfile, echo, serial,
  test, ls) and emits the 512-byte boot.img + 164,355-byte
  core.img as C arrays; the stage-2 link replaces the stubs with
  the real data. That is how `install`/`abdisk`/`grub-install`
  write bootable disks with zero external files.
- **cmp-guards**: `version_gen.h` and `config_gen.h` are FORCE-run
  generators whose output is only rewritten when the content
  differs (`cmp` guard). `version_gen.h` carries
  `#define OC_VERSION_STAMP "<ver> build <build>"` which makes the
  `-D OC_RELEASE_VERSION` visible to the `-MMD -MP` dependency
  graph (WP-10-tui-qa fix: incremental builds used to keep a stale
  banner over new code). `config_gen.h` is an order-only
  prerequisite of every object so it exists before anything
  compiles. The same idempotent-emit (byte-compare) pattern guards
  `userprogs_data.h`, `core_lib_i18n_data.c` and the IME phrase
  tables.

## 6. The app embed chain

`make userprogs` (part of `make all`, guarded by
`userprogs-check` in `make dist`) runs `tools/gen_userprogs.py`:
discovers every `ocos/l1apps/<name>/` folder + flat legacy
program, compiles each freestanding (`-I ocos/interface` only),
links per image mode (ET_EXEC @0x400000 / PIE + PT_INTERP /
ld_so @0x10000000), gzips each ELF into `userprog_gz_<sym>[]`
(one gzip stream per app; ush stays raw), and emits
`ocos/kernel/userprogs_data.h` (the blobs + `OC_USERPROGS[]` +
`OC_USERPROGS_COUNT`). Nothing is hand-registered; a new folder IS
a new command. The staleness set includes `*.c`, `*.asm`,
`ocos/interface/*.h` and every `i18n/*.json` (the WP06g2 hint
sweep added the json after a live-catch).

## 7. The compile cache

`tools/oc_cc.sh` wraps every compile rule with a content-addressed
cache: key = SHA-256(compiler identity + full flag set + source
content hash + the content hashes of every header dependency from
`-MM`). Warm objects are byte-identical to cold ones, so
`make reproducible-check` stays valid; a stale cache entry is
impossible (any header edit changes the key). Manage with
`make cache-stats` / `make cache-clear`.

## 8. Packaging determinism (G13)

- **ELF**: no `__DATE__`/`__TIME__`/`__TIMESTAMP__` in the tree;
  fixed link order (wildcard expansion); `strip --strip-debug`;
  `SOURCE_DATE_EPOCH` honored when exported.
- **SRC zip** (`make src-zip`): `git archive` from tag `v<ver>`
  (HEAD fallback with a loud warning) - tree order, commit mtimes,
  index permissions.
- **OTA** (`make ota`): kernel + manifest (the manifest carries
  the payload sha256 over concatenated member bodies in archive
  order); the ISO is forced fresh first (v2.0.0 fix for
  stale-ISO+fresh-OTA).
- **FAT images**: `tools/fat_zero_dates.py` zeroes date/time
  fields (mformat/mcopy stamp wall clock).
- **ISO**: xorriso pins volume dates/node times/GPT GUID/ESP
  serial; `tools/iso_zero_ctime.py` rewrites Rock Ridge TF
  ctime/atime to epoch.
- **OCOS.TAR** (install payload): ustar, zero mtimes, uid/gid 0,
  sorted, NUL-padded headers.
- `make reproducible-check`: two clean builds, byte-compare the
  ELF, exit 1 on diff.

## 9. Multi-config and cross builds

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_NVME=n iso  # hardware profile: NVMe off
make CONFIG_AHCI=n CONFIG_VIRTIO_BLK=n iso
make ARCH=arm64         # PL011 bring-up shell ELF (aarch64-linux-gnu-)
make ARCH=riscv         # NS16550A bring-up shell ELF (riscv64-linux-gnu-)
```

Disabled configs compile the driver init calls to no-op stubs,
never to deleted code. Note `tools/oc_menuconfig.sh` still
advertises the removed `CONFIG_TUI`/`CONFIG_CHINESE` knobs in its
menu text (stale - the Makefile consumes only
NVME/AHCI/VIRTIO_BLK; the CJK pipeline is unconditional).

## 10. Scriptable API and release automation

`tools/build.sh` doubles as a function library:

```
source tools/build.sh
build_all; build_iso; build_src; build_ota
config_load; config_save; config_get; cache_lookup
cross_compile; cross_toolchain_setup
tools/build.sh api-selftest     # verifies the spec'd interfaces
```

The release SOP composes these instead of parsing make output -
see [../release/process.md](../release/process.md) and
`tools/build_release.sh <ver> [--deep]` (clean `make all` ->
`reproducible-check` -> BIOS+UEFI banner gates -> optional deep
gates -> docs/website regeneration; prints the three SHA256s;
refuses an existing tag or the wrong git identity).
