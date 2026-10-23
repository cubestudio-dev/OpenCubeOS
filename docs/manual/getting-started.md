<!-- SPDX-License-Identifier: Apache-2.0 -->
# Getting started

From zero to an `oc>` prompt in five minutes.

## 1. Download

Grab the current release ISO from the
[downloads page](https://cubestudio-dev.github.io/OpenCubeOS/) or the
[GitHub releases](https://github.com/cubestudio-dev/OpenCubeOS/releases):

- `opencube-<version>.iso` - hybrid BIOS+UEFI bootable image
  (about 12 MiB)

Verify the SHA256 against the value printed on the download page and
in the release note:

```
sha256sum opencube-<version>.iso
```

## 2. Boot it in QEMU

```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 \
    -serial mon:stdio
```

What you will see:

1. The GRUB menu: *Open Cube OS (slot ISO)* plus force-slot entries
   (A / B) - see [boot-modes](boot-modes.md). It auto-boots in 5 s.
2. The kernel boot log: one `OK` line per subsystem as it comes up
   (multiboot2 handshake, long mode, framebuffer, IDT, PIT, keyboard,
   serial, PMM, VMM, heap, drivers, VFS, network, shell).
3. The banner: `Open Cube OS <version> ready. Type 'help' for commands.`
4. The prompt: `oc> `

UEFI instead of legacy BIOS:

```
qemu-system-x86_64 -m 512 -machine q35 \
    -bios /usr/share/ovmf/OVMF.fd \
    -cdrom opencube-<version>.iso -display none -serial mon:stdio
```

## 3. First commands

```
oc> help              (short command list)
oc> help -a           (all 233, alphabetical)
oc> help -w           (grouped by work package)
oc> uname -a          (version + build + tree)
oc> dskstat           (block devices found)
oc> dhcp              (bring up networking)
oc> ping 10.0.2.2     (the QEMU gateway)
oc> wget 10.0.2.2     (fetch the gateway's index page)
oc> dns example.com   (resolve a name through the QEMU DNS forwarder)
oc> health            (CPU / memory / tasks / IO at a glance)
oc> dmesg             (kernel ring buffer)
```

The full reference with every command on its own page:
[commands](commands/index.md).

## 4. Keyboard

- The shell line editor supports history (up/down), `Ctrl+U` line kill,
  and editing within the line.
- On real hardware a PS/2 keyboard is driven directly; over serial
  (COM1) every keystroke you type into `-serial mon:stdio` lands in the
  same input queue.
- The TUI layer (`tui` command) is keyboard-driven: menus, forms and
  the desktop launcher - see [tui-guide](tui-guide.md).

## 5. Files

The boot system ships a `ramfs` (in-memory filesystem). Files you
create vanish on reboot unless you put them on a mounted volume:

```
oc> dskstat                    (list devices: ata0, usda..., nvme0...)
oc> mkfs.fat32 ata0 confirm    (optional: format a scratch disk)
oc> fatmount ata0 /mnt         (FAT32; exfat/ext4 equivalents exist)
oc> write /mnt/hello.txt "hi"  (a persistent file)
oc> cat /mnt/hello.txt
```

The `/etc` volume is a small persistent FAT32 image the system mounts
automatically when present - it carries `opencube.conf`, SSH host
keys, the kernel log and crash dumps.

## 6. Where to go next

- [shell-basics](shell-basics.md) - pipes, redirection, env, aliases,
  scripting
- [network](network.md) - the full networking walk-through
- [boot-modes](boot-modes.md) - A/B slots and what the GRUB menu does
- [update-ota](update-ota.md) - updating a running system
- [recovery](recovery.md) - the WinRE-style recovery workflow
- Building from source: [../dev/build.md](../dev/build.md)
