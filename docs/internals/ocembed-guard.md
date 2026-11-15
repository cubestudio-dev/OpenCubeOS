<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the OCIRE embed guard (ocembed)

How OCOS is embedded under the OCIRE recovery supervisor: the
boot-facts cmdline, the disk mailbox state machine, the log ring
on the OCIRE volume, the protected guard process, whole-disk
write protection, and the kexec-injected ring-3 log agent.
Implementation: `ocos/services/ocembed/` -
`interface/svc_ocembed_{api,core}.h`, `src/svc_ocembed_core.c`
(599 lines); the agent itself is `ocire/agent/ocire_agent.c` (an
independent ABI consumer); the shared boot-state contract is
`include/ocbl_state.h`. See also [../dev/ocire.md](../dev/ocire.md)
for the recovery kernel as a whole.

## 1. What "embed" means

OCOS can be kexec'd by **OCIRE**, an *independent recovery
supervisor kernel*. The supervisor owns the machine first, passes
boot facts on the kernel command line - `oc.log=<LBA>`,
`oc.logsecs=<N>`, `oc.mb=<LBA>`, `oc.wdt=ib700`, `oc.agent=1` -
and attaches the log agent as a multiboot2 module. On a normal
GRUB boot no fact is present and the service stays dormant
(`ocire: embed inactive (normal boot)` - correct behavior, not an
error).

The OCOS side (`ocembed_boot_confirm()`, kmain step 16d) closes
the loop:

1. **locate the recovery disk**: scan `BLK_MAX_DEVICES` for the
   device covering `g_mb_lba` whose sector reads the OCIREMB2
   magic;
2. **protect FIRST**: `driver_block_set_protected(dev)` before any
   signalling;
3. **signal**: mailbox WAIT_BOOT_SIGNAL -> SIGNAL_OK (+boot_count);
4. **disarm the i6300esb** watchdog the supervisor left armed
   (must happen within the 60 s supervised window - "we are ~2 s
   in");
5. **spawn the agent**: `user_process_create(img, len, "ocire_guard")` + set the protection flag;
6. **arm whole-disk write protection**.

Failure honesty: `ocire embed: NO block device covers the mailbox - signal/watchdog/agent skipped, protection NOT armed`;
`ocire embed: boot signal FAILED - see log`;
`ocire embed: i6300esb not found on the bus (mailbox-only signalling)`;
`ocire embed: agent spawn FAILED - the recovery log channel stays unused`;
`ocire embed: no log agent injected (the recovery log channel stays unused)`.

## 2. The mailbox (verbatim)

```c
#define OCIRE_MB_MAGIC        "OCIREMB2"
#define OCIRE_MB_BYTES        128
#define OCIRE_MB_STATE_WAIT_SIGNAL 1
#define OCIRE_MB_STATE_SIGNAL_OK   2

#define OCIRE_LOG_MAGIC       "OCIRELOG1"
#define OCIRE_LOG_HDR_BYTES   16

#pragma pack(push, 1)
typedef struct {
    char magic[8];
    u32  state;
    u32  boot_count;
    u32  fail_count;
    u32  log_start_lba;
    u32  log_sectors;
    u8   agent_sha256[32];
    u8   reserved[68];
} ocembed_mailbox_t;
#pragma pack(pop)
```

State machine: the supervisor leaves the mailbox in
`WAIT_SIGNAL`; OCOS writes `SIGNAL_OK` **only** from that state -
other states are reported honestly, never rewritten. The 32-byte
`agent_sha256` pins the agent image the supervisor expects; the
mailbox lives on the OCIRE volume where OCOS cannot write once
protection is armed.

## 3. The log ring

Layout (verbatim comment): "sector 0 is a 16-byte header (magic
'OCIRELOG1', u32 LE cursor = bytes used in the payload, u24 LE
sequence = wrap counter); sectors 1..N-1 are the payload. cap =
log_sectors*512 - 512. On overflow the cursor wraps to 0 and the
sequence bumps (the reader dumps [0, cursor))."

- **Append order is crash-safe**: the header is updated LAST - a
  torn append loses at most the not-yet-committed bytes, never
  the ring structure.
- A hostile header (`w > cap`) is clamped.
- Ops: `L1SVC_OCIRE_LOG_APPEND` (4) appends `arg` (up to the
  4096-byte sys_l1svc buffer that exists specifically for this
  channel); `L1SVC_OCIRE_KLOG_SNAPSHOT` (5) renders the kernel
  log ring into `out`.
- Anchors: `ocire: appended N bytes`, `ocire: append FAILED (ring io)`, `ocire: empty append`, `(kernel log ring empty)`.

## 4. The guard process and the guard-only channel

The injected agent is spawned as the ring-3 process
**`ocire_guard`** with the `ocire_guard` flag set:

- **kill is refused**: `sys_kill` refuses every signal to a
  guarded process;
- its **binary lives on the OCIRE volume** where OCOS cannot
  touch it (the whole-disk protection below);
- the L1SVC cat 18 channel is **guard-only**: `caller_is_guard()`
  checks the current process's flag; any other caller gets
  `ocire: refused (guard-only channel)` in `out` plus a WARN
  klog (`L1SVC ocire op=%u refused: caller is not ocire_guard`);
- the ops 4/5 are the agent's **only two doors** into the kernel,
  and the numbering is a frozen ABI
  (`ocire/agent/ocire_agent.c` compiles against the numbers - an
  ABI, not a source dependency).

This is the log agent's architecture: OCIRE kexecs OCOS, OCOS
spawns OCIRE's agent as a protected ring-3 process, and the agent
ships OCOS's own kernel-log snapshots back to the OCIRE volume
through the two guard-only ops.

## 5. Whole-disk write protection

When the embed is active (3.0.0-WP03 G4):

- **every write through a covering block device is REFUSED** -
  reads stay open;
- `driver_block_read/write_sectors_trusted` is the kernel-internal
  channel for the embed service and partition children -
  "Never callable from ring 3";
- observable effect: `disk_rw_test hda` fails by design under a
  supervised boot.

## 6. Service declaration and interplay

```c
OC_SERVICE_DECL_L1("ocire", "ocire embed service (boot signal + agent channel + whole-disk protection + ring-3 surface)", NULL, NULL, ocembed_l1_dispatch);
```

The declared category-name exception: the category string
**"ocire"** (the frozen ABI name) is implemented by the
`services/ocembed/` folder.

Inter-service edges: ocembed -> block (trusted RW), klog
(snapshot), usermode (the guard flag). The boot-facts parsing
runs early (`ocembed_early_init(cmdline)`, kmain step 3b) so the
supervised-boot decisions are made before the service registry
even reports.

## 7. Verification

`tools/run_ocire_supervised_tests.py` (WP-03 G4) drives the
OCIRE-supervised boot for real: the 60 s watchdog window, the
mailbox signal, the agent spawn and its log channel.
`tools/run_ocire_tests.py` / `run_ocire_dual_tests.py` /
`run_ocire_shell_tests.py` cover the OCIRE side (standalone
boot/help/unknown/backspace/shutdown x BIOS+UEFI, dual-boot +
partition isolation, the recovery-shell battery incl. digest
damage + nuked BPB).
