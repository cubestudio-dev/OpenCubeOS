<!-- SPDX-License-Identifier: Apache-2.0 -->
# The service surface map

Every L1SVC category, what it fronts, and the apps that wrap it.
The numbering contract is `ocos/interface/l1svc_ops.h` (mirrored
from `ocos/services/sysapi/interface/svc_sysapi_l1svc.h`; renumber
upstream only by appending, then regenerate).

## Categories

| Cat | Name | Fronts | Mega-app home |
|-----|------|--------|---------------|
| 1 | time | RTC / HPET / PM timer / TSC status | hwinfo |
| 2 | block | disks: status, partition, mkfs, fsck, A/B tools, tests | disk-tool |
| 3 | nic | NIC families + wireless (wifi/bt) | net-tool |
| 4 | snd | audio devices, volume, play, per-driver tests | sound-tool |
| 5 | power | shutdown/reboot/halt/sleep + path tests | power |
| 6 | ota (selfhost) | update chain, checkupdate, config store, A/B | update |
| 7 | fs | mount/umount/fatmount/fatstat/tree | disk-tool, files |
| 8 | net | ip/dhcp/dns/arp/route/firewall/ssh/sshd | net-tool |
| 9 | health | snapshot / export / watch | sysdiag |
| 10 | klog | dmesg / logcat / logctl | sysdiag |
| 11 | watchdog | the watchdog command family | sysdiag |
| 12 | crypto | cryptotest / dhtest | (classic only) |
| 13 | l1 | the registry listing itself (help -a/-w), l1test, ldd | - |
| 14 | display | uname/stats/lang/font/encoding/IME ops | hwinfo |
| 15 | input | touchpad / touchtest | hwinfo |
| 16 | camera | uvc/scsi/gameport/parallel/ir + tests | hwinfo |
| 17 | usb | usb status/dev + the usb test family | hwinfo |
| 18 | ocire | the boot-volume log ring (guard-only ops) | - |
| 19 | diag | memory/scheduler/vm tests, PCI/ACPI/I2C/SMBus, crash | sysdiag |
| 20 | tui | the desktop + TUI command family | (tui apps) |

## Beyond L1SVC: the direct syscalls

| Syscall | Used by |
|---------|---------|
| `SYS_OPEN/READ/WRITE/CLOSE/STAT/READDIR` | the file tools (cat, cp, ls, ...) |
| `SYS_FORK/EXECVE/WAIT4` | ush, run, files - the program-launcher pattern |
| `SYS_PIPE/DUP/DUP2` | ush pipes and redirection |
| `SYS_READLINE` (73) | wizard line input (echo + backspace, blocking) |
| `SYS_GETCH` (74) | non-blocking single keys (menus, the editor) |
| `SYS_NETCMD` (101) | ping / wget / ifconfig / netstat bridge |
| `SYS_PS` (102) | ps / top process tables |

The full x86_64 syscall table is registered in
`ocos/services/sysapi/src/svc_sysapi_syscall.c`.

## The output contract

Service ops return **English ASCII text** through the caller's
buffer - these lines are the script-facing contract and the wizard
probes:

```
"IP: 10.0.2.15"            (net ip)
"DHCP success: IP=..."     (net dhcp)
"A/B disk: present"        (ota update status)
"New version available."   (ota checkupdate)
"Current version is up to date."
"reboot required"          (ota update, after install)
```

Renaming any of them is a breaking change: the wizards
(`wizard_lib.h` `wiz_find`), the E2E suites and the user docs all
read them.

## help -a / help -w

The registry listing (L1SVC cat "l1" op 1) renders name + wp tag +
bilingual hint per program. `help -w` groups by tag; the `mega`
group (the 8 merged apps) leads the view, followed by the 18
historical service groups.
