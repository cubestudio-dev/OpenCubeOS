<!-- SPDX-License-Identifier: Apache-2.0 -->
# Kernel data structures (verbatim collection)

The load-bearing kernel structures a developer must know before
touching the corresponding code, copied verbatim from source, each
with its ownership contract: **who reads/writes it, when, and what
breaks if you get it wrong**. File paths are exact. ABI-critical
layouts are pinned by `_Static_assert` or hardcoded assembly
offsets - noted per structure.

## 1. The task control block - `core_sched.h`

```c
typedef int tid_t;
typedef struct task {
    tid_t  tid;
    char   name[32];
    int    state;
    int    priority;
    u64    cpu_time_ticks;   /* total CPU time consumed */
    u64    switch_count;     /* how many times this task was switched to */
    u64    ticks_remaining;  /* time slice remaining */

    /* Saved register context for context switch.
     * The order MUST match context_switch.S. */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;

    /* Stack for kernel threads. */
    u64 *stack_base;     /* allocated stack base */
    u64  stack_size;

    /* Address space (CR3) for this task. 0 = kernel identity mapping. */
    u64 cr3;

    /* RSP0 for TSS (kernel stack top for ring-3 IRQ entry). */
    u64 rsp0;

    /* Function entry point and argument for new tasks. */
    void (*entry)(void *arg);
    void *arg;

    int in_use;

    u64 fpu_state[64] __attribute__((aligned(16)));
    int fpu_saved;
} task_t;
```

(`kernel/core_sched.h`; 32 slots, `MAX_TASKS 32`.)

- **Who/when**: `core_sched.c` writes the register block inside
  `arch_context_switch` (assembly saves at **hardcoded offsets**:
  `OFF_FPU=288`, `OFF_FPU_SAVED=800`, `OFF_RFLAGS=208`, RIP=192,
  RSP=216, CR3=248 - `_Static_assert`s in core_sched.c pin them
  against `arch_context_switch.S`). The scheduler reads
  `state/priority/ticks_remaining` from IRQ0 every 10 ms;
  `cr3`/`rsp0` are reloaded on every switch (CR3 reload + TSS RSP0
  update before the asm switch).
- **FPU region**: the 64-u64 `fxsave` image (512 bytes, 16-byte
  aligned). Every switch does `fxsave`/`fxrstor`; the first
  switch-in does `fninit` + `ldmxcsr 0x1F80` (CR4.OSFXSR +
  OSXMMEXCPT enabled in `core_sched_init`).
- **Impact if wrong**: reordering fields desynchronizes C and
  assembly (the static asserts catch it); a saved `rsp` outside
  `[stack_base, +size)` panics at switch time by design
  ("kernel stack bounds violated on switch").

## 2. The exception/interrupt frame - `arch_idt.h`

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

(`kernel/arch/x86_64/arch_idt.h`; 22 u64s - matches
`OC_DUMP_REGS_U64 22` in the core-dump header, so the crash
dumper's register copy is the same struct.)

- **Who/when**: `arch_idt_stub.S` pushes the 15 GPRs (vectors with
  CPU error codes skip the fake 0 push), `cld` (BUG-0138), dynamic
  16-byte alignment around the `call arch_isr_dispatch`
  (BUG-0137). Exception/IRQ/syscall dispatch read it; the fork
  child path (`arch_enter_ring3_fork.S`) restores all 22 registers
  with `rax=0`; `SIGRETURN` restores the saved copy; the panic
  dumper prints it 4x4.
- **Impact**: this struct IS the kernel's register ABI; the
  sigreturn trampoline bytes `B8 2A 00 00 00 CD 80 F4`
  (`mov eax,42; int 0x80; hlt`) rely on its stack layout.

## 3. klog ring - `core_lib_klog.c`

```c
#define KLOG_RING_EARLY 24                 /* static BSS bootstrap ring (4 KiB) */
#define KLOG_RING_MAX   1024               /* after klog_grow() (heap)   */
#define KLOG_MOD_MAX  12
#define KLOG_MSG_MAX  144

typedef struct {
    u64  ts_ms;
    u8   level;
    char module[KLOG_MOD_MAX];
    char msg[KLOG_MSG_MAX];
} klog_entry_t;

static klog_entry_t g_ring_early[KLOG_RING_EARLY];
static klog_entry_t *g_ring = g_ring_early;
static u32 g_ring_cap = KLOG_RING_EARLY;
static volatile u32 g_ring_head = 0;   /* next write slot               */
static volatile u32 g_ring_count = 0;  /* valid entries (<= cap)        */
static volatile u64 g_total = 0;       /* absolute entries ever written */
static volatile u64 g_dropped = 0;     /* slots overwritten since boot  */
```

- **Who/when**: any kernel code calls `klog_write(level, module, fmt, ...)` (IRQ-safe: slot claim is a short cli/sti critical
  section); the 24-entry BSS bootstrap ring carries the earliest
  boot lines, then `klog_grow()` (kmain, right after
  `mem_heap_init`) kmallocs the 1024-entry ring and copies the
  boot entries over - nothing is lost. Replay ops (`dmesg`,
  `logcat`, `klog_snapshot_text` for the OCIRE agent) read
  oldest-to-newest via `klog_slot_of(seq)`:
  `off = g_total - seq; slot = (g_ring_head + g_ring_cap - off) % g_ring_cap`.
- **Impact**: the big ring lives on the heap because the kernel
  image is pinned below 0x400000 (linker ASSERT). Full details in
  [../internals/klog.md](../internals/klog.md).

## 4. Crash structures - `services/crash`

Core-dump header (`svc_crash_core.h`):

```c
#define OC_DUMP_STACK_BYTES   4096    /* kernel-stack window copy     */
#define OC_DUMP_SERIAL_MAX    2048    /* hex-print cap when diskless  */
#define OC_DUMP_REGS_U64      22      /* arch_irq_frame_t, in u64s    */

typedef struct {
    char magic[8];            /* "OCDUMP01"                          */
    u32 hdr_len;              /* sizeof(oc_dump_hdr_t)               */
    u32 image_len;            /* total bytes captured                */
    u64 build;                /* OC_KERNEL_BUILD                     */
    char version[24];         /* OC_RELEASE_VERSION                  */
    char reason[40];          /* short panic reason                  */
    u64 jiffies;
    u64 rtc_epoch;
    u64 cr3;                  /* page-table root at capture time     */
    u64 pte_count;            /* present leaf PTEs walked            */
    u32 tid, pid;
    u32 ring;
    char task[16];
    u64 stack_base, stack_size, stack_rsp;   /* kernel-stack window  */
    u32 stack_copy_len;       /* bytes actually copied               */
    u32 regs_present;         /* 1 = frame captured, 0 = live regs   */
} oc_dump_hdr_t;
```

Crash-log ring entry (`svc_crash_core.h`):

```c
#define OC_CRASHLOG_CAP        32
#define OC_CRASH_SRC_PANIC     32
#define OC_CRASH_SRC_WDT       33
#define OC_CRASH_SRC_ASSERT    34
#define OC_CRASH_SRC_CONTAINED 35
#define OC_CRASH_SRC_STACKOVF  36

typedef struct {
    u64 jiffies;         /* core_timer_ticks() at record time        */
    u64 rtc_epoch;       /* seconds since 1970 (0 = rtc unavailable) */
    u32 vector;          /* 0..31 exception, 32..36 synthetic source */
    u32 ring;            /* 0 = kernel, 3 = user                     */
    u64 error_code;
    u64 rip;
    u64 rsp;
    u64 cr2;             /* #PF faulting address, 0 otherwise        */
    i32 tid;
    i32 pid;
    char task[16];       /* task name (or "user:<pid>")              */
} crashlog_entry_t;
```

- **Who/when**: the panic path (armed at kmain step 16a:
  `core_dump_alloc()` reserves the dump buffer up-front with
  kmalloc so the panic path never allocates, then
  `crash_install_panic_handlers()`, `crashlog_boot_alloc()`,
  `crash_wdt_init()`) writes both; `crashlog_flush()` appends the
  32-slot ring to `/var/log/crash.log`; the diag ops
  `crashlog`/`dumpinfo` read them back after reboot.
- **Impact**: the dump image layout in capture order is header,
  regs (22 u64 or live snapshot), task block, stack window (up to
  4 KiB clipped to `[stack_base, +size)`), tables (CR3 + PML4
  self-scan PTE count); persisted to `/var/crash/core.dump` or, if
  no filesystem exists, the first 2048 bytes are hex-dumped to
  serial. Full story: [../internals/crash-dump.md](../internals/crash-dump.md).

## 5. Heap block header and free list - `core_mm_heap.c`

```c
#define HEAP_MAGIC      0xDEADBEEFCAFEBABEULL   /* allocated block */
#define HEAP_MAGIC_FREE 0xFEEDFACE12345678ULL   /* free block      */
#define HEAP_MIN_POOL_PAGES 16   /* 64 KiB initial pool  */
#define HEAP_MAX_POOL_PAGES 4096 /* 16 MiB ceiling       */
#define HEAP_ALIGN 16
typedef struct mem_heap_block {
    u64 magic;           /* HEAP_MAGIC if allocated, HEAP_MAGIC_FREE if free */
    u64 size;            /* payload size in bytes (excludes header) */
    struct mem_heap_block *next;  /* next block in the free list (free blocks only) */
    struct mem_heap_block *prev;  /* prev block in the free list */
} mem_heap_block_t;      /* EXACTLY 32 bytes */
```

- **Who/when**: `kmalloc/kzalloc/kfree/krealloc` (first-fit over
  the free list, split + coalesce, up to 16 pools from
  `mem_pmm_alloc_contig`, identity-mapped); the allocator hook
  (`mem_heap_register_allocator_hook`) observes every alloc/free
  for leak tracking; timer-IRQ kmalloc is made atomic against
  task-context alloc by PMM's irq-save windows (BUG-0043).
- **Impact**: the header is EXACTLY 32 bytes and 16-byte aligned -
  the odd-parity 40-byte header bug once shifted ATA-DMA buffers
  by 8 bytes; double-free is detected via the magic; a single
  allocation is capped at 16 MiB.

## 6. PMM bitmap - `core_mm_pmm.c`

```c
#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12
#define PMM_MAX_PAGES (1024 * 1024)  /* 4 GiB / 4 KiB */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)  /* 128 KiB */
static u8  g_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
```

- **Who/when**: every physical-frame consumer (VMM page tables,
  heap pools, kthread stacks, DMA targets) allocates through
  `mem_pmm_alloc` (first-clear-bit scan from rotating hint
  `g_last_scan`) or `mem_pmm_alloc_contig(count)`.
- **Impact**: the physical window **0x400000-0x600000 (512 frames)
  is reserved forever** because user address spaces remap that
  *virtual* window to an empty PT (P3-1 security fix) - PMM must
  never hand those frames out; also reserved: first 1 MiB, kernel
  image, framebuffer, MB2 info/modules, the bitmap itself, and the
  sliver [kernel end, 0x400000).

## 7. VMM PTE flags and the PT_OWNER tag - `core_mm_vmm.c`

```c
#define VMM_FLAG_PRESENT  0x001
#define VMM_FLAG_WRITE    0x002
#define VMM_FLAG_USER     0x004
#define VMM_FLAG_EXEC     0x000  /* exec is default (NX=0) */
#define VMM_FLAG_NOEXEC   0x8000000000000000ULL  /* NX bit (bit 63) */
#define VMM_FLAG_COW      0x200  /* software flag (reserved bit) */
#define VMM_FLAG_DIRTY    0x040
#define VMM_FLAG_ACCESSED 0x020
#define VMM_FLAG_PT_OWNER 0x400  /* software ownership tag on PT frames */
#define VMM_FLAGS_KERNEL (VMM_FLAG_PRESENT | VMM_FLAG_WRITE)
#define VMM_FLAGS_USER   (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER)
```

- **Who/when**: `walk_pt(as, vaddr, create)` sets
  `VMM_FLAG_PT_OWNER` on page-table frames it creates in non-kernel
  address spaces; address-space destruction frees only frames it
  can prove it owns (proof-of-ownership, in non-kernel ASes);
  `mem_vmm_is_mapped_user()` is the USER(+WRITE) fence the syscall
  `access_ok` uses (a supervisor page can never pass a user-buffer
  check - BUG-AUD-2a-001).
- **Impact**: user AS layout is fixed: code 0x400000, brk
  0x500000, ld.so API table 0x08000000, ld.so 0x10000000, main PIE
  0x20000000 (+ASLR), libfoo 0x30000000 (+ASLR), dlopen bump
  0x38000000 (+ASLR), mmap base 0x3C000000, stack top 0x40000000
  (64 KiB, 8 MiB growth floor).

## 8. The boot-state contract - `include/ocbl_state.h`

Shared byte-for-byte by OCBL (bootloader), OCOS (monitored
payload) and OCIRE (recovery kernel); freestanding `ocst_*` types:

```c
#define OCST_STATE_ADDR        0x98000u
#define OCST_STATE_SIZE        0x1000u
#define OCST_RESERVED_ADDR     0x9B000u
#define OCST_RESERVED_SIZE     0x1000u
#define OCST_STATE_MAGIC       0x3153434Fu   /* "OCS1" */
#define OCST_RESERVED_MAGIC    0x3142434Fu   /* "OCB1" */

#define OCST_STAGE_RESET            0u
#define OCST_STAGE_KM_AIN_ENTERED   1u
#define OCST_STAGE_SCHED_READY      2u
#define OCST_STAGE_DRIVERS_READY    3u
#define OCST_STAGE_SERVICES_READY   4u
#define OCST_STAGE_L1_LAUNCHED      5u
#define OCST_STAGE_L1_READY         6u
#define OCST_STAGE_COUNT            7u
#define OCST_STAGE_RUNTIME          255u

typedef struct ocst_state {
    ocst_u32 magic;  ocst_u16 ver;  ocst_u16 size;
    ocst_u16 seq;    ocst_u8 stage; ocst_u8 armed;
    ocst_u8 kflags;  ocst_u8 rsvd0;
    ocst_u32 heartbeat;  ocst_u32 last_progress;
} ocst_state_t;               /* 24 bytes */

typedef struct ocst_record {
    ocst_u32 magic;  ocst_u16 ver;  ocst_u16 size;
    ocst_u8 kind;    ocst_u8 stall; ocst_u16 seq;
    ocst_u32 ts_unix; ocst_u32 silence_sec;
    ocst_u8 routed;  ocst_u8 rsvd0; ocst_u16 rsvd1;
    ocst_u32 checksum;   /* ones-complement of the 24-byte payload */
} ocst_record_t;          /* 28 bytes */
```

- **Who/when**: the kernel side (`core_boot_state.c`) is
  **blind-write only** - it never reads the area back; stage
  writes happen at each kmain milestone, heartbeats ride the 1 s
  tick while the 6300ESB watchdog is taken over via raw PCI/IO
  cycles. OCBL reads/writes (judgment), OCIRE reads (reports).
  Layout guarded by `_Static_assert` (24/28 bytes).
- **Impact**: the two low-memory islands (0x98000/0x9B000) survive
  QEMU warm resets (E820-reserved); L1 can never reach them
  (supervisor-only half of every user AS - verified by a CR3 walk
  at L1_READY).

## 9. Driver ops tables (the four families)

All drivers of a family plug into one ops shape; the family
manager (in the matching service folder) walks the collected
`.oc_drvs` declarations and probes.

### 9.1 block - `drivers/storage/drv_storage_api.h`

```c
typedef struct driver_block_ops {
    /* Read `count` sectors starting at `lba` into `buf`. Returns 0 on
     * success, negative on error. `buf` must be count*512 bytes. */
    int (*read)(driver_block_device_t *dev, u64 lba, u32 count, void *buf);
    /* Write `count` sectors from `buf` starting at `lba`. */
    int (*write)(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
    /* WP-10a: flush the drive's internal write cache to media (ATA
     * FLUSH CACHE, NVMe Flush, virtio-blk FLUSH, ...).  Optional —
     * drivers without a flush command leave it NULL; driver_block_flush() then
     * only flushes the OS-level write-back cache. */
    int (*flush)(driver_block_device_t *dev);
} driver_block_ops_t;

struct driver_block_device {
    char     name[BLK_DEV_NAME_LEN];   /* e.g. "sda", "hda", "vda", "nvme0" */
    driver_block_type_t type;
    u64      sectors;                  /* total sectors (capacity) */
    u32      sector_size;              /* almost always 512 */
    u8       present;                  /* 1 = usable */
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const driver_block_ops_t *ops;              /* driver read/write/flush */
    void    *priv;                     /* driver private data */
    int      index;                    /* WP-10a: registry slot filled in
                                         by driver_block_register() */
};
```

Read/writes go through the 64-slot LRU write-back cache
(`driver_block_cache_slot_t` in
`services/block/interface/svc_block_cache.h`):

```c
#define BLK_CACHE_SLOTS  64   /* 64 * 512 = 32 KiB cache */

typedef struct {
    int  dev_idx;        /* -1 = empty slot */
    u64  lba;
    u8   data[512];
    u8   dirty;          /* 1 = needs write-back */
    u64  last_used;      /* tick count for LRU */
} driver_block_cache_slot_t;
```

- **Who/when**: fs drivers + the partition parser call
  `driver_block_read/write` (cached); the ATA driver swaps a PIO
  drive's ops to DMA via `blk_dev_set_ops`; the OCIRE embed refuses
  writes on protected devices (the `_trusted` variants are the
  kernel-internal channel).
- **Impact**: victim selection tries LRU order and only releases a
  dirty slot when its write-back SUCCEEDS (BUG-0116) - if every
  dirty slot fails, callers fall back to write-through instead of
  losing data.

### 9.2 nic - `drivers/net/drv_net_api.h`

```c
typedef struct driver_nic_ops {
    int (*send)(driver_nic_device_t *dev, const void *buf, int len);
    int (*recv)(driver_nic_device_t *dev, void *buf, int maxlen);
    int (*link_status)(driver_nic_device_t *dev);
    int (*get_mac)(driver_nic_device_t *dev, u8 mac[6]);
} driver_nic_ops_t;

struct driver_nic_device {
    char     name[NIC_NAME_LEN];
    driver_nic_type_t type;
    u8       mac[6];
    u8       present;
    u8       bus, dev, func;
    u16      vendor_id, device_id;
    const driver_nic_ops_t *ops;
    void    *priv;
    int      index;
```

- **Who/when**: the net stack sends/receives frames through
  `driver_nic_send/recv` (the legacy built-in e1000/virtio-net
  paths in `svc_net_core.c` remain as fallback); `send`/`recv` are
  mandatory ops at registration.
- **Impact**: `driver_nic_active()` returns the device net.c routes
  frames to (first registered NIC).

### 9.3 snd - `drivers/audio/drv_audio_api.h`

```c
typedef struct driver_snd_ops {
    int (*play)(driver_snd_device_t *dev, const void *buf, int len);
    int (*stop)(driver_snd_device_t *dev);
    int (*set_rate)(driver_snd_device_t *dev, u32 rate);
    int (*set_volume)(driver_snd_device_t *dev, u32 vol);
    int (*get_caps)(driver_snd_device_t *dev, driver_snd_caps_t *caps);
} driver_snd_ops_t;
```

- **Who/when**: `driver_snd_play` is blocking-cooperative (polls
  DMA position, yields); caps carry the supported-rates bitmap the
  `sample_rate_test` negotiates against.
- **Impact**: PCM contract is little-endian signed 16-bit
  interleaved stereo; `play` takes BYTES, not samples; volume is
  per-device (mixer paths apply `dev->volume`).

### 9.4 usb host controller - `drivers/usb/drv_usb_core.h`

```c
typedef struct driver_usb_hc_ops {
    const char *name;
    /* max bytes in ONE bulk transfer call (0 = 512 default).  MSC
     * chunks its BOT data phase with this so every backend gets a
     * working split: UHCI/OHCI 512, EHCI 4096, XHCI 16 KiB. */
    u16  bulk_max;
    /* blocking control transfer (SETUP+DATA+STATUS) */
    int  (*control)(driver_usb_host_t *h, driver_usb_dev_t *d,
                    const driver_usb_setup_t *setup, void *buf, u16 len,
                    u32 timeout_ms);
    /* blocking bulk transfer; returns bytes moved or negative error */
    int  (*bulk)(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                 void *buf, u16 len, u32 timeout_ms);
    /* one interrupt transaction; returns bytes moved, -OC_USB_ENAK
     * when the endpoint NAKs the whole window */
    int  (*interrupt)(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                      void *buf, u16 len, u32 timeout_ms);
    /* one isochronous packet (audio: one per 1 ms frame) */
    int  (*driver_usb_iso_out)(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                    const void *buf, u16 len);
    int  (*driver_usb_iso_in)(driver_usb_host_t *h, driver_usb_dev_t *d, u8 ep_addr,
                   void *buf, u16 len);
    /* root hub */
} driver_usb_hc_ops_t;
```

- **Who/when**: UHCI/OHCI/EHCI/XHCI backends register through
  `driver_usb_register_host(host, ops)`; the core enumerates
  devices, class drivers (HID/MSC/CDC-ACM/UAC) probe on top.
- **Impact**: `USB_HOSTF_NATIVE_ADDR` (XHCI) means the core must
  not send SET_ADDRESS - the backend maps it to its native
  AddressDevice command.

## 10. The self-registration descriptors

### 10.1 OC_SERVICE_DECL - `services/svc_framework.h`

```c
typedef void (*oc_service_init_fn_t)(void);
typedef int  (*oc_service_late_fn_t)(void);   /* 0 = probed something */
typedef int  (*oc_service_l1_fn_t)(u32 op, const char *arg, char *out, int cap);

typedef struct {
    const char         *category;   /* == services/<category> folder */
    const char         *desc;
    oc_service_init_fn_t init;      /* early bring-up, may be NULL */
    oc_service_late_fn_t late_init; /* probe pass, may be NULL */
    oc_service_l1_fn_t  l1_dispatch; /* SYS_L1SVC entry, may be NULL */
} oc_service_decl_t;

#define OC_SERVICE_DECL(cat, dsc, ini, late) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (oc_service_l1_fn_t)0 }

#define OC_SERVICE_DECL_L1(cat, dsc, ini, late, disp) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (disp) }
```

- **Who/when**: each service file declares exactly one descriptor
  into `.oc_svcs`; `svc_framework_collect()` (kmain step 12c-2)
  walks `__oc_svcs_start.._end` (boundaries from linker.ld) into
  `s_svcs[64]`; `svc_framework_dispatch` scans by exact
  category-string match on every L1SVC call.
- **Impact**: adding a service = adding one file (NAK rule). See
  [writing-services.md](writing-services.md).

### 10.2 OC_DRIVER_DECL - `drivers/drv_framework.h`

```c
typedef struct {
    const char *family;
    const void *ops;        /* const drv_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }
```

- **Who/when**: a driver file declares identity into `.oc_drvs`;
  `drv_framework_collect()` fills the runtime table; family
  managers (block/nic/snd/usb/wireless/sdio) walk
  `drv_framework_at(i)` during their probe pass matching the
  `family` string.
- **Impact**: adding a driver = one .c file in
  `ocos/drivers/<class>/` + one OC_DRIVER_DECL line - no global
  table edit, no registration call site, no core file change.

## 11. Pipes - `services/sysapi/src/svc_sysapi_syscall.c`

```c
#define PIPE_BUF_SIZE 4096
#define MAX_PIPES 32
#define PIPE_WAIT_QUEUE_MAX 8
typedef struct {
    u8 buf[PIPE_BUF_SIZE];
    u32 read_pos;
    u32 write_pos;
    int in_use;
    int reader_count;
    int writer_count;
    int reader_waiters[PIPE_WAIT_QUEUE_MAX];
    int writer_waiters[PIPE_WAIT_QUEUE_MAX];
} sys_pipe_t;
```

- **Who/when**: `SYS_PIPE` creates; fds in `user_proc_t.fds[]`
  carry the kind (0=free, 1=VFS fd, 2=pipe-read, 3=pipe-write);
  per-end wait queues of tids make multi-reader safe (P3-15);
  `pipe_close_fd(pipe_id, kind)` is exported for the unified
  reaper.
- **Impact**: the table is kmalloc'd lazily (moved off a 133 KiB
  static array to respect the 0x400000 image bound); 32 pipes max.

## 12. Synchronization primitives - `core_sync.h`

```c
typedef struct {
    volatile int locked;
    u64          lock_count;  /* total acquisitions */
    u64          _saved_flags; /* irqsave: caller's RFLAGS at acquire */
} spinlock_t;

#define SEM_MAX_WAITERS 16
typedef struct {
    volatile int count;
    spinlock_t   lock;
    u64          wait_count;
    u64          post_count;
    tid_t        waiters[SEM_MAX_WAITERS];
    int          num_waiters;
} sem_t;

#define MUTEX_MAX_WAITERS 16
typedef struct {
    volatile int locked;
    tid_t        owner;       /* tid of the task holding the lock */
    tid_t        waiters[MUTEX_MAX_WAITERS];  /* FIFO queue of blocked tids */
    int          num_waiters;
    u64          lock_count;
} mutex_t;

#define COND_MAX_WAITERS 16
typedef struct {
    spinlock_t   lock;
    int          waiters;
    u64          signal_count;
    tid_t        waiter_tids[COND_MAX_WAITERS];
} cond_t;
```

- **Who/when**: spinlock = save RFLAGS + cli acquire, release
  FIRST then restore IF (BUG-0285 irqsave semantics); holders must
  not sleep/yield. Semaphore waiter registration + BLOCKED happen
  inside one irqsave window (P0-5 lost-wakeup fix). Mutex is FIFO,
  **no priority inheritance** (documented deliberate unfairness;
  dead-owner force-release by the next contended locker).
- **Impact**: `sync_get_stats()` feeds the `syncstat` diag op.

## 13. OCIRE embed mailbox - `services/ocembed`

```c
#define OCIRE_MB_MAGIC        "OCIREMB2"
#define OCIRE_MB_BYTES        128
#define OCIRE_MB_STATE_WAIT_SIGNAL 1
#define OCIRE_MB_STATE_SIGNAL_OK   2

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

- **Who/when**: OCIRE (the recovery supervisor) owns the mailbox on
  its volume; the OCOS embed service writes SIGNAL_OK exactly once
  (only from state WAIT_SIGNAL) after locating the covering block
  device and enabling whole-disk write protection FIRST.
- **Impact**: `oc.log=<LBA>`/`oc.logsecs`/`oc.mb`/`oc.wdt`/`oc.agent=1`
  cmdline facts activate the whole channel; on a normal GRUB boot
  the service is dormant. Full page:
  [../internals/ocembed-guard.md](../internals/ocembed-guard.md).
