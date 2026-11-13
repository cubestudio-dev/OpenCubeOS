<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Repository Manifest

## Version

3.0.0-WP06g3 (development preview: the OCBL bootloader + a pure L0
service kernel + 275 ring-3 programs under src/L1/ + the OCSE install
environment with the GUI install wizard). Stable line: 2.2.0.

## Directory Structure

```
opencube/                     ← repository root
├── src/
│   ├── L0/                   ← the pure service kernel (no shell, no TUI, no IME)
│   │   ├── boot/             ← composition root (7 files): boot.S, long-mode
│   │   │                       entry, kmain.c, l1_launcher.c (the ush default
│   │   │                       entry), l1_selftest.c, the multiboot2 header/stub
│   │   ├── core/             ← kernel mechanisms (62 files): scheduling, sync,
│   │   │                       timers, usermode, the OC_*_DECL registry, crash,
│   │   │                       mem/ (PMM/VMM/heap), lib/, console/, arch/,
│   │   │                       generated/ (build stamps)
│   │   ├── drivers/          ← 15 hardware families (125 files): acpi block
│   │   │                       bluetooth camera i2c input misc nic pci scsi
│   │   │                       sd snd time usb wireless
│   │   └── services/         ← 24 root-service folders (176 files): the folder
│   │                           is the L1SVC facet - audio block camera crash
│   │                           crypto diag display encoding font fs health
│   │                           ipc klog l1 loader net ocembed power selfhost
│   │                           sysapi time tui usb watchdog (+ time/crypto
│   │                           bodies); sysapi/ holds the frozen ABI headers
│   │                           (core_syscall.h 54 + l1svc_defs.h 20/190)
│   └── L1/                   ← the ring-3 world: 275 programs (242 app folders
│                               + 8 flat .c + 25 flat .asm) - ush (the default
│                               boot entry), the TUI desktop family, the editor,
│                               the Chinese console + pinyin IME, one command
│                               per app; plus _shared/ (the common L1 libs)
├── ocbl/                     ← OCBL, the in-tree bootloader: bios/ (512-byte
│                               stage 1, the 16-bit trampoline), efi/ (the UEFI
│                               entry), common/ (menu, disk scan, loader) - own
│                               build (ocbl.mk), NOT part of L0
├── ocire/                    ← the independent recovery kernel (shares no code
│                               with OCOS; own ISO; agent/, boot/, kernel/...)
├── ocse/                     ← the OCSE install-environment kernel (WP-06a/b:
│                               the vendored OCOS core + OCSECL + the wizard)
├── tools/                    ← build/test/generation scripts (43 .py/.sh,
│                               public): build.sh, build_iso.sh, gen_api_docs.py,
│                               update_website.sh, sync_wiki.sh, the e2e suites
├── docs/                     ← the documentation center (796 .md pages):
│                               manual/ (user + commands), dev/, arch/,
│                               drivers/, internals/, l1/, troubleshooting/,
│                               release/, root/ (README/ARCHITECTURE/INTERFACES/
│                               BUILDING/FAQ/CONTRIBUTING/KNOWN_ISSUES mirrors)
│                               + zh/ (the full Chinese mirror) + data/ (the
│                               generated stats/interfaces/commands JSON)
├── website/                  ← the site: static facade (index/architecture/
│                               download/about + assets/i18n/app/site-data) +
│                               docs-src/ (VitePress over docs/) + public/
│                               downloads/ (the mirrored release assets)
├── evidence/                 ← release/batch evidence (runtime captures,
│                               battery logs)
├── tests/                    ← host-side unit tests (the host_* suite)
├── etc/                      ← the default /etc content (opencube.conf)
├── dist/                     ← release assets staged by tools/build_release.sh
│                               (2.2.0 ISO/src/OTA; gitignored)
├── archive/                  ← archived old versions (see archive/README.md)
├── Makefile                  ← top-level build (kernel, l1apps, ocbl, iso)
├── build.ps1                 ← Windows host build (WSL2 / MSYS2)
├── LICENSE                   ← Apache 2.0 (full text)
├── NOTICE                    ← copyright + third-party components
├── README.md                 ← project overview (3.0.0-WP06g3)
├── MANIFEST.md               ← this file
└── .gitignore                ← excludes build/, *.o, *.elf, dist/ contents, etc.
```

**Note:** Binary releases (ISO + src zip) are hosted on
[GitHub Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)
and mirrored under `website/public/downloads/` (the gh-pages deploy
serves the same bytes). Use the website or GitHub Releases page to
download.

## How to Build

```bash
# toolchain (no-root sandbox): gcc + nasm + xorriso + mtools + OVMF
bash tools/install-toolchain.sh

make clean && make          # kernel + L1 apps → build/
make iso                    # bootable hybrid ISO → build/opencube.iso
```

## How to Run

```bash
qemu-system-x86_64 \
  -m 512 -cdrom build/opencube.iso -boot d \
  -netdev user,id=n1 -device e1000,netdev=n1 \
  -vga std
```

OCBL shows the boot menu (10 s countdown), boots OCOS, and the ring-3
`ush>` prompt appears after `L0 ready, waiting for L1`. Type `help`
for the built-ins; every other command is a registry app.

## How to Test

```bash
# the canonical battery matrix runs per work package - see the
# evidence/ tree and docs/dev/e2e-suites.md for the current set
# (b2..b6, T1..T7 style batteries; every command above is covered)
make test                   # host unit tests (tests/host_*.c)
python3 tools/run_*.py      # the QEMU e2e suites (per subsystem)
```

The release gate: the full regression matrix green + CI green
(`.github/workflows/build.yml`: build, ISO, packages, host tests,
reproducibility) before any tag.

## Line Count (3.0.0-WP06g3 generator caliber)

- Source code (src/L0/boot+core+drivers+services + src/L1, .c/.h/.S,
  generated data blobs excluded): **128,890 lines across 624 files**
  (verify with:
  ```bash
  find src/L0/core src/L0/drivers src/L0/services src/L0/boot src/L1 \
    \( -name '*.c' -o -name '*.h' -o -name '*.S' \) \
    | grep -v generated \
    | grep -vE 'chinese_font_data|pinyin_data|grub_boot_data|userprogs_data' \
    | xargs wc -l | tail -1
  ```
  )
- Commands: **285** = 274 ring-3 registry apps + 11 ush built-ins
- L1SVC surface: **20 categories / 190 numbered entries**
- Syscalls: **54**
