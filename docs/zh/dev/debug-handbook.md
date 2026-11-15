<!-- SPDX-License-Identifier: Apache-2.0 -->
# 调试手册

从一台 OCOS 机器上提取证据的完整实战指南：串口捕获、内核
日志环、崩溃转储与寄存器快照，以及 —— 最重要的一
切 —— 在一切看起来都不对劲却无明显错误时的方法论。配套
页面：[debug-interfaces.md](debug-interfaces.md) 盘点约 93
个测试桩程序；[qemu-testing.md](qemu-testing.md) 覆盖
QEMU 特有行为；[debugging.md](debugging.md) 是简短入口。

## 1. 串口提取

每一条启动行、panic 横幅与被捕获的 op 都镜像到 COM1
（115200 8N1，轮询）。串口是*权威*通道（VGA 尽力而为）。

**QEMU**：

```
make run-bios          # -display none -serial mon:stdio -snapshot
# or explicitly:
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 \
    -display none -serial mon:stdio          # interactive (monitor on Ctrl-A C)
qemu-system-x86_64 ... -serial file:boot.log # capture to a file
qemu-system-x86_64 ... -serial stdio -d int,cpu_reset -D qemu.log
```

pexpect 测试装置使用带 `encoding="latin-1"` 的
`-serial mon:stdio`，让每个字节都存活；提示符正则是
`(?:ush>|oc>)\s`（用 `OC_PROMPT` 覆盖）。

**真实硬件**：COM1 插针上接一个 USB-TTL 适配器。串口 tee
编码跟随 `serial_encoding` 设置（`utf8|gbk`，显示 op
17）—— 当你的终端是 GB2312 时代的控制台时设为 `gbk`。

## 2. 读取内核日志环

1024 项的环（`klog_grow()` 之前是 24 项引导环）无条件
记录一切；实时通道（串口 + 控制台）由实时级别（默认
INFO）把关。回放 op 对其展示内容应用活动过滤器。

行格式（环回放、`/var/log/kernel.log` 与实时通道中完全
一致）：

```
[   12.345] WARN  net: link up (e1000)
[SSS.mmm]    LEVEL module: message
```

- 时间戳 = 自 `klog_init` 以来的秒.毫秒
  （`core_timer_now_ms`；PIT 之前为 0.000），
- 级别是 DEBUG 0 / INFO 1 / WARN 2 / ERROR 3 / FATAL 4
  之一（在列中渲染为 `DEBUG/INFO/WARN /ERROR/FATAL`），
- 模块是 12 字符标签（`net`、`health`、`ocire`、
  `klogtest`、...）。

用法表（应用 `dmesg`、`logcat`、`logctl`）：

| 命令 | 语法 | 作用 |
|---|---|---|
| `dmesg` | `dmesg [N]` | 经活动过滤器回放环尾（默认 30，1..1024 行）；页脚 `-- <shown> lines (ring <count>/1024, dropped <d>) --` |
| `logcat` | `logcat -l <lvl> -m <mod> -k <key> -t <sec> -n <N> [-f]` | 过滤回放；`-l` debug/info/warn/error/fatal 或 0-4，`-m` 模块精确匹配，`-k` 关键字子串，`-t` 秒截断，`-n` 条数；`-f` 以 10 Hz 轮询实时跟踪（ENTER 退出） |
| `logctl status` | | 环计数、丢弃数、实时级别 + 逐级掩码、文件字节数/轮转信息、过滤器状态 |
| `logctl level [-1..4]` | | 设置实时级别（-1 静音实时通道；环仍记录） |
| `logctl enable\|disable <level>` | | 逐级发射开关 |
| `logctl clear` | | 重置环 |
| `logctl flush` | | 把未同步条目追加到 `/var/log/kernel.log` |
| `logctl rotate` | | kernel.log -> kernel.log.1（保留 3 份） |
| `logctl threshold <512..16777216>` | | 轮转阈值（默认 1048576） |
| `logctl export [path]` | | 转储到 `/var/log/kernel-export.log` |
| `logctl filter clear \| [level] [module] [keyword]` | | 设置/清除活动过滤器（'-' = 任意）；作用于实时通道与 dmesg/logcat 回放 |
| `logctl test` | | 每级一行共 5 行，模块 `klogtest` —— 过滤器验证工具 |

磁盘通道（`/var/log/kernel.log`，1 MiB 轮转 x3）是
`klog_on_mount()` 在首次 fs 挂载后安装的发射钩子；它追加
每条尚未同步的条目并自动轮转。IRQ 上下文的写方跳过 FILE
通道（推迟到下一个任务上下文写方/flush/定时器踢）。

## 3. 崩溃证据

### 3.1 panic 横幅（报告中应附上的内容）

```
*** KERNEL PANIC: <reason> [task=<name>] ***
kernel: Open Cube OS <version> (build 0x<hex>)
source: exception <n> (<name>) err=0x<hex>
<task line>
<register dump: 4x4 grid r15..rax + rip/cs/ss/rflags/rsp/int/err [+ cr2] + cr3>
<traceback: rbp chain + stack scan>
<policy: HALT or REBOOT>
```

你可能见到的变体：`[KERNEL STACK OVERFLOW: exhausted, task=<name>]`（栈溢出归因）；`core dump: skipped in #DF mode`（#DF 在分发器深度被最小化处理，仅一条静态
串口行 —— 无记录、无转储、无刷洗，见 KNOWN_ISSUES）；
`*** NESTED PANIC INSIDE PANIC - HALTING ***`；
`*** CONTAINED: <what> - isolated, kernel continues ***`
（某个 panic 处理程序认领了恢复）。

### 3.2 crashlog —— 32 槽位环

```
ush> crashlog          # each entry:
[i] <src-name> ring=N err=0x.. rip=0x.. rsp=0x.. [cr2=0x..] [@<rtc>s | @tick=N] [task=<name>] [pid=N]
ush> crashlog flush    # append all entries to /var/log/crash.log
```

来源名：异常名（`#PF Page Fault`、...）或合成来源 ——
PANIC 32 / WDT 33 / ASSERT 34 / CONTAINED 35 /
STACKOVF 36。为空时：`crashlog: no exceptions recorded`。

### 3.3 dumpinfo 与 OCDUMP01 镜像

`dumpinfo` 报告核心转储缓冲区状态。保存的
`/var/crash/core.dump` 以如下内容开头：

```c
char magic[8];            /* "OCDUMP01" */
u32 hdr_len;              /* sizeof(oc_dump_hdr_t) */
u32 image_len;            /* total bytes captured */
u64 build;                /* OC_KERNEL_BUILD */
char version[24];         /* OC_RELEASE_VERSION */
char reason[40]; u64 jiffies; u64 rtc_epoch;
u64 cr3; u64 pte_count; u32 tid, pid; u32 ring; char task[16];
u64 stack_base, stack_size, stack_rsp; u32 stack_copy_len; u32 regs_present;
```

其后是寄存器（22 个 u64）、任务块、4 KiB 栈窗口与页表
扫描。完全没有文件系统时，前 2048 字节以十六进制转储到
串口。`crashtest dump` 不触发 panic 地捕获一个 —— 在
真正需要之前用它验证你的捕获接线。

### 3.4 读取寄存器快照

转储的 22 个 u64 是 `arch_irq_frame_t`（r15..rax、
int_no、error_code、rip、cs、rflags、rsp、ss）。按异常
类别：

| 异常 | 要紧的字段 | 典型含义 |
|---|---|---|
| #PF (14) | `cr2` = 故障地址；error_code bit0 (P) | P=0：不存在（NULL/野指针，或限制之内 RSP 以下的栈增长）；P=1：保护违例 —— 绝不重映射，是真实 bug；U/S 位指示环；I/D 位 = 取指（NX） |
| #GP (13) | error_code 选择子索引 | 坏描述符/特权 —— 经典的 ring-3 I/O 尝试（IOPB 拒绝所有端口） |
| #DE (0)、#OF (4) | rax/操作数 | rip 处的除法/溢出 |
| #UD (6) | rip | 执行了垃圾 —— 查回溯的代码区间过滤 |
| #DF (8) | rsp 相对任务栈窗口 | rsp 位于/低于栈底 => 栈耗尽归因；#DF 只打印最小行 |
| #DB/NMI/#BP (1/2/3) | - | 在 IST2 上处理；ring-3 来源的 kill 在退出栈上运行 |
| 看门狗 (33) | 任务行 | 布防状态下心跳饿死 15 秒 |
| stackovf (36) | 任务名 | 命中 kthread 守卫页 |

`rip` 落在某任务的守卫页内或其栈基之下，就是栈溢出的
铁证；回溯打印 rbp 链加上经 `looks_like_code` 过滤的
栈扫描候选。

## 4. 静默失败方法论

当没有 panic、没有横幅、没有错误行时 —— 自上而下过这架
梯子。

### 4.1 健康告警（客观状态）

`health` 打印 cpu/内存/任务/磁盘/网络，外加 `alarms:`
行。这三个位每秒采样、去重（每次越限一条 ERROR，指标
恢复时重新布防）：

| 告警 | 触发 | 含义 |
|---|---|---|
| `MEMORY>90%` | `mem_used*100/mem_total > 90` | PMM+堆压力 —— 查 `mem`、`frag`、`heap` |
| `TASKS>64` | 活动任务 > 64 | 任务槽位泄漏 —— `ps`/`sched`，找未被收割的退出 |
| `CPU-LOCKED` | 连续 10 次采样忙碌 >= 90% | 自旋/钉死循环 —— `sched`、把嫌疑者 `nice` 降级 |

### 4.2 日志环（不是实时控制台）

即使实时级别或过滤器藏起了行，环也无条件记录：
`logctl level 0`（打开实时门），然后 `dmesg 200`。
`logctl test` 证明流水线本身在工作。若某个"什么都没做"
的子系统在环中连 FATAL 都没有，它可能采用的是诚实跳过
约定（4.5）而非记日志。

### 4.3 kcap 陷阱（消失的输出）

**本代码库中最常见的单一静默失败原因。** 当一个 L1SVC
捕获处于激活状态时，捕获钩子*就是*控制台钩子：字符收集
进调用方缓冲区，帧缓冲与串口 tee 两者都被压制。后果：

- 一个内部触发实时输出的被捕获 op（嵌套的实时会话、
  sleep 停驻、本应打到串口的 klog FATAL）看起来就像
  系统控制台失声。
- 历史上被现场抓住的案例：被捕获的 `power sleep` 在整个
  停驻期间装着捕获钩子，在 OCIRE 监督启动期间吞掉了
  每一条控制台行；`sshd` 不得不改为直连路由，好让
  `net_sshd_log` 实时到达控制台。
- 规则：停驻或占有实时会话的 op 直连路由
  （`out[0]=0; return fn(arg);`）；只有一次性文本渲染器
  以捕获方式运行。如果你在添加 op 而其输出"消失"，先查
  路由方式（[writing-services.md](writing-services.md)）。

### 4.4 返回值约定

- 系统调用成功返回 `>= 0`、出错返回负值（多数路径为
  `-1`）；`SYS_L1SVC` 对任何*已路由*调用都返回 0 ——
  操作自身的失败文本在 `out` 之内（见
  [syscall-reference.md](syscall-reference.md) 第
  1.5/4 节）。
- 驱动/块/nic op 成功返回 0（或正的字节数）、出错返回
  负值。
- 若包装程序打印 `<name>: svc_call_failed`，说明*类别
  或 op 没有路由成功* —— op 编号打错、纯内核服务，或
  （cat 18）非守卫调用方被拒绝：
  `ocire: refused (guard-only channel)`。

### 4.5 "诚实跳过"模式

代码树刻意区分 FAIL 与 SKIP。认出这些字符串，不要把它们
当 bug 追：

- `... => SKIPPED`（例如 `[sample_rate_test] no sound cards registered => SKIPPED`）—— 前置条件缺失，测试
  未运行。
- `honest FAIL`（例如 `oneshot: arm failed (honest FAIL)`）—— 操作确实被尝试并被拒绝。
- `: SKIP - see the dedicated halt session`（halt/reboot
  测试 —— 破坏性，刻意不观察活体停机）。
- `watchdog hw: none (no WDAT table, no Intel TCO LPC)`
  —— 探测诚实地报告缺席（默认 `pc` 机型没有 TCO；
  用 q35）。
- QEMU 下的 `wifi scan` 打印诚实的 "no radio" 输出。
- `ocire: embed inactive (normal boot)` —— cat 18 在
  GRUB 启动上休眠；这是正确行为。
- `DesignWare: not present on this platform (honest skip)`。

### 4.6 死锁 vs. 挂死

- 软看门狗（15 秒，槽位 0 "system-heartbeat"）只在
  *已布防且被饿死*时以来源 WDT panic；shell 行编辑器每
  次按键都踢它 —— 一台还能应答按键的机器在调度器层面
  是活的。
- `ps`/`sched`：卡在 TASK_BLOCKED 且无等待者的任务就是
  你的候选者；`syncstat` 显示原语操作计数器。
- 启动状态监视器的 `/HANGAT` 注入（见下）端到端演练
  刻意挂死路径。

## 5. 故障注入工具

### 5.1 crashtest —— 8 种模式

```
usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]
  panic   : kernel_panic() direct
  exc     : ring-0 #UD exception
  pf      : ring-0 unmapped-address write fault
  assert  : OC_ASSERT(0)
  wd      : forced watchdog timeout
  ovf     : kernel stack overflow (guard page)
  dump    : capture a core dump without panicking
  policy  : show/set the after-panic policy
```

- `exc` 执行 `__asm__ volatile(".byte 0x0F, 0x0B\n")`
  （#UD）。
- `pf` 向 0x700000000000 写入（PML4[224]，保证不存在 ——
  C 的 NULL 解引用行不通：UB 会折叠成 ud2 而且页 0 是
  恒等映射的）。
- `panic|exc|pf|assert|wd|ovf` 会真的让内核崩溃
  （v2.2.0 行为）；`policy`/`dump`/用法是安全的。未知
  变体：`crashtest: unknown variant 'x' (try: crashtest help)`。

在真实故障之前，用 `crashtest dump` + `dumpinfo` +
`crashlog` 证明你的证据链；用会崩溃的模式测试 panic
策略与 OCBL/OCIRE 继电器。

### 5.2 msitest —— MSI 注入

`msitest`（diag op 21）触发真实的 MSI/MSI-X 中断投递
（LAPIC 向量 0x30..0x6F）—— 用它独立于设备流量证明
MSI 路径（使能、向量注册、EOI）。

### 5.3 /HANGAT —— 启动阶段挂死注入器

内核命令行 `oc.hang=<stage>` 在写完指定启动状态阶段 +
布防看门狗*之后*停住启动，演练真实的 6300ESB 继电器链
（阶段监视器写入、心跳停止、硬件复位、OCBL 判定、记录
路由到 OCIRE）。`oc.stmon=off` 关闭整个状态监视器。
阶段名来自 `ocst_stage_name`：BOOT、KM_AIN_ENTERED、
SCHED_READY、DRIVERS_READY、SERVICES_READY、
L1_LAUNCHED、L1_READY。

### 5.4 看门狗测试注入器

`watchdog test kill|panic|log` 孵化一个 `wd-starve`
kthread（优先级 15），把自己注册为受监视 3 秒且从不
喂狗 —— "expect the policy in 3 s"（按注册的策略：
任务被 kill / 内核 panic / ERROR 日志）。

## 6. 宿主侧工具

- `build/opencube_base.elf` 是 `strip` 之前的符号载体
  （流水线以 `-g` 链接，然后 strip 最终 ELF）。
- gdb：`qemu ... -s -S`，然后 `target remote localhost:1234`、`break kmain`、`continue`。
- `-d int,cpu_reset,guest_errors -D qemu.log` 记录中断、
  复位与错误。
- `make test` 运行宿主单元套件（加密向量、tar、gzip、
  MB2 解析、CUBIC、block-512 门禁、...）—— 比启动循环
  的迭代快得多；若干套件在本地使用 ASAN。
- 可重现构建意味着两次复现的崩溃是相同字节 —— 多次
  尝试之间不存在海森堡式构建差异
  （`make reproducible-check`）。
