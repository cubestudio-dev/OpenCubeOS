<!-- SPDX-License-Identifier: Apache-2.0 -->
# Storage troubleshooting

Ordered triage, the mount-refusal paths, the filesystem capability
matrix, the A/B disk, and the write-back cache.

## Ordered triage

Run `disk-tool` and walk the menu, or use the classic spellings:

```
ush> lspci                     (PCI storage controllers present?)
ush> ahci / ata / nvme         (family probe - the message names the fault)
ush> dskstat                   (block instances registered? cache state)
ush> lsblk                     (devices + partitions scanned?)
ush> parted <dev>              (the partition table)
ush> fatstat <dev>             (FAT statistics of a suspect volume)
ush> fsck <dev>                (filesystem check - FAT32 only)
ush> fatmount <dev> /mnt       (mount)
ush> fstest                    (the VFS/fs assertion suite)
```

## The filesystem capability matrix

| Filesystem | Read | Write | Notes |
|------------|------|-------|-------|
| ramfs | RW | RW | the rootfs; everything on it is volatile — only /etc and mounted volumes persist |
| FAT32 | RW | RW | full write support (file creation, cluster allocation, chain extension, directory updates, deletion); caches the first FAT in memory |
| exFAT | RW | RW | dirty-flag warning on unclean mounts (`exfat: warning - volume was not cleanly unmounted`) |
| ext4 | **RO** | — | read-only driver; refuses INCOMP feature bits, block sizes > 64 KiB, odd inode sizes |
| ext2/3 | RO | — | ext2 is a subset of ext4; ext3 journals replay is not performed — mount is best-effort RO |

Case sensitivity follows the backing fs (BUG-0179): FAT32/exFAT are
case-insensitive; ramfs/ext4 are case-sensitive. VFS limits: 16
mounts, 64 fds, names 64 bytes, paths 256 bytes.

## Mount-refusal paths (what each refusal means)

- `vfs_mount: unknown fs type` — the type is not one of
  ramfs/fat32/exfat/ext4.
- `vfs_mount: parent path does not exist` / `mount point exists and is not a directory` — create the mountpoint (`mount <type> <dev> <path>`
  pre-creates it; `fatmount` deliberately does NOT — on fatmount
  failure the two-line honest message says the mountpoint was NOT
  created).
- `fat32: not a FAT32 volume (FAT12/16 detected)` / `invalid BPB` /
  `read boot sector failed rc=<N>` — the volume is not a mountable
  FAT32. `fatstat <dev>` shows the parsed BPB; the usual cause is the
  wrong device node (`lsblk`) — or the alignment-destroying heap
  regression (fixed; re-run `ata_dma_test` if you touched mem_heap.c).
- `fat32: FAT cache alloc failed` / `FAT read failed` — memory or
  medium.
- `ext4: not an ext4 volume (bad magic)` / `read superblock failed` —
  not ext4, or the superblock is beyond the driver's reach.
- `ext4: unsupported INCOMP feature bits 0x<hex> - mount refused` (and
  the block-size/inode-size/group-size guards) — the RO driver refuses
  features it cannot honor; mount a simpler volume.
- `exfat: invalid number_of_fats` — not an exFAT.
- `umount: target is busy - close open files first` — open fds hold
  it (open_count is tracked per node, BUG-0061).
- `umount: unmount nested mounts first` — children first (`mounts`
  shows nesting).
- `umount: cannot unmount the root filesystem` — by design.
- `mount: failed` after a `fatmount` — the honest two-line message
  ends with `no FAT32 partition on device or drive not present`.

## The classics

| Symptom | Likely cause |
|---------|--------------|
| valid FAT32 volume refused (`invalid BPB`) | wrong device node, or the alignment-destroying heap regression (fixed; rerun `ata_dma_test` if you touched mem_heap.c) |
| write lost after reboot | it lived on ramfs — only /etc and mounted volumes persist |
| USB stick appears then vanishes | USB MSC detach path — `usb_hotplug_test` reproduces |
| ext4 mount of an ext2 volume | supported (ext2 is a subset); check `fsck`-class signals first — fsck itself is FAT32-only |
| wizard asks twice for the device name | by design — mkfs/abdisk/install paths require the retype + bare-`y` confirmation |
| A/B disk "absent" right after `abdisk` | re-run `update status`; the wizard re-checks in a loop until the service sees it |
| fsck reports dirty after clean unmount | FSINFO/FAT mirror regression — report it; this class is a bug |
| `blk: write to protected device ... REFUSED (ocire recovery disk)` | whole-disk OCIRE protection — by design; modify that volume only from OCIRE `repair` |
| `blkcache: alloc FAILED - cache disabled (raw I/O)` | the 64-slot cache could not allocate; the system continues uncached (slower but correct) |
| `virtio-blk: write rejected (device is read-only)` | the QEMU drive was attached `readonly=on` — by design |
| ATAPI device on an AHCI port | `<port>: ATAPI (not supported)` — declared limit |

## The A/B disk — layout and repair

Layout created by `abdisk <dev>` (destructive; re-runs
`ota_ab_rescan()` afterwards):

```
p1 boot/flags 32 MB FAT32   /boot/next_B, /boot/ok_B, /boot/bootfail_B, grub.cfg
p2 slot A      128 MB FAT32 /boot/opencube.elf, /etc, /docs...
p3 slot B      128 MB FAT32 same structure as slot A
p4 /data       rest  FAT32 user data, downloaded packages
```

- **Missing A/B disk** is the #3 OTA silent failure: `update status`
  lacks `A/B disk: present`; `update` reports
  `update: cannot determine target slot (not booted from A/B?)`.
- **Repair**: `lsblk` → pick the device → `abdisk <dev>` (classic, or
  the disk-tool wizard's guarded path: it asks the device, asks you to
  RETYPE the exact name, warns, and requires a bare `y`). The chain
  reports its own step failures (`abdisk: failed to write partition table`, `mkfs.fat32 failed on p<N>`, ... — the first one is the real
  one).
- Rollback semantics: `rollback` clears the flags; the boot menu's
  ` rollback` label means `next_B` + `bootfail_B` forced slot A.
- Boot-attempt budget: `/boot/grub/grubenv` `ab_attempts`, max 3.

See [boot-failures.md](boot-failures.md) §6 for the slot-selection
state machine.

## The 64-slot LRU write-back cache

The block layer keeps a 64-slot (64×512 B = 32 KiB) LRU **write-back**
cache: `{dev_idx, lba, data[512], dirty, last_used}` with stats
`{hits, misses, evicts, writebacks, used}` (`dskstat` shows the
picture).

What this means for you:

- Written data may sit in cache until eviction or a flush — run
  `sync` before power operations, unmounts you care about, and any
  experiment that resets the machine.
- `sync` drives the OS cache flush **plus** the per-device flush hook:
  ATA FLUSH CACHE, NVMe Flush (0x0A), virtio-blk FLUSH, AHCI FLUSH
  CACHE EXT, SCSI SYNCHRONIZE CACHE.
- If the cache could not allocate (`blkcache: alloc FAILED - cache disabled (raw I/O)`), writes go straight through (correct, slower).

## The destructive paths

`mkfs.*`, `abdisk`, `install` destroy data. In the wizards they are
guarded (retype the device name, then a bare `y` — only `y`/`Y`
answers yes); through classic/direct dispatch they run immediately —
exactly like the historical commands — so scripts keep their
semantics. **Double-check the device name against `lsblk` before the
classic forms.** `fs_mount_test <device>` also formats its target.
The installer chain (`install <device>`, ≥ 64 MiB, MBR ≤ 2 TiB) and
`grub-install` (MBR gap ≥ core.img size) report step-by-step failures
— [error-catalog.md](error-catalog.md) §5.4.

## Driver-level failure strings

- AHCI: `ahci: issue fail (PxIS.TFE)`, `(PxTFD.ERR)`,
  `ahci: issue TIMEOUT CI=...`, `<port>: IDENTIFY failed`,
  `ahci: BAR5 invalid`.
- ATA: `ata: PIO read DRQ timeout lba=<n>`, `atadma: xfer FAIL rc=<n>`.
- NVMe: `nvme: BAR0 invalid`, `disable timeout ... retrying`,
  `ready timeout, resetting and retrying`, `unsupported formatted LBA size (only 512B supported)`, `Identify Controller/Namespace failed`.
- virtio-blk: `no legacy PCI device found (modern-only devices not supported)`, `used-ring poll timeout`.
- USB MSC: `msc: CBW bulk out failed`, `msc: CSW bad signature or tag`, `usb-msc: device not ready`.

Full list with responses: [error-catalog.md](error-catalog.md) §6.5.

## Reading the probes

- `dskstat` — which block instances registered, plus the cache
  picture (`hits/misses/evicts/writebacks/used` for the 64-slot LRU).
  Zero instances = no storage driver bound (see the driver strings
  below / QEMU flags).
- `lsblk` — devices and scanned partitions; the authoritative source
  for device names (every destructive command takes these names
  exactly).
- `parted <dev>` — the MBR/GPT table view.
- `fatstat <dev>` — the parsed BPB of a suspect FAT32: if the fields
  (bytes/sector, sectors/cluster, reserved, FAT size, root cluster)
  look insane, you are looking at the wrong device or a corrupt
  volume.
- `fsck <dev>` — FAT32-only check; verdicts are per-check `FAIL (<detail>)` lines with a final summary (the dirty-cluster check
  prints `cluster count mismatch: counted <A> ...`).

## QEMU storage flags cheat sheet

| Goal | Flag | Notes |
|------|------|-------|
| plain writable disk | `-drive file=hd.img,format=raw` | shows as IDE/ATA |
| virtio-blk fast path | `-drive file=hd.img,if=virtio,format=raw` | legacy PCI device required (modern-only refused) |
| AHCI | `-device ahci` + `-drive ...` | ATAPI devices print `not supported` |
| NVMe | `-drive file=nvme.img,if=none,id=nvme0 -device nvme,drive=nvme0,serial=ns1` | only 512B LBA size supported |
| read-only intent | `readonly=on` | writes print the honest `virtio-blk: write rejected` |
| USB stick | `-usb -device usb-storage,drive=u0` | detach/reattach via `usb_hotplug_test` |

Remember `-snapshot` (the default in the `run-*` make targets) means
writes do not persist; `run-bios-persist` / `run-uefi-persist` turn
it off so `/etc/opencube.conf` edits land in `build/etc.img`.

## Partition-table notes

- MBR only on the install path (`install` refuses > 2 TiB).
- GPT is parsed for display (`parted`) — the in-system installer
  writes MBR layouts.
- The OCIRE recovery disk's 0xEE "shadow" MBR entry with no GPT
  header is a deliberate disguise: normal GPT tooling ignores the
  16 MiB FAT12 volume behind it, and the whole-disk write protection
  keeps it that way.
- Partition children of the A/B disk (p2/p3) are slot-consumed — they
cannot register their own boot entries.

## Related pages

- [error-catalog.md](error-catalog.md) §5.2-§5.4, §6.5 — every message
- [../manual/filesystems.md](../manual/filesystems.md) — the user guide
- [../manual/update-ota.md](../manual/update-ota.md) — the A/B story
- [boot-failures.md](boot-failures.md) — the boot side of A/B
