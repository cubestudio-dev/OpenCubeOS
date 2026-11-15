<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内存管理：PMM、VMM、堆与恒等窗口

三个分配器协同工作：PMM 拥有物理帧，VMM 拥有页表，堆拥有内核
对象。它们都活在扁平的 `ocos/kernel/core_mm_*` 前缀之下。绑定四个
引导组件的物理内存地图见 [system-map.md](system-map.md)；恒等窗口
的合谋见 [hidden-mechanisms.md](hidden-mechanisms.md)。

## PMM——位图分配器（`core_mm_pmm.c`）

```c
#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12
#define PMM_MAX_PAGES (1024 * 1024)  /* 4 GiB / 4 KiB */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)  /* 128 KiB */
static u8  g_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
```

每个 4 KiB 页一个比特，覆盖恒等映射的 4 GiB。分配是从旋转提示
`g_last_scan` 起始的首次清零位扫描（该提示使引导后的顺序分配为
O(1)，而不必从 0 重扫）；`mem_pmm_alloc_contig(count)` 是面向 DMA
与池请求的首次适配连续段扫描。每次扫描加标记都运行在
`pmm_irq_save/restore` 窗口内（pushfq + cli，仅当原先置位时才恢复
IF）——BUG-0043 修复：在这单 CPU 内核上，定时器 IRQ 中的
`kmalloc` 可能与任务上下文的分配竞争，因此 PMM 操作必须 IRQ
原子。

**初始化时预留**：前 1 MiB；内核镜像 [1 MiB,
`__end_of_kernel`)；帧缓冲；mb2 信息 + 模块；位图本身；**物理窗口
0x400000-0x600000（512 帧 = 2 MiB），永久**——用户地址空间把该
*虚拟*范围重映射到空 PT，因此内核在用户 CR3 上绝不能解引用在该
*物理*范围内分发出去的 PMM 帧（它们会别名映射用户代码）；外加
[内核结束, 0x400000) 这一小条。

紧急行为：最终失败之前最多触发 4 个 OOM 回调；统计跟踪
total_allocs/total_frees/alloc_failures 与 free_fragments（连续段
计数——`frag` 命令打印碎片化，`mem` 打印其余）。

## VMM——四级页表管理器（`core_mm_vmm.c`）

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

引导映射：前 4 GiB 经 PML4[0] -> PDPT[0..3] -> 4 张 PD 表，每张
512 个 2 MiB 大页表项（标志 `0x83` = present|writable|huge|global；
NX 清零——内核页可执行）。

- `walk_pt(as, vaddr, create)`——四级遍历；当 `create` 需要低于
  大页的粒度时**按需把 2 MiB 大页 PDE 拆成 512 个叶子 PTE**
  （幂等）。在非内核地址空间中创建的私有 PT 被打上
  `VMM_FLAG_PT_OWNER` 标签，使 `destroy` **凭所有权证明**释放页表
  帧——内核共享的表绝不能经一个垂死的用户地址空间释放。
- `mem_vmm_split_huge_page()`——公开的强制 4K API。之所以必要，
  是因为**大页 PDE 的 unmap 否则是无声的空操作**——kthread 守卫页
  的 unmap 先拆分，再以高声报警的 klog 验证。
- `mem_vmm_is_mapped_user()`——系统调用 `access_ok` 使用的
  USER(+WRITE) 标志栅栏（BUG-AUD-2a-001：supervisor 页绝不能通过
  用户缓冲检查）。
- `mem_vmm_register_fault_handler(fn)`——最多 4 个 L1 缺页处理器
  链槽位，由缺页路径最先运行。

### 用户地址空间（`create_user_address_space`，`core_usermode.c`）

全新 PML4；拷贝内核 PML4[1..3]；一份私有的 PDPT[0] + PD0，是内核
512 x 2 MiB 大页 PDE 的副本；内核 PDPT[1..3] PD 页的私有副本
（BUG-AUD-2a-001：内核侧的大页拆分不再改写用户地址空间）；以及
**PD0[2]（0x400000-0x600000）被替换为空 PT**——P3-1 安全修复，
绝不预先填充内核物理页。这就是恒等窗口陷阱：ring 3 链接于
0x400000，因此该窗口必须在虚拟上存在，却绝不能别名映射内核的旧
物理后备。

用户 VA 布局：代码 0x400000、brk 0x500000、ld.so API 表
0x08000000、ld.so 0x10000000（固定）、主 PIE 0x20000000（+ASLR）、
libfoo 0x30000000（+ASLR）、dlopen 递增 0x38000000（+ASLR）、
mmap 基址 0x3C000000（窗口止于 0x3FFF0000，16 区域所有权表）、
栈顶 0x40000000（64 KiB）。ASLR：
`user_aslr_page_slice(region_span)`——两次 rdtsc 采样经 splitmix64
终结器，页对齐，区域顶部之下保持 1 MiB 清空。

### 缺页处理器

`mem_vmm_handle_page_fault(vaddr, error_code, rip, faulting_rsp)`：

1. L1 处理器链最先运行（最多 4 个已注册处理器）。
2. CPU 错误码位被尊重（P/W/U/S、I/D——BUG-0044 修复链）；
   **保护违例（P=1）绝不重映射**。
3. 内核态在用户地址上的 not-present 缺页被报告为内核对用户指针的
   错误解引用——不被粉饰。
4. 用户栈增长：仅允许在缺页 RSP 之下 64 KiB 且不低于 8 MiB 下限
   （`VMM_USER_STACK_TOP - VMM_STACK_LIMIT_BYTES`）。
5. 未处理的用户缺页落入 ring 3 击杀路径（先投递 SIGSEGV——见
   [interrupts.md](interrupts.md)）。

## 堆——空闲链表分配器（`core_mm_heap.c`）

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

32 字节头部由 static assert 钉死；两个魔数常量使双重释放与释放后
使用可被检测（kfree 检查 `HEAP_MAGIC`，不匹配即报告并拒绝）。对
空闲链表首次适配并伴分裂 + 合并；最多 **16 个池**以 base+pages
跟踪，因此跨池边界的合并也能工作；池来自 `mem_pmm_alloc_contig`
且恒等映射，因此无需 VMM 映射。单次分配上限：**16 MiB**
（`HEAP_MAX_POOL_PAGES`）。`mem_heap_register_allocator_hook` 为
泄漏跟踪而存在。

对齐纪律在源码中有长篇文档：一次奇校验 40 字节头部的实验曾产生
8 字节偏移并破坏 ATA DMA——32 字节/16 字节纪律是承重的，不是
风格问题。

## 为什么用堆而不是 .bss——0x400000 增长纪律

链接器强制执行：

```
ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the
0x400000 identity-window boundary - move large buffers to kmalloc")
```

凡是在小内核时代之后长出来的东西，*正是因此*都搬到了引导期
kmalloc，且每次搬迁都是有据可查的 bug 修复：TCP 连接表（32 x 约
16 KiB，共约 520 KiB——WP-09）、套接字表、1024 条目的 klog 环
（168 KiB；堆初始化后立即执行的 `klog_grow()` 把 24 条目的引导环
拷贝过去——不丢失任何东西）、崩溃转储缓冲（`core_dump_alloc()`
一次性预先分配：“此后 kmalloc 可能失败、堆可能损坏，但 panic
路径只触碰已预留的缓冲”——见 [crash-recovery.md](crash-recovery.md)），
以及 32 个管道（从 133 KiB 静态区搬离）。

## DMA 约束

由于内核恒等映射全部物理内存，驱动对 DMA 结构按 `phys == virt`
处理，并从 16 字节对齐的堆超额分配 + 页对齐（AHCI/NVMe/virtio/
EHCI/XHCI/USB 皆然）。三类约束：

- **4 GiB 以下**：设备可见地址必须装进 32 位（bcm57xx 驱动对每个
  缓冲设 4 GiB 绊线；一般而言的传统 PCI DMA）。
- **不得从物理 0x400000-0x600000 分配**——PMM 永久预留该窗口
  （上文机制），因此 DMA 目标绝不会落进内核在用户 CR3 上无法安全
  遍历的范围。
- **设备分散规则别扭之处用弹跳缓冲**：AHCI 每端口 4 KiB 弹跳按
  <= 8 扇区分块使单个 PRDT 表项即可；ATA BMDMA 共享弹跳配单个
  8 扇区（4 KiB）PRDT 表项；NVMe 的 4 KiB PRP1 缓冲配 PRP2=0。

## 失败路径

- PMM 耗尽：紧急 OOM 回调链，随后分配失败向上传播（驱动如实报告
  ——例如 `blkcache: alloc FAILED - cache disabled (raw I/O)` 降级
  为无缓存 I/O 而非停机）。
- 堆耗尽/损坏：魔数检查失败会被报告；`kmalloc` 返回 0 在调用点
  处理（panic 路径被专门设计为无需任何分配）。
- 64 KiB 窗口之外的栈增长：该缺页不被当作增长——落入带归因的
  击杀/panic 路径。
- kthread 栈：10 页（40 KiB），**最低一页不映射作为守卫页**（先拆
  大页，再做高声报警的验证，因为大页 PDE 的 unmap 是无声的）；
  调度器在切换时对任何保存在任务栈窗口之外的 rsp 直接 panic，且
  #PF/#DF 归因会指名溢出的任务（见 [scheduler.md](scheduler.md)、
  [interrupts.md](interrupts.md)）。

## 数字速览

| 量 | 值 |
|---|---|
| PMM 覆盖 | 4 GiB，128 KiB 位图 |
| 单次堆分配上限 | 16 MiB；16 个池 |
| 内核镜像上限 | < 0x400000（4 MiB），链接器 ASSERT |
| 恒等窗口预留 | 0x400000-0x600000 = 512 帧，永久 |
| 用户栈 | 64 KiB，8 MiB 增长上限 |
| kthread 栈 | 40 KiB + 守卫页 |
| 引导栈 | 16 KiB（.bss） |
| IST1/IST2/退出栈 | 各 8 KiB |
| 用户 VA 跨度 | 0x400000 .. 0x40000000 |
