<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：OCIRE 嵌入守卫（ocembed）

OCOS 如何被嵌入到 OCIRE 恢复监督者之下：启动事实命令行、
磁盘邮箱状态机、OCIRE 卷上的日志环、受保护的守卫进程、
全盘写保护，以及经 kexec 注入的 ring-3 日志代理。实现：
`ocos/services/ocembed/` ——
`interface/svc_ocembed_{api,core}.h`、
`src/svc_ocembed_core.c`（599 行）；代理本身是
`ocire/agent/ocire_agent.c`（一个独立的 ABI 消费者）；共享
的启动状态契约是 `include/ocbl_state.h`。恢复内核整体另见
[../dev/ocire.md](../dev/ocire.md)。

## 1. "嵌入"是什么意思

OCOS 可以被 **OCIRE** —— 一个*独立的恢复监督者内核* ——
kexec。监督者先拥有机器，把启动事实放到内核命令行上 ——
`oc.log=<LBA>`、`oc.logsecs=<N>`、`oc.mb=<LBA>`、
`oc.wdt=ib700`、`oc.agent=1` —— 并把日志代理作为
multiboot2 模块挂载。在普通 GRUB 启动上没有任何事实存在，
服务保持休眠（`ocire: embed inactive (normal boot)` ——
正确行为，不是错误）。

OCOS 侧（`ocembed_boot_confirm()`，kmain 步骤 16d）闭合
回路：

1. **定位恢复磁盘**：在 `BLK_MAX_DEVICES` 中扫描覆盖
   `g_mb_lba` 且扇区读出 OCIREMB2 魔数的设备；
2. **先保护**：在任何信令之前
   `driver_block_set_protected(dev)`；
3. **信令**：邮箱 WAIT_BOOT_SIGNAL -> SIGNAL_OK
   （+boot_count）；
4. **解除 i6300esb** 看门狗的布防（监督者留下的）—— 必须
   在 60 秒监督窗口内完成（"我们才过了约 2 秒"）；
5. **孵化代理**：`user_process_create(img, len, "ocire_guard")` + 设置保护标志；
6. **布防全盘写保护**。

失败诚实：`ocire embed: NO block device covers the mailbox - signal/watchdog/agent skipped, protection NOT armed`；
`ocire embed: boot signal FAILED - see log`；
`ocire embed: i6300esb not found on the bus (mailbox-only signalling)`；
`ocire embed: agent spawn FAILED - the recovery log channel stays unused`；
`ocire embed: no log agent injected (the recovery log channel stays unused)`。

## 2. 邮箱（原文照录）

```c
#define OCIRE_MB_MAGIC        "OCIREMB2"
#define OCIRE_MB_BYTES        128
#define OCIRE_MB_STATE_WAIT_SIGNAL 1
#define OCIRE_MB_STATE_SIGNAL_OK   2

#define OCIRE_LOG_MAGIC       "OCIRELOG1"
#define OCIRE_LOG_HDR_BYTES   16

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

状态机：监督者把邮箱留在 `WAIT_SIGNAL`；OCOS **只**从该
状态写入 `SIGNAL_OK` —— 其他状态被诚实上报、绝不改写。
32 字节的 `agent_sha256` 钉住监督者所期望的代理镜像；
邮箱位于 OCIRE 卷上，保护布防后 OCOS 无法写入。

## 3. 日志环

布局（注释原文）："sector 0 is a 16-byte header (magic
'OCIRELOG1', u32 LE cursor = bytes used in the payload, u24
LE sequence = wrap counter); sectors 1..N-1 are the payload.
cap = log_sectors*512 - 512. On overflow the cursor wraps to
0 and the sequence bumps (the reader dumps [0, cursor))."

- **追加顺序是崩溃安全的**：头最后更新 —— 一次撕裂的
  追加至多丢掉尚未提交的字节，绝不破坏环结构。
- 敌意头（`w > cap`）会被钳位。
- op：`L1SVC_OCIRE_LOG_APPEND`（4）追加 `arg`（至多
  4096 字节 —— 专门为该通道存在的 sys_l1svc 缓冲区）；
  `L1SVC_OCIRE_KLOG_SNAPSHOT`（5）把内核日志环渲染进
  `out`。
- 锚点：`ocire: appended N bytes`、`ocire: append FAILED (ring io)`、`ocire: empty append`、`(kernel log ring empty)`。

## 4. 守卫进程与仅限守卫的通道

注入的代理被孵化为带 `ocire_guard` 标志的 ring-3进程
**`ocire_guard`**：

- **kill 被拒绝**：`sys_kill` 拒绝发往受守卫进程的每个
  信号；
- 其**二进制位于 OCIRE 卷上**，OCOS 碰不到它（见下面的
  全盘保护）；
- L1SVC cat 18 通道**仅限守卫**：`caller_is_guard()` 检查
  当前进程的标志；任何其他调用方在 `out` 中得到
  `ocire: refused (guard-only channel)` 外加一条 WARN
  klog（`L1SVC ocire op=%u refused: caller is not ocire_guard`）；
- op 4/5 是代理进入内核的**仅有的两扇门**，且编号是冻结
  的 ABI（`ocire/agent/ocire_agent.c` 针对这些编号编译 ——
  是 ABI，不是源码依赖）。

这就是日志代理的架构：OCIRE kexec OCOS，OCOS 把 OCIRE
的代理孵化为受保护的 ring-3 进程，代理再通过两个仅限
守卫的 op 把 OCOS 自己的内核日志快照送回 OCIRE 卷。

## 5. 全盘写保护

嵌入激活时（3.0.0-WP03 G4）：

- **经覆盖块设备进行的每一次写都被拒绝** —— 读保持
  开放；
- `driver_block_read/write_sectors_trusted` 是嵌入服务与
  分区子项的内核内部通道 —— "绝不可从 ring 3 调用"；
- 可观察效果：受监督启动下 `disk_rw_test hda` 按设计
  失败。

## 6. 服务声明与交互

```c
OC_SERVICE_DECL_L1("ocire", "ocire embed service (boot signal + agent channel + whole-disk protection + ring-3 surface)", NULL, NULL, ocembed_l1_dispatch);
```

已声明的类别名例外：类别字符串 **"ocire"**（冻结的 ABI
名）由 `services/ocembed/` 目录实现。

服务间边：ocembed -> block（可信 RW）、klog（快照）、
usermode（守卫标志）。启动事实解析很早运行
（`ocembed_early_init(cmdline)`，kmain 步骤 3b），因此
受监督启动的决策在服务注册表尚未汇报之前就已作出。

## 7. 验证

`tools/run_ocire_supervised_tests.py`（WP-03 G4）真实驱动
OCIRE 受监督启动：60 秒看门狗窗口、邮箱信令、代理孵化及
其日志通道。`tools/run_ocire_tests.py` /
`run_ocire_dual_tests.py` / `run_ocire_shell_tests.py` 覆盖
OCIRE 侧（独立启动/help/未知命令/退格/关机 x BIOS+UEFI、
双启动 + 分区隔离、恢复 shell 电池，含摘要损坏 + BPB 被
抹）。
