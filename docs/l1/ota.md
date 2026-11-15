<!-- SPDX-License-Identifier: Apache-2.0 -->
# The OTA / A-B service from the app side

The update machinery lives in the selfhost service
(`ocos/services/selfhost/`); apps reach it through the L1SVC "ota"
category and the config store.

## The L1SVC surface

| Op | Command | What it does |
|----|---------|--------------|
| 1 | `checkupdate` | fetch + parse the manifest, compare (numeric build compare, BUG-0347) |
| 2 | `config` | list / get / set / restore / path - the /etc/opencube.conf store |
| 5 | `update` | `status` / `--local <pkg>` / bare = the online chain |
| 6 | `rollback` | flip the boot flag to the other slot |
| 7-16 | `update_pkg_test` .. `real_update_test` | the in-guest test battery |

`update status` prints the machine picture the wizards probe:

```
update: current version: 3.0.0-...
update: current boot: ISO          (or slot A / slot B)
update: next boot: (default)
update: available: none (up to date)
update: online update: enabled
update: A/B disk: present          <- the wizard anchor
```

## The chain (what the online flow does)

check (manifest from the configured `update_url`) -> download ->
verify (manifest SHA256 enforced inside install) -> install onto the
*inactive* slot -> set boot -> "reboot required".

Offline: `update --local /path/to/package.tar.gz` runs the same
install path from an attached volume.

## The A/B disk

Two slots + boot flag, created by `abdisk <dev>` (or the
`disk-tool` A/B manager wizard). GRUB reads the flag; the menu
offers force-slot entries. `abcfg` shows the exact grub.cfg those
commands write. The install target is the inactive slot
(BUG-0110: `update_target_slot()` decides, not the caller).

## The config keys (/etc/opencube.conf)

```
update_url=https://cubestudio-dev.github.io/OpenCubeOS/update.json
package_url=https://github.com/cubestudio-dev/OpenCubeOS/releases/latest
auto_check=no
online_update=yes
shell.lang=en
```

`config get update_url` is what the update wizard's DNS step parses
(scheme -> host) before resolving it.

## The mega-app

`update` (no args) = the 5-step wizard; every other argument shape
is the verbatim op. See the user manual's
[../manual/update-ota.md](../manual/update-ota.md) for the operator
view and [wizard-library.md](wizard-library.md) for the anchor
discipline.

## In-guest tests

`ab_partition_test`, the `update_*_test` battery,
`checkupdate_test`, `config_test` - all classic ops of the sysdiag/
update tables, all in the registry.
