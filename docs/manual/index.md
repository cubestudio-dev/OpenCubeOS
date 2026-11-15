<!-- SPDX-License-Identifier: Apache-2.0 -->
# User manual

Everything an operator needs to run Open Cube OS 3.0.0.

The command surface is organized around **eight merged apps** -
`update`, `disk-tool`, `net-tool`, `sound-tool`, `files`, `sysdiag`,
`hwinfo`, `power`. Each one is an intent wizard *and* a verbatim
dispatch for every historical command underneath it. The 284-entry
program registry keeps all the classic single commands available
byte-for-byte.

| Page | What it covers |
|------|----------------|
| [getting-started](getting-started.md) | download, boot in QEMU, the first five minutes |
| [mega-apps](mega-apps.md) | the 8 merged apps: wizard / direct / classic contract |
| [shell-basics](shell-basics.md) | ush: pipes, redirection, env, aliases, scripting |
| [filesystems](filesystems.md) | VFS types, formatting, mounting, the /etc volume |
| [network](network.md) | address setup, WiFi/Bluetooth, firewall, TLS + SSH |
| [update-ota](update-ota.md) | the 5-step update wizard, install, rollback |
| [boot-modes](boot-modes.md) | BIOS + UEFI, the GRUB A/B slot menu, persistence |
| [tui-guide](tui-guide.md) | the desktop, apps, keyboard model |
| [chinese-console](chinese-console.md) | the CN layer, IME, the ASCII contract |
| [recovery](recovery.md) | the WinRE workflow, crash artifacts, rescue kit |
| [commands](commands/index.md) | ALL 284 command reference pages (every command home: L0, SVC, built-ins) |

## The 8 merged apps at a glance

| App | Intent | Classic ops it wraps |
|-----|--------|----------------------|
| `update` | bring the machine to the newest version | the OTA chain: status, checkupdate, install, rollback |
| `disk-tool` | disks, partitions, filesystems | lsblk, parted, mkfs.*, mount family, fsck, A/B tools |
| `net-tool` | connectivity and address management | ip, dhcp, dns, route, arp, firewall, wifi, bt, ssh |
| `sound-tool` | audio devices and playback | sound, volume, play, per-driver status + selftests |
| `files` | day-to-day file work | ls, tree, df, du, cp, mv, rm, edit, stat, chmod |
| `sysdiag` | "something feels wrong - look" | health, dmesg, memory/scheduler stats, selftests |
| `hwinfo` | what hardware is in this machine | uname, clocks, PCI, ACPI, SMBus, USB, cameras |
| `power` | shutdown / reboot / halt / sleep | the power ops plus their path selftests |

Every app follows the same three-door contract:

```
ush> <app>              the intent wizard (menus, checks, confirmations)
ush> <app> direct <op>  explicit verbatim passthrough of one op
ush> <app> <op> [args]  classic dispatch - identical to the historical command
```

Details and per-app walkthroughs: [mega-apps](mega-apps.md).
