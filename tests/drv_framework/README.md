# Verification virtual drivers (WP-08-driver-framework)

Everything in this folder is TEST SCAFFOLDING, not kernel drivers
and not hardware support.  They exist to exercise the driver
framework chain (registration -> ordering -> probe -> work ->
remove -> hotplug release) end to end, on every boot, with zero
QEMU or real-hardware dependency in the framework itself.

| file               | driver(s)    | role                                            |
|--------------------|--------------|-------------------------------------------------|
| drv_vtest_bus.c    | vtest-bus    | simulated bus: enumerates nodes, posts events, cascades removal |
| drv_vtest_ctl.c    | vtest-ctl    | device half: depends on the bus, type-matched auto-bind, PM hooks |
| drv_vtest_mon.c    | vtest-mon    | hotplug observer: subscribes with a type filter, counts deliveries |
| drv_vtest_fail.c   | vtest-fail   | failure injection: probe always returns -55 (isolation canary) |
| drv_vtest_cycle.c  | vtest-cyc-a/b| declared dependency cycle (detection canary)   |

Build integration: the Makefile compiles every `*.c` here into the
kernel image unconditionally (KERNEL_DIRS entry `tests/drv_framework`),
so adding a NEW verification driver is one new file - no table edit
anywhere (the WP-08 zero-global-edit rule, kept testable).
