<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Repository Manifest

## Version
WP-10a (Storage Drivers)

## Directory Structure

```
oc-os/                        ← repository root
├── boot/                     ← boot assembly (3 .S files)
├── kernel/                   ← kernel source (132 files: .c + .h + .S, incl. WP-09 crypto/ssh/sshd/tls + WP-10a storage drivers)
├── userprogs/                ← user programs (.c/.asm/.ld — 23 files)
├── tools/                    ← build/test scripts (9 files, public)
├── docs/                     ← documentation (17 files)
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
# WP-09 canonical regression (one QEMU session, 18/18) — now run WITH the
# four WP-10a disks attached (see EXTENSIONS_WP10a.md for the QEMU args):
#   uname -a, run hello/fork_test/exec_test/pipe_test/signal_test/
#   select_test/mmap_test/dyn_hello/so_test/dlsym_test/pie_test/
#   reloc_test/p3_test, heaptest, l1test, crashlog
python3.13 tools/qemu_runner.py build/opencube.iso uname -a run hello ...

# WP-10a storage suite (each prints input/expect/actual/PASS|FAIL):
#   ahci_test, nvme_test, ata_dma_test, virtio_blk_test,
#   disk_rw_test <dev>, partition_test [dev], fs_mount_test <dev>,
#   real_hw_test        (NOT RUN under QEMU by design; full report on bare metal)
python3.13 tools/qemu_runner.py build/opencube.iso ahci_test nvme_test ...

# DH modexp self-test (5/5)
#   dhtest

# SSH/TLS E2E (paramiko 5.0 required: python3.13 -m pip install paramiko)
python3.13 tools/sshd_test.py build/opencube.iso        # paramiko client -> kernel sshd
python3.13 tools/paramiko_sshd.py 2222 &                # then in kernel: ssh 10.0.2.2 2222 oc oc
python3.13 tools/https_test_server.py cert key dh 8443 & # then in kernel: wget https://10.0.2.2:8443/
```

All 18 regression items + the eight WP-10a storage tests + dhtest 5/5
must PASS.

## Line Count (WP-10a)

- Source code (kernel + boot + userprogs, no docs): **56,019 lines**
  (includes the embedded-data headers; verify with:
  ```bash
  find kernel boot userprogs \( -name '*.c' -o -name '*.h' -o -name '*.S' \) \
    | xargs wc -l | tail -1
  ```)
