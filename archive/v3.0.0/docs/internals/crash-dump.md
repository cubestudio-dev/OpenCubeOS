<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: crash dumps

`kernel/core/core_crash.c` + the exception path in
`arch_exceptions.c`.

## The panic sequence

1. The exception stub builds the frame (vector, RIP, registers,
   CR2/CR3 where relevant).
2. The dumper prints it to serial - the exact lines you need for a
   report.
3. A crashlog record goes to /etc - survives the reboot.
4. The machine halts (or the hardware watchdog resets it if one was
   armed - see [watchdog.md](watchdog.md)).

## After reboot

```
oc> crashlog      (the record list)
oc> dumpinfo      (the decoded dump: vector, fault address, registers)
```

## Why IST matters here

#DF (double fault) and #MC run on IST1 - their own stack - so a
fault during a fault handler still reaches the dumper. The
exception-exit path never context-switches on IST2 (BUG-0139): a
context switch on an IST would corrupt the exception frame of the
next nested fault.

## Testing it

`crashtest` = deliberate panic, then `dumpinfo` proves the round
trip. The regression battery checks `crashlog` on every run.
