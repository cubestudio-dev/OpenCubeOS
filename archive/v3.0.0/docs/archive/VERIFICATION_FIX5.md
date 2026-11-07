<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-09-fix5 Verification (real outputs)

Environment: QEMU (toolchain 10.0.13 at /home/z/opt/extract), BIOS (SeaBIOS
via GRUB El Torito) and UEFI (OVMF) boots, `-drive if=ide,...,file=build/etc.img`
attached as the FAT32 /etc volume. Test server: `tools/update_server.py`
(HTTP 8008 / HTTPS 8443 on the QEMU user network, host = 10.0.2.2).
All kernel-side output is ASCII.

## 1. Build

```
$ make clean && make            # -Wall -Wextra -Werror
(compile log ends with)
ld -n -nostdlib -T linker.ld ... -o build/opencube.elf ...
strip --strip-debug build/opencube.elf
0 errors, 0 warnings.

$ make iso
ISO: build/opencube.iso (El Torito BIOS + UEFI dual boot)
```

## 2. Boot evidence

BIOS:

```
vfs: mounted fat32 at /etc
[00:00:00.180] system config (/etc/opencube.conf, fat32)......... OK
[00:00:00.270] shell command system (83 commands)................ OK
[00:00:00.500] boot complete..................................... OK
Open Cube OS WP-09 ready. Type 'help' for commands.
```

UEFI (OVMF): same four markers verified present
(`kmain entered` / `system config (/etc/opencube.conf, fat32)` /
`boot complete` / `WP-09 ready`), `uname -a` → `Open Cube OS WP-09 x86_64`.

Command count is now **83** (79 before + checkupdate, config, config_test,
checkupdate_test; `edit` counted inside file_cmds registration).

## 3. Test matrix (input → expected → actual → verdict)

### T1 config_test (7/7 PASS)

Input: `config_test` after `config restore`.
Expected: write/read/validation/get_all/delete/restore all PASS.

```
[config_test] write+read custom key: PASS
[config_test] write auto_check=YES (normalized to yes): PASS
[config_test] reject invalid auto_check=maybe: PASS
[config_test] reject non-ASCII value: PASS
[config_test] get_all contains test_key and update_url: PASS
[config_test] delete file -> read reports missing: PASS
[config_test] restore defaults: PASS
[config_test] 7/7 PASS
```

Verdict: **PASS**.

### T2 checkupdate_test (6/6 PASS, HTTP context)

```
[checkupdate_test] https:// prefix -> TLS: PASS
[checkupdate_test] http:// prefix -> HTTP: PASS
[checkupdate_test] ftp:// prefix rejected: PASS
[checkupdate_test] JSON manifest parses: PASS
[checkupdate_test] broken JSON rejected: PASS
[checkupdate_test] live probe (new version available): PASS
[checkupdate_test] 6/6 PASS
```

Verdict: **PASS**.

### T3 update_same_version (HTTPS)

Input: `config set update_url https://10.0.2.2:8443/update.json`,
server returns `{"version": "WP-09", "time": "2026-10-15", ...}`.
Expected: "Current version is up to date." + Version + Time.

```
checkupdate: reading /etc/opencube.conf
[tls] handshake complete (TLSv1.2 DHE-RSA-AES128-SHA256, cipher 0x0067)
Current version is up to date.
Version: WP-09
Time: 2026-10-15
```

Server side: `[srv] TLS handshake OK: TLSv1.2 cipher=('DHE-RSA-AES128-SHA256', ...)`,
`[srv] request: GET /update.json HTTP/1.0`. Verdict: **PASS**.

### T4 update_new_version (HTTP)

Input: `update_url=http://10.0.2.2:8008/update.json`, server returns
`{"version": "WP-10", "time": "2026-10-30", "changes": "driver optimization + config file"}`.
Expected: "New version available." + Version + Time + Changes.

```
checkupdate: reading /etc/opencube.conf
New version available.
Version: WP-10
Time: 2026-10-30
Changes: driver optimization + config file
```

Server side: `[srv] response sent (200, 90 bytes)`. Verdict: **PASS**.

### T5 auto_check=yes (boot auto check, non-blocking)

Input: `config set auto_check yes`, reboot with the HTTP server running.
Expected: automatic check after boot completes, boot not blocked.

```
[00:00:00.510] boot complete..................................... OK
[autoupdate] New version available.
[autoupdate] Version: WP-10
[autoupdate] Time: 2026-10-30
[autoupdate] Changes: auto-boot check evidence
    [autoupdate] check complete: new version available
Open Cube OS WP-09 ready. Type 'help' for commands.
```

Server received exactly one `GET /update.json`. Verdict: **PASS**.

### T6 auto_check=no (manual only)

Input: `config set auto_check no`, reboot with server running.
Expected: no automatic request.

```
[00:00:00.630] boot complete..................................... OK
Open Cube OS WP-09 ready. Type 'help' for commands.
```

Server GET count: **0**. Verdict: **PASS**.

### T7 config_missing

Input: `rm /etc/opencube.conf` then `checkupdate`.
Expected: explicit prompt.

```
checkupdate: config file missing or unreadable
```

Verdict: **PASS** (then `config restore` recreates the defaults).

### T8 config_invalid

Input: `config set auto_check maybe` → rejected; `config_test` items 3-4.
Expected: rejection without changing the file.

```
[config_test] reject invalid auto_check=maybe: PASS
[config_test] reject non-ASCII value: PASS
```

Verdict: **PASS**.

### T9 url_http / T10 url_https

T4 exercises `http://` end to end (plain TCP + HTTP/1.0 GET); T3 exercises
`https://` end to end (TLS 1.2 handshake + encrypted GET + decrypt).
Both manifest fetches succeeded against the local test server.
Verdict: **PASS / PASS**.

### T11 url_invalid

Input: `config set update_url ftp://bad.example/url.json` + `checkupdate`.

```
checkupdate: invalid URL prefix (must be http:// or https://)
```

Verdict: **PASS**.

### T12 network_fail

Input: `update_url=http://10.0.2.2:9999/x.json` (nothing listening).

```
checkupdate: connect failed
```

Verdict: **PASS**.

### T13 json_parse_fail

Input: server returns `this is not json at all`.

```
checkupdate: JSON parse failed
```

Verdict: **PASS**.

### T14 edit /etc/opencube.conf (interactive)

Session (kernel shell): opened with `edit /etc/opencube.conf`, editor
printed the numbered file, `:i# edited by kernel edit command` appended
line 7 (re-printed), `:d7` removed it, `:w` printed `saved`, `:q`
returned to the shell; `cat /etc/opencube.conf` afterwards shows the
unchanged original content. Verdict: **PASS**.

## 4. WP-09 regression (must not regress)

| Check | Result |
|---|---|
| Boot banner + `uname -a` | `Open Cube OS WP-09 x86_64` — PASS |
| hello | `hello from userspace` — PASS |
| fork_test | `fork_test: PASS` |
| exec_test | `exec_test: PASS` |
| pipe_test | `pipe_test: PASS` |
| signal_test | `signal_test: PASS` |
| select_test | `select_test: PASS` |
| mmap_test | `mmap_test: PASS` (`MMAP_OK!`) |
| dyn_hello | `hello from dynamic program` |
| so_test | `main: foo_add(2,3)=5` |
| dlsym_test | `foo_add(7,8)=15` |
| pie_test | `loaded at 0x20000000` |
| reloc_test | `foo_add=30 foo_global=42` |
| p3_test | `p3_test: PASS` |
| heaptest | `PASS` (overhead 1%) |
| l1test | `L1 job interface test: PASS` |
| crashlog | `exception self-test: 3/3 (#DE/#UD/#PF) caught, kernel alive` (boot log) |
| fstest | mkdir/open/write/read/ls OK |

`dhtest`: `5/5 correctness tests passed` (+ group14 vector + determinism
IDENTICAL). `cryptotest`: `3/3 tests passed` (SHA-256 / AES-128 /
HMAC-SHA256, NIST vectors).

SSH both directions:
- paramiko → kernel sshd: `[PASS] kernel: listening / connection accepted /
  session finished cleanly`, `[PASS] client: auth + exec succeeded` (4/4).
- kernel ssh → paramiko server: KEX + NEWKEYS + `USERAUTH_SUCCESS` +
  `exec output: hello-from-OpenCubeOS-kernel-ssh`; server-side captured
  K `87db6e041262b49e` matches the kernel printout byte-for-byte.

HTTPS E2E (WP-09 original test, unchanged):
`wget https://10.0.2.2:8443/index.html` → `Saved 24 bytes to
/wget_https.html`; server: `TLS handshake OK: TLSv1.2
DHE-RSA-AES128-SHA256`, body `hello-from-opencube-tls`.

## 5. Real-internet URL note (honest record)

With the shipped default URL `https://helloopencubeos.space-z.ai/update.json`
the kernel resolves DNS and completes the TCP connect, then receives a TLS
alert from the server (`checkupdate: connect failed`). Cause: the kernel TLS
client (WP-09 deliverable) offers TLS 1.2 with `DHE-RSA-AES128-SHA256`
(1024-bit DH) only, which public TLS endpoints refuse. Against the bundled
TLS 1.2 test server the whole checkupdate/HTTPS path works end to end
(T3/T10). Recorded in docs/KNOWN_ISSUES.md — a broader cipher set would be
an extension of the WP-09 TLS stack, not a fix5 item.
