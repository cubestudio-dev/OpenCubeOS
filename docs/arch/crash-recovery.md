<!-- SPDX-License-Identifier: Apache-2.0 -->
# Crash recovery architecture

## The crash path

`kernel/core/core_crash.c`: an unhandled exception captures the frame
(registers, vector, fault address, CR2/CR3 context), prints the dump
to serial, writes a crashlog record to the /etc volume, and halts.
`dumpinfo` reads the record back after reboot; `crashlog` lists the
history.

## Panic-grade isolation

- IST1 gives #DF/#MC their own stack - a double fault during a
  fault handler still has somewhere to run.
- The exception-exit path never context-switches on IST2 (BUG-0139).
- The boot self-test (#DE/#UD/#PF caught, kernel alive) runs BEFORE
  the shell - the machinery is proven before anything trusts it.

## The recovery posture

The system is its own WinRE: boot from ISO with no disk, inspect a
sick machine's volumes, fix configs, re-install boot code
(`install`/`abdisk`/`grub-install` carry their payload inside the
kernel), pull logs and dumps off /etc. The recovery manual with the
rescue table: [../manual/recovery.md](../manual/recovery.md).

## Testing

`crashtest` (deliberate panic), `dumpinfo` (read-back), the
regression battery's crashlog check, and the E2E hard-reset scenarios
(S12/S13) which prove the *reboot* half of the story through the
hardware watchdogs.
