# Open Cube OS - WP-compat-and-security-hardening B2
# L1 boundary security audit (OCIRE fix + full-system sweep)
# Date: 2026-11-25
# Auditor: cubestudio-dev <cubestudio@qq.com>

Scope (workpack spec 4.1): the OCIRE zero-protection hole fix, plus a
full-system sweep of the three declared L1 attack surfaces:
  S1  L1 out-of-bounds reads of kernel pointers
  S2  L1 direct CR3/CR4 manipulation
  S3  L1SVC interface permission bypass

Method: static code walkthrough of every L1-reachable entry into L0
(the L1SVC syscall dispatch, the block-driver write surface, the diag
service ops, the ocembed guard channel), each backed by the exact
source lines cited below.  "PASS" = the path is closed; "INFO" = open
by design with a bounded, documented risk; "FIXED" = closed by this
workpack.

## 1. The OCIRE hole (the trigger finding) - FIXED

Hole: ocos/services/ocembed/src/svc_ocembed_core.c:519 (pre-fix)
`ocembed_boot_confirm()` opened with `if (!g_embed_active) return;` -
the whole-disk protection (`driver_block_set_protected()`) was only
armed on a SUPERVISED boot (the supervisor passed `oc.mb=<LBA>`).
On every normal boot the OCIRE recovery disk was fully writable from
L1: `mkfs.fat32 hdb confirm` (the OCIRE attack report) destroyed 3574
sectors in 20 seconds (OCIREMB2 LBA 2048, OCIRELOG1 LBA 3072, the MBR
- only OCIREAB1 LBA 1024 survived).

Fix (this workpack, unconditional + bottom-layer, exactly as the spec
demands - no boot flag, no L1 filter):
  1. `ocembed_early_protect()` (svc_ocembed_core.c) - called from
     kmain (core_boot_main.c, right BEFORE
     `driver_block_part_scan_register_all()`, i.e. before the first
     partition child device exists): scans EVERY present block device
     for the OCIRE anchors (OCIREAB1 magic at LBA 1024, OCIREMB2
     magic at LBA 2048) and arms every hit read-only via
     `driver_block_set_protected()`.  Runs on EVERY boot.
  2. `driver_block_write_sectors_raw()` /
     `driver_block_write_sectors()` (svc_block_drivers.c) - both
     write entries now refuse protected devices with -OC_EPERM
     (include/oc_errno.h) BEFORE touching the device / before
     creating dirty cache lines.  Reads stay open.
  3. `driver_block_write_sectors_trusted()` stays un-guarded BY
     DESIGN: it is the kernel-internal channel (the OCIRE mailbox
     signal + boot-log ring appends) and is not reachable from ring 3
     (no L1SVC op maps to it; verified in services/block/src/
     svc_block_l1.c dispatch table).
  4. Defence in depth: `driver_block_part_scan_register_all()`
     (svc_block_part.c) re-arms on the WP-07 anchor hit, so the
     isolation (no child devices) and the write-closure can never
     drift apart.
  5. Command layer (svc_block_cmds.c): mkfs.fat32/exfat/ext4 gain an
     explicit EPERM report (`blk_dev_refuse_protected()`) - cosmetic
     only; removing it changes nothing (the driver layer still
     refuses).  disk_rw_test reports the refusal as an expected gate
     failure instead of a mid-test write error.

Verified write surfaces all funnel through the two guarded entries:
  - mkfs.* (6 raw-write sites, svc_block_cmds.c)
  - fs-layer writes (cached path, svc_fs_*.c)
  - disk_rw_test (raw + cached, svc_block_cmds_test.c)
  - no block ioctl path exists (grep: zero ioctl implementations)
  - the ONLY bypass is `driver_block_write_sectors_trusted()`
    (kernel-internal, see 3)

## 2. S1 - L1 out-of-bounds reads of kernel pointers - PASS

The L1SVC/syscall layer validates EVERY user pointer with the
`access_ok` fence (services/sysapi/src/svc_sysapi_syscall.c):

  - `valid_user_buf()` (line 60): rejects addr < USER_CODE_BASE,
    addr >= USER_STACK_TOP, len > 65536, and addr+len overflow -
    a kernel-space pointer (high canonical half) never passes.
  - `access_ok_read()` (line 86): walks every 4 KiB page in the
    range and demands `mem_vmm_is_mapped_user()` - PRESENT is not
    enough, the leaf PTE must carry the USER flag (BUG-AUD-2a-001
    fix: a copied kernel huge-PDE split used to expose kernel-heap
    leaves inside the user window; that class is fenced).
  - `access_ok_str()` (line 113): same page-walk with the USER flag
    fence, bounded to 16 pages (anti-DoS) for NUL-terminated strings.
  - `copy_from_user/copy_to_user` (line 137/144) only run after the
    fence passes.

Residual (documented, accepted): the comment at line 83 notes a
munmap racing between access_ok and the memcpy is not byte-wise
fault-caught - single-CPU, non-preemptive kernel today; revisit if
preemption lands.

## 3. S2 - L1 direct CR3/CR4 manipulation - PASS

  - Ring-3 execution of `mov cr3` / `mov cr4` faults with #GP
    (hardware privilege check) - there is no L1 instruction path.
  - The CR4 register is only written by the kernel ACPI S3
    sleep/resume path (arch/x86_64/arch_acpi_s3*.S) - no L1SVC op
    reaches it (diag dispatch table audited: no cr4 op exists).
  - `diag_cr3test` (L1SVC cat "diag" op 9, services/diag/src/
    svc_diag_l1.c:680) DOES switch CR3 - but on the kernel side:
    `diag_kernel_as_enter()` saves the current AS, the test builds a
    throw-away AS over a freshly PMM-allocated zero frame, verifies
    the mapping inside it, and `diag_kernel_as_leave()` restores the
    saved CR3 before returning.  The L1 caller cannot choose the CR3
    value, cannot keep the switched context, and no kernel-memory
    page is ever mapped into the throw-away AS - there is no read or
    write path into kernel memory, and no persistent CR3 hijack.
    INFO (open by design, bounded): concurrent cr3test from two
    processes would interleave the non-atomic switch_as sequence
    (the kernel is single-CPU non-preemptive at this layer, so the
    window requires a future preemptive kernel); recommendation:
    gate diag-class self-tests behind CONFIG_DIAG in a later workpack.

## 4. S3 - L1SVC interface permission bypass - PASS (one FIXED)

  - The block write category (mkfs.*, disk_rw, abdisk writes):
    FIXED by item 1 - the refusal is at the block DRIVER layer, not
    at the L1SVC filter (spec 9: "L1-layer filtering to fudge the
    hole" is explicitly not what the command-layer guards are).
  - `ocembed_l1_dispatch()` (svc_ocembed_core.c:445): every op
    re-checks `caller_is_guard()` (`p->ocire_guard` on the current
    process) and refuses with "guard-only channel" - the agent's two
    ops (LOG_APPEND, KLOG_SNAPSHOT) are the only doors and they are
    process-identity-fenced.
  - The diag category (exc, pftest, crashtest, cr3test, ...): kernel
    side self-tests, no cross-process data path, no pointer
    parameters from the caller (arg string unused by these ops).
  - The process category (kill, etc.) already refuses killing the
    protected ocire_guard process (WP-03 contract).

## 5. Checklist (the spec's deliverable)

| # | Surface | Verdict | Evidence |
|---|---------|---------|----------|
| 1 | OCIRE disk write-protection, unconditional | FIXED | ocembed_early_protect() + -OC_EPERM in both write entries |
| 2 | mkfs.* from L1 against the recovery disk | FIXED | refuses with EPERM, sectors unchanged (acceptance test) |
| 3 | L1 kernel-pointer out-of-bounds read | PASS | access_ok range+mapping+USER-flag fence |
| 4 | L1 direct CR3/CR4 | PASS | ring3 #GP; CR4 only in kernel S3 path; cr3test bounded |
| 5 | L1SVC ocire channel | PASS | guard-only, process-identity fenced |
| 6 | L1SVC block write ops | FIXED | driver-layer refusal (this WP) |
| 7 | block ioctl bypass | PASS | no ioctl implementation exists |
| 8 | trusted-channel abuse from ring 3 | PASS | not mapped in any L1SVC dispatch table |

-- end of audit --
