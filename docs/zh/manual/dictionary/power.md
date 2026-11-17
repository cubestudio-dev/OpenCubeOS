<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 电源管理 (9)

### halt

| 字段 | 值 |
|---|---|
| 名称 | halt |
| 分类 | 电源管理 |
| 功能 | 停机 |
| 用法 | `halt` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
halt
```

(备注: captured before the power transition)

### halt_test

| 字段 | 值 |
|---|---|
| 名称 | halt_test |
| 分类 | 电源管理 |
| 功能 | 停机路径测试 |
| 用法 | `halt_test` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
halt_test
halt_test: power_halt() = 'System halted.' + cli;hlt loop (kernel/power.c)
halt_test: this test must return, so it does NOT halt; the live halt is verified by running 'halt' in a dedicated QEMU session
halt_test: live halt not observed (destructive) : SKIP - see the dedicated halt session
ush> 
```

### poweroff

| 字段 | 值 |
|---|---|
| 名称 | poweroff |
| 分类 | 电源管理 |
| 功能 | 关机（ACPI） |
| 用法 | `shutdown / poweroff` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
poweroff
(the machine powered off - QEMU exited)
```

(备注: power action: the console goes silent as the machine powers off)

### poweroff_test

| 字段 | 值 |
|---|---|
| 名称 | poweroff_test |
| 分类 | 电源管理 |
| 功能 | 关机路径测试 |
| 用法 | `poweroff_test` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
poweroff_test
(the machine powered off - QEMU exited)
```

(备注: power action: the console goes silent as the machine powers off)

### reboot

| 字段 | 值 |
|---|---|
| 名称 | reboot |
| 分类 | 电源管理 |
| 功能 | 重启 |
| 用法 | `reboot` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
reboot
[ocst] clean-reboot marker recorded
```

(备注: power-real; qemu-eof=True)

### reboot_test

| 字段 | 值 |
|---|---|
| 名称 | reboot_test |
| 分类 | 电源管理 |
| 功能 | 重启路径测试 |
| 用法 | `reboot_test` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | shutdown |
| 分类 | 电源管理 |
| 功能 | 关机 |
| 用法 | `shutdown / poweroff` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
shutdown
(the machine powered off - QEMU exited)
```

(备注: power action: the console goes silent as the machine powers off)

### sleep

| 字段 | 值 |
|---|---|
| 名称 | sleep |
| 分类 | 电源管理 |
| 功能 | 休眠 N 秒 |
| 用法 | `sleep <sec>` |
| 参数 | `sec` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
sleep 1
ush> 
```

### suspend_test

| 字段 | 值 |
|---|---|
| 名称 | suspend_test |
| 分类 | 电源管理 |
| 功能 | S3 挂起测试 |
| 用法 | `suspend_test` |
| 来源 | `ocos/services/power/src/svc_power_l1.c` |

**实际执行示例**：

```
suspend_test
suspend_test: ACPI S3 available (sleep mask bit3 set, slp_typ resolves)
suspend_test: S3 SLP_TYP value: 1 (live cycle verified in the dedicated QEMU suspend/wake session)
suspend_test: suspend path present and wired : PASS
ush> 
```
