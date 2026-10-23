<!-- SPDX-License-Identifier: Apache-2.0 -->
# A/B slots, install, and OTA updates

## The A/B scheme

`kernel/ota/ota_ab.c` maintains two slots (A and B) plus a boot flag:

- GRUB reads the flag and boots the active slot; the menu offers
  force-slot entries as the manual override.
- An update installs onto the *inactive* slot while you keep running.
- The next boot flips to the updated slot; `update rollback` flips
  back if you reject it.

## Installing to a disk

From a running ISO boot:

```
oc> dskstat                (pick a target disk, e.g. ata0)
oc> install ata0           (partition + copy the running kernel +
                            GRUB boot files + /etc volume)
```

`install` copies the multiboot2 module `self` - literally the kernel
ELF that is running right now - onto the disk, so the installed
system always matches its installer. `abdisk <dev>` does the same
shaped as an A/B update disk (two slots + flags; destroys data).

`abcfg` shows the exact grub.cfg files those commands write.

## Checking for updates

```
oc> checkupdate            (reads the update URL from /etc/opencube.conf
                            - http or https - and parses the manifest)
oc> config get update_url  (the configuration key it uses)
```

The manifest is the website's `update.json`: version, build, package
URL, package SHA256, size.

## Applying an update

```
oc> update download        (fetch the OTA package)
oc> update verify          (SHA256 + signature check)
oc> update install a       (install onto slot A/B - the inactive one)
oc> reboot                 (boot the updated slot)
```

The OTA package (`opencube-<version>-update.tar.gz`) carries the new
kernel ELF plus a manifest. Verification is cryptographic: hash plus
signature - a package that fails either is refused.

## Rollback

If a slot misbehaves after a flip:

1. Pick the other slot from the GRUB menu (force entry), or
2. `oc> update rollback` from inside a running system.

`ab_partition_test` exercises slot discovery, flags and slot
switching on a scratch disk without touching your real boot disk.

## Update-related commands

`checkupdate`, `checkupdate_test`, `update` (the chain above),
`ab_partition_test`, `abcfg`, `abdisk`, `config` (update URL keys).
The full list with one page each:
[commands index](commands/index.md) - "OTA updates and A/B slots"
domain.

## Internals

For how the flags are stored, how install stages the inactive slot,
and the boot-flag handshake with GRUB, see
[../internals/ota-ab.md](../internals/ota-ab.md).
