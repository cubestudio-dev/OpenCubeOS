<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internal call chains

Six dissections of the paths that matter most when reading or
changing the kernel. Each is presented as numbered stages with the
trigger -> processing -> impact contract and the file that owns
each stage. Addresses, constants and error strings are from
source; nothing here is speculative.

## 1. The execve path (running a program)

**Trigger**: ush parses a command line that is not a built-in (or
a program calls `SYS_EXECVE` directly, or `job_create`/`run`
wraps it).

1. **ush dispatch** (`ocos/l1apps/ush/main.c`): the line is split
   (`$VAR` expanded, pipes/redirection handled), argv assembled in
   static storage, `fork()` then `execve(name, argv)` in the child;
   the parent `wait4`s (foreground) or records a job slot
   (background `&`).
2. **Syscall** (`SYS_EXECVE` = 11, `core_usermode.c`): the name is
   looked up via `core_userprog_lookup(name, &size)` - the shared
   name-to-embedded-ELF lookup over the generated
   `OC_USERPROGS[]` registry (`userprogs_data.h`, 284 entries,
   each its own gzip stream; ush alone stays raw because
   `OC_L1_APP_DECL` needs compile-time ELF pointers).
3. **Lazy gunzip** (`core_usermode.c` + `core_lib_gzip.c`): on the
   FIRST exec of an app, the gzip blob is inflated into a
   per-program heap cache (`g_prog_cache[OC_USERPROGS_COUNT]`);
   later execs reuse the cached raw ELF. The inflater is a full
   RFC 1951/1952 implementation (stored/fixed/dynamic Huffman,
   resumable streaming feed, CRC32+ISIZE verification).
4. **ELF load** (`core_usermode.c`): ET_EXEC is mapped at the fixed
   user layout (code base 0x400000); ET_DYN (PIE) routes to the
   loader family (`services/loader`): PT_INTERP is parsed, ld.so is
   mapped at the fixed 0x10000000, the main program plus known
   .so images at their bump bases, ASLR via
   `user_aslr_page_slice` (two rdtsc samples + splitmix64
   finalizer), then control transfers to ld.so's entry - ld.so
   (ring 3) resolves .dynamic, patches GOT/PLT and jumps to
   `app_main`'s caller `_start` (the shared `crt0.asm` reads
   `[rsp]`=argc / `[rsp+8]`=argv, aligns, calls `app_main`).
5. **Address-space construction**: fresh PML4; kernel PML4[1..3]
   copied; PRIVATE PDPT[0]+PD0 (copy of the kernel 512 x 2 MiB
   huge PDEs); PD0[2] (0x400000-0x600000) replaced by an *empty*
   PT (never pre-populated with kernel phys pages); stack top
   0x40000000 (64 KiB).
6. **Scheduling**: the new task occupies one of 16 user proc slots
   / 32 task slots, priority 15 default; the parent's wait4
   completes when the unified reaper (`user_process_reap_resources`)
   runs after exit - from both tick and yield, under cli
   (BUG-0282 double-free fix).

**Impact**: the first-syscall gate - the FIRST syscall from the
default L1 entry (pid == `g_l1_entry_pid`) flips
`OCST_STAGE_L1_READY`, the one-shot boot-state load on the hot
path.

## 2. One L1SVC call end to end

**Trigger**: a ring-3 wrapper calls
`syscall5(SYS_L1SVC, cat, op, arg, out, cap)`.

1. **Ring-3 wrapper** (`ocos/interface/l1_lib.h`): `l1svc_main`
   joins argv into a 256-byte arg line; `out` is a static 32 KiB
   BSS buffer.
2. **Entry**: `int 0x80` -> IDT vector 128 (DPL=3 gate) -> stub
   pushes 15 GPRs + fake error code -> `arch_isr_dispatch` ->
   `core_syscall_dispatch(frame)`; `r8` (5th arg, cap) is stashed
   in `g_syscall_arg5`.
3. **sys_l1svc** (`services/sysapi/src/svc_sysapi_syscall.c`):
   validates out/cap (1..32768, or the 0/0 fire-and-forget
   spelling), copies `arg` into the 256-byte kernel buffer (4096
   for cat OCIRE), `sti`.
4. **Service dispatch** (`services/svc_framework.c`):
   `svc_framework_dispatch(cat, op, arg, out, cap)` scans
   `s_svcs[64]` for the exact category string and calls the
   service's `l1_dispatch(op, arg, out, cap)`.
5. **kcap capture** (`kernel/core_kcap.c`): captured-style ops run
   inside `core_kcap_run_fn(fn, arg, out, cap)` - the capture hook
   IS the console hook (one slot) while active: characters collect
   into `out`, framebuffer draw and the serial tee are suppressed,
   captures stack to 4 levels (only the innermost active capture
   accepts data), overflow is marked never silently truncated; the
   base hook (the boot serial tee installed by kmain) is
   reinstalled when the last capture ends.
6. **Return**: `cli`; the service's own non-zero exit status is
   still a successful call - only an unrouteable cat/op (or empty
   out with rc&lt;0) returns -1; otherwise the NUL-terminated text is
   copied back and 0 returned. Signal delivery (if any) happens
   after the handler, before `iretq`.

**Impact**: this single door carries all 208 ops; the
`sti/cli` window is what lets device-wait loops inside op bodies
be tick-driven instead of freezing the machine.

## 3. The network receive path

**Trigger**: a frame arrives at the NIC (or the 10 ms soft-timer
fires the poll).

1. **Driver layer**: e1000 rings (`e1000_desc_t`, 2048-byte
   buffers) / virtio-net queues (`net_virtq_*`, VIRTIO_BUF_SIZE
   2048) / the WP-10b framework cards expose
   `driver_nic_ops_t.recv(dev, buf, maxlen)` - polled, returning
   one frame (>0), 0 when none, -1 on error.
2. **Poll engine** (`services/net/src/svc_net_core.c`):
   `net_start_timer()` registers
   `core_timer_register_periodic(net_timer_cb, NULL, 10)` -
   `net_poll` every **10 ms**; `net_poll` is also called from
   thread context in `net_recv`'s wait loops (socket reads block
   productively).
3. **Ethernet demux** (`eth_hdr_t`, types ARP 0x0806 / IP
   0x0800): ARP feeds the 16-entry cache
   (`g_arp_cache[ARP_CACHE_SIZE]`); IP continues.
4. **IP** (`net_ip_hdr_t`): fragment reassembly
   (`IP_FRAG_MAX_FRAGS 8`, 8*1480 buffer, 5 s timeout; stats for
   reassembled/dropped/evicted - BUG-0210); bad-checksum
   datagrams are counted and dropped
   (`net_rx_bad_checksum`, BUG-0072).
5. **Netfilter INPUT chain** (`g_nf_rules[NF_MAX_RULES=32]`,
   conntrack `g_nf_ct[32]`, 60 s timeout; ACCEPT/DROP/REJECT,
   NEW/ESTABLISHED): a DROP ends the walk here, counted.
6. **L4 demux**: ICMP echo -> ping responder; UDP -> the
   32-handler table (`g_udp_handlers`, DHCP 68/67, DNS 53 client);
   TCP -> the 32-connection engine (`net_tcp_conn_t` on the kernel
   heap, ~520 KiB): sequencing, SACK blocks, out-of-order cache,
   CUBIC congestion window (RFC 8312 integer math,
   `cubic_state_t`), RTO with srtt/rttvar + Karn's rule,
   timestamps, window scaling.
7. **Socket** (`g_sockets[MAX_SOCKETS=32]`, heap): `SYS_NETRECV`
   finds the socket's rx buffer (TCP rx_buf 8192 - was 2048 before
   WP-09-FIX BUG-002) and copies to the user buffer; the blocked
   reader wakes.

**Impact**: `netstat`/`tcpstats` read the very tables this chain
mutates (`net_stats_t` counters, `g_tcp_conns[]` CUBIC internals);
`net_e1000_resume()` replays legacy e1000 register programming
after ACPI S3 resume.

## 4. The block I/O path

**Trigger**: a filesystem (or partition parser, or the installer)
reads or writes sectors.

1. **fs layer** (services/fs): FAT32/exFAT/ext4 read metadata and
   data through `driver_block_read/write_sectors` (index form) or
   the pointer form `driver_block_read/write(dev, lba, count, buf)` - never directly to a driver.
2. **Cache gate** (`services/block/src/svc_block_cache.c`): the
   64-slot LRU write-back cache (`driver_block_cache_slot_t`, 32
   KiB total) is consulted per (dev_idx, lba); hits update
   `last_used`; misses pick a victim - empty first, then LRU order,
   and a dirty victim is only released when its write-back
   SUCCEEDS (BUG-0116: the old code ignored the write-back return
   and silently dropped data); if every dirty slot fails, callers
   fall back to write-through.
3. **Device dispatch** (`svc_block_drivers.c`): the device's
   `driver_block_ops_t.read/write` runs; `.flush` (optional) is
   invoked by `driver_block_flush(dev)` after pushing dirty
   sectors (ATA FLUSH CACHE / NVMe Flush / virtio-blk FLUSH).
4. **Driver** (drivers/storage): ATA PIO LBA28/LBA48 or Bus-Master
   DMA (PRDT at BAR4, PIO fallback preserved); AHCI READ/WRITE DMA
   EXT through a per-port 4 KiB page-aligned bounce buffer split
   into chunks of at most 8 sectors (one PRDT entry per chunk);
   NVMe Read/Write (0x02/0x01) with 64-bit PRP over two I/O queue
   pairs; virtio-blk request/reply descriptors.
5. **DMA discipline**: transfer buffers respect the heap's 32-byte
   header/16-byte alignment contract (the odd-parity 40-byte
   header bug shifted ATA-DMA buffers by 8); xHCI refuses DMA
   above 4 GiB at probe and per-transfer.
6. **Protection gate** (OCIRE embed): when the embed is active,
   every write through a covering device is REFUSED; the
   kernel-internal `driver_block_read/write_sectors_trusted` is
   the only writer (the embed service and partition children);
   never callable from ring 3.

**Impact**: `sync` (op BLK 11) flushes this chain end-to-end;
`dskstat`/`lsblk` report the per-device stats and cache counters;
`power shutdown` flushes the cache before S5.

## 5. The crash path

**Trigger**: a ring-0 exception no handler claims, a `kernel_panic`
call, an `OC_ASSERT` failure, or a watchdog policy PANIC.

1. **Vector entry** (`arch_idt_stub.S` -> `arch_exceptions.c`):
   nested-#PF guard first (a #PF inside #PF handling prints
   `*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***` and
   cli;hlt); the L1 handler chain (4/vector,
   most-recently-registered first) runs; #PF routes to
   `mem_vmm_handle_page_fault` (stack growth, protection
   violations never remap); ring-3-origin faults take the SIGSEGV
   delivery / kill path instead (vectors 1/2/3 kills run on the
   dedicated exit stack, leaving IST2 intact - BUG-0139).
2. **Panic routing** (`kernel_panic`/`kernel_panic_frame` ->
   handlers installed by services/crash via
   `arch_set_panic_handlers`): #DF is special-cased ENTIRELY at
   dispatcher depth with one static 192-byte serial line (no
   record, no dump, no flush - stated in KNOWN_ISSUES); every
   other source takes the full path.
3. **Normalization + first record**: CR3 switched to the kernel
   address space so traceback/dump/heap reads are safe;
   `klog_record(KLOG_FATAL, "kernel", reason)` writes the ring
   (ring only - no VFS at depth).
4. **Banner + snapshot** (`svc_crash_core.c`): serial-authoritative
   (VGA red best effort):
   `*** KERNEL PANIC: <reason> [task=<name>] ***`, the kernel
   identity line, `source: exception <n> (<name>) err=0x...`,
   task line, the register dump (4x4 grid r15..rax +
   rip/cs/ss/rflags/rsp/int/err + cr2 for #PF + cr3), rbp-chain +
   stack-scan traceback filtered by `looks_like_code`. Stack
   exhaustion is attributed: guard-page #PF or exhausted-stack #DF
   names the task in the reason.
5. **OCDUMP01 core dump** (`core_dump_save`): fills the
   boot-reserved buffer (`core_dump_alloc()` ran kmalloc once at
   kmain step 16a - after this point kmalloc may fail or the heap
   may corrupt, the panic path only touches the reserved buffer):
   header, 22-u64 registers (frame or live snapshot), task block,
   4 KiB stack window, CR3 + PML4 self-scan PTE count; persisted
   to `/var/crash/core.dump` (ramfs rootfs or FAT32); with NO
   filesystem the first 2048 bytes are hex-dumped to serial so
   data is not lost.
6. **Crashlog ring** (`crashlog_record`): a `crashlog_entry_t` in
   the 32-slot ring (vector or synthetic source 32..36; ring 0/3;
   err/rip/rsp/cr2; tid/pid/task; jiffies + rtc_epoch);
   `crashlog_flush()` appends everything to `/var/log/crash.log`.
7. **Policy**: registered panic handlers (up to 4,
   most-recently-registered first) may claim recovery
   (`*** panic handler claimed recovery - resuming ***`,
   `*** CONTAINED: ... - isolated, kernel continues ***` - the
   TUI-guard mechanism); otherwise `CRASH_POLICY_HALT` (default,
   cli + safe hlt) or `CRASH_POLICY_REBOOT` (8042 + 0xCF9),
   switchable live by `crashtest policy [halt|reboot]`.

**OCBL/OCIRE relay (boot failures)**: a hang that stops the
boot-state heartbeats stops feeding the 6300ESB -> hard reset ->
OCBL reads the 0x98000 state area, judges (silence >= 8 s, no
CLEAN marker), writes the BOOT_FAILED record at 0x9B000 -> next
boot auto-boots OCIRE -> OCIRE reads the record and prints the
stall point + repair advice. A *user-requested* OCIRE boot writes
a USER_REQUEST record instead. Full contract:
[data-structures.md](data-structures.md) section 8,
[../internals/crash-dump.md](../internals/crash-dump.md).

## 6. The OTA install path

**Trigger**: `update` (bare online flow), `update --local <pkg>`
(offline), or the update wizard's step 5 confirm.

1. **Check** (`L1SVC_OTA_CHECKUPDATE` -> `ota_update_check`):
   fetch `update.json` (HTTP or HTTPS per the configured
   `update_url`), compare `version` and `build`
   (`OC_KERNEL_BUILD`, numeric comparison - BUG-0347; the build id
   is `git rev-list --count HEAD + 21` so it stays monotonic above
   published ids). Anchors: "Current version is up to date." /
   "New version available.".
2. **Download** (`ota_update_download`): streamed over TCP/TLS to
   `/data/update-pkg.tar.gz` (default; tmp
   `/tmp/update-pkg.tar.gz`); failures print
   `"update: download failed: "`.
3. **Verify**: package SHA256 against the manifest
   (`update_verify`); the manifest also carries the payload sha256
   over concatenated member bodies in archive order.
4. **Decode**: gunzip (RFC 1952/1951, CRC32+ISIZE verified) +
   untar (streaming ustar with checksum verification, GNU 'L'
   long names, pax skip) directly into the **inactive slot**
   (slot B when running A; `ota_ab_target_slot()` - "update:
   cannot determine target slot (not booted from A/B?)" when
   running from ISO).
5. **Write boot flags**: `/boot/next_B` created; the new slot's
   kernel writes `/boot/bootfail_B` **pessimistically at boot**
   and removes it only when the system is fully up
   (`ota_update_confirm_boot` at kmain step 16c);
   `ok_B` marks a confirmed B. Boot selection (grub.cfg):
   `next_B` present AND `bootfail_B` absent -> boot B; both
   present -> ROLLBACK to A; `ok_B` -> B; otherwise A. Boot-attempt
   budget: GRUB env block `/boot/grub/grubenv` (1024 bytes,
   `ab_attempts`), `OC_AB_MAX_ATTEMPTS 3`, reset when A finishes
   booting or B confirms.
6. **Reboot**: the flow prints "reboot required"; `POWER/REBOOT`
   (8042 + 0xCF9) lands on the new slot; rollback
   (`L1SVC_OTA_ROLLBACK`) clears the flags.

**Impact**: a failed write never touches the running slot - the
active system keeps serving until the flip; the A/B disk layout
(p1 boot/flags 32 MB FAT32 + p2 slot A 128M + p3 slot B 128M + p4
/data) is created by `abdisk`, which re-runs `ota_ab_rescan()`
afterwards. Full page: [../internals/ota-ab.md](../internals/ota-ab.md).
