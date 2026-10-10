<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - Try It Guide

Every hardware feature of the kernel, with the exact QEMU command to
attach the device and the exact in-guest commands to exercise it -
including the output you should see.  If your output differs, the last
section lists the likely causes.

This guide covers **storage, network, and sound**.  For the in-system
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
| `help`                            | the 150-command list                    |
| `uname -a`                        | `Open Cube OS <version> x86_64`, where `<version>` is the build-time `git describe --tags` string (e.g. `Open Cube OS WP-10-AUDIT_P2-fix2b-7-g98a8dc7 x86_64`; Makefile `OC_RELEASE_VERSION`) |
| `mkdir /d` + `write /d/f hi` + `cat /d/f` | `hi`                            |
| `mem`, `heap`, `ps`, `sched`      | memory/task/scheduler stats             |
| `heaptest`, `memtest`, `vmtest`   | PASS                                    |
| `run hello`                       | `hello from userspace`                  |
| `run fork_test` / `run pipe_test` / `run signal_test` | ... PASS           |
| `cryptotest`                      | SHA-256 / AES / HMAC 3/3 PASS           |
| `crashlog`                        | last kernel exceptions (if any)         |

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
