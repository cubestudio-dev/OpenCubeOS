<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU debugging techniques

Capture everything, watch the machine (not just the guest), attach
gdb, and know the errata — QEMU's own quirks account for a surprising
share of "OS bugs".

## Capture everything

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none \
    -netdev user,id=n1 -device e1000,netdev=n1
```

The serial log IS the boot narrative — one `OK` per subsystem, panic
banners, register dumps, crashlog flushes. The serial console is
**authoritative** (VGA is best-effort: the panic one-liner points you
back to serial with `Registers and call trace on the serial console.`).

For a triple fault / reset loop:

```
-d int,cpu_reset,guest_errors -D qemu.log
```

The log shows the last interrupt/reset; "the last self-test line that
did NOT print OK" in boot.log names the stage that died.

## Interactive serial + monitor

```
qemu-system-x86_64 ... -serial stdio -monitor stdio
```

gives the guest console and the QEMU monitor on one terminal (the
E2E runner uses `mon:stdio`; commands prefixed `@` go to the
monitor). To feed keystrokes a serial link cannot carry:

```
(qemu) sendkey ctrl-alt-f2      # OCSE GUI<->CLI switch
(qemu) sendkey up               # menu navigation (PS/2 path)
(qemu) sendkey f2               # editor IME toggle
(qemu) screendump shot.ppm      # VGA proof (see errata first!)
```

`sendkey` is THE injection path for arrow/F-keys: CSI bytes over a
TCP serial socket arrive too late at boot-menu time. Pointer input
needs QMP `input-send-event` (HMP `mouse_move` is dead on
`-display none` + QEMU 10).

## GDB into the kernel

```
qemu-system-x86_64 ... -s -S        # gdb stub, frozen at reset
gdb build/opencube_base.elf          # the pre-strip ELF has symbols
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

`build/opencube_base.elf` is kept by `make kernel` precisely for
this — the final `build/opencube.elf` is stripped.

## Deterministic replays

The build is reproducible (`make reproducible-check`): the same tree
produces byte-identical ELFs, so a crash you can reproduce twice is
the same bytes both times. Combine with `-icount` for timing
stability in network tests.

## The QEMU errata list

Quirks verified against this project — check these before blaming the
kernel:

| Erratum | Effect | Workaround |
|---------|--------|------------|
| QEMU 10 `intel-hda` model dead-loops on 44.1 kHz | audio freeze at that rate (real hardware fine; documented in-source) | other rates, or `-device AC97`/`-device sb16` |
| QEMU drops serial RX bytes right after an HMP `screendump` | flaky input in the following seconds (verified with an in-kernel RX trace) | never screenshot mid-functional-round; re-run flapping checks isolated |
| HMP `mouse_move` dead on `-display none` + QEMU 10 | GUI tests can't move the pointer | QMP `input-send-event` |
| Serial echo overlaps guest output | the eaten-`e` `ega]` artifact; shifted fragments | tolerant matching (treat `[tag]` brackets as optional, strip the echo line); see [silent-failures.md](silent-failures.md) §8 |
| TCG (no KVM) RSA-2048 > 500 s | sshd first-boot keygen looks hung | pre-seed `etc.img` (`tools/host_keygen_etc.py`); generous timeouts (banner 420 s) |
| default `pc` machine has no Intel TCO | `watchdog hw: none` (honest) — q35's ICH9-LPC has it | `-machine q35` for TCO tests |
| user-net has no radio | `wifi scan` honest no-radio | skip, or physical hardware |
| El Torito `-boot-info-table` | zero-fills bytes 8..0x47 of the boot image — OCBL stage1 entry code destroyed (silent hang + 5 MB of 0x00 on serial) | never enable it for OCBL ISOs (`build_test_iso.py` deliberately omits it) |

## Device matrix at a glance

The QEMU flags that light up each driver family are tabulated in the
[driver manuals](../drivers/storage.md) — attach exactly the device
you're debugging. The stock favorites:

- `-device e1000,netdev=n1` + `-netdev user,id=n1` — best-tested NIC.
- `-device virtio-net-pci` — virtio paths (watch for TX timeouts).
- `-drive file=hd.img,format=raw` — disks; `readonly=on` produces the
  honest `virtio-blk: write rejected` (by design).
- `-device intel-hda` / `AC97` / `sb16` — audio.
- `-machine q35` — TCO watchdog, ICH9 LPC.

## Two-instance links

Shared `-netdev socket,multicast=...` gives a real two-node link for
the NIC/SSH tests; host-forwarding (`-netdev user,id=n1,hostfwd=tcp::2223-:22`) lets a host paramiko client reach
the guest `sshd 22`.

## Screenshot pipelines

`make shot-bios` / `make shot-uefi` (VNC + monitor screendump → PNG via
PIL, `tools/qemu_shot_vnc.py`); `tools/qemu_snap.py` boots an ISO,
screendumps VGA, renders the 80×25 grid as ASCII. Screen-content
assertions must check ≥ 2 colors and a measurable foreground ratio
(BUG #66: never trust "it rendered" without a machine assertion).

## Monitor sockets and raw serial

For scripted control, put the monitor and serial on unix sockets:

```
qemu-system-x86_64 ... \
    -serial unix:serial.sock,server,nowait \
    -monitor unix:mon.sock,server,nowait
```

- Raw control bytes (the `fix1_before_*` captures) need a unix-socket
  chardev so bytes arrive **verbatim** — a TCP serial link mangles
  timing and eats characters at boot-menu time.
- `socat - UNIX-CONNECT:mon.sock` gives you the monitor;
  `sendkey`, `screendump`, `pmemsave`, device hot-add all work there.
- Memory dump for post-mortems: `(qemu) pmemsave 0xb8000 4000 vga.bin` (the VGA text cells — how the OCIRE scroll proofs work, no
  OCR needed).
- Device triage from the monitor: `info pci`, `info block`, `info usb`; hot-add/remove to bisect a probe failure without rebooting
  the host.

## sendkey cookbook

```
(qemu) sendkey up            # OCBL menu: entry up (also serial CSI ESC[A)
(qemu) sendkey down           # entry down
(qemu) sendkey ret            # select
(qemu) sendkey f2             # editor IME toggle / TUI save-config
(qemu) sendkey f5             # TUI repaint
(qemu) sendkey esc            # leave a TUI page
(qemu) sendkey ctrl-alt-f1    # OCSE GUI <-> CLI switch
(qemu) sendkey ctrl-alt-f2    # the other direction
```

Rules: PS/2 is THE injection path (CSI bytes over serial arrive too
late at menu time); compose keys with dashes (`ctrl-alt-f2`); the
OCBL menu itself understands only Up/Down/Enter by contract, plus
the serial CSI mirror for headless automation.

## Reading boot.log — a worked example

```
[    0.000] BOOT: OK
[    0.412] KM_AIN_ENTERED: OK
[    1.208] SCHED_READY: OK
[    2.017] DRIVERS_READY: FAIL      <-- died here
```

The FAIL line names the stage; the last driver line above it names
the device. Cross-reference the stage ladder in
[boot-failures.md](boot-failures.md) §1; the OCBL side judged this
boot against the same stages (30 s boot / 10 s phase budgets, ≥ 8 s
RTC silence = failure). A panic banner in the same log means the
kernel recovered a record — check `crashlog` on the next boot.

## Determinism and reproducibility

- `make reproducible-check` verifies byte-identical outputs.
- Same tree + same flags = same crash: bisect by build, not by guess.
- `-icount` for timing-stable network rounds (the TCP suites rely on
  it); pair with `-rtc base=utc` for sane clock-dependent tests
  (X509_E_EXPIRED / NOT_YET are the trap otherwise).

## Make targets that drive QEMU for you

| Target | What it does |
|--------|--------------|
| `make run-bios` / `run-uefi` | boot the ISO with serial stdio (BIOS / OVMF) |
| `make run-bios-gui` / `run-uefi-gui` | GTK window so the PS/2 keyboard gets real scancodes |
| `make run-bios-persist` / `run-uefi-persist` | `-snapshot` OFF so /etc edits persist in `build/etc.img` |
| `make shot-bios` / `shot-uefi` | VNC + monitor screendump → PNG (`tools/qemu_shot_vnc.py`) |

All of them assume `build/opencube.iso` exists (`make iso`).

## What to capture for a bug report

1. `boot.log` (full `-serial file:` capture of the session).
2. `qemu.log` if `-d int,cpu_reset` was on.
3. The exact QEMU command line and `-version`.
4. In-guest, when possible: `health`, `dmesg 1024`, `crashlog`,
   `dumpinfo`, `abcfg` (A/B state), `update status`.

## Related pages

- [boot-failures.md](boot-failures.md) — reading the boot narrative
- [silent-failures.md](silent-failures.md) — serial-echo tolerance,
  capture routing
- [../dev/qemu-testing.md](../dev/qemu-testing.md) — the full test
  tooling
- [../dev/debugging.md](../dev/debugging.md) — developer techniques
