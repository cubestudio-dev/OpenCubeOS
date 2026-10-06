<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - In-System Update (OTA) How-To

A step-by-step user guide for the WP-10u update system.  Every step
below shows the exact command and the output you should see.  If your
output differs, see "Troubleshooting" at the end.

What you get at the end: an Open Cube OS system that checks a server for
a new version, downloads it, verifies it, installs it into slot B, boots
into it, and can roll back to slot A - all from the running system.

## 0. What you need

| item                          | where                        |
|-------------------------------|------------------------------|
| Open Cube OS ISO (WP-10u+)    | GitHub Releases / website    |
| QEMU (6.0+) or real hardware  | distro package               |
| The repo tools directory      | this repo, `tools/`          |
| mtools (mformat/mcopy/mdir)   | needed by make_ab_disk.sh    |

All commands run on the host unless the step says "in the guest".

## 1. Create the A/B disk

One command.  Optionally pass a kernel ELF to pre-seed slot A so the
disk can boot on its own:

```sh
bash tools/make_ab_disk.sh build/opencube.elf
```

Expected (abridged):

```
[make_ab_disk] image: build/abdisk.img (920576 sectors)
[make_ab_disk] MBR written: starts=[2048, 133120, 395264, 657408]
[make_ab_disk] formatted p1 (OCS_BOOT, 131072 sectors)
[make_ab_disk] formatted p2 (OCS_A, 262144 sectors)
[make_ab_disk] formatted p3 (OCS_B, 262144 sectors)
[make_ab_disk] formatted p4 (OCS_DATA, 262144 sectors)
[make_ab_disk] p1: /boot/grub/grub.cfg installed
[make_ab_disk] p2 (slot A): /boot/opencube.elf seeded from build/opencube.elf
[make_ab_disk] done: /home/.../build/abdisk.img
```

Layout (matches kernel/ota/ota_ab.h): p1 = boot/flags (64 MiB),
p2 = slot A, p3 = slot B, p4 = data (128 MiB each, all FAT32).

## 2. Boot the ISO with the A/B disk attached

```sh
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso -boot d \
  -drive if=ide,format=raw,file=build/abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

(Or add `-drive if=ide,format=raw,file=build/abdisk.img` to any setup
you already have; the kernel discovers the A/B layout automatically.)

In the guest, verify the disk was detected:

```
oc> update --status
update: current version: WP-10c
update: current boot: A
update: next boot: A
update: available: unknown (run update or checkupdate)
update: online update: enabled
update: A/B disk: present
```

`A/B disk: absent` means the layout was not found - check that the image
was built by make_ab_disk.sh and is attached as a hard drive (not a CD).

The full self-test for the A/B layer:

```
oc> ab_partition_test
[ab_partition_test] A/B disk + slots
[partition devices hdap1..p4] PASS
[slot mounts /ab/boot /ab/a /ab/b /data] PASS
[abp2 boot sector (FAT32 BPB)] PASS
[boot slot known (A/B/ISO)] slot=A PASS
[boot flags readable] next_B=no ok_B=no bootfail_B=no PASS
[set_boot(B) -> flags -> rollback clears] PASS
[set_boot(X) rejected] PASS
[ab_partition_test] 7/7 PASS
```

## 3. Build an update package

```sh
bash tools/make_update_pkg.sh WP-10d build/opencube.elf build/pkg.tar.gz
```

Expected:

```
[make_update_pkg] package: build/pkg.tar.gz
[make_update_pkg] payload sha256 (manifest): 494c16ea...
[make_update_pkg] package sha256 (.tar.gz): d1ce3772...
[make_update_pkg] package size: 314813
```

The .tar.gz contains manifest.json + kernel/opencube.elf + boot/grub.cfg
+ etc/opencube.conf + docs/.  Two SHA256 values are involved:
- `package sha256 (.tar.gz)`: goes into update.json (`package_sha256`);
- `payload sha256 (manifest)`: inside the package, verified while
  streaming the install.

## 4. Serve the manifest and the package

One command serves everything (manifest, optional v2 manifest, package):

```sh
python3 tools/update_server.py --mode http --port 8008 \
  --json '{ "version": "WP-10d", "time": "2026-10-20",
            "changes": "new release",
            "changes_v2_url": "http://10.0.2.2:8008/update-v2.json",
            "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
            "package_sha256": "<64 hex chars from step 3>",
            "package_size": 314813 }' \
  --json2 '{ "version": "WP-10d", "time": "2026-10-20",
             "changes": "the full-length changelog goes here",
             "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
             "package_sha256": "<same>",
             "package_size": 314813 }' \
  --pkg build/pkg.tar.gz
```

`10.0.2.2` is the host as seen from the QEMU user-mode network.
`package_sha256` and `package_size` must match the values printed by
step 3 - the kernel refuses the package otherwise.

`changes` stays short (<= 127 bytes) so old kernels (WP-09) can still
read it; kernels that understand `changes_v2_url` fetch the long
changelog from /update-v2.json (see docs/CONFIG.md 6.1).

## 5. Run the update (in the guest)

```sh
oc> update
update: checking https://.../update.json
update: reading /etc/opencube.conf
New version available.
Version: WP-10d
Changes: new release
update: downloading http://10.0.2.2:8008/pkg.tar.gz
update: downloaded 314813 bytes
update: SHA256 OK
update: installing into slot B
update: manifest payload digest OK
update: installed 4 file(s)
update: boot flags set: next boot = B
update: done. Reboot to start the new version.
```

Then reboot - GRUB reads the `next_B` flag and boots slot B:

```sh
oc> reboot
```

When slot B comes up it writes `ok_B` (boot confirmed).  Check:

```sh
oc> update --status
update: current boot: B
update: next boot: B
```

## 6. Roll back

Any time (from slot A or B):

```sh
oc> rollback
update: next boot set to slot A
oc> reboot
```

If slot B ever fails to reach the desktop banner, the `bootfail_B` flag
it wrote pessimistically at boot stays behind, and GRUB automatically
falls back to slot A on the next power cycle - no command needed.

## 7. Offline update (no network)

Put the package on the data partition (or any mounted FAT32 volume)
and run:

```sh
oc> update --local /data/pkg.tar.gz
```

Everything else (verify, install, boot flags) is identical.

## 8. Check without installing

```sh
oc> checkupdate          # fetch + display only
oc> update --status      # current/next boot, A/B presence, availability
```

## Command reference

| command             | purpose                                   |
|---------------------|-------------------------------------------|
| update              | check + download + verify + install to B  |
| update --local PATH | offline install from a package file       |
| update --status     | current version, boot slots, availability |
| checkupdate         | check + display only (no download)        |
| rollback            | next boot back to slot A                  |
| reboot              | flush devices and restart                 |

## Self-test suite

| test                 | covers                              |
|----------------------|-------------------------------------|
| update_pkg_test      | gzip/DEFLATE + tar (12 checks)      |
| ab_partition_test    | A/B discovery, mounts, flags (7)    |
| update_check_test    | manifest parsing incl. v2 (4)       |
| update_download_test | streaming download (3)              |
| update_verify_test   | SHA256 + negative case (4)          |
| update_install_test  | install into slot B (6)             |
| update_rollback_test | flag switching + rollback (4)       |
| update_local_test    | offline flow (4)                    |
| update_status_test   | status reporting (5)                |
| real_update_test     | full end-to-end phase 1 (7)         |

## Troubleshooting

| message                                     | meaning / fix                          |
|---------------------------------------------|----------------------------------------|
| `update: A/B disk: absent`                  | disk not attached or not built by make_ab_disk.sh |
| `config file missing or unreadable`         | /etc/opencube.conf missing; run `config restore` |
| `invalid URL prefix`                        | update_url must be http:// or https:// |
| `DNS resolution failed` / `connect failed`  | network down; run `dhcp`, then retry   |
| `JSON parse failed`                         | server did not return the manifest     |
| `response too large`                        | manifest bigger than the receive buffer |
| `SHA256 mismatch`                           | package_sha256 in update.json is wrong |
| `package is not valid gzip/tar`             | broken upload; rebuild with make_update_pkg.sh |
| `invalid slot name`                         | use A or B                             |
| `online update disabled`                    | set online_update=yes in /etc/opencube.conf |

Error codes (kernel/ota/ota_update.h): -1..-8 transport (WP-09-fix5 contract),
-9 disabled, -10 no A/B disk, -11 SHA mismatch, -12 bad gzip/tar,
-13 I/O, -14 slot, -15 args.

## Files

- kernel/ota/ota_ab.h/.c - A/B framework + updater
- kernel/ota/ota_update.h/.c - manifest check (Plan D v2 changelog)
- tools/make_ab_disk.sh - A/B disk image builder (host)
- tools/make_update_pkg.sh - update package builder (host)
- tools/update_server.py - manifest + v2 + package test server (host)

Interface details: docs/EXTENSIONS_WP10u.md.  Config keys:
docs/CONFIG.md.
