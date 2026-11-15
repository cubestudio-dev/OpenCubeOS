<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内核数据结构（原文照录集）

开发者在改动相应代码之前必须了解的承重内核结构，从源码
逐字复制，各附其所有权契约：**谁读写它、何时、弄错了会
坏什么**。文件路径是精确的。ABI 关键的布局由
`_Static_assert` 或硬编码汇编偏移钉住 —— 按结构注明。

## 1. 任务控制块 —— `core_sched.h`

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

（`kernel/core_sched.h`；32 个槽位，`MAX_TASKS 32`。）

- **谁/何时**：`core_sched.c` 在 `arch_context_switch` 内部
  写寄存器块（汇编以**硬编码偏移**保存：
  `OFF_FPU=288`、`OFF_FPU_SAVED=800`、`OFF_RFLAGS=208`、
  RIP=192、RSP=216、CR3=248 —— core_sched.c 中的
  `_Static_assert` 把它们钉在 `arch_context_switch.S` 上）。
  调度器每 10 ms 从 IRQ0 读取
  `state/priority/ticks_remaining`；`cr3`/`rsp0` 在每次
  切换时重载（asm 切换之前完成 CR3 重载 + TSS RSP0
  更新）。
- **FPU 区域**：64 个 u64 的 `fxsave` 镜像（512 字节，
  16 字节对齐）。每次切换做 `fxsave`/`fxrstor`；首次切入
  做 `fninit` + `ldmxcsr 0x1F80`
  （`core_sched_init` 中已启用 CR4.OSFXSR + OSXMMEXCPT）。
- **弄错的后果**：重排字段会让 C 与汇编失同步（静态断言
  会抓住）；保存的 `rsp` 落在 `[stack_base, +size)` 之外
  会在切换时刻按设计 panic
  （"kernel stack bounds violated on switch"）。

## 2. 异常/中断帧 —— `arch_idt.h`

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

（`kernel/arch/x86_64/arch_idt.h`；22 个 u64 —— 与核心
转储头中的 `OC_DUMP_REGS_U64 22` 匹配，因此崩溃转储器的
寄存器副本就是同一个结构体。）

- **谁/何时**：`arch_idt_stub.S` 压入 15 个 GPR（带 CPU
  错误码的向量跳过伪造 0 的压栈）、`cld`（BUG-0138）、
  围绕 `call arch_isr_dispatch` 的动态 16 字节对齐
  （BUG-0137）。异常/IRQ/系统调用分发读取它；fork 子进程
  路径（`arch_enter_ring3_fork.S`）以 `rax=0` 恢复全部
  22 个寄存器；`SIGRETURN` 恢复保存的副本；panic 转储器
  以 4x4 打印它。
- **影响**：这个结构体*就是*内核的寄存器 ABI；sigreturn
  蹦床字节 `B8 2A 00 00 00 CD 80 F4`
  （`mov eax,42; int 0x80; hlt`）依赖其栈布局。

## 3. klog 环 —— `core_lib_klog.c`

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

- **谁/何时**：任何内核代码调用 `klog_write(level, module, fmt, ...)`（IRQ 安全：槽位认领是一段短促的 cli/sti 临界
  区）；24 项 BSS 引导环承载最早的启动行，随后
  `klog_grow()`（kmain，紧跟 `mem_heap_init` 之后）
  kmalloc 出 1024 项环并把引导条目拷贝过去 —— 一条不丢。
  回放 op（`dmesg`、`logcat`、供 OCIRE 代理使用的
  `klog_snapshot_text`）经 `klog_slot_of(seq)` 从旧到新
  读取：
  `off = g_total - seq; slot = (g_ring_head + g_ring_cap - off) % g_ring_cap`。
- **影响**：大环放在堆上，因为内核镜像被钉在 0x400000
  以下（链接器 ASSERT）。完整细节见
  [../internals/klog.md](../internals/klog.md)。

## 4. 崩溃结构 —— `services/crash`

核心转储头（`svc_crash_core.h`）：

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

崩溃日志环节（`svc_crash_core.h`）：

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

- **谁/何时**：panic 路径（在 kmain 步骤 16a 布防：
  `core_dump_alloc()` 预先用 kmalloc 保留转储缓冲区，使
  panic 路径永不分配；然后是
  `crash_install_panic_handlers()`、`crashlog_boot_alloc()`、
  `crash_wdt_init()`）写入两者；`crashlog_flush()` 把
  32 槽位环追加到 `/var/log/crash.log`；diag op
  `crashlog`/`dumpinfo` 在重启后读回。
- **影响**：转储镜像按捕获顺序的布局是头、寄存器（22 个
  u64 或活体快照）、任务块、栈窗口（至多 4 KiB，裁剪到
  `[stack_base, +size)`）、表（CR3 + PML4 自扫描 PTE
  计数）；持久化到 `/var/crash/core.dump`，若无文件系统
  则前 2048 字节以十六进制转储到串口。完整故事：
  [../internals/crash-dump.md](../internals/crash-dump.md)。

## 5. 堆块头与空闲链表 —— `core_mm_heap.c`

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

- **谁/何时**：`kmalloc/kzalloc/kfree/krealloc`（空闲链表
  上的首次适配、分裂 + 合并、至多 16 个池来自
  `mem_pmm_alloc_contig`，恒等映射）；分配器钩子
  （`mem_heap_register_allocator_hook`）观察每次分配/释放
  以跟踪泄漏；定时器 IRQ 的 kmalloc 通过 PMM 的 irq-save
  窗口与任务上下文分配互斥（BUG-0043）。
- **影响**：块头恰好 32 字节且 16 字节对齐 —— 奇偶 40
  字节头 bug 曾把 ATA-DMA 缓冲区偏移 8 字节；双重释放经
  魔数检测；单次分配上限 16 MiB。

## 6. PMM 位图 —— `core_mm_pmm.c`

```c
#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12
#define PMM_MAX_PAGES (1024 * 1024)  /* 4 GiB / 4 KiB */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)  /* 128 KiB */
static u8  g_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
```

- **谁/何时**：所有物理帧消费者（VMM 页表、堆池、kthread
  栈、DMA 目标）经 `mem_pmm_alloc`（从旋转提示
  `g_last_scan` 起首次清零位扫描）或
  `mem_pmm_alloc_contig(count)` 分配。
- **影响**：物理窗口 **0x400000-0x600000（512 帧）被永久
  保留**，因为用户地址空间把该*虚拟*窗口重映射到空 PT
  （P3-1 安全修复）—— PMM 绝不能发出那些帧；同样保留：
  首 1 MiB、内核镜像、帧缓冲、MB2 信息/模块、位图本身，
  以及 [内核结束, 0x400000) 这条窄缝。

## 7. VMM PTE 标志与 PT_OWNER 标签 —— `core_mm_vmm.c`

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

- **谁/何时**：`walk_pt(as, vaddr, create)` 在非内核地址
  空间中给它创建的页表帧设置 `VMM_FLAG_PT_OWNER`；地址
  空间销毁只释放它能证明归自己所有的帧（所有权证明，
  非内核 AS 中）；`mem_vmm_is_mapped_user()` 是系统调用
  `access_ok` 所用的 USER(+WRITE) 围栏（supervisor 页
  永远通不过用户缓冲区检查 —— BUG-AUD-2a-001）。
- **影响**：用户 AS 布局是固定的：代码 0x400000、brk
  0x500000、ld.so API 表 0x08000000、ld.so 0x10000000、
  主 PIE 0x20000000（+ASLR）、libfoo 0x30000000（+ASLR）、
  dlopen 递增 0x38000000（+ASLR）、mmap 基 0x3C000000、
  栈顶 0x40000000（64 KiB，8 MiB 增长下限）。

## 8. 启动状态契约 —— `include/ocbl_state.h`

由 OCBL（引导加载程序）、OCOS（被监视载荷）与 OCIRE
（恢复内核）逐字节共享；freestanding 的 `ocst_*` 类型：

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

- **谁/何时**：内核侧（`core_boot_state.c`）**只盲写** ——
  它从不回读该区域；阶段写入发生在 kmain 的每个里程碑，
  心跳搭载 1 秒 tick，同时以原始 PCI/IO 周期接管 6300ESB
  看门狗。OCBL 读/写（判定），OCIRE 读（报告）。布局由
  `_Static_assert` 守护（24/28 字节）。
- **影响**：两块低内存孤岛（0x98000/0x9B000）能在 QEMU
  热复位后存活（E820 保留）；L1 永远够不到它们（每个
  用户 AS 的 supervisor 专属半区 —— 在 L1_READY 时经
  CR3 遍历验证）。

## 9. 驱动 ops 表（四个家族）

一个家族的所有驱动插进同一个 ops 形状；家族管理器（在
对应的服务目录中）遍历收集到的 `.oc_drvs` 声明并探测。

### 9.1 block —— `drivers/storage/drv_storage_api.h`

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

读写经 64 槽位 LRU 写回缓存
（`services/block/interface/svc_block_cache.h` 中的
`driver_block_cache_slot_t`）：

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

- **谁/何时**：fs 驱动 + 分区解析器调用
  `driver_block_read/write`（走缓存）；ATA 驱动经
  `blk_dev_set_ops` 把 PIO 驱动器的 ops 换成 DMA；OCIRE
  嵌入拒绝在受保护设备上的写（`_trusted` 变体是内核内部
  通道）。
- **影响**：牺牲者选择按 LRU 顺序尝试，且只有写回*成功*
  才释放脏槽位（BUG-0116）—— 若所有脏槽位都失败，调用方
  退回写通而不是丢数据。

### 9.2 nic —— `drivers/net/drv_net_api.h`

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

- **谁/何时**：网络栈经 `driver_nic_send/recv` 收发帧
  （`svc_net_core.c` 中遗留的内建 e1000/virtio-net 路径
  保留为回退）；注册时 `send`/`recv` 是必填 op。
- **影响**：`driver_nic_active()` 返回 net.c 路由帧的目标
  设备（首个注册的 NIC）。

### 9.3 snd —— `drivers/audio/drv_audio_api.h`

```c
typedef struct driver_snd_ops {
    int (*play)(driver_snd_device_t *dev, const void *buf, int len);
    int (*stop)(driver_snd_device_t *dev);
    int (*set_rate)(driver_snd_device_t *dev, u32 rate);
    int (*set_volume)(driver_snd_device_t *dev, u32 vol);
    int (*get_caps)(driver_snd_device_t *dev, driver_snd_caps_t *caps);
} driver_snd_ops_t;
```

- **谁/何时**：`driver_snd_play` 是阻塞协作式的（轮询 DMA
  位置、让出）；caps 携带 `sample_rate_test` 所协商的
  支持速率位图。
- **影响**：PCM 契约是小端有符号 16 位交错立体声；
  `play` 接收的是字节数而非采样数；音量按设备设置
  （混音路径应用 `dev->volume`）。

### 9.4 usb 主机控制器 —— `drivers/usb/drv_usb_core.h`

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

- **谁/何时**：UHCI/OHCI/EHCI/XHCI 后端经
  `driver_usb_register_host(host, ops)` 注册；核心枚举
  设备，类驱动（HID/MSC/CDC-ACM/UAC）在其上探测。
- **影响**：`USB_HOSTF_NATIVE_ADDR`（XHCI）意味着核心
  不得发送 SET_ADDRESS —— 后端把它映射为自己的原生
  AddressDevice 命令。

## 10. 自注册描述符

### 10.1 OC_SERVICE_DECL —— `services/svc_framework.h`

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

- **谁/何时**：每个服务文件向 `.oc_svcs` 恰好声明一个
  描述符；`svc_framework_collect()`（kmain 步骤 12c-2）
  遍历 `__oc_svcs_start.._end`（边界来自 linker.ld）装进
  `s_svcs[64]`；`svc_framework_dispatch` 在每次 L1SVC
  调用时按类别字符串精确匹配扫描。
- **影响**：添加一个服务 = 添加一个文件（NAK 规则）。见
  [writing-services.md](writing-services.md)。

### 10.2 OC_DRIVER_DECL —— `drivers/drv_framework.h`

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

- **谁/何时**：一个驱动文件向 `.oc_drvs` 声明身份；
  `drv_framework_collect()` 填充运行时表；家族管理器
  （block/nic/snd/usb/wireless/sdio）在探测轮中遍历
  `drv_framework_at(i)` 并匹配 `family` 字符串。
- **影响**：添加一个驱动 = 在 `ocos/drivers/<class>/` 放
  一个 .c 文件 + 一行 OC_DRIVER_DECL —— 不改全局表、
  没有注册调用点、不动核心文件。

## 11. 管道 —— `services/sysapi/src/svc_sysapi_syscall.c`

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

- **谁/何时**：`SYS_PIPE` 创建；`user_proc_t.fds[]` 中的
  fd 携带类别（0=空闲、1=VFS fd、2=pipe-read、
  3=pipe-write）；按端点的 tid 等待队列保证多读者安全
  （P3-15）；`pipe_close_fd(pipe_id, kind)` 已导出供统一
  回收器使用。
- **影响**：该表惰性 kmalloc（从 133 KiB 静态数组迁移，
  以遵守 0x400000 镜像上界）；至多 32 个管道。

## 12. 同步原语 —— `core_sync.h`

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

- **谁/何时**：自旋锁 = 保存 RFLAGS + cli 获取，先释放
  再恢复 IF（BUG-0285 irqsave 语义）；持有者不得
  睡眠/让出。信号量的等待者注册 + BLOCKED 发生在同一
  irqsave 窗口内（P0-5 丢失唤醒修复）。互斥锁是 FIFO、
  **没有优先级继承**（文档化的刻意不公平；死属主由下一个
  竞争加锁者强制释放）。
- **影响**：`sync_get_stats()` 供给 `syncstat` diag op。

## 13. OCIRE 嵌入邮箱 —— `services/ocembed`

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

- **谁/何时**：OCIRE（恢复监督者）在其卷上拥有该邮箱；
  OCOS 嵌入服务在定位到覆盖块设备并*首先*启用全盘写保护
  之后，恰好在状态为 WAIT_SIGNAL 时写一次 SIGNAL_OK。
- **影响**：`oc.log=<LBA>`/`oc.logsecs`/`oc.mb`/`oc.wdt`/
  `oc.agent=1` 命令行事实激活整条通道；在普通 GRUB 启动
  上该服务休眠。完整页面：
  [../internals/ocembed-guard.md](../internals/ocembed-guard.md)。
