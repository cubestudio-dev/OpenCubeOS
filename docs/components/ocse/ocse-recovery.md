<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE: install failures and recovery

What happens when an install goes wrong, and why an aborted install
does not leave a half-written system behind.

## The error page

Every OCSECL command the wizard runs returns a status.  The first
failing command stops the wizard: the error page shows

- the **command line** that failed (copied verbatim), and
- the **captured output** of that command (the same text the shell
  would have printed),

plus **Retry** and **Back**.  Retry re-runs the failed stage; Back
returns to the page before it.  Nothing else on the machine is
touched while the error page is up.

## Failure modes and handling

| Where | Failure | Handling |
|---|---|---|
| Step 2 | target disk already boots `opencube.elf` | refused before any write - the existing system stays untouched |
| Step 3 | no free gap large enough | the wizard says so; nothing is written |
| Stage 1 | `mount` fails on a stale mount from an earlier failed retry | the wizard drops the stale mount (`umount /mnt`) and retries the mount **once** before giving up |
| Stage 2 | medium check, recovery-partition carve, extract, boot code or boot flag fails inside `install` | the sub-step's own FATAL text names the failing point (`extraction failed` / `/boot/opencube.elf missing after extraction` / `ocbl-install failed` / `could not set the boot flag`); the boot code is written only after a fully successful extraction that verified `/boot/opencube.elf` exists |
| Stage 3/4 | a config value or the user record does not fit | refused with a clear error (bounded builders - never truncated, never overflowed) |
| Stage 5/6 | `sync`/`umount` fail | retried via the error page; the installed files are already on disk, so a later manual `sync` from OCSECL finishes the job |

## Recovering by hand with OCSECL

Because the wizard is only a controller over OCSECL, a failed install
can be inspected and completed by hand from the shell:

```
lsdisk                       pick the target
lspart hd0                   what did the wizard create?
mount hd0p2 /mnt             mount the installed partition
ls /mnt                      inspect what was written
cat /mnt/etc/opencube.conf   the config keys that made it
config set hostname mybox    finish any missing key
sync                         flush
umount /mnt                  done - reboot
```

`hwcheck` re-runs the four hardware checks at any time - the first
thing to try when disks misbehave.

## When the target already has a system

The existing-system refusal is the outer guard: OCSE never formats,
repartitions or installs over a partition that already boots
`opencube.elf`.  To reinstall over an old Open Cube OS on purpose,
delete the old partition first (`rmpart <disk> <n> confirm`) or pick
a different target - both are explicit, confirmed actions.

## What `install` writes, in order

Inside stage 2 the `install` command runs its own fixed sequence,
aborting at the first FATAL: an optional dedicated OCIRE recovery
partition carve (when the layout calls for one, with
`boot/ocire.elf` on the system volume), the extraction of the
payload tar (verified by the presence of `/boot/opencube.elf`), the
OCBL boot code (`ocbl-install` - image validity, room before the
first partition, stage1 + core verify), and finally the boot flag on
the created partition.

## After a successful install

The final page offers **reboot**.  The installed disk carries: the
OCOS payload, the OCBL boot code (`ocbl-install` ran inside
`install`), the boot flag on the created partition, the config keys
and the user record.  On the A/B recovery-disk layout, the recovery
disk is prepared separately (see the OCBL pages) - OCSE's `install`
targets the plain single-system layout.

See also: [OCBL troubleshooting and status codes](../ocbl/ocbl-troubleshooting.md)
for what the boot loader reports on the first boots of the installed
system.
