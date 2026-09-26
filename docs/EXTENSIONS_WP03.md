<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-03 Extension API

WP-03 adds **four** new L0->L1 extension points (on top of WP-01's 4 and WP-02's 6). All previous interfaces remain unchanged.

---

## 11. Physical Memory Manager (PMM)

```c
#include "pmm.h"

u64  pmm_alloc_frame(void);
void pmm_free_frame(u64 paddr);
void pmm_get_stats(pmm_stats_t *out);
void pmm_register_emergency_callback(pmm_emergency_cb_fn fn);
```

`pmm_alloc_frame()` returns a 4 KiB physical page address, or 0 on failure. The page is identity-mapped in the kernel address space (first 4 GiB), so you can use the returned address directly as a pointer.

`pmm_stats_t` contains: `total_pages`, `used_pages`, `free_pages`, `total_bytes`, `used_bytes`, `free_bytes`, `free_fragments`.

### Usage

```c
u64 page = pmm_alloc_frame();
if (page == 0) { /* out of memory */ }
oc_memset((void*)page, 0, 4096);  /* identity-mapped, safe to write */
/* ... use the page ... */
pmm_free_frame(page);
```

Emergency callbacks fire when PMM is about to return 0. L1 can free cached pages to let the allocation succeed.

---

## 12. Virtual Memory Manager (VMM)

```c
#include "vmm.h"

vmm_as_t vmm_create_address_space(void);
void     vmm_destroy_address_space(vmm_as_t as);
int      vmm_map_page(vmm_as_t as, u64 vaddr, u64 paddr, u64 flags);
u64      vmm_unmap_page(vmm_as_t as, u64 vaddr);
int      vmm_protect_page(vmm_as_t as, u64 vaddr, u64 flags);
int      vmm_is_mapped(vmm_as_t as, u64 vaddr, u64 *paddr_out);
void     vmm_switch_as(vmm_as_t as);
vmm_as_t vmm_current_as(void);
void     vmm_register_fault_handler(vmm_fault_handler_fn fn);
void     vmm_get_fault_stats(vmm_fault_stats_t *out);
```

`vmm_as_t` is the physical address of the PML4 table. `0` = kernel address space.

Page flags: `VMM_FLAG_PRESENT`, `VMM_FLAG_WRITE`, `VMM_FLAG_USER`, `VMM_FLAG_NOEXEC`, `VMM_FLAG_COW`.

New address spaces have the kernel region mapped (shared PML4 entries). User space is empty.

### Usage

```c
vmm_as_t as = vmm_create_address_space();
u64 phys = pmm_alloc_frame();
vmm_map_page(as, 0x100000000, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
/* ... */
vmm_unmap_page(as, 0x100000000);
vmm_destroy_address_space(as);
```

Fault handler: `typedef int (*vmm_fault_handler_fn)(u64 vaddr, u64 error_code, u64 rip);` Return 1 = handled (resume), 0 = let L0 handle.

---

## 13. Kernel Heap

```c
#include "heap.h"

void *kmalloc(u64 size);
void *kzalloc(u64 size);
void  kfree(void *ptr);
void *krealloc(void *ptr, u64 new_size);
void  heap_get_stats(heap_stats_t *out);
void  heap_register_allocator_hook(heap_hook_fn fn);
```

Standard malloc/free API. `kzalloc` zeros the allocation. `krealloc` may move data. `kfree` detects double-free (silently ignores).

`heap_stats_t` contains: `heap_size`, `allocated`, `free`, `overhead`, `alloc_count`, `free_count`, `total_allocs`, `total_frees`.

### Usage

```c
void *buf = kmalloc(1024);
if (!buf) { /* out of memory */ }
/* ... use buf ... */
kfree(buf);

char *str = kzalloc(256);  /* zeroed */
/* ... */
kfree(str);
```

Allocator hook: `typedef void (*heap_hook_fn)(void *ptr, u64 size, int is_alloc);` Called on every alloc/free. Useful for debugging/leak tracking.

---

## 14. Shell Command Registration

```c
#include "shell.h"

int shell_register_command(const char *name, shell_cmd_fn handler, const char *help);
int shell_unregister_command(const char *name);
```

`typedef int (*shell_cmd_fn)(const char *args);` - `args` is everything after the command name.

### Usage

```c
static int my_command(const char *args) {
    oc_console_puts("my command called with: ");
    oc_console_puts(args);
    oc_console_putc('\n');
    return 0;
}

void init_my_shell_ext(void) {
    shell_register_command("mycmd", my_command, "do something cool");
}
```

After registration, the user can type `mycmd hello world` at the `oc>` prompt and the handler receives `"hello world"` as `args`.

---

## ABI Stability

All WP-03 functions and their typedefs are frozen. Future WPs may add new functions but will not change existing ones incompatibly.
