<!-- SPDX-License-Identifier: Apache-2.0 -->
# VFS and the filesystem family

## The VFS contract

`fs/fs_vfs.{c,h}`: one API - open/read/write/seek/close/stat/mkdir/
rmdir/readdir/lookup - over registered filesystem types. A type
registers mount/unmount hooks plus file_ops and dir_ops. The global
node tree resolves paths and crosses mount points transparently.

Real behaviours: per-fs case sensitivity in the node cache, enforced
permission bits (mode 0000 refuses), bitwise write-access checks,
i64 seek, `umount /` refused, mount shadowing without duplicate
children, bounded `list_mounts`.

## FAT32 (`fs/fs_fat32.c`)

LFN with alias generation + collision scan; checksum and sequence
validation on read; cross-cluster LFN deletion with backtrack; dual-
FAT mirror maintained; FSINFO read + maintained + persisted with the
next-free hint.

## exFAT (`fs/fs_exfat.c`)

Active-FAT tracking, upcase table handling, cluster chains.

## ext4 (`fs/fs_ext4.c`)

Extent-based reads and writes, block groups, inode tables.

## ramfs (`fs/fs_ramfs.c`)

The volatile root filesystem; sparse holes read back as zeros.

## The /etc volume

A 32 MiB FAT32 image mounted at /etc (config, SSH keys, logs, crash
dumps) - built by the Makefile from `etc/opencube.conf`. It is the
persistence story for an otherwise in-memory system.

## Testing

`fstest` (the dual-sided assertion suite), `fsck`, `fatstat`,
`fs_mount_test`, plus the host-side harnesses in `tests/` (FAT
structure parsing with ASAN). The regression battery runs fstest on
every boot.
