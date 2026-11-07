<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: OTA/A-B mechanics

`kernel/ota/ota_ab.{c,h}` + `ota_update.{c,h}`.

## Slot state machine

Each slot carries: kernel image, grub.cfg stanza, state (booting /
successful / failed). The boot flag selects the active slot; GRUB's
menu reads it (the ISO entry consults it, the force entries
override it).

## Install onto the inactive slot

`update install <slot>` writes the verified package's kernel onto
the INACTIVE slot and sets the boot flag. You keep running on the
current slot until reboot - a failed write never touches your
running system.

## First boot after flip

The new slot boots in "booting" state; a successful userspace start
marks it "successful" (the E2E walks this); if it never confirms,
the flag falls back - that, plus the manual force entries, is the
rollback story.

## Verification chain

Manifest (update.json) -> package SHA256 -> signature. All three
must pass before install touches a slot; failures refuse with the
stage named.

## The in-kernel GRUB payload

`tools/embed_grub.py` embeds boot.img + a grub-mkimage-built
core.img (the module set the on-disk grub.cfg needs) into the kernel
as generated C data - that is how `install`/`abdisk`/
`grub-install` write bootable disks with zero external files
(the "rule-9 self-hosting" surface).
