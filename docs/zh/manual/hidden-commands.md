<!-- SPDX-License-Identifier: Apache-2.0 -->
# 隐藏命令与测试桩 — 调试面

284 个已注册程序中约三分之一是**测试桩**：主体从
v2.2.0 shell 迁移而来，现在经由 L1SVC 桥接提供服务，或以平坦
ring-3 探针形式链接。它们都是普通的注册表命令 —— 在
`ush>` 提示符下输入名字并按 ENTER 即可；无需特殊运行器，
除非特别说明也无需 flag。`help` 中没有任何标记表明它们是测试：
它们与 `ls` 或 `ping` 一样出现在 A-Z 列表中。

全仓扫描统计出 **约 93 个测试味程序**：69 个 `*test*` 命名的
目录应用外加 24 个平坦单文件探针（边界是模糊的 ——
`hello`、`loop`、`badapp`、`p0leak` 是作业测试用作目标的平坦工具
探针）。本页逐一列出，按其演练的内容分组，附运行方法与健康
运行的输出。

> **安全性**：除标注项外，此处一切均为只读或自包含 **除**
> 标注项之外：`fs_mount_test`（格式化设备）、四个电源路径
> 探针（可能终结会话）、`crashtest`（故意让内核 panic ——
> 这就是测试本身）、`stackovf`/`badapp`/`sigsegv_test`（故意
> 自杀）。

## 它们如何接线

- **目录桩**（`ocos/l1apps/<name>/main.c`）是标准
  `l1svc_main()` 封装：argv 被连接成一条 256 字节行并发送给
  所属服务 op；服务运行真正的测试主体，打印文本经输出捕获
  返回。
- **平坦探针**（`ocos/l1apps/<name>.c|.asm`，标签 `l1`）是真实
  的 ring-3 程序，自己发起原始系统调用 —— 它们直接演练
  内核面并打印各自的判定。
- 判定行统一为 `PASS` / `FAIL` / `SKIPPED`（硬件缺失时为
  `(honest skip)` / `(honest FAIL)` —— 那是*正确*的结果，
  不是失败）。

---

## A 类 — 目录测试桩，按家族

### OTA / 更新管线（12）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `ab_partition_test` | A/B 标志文件协议与槽位检测 | `ab_partition_test` | 逐项 OK 行 + 最终 PASS |
| `checkupdate_test` | checkupdate 负面固定件（拒绝 `ftp://` 前缀、拒绝损坏 JSON、128 字节缓冲拒绝超长变更、缺失 URL） | `checkupdate_test` | 每个恶意固定件 `(correctly rejected) => PASS` |
| `config_test` | 配置后端（读/写/恢复、CRC32 损坏、截断、ustar 坏校验和拒绝） | `config_test` | PASS 块 |
| `update_check_test` | check 步骤单元测试 | `update_check_test` | PASS |
| `update_download_test` | download 步骤单元测试 | `update_download_test` | PASS |
| `update_install_test` | install 步骤单元测试 | `update_install_test` | PASS |
| `update_local_test` | 离线包安装（`--local` 路径） | `update_local_test` | PASS |
| `update_pkg_test` | 包解析（gzip/tar/sha256：损坏 CRC32、截断流、坏 ustar 校验和、错误 sha256 全部被拒） | `update_pkg_test` | 各拒绝 PASS |
| `update_rollback_test` | 回滚标志清除 | `update_rollback_test` | PASS；`set_boot(X) rejected` |
| `update_status_test` | status op | `update_status_test` | PASS |
| `update_verify_test` | SHA256 校验 | `update_verify_test` | PASS |
| `real_update_test` | 针对在线源的完整真实流程（check→download→verify→install） | `real_update_test` | PASS，或如实的已是最新 |

支撑工具：`tools/make_update_pkg.sh`（包构建器）和
`tools/fix1_checkupdate_e2e.py`（E2E）。

### 块 / 存储（8）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `ahci_test` | AHCI 端口 IDENTIFY + I/O | `ahci_test` | 每端口 PASS；无控制器 → 如实跳过 |
| `nvme_test` | NVMe admin/io 队列 | `nvme_test` | PASS |
| `ata_dma_test` | ATA DMA 传输 | `ata_dma_test` | PASS |
| `virtio_blk_test` | virtio-blk 环 I/O | `virtio_blk_test` | PASS |
| `disk_rw_test` | 裸扇区读/写往返 | `disk_rw_test <device>` | 数据比对 PASS |
| `partition_test` | MBR/GPT 扫描 | `partition_test <device>` | 表转储 + PASS |
| `fs_mount_test` | 格式化后挂载 — **destructive** | `fs_mount_test <device>` | 格式化该设备，然后挂载：PASS |
| `real_hw_test` | 真实硬件磁盘 + NIC 小节 | `real_hw_test` | 每设备 PASS |

### NIC 驱动（10）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `e1000e_test`, `igb_test`, `ixgbe_test`, `bcm57xx_test`, `rtl810x_test`, `rtl8125_test`, `rtl8139_test`, `rtl8168_test` (8) | 每驱动寄存器探针 + 环回 | `<name>_test` | PASS，或无卡时如实的 `init failed` |
| `nic_rw_test` | 经块无关 API 的 NIC 帧环回 | `nic_rw_test` | PASS；无卡时 `no NICs registered => SKIP` |
| `other_nic_test` | 尽力而为的家族（3c59x / nForce / Atheros AR81xx / Marvell） | `other_nic_test` | PASS 或如实跳过 |

### 音频（8）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `hda_test` / `ac97_test` / `sb16_test` / `es1370_test` / `virtio_snd_test` / `usb_audio_test` (6) | 每驱动播放自测 | 裸调用 | PASS；驱动缺失 → 如实跳过 |
| `audio_rw_test` | PCM 读/写环回 | `audio_rw_test` | PASS；`no sound cards registered => SKIPPED` |
| `sample_rate_test` | 采样率重编程精度 | `sample_rate_test` | PASS；非法速率 `(unsupported, correctly rejected) => PASS`；QEMU 的 hda 模型在 44.1 kHz 上死循环 —— 已报告为已知模型限制，真实硬件编程正常 |

### USB（7）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `usb_core_test` | 主机控制器核心（枚举步骤） | 裸调用 | PASS |
| `usb_hotplug_test` | 插入/拔出路径 | 裸调用 | PASS |
| `usb_hub_test` | hub 枚举 | 裸调用 | PASS |
| `usb_kbd_test` | HID 键盘 | 裸调用 | PASS |
| `usb_mouse_test` | HID 鼠标 | 裸调用 | PASS |
| `usb_serial_test` | CDC-serial | 裸调用 | PASS |
| `usb_storage_test` | MSC 读/写（`blk_read_sectors`，数据比对） | 裸调用 | PASS |

### 遗留端口 / 摄像头（6）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `uvctest` | UVC 采集（MJPEG） | 裸调用 | PASS |
| `scsitest` | SCSI/LSI 53C895A 扫描 | 裸调用 | PASS |
| `gameporttest` | 游戏端口摇杆 | 裸调用 | PASS 或 `no stick (timeout)` 如实跳过 |
| `paralleltest` | 并行端口 SPP 字节 | 裸调用 | PASS |
| `irtest` | IR 解码（NEC/RC5/RC6/SIRC，损坏帧拒绝） | 裸调用 | PASS |
| `touchtest` | 触摸板描述符解析 + TAP/SCROLL/PINCH/SWIPE 手势 | 裸调用 | 每手势 PASS |

### 电源路径（4）— 可能终结会话

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `poweroff_test` | 关机端口链 | 裸调用（向导先警告 + 确认） | 机器仍在运行则 `power-off NOT observed : FAIL`；在真实硬件上会话直接结束 |
| `suspend_test` | S3 挂起/恢复 | 裸调用 | 失败时 `S3 resume failed.`；恢复 = 成功 |
| `halt_test` | halt 路径 | 裸调用 | 未观察到活体 halt → FAIL/SKIP 判定 |
| `reboot_test` | 重启路径 | 裸调用 | 机器重启（会话结束） |

### 显示 / CJK（5）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `chinesetest` | CJK 字体 + 编码测试组 | 裸调用 | PASS（无 `FAILURES PRESENT`） |
| `font-test` | 字体渲染器 + 负面固定件（区/位/槽/前导/截断/代理项/超长/C1 拒绝） | 裸调用 | 每个恶意输入 `(rejected) => PASS` |
| `encoding-test` | GB2312↔UTF-8 编解码器 | 裸调用 | PASS |
| `ime-test` | IME 引擎 + 会话（词组、翻页、数字选择协议断言） | 裸调用 | PASS |
| `tui-test` | TUI 渲染/窗口/输入/IME + 崩溃防护（`panic -> -1000` 遏制） | 裸调用 | 判定块 PASS |

### 内核核心 / 诊断 / 网络 / 加密（17 + 12）

| 命令 | 测试内容 | 运行 | 健康输出 |
|---------|---------------|-----|----------------|
| `memtest` | 内存服务测试组 | 裸调用 | PASS |
| `heaptest` | 堆压力 + 回绕防护（`wrap size 0xFF...F1 was served` 必须不得被如实服务） | 裸调用 | PASS |
| `heapbounds` | 堆边界条件 | 裸调用 | PASS |
| `fstest` | VFS 套件，含 BUG 编号回归行（BUG-0178/0179/0180/0182/0184/0061） | 裸调用 | 每条 BUG 行 PASS |
| `vmtest` | VMM 单元测试 | 裸调用 | PASS |
| `vmkernelpt` | 内核页表不变量 | 裸调用 | PASS |
| `vmmap` | 地址空间映射遍历 | 裸调用 | 转储 + PASS |
| `cr3test` | CR3 审计 | 裸调用 | PASS |
| `pftest` | 缺页处理计数器 | 裸调用 | PASS |
| `pmmrace` | PMM 分配竞态检测器 | 裸调用 | PASS（无 `FAIL (race)`） |
| `irqabitest` | IRQ 亲和性 | 裸调用 | PASS |
| `msitest` | MSI 中断 — **真实注入**，见下文 | 裸调用 | `msitest: PASS` |
| `timer` | 定时器节拍 | 裸调用 | PASS |
| `synctest` | 同步原语 | 裸调用 | PASS |
| `multi` | 多核唤醒（3 个交错任务） | 裸调用 | PASS |
| `spawn` | 后台孵化 | 裸调用 | PASS |
| `exc` | 异常报告状态（#UD/#PF/#BP 计数器） | 裸调用 | 计数器符合预期 |
| `crashtest` | 崩溃路径注入 — 8 种模式 | `crashtest <mode>` | 见下表 |
| `dumpinfo` | 核心转储缓冲状态 | 裸调用 | 大小 + 使用量 |
| `cryptotest` | SHA-256 / AES-128 / HMAC-SHA256/384 + 堆金丝雀 | 裸调用 | 全部 PASS，不得出现 `DIFFERENT (memory corruption!)` |
| `dhtest` | Diffie-Hellman | 裸调用 | PASS |
| `l1test` | L1 作业/IPC/动态链接 | 裸调用 | PASS |
| `nf_test` | netfilter 规则（input drop、命中、移除后 ping 恢复） | 裸调用 | PASS |
| `tcpcc_test` | TCP 拥塞控制（CUBIC） | 裸调用 | PASS |
| `tcptest` | TCP 环回 | 裸调用 | PASS |
| `dnstest` | DNS 解析器测试组 + 内建向量 | 裸调用 | PASS |
| `acpitest` | ACPI 解析器（固定件表、`\_S5` 求值、活动 DSDT） | 裸调用 | PASS |
| `i2ctest` | I2C 控制器（i801/PIIX4、DesignWare） | 裸调用 | PASS 或如实的 `not present` |
| `smbustest` | SMBus + NACK 视为错误检查 | 裸调用 | PASS |
| `touchtest` | （见上文遗留端口） | | |

### 自校验的 help 门（3）

它们解析**活动注册表文本本身** —— 兼作注册表回归门
（如果某命令被意外从构建中遗漏，它们会在运行时失败，
而不只是 CI 中失败）：

| 命令 | 断言 |
|---------|---------|
| `help_default_test` | 默认列表的计数与形态 |
| `help_a_test` | `help -a` 输出严格 A-Z 有序 |
| `help_wp_test` | `help -w` 将每条目归入工作包头之下 |

### live-fix 探针（1）

| 命令 | 功能 | 运行 |
|---------|--------------|-----|
| `p2test` | 三个定向探针之一，由 `tools/wp_p2_batch_a_test.py` 经 QEMU 串口控制台驱动 | `p2test utf8` 将 174 个汉字（522 字节，一次系统调用）推过两条写路径；`p2test read0` 检查 POSIX `read(0,buf,0)==0`；`p2test pipe` 连击管道槽复用 100 轮。失败串：`UTF8-FAIL short write`、`READ0-FAIL`、`PIPE-FAIL round=<n>` |

---

## msitest — MSI 真实注入（op 21，diag 服务）

`msitest` 是唯一会在**你的真实硬件上触发真实中断**的桩。
三个步骤，按序执行（`svc_diag_l1.c`）：

1. **LAPIC 存在性 + self-IPI** — 在向量 0x50 上注册处理程序，
   经确切的消息写路径发送 self-IPI，短暂自旋，
   然后比较向量计数器：`self-IPI vector 0x50: fired N time(s) PASS`。
2. **MSI 能力设备普查** — 遍历 PCIe 设备列表统计
   `cap_msi || cap_msix`：`MSI-capable devices: N of M`。
3. **真实触发** — 找到第一块 e1000，在向量 0x51 上武装 MSI，
   启用设备，然后向 Interrupt Cause
   Set 寄存器（BAR0 + 0x00C8）写入 `0xFFFFFFFF` 以一次性强制
   **所有**中断原因。必须落下一次真正的 MSI 消息写：`e1000 MSI fire (ICS->MSI): fired N time(s) PASS`。随后解除 MSI 并
   注销向量。无 e1000 或无 MSI 能力/BAR0 →
   `SKIP (reported honestly)`。

最终判定：`msitest: PASS` / `msitest: FAIL`。若步骤 3 失败而
步骤 1-2 通过，则是 MSI 路由（而非 LAPIC）损坏 —— 先查
`pcibars` 的 BAR0 映射与 `pcie` 的链路状态。

---

## B 类 — 平坦 ring-3 探针（24）

`ocos/l1apps/` 下的单文件程序，直接演练内核面。
全部已注册；全部裸运行。（其余 `l1`
标签的程序 —— `ush`、`ld_so`、`ldd`、`l1test` —— 是 shell、
动态加载器本身、其列表器、以及作业/IPC 自测，
不是探针；`l1test` 已列在上文诊断家族中。）

### 内核面探针（17）

| 命令 | 探测内容 | 预期 |
|---------|----------------|----------|
| `fork_test` | fork/wait4 往返 | 子进程退出被收割，PASS |
| `exec_test` | execve 替换镜像 | 出现被 exec 程序的输出 |
| `fdref_test` | dup/fork 间的 fd 引用计数 | close 语义正确 |
| `pipe_test` | 管道写/读 | 字节往返 |
| `select_test` | 对就绪 fd 的 select | 就绪判定正确 |
| `select_zero_test` | 零 fd 的 select | 立即返回，不挂起 |
| `signal_test` | 信号投递 + 处理程序 | 处理程序运行，进程继续 |
| `sigsegv_test` | SIGSEGV 遏制 | `*** SIGSEGV delivered to pid=N ... kernel continues ***`，进程被杀或处理程序运行 |
| `sse_test` | 切换间的 SSE 上下文 | FP 值保持不变 |
| `stackovf` | 故意的用户栈溢出 | 进程被杀，内核继续 |
| `int3_user` | ring-3 #BP | 被遏制，内核继续 |
| `priv_test` | ring 3 中的特权指令 | #GP 投递给进程，内核继续 |
| `pf_test` | ring-3 缺页 | 被遏制 |
| `p3_test` | 优先级调度 | 顺序符合优先级 |
| `mmap_test` | mmap 基础 | 已映射、可读、已解除映射 |
| `mmap_multi` | 同时多重映射 | 全部相互独立 |
| `mprotect_test` | mprotect W^X 强制 | 写只读页触发缺页 |

### 进程组 / 动态链接探针（7 + 固定件）

| 命令 | 探测内容 | 预期 |
|---------|----------------|----------|
| `g6_test` | G6 里程碑链（fork/exec/kill/oom），异步汇总 | `failed tests = 0` |
| `g6_exec_a` / `g6_exec_b` | g6_test 驱动的两个 exec 半部 | 两半的输出 |
| `g6_oom` | OOM 路径（地址空间清理） | 进程干净死亡，内核存活 |
| `g7mmap` | G7 mmap 里程碑（BUG-0281） | PASS |
| `pie_test` | PIE 程序加载 | `loaded at 0x...` |
| `reloc_test` | 重定位已施加 | `foo_add=30 foo_global=42 fp=300` |
| `so_test` / `dlsym_test` / `dyn_hello` / `main_dyn` | solib 调用、dlsym、动态 hello、动态 main | `foo_add(2,3)=5`、`foo_add(7,8)=15`、`hello from dynamic program` |
| `hello` / `loop` / `badapp` / `p0leak` | 固定件：hello world；空闲循环（作业目标）；故意行为不端者；用户指针围栏（`all kernel-address probes fenced`） | 如所述 |

---

## crashtest — 8 种故意失败模式

`crashtest <mode>` 注入一次特定崩溃，以便你观察完整的
WP-11 恢复路径（转储 + crashlog + klog 刷写 + 策略）。可以通过
裸命令或 `sysdiag` classic/direct 分发触达 —— **刻意不设为
向导按钮**（"为故意 panic 注入设一个向导按钮是个坑"）。

| 模式 | 行为 | 应当看到 |
|------|--------------|---------------------|
| `panic` | 直接调用 `kernel_panic()` | `*** KERNEL PANIC: manual crashtest: kernel_panic() ***` + 寄存器 + 回溯 + 策略行 |
| `exc` | 执行一条 ring-0 #UD | 异常横幅，随后 panic 路径；`crashtest: #UD returned unexpectedly` 表示故障未被触发 —— 本身即失败 |
| `pf` | 在 ring 0 写未映射地址 | `ring-0 exception 14 (#PF Page Fault)` panic |
| `assert` | `OC_ASSERT(0)` | panic 原因 `assertion failed: 0` |
| `wd` | 强制软件 watchdog 超时（武装 + 心跳回拨） | `watchdog timeout: ...` panic |
| `ovf` | 孵化一个逐帧增长 4 KiB 直至守卫页的 kthread | `KERNEL STACK OVERFLOW (guard page hit, task=...)` panic |
| `dump` | `core_dump_save("manual crashtest dump")` — 不 panic | 转储已写入；启动期缓冲缺失时 `crashtest: dump FAILED (no boot-time buffer)` |
| `policy halt\|reboot` | 设置 panic 后动作 | `crashtest: policy set - after the next panic the kernel will HALT` |

未知变体打印 `crashtest: unknown variant '<v>' (try: crashtest help)`。任意 panic 模式之后，验证记录已落盘：`crashlog`
（32 槽环形）、`dumpinfo`（核心转储缓冲）、`dmesg`（klog FATAL
行），以及文件系统上的 `/var/log/crash.log` + `/var/crash/core.dump`。

## 从脚本运行桩

每个桩都是普通命令，因此通常的 shell 特性都适用：

```
ush> memtest > /tmp/mem.txt
ush> run dnstest
ush> sysdiag direct memtest        # the mega-app verbatim door
ush> logcat -k msitest -f          # follow only msitest's klog lines
```

E2E 套件在串口控制台上做的正是这些（见
[../dev/e2e-suites.md](../dev/e2e-suites.md)）；逐应用记录
（`tools/run_l1_per_app.py`）为每个程序启动一次 ISO 并通过
`run <name>` 运行它。

## 相关页面

- [function-dictionary.md](function-dictionary.md) — 全部 296 条命令
- [../troubleshooting/error-codes.md](../troubleshooting/error-codes.md) —
  桩输出中错误代码的含义
- [../troubleshooting/silent-failures.md](../troubleshooting/silent-failures.md) —
  为何桩可能"通过"而功能已损坏（honest-skip 与 FAIL 之辨）
