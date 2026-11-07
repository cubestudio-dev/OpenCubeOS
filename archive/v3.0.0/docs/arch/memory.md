<!-- SPDX-License-Identifier: Apache-2.0 -->
# Memory management

## PMM - the physical page store

`kernel/mem/mem_pmm.{c,h}`: a bitmap over 4 KiB page frames built
from the multiboot2 memory map. Reserved regions (kernel image,
framebuffer, the mbi itself) are marked used at init. O(1) alloc and
free. `memtest` / `pmmrace` exercise it in-guest.

## VMM - four-level paging

`kernel/mem/mem_vmm.{c,h}`: the kernel keeps boot.S's identity map of
the low 4 GiB; VMM creates and edits additional address spaces for
user processes. Protection flags map to read/write/nx/user bits.
`vmtest`, `vmmap`, `vmkernelpt`, `cr3test` inspect it live.

## Heap - kmalloc/kfree

`kernel/mem/mem_heap.{c,h}`: free-list, first-fit, coalescing; blocks
are 16-byte aligned (the header is exactly 32 bytes - a historical
alternating-parity bug made payload alignment depend on allocation
parity, which then shifted ATA-DMA PRDT windows and broke FAT32
mounts; the header size is now static-asserted). 16 MiB fail-fast
ceiling; `heaptest` + `heapbounds` are the tests, `heap`/`frag` the
inspectors.

## Growth by fault

Stack-growth and heap-growth #PF are handled in the exception path:
a fault below a task's stack pointer within its stack region maps a
new page; heap growth maps backing pages; anything else escalates to
the crash path. `exc` proves the trap machinery each boot.

## DMA constraints

Drivers that program physical addresses into device structures
validate alignment and, for 32-bit DMA masks, that the address is
below 4 GiB (xHCI refuses above-4GiB DMA at probe AND per-transfer;
ATA-DMA masks the PRDT). This is why the heap's alignment guarantees
are load-bearing.
