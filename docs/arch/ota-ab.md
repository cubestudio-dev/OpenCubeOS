<!-- SPDX-License-Identifier: Apache-2.0 -->
# OTA and A/B architecture

## Slots and flags

`kernel/ota/ota_ab.{c,h}`: two slots + a boot flag + per-slot state
(successful / booting / failed). GRUB's menu scripts read the flag;
`update` writes it after a staged install; `rollback` flips it back.

## The update chain

`kernel/ota/ota_update.{c,h}`: check (manifest fetch + parse),
download (http/https to a file), verify (SHA256 + signature),
install (onto the inactive slot), set_boot (flag), rollback, and
status queries. The L1 surface mirrors it 1:1
(`l1_ext_check_update`, `l1_ext_update_download`, ...).

## The manifest

`update.json` on the release site: version, build, time, changes,
package URL, package SHA256, package size. `checkupdate` reads the
URL from `/etc/opencube.conf` and parses exactly this.

## The package

`opencube-<version>-update.tar.gz`: the new kernel ELF + manifest.
Deterministic within the build day (the manifest embeds the UTC
date); the ELF itself is fully reproducible.

## In-guest tests

`ab_partition_test`, `update_*_test` (the whole chain, packaged as
commands), `checkupdate_test` (URL/JSON units + live probe), and the
release-gate E2E that walks check->download->verify->install->reboot
for real.
