<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Watchdog (1)

### watchdog

| Field | Value |
|---|---|
| Name | watchdog |
| Category | Watchdog |
| Function | watchdog control |
| Usage | `watchdog [subcommand ...]; watchdog [status|slots|hw ...|start|stop|feed|timeout|register|test] ; watchdog hw status|start|stop|feed|timeout ; watchdog hw timeout <2..63 seconds> ; watchdog register <name> <ms> <0=panic 1=reboot 2=kill 3=log> ; watchdog start|stop|feed <id> ; watchdog timeout <id> <ms 100..3600000>` |
| Parameters | `name`, `ms`, `id` |
| Return codes | 0, 1 |
| Source | `ocos/services/watchdog/src/svc_watchdog_l1.c` |

**Executed example**:

```
watchdog
watchdog status:
  hardware: none
watchdog slots (2):
  [0] system-heartbea  15000 ms  PANIC  stopped  timeouts=0
  [1] kernel-health  15000 ms  PANIC  ARMED  timeouts=0
ush> 
```
