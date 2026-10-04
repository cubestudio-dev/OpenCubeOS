<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — System Configuration File (WP-09-fix5)

`/etc/opencube.conf` is the **first user-editable configuration file** of
Open Cube OS. This document is the pattern contract for it and for all
future system configuration files.

All content inside the system is **ASCII (0x20–0x7E)** only — the 8×16
bitmap font has no glyphs beyond printable ASCII.

## 1. Storage

| Aspect | Value |
|---|---|
| Path | `/etc/opencube.conf` |
| Backend | FAT32 volume attached as `ata0`, mounted at `/etc` (persistent) |
| Fallback | ramfs `/etc` seeded from compiled-in defaults (no disk attached) |
| Encoding | ASCII text, LF or CRLF line endings |
| Size limit | 4096 bytes (files are expected to stay small) |

The boot log shows the active backend:

```
vfs: mounted fat32 at /etc
[00:00:00.180] system config (/etc/opencube.conf, fat32)......... OK
```

If no disk is attached the kernel creates a ramfs `/etc` and writes the
same defaults — every boot has a working config file either way.

## 2. Format

```
# Open Cube OS configuration
# Update check URL (HTTP or HTTPS)
update_url=https://cubestudio-dev.github.io/OpenCubeOS/update.json

# Auto check on boot (yes / no)
auto_check=no
```

Rules:

- `key=value` per line; `#` at the start of a line (or after leading
  blanks) begins a comment; blank lines are ignored.
- Keys are matched exactly (`oc_strcmp`); values are trimmed of
  surrounding blanks.
- Lines containing bytes outside printable ASCII are **ignored** when
  reading and **rejected** when writing.
- Lines without `=` or with an empty key are ignored (reading is
  forgiving; the rest of the file still applies).

## 3. Well-known keys and default behaviour

| Key | Meaning | Default (file missing / key missing / empty value) |
|---|---|---|
| `update_url` | Manifest URL for `checkupdate`; scheme decides transport (`https://` → TLS 1.3/1.2 client, `http://` → plain TCP, anything else → error) | `https://cubestudio-dev.github.io/OpenCubeOS/update.json` |
| `auto_check` | `yes` / `no` — run the update check automatically after boot completes | `no` |

Defined behaviour (documented policy, implemented in `kernel/config.c`):

| Situation | Behaviour |
|---|---|
| File missing at boot | recreated from compiled-in defaults |
| File missing while reading (`oc_config_read`) | error `OC_CONFIG_E_NOFILE` (checkupdate prints `config file missing or unreadable`) |
| Key missing / empty value | built-in default is used (`oc_config_read_default`) |
| `auto_check` invalid value (not yes/no) | treated as `no` (safe default), warning logged; `config set` **rejects** the value outright |
| `update_url` invalid prefix | accepted by `config set`, rejected at check time (`invalid URL prefix (must be http:// or https://)`) |
| Non-ASCII value in `config set` | rejected |

## 4. Editing from inside the system

Any of these work on the running system; edits take effect immediately
(no reboot needed) and, on the FAT32 backend, survive reboots:

```
edit /etc/opencube.conf      # kernel-shell line editor (:i :d :p :w :wq :q :q!)
vi /etc/opencube.conf        # same editor inside ush (user shell)
nano /etc/opencube.conf      # alias of vi
config set auto_check yes    # validated key/value writes via shell
config get update_url        # read one key
config list                  # print the whole file
config restore               # delete + recreate from defaults
```

`checkupdate` reads the config **every time it runs**, so an edit applies
to the next check without restarting anything.

## 5. Shell commands (WP-09-fix5)

| Command | Purpose |
|---|---|
| `checkupdate` | fetch the manifest (HTTP or HTTPS per `update_url`), compare versions, print version/time/changes |
| `config` | `list` / `get <key>` / `set <key> <value>` / `restore` / `path` |
| `edit <file>` | line editor (works on any file, pattern use-case is the config) |
| `config_test` | config subsystem self-test (7 checks) |
| `checkupdate_test` | update-check self-test (5 unit checks + 1 live probe) |

### checkupdate output

Up to date (server version equals `WP-09`):

```
checkupdate: reading /etc/opencube.conf
Current version is up to date.
Version: WP-09
Time: 2026-10-15
```

New version available:

```
checkupdate: reading /etc/opencube.conf
New version available.
Version: WP-10
Time: 2026-10-30
Changes: driver optimization + config file
```

Errors (full list, one line each):

```
checkupdate: config file missing or unreadable
checkupdate: invalid URL prefix (must be http:// or https://)
checkupdate: DNS resolution failed
checkupdate: connect failed
checkupdate: no response (timeout)
checkupdate: malformed HTTP response
checkupdate: JSON parse failed
checkupdate: response too large
```

## 6. Manifest format

The server returns a flat JSON object (ASCII strings):

```json
{
  "version": "WP-10",
  "time": "2026-10-30",
  "changes": "driver optimization + config file",
  "changes_v2_url": "https://cubestudio-dev.github.io/OpenCubeOS/update-v2.json"
}
```

`version` is compared to the running version with an exact string match.
The kernel parser (`json_get_string` in `kernel/update.c`) extracts
top-level string fields; escape sequences are handled by taking the next
character literally. This is a deliberately scoped parser for the
documented manifest shape — not a general JSON library (no nesting, no
numbers/arrays/booleans needed by the contract).

### 6.1 Dual manifest ("Plan D") — old-version compatibility

The published manifests keep every kernel generation working:

| file | served content | who reads it |
|---|---|---|
| `/update.json` | short `changes` (<= 127 bytes) + optional `changes_v2_url` | every kernel; WP-09 stops here |
| `/update-v2.json` | the same manifest with the full-length `changes` | kernels that understand `changes_v2_url` |

The v2 manifest's long-changelog field is named `changes` (canonical,
matching every published manifest).  WP-10d briefly shipped it as
`changes_full`; current kernels accept BOTH names (`changes` wins when
both are present), so no manifest generation stops parsing.

Rationale: WP-09 holds `changes` in a 128-byte buffer and fails the whole
check with `JSON parse failed` on overflow, while WP-10a+ truncate a long
value.  A server that only published the long changelog would break
WP-09; one that only published short changelogs would degrade the new
kernels' display.  The dual manifest gives old kernels a readable
manifest and new kernels the full changelog (rule 8).

Kernel side (all display-only, best-effort — a failed v2 fetch never
fails the check):

- `oc_update_changes_v2_url(body, url, cap)` — extract the optional
  `changes_v2_url` field (`0` ok / `-1` missing or empty);
- `oc_update_fetch_changes_v2(v2url, out, outcap)` — fetch the v2
  manifest (absolute `http://`/`https://` only) and copy its long
  `changes` (or `changes_full`) into `out` (`0` upgraded / `-1` rejected
  URL / `<0` transport error);
- `oc_check_update()` and `oc_update_check_pkg()` apply both after the
  base manifest parses.

### 6.2 Redirect following

Both HTTP transports (manifest fetch and package download) follow up to
3 HTTP redirects (301/302/303/307/308) when the server replies with an
absolute `Location` URL.  This is required for the GitHub release
download URL used by `package_url`: `https://github.com/...` answers
`302` and points at `https://release-assets.githubusercontent.com/...`,
whose redirect headers alone are ~5.3 KB (the receive buffers are sized
for that).  Redirect chains longer than 3 hops, relative `Location`
values and non-absolute URLs fail the request instead of looping.

## 7. Boot auto check

When `auto_check=yes`:

- `kmain` spawns the `checkupdate` kernel thread **after** `boot complete`
  and **before** entering the interactive shell;
- the thread waits up to 10 s for the network link, then runs one check;
- results are printed with the `[autoupdate]` prefix and logged; failures
  are logged and never block or crash the boot;
- when the network never comes up the check is **skipped** with a log
  entry (`[autoupdate] skipped: network not ready`).

When `auto_check=no` (default) nothing runs automatically; users run
`checkupdate` manually.

## 8. L1 extension interfaces (kernel/ext.h)

| Interface | Purpose |
|---|---|
| `oc_ext_config_read(key, val_out, outlen)` | strict read (see §3 semantics) |
| `oc_ext_config_write(key, value)` | validated write (preserves comments/other keys) |
| `oc_ext_config_get_all(buf, buflen)` | whole file |
| `oc_ext_check_update(out)` | synchronous check (`0` up to date, `1` new, `<0` error) |
| `oc_ext_check_update_async()` | spawn the non-blocking check thread |

Each interface has its declaration + contract comment in `kernel/ext.h`,
its implementation in `kernel/config.c` / `kernel/update.c`, and is
covered by `config_test` / `checkupdate_test` / the WP-09-fix5 test batch.

## 9. Test server

`tools/update_server.py` serves the manifest for end-to-end tests:

```
python3.13 tools/update_server.py --mode http  --port 8008 --json '<json>'
python3.13 tools/update_server.py --mode https --port 8443 --json '<json>' \
        --cert /tmp/hcert.pem --key /tmp/hkey.pem --dh /tmp/dhparam.pem
```

The kernel reaches the host via the QEMU user network address `10.0.2.2`.

TLS capability note: the kernel TLS client negotiates TLS 1.3 (X25519 key
share, AES-128/256-GCM or ChaCha20-Poly1305, full key schedule and
encrypted handshake) with certificate-chain + hostname verification
against embedded public CA roots, and falls back to TLS 1.2 ECDHE_RSA
with AES-GCM / ChaCha20-Poly1305 (legacy DHE-CBC retained). Public HTTPS
servers work out of the box — verified against
cubestudio-dev.github.io, www.google.com and www.cloudflare.com. The
bundled test server above speaks TLS 1.2 DHE-CBC on purpose: it exercises
the legacy fallback path end-to-end.
