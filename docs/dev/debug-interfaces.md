<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hidden debug interfaces

The inventory of debugging surfaces that are registered commands
but not advertised as user tools: the ~93 test-stub programs, the
diag category's ring-0 self-tests and chipset operations, the
registry gates, the live-fix probes, and the per-driver passthrough
test faces. Every entry is a real registered program or L1SVC op -
type its name at the `ush>` prompt. For how to *read* the output,
see [debug-handbook.md](debug-handbook.md); the full op numbering
is [interface-dictionary.md](interface-dictionary.md).

## 1. How to run any of these

- Type the program name at the `ush>` prompt (they are registry
  entries, surfaced by `help -a`).
- Arguments, where noted, are passed exactly like any command
  (`disk_rw_test hda`); the wrapper joins argv into the op's arg
  line.
- From the host: `python3 tools/run_l1_per_app.py` boots the ISO
  once per program and records each output (fresh process table
  per row); `tools/run_fix4_regression.py` runs the fixed battery
  (see [e2e-suites.md](e2e-suites.md)).
- The service side owns the grammar: for l1svc-wrapped apps the
  program is a transparent pipe - a "hidden" flag is one that the
  registry hint never shows but the service parses.

## 2. Test-stub programs (~93)

Grouped by what they exercise. "Run" is the invocation; anything
without arguments is a bare name.

### 2.1 OTA / update pipeline (11 + config_test)

| Program | Tests | Run |
|---|---|---|
| `ab_partition_test` | A/B slot + flag semantics | bare |
| `checkupdate_test` | update-check unit test | bare |
| `update_check_test` | check stage unit test | bare |
| `update_download_test` | download stage unit test | bare |
| `update_install_test` | install stage unit test | bare |
| `update_local_test` | offline package install | bare |
| `update_pkg_test` | package manifest/checksum | bare |
| `update_rollback_test` | rollback stage unit test | bare |
| `update_status_test` | status stage unit test | bare |
| `update_verify_test` | SHA256 verify stage | bare |
| `real_update_test` | the full real flow | bare |
| `config_test` | /etc config backend | bare |

Drivers: `tools/make_update_pkg.sh` (package), the E2E
`tools/fix1_checkupdate_e2e.py`.

### 2.2 Block / storage (8)

| Program | Tests | Run |
|---|---|---|
| `ahci_test` | AHCI port read | bare |
| `nvme_test` | NVMe namespace read | bare |
| `ata_dma_test` | ATA bus-master DMA vs PIO timing | bare |
| `virtio_blk_test` | virtio-blk round trip | bare |
| `disk_rw_test` | raw sector write/read/compare | `disk_rw_test <device>` |
| `partition_test` | partition scan | `partition_test [device]` |
| `fs_mount_test` | format + mount round trip | `fs_mount_test <device>` (**formats it**) |
| `real_hw_test` | real hardware disk + NIC survey | bare |

### 2.3 NIC drivers (10)

`e1000e_test`, `igb_test`, `ixgbe_test`, `bcm57xx_test`,
`rtl810x_test`, `rtl8125_test`, `rtl8139_test`, `rtl8168_test`,
`nic_rw_test` (frame loopback), `other_nic_test` (best-effort
family detect). All bare. These are the *direct passthrough* debug
faces of the per-family drivers - each prints that driver's own
register/link/MAC state, not a generic summary.

### 2.4 Audio (8)

`hda_test`, `ac97_test`, `sb16_test`, `es1370_test`,
`virtio_snd_test`, `usb_audio_test` (per-driver 500 ms tone
selftests; hda/ac97/virtio at 48000, sb16/es1370 at 44100),
`audio_rw_test` (PCM loopback, 48 KB buffer),
`sample_rate_test` (caps-bitmap vs set_rate negotiation; honest
SKIPPED with no cards; HDA+44100 reports the caps bit only under
QEMU). All bare.

### 2.5 USB (7)

`usb_core_test` (enumeration + control transfers),
`usb_hotplug_test` (connect/disconnect, extra args),
`usb_hub_test` (external hubs), `usb_kbd_test` (HID keyboard),
`usb_mouse_test` (HID pointer), `usb_serial_test` (CDC-ACM),
`usb_storage_test` (MSC BOT - registers block devices). All bare.

### 2.6 Camera + legacy ports (6)

`uvctest` (UVC capture), `scsitest` (SCSI over block layer, LSI
53C895A), `gameporttest`, `paralleltest` (SPP bytes),
`irtest` (NEC/RC5/RC6/SIRC), `touchtest` (i2c-hid gestures). All
bare.

### 2.7 Power paths (4)

`poweroff_test`, `suspend_test` (real ACPI S3),
`halt_test`, `reboot_test` - each exercises the REAL path and can
end the session (the power wizard warns + confirms before running
them; halt/reboot tests honestly SKIP live observation).

### 2.8 Display / CJK (5)

`chinesetest`, `font-test`, `encoding-test`,
`ime-test` (engine + session battery: phrase/paging/digit-pick
protocol assertions), `tui-test` (TUI selftest verdict block,
~4.4 KiB). All bare.

### 2.9 Kernel core self-tests (16+)

| Program | Tests |
|---|---|
| `memtest` | memory service battery |
| `heaptest` | heap stress (alloc/free/coalesce) |
| `heapbounds` | heap boundary conditions |
| `fstest` | VFS stress |
| `vmtest` | VMM unit test |
| `vmkernelpt` | kernel page-table dump |
| `vmmap` | address-space map dump |
| `cr3test` | CR3 register audit |
| `pftest` | page-fault handler |
| `pmmrace` | PMM alloc/free race detector |
| `irqabitest` | IRQ-entry ABI probe (rsp mod 16, DF=0) |
| `msitest` | MSI/MSI-X vector delivery |
| `timer` | soft timers |
| `synctest` | spinlock/sem/mutex/cond |
| `multi` | 3 interleaved task wake test |
| `spawn` | background spawn demo |
| `exc` | exception self-test report (shares the boot counter) |
| `nice` | priority demo - `nice <tid> <prio 0..31>` |

Plus the recovery pair `crashtest` (fault injection, see
[debug-handbook.md](debug-handbook.md) 5.1) and `dumpinfo`, the
crypto pair `cryptotest`/`dhtest`, `l1test` (job/IPC/dynlink),
and the net tests `nf_test`/`tcpcc_test`/`tcptest`/`dnstest`.

### 2.10 Ring-3 extension family (flat programs, tag l1)

Kernel-surface probes compiled as flat legacy programs:

`fork_test`, `exec_test`, `fdref_test`, `pipe_test`,
`select_test`, `select_zero_test`, `signal_test`, `sigsegv_test`,
`sse_test` (FPU state across switches), `stackovf` (user stack
guard), `int3_user` (#BP from ring 3), `priv_test` (IOPB
deny-all - ring-3 port access must fault), `pf_test`, `p3_test`
(priority), `mmap_test`, `mmap_multi`, `mprotect_test`,
`g6_test` (process group; async tally `failed tests = 0`),
`g6_exec_a`, `g6_exec_b`, `g6_oom`, `g7mmap` (BUG-0281), and the
dynamic-link family `pie_test`, `reloc_test`, `so_test`,
`dlsym_test`, `dyn_hello`, `main_dyn`, plus `hello`, `loop` (job
test target) and `badapp` (intentionally misbehaving - the
canary class for exception kills).

### 2.11 Self-verifying help tests (3 registry gates)

| Program | Asserts |
|---|---|
| `help_default_test` | the default listing's count/shape |
| `help_a_test` | the A-Z ordering invariant |
| `help_wp_test` | the work-package grouping of the LIVE registry text |

They parse the registry text itself (the same `L1SVC L1/APPS`
rendering `help` uses), so they double as runtime regression
gates for the generated `OC_USERPROGS[]` table.

### 2.12 Live-fix probes (1)

`p2test <utf8|read0|pipe>`:

- `utf8` pushes 174 hanzi (522 bytes, ONE syscall) through both
  write paths (console + serial encoding);
- `read0` checks POSIX `read(0, buf, 0) == 0`;
- `pipe` hammers pipe slot-reuse for 100 rounds.

Driven over the QEMU serial console by
`tools/wp_p2_batch_a_test.py` (batch A live probes: read0 #14,
pipe slot-leak #9).

## 3. The diag category's hidden depth (39 ops)

Beyond the vitals (1-8), two groups the casual user never meets:

- **G9 ring-0 self-tests (ops 9-26)**: cr3test, exc, pftest,
  pmmrace, vmtest, vmkernelpt, vmmap, memtest, heaptest,
  heapbounds, irqabitest, timer, msitest, spawn, multi, nice,
  synctest, fstest - same bodies as the apps above, reachable
  through L1SVC without the wrapper program. Five of them
  (vmmap/pftest/cr3test/vmkernelpt) run inside a
  **switch-to-kernel-AS wrap**: their v2.2.0 semantics depended on
  the kernel address space being current, so the dispatcher
  recreates that context from the ring-3 syscall and restores the
  spawner's AS before returning.
- **G10 chipset operations (ops 27-39)**: acpi, acpidev, acpipow,
  acpitest (AML parser), pcidump (`pcidump <bus:dev.fn>` e.g.
  `0:1f.3` - full config space), pcibars, pcie, i2cdetect,
  i2cget (`i2cget <bus> <addr> [reg]`, hex), i2cset
  (`i2cset <bus> <addr> <reg> <val>`), i2ctest, smbus, smbustest
  (byte/word/block round-trips).

## 4. Hidden parameters worth knowing

The registry hint never shows these; the service parses them:

| Command | Hidden arg | Effect |
|---|---|---|
| `top` | `-n N` | refresh N times (1..10 clamped), 1 s apart |
| `ln` | `-s <target> <linkpath>` | symlink instead of hard link |
| `sed` | `s/old/new/[g]`, `-n Np`, `Nd` | substitute / print Nth / delete Nth |
| `p2test` | `utf8 \| read0 \| pipe` | probe selector (above) |
| `health` | `watch` / `export [path]` | different L1SVC ops |
| 8 mega-apps | `direct` | skip the literal word, verbatim passthrough |
| `help` | `-a` / `--all`, `-w` / `--wp` | full A-Z / grouped listing |
| `update` | `status`, `--status`, `--local <path>`, `local <path>` | status view / offline install |
| `dns` | `[aaaa\|cname\|mx\|txt\|ns\|srv]` | record-type selector |
| `wget` | `https://...` or 3-arg `host port path` | TLS path / legacy positional form |
| `ssh` | `[port=2222] [user=oc] [password=oc\|"-"]` | `-` = publickey via /etc/ssh_client_key |
| `ime` | `-i` | interactive pinyin compose line (digits pick, space = first, PgUp/PgDn or -/=/= page, Backspace drops a letter, Enter commits raw, Esc cancels) |
| `dmesg` | `[N]` | line count 1..1024 (default 30) |
| `uname` | `[-a]` | full identity string |
| `serial_encoding` | `[utf8\|gbk]` | the COM1 tee encoding |
| `sleep` | fractional seconds | `sleep 0.5` = 500 ms delay (NOT suspend) |

## 5. NIC passthrough faces (wifi / bt)

| Command | Modes |
|---|---|
| `wifi` | bare = adapter + family registry status; `wifi scan` = passive beacon scan (honest "no radio" under QEMU); `wifi test` = 802.11 core + per-family selftest (families without hardware listed by name); `wifi sdio` = SDHCI bus state + selftest |
| `bt` | bare = transport + controller status; `bt usb` = HCI-USB transport; `bt uart` = H4 UART transport; `bt test` = flow selftest (real E2E over COM2 H4 bytes into `tools/bt_test_peer.py`) |

## 6. USB test ops (the 9-op debug face)

Cat 17 exposes the whole USB stack's self-tests as first-class
ops: `usb` (status), `usbdev [slot]` (per-device detail), then
`usb_core_test`, `usb_kbd_test`, `usb_mouse_test`,
`usb_storage_test`, `usb_serial_test`, `usb_hotplug_test [...]`,
`usb_hub_test`. The kernel-side poll thread
(`driver_usb_poll_start`) is started in kmain and stays resident.

## 7. Boot-time self-tests (not commands)

Two run before the shell exists - their lines appear in every
boot log:

- `l1_ext_self_test()` - "WP-01 ext self-test: N/4 points
  reachable" (probes the 4 extension points: fb access, renderer,
  font engine, console hook).
- `arch_exc_tests()` - "exception self-test: N/3 (#DE/#UD/#PF)
  caught, kernel alive" - shares its pass counter with the `exc`
  command.

## 8. The registry gates that keep this honest

- `make userprogs-check` - fails `make dist` when any l1apps
  source / interface header / i18n json is > 2 s newer than the
  generated `userprogs_data.h`.
- `tools/run_fix4_regression.py` check 6 - the registry floor:
  `help -a` must list >= 276 programs (measured 284).
- `help_*_test` (section 2.11) - runtime shape assertions.
- `tools/gen_api_docs.py` - regenerates `docs/data/*.json` and the
  292 command pages from the *live* `help -a` capture, tolerating
  serial-echo overlap (the `[tag]` brackets are optional because
  an echo overlap can eat the leading `"e"` of `"ega]"`).
