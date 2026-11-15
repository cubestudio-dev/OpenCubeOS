<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hidden mechanisms: the load-bearing behaviors you cannot see from any single file

These are the mechanisms that make Open Cube OS work but live *between*
files, *under* interfaces, or inside fixed magic constants. Each entry
states the mechanism, its trigger condition, where the code lives, and
what it affects. This page exists because a reader who knows every
directory separately still cannot predict the system's behavior without
these twelve.

## 1. The 0x400000-0x600000 identity-window trap

**Mechanism.** User address spaces are created by
`create_user_address_space()` in `ocos/kernel/core_usermode.c`: a fresh
PML4, kernel PML4[1..3] copied, a *private* PDPT[0]+PD0 copy of the
kernel's 512 x 2 MiB huge PDEs - and then `PD0[2]`, which covers virtual
0x400000-0x600000, is **replaced by an empty page table**. Ring-3 code
links at 0x400000 (`ocos/l1apps/user.ld`), so every user program lives
exactly in that window. The physical frames backing the old identity
window would be invisible from any user CR3.

**Trigger.** Every process creation and every fork address-space copy.

**Code.** `ocos/kernel/core_mm_vmm.c` (the walk/flag machinery,
`VMM_FLAG_PT_OWNER` below), `ocos/kernel/core_usermode.c` (the PD0[2]
swap - the P3-1 security fix: never pre-populated with kernel phys
pages), `ocos/kernel/arch/x86_64/linker.ld` (`ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the 0x400000 identity-window boundary - move large buffers to kmalloc")`), `ocos/kernel/core_mm_pmm.c` (the
window reservation).

**Impact.** Three cooperating defenses: (a) the linker refuses any
kernel image that would reach the window; (b) the PMM *permanently
reserves* the 512 frames (2 MiB) of physical 0x400000-0x600000 at init -
they can never be handed out as page tables or DMA targets, because the
kernel could not safely dereference them while running on a user CR3;
(c) user ASes see an empty PT there, so a user-mode dereference faults
instead of aliasing kernel memory. Consequence for every developer: big
static buffers are forbidden in the kernel image - that is why the TCP
conn table, the socket table, the crash dump buffer and pipes all live
on the heap ([network-stack.md](network-stack.md),
[crash-recovery.md](crash-recovery.md)).

## 2. `core_kcap` - the console-hook capture stack

**Mechanism.** The console driver (`ocos/kernel/core_console_screen.c`)
holds **exactly one output-hook slot**. `core_kcap_run_fn(fn, arg, out, cap)` in `ocos/kernel/core_kcap.c` pushes a capture onto a stack (up to
4 levels), makes the innermost capture THE console hook, runs `fn`, and
pops. While a capture is active, every character the function prints is
collected into the caller's buffer - and **neither the framebuffer nor
the serial tee sees it**. The base hook (the serial encoder tee,
installed by kmain) is reinstalled when the last capture ends. Overflow
is marked, never silently truncated.

**Trigger.** Every `SYS_L1SVC` call whose op body is a legacy
print-style command: the syscall path routes `sys_l1svc -> <cat> dispatch -> core_kcap_run_fn(body, ...)`. This is *the* mechanism that
lets 208 service ops keep their v1/v2.2.0 `screen_console_puts` bodies
and still return text to ring 3.

**Code.** `ocos/kernel/core_kcap.{c,h}` (147 lines - the smallest
load-bearing file in the tree); consumers in every
`ocos/services/*/src/svc_*_l1.c`.

**Impact.** The whole L1SVC text surface rests on it
([syscall-ring3.md](syscall-ring3.md)). Its limits are contract:
captured bodies must not sleep or park - `power sleep` and `sshd` and
all 9 TUI ops route **direct** (`out[0]=0` and live console output)
because running them captured would swallow live console lines (found
live: a supervised boot with the OCIRE log agent went console-silent
the moment the agent's first sleep started). Nesting: a service body
that itself calls another captured body nests up to 4 deep; only the
innermost active capture accepts data.

## 3. OCST - the boot-state relay across three kernels

**Mechanism.** Two 4 KiB low-memory islands that survive QEMU warm
resets, carved E820-reserved in the multiboot2 map OCBL builds:

```c
#define OCST_STATE_ADDR        0x98000u
#define OCST_RESERVED_ADDR     0x9B000u
#define OCST_STATE_MAGIC       0x3153434Fu   /* "OCS1" */
#define OCST_RESERVED_MAGIC    0x3142434Fu   /* "OCB1" */
```

The state area (0x98000) carries `stage` (RESET=0, KM_AIN_ENTERED=1,
SCHED_READY=2, DRIVERS_READY=3, SERVICES_READY=4, L1_LAUNCHED=5,
L1_READY=6, RUNTIME=255), a heartbeat and a progress clock. The
reserved area (0x9B000) carries the post-mortem `ocst_record_t` (kind
BOOT_FAILED or USER_REQUEST, silence seconds, ones-complement
checksum). The full layouts live in `include/ocbl_state.h`, guarded by
`_Static_assert` (24 and 28 bytes).

**Trigger.** Normal boot: OCBL arms a 30 s boot budget (10 s per phase,
30 s L1 wait, 15 s runtime heartbeat) on the Intel 6300ESB watchdog
(PCI 8086:25AB) before handing off to OCOS; the kernel
(`core_boot_state.c`) takes the watchdog over and **blind-writes**
stage/heartbeat - it never reads the islands back; kmain's 1 s tick
pets the dog. Failure: if the kernel hangs, the watchdog hard-resets;
next boot OCBL reads the islands, judges failure at **>= 8 s RTC
silence**, writes a BOOT_FAILED record, shows a 4-second notice, pulses
the 8042 reset - and the boot after *that* auto-routes to the OCIRE
entry, one-shot (`ocbl_statemon_route_pending()`).

**Code.** `include/ocbl_state.h` (the tri-build contract),
`ocbl/common/ocbl_statemon.c` (judge/arm/route),
`ocos/kernel/core_boot_state.c` (the monitored side - parses `oc.hang=N`
and `oc.stmon=off`), `ocire/ocire_ocbl.c` (the post-mortem reader that
explains *why* the machine is in recovery, with per-stage repair
advice).

**Impact.** A bricked machine self-diagnoses across three independent
kernels with no shared code and no writable disk in the loop. OCIRE and
OCSE handoffs get `ocbl_wdt_disarm()` instead of the statemon arm (they
own their own watchdog story - a blind 60 s window used to reset a
still-running installer session, the WP07a fix). Details:
[crash-recovery.md](crash-recovery.md).

## 4. IST discipline - the three stacks of the exception path

**Mechanism.** The TSS (`arch_idt.c`) carries IST1 = 8 KiB
`g_panic_stack` (vectors 8 #DF and 18 #MC) and IST2 = 8 KiB
`g_debug_stack` (vectors 1 #DB, 2 NMI, 3 #BP). A ring-3 kill taken on
an IST2 vector must never run on IST2 itself - the scheduler would save
an rsp inside IST2 and the next resume would corrupt it. So the kill
path switches to a dedicated **8 KiB exit stack** first:
`arch_exc_run_on_exit_stack()` (`arch_exc_exit.S`, the BUG-0139 fix)
before reap+yield.

**Trigger.** Any exception delivery: the CPU hardware picks the IST from
the IDT gate; `arch_exc_dispatch` decides user-kill vs panic; vectors
1/2/3 take the exit-stack detour.

**Code.** `ocos/kernel/arch/x86_64/arch_idt.c` (TSS/IDT setup, IST
assignment), `arch_idt_stub.S` (the 256 stubs - error-code vectors skip
the fake push), `arch_exc_exit.S` (42 lines),
`arch_exceptions.c` (the dispatch chain).

**Impact.** #DF/#MC panics get an unwritable-by-anything-else stack
(the panic machinery can trust its own stack); debugger/breakpoint
activity in ring 3 cannot wedge the scheduler; and the "kernel stack
overflow" attribution (#PF on a guard page, #DF with rsp below stack
bottom - both naming the task) works even at dispatcher depth.
[interrupts.md](interrupts.md) has the full vector map.

## 5. The lazy gzip program registry - 284 apps, one at a time

**Mechanism.** `tools/gen_userprogs.py` compiles every
`ocos/l1apps/<app>/` folder against `ocos/interface/` only, gzip's each
ELF, and emits `ocos/kernel/userprogs_data.h` (27,082 lines):
`userprog_gz_<name>[]` byte arrays plus the `OC_USERPROGS[]` registry
(name, gz pointer, raw pointer, gz_len, raw_size, wp category tag,
bilingual hints). At `SYS_EXECVE` / `core_userprog_lookup()` time the
kernel inflates the app on demand (`core_lib_gzip.c`, RFC 1951/1952
with CRC32+ISIZE verification) and caches the raw ELF per app.
**`ush` stays raw** - uncompressed, linked into the image - because
`OC_L1_APP_DECL` (the default-entry declaration in
`core_boot_l1launch.c`) needs compile-time ELF pointers.

**Trigger.** First execution of any of the 284 programs; every
`ldd <program>` introspection; `job_create`.

**Code.** `ocos/kernel/userprogs_data.h` (generated),
`core_usermode.c` (`core_userprog_lookup`), `core_lib_gzip.c`,
`tools/gen_userprogs.py`.

**Impact.** The kernel image stays under the 0x400000 linker ASSERT
(see mechanism 1) while shipping 284 programs - the compressed payload
dominates the image but stays affordable, and the uncompressed-ELF
cache lives on the heap, not in .bss. The `l1` service's `help` op
renders FROM the registry (no hand-written command lists anywhere), and
`-w` groups by the wp tag ([syscall-ring3.md](syscall-ring3.md)).

## 6. `.oc_svcs` / `.oc_drvs` / `.oc_apps` - services are not processes

**Mechanism.** "Services" are link-time-registered kernel objects, not
forked daemons. `OC_SERVICE_DECL`/`OC_SERVICE_DECL_L1`
(`ocos/services/svc_framework.h`) place a descriptor into the `.oc_svcs`
linker section; `OC_DRIVER_DECL` (`ocos/drivers/drv_framework.h`) does
the same into `.oc_drvs`; `OC_L1_APP_DECL` into `.oc_apps`.
`linker.ld` exports `__oc_svcs_start/_end` (and the driver/app
equivalents); at boot `svc_framework_collect()` / `drv_framework_collect()`
/ `core_registry_collect()` walk the sections into static tables (64
services, 256 drivers, the app registry) and kmain logs
`registry: drv=N svc=N app=N`. Dispatch is a category-string walk:

```c
int svc_framework_dispatch(const char *cat, u32 op,
                            const char *arg, char *out, int cap)
{
    if (!cat || !out || cap <= 0) return -1;
    for (int i = 0; i < s_nsvcs; i++) {
        const oc_service_decl_t *s = s_svcs[i];
        const char *a = s->category, *b = cat;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (!s->l1_dispatch) return -1;   /* kernel-only service */
            return s->l1_dispatch(op, arg, out, cap);
        }
    }
    return -1;
}
```

**Trigger.** Boot collection (kmain step "registry: drv=N svc=N
app=N"), then every `SYS_L1SVC` call and every family probe pass.

**Code.** `ocos/services/svc_framework.{c,h}`,
`ocos/drivers/drv_framework.{c,h}`, `ocos/kernel/core_registry.{c,h}`,
`ocos/kernel/arch/x86_64/linker.ld` (the `SUBALIGN(8)` identity
sections).

**Impact.** The NAK rule ("everything is extensible by adding a file"):
a new driver or service is one `.c` file with one declaration line - no
global table edit, no registration call site. Boot ORDER stays the
explicit hand-curated kmain sequence; the collected tables are only the
identity/dispatch surface. Nothing is forked at boot except the ring-3
launcher (ush) and, on supervised boots, the OCIRE log agent. The
`placeholder` driver (`ocos/drivers/misc/drv_misc_placeholder.c`) is a
deliberate live test of the mechanism - collected, never probed.
[driver-model.md](driver-model.md) walks the driver side.

## 7. The OCIRE guard channel - a second kernel's mailbox

**Mechanism.** On OCIRE-supervised boots, OCOS starts with extra
cmdline facts (`oc.log=<lba> oc.logsecs=<n> oc.mb=<lba> oc.wdt=ib700 oc.agent=1`). The `ocembed` service
(`ocos/services/ocembed/src/svc_ocembed_core.c`) then:

1. finds the recovery disk by scanning block devices for one covering
   the mailbox LBA whose sector reads the `OCIREMB2` magic;
2. **write-protects the whole disk first** (`driver_block_set_protected`)
   - every later write through a covering device is REFUSED; only the
   kernel-internal `driver_block_*_sectors_trusted` channel may write;
3. confirms boot: mailbox state WAIT_BOOT_SIGNAL -> SIGNAL_OK
   (boot_count++), and disarms the i6300esb watchdog the supervisor
   armed ("we are ~2 s in" of its 60 s window);
4. spawns the log agent ELF as a process flagged `ocire_guard` -
   `sys_kill` refuses every signal to a guarded process, and the binary
   lives on the OCIRE volume that OCOS cannot write at all.

The agent's ONLY two doors into the kernel are the guard-only L1SVC ops
4 (LOG_APPEND - append to the `OCIRELOG1` boot-log ring) and 5
(KLOG_SNAPSHOT - kernel log ring text); `caller_is_guard()` checks the
process flag and refuses everyone else with
`"ocire: refused (guard-only channel)"`. The mailbox (128 bytes, packed):

```c
#define OCIRE_MB_MAGIC        "OCIREMB2"
#define OCIRE_LOG_MAGIC       "OCIRELOG1"

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

**Trigger.** Only OCIRE kexec boots (the mailbox LBA fact on the
cmdline). On a normal OCBL/GRUB boot the service is dormant:
`"ocire: embed inactive (normal boot)"`.

**Code.** `ocos/services/ocembed/` (the whole family; category string
`"ocire"` - the one declared exception to the folder-name rule, frozen
ABI), `ocire/ocire_bootsvc.c` + `ocire/ocire_kexec.c` (the supervisor
side), `ocire/agent/ocire_agent.c` (the agent loop: sleep 5 s, snapshot,
append), `ocos/services/block/` (the trusted-RW channel).

**Impact.** Two-layer protection (unkillable process + unwritable
binary volume), crash-safe log ordering (the ring header is updated
LAST, so a torn append loses at most the uncommitted bytes), and a
honest degradation path for every failure (`agent spawn FAILED - the recovery log channel stays unused`). The 4096-byte arg buffer carved
for cat 18 in `sys_l1svc` exists specifically for the agent's 4 KiB
appends.

## 8. `/HANGAT` - boot-failure injection from shipped images

**Mechanism.** OCBL (both flavors) probes a 1-byte file `/HANGAT` on
the payload volume; if it contains `'1'..'7'`, OCBL silently appends
`oc.hang=N` to the kernel cmdline. The kernel's
`core_boot_state.c` parses it and deliberately halts after writing
stage N ('7' = post-L1_READY runtime hang) - watchdog armed.

**Trigger.** The file's presence, that's all. A complete no-op without
it - which is the point: the watchdog -> reset -> OCBL judgment ->
OCIRE route chain (mechanism 3) can be exercised **from shipped
images**, no test-built kernel needed.

**Code.** `ocbl/common/ocbl_statemon.c` (the probe),
`ocbl/common/ocbl_main.c` + `ocbl/efi/efi_entry.c` (the call sites),
`ocos/kernel/core_boot_state.c` (the halt).

**Impact.** The whole recovery story is regression-testable against
the release ISO (`tools/wp07a_state_e2e.py`,
`tools/wp12_watchdog_logging_e2e.py` drive it). Related cmdline
switches on the same parse path: `oc.stmon=off` disables the entire
state monitor.

## 9. HMAC_DRBG - the entropy chain

**Mechanism.** `crypto_random()` is a full NIST SP 800-90A HMAC_DRBG
(state `drbg_k[32]`/`drbg_v[32]`, `drbg_update` per §10.1.2), seeded
from RDRAND when CPUID offers it (8 bytes at a time, carry-flag
checked), otherwise re-mixed from RDTSC + boot jitter; bytes 24-31 are
always xored with `rdtsc ^ (core_timer_ticks() << 1)`. Instantiated at
first use, reseeded on demand, and protected by the crypto IRQ window
(a single-core `pushfq/cli/popfq` pair, NOT a lock - documented as a
build-environment property) so concurrent kthreads can never observe
the same generated block.

**Trigger.** Every TLS handshake, every SSH session, every place the
kernel needs randomness.

**Code.** `ocos/services/crypto/src/svc_crypto_core.c` (BUG-0106
replaced the old timer-seeded xorshift32 - whose output made "both the
SSH server and the TLS client effectively transparent" per the source
comment), `svc_crypto_irq_window.h`.

**Impact.** Key material is no longer computable by a passive observer;
the embedded root CA store (`svc_crypto_roots.h`, generated by
`tools/embed_roots.py` - ISRG X1/X2, GlobalSign, GTS R1/R4, DigiCert,
Baltimore, Amazon, Microsoft, Sectigo E46/R46) makes the TLS chain
verifier self-contained. [../internals/drbg-crypto.md](../internals/drbg-crypto.md).

## 10. Health -> watchdog linkage - the alive-but-alarmed feed

**Mechanism.** The health service samples every second
(`health_sample_t`: CPU from scheduler idle deltas, memory from
PMM+heap, tasks, disk, net counters) and evaluates three alarms:
memory > 90% used, active tasks > 64, CPU >= 90% busy for 10
consecutive samples. Alarms are deduplicated (one ERROR klog per
crossing). Independently, the alarms **feed the watchdog subsystem's
slot 0 ("system-heartbeat")** as an extra feed source.

**Trigger.** The 1 s sampler tick, registered by `health_init()` in
kmain.

**Code.** `ocos/services/health/src/svc_health_core.c`,
`ocos/services/crash/src/svc_crash_watchdog.c` (slot 0, default
timeout 15 s, policy PANIC).

**Impact.** An alarmed-but-alive system keeps feeding the dog; a dead
one stops, and the WP-11 panic path (dump + crashlog + policy) fires
instead of a mute hang. The hardware watchdog (ACPI WDAT / Intel TCO)
is probed but **never armed at boot** - arming is explicit
(`watchdog hw start <sec>`, TCO range 2-63 s) and whenever both are
armed the hardware timeout is set >= 2x the software one.
[../internals/health.md](../internals/health.md),
[../internals/watchdog.md](../internals/watchdog.md).

## 11. Hardcoded driver probe priority - who wins the bus

**Mechanism.** The service-side family managers walk the collected
`.oc_drvs` table in fixed name order:

```c
/* svc_block_drivers.c */
static const char *const order[] = {
    "ata_dma", "ata", "virtio_blk", "ahci", "nvme", "scsi",
};
/* svc_net_drivers.c */
static const char *const order[] = {
    "e1000e", "igb", "rtl8168", "rtl8125", "rtl810x",
    "rtl8139", "bcm57xx", "ixgbe", "other",
};
/* svc_audio_drivers.c */
static const char *const s_snd_probe_order[] = {
    "hda", "ac97", "es1370", "usbaudio", "sb16", "virtio",
};
```

**Trigger.** `block_svc_probe_all()` / `nic_svc_probe_all()` /
`snd_svc_probe_all()` during kmain's driver phase.

**Code.** `ocos/services/{block,net,audio}/src/svc_*_drivers.c`.

**Impact.** Block: BMDMA probes first so a drive may register with DMA
ops; the PIO pass then skips drives already taken (`blk_dev_set_ops()`
upgrades a PIO drive in place). NIC: **first probe that registers a
card wins** - the kernel routes through a single NIC; wireless bridges
in later via `wl_nic_bridge_register()`. Sound: unlike nic, **all**
families are probed - multiple cards coexist. The order lists are the
only place driver priority is decided; the registration mechanism
itself (mechanism 6) is order-free. [driver-model.md](driver-model.md).

## 12. QEMU errata compiled into the drivers

**Mechanism.** Where a driver had to deviate from the letter of the
hardware spec to actually work under QEMU, the deviation and its
evidence are embedded as BUG-numbered inline comments - the tree's
substitute for a formal errata document. The two biggest:

- **AHCI 16-byte PRDT entries** (`ocos/drivers/storage/drv_storage_ahci.c`):
  real AHCI 1.3.1 specifies 8-byte PRDT entries; QEMU 10.x parses
  16-byte entries (`u64 addr + u32 rsvd + u32 flags_size`, flag nibble
  at bit 31) matching `hw/ide/ahci.c populate_sglist()`. The driver
  uses the QEMU layout, plus a 4 KiB per-port **bounce buffer** chunked
  to <= 8 sectors so a single PRDT entry suffices, and a stop/clear/
  start bounce retry because QEMU rejects the FIRST command after port
  start.
- **SB16 8237 page registers** (`ocos/drivers/audio/drv_audio_sb16.c`):
  ISA DMA channel 5 lives on the *second* 8237 with a scrambled page
  register map - ch5 -> 0x8B, ch6 -> 0x89, ch7 -> 0x8A (BUG-0127: the
  code previously programmed channel 6's page register, so buffers
  >= 1 MiB streamed from the wrong page); mode byte 0x49 single-cycle
  read for ch5 (BUG-0128).

**Trigger.** Every I/O through the affected drivers (AHCI on any
QEMU SATA disk; SB16 playback).

**Code.** The two files above, plus the whole family of similar notes:
the NVMe register map and admin-opcode fixes (WP-10a), the igb SRRCTL
1 KiB units (BUG-0333), the EHCI link-type tag bits 2:1 (BUG-0175),
the virtio-blk legacy register offsets, the rtl8139 TSD OWN semantics,
the first-command AHCI bounce.

**Impact.** The drivers work on the only platforms the project can
actually test (QEMU machines), and every deviation is auditable in
place - each comment cites the QEMU source file or Linux version it
was resolved against. The flip side is honestly documented too:
datasheet-only drivers (ixgbe, rtl8169 family, bcm57xx, virtio-snd)
state they were never exercised on real hardware, and their test
commands report SKIP rather than fake output.

## Bonus round - small traps worth knowing

- **`build_test_iso.py` deliberately omits `-boot-info-table`**: the
  xorriso flag would zero bytes 8..0x47 of the El Torito image and
  destroy OCBL stage1's entry code (found live as a silent hang).
- **The EFI flavor zeroes its own .bss at entry**: EDK2's loader does
  not guarantee the raw-vs-virtual slack (live-caught `g_ndisks` =
  2.9 billion).
- **The dead `user_process_kill(pid)` tombstone**: explicitly deleted
  with a comment warning against resurrecting it (three invariant
  violations); the unified reaper `user_process_kill_by_tid` is the
  only sanctioned kill path.
- **Early-EOI policy** on both PIC and MSI paths, so a context switch
  inside a handler cannot wedge the interrupt controller.
- **The 0xEE shadow trick**: the OCIRE recovery disk's MBR carries a
  single type-0xEE (GPT-protective) entry with NO GPT header;
  `ocbl_part.c` special-cases it (`OCBL_PT_SHADOW`) and the 16 MiB
  FAT12 volume behind it is both GRUB-blocklist-loadable and invisible
  to normal GPT tooling.
