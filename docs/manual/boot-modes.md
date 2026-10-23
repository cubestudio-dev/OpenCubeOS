<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot modes: BIOS, UEFI, and the A/B slot menu

## The ISO is hybrid

`build/opencube.iso` (the release asset) carries two boot paths:

- **Legacy BIOS**: an El Torito boot image (GRUB i386-pc, built by
  `tools/build_iso.sh` with `grub-mkimage` + `xorriso`).
- **UEFI**: an EFI boot image (`x86_64-efi`) on the same ISO.

Both paths load the same kernel ELF and attach the kernel itself as a
multiboot2 module tagged `self` - this is the mainstream installer
pattern: the `install` and `abdisk` commands copy *the exact kernel
that is currently running* onto a target disk, so an installed system
always matches its installer.

## The GRUB menu

```
* Open Cube OS (slot ISO)
  Open Cube OS (slot A, force)
  Open Cube OS (slot B, force)
```

- **slot ISO** - the default. Boot the kernel from the boot medium and
  read the A/B boot flag to decide which disk slot to chain (falls
  back to the ISO kernel when no flag exists).
- **slot A / slot B force** - ignore the flag and boot that disk slot
  directly. This is the recovery path when a bad update leaves the
  active slot unbootable: pick the other slot from the menu.

The menu auto-boots the highlighted entry after 5 seconds.

## What the kernel does after GRUB

Boot order (this is exactly the serial log):

```
multiboot2 handshake OK
long mode entry OK
framebuffer 800x600x32 OK
console grid / 8x16 font engine OK
IDT + GDT + TSS (256 gates) OK
8259 PIC remap (IRQ0-15 -> vec 32-47) OK
PIT @ 100 Hz + tick counter OK
PS/2 keyboard OK
COM1 serial RX OK
console input line editor OK
interrupts enabled (sti) OK
exception self-test: 3/3 (#DE/#UD/#PF) OK
PMM / VMM / heap OK
... drivers, VFS, network ...
shell command system (233 commands) OK
boot complete OK
crash recovery armed (panic + dump + crashlog) OK
```

The multiboot2 parser (`kernel/arch/x86_64/arch_multiboot2.c`) reads
the framebuffer tag, the memory map, the command line and the ACPI
RSDP tags (types 14/15) that GRUB forwards from firmware - on UEFI
boots the RSDP comes through this path, on BIOS boots the ACPI driver
can also scan memory.

## Boot arguments

GRUB passes the kernel command line; `grub.cfg` uses it to pass
`BOOT_SLOT=ISO|A|B` plus console preferences. Inside a running system,
`uname -a` shows the version and build, and `abcfg` prints the exact
grub.cfg files that `abdisk`/`install` generate.

## Persistence: the /etc volume

The system looks for a FAT32 volume to mount at `/etc`. When you boot
the plain ISO there is none - configuration is ephemeral. `install`
creates one on the target disk (it holds `opencube.conf`, SSH host
keys, `kernel.log*`, crash dumps). If you only want persistence
without installing, create the volume by hand:

```
oc> abdisk <dev>       (A/B disk with /etc volume - destroys data!)
```

## Booting real hardware

Write the ISO to a USB stick (`dd if=opencube.iso of=/dev/sdX` or your
favourite tool). The requirements are modest: x86_64 CPU, 512 MiB RAM.
Boot through BIOS or UEFI firmware as you prefer - both images are on
the stick.
