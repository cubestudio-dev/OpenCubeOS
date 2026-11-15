<!-- SPDX-License-Identifier: Apache-2.0 -->
# A/B slots, install, and OTA updates

## The update wizard

The intended path is the wizard - it walks the whole "bring this
machine to the newest version" intent in five checked steps:

```
ush> update
```

| Step | What it does | What it can fix |
|------|--------------|-----------------|
| 1/5 network | reads the IP status | a 0.0.0.0 address offers a DHCP fix, then rechecks |
| 2/5 A/B disk | reads `update status` | absent disk: shows `lsblk`, walks you through a double-confirmed `abdisk <dev>`, rechecks |
| 3/5 DNS | resolves the update host parsed from the live `config get update_url` | - |
| 4/5 check | `checkupdate` | up-to-date ends the wizard OK |
| 5/5 install | confirmed online update | confirmed reboot into the new slot |

Every step prints its verdict (`[OK]` / `[!!]`) and aborts cleanly
when its precondition cannot be repaired - nothing destructive runs
without a bare `y`.

## The A/B scheme

The selfhost service maintains two slots (A and B) plus a boot flag:

- GRUB reads the flag and boots the active slot; the menu offers
  force-slot entries as the manual override.
- An update installs onto the *inactive* slot while you keep running.
- The next boot flips to the updated slot; `update rollback` flips
  back if you reject it.

`update status` shows the whole picture: current version, current
boot slot, next boot, available update, online-update flag, and
whether the A/B disk is present.

## Installing to a disk

From a running ISO boot:

```
ush> dskstat                (pick a target disk, e.g. hd0)
ush> install hd0            (partition + copy the running kernel +
                             GRUB boot files + /etc volume)
```

or through the wizard: `disk-tool` -> A/B update disk manager ->
create (the device name is asked, retyped and confirmed).

`install` copies the multiboot2 module `self` - literally the kernel
ELF that is running right now - onto the disk, so the installed
system always matches its installer. `abdisk <dev>` does the same
shaped as an A/B update disk (two slots + flags; destroys data).
`abcfg` shows the exact grub.cfg files those commands write.

## Checking for updates

```
ush> checkupdate            (reads the update URL from /etc/opencube.conf
                             - http or https - and parses the manifest)
ush> config get update_url  (the configuration key it uses)
```

The manifest is the website's `update.json`: version, build, package
URL, package SHA256, size. The build comparison is numeric
(BUG-0347), so a rebuild with a higher build number wins even when
the version string is equal.

## Applying an update

The online flow (what the wizard's step 5 runs after confirmation,
and what bare `update direct` does):

```
ush> update direct          check -> download -> verify -> install
ush> reboot                 boot the updated slot
```

Offline / air-gapped machines:

```
ush> update --local /path/to/package.tar.gz
```

The OTA package (`opencube-<version>-update.tar.gz`) carries the new
kernel ELF plus a manifest. Verification is cryptographic: the
manifest SHA256 is enforced inside the install - a package that
fails it is refused.

## Rollback

If a slot misbehaves after a flip:

1. Pick the other slot from the GRUB menu (force entry), or
2. `ush> update rollback` from inside a running system.

`ab_partition_test` exercises slot discovery, flags and slot
switching on a scratch disk without touching your real boot disk.

## Update-related commands

`checkupdate`, `update` (wizard + the chain above), `rollback`,
`config` (update URL keys), `abdisk`, `abcfg`, `ab_partition_test`.
All of them are also classic ops of the `update` / `disk-tool`
apps. Full list with one page each:
[commands index](commands/index.md) - "OTA updates and A/B slots"
domain.

## Internals

For how the flags are stored, how install stages the inactive slot,
and the boot-flag handshake with GRUB, see
[../internals/ota-ab.md](../internals/ota-ab.md).
