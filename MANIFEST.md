# Open Cube OS — Repository Manifest

## Version
WP-08 (Final)

## Directory Structure

```
oc-os/                        ← repository root
├── boot/                     ← boot assembly (3 .S files)
├── kernel/                   ← kernel source (42 .c + 46 .h + 4 .S = 92 files)
├── userprogs/                ← user programs (.c/.asm/.ld/.elf/.o/.so)
├── tools/                    ← build/test scripts (10 files)
├── docs/                     ← documentation (EXTENSIONS_WP01..WP08cd.md + BUILD.md + STATUS.md + WORKFLOW.md + HANDOFF.md + worklog.md + MANIFEST.txt)
├── build/                    ← build artifacts (opencube.elf + opencube.iso + .o files) — gitignored
├── web/                      ← Next.js web page source (page.tsx + layout.tsx)
│   └── app/
├── releases/                 ← release artifacts
│   ├── WP08/                 ← WP-08 release
│   │   ├── opencube-wp08.iso
│   │   └── SHA256SUMS
│   └── WP08b/                ← WP-08b archived release (ISO + src zip)
├── archive/                  ← archived old versions (see archive/README.md)
│   ├── old-src-tree/         ← earlier-era source tree (contains dynlink.c/h)
│   ├── WP06/                 ← WP-06 source zip
│   ├── old-ldso/             ← Batch 2 assembly ld.so
│   └── old-tests/            ← old dyn_test.elf
├── Makefile                  ← top-level build
├── linker.ld                ← kernel link script
├── grub.cfg                  ← GRUB boot config
├── LICENSE                   ← Apache 2.0
├── README.md                 ← project overview
├── VERIFICATION_REPORT.md    ← WP-01..08 verification report
├── .gitignore                ← excludes build/, *.o, node_modules/, __pycache__/
└── run_wp08a_tests.py        ← QEMU automated test runner
```

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

- Source code (no docs, no auto-gen): 26,xxx lines
- With docs: 31,xxx lines
