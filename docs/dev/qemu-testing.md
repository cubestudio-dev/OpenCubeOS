<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU testing

How the project actually tests in QEMU - the same recipes the
release gates use, plus the QEMU-specific behaviors (errata,
firmware paths, input injection) that cost hours if you learn them
the hard way. Evidence-extraction methodology is in
[debug-handbook.md](debug-handbook.md); the suite mechanics are in
[e2e-suites.md](e2e-suites.md).

## 1. Interactive boot

```
make run-bios          # default: 512M, e1000 + user net, serial on stdio
make run-uefi          # OVMF via a pflash copy of OVMF_FD
make run-bios-gui      # GTK window (PS/2 keyboard receives scancodes)
make run-uefi-gui
make run-bios-persist  # -snapshot OFF: /etc edits persist in build/etc.img
make shot-bios         # VNC + monitor screendump -> PNG (tools/qemu_shot_vnc.py)
```

The `run-*` targets build the ISO and a fresh 32 MiB FAT32 `/etc`
volume (`build/etc.img`) so persistence tests start clean; the
persist variants deliberately keep your edits.

## 2. OVMF paths

- Default: `OVMF_FD = $(OC_TOOLS)/share/ovmf/OVMF.fd` - derived
  from the toolchain root (`/usr` on a system install,
  `<tree>/share/ovmf/OVMF.fd` in the no-root sandbox from
  `tools/install-toolchain.sh`).
- The `run-uefi` targets copy `OVMF.fd` to a pflash *work* file
  (`-drive if=pflash,...OVMF_WORK.fd`) so the firmware vars image
  is writable but your pristine `OVMF.fd` is never modified.
- The release boot gates run OVMF through pflash CODE+VARS on a
  FRESH mktemp log per run (BUG #63 fake-green fix - appending to
  an old log made a dead boot look green).
- `QEMU_DATADIR`/`SEABIOS_DIR` follow `OC_TOOLS` the same way and
  feed QEMU `-L` and the BIOS lookup.

## 3. Deterministic boot-log testing

```
python3 tools/run_fix4_regression.py build/opencube.iso
```

pexpect over `-serial mon:stdio` (latin-1, NUL-stripped,
ANSI-stripped, prompt `(?:ush>|oc>)\s`), then the fixed command
battery with per-command-section assertions. Boot to the `ush>`
prompt takes ~2 s of wall time; the full 31-check battery runs in
well under a minute. The full check list:
[e2e-suites.md](e2e-suites.md) section 2.

## 4. QEMU-specific behaviors and errata

The behaviors that differ from real hardware (all verified in
source or with in-kernel traces):

- **AHCI/NVMe bounce buffers are a QEMU-visible contract**: AHCI
  does READ/WRITE DMA EXT through a per-port 4 KiB page-aligned
  bounce buffer split into chunks of at most 8 sectors (one PRDT
  entry per chunk); NVMe uses one page bounce buffer with 64-bit
  PRPs. If you touch the storage drivers, keep the bounce
  discipline - QEMU's DMA engine is strict about PRDT alignment in
  ways some real controllers are not.
- **PIIX3 prog-if quirk**: the ATA BMDMA probe matches IDE class
  0x0101 with ANY prog-if (mask 0xFFFF00) because QEMU's PIIX3
  reports 0x80 while real chips report 0x00/0x8A/... - a
  base-class-only probe latches onto IDE controllers wrongly.
- **TCO presence**: the Intel TCO watchdog lives on the LPC bridge
  of the **q35** machine (ICH9-LPC, PCI 00:1f.0); the default
  `pc` machine (PIIX3) has none - the probe honestly reports
  "none". Watchdog E2E scenarios boot q35 with `-no-reboot`.
- **HDA + 44100 freezes the VM**: the driver programs it correctly
  on real hardware, but under QEMU the whole VM freezes - so
  `sample_rate_test` reports the caps bit only for that
  combination. This is reported honestly, not papered over.
- **Serial RX bytes drop after an HMP screendump**: QEMU
  intermittently loses serial input right after a monitor
  screendump (verified with an in-kernel RX trace). Functional
  test rounds therefore never take screenshots; flapping checks
  re-run isolated (`wp10_native_manual.py`).
- **xHCI DMA above 4 GiB is refused** at probe and per-transfer -
  check your `-m` size if an xHCI device "vanishes".
- **Virtio-blk legacy capacity** is read at config offset 0x14
  (the fix for the old "capacity = 0" bug).
- **e1000 resume**: `net_e1000_resume()` replays legacy e1000
  register programming after ACPI S3 (QEMU resets the NIC on
  suspend).
- **Display**: the MB2 header requests 800x600x32; UEFI-GOP hands
  off RGBX while BIOS-VBE hands off BGRX - `screen_fb_rgb` derives
  masks from the GRUB tag, so both work.
- **Warm resets** keep DRAM: the boot-state islands 0x98000/
  0x9B000 survive QEMU warm resets (E820-reserved, empirically
  proven); cold boots clear them and every reader validates the
  magic first.

## 5. Driving the OCBL boot menu (sendkey)

The OCBL boot menu (the in-tree bootloader, 10 s countdown) reads
PS/2 keys - it exists before the kernel and before any serial
console. In headless QEMU you drive it through the HMP monitor:

- Start QEMU with `-monitor stdio` (or the unix-socket monitor the
  test rigs use) alongside `-serial stdio`.
- `sendkey down` / `sendkey up` move the highlight;
  `sendkey ret` boots the entry; the countdown boots the default
  after 10 s.
- Picking the OCIRE entry in the menu writes a USER_REQUEST record
  (the exact distinction OCIRE reports: "requested by the user" vs
  a judged failure with repair advice) - the interactive sendkey
  sessions in the WP04/WP07a evidence transcripts drive exactly
  this.
- The same `sendkey` channel is how the suites deliver keystrokes
  a serial link cannot carry (arrows, Home/End) into the *guest*
  (line-editor and editor tests); pointer input goes through QMP
  `input-send-event` (HMP mouse_move is dead on `-display none` +
  QEMU 10).

## 6. Screenshots

```
make shot-bios     # boot screenshot to build/shot-bios.png (VNC + PIL)
make shot-uefi
```

The framebuffer assertion (regression check 1) requires the PPM
screendump to show >= 2 colors and a measurable foreground ratio -
a serial-only gate once passed on a dead panel, so screenshots are
the *only* accepted proof of a live display, and functional rounds
avoid them because of the serial-RX errata above.

## 7. QEMU devices that map to drivers

| QEMU flag | Kernel driver |
|-----------|---------------|
| `-device e1000` (default net) | e1000 NIC (built-in path) |
| `-device e1000e` etc. | WP-10b framework families |
| `-device virtio-blk-pci` | virtio-blk (`vda`) |
| `-device nvme` | NVMe (`nvme0`) |
| `-device ahci` + SATA disk | AHCI (`sda`...) |
| IDE disks | ATA PIO + Bus-Master DMA (`hda`..) |
| `-device usb-storage` through xHCI | USB MSC (registers block devices) |
| `-device AC97` / `-device intel-hda` + `-device hda-output` | sound |
| `-device virtio-sound-pci` | virtio-snd |
| q35 machine | Intel TCO watchdog |
| `-bios OVMF.fd` / pflash | UEFI boot path |
| LSI 53C895A (`-device lsi`) | SCSI (camera-family `scsi` op) |
| `-acpitable file=wdat.bin` | ACPI WDAT watchdog (tools/make_wdat_table.py) |

## 8. Speed

- `-display none -serial mon:stdio` for every automated run.
- Boot to the `ush>` prompt takes ~2 s of wall time in the
  regression environment.
- The compile cache (`tools/oc_cc.sh`) keeps the *build* inner
  loop warm; see [build.md](build.md) section 7.
- `-icount` gives timing stability for network tests; the
  reproducible build means a crash reproduced twice is the same
  bytes both times.
