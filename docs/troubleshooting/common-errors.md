<!-- SPDX-License-Identifier: Apache-2.0 -->
# Common errors — the high-frequency list

The errors you will actually hit, most-frequent first, each as
**symptom → cause → fix**. This is the short list; the complete
per-subsystem inventory (with investigation chains) is
[error-catalog.md](error-catalog.md); failures that print nothing are
[silent-failures.md](silent-failures.md).

## Log and watchdog errors

| Symptom | Cause | Fix |
|---------|-------|-----|
| `logcat: bad level (debug/info/warn/error/fatal or 0-4)` | wrong level spelling | `logcat -l warn` or `-l 2` |
| `klog: flush FAILED (no filesystem mounted?)` (rotate/export twins) | /var/log not backed by a volume | mount one; `logctl flush` then writes `/var/log/kernel.log` |
| `logctl: bad level ('-' = any)` | filter syntax | `logctl filter <clear\|[level] [module] [keyword]>` |
| dmesg footer says `dropped <N>` | the 1024-entry ring wrapped | `dmesg 1024`; consider `logctl threshold` + `flush` |
| a message you expected never appears | level disabled or filter active | `logctl status`; `logctl test` calibrates the pipeline (5 lines, module `klogtest`) |
| `watchdog: register FAILED (table full?)` | 8 slots max | `watchdog slots`; stop one first |
| `watchdog hw: start FAILED (bad timeout?)` | TCO range is 2..63 s | `watchdog hw start 30` |
| `watchdog hw: feed FAILED (not armed?)` | feed before start | `watchdog hw start` first |
| `health: export FAILED (no filesystem mounted?)` | export target missing | mount a volume; `health export /mnt/health.txt` |

## TUI and display errors

| Symptom | Cause | Fix |
|---------|-------|-----|
| `tui` pages print nothing through scripts | TUI ops route DIRECT, bypassing capture | run `tui` interactively; only `tui-test` serves text through capture |
| `page '%s' failed (rc=%d)` / `tui: desktop init failed` | desktop internals | `dmesg 100`; report with serial capture |
| `value out of range (1980-2099, ...)` (time page) | RTC set guards | set a valid date |
| `volume: device rejected the level` (hw page) | mixer clamp | use 0-100 |
| editor `[save failed]` | read-only fs or partial buffer | check `mounts`; `[PARTIAL: save refused]` = over-limit file |
| Chinese shows as keys or tofu | locale/encoding mismatch | `lang zh`, `serial_encoding utf8` (match your terminal) |

## OCSE installer errors (from the install medium)

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ocblmbr.bin MISSING` / `ocos.tar MISSING` | incomplete medium | re-download the ISO; verify size/SHA |
| `install: FATAL - extraction failed` | ocos.tar corrupt | same |
| `mkpart failed` / `created partition not found` | target disk problems | run the disk checks first; try another disk |
| `install: FATAL - could not set the boot flag` | partition table write failed | re-run; check the medium |
| installer settings ignored after install | BUG-129 config dialect split | set keys again from the running system: `config set shell.lang zh` |
| OCSE GUI clicks do nothing | BUG-113 | CLI hotkey Ctrl+Alt+F2, or the `click <x> <y>` command |

## Power errors

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Suspend not supported (ACPI required).` | no ACPI tables on the machine | expected on some VMs/old boards |
| `S3 resume failed.` | resume path broken | report; data loss risk — save first |
| `System halted.` and nothing else | that IS the halt success message | power off at the host |

## Recovery environment (OCIRE) errors

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Supervisor: no OCIRE recovery volume found` | the hd1 0xEE+FAT12 volume is absent | re-install with the recovery partition (OCSE creates it) |
| `logread: BAD ring header (magic/cursor)` | boot-log ring corrupt | `repair quick` resets it |
| `WARNING: log agent digest mismatch` | agent image vs mailbox digest differ (tamper guard) | `syscheck`, then `repair` |
| `REBUILD FAILED` / `RESET FAILED` during repair | golden set or medium damage | re-run; another golden set |
| `Unknown command: <cmd>` in `oc>` | the recovery shell is tiny | `help` lists it; hidden extra: `lang [en\|zh]` |

## The universal first moves

```
ush> health          # alarms: memory >90%, tasks >64, CPU >=90%x10s
ush> dmesg 100       # the kernel log tail (ring 1024)
ush> crashlog        # panics you may not have seen (32-slot ring)
```

---

## Shell and file tools

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ush: command not found: <cmd>` | typo, or the program is not in the registry | `help -a` (or `help -w` for grouped); check [../manual/function-dictionary.md](../manual/function-dictionary.md) |
| `ush: syntax error near '>'` (or `'<'`) | empty redirect target | name the file: `cmd > file` |
| `ush: 'exit' is not available; ...` | by design — ush is the boot entry | `shutdown` / `reboot` |
| `ush: job control requires external programs` | `fg`/`bg` are recognized but not implemented | use `jobs` to list; there is no resume |
| `cat: cannot open <path>` (also head/tail/sort/uniq/wc/awk) | sys_open failed: missing file, wrong cwd | `pwd`, `ls`; paths are relative to the shell cwd |
| `mkdir: cannot create '<path>'` | exists, or parent missing | create parents first |
| `rm: cannot remove '<path>'` / `rmdir: ...` | file busy (open fd), or dir not empty | close it; `ls` the dir |
| `ln: cannot create symlink (target must be an absolute path)` | symlink targets are absolute-only | `ln -s /abs/target link` |
| `sed: unknown script` | only `s/old/new/[g]`, `-n Np`, `Nd` exist | see [../manual/ush-reference.md](../manual/ush-reference.md) |
| `awk: missing {print ...}` | the tiny awk supports `{print ...}` only | same |
| `help: unknown option '<o>'` | only `-a`/`--all`, `-w`/`--wp` exist | `help -a` |
| `run: unknown program: <name>` | registry lookup failed | `help -a`; the registry is build-frozen at 284 |
| `ls *` lists a file literally named `*` | **no globbing anywhere** (ush and ls) | name files explicitly |
| `disk-tool: unknown op: <op>` (any mega-app) | the verbatim table miss | the app's `usage:` lists its table; `help -w` groups all commands |
| `<app>: service call failed (rc=-1)` (250 apps share this) | the L1SVC bridge returned -1 | re-run; `dmesg 100`; the family's `_test` stub; then [error-catalog.md](error-catalog.md) §7.1 |
| nano/vi screen left dirty after exit | BUG-111 (editor afterimages) | `clear` |
| tofu blocks for Chinese on screen (serial fine) | BUG-110/130 console-fallback UTF-8 | fixed in current builds; update the image |
| editor: `[PARTIAL: save refused]` | file exceeds 256 lines × 128 bytes | by design (BUG-34 fix); split the file |

## Network

| Symptom | Cause | Fix |
|---------|-------|-----|
| `DHCP failed (timeout)` | no OFFER/ACK — user-net missing, cable, or server | `ip` (0.0.0.0?), then `dhcp` again; QEMU: `-netdev user,id=n1 -device e1000,netdev=n1` |
| `DNS resolution failed` / `(timeout)` | no DNS server, or UDP blocked | re-run `dhcp` (it sets DNS); `dns <host>` to test; see [network-issues.md](network-issues.md) |
| `ping` prints nothing then fails | name resolution failed first ("Resolving...") | `dns <host>`; or ping the IP directly |
| `connect failed` | routing/firewall | `route` (gateway present?), `firewall` list, `firewall reset` if experimenting |
| `wget: cannot create file, printing to console:` | target path unwritable | check `pwd`/mount; the body still prints to console |
| `TLS failed (code <n>)` / `[tls] x509 verify failed: ... REFUSED` | the trust decision — self-signed/unanchored chains are refused **by default** | correct the server cert; testing-only opt-in: `config set tls.insecure 1` |
| `[ssh] HOST SIGNATURE INVALID - disconnecting` | TOFU mismatch | `[ssh] WARNING: HOST KEY CHANGED!` means the entry in `/etc/ssh_known_hosts` must be re-trusted deliberately |
| `[ssh] USERAUTH_FAILURE — wrong password` | credentials | defaults are user `oc` / password `oc`; publickey = password `-` with `/etc/ssh_client_key` |
| `sshd` refuses to start: `cannot obtain a per-installation host key` | no key, keygen impossible | pre-seed `etc.img` (`tools/host_keygen_etc.py`), or free memory and retry (first boot generates in background) |
| first connect to sshd stalls | cold ARP cache after DHCP | warm it: `ping <peer>` first |
| `e1000e: init failed` (any `<nic>: init failed`) | driver probe failed — card absent, BARs bad, DMA unreachable | `lspci` → `pcidump <b:d.f>` → family status → family `_test`; template in [network-issues.md](network-issues.md) |
| `virtio TX timeout: avail_idx=...` | virtio NIC stopped consuming descriptors | restart the VM/device; report if persistent |
| `wifi scan` prints no networks | QEMU has no radio — honest output | real hardware, or skip |

## Storage and filesystems

| Symptom | Cause | Fix |
|---------|-------|-----|
| `mount: failed` / `vfs_mount: unknown fs type` | bad type/device/args, or the fs is not supported | `lsblk`, `mounts`; types: ramfs/fat32/exfat/ext4 |
| `fat32: not a FAT32 volume (FAT12/16 detected)` / `invalid BPB` | wrong device node, or not FAT32 | `fatstat <dev>` shows the parsed BPB; wrong node? `lsblk` |
| `ext4: unsupported INCOMP feature bits ... mount refused` | ext4 driver is READ-ONLY with feature limits | mount a simpler ext4, or read it elsewhere |
| `umount: target is busy - close open files first` | open fds | close them; `umount: unmount nested mounts first` → children first (`mounts` shows nesting) |
| `umount: cannot unmount the root filesystem` | by design | — |
| `fsck: not a FAT32 volume (fsck supports FAT32 only; ...)` | fsck is FAT32-only | repair elsewhere; `fatstat` to confirm |
| `parted: device not found` / `mkfs.*: device not found` | bad device string | `lsblk` first; names are exact |
| `mkfs.exfat: device too small (need at least 65 sectors, got <N>)` | target too small | bigger device |
| `grub-install: MBR gap too small for <N>` | post-MBR gap cannot host core.img | repartition with ≥ 1 MiB gap (or GPT) |
| `install: disk too large for MBR (>2TiB) or too small` | MBR limits | use a ≤ 2 TiB target |
| writes lost after reboot | they lived on ramfs — only /etc and mounted volumes persist | `mounts` shows what persists; `sync` before power ops |
| USB stick appears then vanishes | MSC detach path | `usb_hotplug_test` reproduces; replug |
| `blk: write to protected device ... REFUSED (ocire recovery disk)` | whole-disk OCIRE protection (klog-only line) | by design — modify that volume only from OCIRE `repair` |
| A/B disk "absent" right after `abdisk` | service cache not yet refreshed | re-run `update status`; the wizard re-checks in a loop |
| `blkcache: alloc FAILED - cache disabled (raw I/O)` | cache table alloc failed; system continues uncached | `mem`; report if persistent |

## Update / OTA

| Symptom | Cause | Fix |
|---------|-------|-----|
| `update` prints nothing / "up to date" but you expected an update | six-cause silent chain | walk [silent-failures.md](silent-failures.md) §7; or just run the wizard: `update` (no args) |
| `update: cannot determine target slot (not booted from A/B?)` | booted from ISO, no A/B layout | `update status`; `abdisk <dev>` to create (destructive) |
| `update: download failed: <why>` | network / server | the `<why>` is an OC_UPDATE_E_* text — [error-codes.md](error-codes.md) §1 |
| `SHA256 mismatch` / `package (tar.gz) invalid` | corrupted download | re-download; verify size |
| `config: write rejected (empty, non-ASCII, too long, or invalid value)` | strict value guards | `config list` for shapes; ASCII values only |
| `config: cannot read /etc/opencube.conf` | /etc not mounted | [storage-issues.md](storage-issues.md) |

## Kernel / boot / crash

| Symptom | Cause | Fix |
|---------|-------|-----|
| `*** KERNEL PANIC: <reason> ***` | 11 possible sites — the reason string names it | [error-codes.md](error-codes.md) §panic-sites; serial is authoritative; `crashlog` after reboot |
| machine rebooted "by itself" | panic with policy REBOOT, or watchdog, or OCBL judged a stall | `crashlog`, `dmesg`, `/var/log/crash.log`; OCBL side: [boot-failures.md](boot-failures.md) |
| whole machine freezes, no panic (typing 'a'/'u' fast, or `loop`) | BUG-109 freeze race / BUG-112 loop containment gap | capture `-serial file:` and report; avoid racing console input |
| `[user] process table full (max <N> processes)` | too many processes | `ps`; `kill <pid>` |
| `[user] not an ELF file` | exec target is not ELF64 | expected for badapp-class fixtures; check the binary |
| `KERNEL PANIC (no crash handler installed): <reason>` | panic before the crash service registered | early-boot bug class; capture serial, report |
| boot stops at a `FAIL` stage line | that subsystem's init failed | [boot-failures.md](boot-failures.md); the message above the FAIL names the device |
| `Power off not supported. System halted.` | all power ports exhausted (VM) | use the host's power control |
| `sleep: invalid time` | non-numeric/negative argument | `sleep 2`, `sleep 0.5` |

## Audio / USB / misc hardware

| Symptom | Cause | Fix |
|---------|-------|-----|
| `play: device error` / `: unknown device '<name>'` | no such audio device | `sound` lists registered devices |
| `[audio_rw_test] no sound cards registered => SKIPPED` | no audio device attached | QEMU: `-device intel-hda`; honest skip otherwise |
| QEMU hda freezes at 44.1 kHz | QEMU-10 model dead-loop (documented in-source) | real hardware is fine; other rates work |
| `usb: SET_ADDRESS failed` / `GET_DESCRIPTOR(...) failed` | device did not enumerate | replug; `usb`, then `usb_storage_test` |
| `virtio-blk: write rejected (device is read-only)` | the QEMU drive is readonly=on | by design; drop the flag |
| `no hardware watchdog present` lines | no WDAT/TCO — probe honesty | q35 has TCO; default pc does not |
| `msitest: FAIL` while self-IPI passed | MSI routing broken | `pcibars`, `pcie`, report |
| `date: ms since boot (no RTC driver)` | no CMOS RTC (VM default) | `-rtc base=...` in QEMU; informational |

## Build-time (host)

| Symptom | Cause | Fix |
|---------|-------|-----|
| `grub-mkimage: cannot open ... unicode.pf2` | font lives in grub-common | install it / point OC_TOOLS at a tree with `usr/share/grub/unicode.pf2` |
| `src-zip: not a git checkout` | no git metadata | build from a clone or use `make dist` |
| `error: operand size mismatch for 'setc'` | pre-fix tree on new GAS | update past the u8-flag fix |
| packed-member warnings on new GCC | pre-fix tree | update (packed tag types) |
| `embed_grub: missing /usr/usr/lib/grub...` | pre-fix script | update (dual-layout probe) |
| `chinese_font gz layout mismatch - regenerate ...` | font blob vs header drift | regenerate chinese_font_data.c |
| a `_Static_assert` fired | cross-module layout contract drifted | the assert message names both sides — [error-codes.md](error-codes.md) §static-asserts |

## Testing gates

- SSH E2E needs the seeded host key (the mtools recipe in
  [../dev/e2e-suites.md](../dev/e2e-suites.md)).
- HTTPS E2E item 0 requires the self-signed chain to be REFUSED — if
  it connects, check `tls.insecure` is 0.
- The regression suite needs pexpect; the SSH suite paramiko.
- Flaky serial? See [silent-failures.md](silent-failures.md) §8 and
  [qemu-debugging.md](qemu-debugging.md) §errata.

## Where to go next

| If the error is... | Go to |
|--------------------|------|
| a numeric code or a panic banner | [error-codes.md](error-codes.md) |
| a message string from a specific subsystem | [error-catalog.md](error-catalog.md) |
| not printed at all | [silent-failures.md](silent-failures.md) |
| during boot / in OCBL / OCIRE | [boot-failures.md](boot-failures.md) |
| network-shaped | [network-issues.md](network-issues.md) |
| disk/filesystem-shaped | [storage-issues.md](storage-issues.md) |
| only in QEMU | [qemu-debugging.md](qemu-debugging.md) |
| a command you cannot find | [../manual/function-dictionary.md](../manual/function-dictionary.md) |
