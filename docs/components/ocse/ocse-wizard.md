<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE: the 9-step install wizard and the OCSECL command dictionary

The installer offers two doors onto the **same** command surface:
the graphical wizard (the default) and the OCSECL shell.  The wizard
implements **no install logic of its own** - every mutating action it
performs is a real OCSECL command line executed through
`ocse_cl_exec`, which is exactly what the shell would run.  A failed
command shows its captured output on the error page with
Retry/Back.

## The nine steps

| # | Page | What happens |
|---|------|--------------|
| 1 | Welcome + language | language pick (English / Simplified Chinese); the whole UI, wizard included, follows the choice live |
| 2 | Target disk | `lsdisk` lists the install-target disks; a disk that already boots `opencube.elf` is refused (the existing-system check) |
| 3 | Partitioning | `lspart <disk>` lists partitions and free gaps; the wizard preselects the **largest free gap** and builds `mkpart <disk> <startMiB> <endMiB>`; Advanced options can delete a partition (`rmpart ... confirm`) |
| 4 | Formatting | `mkfs.fat32 <device> confirm` - FAT32 is the OCBL boot-partition contract; the page explains that formatting destroys the data on that partition only |
| 5 | User account | name, full name and password; the password is hashed to a 64-hex SHA-256 **before it leaves the page** - the record written later is `name:full:sha256`, never a plain password |
| 6 | System configuration | hostname, language, update URL, automatic update check |
| 7 | Review | everything above, one screen, before anything is written |
| 8 | Install | the real command sequence, stage by stage (below) |
| 9 | Done | finish or reboot into the installed system |

## Step 8: the six install stages

Each stage is a real command line; the progress bar tracks the
stages, and the first failure jumps to the error page carrying the
failed command and its captured output.

| Stage | Command (built) | Notes |
|---|---|---|
| 1. Mount | `mount <device> /mnt` | a stale mount from a failed retry is dropped (`umount /mnt`) and the mount retried once |
| 2. Install | `install <disk> <n> confirm` | the full install state machine: medium check, FAT gate, payload extract, boot code, boot flag |
| 3. Config | `config set hostname <h>` then `config set lang <en|zh>`, optional `config set update_url <u>`, `config set auto_check <0|1>` | four keys on the installed config |
| 4. User record | `write /mnt/etc/users <name>:<full>:<sha256>` | the SHA-256 password hash; a record that cannot fit is refused, never truncated |
| 5. Sync | `sync` | flush the block cache |
| 6. Unmount | `umount /mnt` | the target is left clean |

## The OCSECL command dictionary (40 commands)

The shell is always reachable from the panel (hotkey) and from the
serial console.  `help` lists everything; `help <cmd>` shows one
command.

| Command | Purpose |
|---|---|
| `help` | show all commands or one command's help |
| `lsdisk` | list install-target disks |
| `lspart <disk>` | list partitions of a disk |
| `parted <disk>` | show the raw partition table |
| `mkpart <disk> <startMiB> <endMiB>` | create a partition |
| `rmpart <disk> <n> confirm` | delete partition #n (needs `confirm`) |
| `setflag <disk> <n> boot [on\|off]` | set a partition flag |
| `mkfs.fat32 <device> confirm` | format FAT32 (DESTROYS DATA; needs `confirm`) |
| `mkfs.exfat <device> confirm` | format exFAT (DESTROYS DATA; needs `confirm`) |
| `mkfs.ext4 <device> confirm` | format ext4 (DESTROYS DATA; needs `confirm`) |
| `mount <device> <path>` | mount (filesystem auto-detected) |
| `umount <path>` | unmount the filesystem at path |
| `ls [path]` | list a directory |
| `cd [path]` | change directory (default `/`) |
| `pwd` | print the working directory |
| `cat <file>` | print a text file |
| `write <file> [text ...]` | write one line |
| `touch <file>` | create an empty file |
| `cp <src> <dst>` | copy a file |
| `mv <src> <dst>` | move or rename a file |
| `rm <file>` | delete a file |
| `mkdir <path>` | create a directory |
| `rmdir <path>` | delete an empty directory |
| `tar -t <arc>` / `tar -x <arc> [dir]` | ustar list / extract |
| `dd if= of= [bs= count= skip= seek=] [confirm]` | block copy |
| `sha256sum <file>` / `<dev> <sectors>` | SHA-256 digest of a file or a disk span |
| `install <disk> <n> confirm` | install OCOS onto a partition |
| `ocbl-install <disk> confirm` | write the OCBL boot code |
| `config [set <k> <v>]` | show/edit the installed config |
| `sync` | flush the block cache to the disks |
| `reboot` | flush disks and reboot |
| `shutdown` | flush disks and power off |
| `hwcheck` | re-run the four hardware checks |
| `disks` | raw disk inventory |
| `lang [en\|zh]` | show or set the language |
| `serial_encoding [utf8\|gbk]` | show/set the serial line encoding |
| `clear` | clear the screen |
| `click <x> <y>` | probe the GUI button geometry (diagnostics) |
| `gui` | switch back to the graphical panel |
| `halt` | stop the CPU (no ACPI path) |

Next: [install failures and recovery](ocse-recovery.md).
