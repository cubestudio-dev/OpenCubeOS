<!-- SPDX-License-Identifier: Apache-2.0 -->
# Common errors and their fixes

## Build

| Symptom | Cause | Fix |
|---------|-------|-----|
| `grub-mkimage: cannot open ... unicode.pf2` | font lives in `grub-common` | install it / point OC_TOOLS at a tree with `usr/share/grub/unicode.pf2` |
| `src-zip: not a git checkout` | no git metadata | build from a clone or use `make dist` |
| `error: operand size mismatch for 'setc'` | pre-fix tree on new GAS | update past commit fc4e5a5 (u8 flag) |
| packed-member warnings on new GCC | pre-fix tree | update past fc4e5a5 (packed tag types) |
| `embed_grub: missing /usr/usr/lib/grub...` | pre-fix script | update past fc4e5a5 (dual-layout probe) |

## Boot

| Symptom | Meaning | Next step |
|---------|---------|-----------|
| No `multiboot2 handshake OK` | GRUB didn't hand off a valid mbi | check the ISO booted the right entry; capture serial |
| Boot stops at a non-OK line | that subsystem failed | the file implementing it - read its init error path |
| Triple-fault/reset loop | early exception before the dump path works | `-d int,cpu_reset -D qemu.log` + gdb via `-s -S` |
| GRUB menu boots wrong slot | stale A/B flag | force the other slot from the menu, then `update rollback` |

## Runtime

| Symptom | Meaning | Next step |
|---------|---------|-----------|
| `mount` refuses a valid FAT32 volume | BPB check failed | `fatstat <dev>` shows the parsed BPB; wrong device? |
| NIC silent | driver didn't bind | `lspci` + the family command (`e1000`...) |
| TCP connects then stalls | checksum/refactor regression | `tcpstats`, `tcptest` |
| `usda` missing | USB MSC didn't bind | `usb`, `usb_storage_test` |
| fsck reports dirty after clean unmount | FSINFO/FAT mirror regression | report it - this class is a bug |

## Testing

- SSH E2E needs the seeded /etc volume (`host_keygen_etc`).
- HTTPS E2E needs the reference server reachable.
- The regression suite needs pexpect; the SSH suite paramiko.
