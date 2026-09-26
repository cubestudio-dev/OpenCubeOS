<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Repository Manifest

## Version
WP-08 (Final)

## Directory Structure

```
oc-os/                        ← repository root
├── boot/                     ← boot assembly (3 .S files)
├── kernel/                   ← kernel source (94 files: .c + .h + .S)
├── userprogs/                ← user programs (.c/.asm/.ld — 21 files)
├── tools/                    ← build/test scripts (5 files, public)
├── docs/                     ← documentation (15 files)
├── build/                    ← build artifacts (opencube.elf + .o files) — gitignored
├── archive/                  ← archived old versions (see archive/README.md)
│   └── old-ldso/             ← Batch 2 assembly ld.so (only tracked subdir)
├── Makefile                  ← top-level build
├── linker.ld                ← kernel link script
├── grub.cfg                  ← GRUB boot config
├── LICENSE                   ← Apache 2.0 (201 lines, full text)
├── NOTICE                    ← copyright + third-party components
├── README.md                 ← project overview (WP-01..WP-08)
├── MANIFEST.md               ← this file
├── VERIFICATION_REPORT.md     ← WP-01..WP-08 verification report
└── .gitignore                ← excludes build/, *.o, *.elf, *.iso, *.zip, releases/, etc.
```

**Note:** Binary releases (ISO + src zip) are hosted on
[GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases),
not stored in this repo. The `releases/` directory (if it exists locally)
is gitignored. Use the website or GitHub Releases page to download.

## How to Build

```bash
source source env.sh (toolchain setup)   # set up toolchain (gcc/nasm/xorriso/grub-mkimage/qemu)
make clean && make          # build kernel ELF → build/opencube.elf
make iso                    # build bootable ISO → build/opencube.iso
```

## How to Run

```bash
qemu-system-x86_64 \
  -m 256M -cdrom build/opencube.iso -boot d \
  -no-reboot -display none -serial stdio -monitor none \
  -vga std -snapshot
```

Shell prompt `oc>` appears after boot. Type `help` for commands.

## How to Test

```bash
# WP-08a regression (7 tests)
run hello / fork_test / exec_test / pipe_test / signal_test / select_test / mmap_test

# WP-08b dynamic linking (5 tests + ldd)
run dyn_hello / so_test / dlsym_test / pie_test / reloc_test
ldd so_test / ldd hello

# Automated test runner
python3 run_wp08a_tests.py build/opencube.iso
```

All 12 tests must PASS.

## Line Count (WP-08)

- Source code (no docs, no auto-gen): **26,018 lines**
- With docs: **30,062 lines**
- Verify with:
  ```bash
  git ls-files | grep -E '\.(c|h|S|asm|ld)$|^Makefile$|^grub\.cfg$|^linker\.ld$|^\.gitignore$' \
    | grep -v 'userprogs_data.h\|solib_data.h\|font_data.c' | xargs wc -l
  ```
