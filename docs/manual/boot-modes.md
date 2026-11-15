<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot modes

## BIOS (legacy)

The default: GRUB i386-pc from the ISO, loading the multiboot2
kernel with its embedded module chain. The same loader is what
`install` / `abdisk` write to a disk, so an installed system boots
exactly like the ISO it came from.

## UEFI

The ISO carries an EFI boot leg; OVMF boots it under QEMU:

```
qemu-system-x86_64 -m 512M -bios OVMF.fd -cdrom opencube-3.0.0.iso \
    -boot d
```

On real hardware pick the EFI entry in the firmware boot menu. The
A/B slot logic (below) is identical in both modes.

## The A/B slot menu

On an A/B disk, GRUB reads the boot flag and offers:

- the **active slot** (default, auto-selected),
- **force-slot-A** and **force-slot-B** entries - the manual
  override when the active slot misbehaves.

The flag lives on the A/B disk; `update status` shows which slot is
running and which boots next, and `update rollback` flips it from
inside the system.

## Persistence

An ISO boot runs from RAM: everything resets on reboot. For a
persistent system:

1. `install <disk>` - partition + copy the running kernel + GRUB +
   /etc volume, single-system shape; or
2. `abdisk <disk>` (via `disk-tool`'s A/B manager) - the two-slot
   update-disk shape, which OTA updates target.

Attach a config volume (`etc.img`, a FAT32 disk with
`opencube.conf` and optionally `ssh_host_key`) to carry settings
and host keys across reboots without installing.

## Boot self-tests

Every boot runs the self-test chain before the shell:

```
exception self-test: 3/3
boot complete....... OK
```

A boot that reaches `ush>` has already proven its exception stack,
timer and console. The regression gate asserts the same lines plus
a live framebuffer check (BUG #66), so "it booted" is a tested
statement, not a hope.
