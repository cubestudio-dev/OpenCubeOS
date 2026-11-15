<!-- SPDX-License-Identifier: Apache-2.0 -->
# OTA and A/B: slots, flags, the update chain, and rollback

The self-hosting story lives in `ocos/services/selfhost/`
(`svc_selfhost_{ab,update,cmds_config,cmds_test,l1}.c`): the kernel
can check, download, verify, install and roll back its own next
version using only its own drivers (net + TLS + fs + block + crypto),
plus the `/etc/opencube.conf` config store. The boot-loader side of
the flag protocol (OCBL's judgment chain and the two GRUB configs) is
[boot-flow.md](boot-flow.md); the disk layout tooling is
[driver-model.md](driver-model.md).

## The slot layout (verbatim from `svc_selfhost_ab.h`)

```
 *     +--------------------+
 *     | p1 boot/flags 32MB |  FAT32: /boot/next_B, /boot/ok_B,
 *     |                    |         /boot/bootfail_B, /boot/grub/grub.cfg
 *     +--------------------+
 *     | p2 slot A     128M |  FAT32: /boot/opencube.elf, /etc, /docs, ...
 *     | p3 slot B     128M |  FAT32: same structure as slot A
 *     | p4 /data     rest  |  FAT32: user data, downloaded packages
 *     +--------------------+
```

Constants: `OC_AB_SLOT_ISO 0 / A 1 / B 2`; mounts `/ab/boot`, `/ab/a`,
`/ab/b`, `/data`; flags `/boot/next_B`, `/boot/ok_B`,
`/boot/bootfail_B`; package default `/data/update-pkg.tar.gz` (tmp
`/tmp/update-pkg.tar.gz`). `tools/make_ab_disk.sh` builds the layout
(MBR, p1 64 MiB flags, p2/p3 128 MiB slots, p4 data; mtools-based, no
loop devices, no root).

## The flag protocol

Three marker files on the flags partition decide the next boot:

| Flags present | Boots | Meaning |
|---|---|---|
| `next_B`, no `bootfail_B` | slot B | an update is staged |
| `next_B` + `bootfail_B` | **slot A (" rollback")** | the B kernel started but never confirmed - automatic rollback |
| `ok_B` | slot B | B confirmed healthy |
| none | slot A | default |

The pessimistic marker is the heart of the scheme: **a slot-B kernel
writes `bootfail_B` at boot and removes it only when the system is
fully up** (`ota_update_confirm_boot()` in kmain, which also resets
the boot-attempt counter). A B kernel that panics mid-boot leaves the
marker in place, and the next reset - OCBL judgment, GRUB menu, or
`grub-ab.cfg` alike - falls back to slot A. The current slot arrives
via the multiboot2 cmdline `oc.slot=A|B|ISO` (OCBL writes it; the
kernel reads it with `ota_ab_set_boot_slot_arg` before scanning).

**Boot-attempt budget** (BUG-0305/A5-16): the GRUB env block
`/boot/grub/grubenv` (1024 bytes, `ab_attempts` variable) with
`OC_AB_MAX_ATTEMPTS 3`; the kernel resets the counter when A finishes
booting or B confirms - so a kernel that resets before reaching
confirm cannot retry forever.

Three implementations of identical semantics: `grub.cfg` (ISO
medium), `grub-ab.cfg` (installed disk, refreshed by every update
install as the `boot/grub.cfg` package file), and OCBL's native
`judge_volume()`.

## The update flow (`oc> update`)

1. **check**: fetch `update.json` from the configured update URL -
   version, package_url, package_sha256, package_size (the `checkupdate`
   command does only this; `auto_check` in `/etc/opencube.conf` makes
   kmain run it asynchronously at boot).
2. **download**: HTTP or HTTPS, streamed to `/data`.
3. **verify**: SHA-256 against the manifest.
4. **install**: gunzip (RFC 1952/1951 - `core_lib_gzip.c`) + untar
   (ustar - `core_lib_tar.c`) **into slot B** (the inactive slot; the
   system keeps running from A throughout).
5. **set boot**: write the flags, print "reboot required".

`rollback` clears the flags. The package itself
(`tools/make_update_pkg.sh`, `opencube-<version>-update.tar.gz`) is
deterministic within the build day; the ELF is fully reproducible
(`make reproducible-check` byte-compares two clean builds).

## The disk-install path - the 8-step state machine

Installing onto bare metal goes through OCSE's
`ocse_inst_install(disk, part, confirm, work, sz)`
(`ocse/ocse_inst.c`) - eight steps, each a real failure point:

1. **medium** - verify the install medium (the OCOS.TAR +
   `ocblmbr.bin` self-check).
2. **target device** - the chosen disk must exist and be usable.
3. **target filesystem** - **FAT32 only**: OCBL reads FAT12/16/32 +
   ISO9660, so a bootable install target must be FAT32; enforced
   honestly (`mkfs.fat32` with the literal `confirm` token as the
   BUG-0239 data-loss gate).
4. **mount** the target.
5. **confirm gate** - the typed confirmation.
6. **extraction** - `tar -xf /install/ocos.tar`.
7. **boot code** - `ocbl-install`: stage1 at LBA 0 with the disk's
   CURRENT partition table bytes 446..509 preserved; patch block
   (magic 'OCBL', core_lba=1, sector count) written at image offset
   0x1A0; the core lands in the pre-partition gap at LBA 1..N
   (mkpart aligns at LBA 2048, leaving 2047 sectors; the core needs
   ~62); full read-back verify. Errors: -1 medium, -2 no table, -3 no
   room, -4 write, -5 verify.
8. **boot flag** - set the active partition (MBR 0x80 / GPT
   legacy-boot attribute).

What gets installed (`tools/make_ocos_tar.py` - "single source of
truth for what install OCOS means"): `boot/opencube.elf` (the OCOS
kernel, the name OCBL scans for), `boot/ocire.elf` (the recovery
kernel, additionally copied to a dedicated recovery partition when
unallocated tail space exists), `etc/opencube.conf`. The `install`,
`abdisk` and `grub-install` in-system commands drive the same layers
(the latter two through the embedded GRUB boot data,
`tools/embed_grub.py`). The 9-step OCSE wizard
([boot-flow.md](boot-flow.md)) is a controller over exactly this
surface - "the wizard implements NO install logic of its own".

## The L1 surface: 16 ops

```c
#define L1SVC_OTA_CHECKUPDATE      1  /* checkupdate           */
#define L1SVC_OTA_CONFIG           2  /* config list|get|set   */
#define L1SVC_OTA_CONFIG_TEST      3  /* config_test           */
#define L1SVC_OTA_CHECKUPDATE_TEST 4  /* checkupdate_test      */
#define L1SVC_OTA_UPDATE           5  /* update [...]          */
#define L1SVC_OTA_ROLLBACK         6  /* rollback              */
#define L1SVC_OTA_PKG_TEST         7  /* update_pkg_test       */
#define L1SVC_OTA_AB_TEST          8  /* ab_partition_test     */
#define L1SVC_OTA_CHECK_TEST       9  /* update_check_test     */
#define L1SVC_OTA_DOWNLOAD_TEST   10  /* update_download_test  */
#define L1SVC_OTA_VERIFY_TEST     11  /* update_verify_test    */
#define L1SVC_OTA_INSTALL_TEST    12  /* update_install_test   */
#define L1SVC_OTA_ROLLBACK_TEST   13  /* update_rollback_test  */
#define L1SVC_OTA_LOCAL_TEST      14  /* update_local_test     */
#define L1SVC_OTA_STATUS_TEST     15  /* update_status_test    */
#define L1SVC_OTA_REAL_TEST       16  /* real_update_test      */
```

`config [set <k> <v>]` views/validates/rewrites
`/etc/opencube.conf` (`config set shell.lang` syncs the live console
language). Key error strings: `"update: cannot determine target slot (not booted from A/B?)"`, `"update: download failed: "`,
`"update: install failed: "`, `"update: setting boot failed"`,
`"rollback: failed"`, `"config: cannot read /etc/opencube.conf"`.

## How the pieces read/write the slots

The read/write path is ordinary block I/O: A/B slot writes = blk ops
-> 64-slot LRU write-back cache -> partition child (bounds-enforced)
-> parent disk ops (ata_dma/ahci/nvme/virtio/scsi/usb-msc). Update
install writes slot B while running from slot A; `abdisk` re-runs
`ota_ab_rescan()` after creating a layout. On OCIRE-supervised boots
the whole recovery disk is write-protected before any of this runs
([hidden-mechanisms.md](hidden-mechanisms.md)).

In-guest tests: the `update_*_test` chain (each stage packaged as a
command - `ab_partition_test`, `checkupdate_test`, `download`,
`verify`, `install`, `rollback`, `local`, `status`, `real_update_test`)
plus the release-gate E2E that walks check -> download -> verify ->
install -> reboot for real. `update_test_vectors` live in
`src/svc_selfhost_update_test_vectors.inc`.

User-facing guides: [../manual/update-ota.md](../manual/update-ota.md),
[../l1/ota.md](../l1/ota.md); internals twin:
[../internals/ota-ab.md](../internals/ota-ab.md).
