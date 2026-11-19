<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Power management (9)

### halt

| Field | Value |
|---|---|
| Name | halt |
| Category | Power management |
| Function | halt the machine |
| Usage | `halt` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
halt
```

(Note: power-real; qemu-eof=False)

### halt_test

| Field | Value |
|---|---|
| Name | halt_test |
| Category | Power management |
| Function | halt path test |
| Usage | `halt_test` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
halt_test
halt_test: power_halt() = 'System halted.' + cli;hlt loop (kernel/power.c)
halt_test: this test must return, so it does NOT halt; the live halt is verified by running 'halt' in a dedicated QEMU session
halt_test: live halt not observed (destructive) : SKIP - see the dedicated halt session
ush> 
```

### poweroff

| Field | Value |
|---|---|
| Name | poweroff |
| Category | Power management |
| Function | power off (ACPI) |
| Usage | `shutdown / poweroff` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
poweroff
```

(Note: power-real; qemu-eof=True)

### poweroff_test

| Field | Value |
|---|---|
| Name | poweroff_test |
| Category | Power management |
| Function | poweroff path test |
| Usage | `poweroff_test` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
poweroff_test
(the machine powered off - QEMU exited)
```

(Note: restored from the f5af2ab6 capture (command behavior unchanged since))

### reboot

| Field | Value |
|---|---|
| Name | reboot |
| Category | Power management |
| Function | reboot the machine |
| Usage | `reboot` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
reboot
[ocst] clean-reboot marker recorded
```

(Note: power-real; qemu-eof=True)

### reboot_test

| Field | Value |
|---|---|
| Name | reboot_test |
| Category | Power management |
| Function | reboot path test |
| Usage | `reboot_test` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
reboot_test
reboot_test: block-device flush (reboot pre-step) executed
reboot_test: reset path = 8042 controller (0x64, 0xFE) + ACPI RESET_REG fallback (0xCF9)
reboot_test: this test must return, so it does NOT reset; the live reset is verified by 'reboot' (boot banner reappears)
reboot_test: pre-step flush executed : PASS
reboot_test: live reset not observed (destructive) : SKIP - see the dedicated reboot session
ush> 
```

### shutdown

| Field | Value |
|---|---|
| Name | shutdown |
| Category | Power management |
| Function | power off the machine |
| Usage | `shutdown / poweroff` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
shutdown
```

(Note: power-real; qemu-eof=True)

### sleep

| Field | Value |
|---|---|
| Name | sleep |
| Category | Power management |
| Function | suspend for N seconds |
| Usage | `sleep <sec>` |
| Parameters | `sec` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
sleep 1
ush> 
```

### suspend_test

| Field | Value |
|---|---|
| Name | suspend_test |
| Category | Power management |
| Function | S3 suspend test |
| Usage | `suspend_test` |
| Source | `ocos/services/power/src/svc_power_l1.c` |

**Executed example**:

```
suspend_test
suspend_test: ACPI S3 available (sleep mask bit3 set, slp_typ resolves)
suspend_test: S3 SLP_TYP value: 1 (live cycle verified in the dedicated QEMU suspend/wake session)
suspend_test: suspend path present and wired : PASS
ush> 
```
