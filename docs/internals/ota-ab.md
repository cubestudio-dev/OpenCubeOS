<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: OTA and the A/B slots

The self-hosting/update story: the kernel can check, download,
verify, install and roll back its own next version using only its
own drivers (net+TLS+fs+block+crypto), plus the
`/etc/opencube.conf` config store. Implementation:
`ocos/services/selfhost/` - `interface/svc_selfhost_{api,ab,update}.h`,
`src/svc_selfhost_{ab,update,cmds_config,cmds_test,l1}.c`. The
install-path dissection:
[../dev/call-chains.md](../dev/call-chains.md) section 6. User
guide: [../manual/update-ota.md](../manual/update-ota.md).

## 1. The A/B slot layout (verbatim from svc_selfhost_ab.h)

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

Constants: `OC_AB_SLOT_ISO 0 / A 1 / B 2`; mounts `/ab/boot /ab/a /ab/b /data`; flags `/boot/next_B`, `/boot/ok_B`,
`/boot/bootfail_B`; package default `/data/update-pkg.tar.gz`
(tmp `/tmp/update-pkg.tar.gz`). The current slot arrives via the
multiboot2 cmdline `oc.slot=A|B|ISO`.

## 2. Boot-flag semantics

Boot selection (grub.cfg logic):

| `next_B` | `bootfail_B` | `ok_B` | Boots |
|---|---|---|---|
| present | absent | - | **B** (a fresh install is pending) |
| present | present | - | **ROLLBACK to A** |
| absent | - | present | **B** (confirmed good) |
| absent | - | absent | **A** (default) |

- **The pessimistic marker**: "the B kernel writes bootfail_B
  pessimistically at boot and removes it when the system is fully
  up" (`ota_update_confirm_boot()` at kmain step 16c clears the
  bootfail marker). A slot that never reaches "fully up" keeps the
  marker -> the next boot rolls back - that is the whole
  crash-safety story.
- **Boot-attempt budget** (BUG-0305/A5-16): the GRUB env block
  `/boot/grub/grubenv` (1024 bytes, the `ab_attempts` variable),
  `OC_AB_MAX_ATTEMPTS 3`; the kernel resets the counter when A
  finishes booting or B confirms - a slot that reboots in a loop
  gives up after 3 tries and falls back.

## 3. The update flow

`update` (oc> update; the wizard walks the same ops):

1. **check** - fetch `update.json` (version + package_url +
   package_sha256 + package_size) over HTTP or HTTPS; compare
   version + build (the numeric build comparison, BUG-0347).
2. **download** - streamed to /data.
3. **verify** - package SHA256 against the manifest.
4. **gunzip + untar** - RFC 1952/1951 inflate (CRC32+ISIZE
   verified) + streaming ustar (checksum verification, GNU 'L'
   long names, pax skip) directly into the **inactive slot**.
5. **write boot flags** - print "reboot required".
6. **reboot** onto the new slot; rollback clears the flags.

A failed write never touches the running slot - the active system
keeps serving until the flip.

## 4. The 16 L1 ops

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

`update`'s hidden grammar: bare = the online flow; `status` /
`--status` = the A/B + version status view (prints the
`A/B disk: present` anchor); `--local <path>` / `local <path>` =
offline install from a package tar.gz (manifest checksum
verified; needs the A/B disk). `config`'s grammar:
`config list | get <key> | set <key> <value> | restore | path`
over `/etc/opencube.conf`.

Dependencies (the self-hosting proof): block
(`svc_block_drivers.h`, `svc_block_part.h`), fs (`svc_fs_vfs.h`),
net (`svc_net_core.h`, `svc_net_tls.h`), display
(`svc_display_lang.h` - `config set shell.lang` syncs the live
console language).

## 5. Error anchors (verbatim)

`update: status unavailable`, `update: download failed: `,
`update: cannot determine target slot (not booted from A/B?)`,
`update: install failed: `, `update: setting boot failed`,
`update: check failed: `, `rollback: failed`,
`config: cannot read /etc/opencube.conf`, `: key not found`,
`config: restore failed`.

The update wizard's decision anchors (the stable English output a
script would grep): "A/B disk: present", "New version
available.", "Current version is up to date.", "DHCP success",
"DNS resolution failed", "reboot required", "IP:".

## 6. The disk tooling

- `abdisk <dev>` (block op 20) creates the layout destructively
  and re-runs `ota_ab_rescan()` afterwards so the running kernel
  sees it immediately.
- `abcfg` (block op 23) shows the A/B boot configuration.
- `install <dev>` / `grub-install <dev>` (block ops 21/22) write
  the bootable system using the embedded GRUB payload
  (`tools/embed_grub.py` embeds boot.img + a grub-mkimage
  core.img as generated C arrays in the kernel - the "rule-9
  self-hosting" surface: bootable disks with zero external
  files).
- Host-side: `tools/make_ab_disk.sh` builds `build/abdisk.img`
  (p1 boot/flags 64M + slot A 128M + slot B 128M + data 128M,
  mtools-only, no loop, no root), `tools/make_update_pkg.sh`
  builds the OTA package (`manifest.json` with version + files +
  payload sha256 over concatenated payload bodies in archive
  order; no boot cfg member since WP05 - OCBL scans dynamically),
  `tools/update_server.py` serves the manifest for tests.

## 7. Verification

The unit family (ops 3-4, 7-16) covers each stage; the E2E
`tools/fix1_checkupdate_e2e.py` proves the shipped ISO's
checkupdate sees the real live feed and reports honestly
(version-inversion regression); the update wizard's steps 1-5 are
exercised by the mega-app smoke; `ab_partition_test` asserts the
flag semantics. The E2E update test builds a deliberately older
`OC_RELEASE_VERSION` override so the live feed looks newer.
