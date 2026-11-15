<!-- SPDX-License-Identifier: Apache-2.0 -->
# 隐藏机制全集：从任何单个文件都看不见的承重行为

这些机制让 Open Cube OS 得以运转，却存活于文件*之间*、接口*之下*，
或固定的魔数常量之内。每个条目陈述机制本身、触发条件、代码所在
位置及其影响。本页之所以存在，是因为即便读者分别了解了每个目录，
缺少这十二条仍然无法预测系统行为。

## 1. 0x400000-0x600000 恒等窗口陷阱

**机制。**用户地址空间由 `ocos/kernel/core_usermode.c` 中的
`create_user_address_space()` 创建：全新的 PML4、拷贝内核
PML4[1..3]、对内核 512 x 2 MiB 大页 PDE 做一份*私有*的 PDPT[0]+PD0
拷贝——然后，覆盖虚拟 0x400000-0x600000 的 `PD0[2]` 被**替换为
一张空页表**。ring 3 代码链接于 0x400000（`ocos/l1apps/user.ld`），
因此每个用户程序恰好活在这一窗口内。旧恒等窗口背后的物理帧对
任何用户 CR3 都不可见。

**触发。**每次进程创建与每次 fork 地址空间拷贝。

**代码。** `ocos/kernel/core_mm_vmm.c`（遍历/标志机制，见下文
`VMM_FLAG_PT_OWNER`）、`ocos/kernel/core_usermode.c`（PD0[2] 交换
——P3-1 安全修复：绝不预先填充内核物理页）、
`ocos/kernel/arch/x86_64/linker.ld`（`ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the 0x400000 identity-window boundary - move large buffers to kmalloc")`）、`ocos/kernel/core_mm_pmm.c`
（窗口预留）。

**影响。**三重协防：(a) 链接器拒绝任何可能长到该窗口的内核镜像；
(b) PMM 在初始化时*永久预留*物理 0x400000-0x600000 的 512 个帧
（2 MiB）——它们永远不能被分发为页表或 DMA 目标，因为内核在用户
CR3 上运行时无法安全解引用它们；(c) 用户地址空间在此看到一张
空 PT，因此用户态解引用会触发异常而不是别名映射内核内存。对每位
开发者的后果：内核镜像中禁止大型静态缓冲——这正是 TCP 连接表、
套接字表、崩溃转储缓冲与管道全都活在堆上的原因
（[network-stack.md](network-stack.md)、[crash-recovery.md](crash-recovery.md)）。

## 2. `core_kcap`——控制台钩子捕获栈

**机制。**控制台驱动（`ocos/kernel/core_console_screen.c`）持有
**恰好一个输出钩子槽位**。`ocos/kernel/core_kcap.c` 中的
`core_kcap_run_fn(fn, arg, out, cap)` 把一次捕获压入栈（最多 4 层），
令最内层捕获成为控制台钩子本体，运行 `fn`，再弹出。捕获激活期间，
函数打印的每个字符都被收集进调用者的缓冲区——帧缓冲与串口分岔
**都看不到它**。基础钩子（由 kmain 安装的串口编码器分岔）在最后
一次捕获结束时重新装回。溢出会被标记，绝不静默截断。

**触发。**每个 op 体是遗留打印式命令的 `SYS_L1SVC` 调用：系统调用
路径路由为 `sys_l1svc -> <cat> dispatch -> core_kcap_run_fn(body, ...)`。正是这一机制让 208 个服务 op 保留 v1/v2.2.0 时代的
`screen_console_puts` 函数体，同时仍能向 ring 3 返回文本。

**代码。** `ocos/kernel/core_kcap.{c,h}`（147 行——目录树中最小的
承重文件）；消费者位于每个 `ocos/services/*/src/svc_*_l1.c`。

**影响。**整个 L1SVC 文本面都建立在它之上
（[syscall-ring3.md](syscall-ring3.md)）。它的限制即是契约：被捕获
的函数体不得睡眠或驻留——`power sleep`、`sshd` 以及全部 9 个 TUI
op 走**直通**路由（`out[0]=0` 加实时控制台输出），因为以捕获方式
运行它们会吞掉实时控制台行（实测发现：带 OCIRE 日志代理的监督
引导在代理第一次睡眠开始的瞬间就陷入了控制台静默）。嵌套：一个
服务体自身再调用另一个被捕获体时可嵌套至 4 层深；只有最内层的
活跃捕获接受数据。

## 3. OCST——跨三个内核的引导状态接力

**机制。**两块能在 QEMU 热复位中幸存的 4 KiB 低内存孤岛，在 OCBL
构建的 multiboot2 映射中被划为 E820 保留：

```c
#define OCST_STATE_ADDR        0x98000u
#define OCST_RESERVED_ADDR     0x9B000u
#define OCST_STATE_MAGIC       0x3153434Fu   /* "OCS1" */
#define OCST_RESERVED_MAGIC    0x3142434Fu   /* "OCB1" */
```

状态区（0x98000）承载 `stage`（RESET=0、KM_AIN_ENTERED=1、
SCHED_READY=2、DRIVERS_READY=3、SERVICES_READY=4、L1_LAUNCHED=5、
L1_READY=6、RUNTIME=255）、一个心跳和一个进度时钟。保留区
（0x9B000）承载事后 `ocst_record_t`（类别 BOOT_FAILED 或
USER_REQUEST、静默秒数、反码校验和）。完整布局位于
`include/ocbl_state.h`，由 `_Static_assert` 守护（24 与 28 字节）。

**触发。**正常引导：OCBL 在向 OCOS 交接之前，在 Intel 6300ESB
看门狗（PCI 8086:25AB）上布防 30 秒引导预算（每阶段 10 秒、L1
等待 30 秒、运行时心跳 15 秒）；内核（`core_boot_state.c`）接管
看门狗并**盲写**阶段/心跳——它从不回读孤岛；kmain 的 1 秒节拍
喂狗。失败：若内核挂起，看门狗硬复位；下一次引导 OCBL 读取孤岛，
以**>= 8 秒 RTC 静默**判定失败，写入 BOOT_FAILED 记录，显示 4 秒
提示，脉冲 8042 复位——而*再下一次*引导自动路由到 OCIRE 条目，
一次性（`ocbl_statemon_route_pending()`）。

**代码。** `include/ocbl_state.h`（三方构建契约）、
`ocbl/common/ocbl_statemon.c`（判定/布防/路由）、
`ocos/kernel/core_boot_state.c`（受监视一侧——解析 `oc.hang=N` 与
`oc.stmon=off`）、`ocire/ocire_ocbl.c`（事后读取器，解释机器*为何*
处于恢复模式，并给出按阶段的修复建议）。

**影响。**一台变砖的机器能在三个互不共享代码的独立内核之间自我
诊断，且全程没有可写磁盘介入。OCIRE 与 OCSE 的交接得到的是
`ocbl_wdt_disarm()` 而非 statemon 布防（它们有自己的看门狗故事
——过去一个盲目的 60 秒窗口会复位仍在运行的安装器会话，即 WP07a
修复）。细节见 [crash-recovery.md](crash-recovery.md)。

## 4. IST 纪律——异常路径的三张栈

**机制。**TSS（`arch_idt.c`）携带 IST1 = 8 KiB `g_panic_stack`
（向量 8 #DF 与 18 #MC）和 IST2 = 8 KiB `g_debug_stack`（向量 1
#DB、2 NMI、3 #BP）。在 IST2 向量上执行的 ring 3 击杀绝不能在
IST2 自身上运行——调度器会把 rsp 保存在 IST2 内部，下一次恢复
就会破坏它。因此击杀路径先切换到专用的 **8 KiB 退出栈**：在
reap+yield 之前先走 `arch_exc_run_on_exit_stack()`
（`arch_exc_exit.S`，BUG-0139 修复）。

**触发。**任何异常递交：CPU 硬件从 IDT 门中选择 IST；
`arch_exc_dispatch` 决定用户击杀还是 panic；向量 1/2/3 走退出栈
绕行。

**代码。** `ocos/kernel/arch/x86_64/arch_idt.c`（TSS/IDT 设置、
IST 分派）、`arch_idt_stub.S`（256 个桩——带错误码的向量跳过伪
压栈）、`arch_exc_exit.S`（42 行）、`arch_exceptions.c`（分派链）。

**影响。**#DF/#MC panic 获得一张其他任何代码都无法写入的栈
（panic 机制可以信任自己的栈）；ring 3 中的调试器/断点活动不会
卡死调度器；而“内核栈溢出”归因（守卫页上的 #PF、rsp 低于栈底的
#DF——两者都指名任务）即便在分派器深度也能工作。
[interrupts.md](interrupts.md) 有完整的向量映射。

## 5. 惰性 gzip 程序注册表——284 个应用，一次一个

**机制。** `tools/gen_userprogs.py` 仅针对 `ocos/interface/` 编译
每个 `ocos/l1apps/<app>/` 目录，gzip 压缩每个 ELF，并产出
`ocos/kernel/userprogs_data.h`（27,082 行）：`userprog_gz_<name>[]`
字节数组外加 `OC_USERPROGS[]` 注册表（名字、gz 指针、raw 指针、
gz_len、raw_size、wp 类别标签、双语提示）。在 `SYS_EXECVE` /
`core_userprog_lookup()` 时刻，内核按需解压应用（`core_lib_gzip.c`，
RFC 1951/1952，带 CRC32+ISIZE 校验）并按应用缓存原始 ELF。
**`ush` 保持 raw**——不压缩、直接链入镜像——因为 `OC_L1_APP_DECL`
（`core_boot_l1launch.c` 中的默认入口声明）需要编译期 ELF 指针。

**触发。**284 个程序中任何一个的首次执行；每次 `ldd <program>`
自省；`job_create`。

**代码。** `ocos/kernel/userprogs_data.h`（生成物）、
`core_usermode.c`（`core_userprog_lookup`）、`core_lib_gzip.c`、
`tools/gen_userprogs.py`。

**影响。**内核镜像在装载 284 个程序的同时保持在 0x400000 链接
ASSERT 之下（见机制 1）——压缩载荷主导了镜像体积但仍可负担，
未压缩 ELF 缓存活在堆上而非 .bss。`l1` 服务的 `help` op 从注册表
渲染（任何地方都没有手写命令清单），`-w` 按 wp 标签分组
（[syscall-ring3.md](syscall-ring3.md)）。

## 6. `.oc_svcs` / `.oc_drvs` / `.oc_apps`——服务不是进程

**机制。**“服务”是链接期注册的内核对象，不是 fork 出来的守护
进程。`OC_SERVICE_DECL`/`OC_SERVICE_DECL_L1`
（`ocos/services/svc_framework.h`）把描述符放进 `.oc_svcs` 链接段；
`OC_DRIVER_DECL`（`ocos/drivers/drv_framework.h`）同样放进
`.oc_drvs`；`OC_L1_APP_DECL` 放进 `.oc_apps`。`linker.ld` 导出
`__oc_svcs_start/_end`（以及驱动/应用的对应物）；引导时
`svc_framework_collect()` / `drv_framework_collect()` /
`core_registry_collect()` 遍历各段收进静态表（64 个服务、256 个
驱动、应用注册表），kmain 记录日志 `registry: drv=N svc=N app=N`。
分派是一次类别字符串遍历：

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

**触发。**引导期收集（kmain 步骤 "registry: drv=N svc=N app=N"），
之后是每次 `SYS_L1SVC` 调用与每次家族探测遍。

**代码。** `ocos/services/svc_framework.{c,h}`、
`ocos/drivers/drv_framework.{c,h}`、`ocos/kernel/core_registry.{c,h}`、
`ocos/kernel/arch/x86_64/linker.ld`（`SUBALIGN(8)` 恒等段）。

**影响。**NAK 规则（“一切皆可通过增加一个文件来扩展”）：一个新
驱动或服务就是一个 `.c` 文件加一行声明——无需改全局表，无需注册
调用点。引导顺序仍是显式、手工编排的 kmain 序列；收集来的表只是
身份/分派面。引导期除了 ring 3 启动器（ush）以及监督引导时的
OCIRE 日志代理外，不 fork 任何东西。`placeholder` 驱动
（`ocos/drivers/misc/drv_misc_placeholder.c`）是对该机制的有意实测
——被收集，从不被探测。[driver-model.md](driver-model.md) 走读
驱动一侧。

## 7. OCIRE 守卫通道——第二个内核的邮箱

**机制。**在 OCIRE 监督的引导中，OCOS 带着额外的命令行事实启动
（`oc.log=<lba> oc.logsecs=<n> oc.mb=<lba> oc.wdt=ib700 oc.agent=1`）。随后 `ocembed` 服务
（`ocos/services/ocembed/src/svc_ocembed_core.c`）：

1. 通过扫描块设备找到恢复磁盘——寻找覆盖邮箱 LBA 且该扇区读出
   `OCIREMB2` 魔数的设备；
2. **先整盘写保护**（`driver_block_set_protected`）——此后经任何
   覆盖设备的写入都被拒绝；只有内核内部的
   `driver_block_*_sectors_trusted` 通道可以写；
3. 确认引导：邮箱状态 WAIT_BOOT_SIGNAL -> SIGNAL_OK（boot_count++），
   并解除监督者布防的 i6300esb 看门狗（其 60 秒窗口“已过约 2 秒”）；
4. 把日志代理 ELF 拉起为一个带 `ocire_guard` 标志的进程——
   `sys_kill` 拒绝发往受守护进程的一切信号，且该二进制位于 OCOS
   完全无法写入的 OCIRE 卷上。

代理进入内核的仅有的两扇门是仅限守卫的 L1SVC op 4（LOG_APPEND——
向 `OCIRELOG1` 引导日志环追加）与 op 5（KLOG_SNAPSHOT——内核日志
环文本）；`caller_is_guard()` 检查进程标志，并以
`"ocire: refused (guard-only channel)"` 拒绝其他一切调用者。邮箱
（128 字节，packed）：

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

**触发。**仅 OCIRE kexec 引导（命令行上的邮箱 LBA 事实）。在正常
的 OCBL/GRUB 引导中该服务休眠：`"ocire: embed inactive (normal boot)"`。

**代码。** `ocos/services/ocembed/`（整个家族；类别字符串
`"ocire"`——目录名规则唯一被声明的例外，冻结 ABI）、
`ocire/ocire_bootsvc.c` + `ocire/ocire_kexec.c`（监督者一侧）、
`ocire/agent/ocire_agent.c`（代理循环：睡 5 秒、快照、追加）、
`ocos/services/block/`（可信读写通道）。

**影响。**双层保护（杀不死的进程 + 写不了的二进制卷）、崩溃安全
的日志顺序（环节头最后更新，因此撕裂的追加最多丢失未提交的字节），
以及每次失败的诚实降级路径（`agent spawn FAILED - the recovery log channel stays unused`）。`sys_l1svc` 为 cat 18 划出的 4096 字节
arg 缓冲正是为代理的 4 KiB 追加而存在。

## 8. `/HANGAT`——从交付镜像注入引导失败

**机制。**OCBL（两种形态）在载荷卷上探测一个 1 字节文件
`/HANGAT`；若其内容为 `'1'..'7'`，OCBL 静默地向内核命令行追加
`oc.hang=N`。内核的 `core_boot_state.c` 解析它，并在写入阶段 N 后
有意停机（'7' = L1_READY 之后的运行时挂起）——看门狗处于布防
状态。

**触发。**仅此文件的存在，别无其他。没有它就是完全的空操作——
这正是要点：看门狗 -> 复位 -> OCBL 判定 -> OCIRE 路由这条链
（机制 3）可以**从交付镜像**直接演练，无需专门构建测试内核。

**代码。** `ocbl/common/ocbl_statemon.c`（探测）、
`ocbl/common/ocbl_main.c` + `ocbl/efi/efi_entry.c`（调用点）、
`ocos/kernel/core_boot_state.c`（停机）。

**影响。**整个恢复故事都可对发布 ISO 做回归测试
（`tools/wp07a_state_e2e.py`、`tools/wp12_watchdog_logging_e2e.py`
驱动它）。同一解析路径上的相关命令行开关：`oc.stmon=off` 关闭
整个状态监视器。

## 9. HMAC_DRBG——熵链

**机制。** `crypto_random()` 是完整的 NIST SP 800-90A HMAC_DRBG
（状态 `drbg_k[32]`/`drbg_v[32]`，按 §10.1.2 执行 `drbg_update`），
当 CPUID 提供时以 RDRAND 播种（每次 8 字节，检查进位标志），
否则以 RDTSC + 引导抖动重新混合；第 24-31 字节始终与
`rdtsc ^ (core_timer_ticks() << 1)` 异或。首次使用时实例化，按需
重播种，并由 crypto IRQ 窗口保护（单核的 `pushfq/cli/popfq` 一对，
不是锁——被记录为构建环境属性），因此并发 kthread 永远不会观察
到同一个生成块。

**触发。**每次 TLS 握手、每个 SSH 会话、内核需要随机性的每个
地方。

**代码。** `ocos/services/crypto/src/svc_crypto_core.c`（BUG-0106
取代了旧的定时器播种 xorshift32——按源码注释的说法，其输出让
“SSH 服务器与 TLS 客户端双方都形同透明”）、
`svc_crypto_irq_window.h`。

**影响。**密钥材料不再能被被动观察者推算；内嵌的根 CA 库
（`svc_crypto_roots.h`，由 `tools/embed_roots.py` 生成——ISRG
X1/X2、GlobalSign、GTS R1/R4、DigiCert、Baltimore、Amazon、
Microsoft、Sectigo E46/R46）使 TLS 链验证器自足。
见 [../internals/drbg-crypto.md](../internals/drbg-crypto.md)。

## 10. Health -> 看门狗联动——活着但告警的喂送

**机制。**health 服务每秒采样（`health_sample_t`：CPU 取自调度器
空闲增量，内存取自 PMM+堆，另有任务、磁盘、网络计数器），并评估
三种告警：内存使用 > 90%、活跃任务 > 64、CPU 连续 10 个样本 >=
90% 繁忙。告警去重（每次越限一条 ERROR klog）。独立地，这些告警
**喂给看门狗子系统的槽位 0（“system-heartbeat”）**作为额外喂送源。

**触发。**由 kmain 中 `health_init()` 注册的 1 秒采样器节拍。

**代码。** `ocos/services/health/src/svc_health_core.c`、
`ocos/services/crash/src/svc_crash_watchdog.c`（槽位 0，默认超时
15 秒，策略 PANIC）。

**影响。**告警但仍活着的系统持续喂狗；死掉的系统停止喂狗，WP-11
panic 路径（转储 + crashlog + 策略）随之触发，而非无声挂起。硬件
看门狗（ACPI WDAT / Intel TCO）会被探测，但**绝不在引导时布防**
——布防是显式动作（`watchdog hw start <sec>`，TCO 范围 2-63 秒），
且每当软硬件同时布防，硬件超时被设为 >= 软件超时的 2 倍。
见 [../internals/health.md](../internals/health.md)、
[../internals/watchdog.md](../internals/watchdog.md)。

## 11. 硬编码的驱动探测优先级——谁赢得总线

**机制。**服务侧的家族管理器按固定的名字顺序遍历收集来的
`.oc_drvs` 表：

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

**触发。**kmain 驱动阶段中的 `block_svc_probe_all()` /
`nic_svc_probe_all()` / `snd_svc_probe_all()`。

**代码。** `ocos/services/{block,net,audio}/src/svc_*_drivers.c`。

**影响。**块设备：BMDMA 先探测，使驱动器可以带 DMA 操作集注册；
PIO 遍随后跳过已被接手的驱动器（`blk_dev_set_ops()` 原地升级
PIO 驱动器）。NIC：**第一个注册到网卡的探测获胜**——内核只经
单一 NIC 路由；无线网卡随后经 `wl_nic_bridge_register()` 桥接
进来。声音：与 nic 不同，**所有**家族都会被探测——多卡共存。
顺序表是驱动优先级唯一的决定处；注册机制本身（机制 6）与顺序
无关。见 [driver-model.md](driver-model.md)。

## 12. 编译进驱动的 QEMU 勘误

**机制。**凡是驱动为了在 QEMU 下真正可用而不得不偏离硬件规范
字面之处，偏离及其证据都以 BUG 编号的行内注释嵌入——这是目录树
对正式勘误文档的替代。最大的两处：

- **AHCI 16 字节 PRDT 表项**
  （`ocos/drivers/storage/drv_storage_ahci.c`）：真实 AHCI 1.3.1
  规定 8 字节 PRDT 表项；QEMU 10.x 解析 16 字节表项（`u64 addr + u32 rsvd + u32 flags_size`，标志半字节在 bit 31），与
  `hw/ide/ahci.c populate_sglist()` 一致。驱动采用 QEMU 布局，
  外加每端口 4 KiB 的**弹跳缓冲**，按 <= 8 扇区分块以便单个 PRDT
  表项即可满足，并有 stop/clear/start 弹跳重试，因为 QEMU 会拒绝
  端口启动后的第一条命令。
- **SB16 8237 页寄存器**（`ocos/drivers/audio/drv_audio_sb16.c`）：
  ISA DMA 通道 5 位于*第二片* 8237，其页寄存器映射是打乱的——
  ch5 -> 0x8B、ch6 -> 0x89、ch7 -> 0x8A（BUG-0127：代码过去编程的
  是通道 6 的页寄存器，导致 >= 1 MiB 的缓冲从错误的页流出）；ch5
  的模式字节 0x49 单周期读（BUG-0128）。

**触发。**经受影响驱动的一切 I/O（任意 QEMU SATA 盘上的 AHCI；
SB16 回放）。

**代码。**上述两个文件，外加一整族类似注记：NVMe 寄存器映射与
管理操作码修复（WP-10a）、igb SRRCTL 1 KiB 单位（BUG-0333）、
EHCI link-type 标志位 bits 2:1（BUG-0175）、virtio-blk legacy
寄存器偏移、rtl8139 TSD OWN 语义、AHCI 首命令弹跳。

**影响。**驱动在项目唯一能实际测试的平台上（QEMU 机器）可用，
且每处偏离都可就地审计——每条注释都注明其对照裁决所依据的 QEMU
源文件或 Linux 版本。反面也如实记录：纯数据手册驱动（ixgbe、
rtl8169 家族、bcm57xx、virtio-snd）声明从未在真实硬件上演练，
其测试命令报告 SKIP 而非伪造输出。

## 加场——值得知道的小陷阱

- **`build_test_iso.py` 有意省略 `-boot-info-table`**：该 xorriso
  标志会把 El Torito 镜像的第 8..0x47 字节清零，毁掉 OCBL stage1
  的入口代码（实测发现为一次无声挂起）。
- **EFI 形态在入口处自行清零 .bss**：EDK2 的加载器不保证 raw 与
  virtual 之间的余量（实测捕获 `g_ndisks` = 29 亿）。
- **已死的 `user_process_kill(pid)` 墓碑**：被显式删除并附注释
  警告不得复活（三处不变量违反）；统一收割器
  `user_process_kill_by_tid` 是唯一被认可的击杀路径。
- PIC 与 MSI 两条路径上的**提前 EOI 策略**，使处理器内的上下文
  切换不会卡死中断控制器。
- **0xEE 影子技巧**：OCIRE 恢复盘的 MBR 携带单个 type-0xEE（GPT
  保护）表项却没有 GPT 头；`ocbl_part.c` 对其特判
  （`OCBL_PT_SHADOW`），其后的 16 MiB FAT12 卷既可被 GRUB 按块
  清单加载，又对常规 GPT 工具不可见。
