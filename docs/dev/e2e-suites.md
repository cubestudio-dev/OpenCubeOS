<!-- SPDX-License-Identifier: Apache-2.0 -->
# End-to-end suites

The deep tests that gate a release. All of them drive a real QEMU
boot through pexpect on the serial console; the matrix below is
what the release evidence quotes (real outputs, skip counts
declared). CI itself is build-only - these run sandbox/release-time
(`tools/build_release.sh --deep`).

## 1. The verification matrix

| # | Gate | Tool | The line it must print |
|---|------|------|------------------------|
| 1 | Host unit tests (`make test`) | `tools/run_host_tests.py` | all harnesses PASS (SKIP-FIXTURE declared) |
| 2 | Full regression 31/31 | `tools/run_fix4_regression.py` | `=== 31/31 checks passed` |
| 3 | SSH E2E | `tools/ssh_g11a_e2e.py` | `[g11a] ALL PASS` (5 checks) |
| 4 | HTTPS E2E | `tools/run_fix4_https_e2e.py` | `=== 8/8 HTTPS E2E checks passed` |
| 5 | Watchdog/logging/health | `tools/wp12_watchdog_logging_e2e.py` | S1-S14 |
| 6 | TUI / Chinese / desktop | `tools/wp10_*.py` | per-suite |
| 7 | Mega-app smoke | the WP-l1apps-merge smoke script | 21/21 |

## 2. The regression gate - all 31 checks

`tools/run_fix4_regression.py` boots the ISO (512M, `-serial mon:stdio`, e1000 user-net, an HMP monitor unix socket for the
framebuffer assertion), waits for `ush>`, runs 24 commands, then
checks. Each pattern is scoped to ONE command's output section
(BUG #64 fix: whole-log matching was structurally always-true) and
the echoed command line is stripped so name-bearing patterns can
never be self-satisfied.

**Boot group (6)**

1. screen framebuffer alive (BUG #66) - HMP screendump, >= 2
   colors + a measurable foreground ratio (serial-only gates once
   passed on a dead panel)
2. boot banner ready - `Open Cube OS User-space Shell \(3\.0\.0`
3. boot prompt hint - `Type 'help' for built-in commands`
4. boot complete OK - `boot complete\.+ OK`
5. exception self-test 3/3
6. command count >= 276 - counted dynamically from the `help -a`
   registry rows (measured **284** on the WP-l1apps-merge tree);
   the interface-freeze floor

**System group (2)**

7. uname current tree - version regex incl. WP-10/11/12/13/14
   markers
8. crashlog clean boot - `no exceptions recorded|0 entries|N entries`

**Self-test programs group (17)**

9. heaptest pass - `\bPASS\b`
10. fstest pass - `VFS self-test.*PASS`
11. cryptotest NIST pass
12. l1test wp8b loader 14/14 - `ext_wp8b selftest 14/14 - PASS`
13. l1test job steps PASS
14. l1test overall PASS
15. hello output
16. fork_test
17. exec_test
18. pipe_test
19. signal_test
20. select_test
21. mmap_test
22. mmap_multi
23. g7mmap (BUG-0281)
24. p3_test (priority)
25. g6_test full chain - async tally `failed tests = 0` (separate
    300 s wait)

**Dynamic-link group (5)**

26. dyn_hello - `hello from dynamic program`
27. so_test - `foo_add(2,3)=5`
28. dlsym_test - `foo_add(7,8)=15`
29. pie_test - `loaded at 0x...`
30. reloc_test - `foo_add=30 foo_global=42 fp=300`

**Security group (1)**

31. p0leak fence pass - `all kernel-address probes fenced`

Exit code = number of failed checks; the release matrix quotes
31/31.

## 3. The SSH E2E (5/5) - tools/ssh_g11a_e2e.py

Direction A (guest client -> host `tools/paramiko_sshd.py` on
:2222):

1. `ssh-auth` - `[ssh] authenticated (USERAUTH_SUCCESS)`
2. `ssh-exec` - `[ssh] exec output: hello-from-OpenCubeOS-kernel-ssh`

Direction B (guest `sshd 22 oc oc` <- host paramiko client via
hostfwd tcp::2223-:22):

3. `sshd-listen` - `listening on port 22`
4. `sshd-exec` - client `uname` returns `Open Cube OS`
5. `sshd-capture-log` - `exec: captured N bytes of output`

Operational notes baked into the tooling:

- TCG RSA-2048 keygen > 500 s - pre-seed the host key (n||d, 256 B
  each, e=65537) into `build/etc.img` with
  `tools/host_keygen_etc.py`; the loader takes the "key already on
  disk" fast path (`OC_ETC_IMG` selects the pre-seeded image).
- Generous paramiko timeouts (banner 420 s) - every TCG handshake
  stage takes tens of seconds.
- Warm the ARP cache with one client-direction packet before the
  server test (cold-cache SYN-ACK stall).

## 4. The HTTPS E2E (8/8) - tools/run_fix4_https_e2e.py

Against `tools/https_test_server.py` (one-request OpenSSL server
matched to the kernel client: TLS 1.2 only, 0x0067
DHE-RSA-AES128-SHA256, 1024-bit RFC 3526 DH, self-signed; `observe`
mode = raw-byte close_notify recorder; `eof` mode = hard-close
truncation server):

0. **self-signed chain REFUSED by default** (BUG #21 acceptance) -
   `config set tls.insecure 1` is the explicit opt-in used for the
   compatibility pass
1. TLS handshake - kernel logs `Connecting ... (TLS)`
2. TLS 1.2 + cipher DHE-RSA-AES128-SHA256 (0x0067) negotiated
   (server side)
3. encrypted `GET /hello.txt` received (server side)
4. canonical body served (`body=24`)
5. kernel saved the body (`wget` + `cat /wget_https.html` matches)
6. kernel close_notify observed (raw-byte recorder mode)
7. **truncation guard** - the hard-close server (`eof` mode) makes
   the kernel fail promptly (< 45 s, error text) instead of
   hanging

Cert/DH fixtures are generated in-run: openssl self-signed
RSA-2048 + DH params pinned to the RFC 3526 1024-bit MODP group
(ASN.1-built from the server's own `P_HEX`).

## 5. The mega-app smoke (21/21)

Phase A - classic dispatch is byte-compatible: `disk-tool lsblk`,
`net-tool ip`, `sound-tool sound`, `sysdiag health`, `hwinfo uname`, `power sleep 1`, `files df`, `update status`,
`disk-tool direct lsblk`, and the unknown-op usage line.

Phase B - the wizards: every app's no-arg menu renders and quits
on `q`; `disk-tool` runs lsblk through menu key 1 + the ENTER
pause; `update` walks steps 1-2 for real (network check, A/B
absence handling) and returns to ush cleanly.

Note: there is no committed `tools/test_megaapps.py` in the tree -
the smoke rig is referenced as "the WP-l1apps-merge smoke script"
(21/21 quoted in the release notes); a maintainer reproducing the
matrix re-creates it from the Phase A/B checklists above.

## 6. Host unit tests - tools/run_host_tests.py

The harnesses compile L0 components host-side and run them:
multiboot2 parser (ASAN), crypto (NIST AES/SHA/HMAC, RSA, X.509,
AEAD), TLS 1.3 key schedule vs RFC 8448, TCP congestion control
(CUBIC), block-512 gate, and the subsystem batteries (BT, WiFi,
camera, SCSI, IR, touchpad). SKIP-FIXTURE harnesses run for real
when the openssl fixtures exist - the state is always declared.
Full inventory: [host-tests.md](host-tests.md).

## 7. Watchdog / TUI / Chinese suites

`tools/wp12_watchdog_logging_e2e.py` - 14 scenarios S1-S14: klog
levels/switches, logcat filters, dmesg replay, logctl,
persistence + rotation `.1` chains, the global filter, **S12: the
Intel TCO real reset + second boot** (q35, `-no-reboot`, discover,
arm, starve, assert the second boot), **S13: ACPI WDAT** (a
synthetic-but-valid WDAT table injected with
`tools/make_wdat_table.py` via `-acpitable` drives the real q35
TCO registers), policy kill, health JSON.

`tools/wp10_*.py` - TUI/Chinese/desktop/ chipset / timer / wireless /
camera families; the manual verifier re-runs FLAPPING checks as
isolated fresh-QEMU scenarios (QEMU drops serial RX bytes after
HMP screendumps - verified with an in-kernel RX trace; functional
rounds therefore never take screenshots).

## 8. pexpect mechanics and serial-echo tolerance

The shared patterns every suite inherits
(`tools/qemu_runner.py`):

- `pexpect.spawn(QEMU, [...], encoding="latin-1", codec_errors="replace")` - latin-1 so every byte survives; NULs
  stripped before regex.
- Prompt regex `(?:ush>|oc>)\s` (override `OC_PROMPT`); the v2
  flow adapter `boot_to_recovery()` sends `exit` at `ush>` to
  reach the `oc>` recovery prompt.
- `strip_ansi()` removes CSI sequences before matching.
- **Serial-echo tolerance** - the "ega]" phenomenon: when the
  guest echoes the typed command, the echo can overlap the
  *output* of the same command and eat the leading `"e"` of a
  `[tag]` bracket, so `gen_api_docs.py` treats `[tag]` brackets as
  optional and the regression gate scopes every pattern to its own
  command section and strips the echo line first.
- Keystrokes a serial link cannot carry (arrows/Home/End) go
  through the HMP monitor `sendkey`; pointer input via QMP
  `input-send-event` (HMP mouse_move is dead on `-display none` +
  QEMU 10); screendumps via HMP `screendump` on a unix monitor
  socket; raw serial via unix-socket chardevs when control bytes
  must arrive verbatim.
- Known flake source: QEMU intermittently drops serial RX right
  after an HMP screendump -> functional rounds never take
  screenshots; flapping checks re-run isolated
  (`wp10_native_manual.py`).
