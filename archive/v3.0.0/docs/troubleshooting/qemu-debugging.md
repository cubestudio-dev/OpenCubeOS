<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU debugging techniques

## Capture everything

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none \
    -netdev user,id=n1 -device e1000,netdev=n1
```

The serial log IS the boot narrative - one `OK` per subsystem.

## Watch the machine, not just the guest

```
-d int,cpu_reset,guest_errors -D qemu.log    (interrupts/resets/errors)
-monitor stdio                               (QEMU monitor alongside)
```

## GDB into the kernel

```
qemu-system-x86_64 ... -s -S        # gdb stub, frozen at reset
gdb build/opencube_base.elf          # the pre-strip ELF has symbols
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

`build/opencube_base.elf` is kept by `make kernel` precisely for
this - the final `build/opencube.elf` is stripped.

## Deterministic replays

The build is reproducible - the same tree produces byte-identical
ELFs, so a crash you can reproduce twice is the same bytes both
times. Combine with QEMU's own `-icount` for timing stability in
network tests.

## Device matrix at a glance

The QEMU flags that light up each driver family are tabulated in the
[driver manuals](../drivers/storage.md) - attach exactly the device
you're debugging.
