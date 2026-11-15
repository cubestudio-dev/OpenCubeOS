<!-- SPDX-License-Identifier: Apache-2.0 -->
# Memory: PMM, VMM, heap, and the identity window

Three allocators cooperate: the PMM owns physical frames, the VMM owns
page tables, the heap owns kernel objects. All live under the flat
`ocos/kernel/core_mm_*` prefix. The physical memory map that binds the
four boot components is [system-map.md](system-map.md); the identity
window conspiracy is [hidden-mechanisms.md](hidden-mechanisms.md).

## PMM - the bitmap allocator (`core_mm_pmm.c`)

```c
#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12
#define PMM_MAX_PAGES (1024 * 1024)  /* 4 GiB / 4 KiB */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)  /* 128 KiB */
static u8  g_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
```

One bit per 4 KiB page, covering the identity-mapped 4 GiB. Allocation
is a first-clear-bit scan starting at the rotating hint `g_last_scan`
(the hint makes sequential allocations O(1) after boot instead of
rescanning from 0); `mem_pmm_alloc_contig(count)` is a first-fit run
scan for DMA and pool requests. Every scan-and-mark runs inside a
`pmm_irq_save/restore` window (pushfq + cli, restore IF only if it was
set) - the BUG-0043 fix: a timer-IRQ `kmalloc` can race a task-context
alloc on this single-CPU kernel, so PMM operations must be IRQ-atomic.

**Reserved at init**: the first 1 MiB; the kernel image
[1 MiB, `__end_of_kernel`); the framebuffer; the mb2 info + modules;
the bitmap itself; **the physical window 0x400000-0x600000 (512 frames
= 2 MiB), permanently** - user address spaces remap that *virtual*
range to an empty PT, so the kernel can never dereference PMM frames
handed out in that *physical* range while on a user CR3 (they would
alias user code); plus the sliver [kernel end, 0x400000).

Emergency behavior: up to 4 OOM callbacks fire before final failure;
stats track total_allocs/total_frees/alloc_failures and
free_fragments (the contiguous-run count - the `frag` command prints
fragmentation, `mem` the rest).

## VMM - the 4-level page-table manager (`core_mm_vmm.c`)

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

Boot map: the first 4 GiB via PML4[0] -> PDPT[0..3] -> 4 PD tables,
each 512 entries of 2 MiB huge pages (flags `0x83` = present|writable|
huge|global; NX clear - kernel pages executable).

- `walk_pt(as, vaddr, create)` - the 4-level walk; **splits a 2 MiB
  huge PDE into 512 leaf PTEs on demand** when `create` needs
  granularity below a huge page (idempotent). Private PTs created in
  non-kernel address spaces are tagged `VMM_FLAG_PT_OWNER` so
  `destroy` frees page-table frames **by proof-of-ownership** - a
  kernel-shared table can never be freed through a dying user AS.
- `mem_vmm_split_huge_page()` - the public force-4K API. Necessary
  because **a huge-PDE unmap is otherwise a silent no-op** - the
  kthread guard-page unmap splits first and verifies with a fail-loud
  klog.
- `mem_vmm_is_mapped_user()` - the USER(+WRITE) flag fence used by the
  syscall `access_ok` (BUG-AUD-2a-001: supervisor pages can never pass
  a user-buffer check).
- `mem_vmm_register_fault_handler(fn)` - up to 4 L1 fault-handler
  chain slots, run first by the page-fault path.

### User address spaces (`create_user_address_space`, `core_usermode.c`)

Fresh PML4; kernel PML4[1..3] copied; a PRIVATE PDPT[0] + PD0 that is
a copy of the kernel's 512 x 2 MiB huge PDEs; PRIVATE copies of the
kernel PDPT[1..3] PD pages (BUG-AUD-2a-001: a kernel-side huge-page
split no longer mutates user ASes); and **PD0[2] (0x400000-0x600000)
replaced by an empty PT** - the P3-1 security fix, never
pre-populated with kernel phys pages. This is the identity-window trap:
ring-3 links at 0x400000, so the window must exist virtually but must
never alias the kernel's old physical backing.

User VA map: code 0x400000, brk 0x500000, ld.so API table 0x08000000,
ld.so 0x10000000 (fixed), main PIE 0x20000000 (+ASLR), libfoo
0x30000000 (+ASLR), dlopen bump 0x38000000 (+ASLR), mmap base
0x3C000000 (window ends 0x3FFF0000, 16-region ownership table), stack
top 0x40000000 (64 KiB). ASLR: `user_aslr_page_slice(region_span)` -
two rdtsc samples through a splitmix64 finalizer, page-aligned, 1 MiB
kept clear below the region top.

### The page-fault handler

`mem_vmm_handle_page_fault(vaddr, error_code, rip, faulting_rsp)`:

1. The L1 handler chain runs first (up to 4 registered handlers).
2. CPU error-code bits are respected (P/W/U/S, I/D - the BUG-0044 fix
   chain); **protection violations (P=1) never remap**.
3. A kernel-mode not-present fault on a user address is reported as a
   bad kernel deref of a user pointer - not papered over.
4. User stack growth: only within 64 KiB below the faulting RSP and
   above the 8 MiB floor (`VMM_USER_STACK_TOP - VMM_STACK_LIMIT_BYTES`).
5. Unhandled user faults fall through to the ring-3 kill path
   (SIGSEGV delivery first - [interrupts.md](interrupts.md)).

## Heap - the free-list allocator (`core_mm_heap.c`)

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

The 32-byte header is pinned by static assert; the two magic constants
make double-free and use-after-free detectable (kfree checks
`HEAP_MAGIC`, reports and refuses on mismatch). First-fit over the
free list with split + coalesce; up to **16 pools** tracked with
base+pages so coalescing works across pool boundaries; pools come from
`mem_pmm_alloc_contig` and are identity-mapped, so no VMM mapping is
needed. Single-allocation ceiling: **16 MiB** (`HEAP_MAX_POOL_PAGES`).
`mem_heap_register_allocator_hook` exists for leak tracking.

Alignment discipline is documented at length in the source: an
odd-parity 40-byte header experiment once produced 8-byte shifts that
broke ATA DMA - the 32-byte/16-byte discipline is load-bearing, not
style.

## Why the heap, not .bss - the 0x400000 growth discipline

The linker enforces it:

```
ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the
0x400000 identity-window boundary - move large buffers to kmalloc")
```

Everything that grew past the era of small kernels moved to boot-time
kmalloc *for exactly this reason*, and each move is a documented bug
fix: the TCP connection table (32 x ~16 KiB, ~520 KiB total - WP-09),
the socket table, the 1024-entry klog ring (168 KiB; `klog_grow()`
right after heap init copies the 24-entry bootstrap ring over -
nothing is lost), the crash dump buffer (`core_dump_alloc()` once,
up-front: "after this point kmalloc may fail or the heap may corrupt,
but the panic path only ever touches the already-reserved buffer" -
[crash-recovery.md](crash-recovery.md)), and the 32 pipes (moved off a
133 KiB static).

## DMA constraints

Because the kernel identity-maps all physical memory, drivers treat
`phys == virt` for DMA structures and over-allocate + page-align from
the 16-byte-aligned heap (AHCI/NVMe/virtio/EHCI/XHCI/USB all do).
Three families of constraint:

- **Below-4-GiB**: device-visible addresses must fit 32 bits (the
  bcm57xx driver runs a 4 GiB tripwire on every buffer; legacy PCI
  DMA generally).
- **No allocations from 0x400000-0x600000 physical** - PMM reserves
  the window permanently (mechanism above), so a DMA target can never
  land in a range the kernel cannot safely walk on a user CR3.
- **Bounce buffers where the device's scatter rules are awkward**: the
  AHCI per-port 4 KiB bounce chunked to <= 8 sectors so one PRDT entry
  suffices; the ATA BMDMA shared bounce with one 8-sector (4 KiB)
  PRDT entry; NVMe's 4 KiB PRP1 buffer with PRP2=0.

## Failure paths

- PMM exhaustion: the emergency OOM callback list, then allocation
  failure propagates (drivers report honestly - e.g. `blkcache: alloc FAILED - cache disabled (raw I/O)` degrades to uncached I/O rather
  than halting).
- Heap exhaustion / corruption: magic-check failures are reported;
  `kmalloc` returning 0 is handled at call sites (the panic path is
  specifically designed to need no allocation).
- Stack growth outside the 64 KiB window: the fault is not treated as
  growth - it falls to the kill/panic path with attribution.
- Kthread stacks: 10 pages (40 KiB) with the **lowest page unmapped as
  a guard page** (split-huge first, then a fail-loud verification,
  because a huge-PDE unmap is silent); the scheduler panics on any
  saved rsp outside the task's stack window at switch time, and #PF/#DF
  attribution names the overflowed task
  ([scheduler.md](scheduler.md), [interrupts.md](interrupts.md)).

## Numbers at a glance

| Quantity | Value |
|---|---|
| PMM coverage | 4 GiB, 128 KiB bitmap |
| Heap ceiling per alloc | 16 MiB; 16 pools |
| Kernel image limit | < 0x400000 (4 MiB), linker ASSERT |
| Identity window reservation | 0x400000-0x600000 = 512 frames, permanent |
| User stack | 64 KiB, 8 MiB growth cap |
| Kthread stack | 40 KiB + guard page |
| Boot stack | 16 KiB (.bss) |
| IST1/IST2/exit stacks | 8 KiB each |
| User VA span | 0x400000 .. 0x40000000 |
