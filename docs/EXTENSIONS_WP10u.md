# Open Cube OS - WP-10u Extension Interfaces (In-System Update)

SPDX-License-Identifier: Apache-2.0
Copyright 2026 cubestudio-dev <cubestudio@qq.com>

WP-10u adds a Windows-Update-style in-system updater on top of an A/B
partition scheme.  Every interface below has a header declaration with
comments, a working implementation, and a documented error contract.

## Files

| file                    | purpose                                              |
|-------------------------|------------------------------------------------------|
| kernel/ab_update.h/.c   | A/B slot framework + update core + shell commands    |
| kernel/update.h/.c      | manifest check (extended with package fields)        |
| kernel/gzip.h/.c        | gzip (RFC 1952) + DEFLATE (RFC 1951) inflate         |
| kernel/tar.h/.c         | streaming ustar reader                               |
| kernel/update_test_cmds.c | WP-10u shell test suite                            |
| tools/make_ab_disk.sh   | builds the A/B disk image (build/abdisk.img)         |
| tools/make_update_pkg.sh| builds an update package (tar.gz + manifest.json)    |

## A/B disk layout (MBR, first hard disk)

```
p1  boot/flags  FAT32   /boot/next_B, /boot/ok_B, /boot/bootfail_B,
                        /boot/grub/grub.cfg
p2  slot A      FAT32   /boot/opencube.elf, /etc, /docs, /userprogs
p3  slot B      FAT32   same structure as slot A
p4  data        FAT32   user data, downloaded packages (/data)
```

The kernel registers each partition as its own block device
(`<parent>p1`..`p4`, e.g. `hdap1`) and mounts:

```
/ab/boot  -> p1     /ab/a -> p2     /ab/b -> p3     /data -> p4
```

## Boot slot selection (grub.cfg, CD and disk versions)

| flags state                          | boots |
|--------------------------------------|-------|
| next_B present, bootfail_B absent    | slot B (update pending) |
| next_B present, bootfail_B present   | slot A (AUTOMATIC ROLLBACK) |
| ok_B present                         | slot B (confirmed default) |
| none of the above                    | slot A |
| no A/B disk                          | the ISO copy (oc.slot=ISO) |

The kernel writes `bootfail_B` pessimistically when booting slot B and
removes it (plus writes `ok_B`) once the system is fully up.  A slot-B
kernel that never reaches `oc_update_confirm_boot()` therefore triggers
an automatic rollback on the next boot.

The current slot travels through the multiboot2 command line
(`oc.slot=A` / `oc.slot=B` / `oc.slot=ISO`).

## Configuration (4 keys)

```
update_url=https://cubestudio-dev.github.io/OpenCubeOS/update.json
package_url=https://github.com/cubestudio-dev/OpenCubeOS/releases/latest
auto_check=no
online_update=yes
```

`online_update=no` disables `update` (online) and leaves `update
--local` (offline packages, e.g. from a USB drive) available.

## Update manifest (update.json)

```json
{
  "version": "WP-10u",
  "time": "2026-10-05",
  "changes": "In-system update: A/B partitions, rollback, offline update",
  "changes_v2_url": "https://.../update-v2.json",
  "package_url": "https://.../opencube-wp10u-update.tar.gz",
  "package_sha256": "<64 hex chars, SHA256 of the .tar.gz>",
  "package_size": 290109
}
```

The three `package_*` fields are optional for `checkupdate` (older
manifests stay compatible) and required for `update`.

`changes_v2_url` is the dual-manifest ("Plan D") pointer described in
docs/CONFIG.md §6.1: the base manifest keeps `changes` short enough for
WP-09's 128-byte buffer, and kernels that understand the field fetch the
long-changelog `/update-v2.json` (same `version`/`package_*` values, full
`changes`).  `update` itself always downloads `package_url` from the
base manifest; the v2 document only upgrades the display-only changelog.

## Package format (tar.gz)

```
opencube-wp10u-update/
  manifest.json          version, files[], sha256
  kernel/opencube.elf    installed to /boot/opencube.elf
  boot/grub.cfg          installed to /boot/grub.cfg AND mirrored to p1
  etc/opencube.conf      installed to /etc/opencube.conf
  docs/...               installed to /docs/...
```

Path mapping (`map_package_path` in ab_update.c):
`kernel/opencube.elf` -> `/boot/opencube.elf`, everything else -> `/<path>`
relative to the package root; a top-level wrapper directory (as shown
above) is stripped automatically.

`manifest.json` fields: `version` (must parse), `sha256` (SHA256 over the
concatenation of every payload file body, in archive order, excluding
manifest.json itself - verified by the installer while streaming).

## L1 extension interfaces (kernel/ext.h, section 7)

```c
int  oc_ext_update_check_pkg(oc_ext_update_pkg_info_t *out);
int  oc_ext_update_download(const char *url, const char *path);
int  oc_ext_update_verify(const char *path, const char *sha256_hex);
int  oc_ext_update_install(const char *pkg_path, const char *slot);
int  oc_ext_update_rollback(void);
int  oc_ext_update_set_boot(const char *slot);
int  oc_ext_update_get_status(oc_update_status_t *out);
```

- `oc_ext_update_check_pkg` - fetch update.json; fills version/time/
  changes plus package_url/package_sha256/package_size; returns
  OC_UPDATE_OK (0, up to date) / OC_UPDATE_NEW (1) / negative error;
  caches the result for `update --status`.
- `oc_ext_update_download(url, path)` - streaming HTTP or HTTPS download
  to a VFS path; returns the byte count or a negative error; the
  HTTP response status is checked and Content-Length is enforced.
- `oc_ext_update_verify(path, sha256_hex)` - streaming SHA256 over the
  file, case-insensitive hex compare; OC_UPDATE_E_SHA on mismatch.
- `oc_ext_update_install(pkg_path, slot)` - gunzip -> untar -> write to
  the slot; verifies the manifest payload digest; refreshes the flags
  partition copy of grub.cfg; returns 0 on success.
- `oc_ext_update_set_boot("A"|"B")` - B: create next_B, clear ok_B and
  bootfail_B; A: clear all three.
- `oc_ext_update_rollback()` - clear the flags so the next boot uses A.
- `oc_ext_update_get_status(out)` - fill oc_update_status_t
  (current_version, available_version, current_boot, next_boot,
  online_update, update_available, ab_present).

### Error codes (kernel/update.h)

```
OC_UPDATE_E_DISABLED (-9)  online_update=no
OC_UPDATE_E_NOAB     (-10) no A/B disk
OC_UPDATE_E_SHA      (-11) SHA256 mismatch
OC_UPDATE_E_TARGZ    (-12) package is not valid gzip/tar
OC_UPDATE_E_IO       (-13) filesystem I/O error
OC_UPDATE_E_SLOT     (-14) invalid slot name (use A or B)
OC_UPDATE_E_ARGS     (-15) NULL / malformed arguments
```

Plus the WP-09-fix5 transport codes (-1..-8, unchanged).

## Shell commands

```
update                  online update (check -> download -> verify ->
                        install into B -> set boot B)
update --local <path>   offline update from a package file
update --status         print current version / boot slot / next boot /
                        available version / online update / A/B presence
rollback                switch the next boot back to slot A
reboot                  flush all block devices, then reset (8042 + ACPI)
```

## Shell test suite

```
update_pkg_test       gzip/DEFLATE + tar unit tests (12 cases)
ab_partition_test     A/B disk discovery, partition devices, mounts (7)
update_check_test     manifest check incl. package fields (4)
update_download_test  streaming download (3)
update_verify_test    SHA256 verify + negative case (4)
update_install_test   install into slot B (6)
update_rollback_test  boot-flag switching + rollback (4)
update_local_test     offline flow through update --local (4)
update_status_test    oc_update_get_status coverage (5)
real_update_test      end-to-end phase 1 (7); phase 2 is the reboot
                      sequence documented in the delivery report
```

## End-to-end flow (validated in QEMU)

```
oc> real_update_test          # download -> verify -> install -> boot B
oc> reboot                    # GRUB selects slot B via next_B
slot B kernel boots, writes ok_B (confirmed)
oc> rollback                  # clear flags
oc> reboot                    # GRUB falls back to slot A
```
