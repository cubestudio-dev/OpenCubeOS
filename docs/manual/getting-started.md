<!-- SPDX-License-Identifier: Apache-2.0 -->
# Getting started

## Boot the ISO

The quickest path is QEMU with the release ISO:

```
qemu-system-x86_64 -m 512M -cdrom opencube-3.0.0.iso -boot d \
    -netdev user,id=n1 -device e1000,netdev=n1
```

Add `-vga std` for the graphical console (the TUI desktop needs it),
or run headless with `-display none -serial mon:stdio`. On UEFI
hosts add `-bios OVMF.fd` (see [boot-modes](boot-modes.md)).

The boot self-tests run, then the ring-3 shell greets you:

```
Open Cube OS User-space Shell (3.0.0)
ush>
```

## The first five minutes

The 8 merged apps are the intended surface - start with a wizard and
watch it work:

```
ush> hwinfo            what hardware is in this machine
ush> sysdiag           health snapshot, kernel log, memory picture
ush> net-tool          address setup, WiFi scan, connectivity tests
ush> update            the 5-step OTA wizard (network -> A/B -> DNS
                        -> check -> install)
```

Press a digit to pick a menu item, `q` to quit. Destructive paths
make you retype the target name and answer a bare `y`.

Two quick read-only tours:

```
ush> disk-tool             disks, partitions, mounts
ush> files                 browse, copy, edit
```

## Classic commands still work

Every historical command remains registered - the interface-freeze
rule guarantees it (commands are added, never removed; the registry
holds 284 programs):

```
ush> lsblk                 same service call as disk-tool lsblk
ush> checkupdate           same as update's step 4
ush> help                  shell built-ins + registry summary
ush> help -a               every registry entry, A-Z, tagged
ush> help -w               grouped by domain (the mega group first)
```

## Files

```
ush> files                 the wizard: ls / tree / df+du / cp / mv /
                            rm / edit / stat+chmod
ush> files edit /etc/opencube.conf      the full-screen editor
ush> cat /etc/opencube.conf              quick read
ush> cd /etc && ls
```

The ISO runs from RAM (ramfs root); attach a disk and use
`disk-tool` to partition + format + mount for persistence
([filesystems](filesystems.md)).

## Network

With the user-mode NIC above:

```
ush> dhcp                  or net-tool -> "obtain an address via DHCP"
ush> ping 10.0.2.2         the QEMU gateway
ush> dns example.com
ush> wget example.com 80 /
```

TLS and SSH are built in: `wget` negotiates HTTPS, `sshd` serves the
shell, `ssh` connects out ([network](network.md)).

## Shut down

```
ush> power                 the wizard (confirmed shutdown/reboot)
ush> poweroff              the classic one-shot
```

## Where to go next

- [mega-apps](mega-apps.md) - the wizard / direct / classic contract
- [shell-basics](shell-basics.md) - pipes, redirection, scripting
- [update-ota](update-ota.md) - the update wizard and the A/B scheme
- [chinese-console](chinese-console.md) - `lang zh` and the IME
