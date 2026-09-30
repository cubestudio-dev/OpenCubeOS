<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Repository Manifest

## Version
WP-09 (Final)

## Directory Structure

```
oc-os/                        ← repository root
├── boot/                     ← boot assembly (3 .S files)
├── kernel/                   ← kernel source (105 files: .c + .h + .S, incl. WP-09 crypto/ssh/sshd/tls)
├── userprogs/                ← user programs (.c/.asm/.ld — 23 files)
├── tools/                    ← build/test scripts (9 files, public)
├── docs/                     ← documentation (18 files, incl. verification/ logs)
├── build/                    ← build artifacts (opencube.elf + .o files) — gitignored
├── archive/                  ← archived old versions (see archive/README.md)
│   └── old-ldso/             ← Batch 2 assembly ld.so (only tracked subdir)
├── Makefile                  ← top-level build
├── linker.ld                ← kernel link script
├── grub.cfg                  ← GRUB boot config
├── LICENSE                   ← Apache 2.0 (201 lines, full text)
├── NOTICE                    ← copyright + third-party components
├── README.md                 ← project overview (WP-01..WP-09)
├── MANIFEST.md               ← this file
├── VERIFICATION_REPORT.md     ← WP-01..WP-09 verification report
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
# WP-09 canonical regression (one QEMU session, 18/18):
#   uname -a, run hello/fork_test/exec_test/pipe_test/signal_test/
#   select_test/mmap_test/dyn_hello/so_test/dlsym_test/pie_test/
#   reloc_test/p3_test, heaptest, l1test, crashlog
python3.13 tools/qemu_runner.py build/opencube.iso uname -a run hello ...

# DH modexp self-test (5/5)
#   dhtest

# SSH/TLS E2E (paramiko 5.0 required: python3.13 -m pip install paramiko)
python3.13 tools/sshd_test.py build/opencube.iso        # paramiko client -> kernel sshd
python3.13 tools/paramiko_sshd.py 2222 &                # then in kernel: ssh 10.0.2.2 2222 oc oc
python3.13 tools/https_test_server.py cert key dh 8443 & # then in kernel: wget https://10.0.2.2:8443/
```

All 18 regression items + dhtest 5/5 must PASS. Evidence archived in
docs/VERIFICATION_BATCH_B.md + docs/verification/.

## Line Count (WP-09)

- Source code (kernel + boot + userprogs, no docs): **46,014 lines**
- Verify with:
  ```bash
  git ls-files | grep -E '\.(c|h|S|asm|ld)$|^Makefile$|^grub\.cfg$|^linker\.ld$|^\.gitignore$' \
    | grep -v 'userprogs_data.h\|solib_data.h\|font_data.c' | xargs wc -l
  ```
