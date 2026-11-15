<!-- SPDX-License-Identifier: Apache-2.0 -->
# Filesystems

## The VFS layer

The kernel VFS fronts four filesystem implementations:

| Type | Read | Write | Where it shines |
|------|------|-------|-----------------|
| ramfs | yes | yes | the ISO root - everything in RAM |
| FAT32 | yes | yes | interchange with every OS |
| exFAT | yes | yes | large removable volumes |
| ext4 | read* | no* | inspecting Linux disks |

(*) the ext4 leg is mount/read-oriented in this release.

## Day-to-day file work

```
ush> files                  the wizard: ls / tree / df+du / cp / mv /
                            rm (confirmed) / edit / stat+chmod
ush> files edit /etc/opencube.conf
```

or the classic programs directly - `ls`, `tree`, `df`, `du`, `cp`,
`mv`, `rm`, `edit`, `stat`, `chmod`, `cat`, `mkdir`, `ln`, `wc`,
`grep`, `head`, `tail`, `sort`, `awk`. They are ordinary registry
programs; `files` is a second door into the same binaries.

## Formatting and mounting

`disk-tool` walks the whole flow:

```
ush> disk-tool              -> list devices (lsblk)
                           -> partition table (parted)
                           -> create a filesystem (mkfs.fat32 / exfat /
                              ext4 - the device name is asked, retyped
                              and confirmed)
                           -> mount manager (list / mount / umount /
                              fatmount / fatstat)
```

Classic form:

```
ush> mkfs.fat32 hd0p1       (destroys data - no wizard, no extra prompt)
ush> mount fat32 hd0p1 /mnt
ush> umount /mnt
```

`fatmount` is the FAT-specific convenience (auto-detects the FAT
volume), `fatstat` reports FAT statistics, `fsck` checks a
filesystem.

## The /etc volume

`/etc/opencube.conf` carries the persistent configuration (update
URLs, auto-check, online-update, shell language). On an ISO boot it
lives in RAM and resets on reboot; on an installed system (or when
an `etc.img` disk is attached) it persists:

```
ush> config list
ush> config get update_url
ush> config set online_update no
ush> config restore         (back to defaults)
```

## Paths and the current directory

`cd`, `pwd`, `ls` behave like their Unix namesakes; paths are `/`
separated, absolute from the ramfs root. The editor writes back in
place; `cp`/`mv`/`rm`/`chmod`/`stat` take the obvious shapes
(`chmod <octal> <path>`).

## What is where on a running system

```
/            ramfs root (the ISO content)
/etc         the config volume (persisted when etc.img is attached)
/mnt, /media conventional mount points
/bin, /lib   the embedded program registry is in-kernel; these
             paths exist for the dynamic-linker leg (ld.so, libfoo)
```
