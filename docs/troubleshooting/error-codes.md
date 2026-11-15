<!-- SPDX-License-Identifier: Apache-2.0 -->
# Error codes — the complete numeric surface

Open Cube OS has **no global `errno`** and no `errno.h` — the
freestanding kernel does not link libc. The universal convention is:

- **0 = success, negative = failure** (`-1` generic; `Returns 0 on success, -1 on failure` is the documented idiom).
- Subsystems that need precision use **dense negative enums** — 8
  families, **61 codes total**, listed below with the trigger and the
  response for each.
- Sizes/lengths return `-1`; ring-buffer reads return `-1` for "past
  the window"; watchdog slot APIs return **-1 (table full) / -2 (bad
  arguments)**.
- The one positive sentinel family is the crash-source pseudo-vectors
  `OC_CRASH_SRC_*` (32..36), which ride the `vector` field of crashlog
  records.

Commands in this page: [dmesg](../manual/commands/dmesg.md),
[logcat](../manual/commands/logcat.md), [logctl](../manual/commands/logctl.md),
[crashlog](../manual/commands/crashlog.md), [health](../manual/commands/health.md),
[update](../manual/commands/update.md), [config](../manual/commands/config.md).

---

## Family 1 — OC_UPDATE_E_* (15 codes, update pipeline)

Defined in `services/selfhost/interface/svc_selfhost_update.h`; decoded
to text in `svc_selfhost_ab.c` / `svc_selfhost_update.c`. These are the
codes behind every `update: ... failed: <why>` line.

| Code | Value | Meaning | Trigger scenario | Response |
|------|-------|---------|------------------|----------|
| `OC_UPDATE_E_CONFIG` | -1 | config file missing or unreadable | `/etc/opencube.conf` absent on the /etc volume | check `config path`; is the /etc volume mounted? See [storage-issues.md](storage-issues.md) |
| `OC_UPDATE_E_PREFIX` | -2 | invalid URL prefix (must be http:// or https://) | `update_url` typo (`ftp://...`) | `config get update_url`, fix with `config set update_url http://...` |
| `OC_UPDATE_E_DNS` | -3 | DNS resolution failed | the update host does not resolve | `dns <host>`; check the DNS server from `dhcp`/`ip` output |
| `OC_UPDATE_E_CONNECT` | -4 | connect failed | server unreachable (routing/firewall) | `ping` the host, `route` shows the gateway, `firewall` list |
| `OC_UPDATE_E_TIMEOUT` | -5 | no response (timeout) | server accepted nothing | retry; check host reachability |
| `OC_UPDATE_E_HTTP` | -6 | HTTP error | server returned an error status | inspect the server side / URL path |
| `OC_UPDATE_E_JSON` | -7 | JSON parse failed | update.json malformed | fetch it manually with `wget` and read it |
| `OC_UPDATE_E_BUFSIZE` | -8 | response too large | changes list overflows the 128-byte buffer | server-side fix (shorten changes) |
| `OC_UPDATE_E_DISABLED` | -9 | online_update=no in the config | update disabled by config | `config set online_update yes` |
| `OC_UPDATE_E_NOAB` | -10 | no A/B disk present | not booted from the A/B layout | `update status`, then create it: `abdisk <dev>` (destructive) |
| `OC_UPDATE_E_SHA` | -11 | SHA256 verification failed | corrupted or tampered package | re-download; check `package_sha256` in update.json |
| `OC_UPDATE_E_TARGZ` | -12 | package is not valid gzip/tar | truncated download | re-download; verify size |
| `OC_UPDATE_E_IO` | -13 | filesystem I/O error | /data write failure | `df`, `fsck` the volume |
| `OC_UPDATE_E_SLOT` | -14 | invalid slot (use A or B) | internal slot math failed | run `abcfg`; report if persistent |
| `OC_UPDATE_E_ARGS` | -15 | NULL / malformed arguments | internal caller bug | report with the exact command line |

## Family 2 — OC_GZIP_ERR_* (13 codes incl. 0, gunzip)

`kernel/core_lib_gzip.h` (identical copy in `ocse/kernel/lib/lib_gzip.h`).
Surfaced by the OTA package path and by every gzipped registry program
at exec time (app inflation).

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `OC_GZIP_ERR_NONE` | 0 | success | — | — |
| `OC_GZIP_ERR_ARGS` | -1 | NULL pointer / bad capacity | API misuse | internal; report |
| `OC_GZIP_ERR_MAGIC` | -2 | not a gzip stream (1f 8b) | file is not gzip | re-download / re-compress the package |
| `OC_GZIP_ERR_METHOD` | -3 | CM != 8 (deflate) | non-deflate gzip | rebuild the package (`tools/make_update_pkg.sh`) |
| `OC_GZIP_ERR_HEADER` | -4 | malformed gzip header | truncation at the head | re-download |
| `OC_GZIP_ERR_BLOCK` | -5 | invalid block header / length check | mid-stream corruption | re-download; verify SHA256 first |
| `OC_GZIP_ERR_TABLE` | -6 | invalid Huffman table | corrupt deflate tables | as above |
| `OC_GZIP_ERR_DIST` | -7 | distance beyond window | corrupt back-reference | as above |
| `OC_GZIP_ERR_TRUNC` | -8 | stream ended before final block | truncated tail | as above |
| `OC_GZIP_ERR_CRC` | -9 | CRC32 mismatch in trailer | silent corruption | re-download (this is why OC_UPDATE_E_SHA is checked first) |
| `OC_GZIP_ERR_ISIZE` | -10 | ISIZE mismatch in trailer | size field wrong | as above |
| `OC_GZIP_ERR_SINK` | -11 | sink callback failed | output device errored | check disk space |
| `OC_GZIP_ERR_STATE` | -12 | API misuse (feed after finish, ...) | caller bug | internal; report |

## Family 3 — OC_TAR_ERR_* (7 codes, ustar)

`kernel/core_lib_tar.h` (+ ocse twin). The untar half of package install.

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `OC_TAR_ERR_ARGS` | -1 | NULL / bad arguments | API misuse | internal |
| `OC_TAR_ERR_HEADER` | -2 | malformed / short header block | corrupt archive | rebuild the package |
| `OC_TAR_ERR_CSUM` | -3 | header checksum mismatch | corrupt header | rebuild; `ustar bad checksum rejected` in the fixtures |
| `OC_TAR_ERR_SIZE` | -4 | file size field invalid | non-octal size field | rebuild |
| `OC_TAR_ERR_SINK` | -5 | file callback failed | target write error | disk full? `df` |
| `OC_TAR_ERR_STATE` | -6 | API misuse | caller bug | internal |
| `OC_TAR_ERR_TRUNC` | -7 | archive ended mid-entry | truncated package | re-download |

## Family 4 — OC_CONFIG_E_* (5 codes, /etc/opencube.conf)

`kernel/core_lib_config.h` (+ ocse twin). Behind `config get/set` and
the config backend.

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `OC_CONFIG_E_ARGS` | -1 | NULL / bad arguments | API misuse | internal |
| `OC_CONFIG_E_NOFILE` | -2 | /etc/opencube.conf missing or unreadable | no /etc volume, or first boot | `config path`; mount /etc; `config restore` rebuilds defaults |
| `OC_CONFIG_E_NOKEY` | -3 | key not found (or empty value) | typo'd key | `config list` shows valid keys |
| `OC_CONFIG_E_TOOLONG` | -4 | value does not fit the output buffer | oversized value | shorten the value |
| `OC_CONFIG_E_IO` | -5 | VFS read/write error | volume error | `fsck`, remount |

## Family 5 — ENC_E_* (5 codes, GB2312/UTF-8 conversion)

`services/encoding/interface/svc_encoding_core.h` (+ ocse twin). Used
by the CJK console layer and `serial_encoding`.

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `ENC_E_INVAL` | -2 | NULL pointer / zero capacity | API misuse | internal |
| `ENC_E_TRUNC` | -3 | input ends mid-sequence | truncated multibyte input | check the sender's encoding (`serial_encoding`) |
| `ENC_E_ILLEGAL` | -4 | byte sequence not valid in the charset | GBK bytes read as UTF-8 or vice versa | `serial_encoding utf8\|gbk` to match the peer |
| `ENC_E_NOSPACE` | -5 | output buffer too small | huge conversion | internal |
| `ENC_E_UNMAP` | -6 | codepoint has no mapping in the target | rare hanzi without GB2312 mapping | unavoidable; the font layer falls back |

## Family 6 — X509_E_* (8 codes + OK, certificate verification)

`services/crypto/interface/svc_crypto_x509.h`; decoded in
`svc_crypto_x509.c`. These decide whether HTTPS connections live.

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `X509_OK` | 0 | verified | chain anchored in the embedded root store (ISRG Root X1 etc.) | — |
| `X509_E_FORMAT` | -1 | bad certificate format | malformed DER | server-side fix |
| `X509_E_UNSUPPORTED` | -2 | unsupported algorithm | non-RSA chain | server-side fix |
| `X509_E_EXPIRED` | -3 | certificate expired | stale server cert | `rtc` (is the guest clock sane?); then server-side |
| `X509_E_NOT_YET` | -4 | certificate not yet valid | clock in the past | `rtc` |
| `X509_E_NO_ROOT` | -5 | issuer certificate missing | self-signed or unanchored chain | REFUSED by default (the security model); explicit opt-in only via `config set tls.insecure 1` for testing |
| `X509_E_BADSIG` | -6 | signature verification failed | tampered cert | treat as hostile; do not bypass |
| `X509_E_HOSTNAME` | -7 | hostname mismatch | cert for another name | fix the URL or the cert |
| `X509_E_NO_ISSUER` | -8 | (reserved/unknown) | decode-table fallback prints `unknown error` | report |

## Family 7 — OC_USB_E* (7 codes, USB transfers)

`drivers/usb/drv_usb_core.h` (+ ocse twin). Every USB transfer returns
one of these.

| Code | Value | Meaning | Trigger | Response |
|------|-------|---------|---------|----------|
| `OC_USB_ETIMEDOUT` | -100 | transfer timed out | stalled device | replug; `usb` to re-enumerate |
| `OC_USB_ENODEV` | -101 | no device | device detached | replug; `usb_hotplug_test` reproduces detach path |
| `OC_USB_ESTALL` | -102 | endpoint stalled | device refused the request | device-side; `usb_storage_test` shows CSW state |
| `OC_USB_EIO` | -103 | I/O error | bus error | check the controller (`usb`, `lspci`) |
| `OC_USB_ENOMEM` | -104 | out of memory | allocation failure | `mem`/`free`; report |
| `OC_USB_EINVAL` | -105 | invalid argument | API misuse | internal |
| `OC_USB_ENAK` | -106 | NAK (not ready yet) | device busy | retry |

## Family 8 — OC_POWER_E_UNSUPPORTED (1 code)

`services/power/interface/svc_power_core.h` (+ ocse twin): `-1` —
"machine still running after all ports". Returned when the power-off
walk tried every port (ACPI S5, port 0x64, CF9) and the machine is
still alive; the user sees `Power off not supported. System halted.` —
use the host's power control in a VM.

### Decoded text tables (where the strings live)

Each family has a twin decode-to-text table next to the call site —
the natural grep anchors: `svc_selfhost_ab.c` (update),
`svc_crypto_x509.c` (x509), the `put()` strings in the fs/block/net
services. The SMBus i801 status words decode to `NACK/device error`,
`bus error/collision`, `transaction failed`, `timeout`.

---

## The 11 kernel_panic call sites

Panic reasons are **composed**, not literal (built with
strcpy/strcat/u64_to_str on a stack buffer), so grep for the constant
prefix, not the whole string.

| # | Site | Reason string (format) | Trigger |
|---|------|------------------------|---------|
| 1 | `arch_exceptions.c:203` | passthrough `<reason>` | the arch fallback used when the crash service is not yet registered — prints `KERNEL PANIC (no crash handler installed): <reason>` + `System halted.` |
| 2 | `core_sched.c:658` | `kernel stack bounds violated on switch (task=<name>, rsp=0x<hex> window=[0x<lo>,0x<hi>])` | the scheduler is about to switch to a task whose saved RSP is outside its stack window (corruption/overflow); it refuses to resume on foreign memory |
| 3 | `svc_crash_core.c:563` | `watchdog timeout: no heartbeat for <N>s (task=<name>)` | the software crash-watchdog saw no heartbeat for OC_WDT_TIMEOUT_MS (15 s default; armed only after boot, tick registered but DISARMED by default to avoid false panics during long shell commands) |
| 4 | `svc_crash_watchdog.c:451` | `watchdog timeout: <N>s without a feed (task=<name>)` | a registered watchdog slot with policy PANIC exceeded its budget (e.g. `watchdog test panic`) |
| 5 | `svc_diag_l1.c:261` | `manual crashtest: kernel_panic()` | the `crashtest panic` command |
| 6 | `ocse/core_watchdog.c:450` | (twin of #4) | OCSE installer kernel watchdog |
| 7 | `ocse/core_sched.c:613` | (twin of #2) | OCSE scheduler |
| 8 | `ocse/core_crash.c:567` | `watchdog timeout: no shell heartbeat for <N>s` | OCSE soft watchdog |
| 9 | `arch_exceptions.c:438+` (ocos) | `KERNEL STACK OVERFLOW (guard page hit, task=<name>)` | #PF whose CR2 is inside a kthread guard page |
| 10 | (twin) | `KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)` | #DF with RSP at/below the task stack base or ≥ 0x40000000000 |
| 11 | (both trees) | `ring-0 exception <N> (<#XX Name>)` | any other unhandled ring-0 exception |

Banner-only halts (no panic record): `PANIC: idle task stack allocation failed - halting` (sched init OOM — both kernels).

### The unified panic banner (what prints, in order)

On serial (authoritative) + VGA (best effort):

1. `*** NESTED PANIC INSIDE PANIC - HALTING ***` — one line, then halt
   (never recurses).
2. #DF special case: `*** KERNEL PANIC: <reason> [task=<name>] ***` +
   `source: #DF Double Fault err=0x<hex> rip=0x<hex> handled at dispatcher depth]` + `core dump: skipped in #DF mode (see KNOWN_ISSUES)` + policy line.
3. CR3 normalization; klog ring FATAL record; crashlog record (no text).
4. `*** KERNEL PANIC: <reason> ***` (+ ` [KERNEL STACK OVERFLOW: exhausted, task=<name>]` annotation) + kernel version/build + source
   line (`manual/panic|watchdog-timeout|assertion-failed|contained(TUI)| kstack-overflow|exception <N>`) + task + registers + traceback.
5. VGA one-liner (reason truncated to 90 chars) +
   `Registers and call trace on the serial console.`
6. Core dump status: `core dump: UNAVAILABLE (no boot-time buffer)` or
   `<N> bytes captured, persisted to /var/crash/core.dump` (or serial
   fallback / `persist FAILED`).
7. L1 panic-handler chain: `*** panic handler claimed recovery - resuming ***` (rare — the TUI crash guard is the one user).
8. Log flush status: `crash log: <n> bytes to /var/log/crash.log; klog flushed <n> bytes to /var/log/kernel.log` (or FAILED variants).
9. Policy: `policy: HALT|REBOOT - system stopped (safe halt loop)| - rebooting NOW`.

### Crash-source pseudo-names (crashlog `vector` field 32..36)

`manual/panic`, `watchdog-timeout`, `assertion-failed`,
`contained(TUI)`, `kstack-overflow`, fallback `exception`. The x86
vector names in records: `#DE Divide Error`, `#DB Debug`, `#BP Breakpoint`, `#OF Overflow`, `#BR Bound Range Exceeded`, `#UD Invalid Opcode`, `#NM Device Not Available`, `#DF Double Fault`, `#TS Invalid TSS`, `#NP Segment Not Present`, `#SS Stack-Segment Fault`, `#GP General Protection`, `#PF Page Fault`, `#MF x87 FPU Error`, `#AC Alignment Check`, `#MC Machine Check`, `#XM SIMD FP Exception`, `#SX Security Exception`, fallback `Unknown`.

---

## The 30 `_Static_assert` guards

All compile-time layout contracts — a drift **fails the build**, never
the runtime. If you hit one, the two sides of a cross-module ABI
stopped agreeing:

| Guard | Where | Contract |
|-------|-------|----------|
| `ocst_u32 must be 4 bytes` / `ocst_state_t layout drifted` / `ocst_record_t layout drifted` | `include/ocbl_state.h` | the OCBL↔OCOS↔OCIRE cross-boot mailbox (0x98000/0x9B000) |
| `regs struct layout drift` | `ocbl/bios/ocbl_bios.h` | BIOS register frame |
| `ehdr layout` / `phdr layout` | `ocbl/common/ocbl_load.c` | ELF headers OCBL parses |
| `task_t.fpu_state offset must match OFF_FPU in arch_context_switch.S` (+ `fpu_saved`, `rflags` twins) | `ocos/core_sched.c` **and** `ocse/core_sched.c` (3 each) | scheduler↔asm context-switch ABI |
| `registry empty` | `ocos/core_usermode.c` | the app registry compiled in |
| `chinese_font gz layout mismatch - regenerate chinese_font_data.c` | `services/font/svc_font_chinese.c` | the embedded CJK font blob |
| 8 UHCI schedule-page overlap guards: `ISO TD overlaps int QH`, `int QH overlaps ctrl QH`, `ctrl QH overlaps ctrl TDs`, `ctrl TD pool overlaps bulk QH`, `bulk QH overlaps bulk TDs`, `bulk TD pool overlaps int TDs`, `int TD pool overlaps scratch`, `scratch overflows schedule page` | `drivers/usb/drv_usb_core.c` **and** the ocse twin (8 each) | the UHCI frame-list layout math |

---

## Log formats — reading the output

### klog (dmesg / logcat)

```
[<seconds>.<mmm padded to 3>] <LEVEL padded to 6> <module>: <message>
```

Example: `[    12.345] ERROR  watchdog: watchdog slot 3 (net-rx) -> KILL task`.

Levels: `DEBUG` 0, `INFO` 1, `WARN` 2, `ERROR` 3, `FATAL` 4. The ring
holds 1024 entries; overwrites are counted into `klog_dropped()` and
`dmesg` shows the footer `-- <shown> lines (ring <n>/1024, dropped <d>) --`. Filter with `logcat -l <level>` (debug..fatal or 0-4), `-m <module>`, `-k <keyword>`, `-t <seconds>`, `-n <count>`; follow with
`-f`. See [silent-failures.md](silent-failures.md) for level-filtering
traps.

### Boot stage log

```
[HH:MM:SS.mmm] <stage>: OK|FAIL
```

Quiet level 1 suppresses non-FAIL lines; any `FAIL` substring marks a
failure for the boot-state judge. Stage names (the OCST ladder):
`BOOT`, `KM_AIN_ENTERED`, `SCHED_READY`, `DRIVERS_READY`,
`SERVICES_READY`, `L1_LAUNCHED`, `L1_READY`, runtime marker 255 =
`RUNTIME`.

### Health alarms (klog WARN class)

```
alarm: memory >90% used (<used>/<total> KiB)
alarm: <n> active tasks (>64)
alarm: CPU >=90% busy for 10 consecutive seconds
```

The `health` command is the first thing to run when the machine "feels
wrong" — see [silent-failures.md](silent-failures.md).

### Crash records

`crashlog` lists the 32-entry ring (fields: jiffies, rtc_epoch, vector,
ring, error_code, rip, rsp, cr2, tid, pid, task[16]); flushed to
`/var/log/crash.log`; core dumps land in `/var/crash/core.dump`; klog
flushes to `/var/log/kernel.log`.

## Related pages

- [error-catalog.md](error-catalog.md) — every message string by
  subsystem, with investigation chains
- [silent-failures.md](silent-failures.md) — failures that print
  nothing at all
- [../manual/hidden-commands.md](../manual/hidden-commands.md) — the
  test stubs that exercise these paths on purpose
- [../arch/crash-recovery.md](../arch/crash-recovery.md) — the panic
  machinery design
