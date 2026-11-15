<!-- SPDX-License-Identifier: Apache-2.0 -->
# 错误代码 — 完整的数值面

Open Cube OS **没有全局 `errno`** 也没有 `errno.h` —
freestanding 内核不链接 libc。通用约定是：

- **0 = 成功，负数 = 失败**（`-1` 为通用；`Returns 0 on success, -1 on failure` 是文档化习语）。
- 需要精度的子系统使用**稠密负数枚举** — 8 个家族，
  **共 61 个代码**，下文逐一列出其触发场景与应对。
- 尺寸/长度返回 `-1`；环形缓冲读取越过窗口返回 `-1`；
  watchdog 槽位 API 返回 **-1（表满）/ -2（参数错误）**。
- 唯一的正数哨兵家族是崩溃来源伪向量
  `OC_CRASH_SRC_*` (32..36)，它们搭载在 crashlog
  记录的 `vector` 字段上。

本页涉及的命令：[dmesg](../manual/commands/dmesg.md)、
[logcat](../manual/commands/logcat.md)、[logctl](../manual/commands/logctl.md)、
[crashlog](../manual/commands/crashlog.md)、[health](../manual/commands/health.md)、
[update](../manual/commands/update.md)、[config](../manual/commands/config.md)。

---

## 家族 1 — OC_UPDATE_E_*（15 个代码，更新管线）

定义于 `services/selfhost/interface/svc_selfhost_update.h`；在
`svc_selfhost_ab.c` / `svc_selfhost_update.c` 中解码为文本。这些是
每条 `update: ... failed: <why>` 行背后的代码。

| 代码 | 值 | 含义 | 触发场景 | 应对 |
|------|-------|---------|------------------|----------|
| `OC_UPDATE_E_CONFIG` | -1 | 配置文件缺失或不可读 | /etc 卷上没有 `/etc/opencube.conf` | 检查 `config path`；/etc 卷是否已挂载？见 [storage-issues.md](storage-issues.md) |
| `OC_UPDATE_E_PREFIX` | -2 | URL 前缀非法（必须为 http:// 或 https://） | `update_url` 拼写错误（`ftp://...`） | `config get update_url`，用 `config set update_url http://...` 修正 |
| `OC_UPDATE_E_DNS` | -3 | DNS 解析失败 | 更新主机无法解析 | `dns <host>`；从 `dhcp`/`ip` 输出检查 DNS 服务器 |
| `OC_UPDATE_E_CONNECT` | -4 | 连接失败 | 服务器不可达（路由/防火墙） | `ping` 该主机，`route` 查看网关，`firewall` 列表 |
| `OC_UPDATE_E_TIMEOUT` | -5 | 无响应（超时） | 服务器未接受任何内容 | 重试；检查主机可达性 |
| `OC_UPDATE_E_HTTP` | -6 | HTTP 错误 | 服务器返回错误状态 | 检查服务器端 / URL 路径 |
| `OC_UPDATE_E_JSON` | -7 | JSON 解析失败 | update.json 格式错误 | 用 `wget` 手动获取并阅读 |
| `OC_UPDATE_E_BUFSIZE` | -8 | 响应过大 | changes 列表溢出 128 字节缓冲 | 服务端修复（缩短 changes） |
| `OC_UPDATE_E_DISABLED` | -9 | 配置中 online_update=no | 更新被配置禁用 | `config set online_update yes` |
| `OC_UPDATE_E_NOAB` | -10 | 无 A/B 磁盘 | 未从 A/B 布局启动 | `update status`，然后创建：`abdisk <dev>`（destructive） |
| `OC_UPDATE_E_SHA` | -11 | SHA256 校验失败 | 包损坏或被篡改 | 重新下载；检查 update.json 中的 `package_sha256` |
| `OC_UPDATE_E_TARGZ` | -12 | 包不是有效的 gzip/tar | 下载被截断 | 重新下载；核对大小 |
| `OC_UPDATE_E_IO` | -13 | 文件系统 I/O 错误 | /data 写入失败 | `df`，对卷执行 `fsck` |
| `OC_UPDATE_E_SLOT` | -14 | 槽位非法（应为 A 或 B） | 内部槽位运算失败 | 运行 `abcfg`；若持续出现则报告 |
| `OC_UPDATE_E_ARGS` | -15 | NULL / 畸形参数 | 内部调用方 bug | 附确切命令行报告 |

## 家族 2 — OC_GZIP_ERR_*（13 个代码含 0，gunzip）

`kernel/core_lib_gzip.h`（`ocse/kernel/lib/lib_gzip.h` 中有相同副本）。
由 OTA 包路径和每个 gzipped 注册表程序在 exec 时暴露
（应用解压）。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `OC_GZIP_ERR_NONE` | 0 | 成功 | — | — |
| `OC_GZIP_ERR_ARGS` | -1 | NULL 指针 / 容量错误 | API 误用 | 内部问题；报告 |
| `OC_GZIP_ERR_MAGIC` | -2 | 不是 gzip 流（1f 8b） | 文件不是 gzip | 重新下载 / 重新压缩该包 |
| `OC_GZIP_ERR_METHOD` | -3 | CM != 8（deflate） | 非 deflate 的 gzip | 重建该包（`tools/make_update_pkg.sh`） |
| `OC_GZIP_ERR_HEADER` | -4 | gzip 头畸形 | 头部被截断 | 重新下载 |
| `OC_GZIP_ERR_BLOCK` | -5 | 块头 / 长度检查非法 | 流中段损坏 | 重新下载；先核对 SHA256 |
| `OC_GZIP_ERR_TABLE` | -6 | Huffman 表非法 | deflate 表损坏 | 同上 |
| `OC_GZIP_ERR_DIST` | -7 | 距离超出窗口 | 回引损坏 | 同上 |
| `OC_GZIP_ERR_TRUNC` | -8 | 流在最终块前结束 | 尾部被截断 | 同上 |
| `OC_GZIP_ERR_CRC` | -9 | 尾部 CRC32 不匹配 | 静默损坏 | 重新下载（这正是先检查 OC_UPDATE_E_SHA 的原因） |
| `OC_GZIP_ERR_ISIZE` | -10 | 尾部 ISIZE 不匹配 | 大小字段错误 | 同上 |
| `OC_GZIP_ERR_SINK` | -11 | sink 回调失败 | 输出设备出错 | 检查磁盘空间 |
| `OC_GZIP_ERR_STATE` | -12 | API 误用（finish 后继续 feed 等） | 调用方 bug | 内部问题；报告 |

## 家族 3 — OC_TAR_ERR_*（7 个代码，ustar）

`kernel/core_lib_tar.h`（+ ocse 孪生）。包安装的解包一半。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `OC_TAR_ERR_ARGS` | -1 | NULL / 参数错误 | API 误用 | 内部问题 |
| `OC_TAR_ERR_HEADER` | -2 | 头块畸形 / 过短 | 归档损坏 | 重建该包 |
| `OC_TAR_ERR_CSUM` | -3 | 头校验和不匹配 | 头损坏 | 重建；固定件中的 `ustar bad checksum rejected` |
| `OC_TAR_ERR_SIZE` | -4 | 文件大小字段非法 | 非八进制大小字段 | 重建 |
| `OC_TAR_ERR_SINK` | -5 | 文件回调失败 | 目标写入错误 | 磁盘满？`df` |
| `OC_TAR_ERR_STATE` | -6 | API 误用 | 调用方 bug | 内部问题 |
| `OC_TAR_ERR_TRUNC` | -7 | 归档在条目中途结束 | 包被截断 | 重新下载 |

## 家族 4 — OC_CONFIG_E_*（5 个代码，/etc/opencube.conf）

`kernel/core_lib_config.h`（+ ocse 孪生）。位于 `config get/set` 与
配置后端之后。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `OC_CONFIG_E_ARGS` | -1 | NULL / 参数错误 | API 误用 | 内部问题 |
| `OC_CONFIG_E_NOFILE` | -2 | /etc/opencube.conf 缺失或不可读 | 无 /etc 卷，或首次启动 | `config path`；挂载 /etc；`config restore` 重建默认值 |
| `OC_CONFIG_E_NOKEY` | -3 | 键未找到（或值为空） | 键拼写错误 | `config list` 显示有效键 |
| `OC_CONFIG_E_TOOLONG` | -4 | 值放不进输出缓冲 | 值过长 | 缩短该值 |
| `OC_CONFIG_E_IO` | -5 | VFS 读/写错误 | 卷错误 | `fsck`，重新挂载 |

## 家族 5 — ENC_E_*（5 个代码，GB2312/UTF-8 转换）

`services/encoding/interface/svc_encoding_core.h`（+ ocse 孪生）。
由 CJK 控制台层和 `serial_encoding` 使用。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `ENC_E_INVAL` | -2 | NULL 指针 / 零容量 | API 误用 | 内部问题 |
| `ENC_E_TRUNC` | -3 | 输入在序列中途结束 | 多字节输入被截断 | 检查发送方编码（`serial_encoding`） |
| `ENC_E_ILLEGAL` | -4 | 字节序列在该字符集中非法 | GBK 字节被按 UTF-8 读取或反之 | `serial_encoding utf8\|gbk` 与对端匹配 |
| `ENC_E_NOSPACE` | -5 | 输出缓冲过小 | 超大转换 | 内部问题 |
| `ENC_E_UNMAP` | -6 | 码点在目标中无映射 | 无 GB2312 映射的罕用汉字 | 无法避免；字体层会回退 |

## 家族 6 — X509_E_*（8 个代码 + OK，证书校验）

`services/crypto/interface/svc_crypto_x509.h`；在
`svc_crypto_x509.c` 中解码。它们决定 HTTPS 连接的生死。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `X509_OK` | 0 | 已校验 | 链锚定于内嵌根存储（ISRG Root X1 等） | — |
| `X509_E_FORMAT` | -1 | 证书格式错误 | DER 畸形 | 服务端修复 |
| `X509_E_UNSUPPORTED` | -2 | 不支持的算法 | 非 RSA 链 | 服务端修复 |
| `X509_E_EXPIRED` | -3 | 证书已过期 | 服务器证书过期 | `rtc`（客户机时钟正常吗？）；然后服务端处理 |
| `X509_E_NOT_YET` | -4 | 证书尚未生效 | 时钟在过去 | `rtc` |
| `X509_E_NO_ROOT` | -5 | 签发者证书缺失 | 自签名或无锚链 | 默认拒绝（安全模型）；仅测试时可经 `config set tls.insecure 1` 显式选择加入 |
| `X509_E_BADSIG` | -6 | 签名校验失败 | 证书被篡改 | 视为敌意；不要绕过 |
| `X509_E_HOSTNAME` | -7 | 主机名不匹配 | 证书属于另一名字 | 修正 URL 或证书 |
| `X509_E_NO_ISSUER` | -8 | （保留/未知） | 解码表回退打印 `unknown error` | 报告 |

## 家族 7 — OC_USB_E*（7 个代码，USB 传输）

`drivers/usb/drv_usb_core.h`（+ ocse 孪生）。每次 USB 传输都返回
其中之一。

| 代码 | 值 | 含义 | 触发 | 应对 |
|------|-------|---------|---------|----------|
| `OC_USB_ETIMEDOUT` | -100 | 传输超时 | 设备停顿 | 重新插拔；`usb` 重新枚举 |
| `OC_USB_ENODEV` | -101 | 无设备 | 设备已拔出 | 重新插拔；`usb_hotplug_test` 复现拔出路径 |
| `OC_USB_ESTALL` | -102 | 端点停顿 | 设备拒绝请求 | 设备侧问题；`usb_storage_test` 显示 CSW 状态 |
| `OC_USB_EIO` | -103 | I/O 错误 | 总线错误 | 检查控制器（`usb`、`lspci`） |
| `OC_USB_ENOMEM` | -104 | 内存不足 | 分配失败 | `mem`/`free`；报告 |
| `OC_USB_EINVAL` | -105 | 参数非法 | API 误用 | 内部问题 |
| `OC_USB_ENAK` | -106 | NAK（尚未就绪） | 设备忙 | 重试 |

## 家族 8 — OC_POWER_E_UNSUPPORTED（1 个代码）

`services/power/interface/svc_power_core.h`（+ ocse 孪生）：`-1` —
"走完所有端口后机器仍在运行"。当关机遍历尝试了每个端口
（ACPI S5、端口 0x64、CF9）而机器仍活着时返回；用户看到
`Power off not supported. System halted.` — 在 VM 中请使用
宿主机的电源控制。

### 解码文本表（字符串所在处）

每个家族在调用点旁都有孪生的解码到文本表 ——
天然的 grep 锚点：`svc_selfhost_ab.c`（update）、
`svc_crypto_x509.c`（x509）、fs/block/net 服务中的 `put()`
字符串。SMBus i801 状态字解码为 `NACK/device error`、
`bus error/collision`、`transaction failed`、`timeout`。

---

## 11 个 kernel_panic 调用点

panic 原因是**拼装**的而非字面量（在栈缓冲上用
strcpy/strcat/u64_to_str 构造），因此要 grep 常量
前缀，而不是整串。

| # | 位置 | 原因串（格式） | 触发 |
|---|------|------------------------|---------|
| 1 | `arch_exceptions.c:203` | 透传 `<reason>` | 崩溃服务尚未注册时使用的架构回退 — 打印 `KERNEL PANIC (no crash handler installed): <reason>` + `System halted.` |
| 2 | `core_sched.c:658` | `kernel stack bounds violated on switch (task=<name>, rsp=0x<hex> window=[0x<lo>,0x<hi>])` | 调度器即将切换到的任务其保存的 RSP 位于其栈窗口之外（损坏/溢出）；它拒绝在陌生内存上恢复 |
| 3 | `svc_crash_core.c:563` | `watchdog timeout: no heartbeat for <N>s (task=<name>)` | 软件崩溃 watchdog 在 OC_WDT_TIMEOUT_MS 内未见心跳（默认 15 s；仅在启动后武装，节拍已注册但默认 DISARMED 以避免长 shell 命令期间的误 panic） |
| 4 | `svc_crash_watchdog.c:451` | `watchdog timeout: <N>s without a feed (task=<name>)` | 策略为 PANIC 的已注册 watchdog 槽位超出预算（如 `watchdog test panic`） |
| 5 | `svc_diag_l1.c:261` | `manual crashtest: kernel_panic()` | `crashtest panic` 命令 |
| 6 | `ocse/core_watchdog.c:450` | （#4 的孪生） | OCSE 安装器内核 watchdog |
| 7 | `ocse/core_sched.c:613` | （#2 的孪生） | OCSE 调度器 |
| 8 | `ocse/core_crash.c:567` | `watchdog timeout: no shell heartbeat for <N>s` | OCSE 软 watchdog |
| 9 | `arch_exceptions.c:438+` (ocos) | `KERNEL STACK OVERFLOW (guard page hit, task=<name>)` | CR2 落在 kthread 守卫页内的 #PF |
| 10 | （孪生） | `KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)` | RSP 位于任务栈基之下或 ≥ 0x40000000000 的 #DF |
| 11 | （两棵树） | `ring-0 exception <N> (<#XX Name>)` | 任何其他未处理的 ring-0 异常 |

仅横幅式停机（无 panic 记录）：`PANIC: idle task stack allocation failed - halting`（调度器初始化 OOM — 两个内核皆有）。

### 统一 panic 横幅（打印内容，按序）

在串口（权威）+ VGA（尽力而为）上：

1. `*** NESTED PANIC INSIDE PANIC - HALTING ***` — 一行，然后停机
   （绝不递归）。
2. #DF 特例：`*** KERNEL PANIC: <reason> [task=<name>] ***` +
   `source: #DF Double Fault err=0x<hex> rip=0x<hex> handled at dispatcher depth]` + `core dump: skipped in #DF mode (see KNOWN_ISSUES)` + 策略行。
3. CR3 规范化；klog 环形 FATAL 记录；crashlog 记录（无文本）。
4. `*** KERNEL PANIC: <reason> ***`（+ ` [KERNEL STACK OVERFLOW: exhausted, task=<name>]` 注记）+ 内核版本/构建 + 来源
   行（`manual/panic|watchdog-timeout|assertion-failed|contained(TUI)| kstack-overflow|exception <N>`）+ 任务 + 寄存器 + 回溯。
5. VGA 单行（原因截断到 90 字符）+
   `Registers and call trace on the serial console.`
6. 核心转储状态：`core dump: UNAVAILABLE (no boot-time buffer)` 或
   `<N> bytes captured, persisted to /var/crash/core.dump`（或串口
   回退 / `persist FAILED`）。
7. L1 panic 处理链：`*** panic handler claimed recovery - resuming ***`（罕见 — TUI 崩溃防护是唯一用户）。
8. 日志刷写状态：`crash log: <n> bytes to /var/log/crash.log; klog flushed <n> bytes to /var/log/kernel.log`（或 FAILED 变体）。
9. 策略：`policy: HALT|REBOOT - system stopped (safe halt loop)| - rebooting NOW`。

### 崩溃来源伪名（crashlog `vector` 字段 32..36）

`manual/panic`、`watchdog-timeout`、`assertion-failed`、
`contained(TUI)`、`kstack-overflow`、回退 `exception`。记录中的
x86 向量名：`#DE Divide Error`、`#DB Debug`、`#BP Breakpoint`、`#OF Overflow`、`#BR Bound Range Exceeded`、`#UD Invalid Opcode`、`#NM Device Not Available`、`#DF Double Fault`、`#TS Invalid TSS`、`#NP Segment Not Present`、`#SS Stack-Segment Fault`、`#GP General Protection`、`#PF Page Fault`、`#MF x87 FPU Error`、`#AC Alignment Check`、`#MC Machine Check`、`#XM SIMD FP Exception`、`#SX Security Exception`、回退 `Unknown`。

---

## 30 个 `_Static_assert` 防护

全部是编译期布局契约 — 漂移**令构建失败**，绝不会
在运行时发生。如果你撞上一个，说明跨模块 ABI 的两侧
不再一致：

| 防护 | 位置 | 契约 |
|-------|-------|----------|
| `ocst_u32 must be 4 bytes` / `ocst_state_t layout drifted` / `ocst_record_t layout drifted` | `include/ocbl_state.h` | OCBL↔OCOS↔OCIRE 跨引导邮箱（0x98000/0x9B000） |
| `regs struct layout drift` | `ocbl/bios/ocbl_bios.h` | BIOS 寄存器帧 |
| `ehdr layout` / `phdr layout` | `ocbl/common/ocbl_load.c` | OCBL 解析的 ELF 头 |
| `task_t.fpu_state offset must match OFF_FPU in arch_context_switch.S`（+ `fpu_saved`、`rflags` 孪生） | `ocos/core_sched.c` **与** `ocse/core_sched.c`（各 3 个） | 调度器↔asm 上下文切换 ABI |
| `registry empty` | `ocos/core_usermode.c` | 编译进内核的应用注册表 |
| `chinese_font gz layout mismatch - regenerate chinese_font_data.c` | `services/font/svc_font_chinese.c` | 内嵌 CJK 字体 blob |
| 8 个 UHCI 调度页重叠防护：`ISO TD overlaps int QH`、`int QH overlaps ctrl QH`、`ctrl QH overlaps ctrl TDs`、`ctrl TD pool overlaps bulk QH`、`bulk QH overlaps bulk TDs`、`bulk TD pool overlaps int TDs`、`int TD pool overlaps scratch`、`scratch overflows schedule page` | `drivers/usb/drv_usb_core.c` **与** ocse 孪生（各 8 个） | UHCI 帧表布局数学 |

---

## 日志格式 — 解读输出

### klog（dmesg / logcat）

```
[<seconds>.<mmm padded to 3>] <LEVEL padded to 6> <module>: <message>
```

示例：`[    12.345] ERROR  watchdog: watchdog slot 3 (net-rx) -> KILL task`。

级别：`DEBUG` 0、`INFO` 1、`WARN` 2、`ERROR` 3、`FATAL` 4。环形
容纳 1024 条；覆盖计入 `klog_dropped()`，
`dmesg` 显示页脚 `-- <shown> lines (ring <n>/1024, dropped <d>) --`。用 `logcat -l <level>`（debug..fatal 或 0-4）、`-m <module>`、`-k <keyword>`、`-t <seconds>`、`-n <count>` 过滤；
`-f` 跟随。级别过滤陷阱见
[silent-failures.md](silent-failures.md)。

### 引导阶段日志

```
[HH:MM:SS.mmm] <stage>: OK|FAIL
```

安静级别 1 抑制非 FAIL 行；任何 `FAIL` 子串都为引导状态
判定标记失败。阶段名（OCST 阶梯）：
`BOOT`、`KM_AIN_ENTERED`、`SCHED_READY`、`DRIVERS_READY`、
`SERVICES_READY`、`L1_LAUNCHED`、`L1_READY`，运行时标记 255 =
`RUNTIME`。

### 健康告警（klog WARN 类）

```
alarm: memory >90% used (<used>/<total> KiB)
alarm: <n> active tasks (>64)
alarm: CPU >=90% busy for 10 consecutive seconds
```

机器"感觉不对"时第一个要运行的就是 `health` 命令 —
见 [silent-failures.md](silent-failures.md)。

### 崩溃记录

`crashlog` 列出 32 条环形记录（字段：jiffies、rtc_epoch、vector、
ring、error_code、rip、rsp、cr2、tid、pid、task[16]）；刷写到
`/var/log/crash.log`；核心转储落在 `/var/crash/core.dump`；klog
刷写到 `/var/log/kernel.log`。

## 相关页面

- [error-catalog.md](error-catalog.md) — 按子系统罗列每条消息串，
  附排查链
- [silent-failures.md](silent-failures.md) — 完全不打印任何内容的
  失败
- [../manual/hidden-commands.md](../manual/hidden-commands.md) —
  刻意演练这些路径的测试桩
- [../arch/crash-recovery.md](../arch/crash-recovery.md) — panic
  机制设计
