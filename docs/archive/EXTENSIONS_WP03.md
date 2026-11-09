<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-03 Extension API

WP-03 adds **four** new L0->L1 extension points (on top of WP-01's 4 and WP-02's 6). All previous interfaces remain unchanged.

---

## 11. Physical Memory Manager (PMM)

```c
#include "mem_pmm.h"

u64  mem_pmm_alloc_frame(void);
void mem_pmm_free_frame(u64 paddr);
void mem_pmm_get_stats(mem_pmm_stats_t *out);
void mem_pmm_register_emergency_callback(mem_pmm_emergency_cb_fn fn);
```

`mem_pmm_alloc_frame()` returns a 4 KiB physical page address, or 0 on failure. The page is identity-mapped in the kernel address space (first 4 GiB), so you can use the returned address directly as a pointer.

`mem_pmm_stats_t` contains: `total_pages`, `used_pages`, `free_pages`, `total_bytes`, `used_bytes`, `free_bytes`, `free_fragments`.

### Usage

```c
u64 page = mem_pmm_alloc_frame();
if (page == 0) { /* out of memory */ }
memset((void*)page, 0, 4096);  /* identity-mapped, safe to write */
/* ... use the page ... */
mem_pmm_free_frame(page);
```

Emergency callbacks fire when PMM is about to return 0. L1 can free cached pages to let the allocation succeed.

---

## 12. Virtual Memory Manager (VMM)

```c
#include "mem_vmm.h"

mem_vmm_as_t mem_vmm_create_address_space(void);
void     mem_vmm_destroy_address_space(mem_vmm_as_t as);
int      mem_vmm_map_page(mem_vmm_as_t as, u64 vaddr, u64 paddr, u64 flags);
u64      mem_vmm_unmap_page(mem_vmm_as_t as, u64 vaddr);
int      mem_vmm_protect_page(mem_vmm_as_t as, u64 vaddr, u64 flags);
int      mem_vmm_is_mapped(mem_vmm_as_t as, u64 vaddr, u64 *paddr_out);
void     mem_vmm_switch_as(mem_vmm_as_t as);
mem_vmm_as_t mem_vmm_current_as(void);
void     mem_vmm_register_fault_handler(mem_vmm_fault_handler_fn fn);
void     mem_vmm_get_fault_stats(mem_vmm_fault_stats_t *out);
```

`mem_vmm_as_t` is the physical address of the PML4 table. `0` = kernel address space.

Page flags: `VMM_FLAG_PRESENT`, `VMM_FLAG_WRITE`, `VMM_FLAG_USER`, `VMM_FLAG_NOEXEC`, `VMM_FLAG_COW`.

New address spaces have the kernel region mapped (shared PML4 entries). User space is empty.

### Usage

```c
mem_vmm_as_t as = mem_vmm_create_address_space();
u64 phys = mem_pmm_alloc_frame();
mem_vmm_map_page(as, 0x100000000, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
/* ... */
mem_vmm_unmap_page(as, 0x100000000);
mem_vmm_destroy_address_space(as);
```

Fault handler: `typedef int (*mem_vmm_fault_handler_fn)(u64 vaddr, u64 error_code, u64 rip);` Return 1 = handled (resume), 0 = let L0 handle.

---

## 13. Kernel Heap

```c
#include "mem_heap.h"

void *kmalloc(u64 size);
void *kzalloc(u64 size);
void  kfree(void *ptr);
void *krealloc(void *ptr, u64 new_size);
void  mem_heap_get_stats(mem_heap_stats_t *out);
void  mem_heap_register_allocator_hook(mem_heap_hook_fn fn);
```

Standard malloc/free API. `kzalloc` zeros the allocation. `krealloc` may move data. `kfree` detects double-free (silently ignores).

`mem_heap_stats_t` contains: `mem_heap_size`, `allocated`, `free`, `overhead`, `alloc_count`, `free_count`, `total_allocs`, `total_frees`.

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

Allocator hook: `typedef void (*mem_heap_hook_fn)(void *ptr, u64 size, int is_alloc);` Called on every alloc/free. Useful for debugging/leak tracking.

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
    screen_console_puts("my command called with: ");
    screen_console_puts(args);
    screen_console_putc('\n');
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
