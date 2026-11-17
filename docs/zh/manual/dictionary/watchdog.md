<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 看门狗 (1)

### watchdog

| 字段 | 值 |
|---|---|
| 名称 | watchdog |
| 分类 | 看门狗 |
| 功能 | 看门狗控制 |
| 用法 | `watchdog [subcommand ...]; watchdog [status|slots|hw ...|start|stop|feed|timeout|register|test] ; watchdog hw status|start|stop|feed|timeout ; watchdog hw timeout <2..63 seconds> ; watchdog register <name> <ms> <0=panic 1=reboot 2=kill 3=log> ; watchdog start|stop|feed <id> ; watchdog timeout <id> <ms 100..3600000>` |
| 参数 | `name`, `ms`, `id` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/watchdog/src/svc_watchdog_l1.c` |

**实际执行示例**：

```
watchdog
watchdog status:
  hardware: none
watchdog slots (2):
  [0] system-heartbea  15000 ms  PANIC  stopped  timeouts=0
  [1] kernel-health  15000 ms  PANIC  ARMED  timeouts=0
ush> 
```
