<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - Try It Guide

Every hardware feature of the kernel, with the exact QEMU command to
attach the device and the exact in-guest commands to exercise it -
including the output you should see.  If your output differs, the last
section lists the likely causes.

This guide covers **storage, network, sound, the chipset input
domains (WP-10-input_chipset), and the time sources
(WP-10-timer_rtc)**.  For the in-system
update (OTA) flow there is a dedicated step-by-step guide:
**docs/UPDATE-HOWTO.md**.

Notes that apply everywhere:

- The `tools/qemu_runner.py` script used below boots the ISO and types
  the guest commands for you (one session, serial console).  You can
  also boot `make run-bios` and type the commands yourself.
- Success for `mkdir`, `touch`, `rm`, `cp`, `mv` is silent (Unix
  convention).  Failures always print a reason.
- `run <program>` starts a user program; its output can appear after
  the `oc>` prompt because the program runs concurrently with the
  shell.  The output always arrives before the next command you type
  finishes.

## 0. What you need

| item                    | where                                   |
|-------------------------|-----------------------------------------|
| Open Cube OS ISO        | GitHub Releases / website               |
| QEMU (6.0+)             | distro package                          |
| a blank raw disk image  | `dd if=/dev/zero of=disk.img bs=1M count=64` |

## 1. Storage (WP-10a)

Attach a blank disk on the IDE bus:

```sh
dd if=/dev/zero of=disk.img bs=1M count=64
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=disk.img
```

In the guest - format, mount, write, read, check, unmount:

```
oc> lsblk
Block devices:
  hda [ATA] 131072 sectors (65536 KiB) sector_size=512
oc> mkfs.fat32 hda
FAT32 formatted on hda
oc> fatmount hda /mnt
vfs: mounted fat32 at /mnt
fat32 mounted. Try: ls /mnt
oc> write /mnt/note.txt audit test file
wrote 15 bytes
oc> cat /mnt/note.txt
audit test file
oc> df
Filesystem     Mount     Device    Use
ramfs         /        -        7 nodes, 485 bytes
fat32         /mnt     hda      129020 free clusters, 512 bytes/cluster
oc> umount /mnt
umounted /mnt
oc> fsck hda
fsck: FAT32 filesystem on hda
  ...
  fsck: PASS (basic checks OK)
```

`mkfs.exfat` and `mkfs.ext4` also work; `fsck` reports those volume
types honestly ("ext2/3/4 volume detected ... fsck supports FAT32
only") instead of misparsing them.

Other controllers - attach one of these instead of (or besides) the
IDE drive:

```sh
# AHCI (SATA)
-device ich9-ahci,id=ahci -drive if=none,id=da,file=disk.img,format=raw \
-device ide-hd,drive=da,bus=ahci.0
# NVMe
-device nvme,id=nv0,serial=oc -drive if=none,id=dn,file=disk.img,format=raw \
-device nvme-ns,drive=dn
# virtio-blk
-drive if=none,id=dv,file=disk.img,format=raw -device virtio-blk-pci,drive=dv
```

The disk shows up as `sda` (AHCI), `nvme0` (NVMe) or `vda` (virtio) in
`lsblk`; each has a self-test (`ahci_test`, `nvme_test`,
`virtio_blk_test`, `ata_dma_test`, `disk_rw_test <dev>`,
`fs_mount_test <dev>`, `partition_test`) that prints input / expected /
actual and a verdict.

## 2. Network (WP-10b)

The default `qemu_runner.py` session already has an e1000 NIC.  For a
plain QEMU run add:

```sh
-netdev user,id=n1 -device e1000,netdev=n1
```

In the guest - DHCP, interface, ping, DNS, routes:

```
oc> dhcp
DHCP success: IP=10.0.2.15
  mask=255.255.255.0
  gateway=10.0.2.2
  DNS=10.0.2.3
oc> ifconfig
e1000:
  HWaddr 52:54:00:12:34:56
  inet 10.0.2.15
  ...
  link: UP
oc> ping 10.0.2.2
PING 10.0.2.2 ...
  reply received
  reply received
  reply received
  reply received
oc> dns example.com
A: 104.20.23.154
oc> route
Routing table:
  0.0.0.0 mask=0.0.0.0 gw=10.0.2.2 (default)
```

Other driver families (each has a matching `*_test` self-test):

```sh
-device e1000e,netdev=n1    # then run: e1000e_test
-device igb,netdev=n1       # then run: igb_test
-device rtl8139,netdev=n1   # then run: rtl8139_test
```

(with `-netdev user,id=n1` in front of each).  Families without a QEMU
model (ixgbe, bcm57xx, ...) report `SKIPPED` with the reason - no fake
output is produced for absent devices.

## 3. Sound (WP-10c)

Attach an Intel HDA card (the most common real-world controller).  QEMU
10 needs an explicit `audiodev`; the `wav` backend writes what the
kernel plays into a WAV file so you can verify the PCM path on the
host:

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0
```

In the guest - list cards, status, volume, play:

```
oc> sound
sound devices:
  0: hda (hda) rate=48000 ch=2 vol=74 played=0B
oc> hda
Intel HDA: vid=0x8086 did=0x2668 at 00:04.0 codec=00 dac_nid=02 ...
oc> volume 60
volume: hda = 60
oc> volume
volume: hda = 60
oc> play
play: hda 440Hz 48000 frames @ 48000Hz
```

If you name a device wrong, the command tells you the available names:

```
oc> play usbaudio
play: unknown device 'usbaudio' (available: hda)
usage: play [device] [rate 4000-96000]
```

More cards in one machine (all five families):

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0 \
  -device AC97,audiodev=snd0 -device sb16,audiodev=snd0 \
  -device ES1370,audiodev=snd0 \
  -device piix3-usb-uhci -device usb-audio,audiodev=snd0
```

`sound` then lists five cards; `play <name>` and
`volume <name> 0-100` work per card (`hda`, `ac97`, `es1370`,
`usb-audio`, `sb16` - exactly the names `sound` shows).  Self-tests:
`hda_test`, `ac97_test`, `sb16_test`, `es1370_test`,
`usb_audio_test`, plus `audio_rw_test` (play/stop round-trip on every
card) and `sample_rate_test` (44.1/48 kHz).

Notes:
- QEMU 10 has **no** `virtio-snd-pci` model, so `virtio_snd_test`
  reports SKIPPED there; on real virtio hardware it runs.
- The QEMU-10 `intel-hda` model dead-loops when re-programmed to
  44.1 kHz; `sample_rate_test` verifies the capability bit instead
  (see docs/EXTENSIONS_WP10c.md).

## 4. Update / rollback (WP-10u)

Full step-by-step guide with expected output:
**docs/UPDATE-HOWTO.md**.  Short version — create the A/B disk **from
inside the OS** (rule-9 self-hosting; no host script needed):

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

```
oc> abdisk hda               # creates A/B layout + GRUB + slot A kernel
oc> update --status          # A/B disk: present (no reboot needed)
oc> ab_partition_test        # 7/7 PASS
oc> dhcp && config set update_url http://10.0.2.2:8008/update.json
oc> update                   # download, verify, install to slot B
oc> reboot                   # boots slot B automatically
oc> rollback                 # next boot back to slot A
```

After `reboot`, GRUB picks the slot from the flag files on p1
(`BOOT_SLOT=B`) and the update is confirmed once the system is fully up
(`slot B boot confirmed`).  Reboot **without the ISO** and the disk
boots on its own - `abdisk` installs the GRUB BIOS boot loader into the
MBR gap (boot.img -> LBA0, core.img -> LBA1..).

## 4b. Install the OS to a disk (single system)

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=disk.img
```

```
oc> install hda              # partition + FAT32 + kernel + GRUB
oc> sync                     # flush the disk cache
```

Reboot with the ISO removed (`-boot c`) and the machine boots Open Cube
OS straight from the disk.  `grub-install <dev>` writes only the boot
loader onto an already-partitioned disk (MBR gap, or a GPT BIOS boot
partition).  `abcfg` prints the grub.cfg files that abdisk/install
write.  Note: these commands install the **BIOS** boot path; boot the
installer from the ISO for UEFI machines (the ISO is BIOS+UEFI).

## 5. No hardware needed

These work in a bare QEMU session with no extra devices:

| try this                          | what you should see                     |
|-----------------------------------|-----------------------------------------|
| `help`                            | the 192-command list                    |
| `uname -a`                        | `Open Cube OS <version> x86_64`, where `<version>` is the build-time `git describe --tags` string (e.g. `Open Cube OS WP-10-AUDIT_P2-fix2b-7-g98a8dc7 x86_64`; Makefile `OC_RELEASE_VERSION`) |
| `mkdir /d` + `write /d/f hi` + `cat /d/f` | `hi`                            |
| `mem`, `heap`, `ps`, `sched`      | memory/task/scheduler stats             |
| `heaptest`, `memtest`, `vmtest`   | PASS                                    |
| `run hello`                       | `hello from userspace`                  |
| `run fork_test` / `run pipe_test` / `run signal_test` | ... PASS           |
| `cryptotest`                      | SHA-256 / AES / HMAC 3/3 PASS           |
| `crashlog`                        | last kernel exceptions (if any)         |

## 6. Chipset input domains (WP-10-input_chipset)

Five chipset domains, all exercisable in a bare QEMU session (no extra
devices needed): ACPI, PCI/PCIe, SMBus, I2C and the I2C-HID touchpad
pipeline.

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -no-reboot -display none -serial stdio -monitor none
```

| try this              | what you should see                                                                        |
|-----------------------|--------------------------------------------------------------------------------------------|
| `acpi`                | `RSDP: 0x...` + table list (RSDT/FACP/APIC/HPET/WAET), `FADT: rev 1 SCI=IRQ9 PM1a_CNT=0x0604 ...`, `MCFG: not present (legacy CF8/CFC only)` |
| `acpidev`             | `AML namespace: nodes=342 methods=102 devices=54` + `\_SB_.PCI0 ...` device paths          |
| `acpitest`            | `acpitest: PASS` (fixture table eval + `\_S5` resolve + 54-device namespace walk)          |
| `acpipow`             | sleep states `0x38 (S1..S5 bit i-1)` (S3 SLP_TYP=0x01, S4 0x02, S5 0x00), `SCI: IRQ 9`     |
| `pcibars`             | `PCI devices: 6` with per-BAR base/size (e.g. `00:02.0 BAR0: base=0xfd000000 size=0x01000000 mem prefetch`) |
| `pcie`                | `MMCONFIG: not active (MCFG absent) - legacy CF8/CFC only` + per-device capabilities       |
| `pcidump 0:1.3`       | config dump of the PIIX4 PM device (32 dwords, legacy CF8/CFC window)                      |
| `msitest`             | `LAPIC: present, id=0`, `self-IPI vector 0x50: fired 1 time(s)  PASS`, `msitest: PASS`     |
| `i2ctest`             | `controller: PIIX4 (8086:7113) base 0x0700 ...` + `i2ctest: PASS`                          |
| `i2cdetect`           | grid for `i2c-0 (piix4)` showing `50 51 52 53 54 55 56 57`                                 |
| `smbus`               | `SMBus devices: 8` with EEPROM first bytes at 0x50..0x57                                   |
| `smbustest`           | `smbustest: byte/word/block round-trips  PASS`                                             |
| `i2cget 0 0x50 0x00`  | `0x00` (EEPROM byte read-back)                                                             |
| `touchpad`            | binding status reported honestly (QEMU ships no I2C-HID model) + `reports decoded: 36  events queued total: 9` |
| `touchtest`           | `touchtest: descriptor parse + TAP/SCROLL/PINCH/SWIPE  PASS`                               |

The QEMU default machine offers neither PCIe (no MCFG) nor an I2C-HID
touchpad, so those paths report their absence honestly instead of
failing - see docs/KNOWN_ISSUES.md §11 and
docs/EXTENSIONS_WP10-input_chipset.md §6.

## 7. Time sources (WP-10-timer_rtc)

Four time-source domains, all exercisable in the same bare QEMU
session (no extra devices needed): CMOS RTC, HPET, ACPI PM timer and
TSC.

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -no-reboot -display none -serial stdio -monitor none
```

| try this              | what you should see                                                                        |
|-----------------------|--------------------------------------------------------------------------------------------|
| `rtc`                 | `RTC: 2026-10-13 13:41:02 (UTC, CMOS ports 0x70/0x71)` + `BCD=1 24h=1 rate=1024Hz` + `VRT=1 FADT century reg=32` + register dump |
| `rtc uie 3`           | `update-ended interrupts: 3 (expected 3) PASS` (IRQ8, one fire per second)                 |
| `rtc pie 10 32`       | `periodic interrupts: 32 (expected 32) PASS` (rate index 10 = 64 Hz)                       |
| `rtc alarm 3`         | `alarm interrupt: fired PASS` (AIE, time-of-day match ~3 s ahead)                          |
| `hpet`                | `HPET: base=fed00000 (ACPI table, GAS SystemMemory)` + `Caps: rev=1 timers=3 64bit=1 legacy=1 period=10000000fs freq=100000000Hz` |
| `hpet counter 5`      | five counter reads + `measured 100113820 Hz (nominal 100000000) PASS`                      |
| `hpet oneshot 100`    | `oneshot: fired 1 (expected 1) PASS` (timer 1 -> legacy IRQ8)                              |
| `hpet periodic 100 3` | `periodic: 3/3 PASS`                                                                       |
| `hpet test`           | `hpet: PASS` (counter + oneshot + periodic)                                                |
| `pmtimer`             | `PM timer: port=00000608 (SystemIO) width=32bit (FADT pm_tmr_blk)  delta mask=00ffffff (24bit)` + raw counter |
| `pmtimer cal`         | `measured: 3576824 Hz (nominal 3579545, delta 7 bp) PASS` (PIT comparison, prints samples) |
| `pmtimer wrap`        | wrap-safe delta + 64-bit extension + no-wrap delta, `pmtimer wrap: PASS`                   |
| `pmtimer test`        | `pmtimer: PASS` (calibrate + monotonic)                                                    |
| `tsc`                 | raw read + `invariant: 0 (CPUID 0x80000007 EDX[8])` (honest under TCG) + calibration state  |
| `tsc cal`             | two ~1.1 s PIT runs + drift, `tsc cal: PASS (cached ... Hz via hpet)`                      |
| `tsc cal hpet`        | `TSC calibration vs HPET (~0.1 s): 3199958020 Hz`                                          |
| `tsc delay 100`       | `PIT-measured actual: 100 ms PASS`                                                         |
| `tsc test`            | `tsc: PASS` (monotonic 1000 reads + 2x PIT calibration + delay)                            |

The system tick pauses for the duration of the HPET legacy-route tests
(~1.5 s max, restored afterwards) - see docs/KNOWN_ISSUES.md §12 and
docs/EXTENSIONS_WP10-timer_rtc.md §6.

## Troubleshooting

| symptom                                   | cause / fix                                |
|-------------------------------------------|--------------------------------------------|
| `mkfs.fat32: device not found`            | wrong device name; run `lsblk` first       |
| `fsck: cannot read boot sector`           | the disk was never formatted               |
| `connect failed` after `checkupdate`      | no network: run `dhcp` first               |
| `dhcp` prints discover but no lease       | NIC missing: add `-netdev user,id=n1 -device e1000,netdev=n1` |
| `play: no sound card registered`          | no audio device: see section 3             |
| QEMU 10 rejects audio args                | every audio device needs `audiodev=snd0`   |
| `SKIPPED` / `FAIL (no ... registered)`    | honest report for absent hardware, not a bug |

## 8. Wireless + Bluetooth (WP-10-wireless_bluetooth)

Boot the ISO (BIOS or UEFI) and try:

| command | what you should see |
|---|---|
| `wifi` | 7 families listed; 7 device slots with bus/vid/did and honest `state=down` (QEMU has no 802.11 hardware) |
| `wifi test` | the 802.11 core selftest + every family selftest, one PASS line per check, ending `wifi test: 0 failures - PASS` |
| `wifi scan` | `wifi scan: 0 networks seen (no beacons - no radio present)` |
| `wifi sdio` | `sdio bus: state=... hosts=N` + bus selftest PASS lines (CMD52/CMD53 encodings) |
| `bt` | `bluetooth: transport=h4-uart2 controller=66:55:44:33:22:11 ...` + the L2CAP channel table (with the test peer attached on COM2) |
| `bt uart` | `h4: uart2 ok, rx ring N tx=M` |
| `bt usb` | `usb-bt: no dongle attached` (honest; QEMU removed its BT models) |
| `bt test` | the full HCI -> L2CAP -> SDP -> RFCOMM -> SPP -> HID flow with a real peer: every step PASS, `bt test: 0 failures - PASS` |

The Bluetooth flow is genuinely end-to-end: the kernel's H4 UART driver
exchanges real bytes with a host-side controller peer
(tools/bt_test_peer.py; the E2E driver wires it to QEMU's second
serial port automatically).

## 9. Cameras, parallel, game port, infrared, SCSI (WP-10-camera_misc)

Boot the ISO with the SCSI model and the parallel capture file:

```
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -device lsi,id=scsi0 \
  -drive if=none,id=sd0,format=raw,file=disk.img \
  -device scsi-hd,drive=sd0,bus=scsi0.0 \
  -parallel file:parallel-out.txt
```

| command | what you should see |
|---|---|
| `scsi` | `scsi HBAs: 1` + `lsi0 (initiator id 7)` + `sda: target 0 lun 0 disk "QEMU QEMU HARDDISK" spc3 131072x512 blk=0` |
| `scsitest` | CDB goldens, parser checks, then live checks ending `scsitest: 0 failures - PASS` (INQUIRY, READ CAPACITY vs blk, a WRITE/READ round trip on the last sector with the original content restored, LBA 0 through blk vs raw SCSI) |
| `lsblk` | `sda` in the block table (64 MiB, from the scsi-hd image) |
| `paralleltest` | 10 checks ending `paralleltest: 0 failures - PASS`; 'OPENCUBE' lands in parallel-out.txt |
| `parallel` | LPT candidates + the probed port + current mode |
| `uvctest` | 30/30 checks against the built-in spec fixtures (byte-exact JPEG + UVC 1.1 descriptors) |
| `uvc` | `camera subsystem: 0 cameras` (honest: QEMU ships no camera) |
| `gameporttest` | pure-function checks PASS; live steps honestly skipped (QEMU has no game port) |
| `irtest` | NEC/RC-5/RC-6/SIRC round trips + known-answer vectors ending `irtest: 0 failures - PASS`; live SIR loopback on the emulated 16550 |
| `ir` | registry status (CIR honestly absent, SIR when the 16550 probe binds) |

Troubleshooting:

| symptom | cause / fix |
|---|---|
| `scsi HBAs: 0` | no LSI device: add `-device lsi,id=scsi0` |
| `scsi devices: 0` | no disk behind the HBA: add the `-drive if=none...` + `-device scsi-hd...` pair |
| `scsitest` rw check fails | the test writes the LAST sector of the disk - use a scratch image, not your install disk |
| parallel-out.txt empty | add `-parallel file:parallel-out.txt` |
| `camera subsystem: 0 cameras` | QEMU models no UVC camera - honest state, parser verified by fixtures |

## 10. TUI (WP-10-tui)

The TUI is the full-screen ncurses-style layer on top of the kernel
console.  Two real backends: the built-in framebuffer console
(default, `tui.backend=fb`) and an ANSI/VT100 terminal on COM1
(`tui.backend=serial`).  All TUI commands work on BIOS and UEFI boots
with no extra QEMU hardware.

Quick start at the `oc>` prompt:

```
oc> tui                # full-screen main menu (arrows / Tab / Enter / Esc)
oc> tui-test           # 40+ self-checks, prints input/expected/actual/verdict
oc> tui-hw             # hardware configuration (11 domain pages, Tab cycles)
oc> tui-hw Memory      # one page directly
oc> tui-net            # WiFi scan/list/connect + Bluetooth manager
oc> tui-sysinfo        # overview/processes/logs/monitor/power/update/time/help
```

What you should see (framebuffer backend, QEMU, no extra hardware):

- `tui` renders a full-screen bordered menu with an aqua status line;
  arrows move the selection, Enter opens the section, F1 opens help,
  F10/Esc leaves and restores the shell.
- `tui-test` ends with `tui-test: <n>/<n> checks passed, all green`
  (exit code = failure count; every failed check prints
  input/expected/actual).
- `tui-hw Memory` shows the live PMM numbers (total/used/free pages
  and the allocation counters); `tui-hw CPU` shows the CPUID vendor
  and model string of the machine QEMU presents.
- `tui-net` reports honestly on QEMU: the 802.11 device list is empty
  (no QEMU WiFi model) and the WiFi link stays DOWN until real
  hardware associates; the Bluetooth manager reports
  "no Bluetooth transport" unless a USB dongle or the COM2 H4 peer is
  attached (see section 8 for the H4 peer setup).
- `tui-sysinfo overview` shows the WP-10-tui version string, the
  monotonic build id, uptime and the live memory/task numbers.

Shortcut map: F1 help, F2 save config, F3 search, F5 refresh, F10
exit, Tab switch domain/field, arrows navigate, Enter confirm, Esc
cancel.  The mouse works through the touchpad path when an I2C-HID
device exists and through xterm SGR mouse reports when the backend is
a real terminal on COM1.

## Try Chinese support (WP-10-chinese_support)

Boot the ISO, then at the `oc>` prompt:

```
chinese            # font source, 6763+682 glyph coverage, IME stats
chinesetest        # 91-check suite: font data/lookup/manage, GB2312,
                   # UTF-8 (strict), conversion, detection, renderer,
                   # console, line editing, font registry, TUI/IME, VFS
font               # registered fonts + the active mixed-width renderer
font-test          # draws a real 16x16 CJK sample row on the screen
encoding           # codec status line (decode/encode/convert/detect)
encoding-test      # GB2312 + UTF-8 round-trip spot check
ime                # pinyin IME coverage (396 syllables / 3755 chars)
ime -i             # interactive console composition line: type pinyin
                   # letters, digits 1-9 pick, Space = first candidate,
                   # Backspace drops a letter, Enter ends, Esc cancels
ime-test           # ni -> ni, hao -> hao resolution check
tui-cn             # Chinese TUI launcher: hardware config /
                   # wireless network / system information / and the
                   # 中文输入 pinyin composition pad, all with
                   # live driver data and Chinese labels
```

Type a Chinese document over serial (paste UTF-8 - the serial bridge
now forwards 8-bit bytes) and watch the shell line editor keep the
characters intact with correct cursor widths; `cat` any UTF-8 file to
see real CJK glyphs on the framebuffer console.  Inside `tui-cn` the
hardware pages (Tab cycles 11 domains, S saves / L loads the config)
are fully usable, the Chinese input pad (launcher item 4) composes
text with the pinyin IME (type `ni`, press `1` or Space, Enter to
commit, Esc to leave), and the Chinese WiFi page takes plain-ASCII
SSIDs in its connect field (the editor never enters pinyin
composition).
