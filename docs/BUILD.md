# Open Cube OS — Build Guide (WP-13-build_system)

This document describes the engineering build system introduced in
WP-13: one-command builds on Linux and Windows (WSL2/MSYS2),
reproducible builds, ARM64/RISC-V cross-compilation, the compile
cache, multi-configuration builds and the CI pipeline.

Copyright 2026 cubestudio-dev <cubestudio@qq.com> · Apache 2.0.

---

## 1. Quick start

### Linux (Debian/Ubuntu)

```bash
./build.sh                 # full pipeline: kernel + ISO + SRC zip + OTA
./build.sh iso             # kernel + bootable hybrid BIOS+UEFI ISO only
./build.sh --install all   # apt-install missing deps first (needs root)
./build.sh check           # reproducibility check (two clean builds)
```

`build.sh` detects the build dependencies and tells you exactly what is
missing and how to install it. With no root access, use the relocated
toolchain route:

```bash
bash tools/install-toolchain.sh    # 117 packages, no root required
```

### Windows

```powershell
.\build.ps1                # detects WSL2 (recommended) or MSYS2/Git-Bash
.\build.ps1 iso
.\build.ps1 -Install       # apt-install deps inside WSL first
```

The kernel is a freestanding ELF built with the GNU toolchain, so the
actual build always runs in a POSIX environment; `build.ps1` finds one
and forwards to `tools/build.sh`. WSL2 is the reliable route (MSYS2 has
no xorriso/grub packages).

### Plain make

`make`, `build.sh` and `build.ps1` are the same entry point — all three
dispatch to the identical make targets:

| target | effect |
|---|---|
| `make` / `make kernel` | kernel ELF only (`build/opencube.elf`) |
| `make all` | kernel + hybrid BIOS+UEFI ISO + SRC zip + OTA package |
| `make iso` | kernel + bootable ISO (`build/opencube.iso`) |
| `make src-zip` | deterministic source zip (git archive) |
| `make ota` | OTA update package (tools/make_update_pkg.sh) |
| `make reproducible-check` | two clean builds, byte-compare the ELF |
| `make test` | host test suite (tests/host_*.c) |
| `make run-bios` / `make run-uefi` | boot the ISO in QEMU |
| `make clean` | remove build artifacts |

---

## 2. Dependencies

| tool | Debian package | purpose |
|---|---|---|
| gcc (≥ 12) | `gcc` | kernel + host tests |
| binutils | `binutils` | ld / strip |
| nasm | `nasm` | 32/64-bit boot assembly |
| grub-mkimage | `grub-common` | BIOS El Torito + EFI bootloader images |
| xorriso | `xorriso` | hybrid ISO assembly |
| mtools (mformat/mcopy) | `mtools` | ESP FAT image, /etc volume |
| python3 | `python3` | build generators (embed_grub.py, …) |
| zip | `zip` | dist packaging |
| qemu-system-x86 | `qemu-system-x86` | run-bios/run-uefi + E2E tests |
| OVMF | `ovmf` | UEFI boot (run-uefi) |
| SeaBIOS | `seabios` | BIOS firmware files |

One-liner:

```bash
sudo apt-get install gcc nasm xorriso mtools grub-common \
     grub-pc-bin grub-efi-amd64-bin ovmf qemu-system-x86 zip python3
```

For cross-compilation (section 5) add:

```bash
sudo apt-get install gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu
```

If the tools live in a relocated root (no root, `dpkg -x` layout),
export `OC_TOOLS=/path/to/toolchain-root` (with `usr/lib/grub` under
it); `build.sh` autodetects the known sandbox locations.

---

## 3. Incremental and cached builds

- **Timestamp incremental** (make): every C compile emits `-MMD -MP`
  dependency files; touching a header rebuilds exactly the affected
  objects. An unchanged tree compiles nothing on re-`make`.
- **Content-addressed cache** (WP-13): every C compile routes through
  `tools/oc_cc.sh`. The cache key is the SHA-256 over the compiler
  identity (path + version), the full flag list, the source content and
  the content of every header the source depends on (from the
  compiler's own `-MM` probe). Hits restore the `.o` AND the `.d`;
  misses compile for real and store both.

  ```bash
  make cache-stats        # hit rate + entry count
  make cache-clear        # wipe ~/.cache/opencube-cc
  OC_CACHE_OFF=1 make ... # bypass
  OC_CACHE_DIR=... make   # relocate
  ```

  Measured on the WP-13 tree: cold build 46.6 s → clean rebuild with a
  warm cache 14.4 s (186/186 hits, 3.2×), and the cache-warm ELF is
  byte-identical to the cold ELF, so `reproducible-check` passes with
  the cache enabled.

---

## 4. Reproducible builds

```bash
make reproducible-check
```

runs two full clean builds from the same tree and byte-compares the
kernel ELF (`sha256sum` + `cmp`). PASS output:

```
== reproducible-check: build 1/2 ==
a2a14aa1...  build/opencube.elf
== reproducible-check: build 2/2 (clean again) ==
reproducible-check: PASS - ELF byte-identical across two clean builds
```

Why it holds (all verified during WP-13):

- the tree contains **no `__DATE__` / `__TIME__` / `__TIMESTAMP__`**
  macros (grep-checked);
- `grub-mkimage` output is byte-stable for identical inputs (verified
  by hashing two runs);
- gcc/ld are deterministic for identical inputs + flags;
- the link order is fixed by the wildcard expansion on the same
  filesystem;
- `strip --strip-debug` removes the path-bearing sections;
- the generated `version_gen.h` / `config_gen.h` are cmp-guarded, so
  unchanged values do not thrash mtimes.

`SOURCE_DATE_EPOCH` is honoured when the caller exports it (forwarded
to sub-tools that embed timestamps); the ELF itself does not need it
because nothing in it is time-derived. The SRC zip (below) is
deterministic by construction.

**Deterministic source zip** (`make src-zip`, part of `make all`):
`git archive` packs the tracked files in tree order with commit-time
mtimes and index permissions — two builds of the same commit produce
byte-identical zips (verified with `cmp`). Heavy non-source trees
(website downloads, releases, build artifacts) are excluded by
pathspec, the same exclusion set the release chain uses.

The OTA package embeds the build-day UTC date in its manifest
(`"time": $(date -u +%F)`), so it is reproducible within a day; the
ELF — the acceptance criterion — is fully byte-reproducible.

---

## 5. Cross-compilation (ARM64, RISC-V)

WP-13 adds the cross-compile scaffolding with real bare-metal
bring-up shells for both architectures (full per-arch kernels are
later work packages):

```bash
make ARCH=arm64           # build/opencube-arm64.elf   (aarch64-linux-gnu-)
make ARCH=riscv           # build/opencube-riscv.elf  (riscv64-linux-gnu-)
make ARCH=arm64 run-bios  # boot it under qemu-system-aarch64 -M virt
make ARCH=riscv run-bios  # boot it under qemu-system-riscv64 -M virt -bios none
make ARCH=arm64 CROSS=aarch64-none-elf-   # custom toolchain prefix
```

What each shell actually does (no stubs — real MMIO):

| | arm64 | riscv |
|---|---|---|
| entry | `0x40000000`, EL1, MMU off | `0x80000000`, M-mode, MMU off |
| files | `arch/arm64/boot.S` (hart park, bss zero, stack), `kernel_main.c`, `linker.ld`, `build.mk` | `arch/riscv/…` same set |
| UART | PL011 @ `0x0900_0000` (real divisor config for 115200 8N1) | NS16550A @ `0x1000_0000` (divisor latch programming) |
| time | ARM generic timer (CNTPCT/CNTFRQ) | CLINT mtime @ `0x0200_bff8` (10 MHz) |
| output | banner + dtb pointer + timer freq + 1 s heartbeat | banner + heartbeat |

Verified live in QEMU (not just linked): both print their banner and a
monotonically increasing `alive <ms>` heartbeat. Unsupported ARCH
values fail with an actionable error.

Toolchains: `apt-get install gcc-aarch64-linux-gnu
gcc-riscv64-linux-gnu` (or any GCC ≥ 12 with those targets; the CROSS
prefix is overridable).

---

## 6. Multi-configuration builds

Five options control what gets compiled in:

| option | controls | off ⇒ |
|---|---|---|
| `CONFIG_TUI` | the native TUI desktop + tui* commands (9) | no tui/ objects |
| `CONFIG_CHINESE` | Chinese renderer + IME + chinese* commands (8); forces `CONFIG_TUI=n` when off | no chinese/ objects, ASCII-equivalent stubs |
| `CONFIG_NVME` | NVMe block driver | driver not compiled/probed |
| `CONFIG_AHCI` | SATA AHCI block driver | ditto |
| `CONFIG_VIRTIO_BLK` | virtio-blk driver | ditto |

Three ways to set them:

```bash
make defconfig                 # write .config with all options y
make menuconfig                # interactive editor (tools/oc_menuconfig.sh)
make CONFIG_TUI=n iso          # one-shot command-line override
```

`.config` persists across builds (it is a make fragment the Makefile
includes); command-line overrides win for that invocation. `.config`
is gitignored — it is a local build preference.

The off-path is **real code, not stubs**: with `CONFIG_CHINESE=n` the
shell write path routes through `screen_console_puts` (the stock byte
console) via generated macros in `kernel/generated/config_gen.h`, so
the system still boots and the shell still works — verified live:

```
$ make CONFIG_TUI=n CONFIG_CHINESE=n iso && qemu boot …
[00:00:02.240] shell command system (216 commands)............... OK
```

216 = 233 − 17 (9 tui + 8 chinese commands). The full configuration
registers all 233 commands (regression-asserted). The TUI crash
containment window (`__tui_text_start/end`) becomes an empty range
and every ring-0 fault goes straight to the unified WP-11 panic path.

---

## 7. CI/CD (GitHub Actions)

`.github/workflows/build.yml` runs on every push to `main` and every
PR:

1. install dependencies (apt),
2. kernel build with a **zero-errors-zero-warnings gate**,
3. hybrid BIOS+UEFI ISO,
4. `make reproducible-check` (two clean builds, byte-compare),
5. QEMU regression suite (`tools/run_fix4_regression.py`, 28 checks),
6. `make cache-stats` (the content cache is restored via
   `actions/cache` keyed on the commit SHA — entries are content
   hashes, so any hit is always valid),
7. `make all` packaging,
8. artifact upload (ISO + SRC zip + OTA, 14-day retention).

Branch protection: the `ci` check is required for merges to `main`
(configured in the repository settings).

---

## 8. Troubleshooting

| symptom | fix |
|---|---|
| `missing separator (did you mean TAB)` | recipe lines must start with a real TAB — do not convert Makefile indentation |
| `grub-mkimage: Prefix not specified` | you called it manually; the Makefile passes `-p /boot/grub` |
| `qemu: could not load PC BIOS 'bios-256k.bin'` | relocated toolchain: point QEMU at its data dir with `-L $OC_TOOLS/usr/share/qemu` |
| `make ARCH=mips` error | only `x86_64`, `arm64`, `riscv` are supported |
| cache behaves oddly after editing a header | `make cache-clear` (the WP-13 key scheme covers headers via `-MM`, but a cleared cache is always a valid state) |

---

## 9. The persistent /etc volume (build/etc.img) — rebuilt per test run, never committed

The SSH and HTTPS end-to-end suites attach a persistent `/etc` volume to
the QEMU guest as a second IDE drive:

```sh
-drive if=ide,format=raw,file=build/etc.img
```

- **32 MiB FAT32 image, volume label `OCS_ETC`.** The kernel mounts it
  as `/etc` at boot (WP-09-fix5 `lib_config_init()`); without it the
  kernel falls back to a ramfs `/etc`.
- Holds `/etc/opencube.conf` plus the per-installation SSH material
  (BUG-0075 trust model): `ssh_host_key`, `ssh_client_key`,
  `ssh_authorized_keys`.

It is NOT in git: `build/` is a runtime tree and `etc.img` carries
private key material that must stay **per-installation** (committing it
would ship one shared host key to every checkout — exactly what the
p1fix2 BUG-0075 fix removed from the kernel). Recreate it locally with
`make build/etc.img` (or the equivalent mformat/mcopy lines in the
Makefile) and inject the SSH material with `tools/host_keygen_etc.py`.

## 10. Screenshots

```sh
make shot-bios   # -> build/shot-bios.png  (800x600)
make shot-uefi   # -> build/shot-uefi.png  (800x600)
```

These use `tools/qemu_shot_vnc.py`, which boots QEMU with a VNC display,
waits for the kernel to finish booting, then sends `screendump` to
QEMU's monitor socket; the PPM is converted to PNG via PIL.

## 11. Layout of the build system

```
Makefile                     targets, config dispatch, generated headers
build.sh / build.ps1         unified one-command entries (Linux / Windows)
tools/oc_cc.sh               content-addressed compile cache wrapper
tools/oc_menuconfig.sh       interactive .config editor
tools/build_iso.sh           hybrid BIOS+UEFI ISO assembly
tools/make_update_pkg.sh     OTA package
tools/install-toolchain.sh   no-root relocated toolchain (117 packages)
arch/arm64/                  ARM64 bring-up shell + cross build rules
arch/riscv/                  RISC-V bring-up shell + cross build rules
.github/workflows/build.yml  CI pipeline
kernel/generated/            version_gen.h + config_gen.h (generated)
```
