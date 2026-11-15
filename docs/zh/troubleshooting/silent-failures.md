<!-- SPDX-License-Identifier: Apache-2.0 -->
# 静默失败 — 什么都不打印的时候

Open Cube OS 的多数失败很吵：错误串、`FAIL` 判定、
panic 横幅。本页讲另一类：**不打印任何东西**就出错的
情形、设计为何让它们安静、以及让它们现形的准确
命令。读一遍；它会为你省下一个下午。

一行规则：**先 `health`，再 `dmesg 100`，然后 `crashlog`。**
这三样几乎能看穿每一次静默失败。

---

## 1. 输出捕获 — 当你的控制台什么都不显示，因为文本
进了缓冲

### 机制

多数服务 op 是 v2.2.0 的 shell 命令体，经
`screen_console_puts` 打印。为了通过 L1SVC 系统调用提供
它们，分发器将其包进 `core_kcap_run_fn(fn, arg, out, cap)`：

- 捕获活跃期间，捕获钩子**就是控制台钩子**
  （控制台驱动只持有一个钩子槽）。
- 字符进入调用方的缓冲；framebuffer 绘制
  和串口分流在此期间均被**抑制**。
- 捕获最多叠 4 层；最后一个捕获结束时重新安装
  引导串口分流。
- 溢出"以标记上报，绝不静默截断"。

你会实际遇到的后果：

- 从向导或脚本运行的命令**不实时打印** —
  其输出进入捕获缓冲，由调用方渲染。
  这是正确的，不是失败。
- 如果被捕获的命令体循环或等待控制台输入，机器看起来
  像死了：提示符再也不回来。

### 三种路由风格（为何有些 op 实时打印而另一些不）

| 风格 | op | 原因 |
|-------|-----|-----|
| **captured**（`core_kcap_run_fn`） | 多数一次性文本 op（状态、列表、测试） | 输出属于调用方 |
| **direct**（`out[0]=0; return fn(arg);`） | `sshd`、全部 9 个 TUI op、`ime -i`、`power sleep` | 被捕获的命令体不得睡眠 —— 被捕获运行时它们吞掉了实时控制台输出（在 OCIRE 监督引导期间现场发现） |
| **buffer-render**（服务自己写 `out`） | l1、klog、health、diag、fs、time、watchdog 分发器 | 不涉及遗留打印体 |

所以当 `power sleep 2` 实时打印倒计时而 `health` 直到
返回都不回显 — 那是路由表，不是 bug。而如果你从
`sshd` 或某个 TUI 页面看到**完全没有输出**，检查串口
控制台：direct 路由的输出偏好实时
控制台，在慢终端上可能被漏看。

## 2. 返回值约定 — 无 errno 可读

**没有全局 `errno` 变量也没有 `errno.h`**（freestanding
内核，无 libc）：

- 0 = 成功，负数 = 失败（`-1` 为通用）。
- 精度来自 8 个稠密负数枚举家族（61 个代码 —
  [error-codes.md](error-codes.md)）。
- 环形缓冲 API 返回 ≥ 0 的槽索引；`-1` = 越过窗口。
- watchdog 槽位 API 返回 -1（表满）/ -2（参数错误）。

陷阱：一个以 `-1` 失败且不打印的命令（参数合法但
操作被拒，例如写 OCIRE 保护的恢复盘）在**任何地方都
不留下消息**，除非服务自己选择写入 klog。当命令"什么
都没做"：

1. 重新运行并在第二步观察 `dmesg 30` — 许多拒绝是
   klog WARN/ERROR 行，不是控制台行。
2. 检查该家族的 `_test` 桩 — 它以可见的判定
   演练同一 op。
3. 记住刻意的拒绝：写恢复盘
   （`blk: write ... REFUSED` 仅在 klog）、杀 ocire_guard
   （看起来像"no such process"）。

## 3. "如实跳过"模式 — 一个 PASS 形状的非答案

硬件缺失条件刻意报告 `(honest skip)` /
`(honest FAIL)` 而不是让套件失败。示例：

- `hpet: legacy route not available (honest skip)`
- `oneshot: arm failed (honest FAIL)`
- `[audio_rw_test] no sound cards registered => SKIPPED`
- `msitest` 的 `e1000: no MSI cap or BAR0  SKIP (reported honestly)`
- `[nic_rw_test] no NICs registered => SKIP`
- QEMU 中的 `wifi scan`：如实的"no radio"输出

解读规则：**以如实跳过收尾的套件什么也没测**。
如果你预期硬件在场（你接了设备、传了 QEMU
flag），如实跳过意味着探针没找到它 — 去查总线
（`lspci`、`usb`），而不是查测试。

## 4. 健康优先 — 三个告警位

健康监视器每秒采样并恰好升起三种告警（klog WARN）：

```
alarm: memory >90% used (<used>/<total> KiB)
alarm: <n> active tasks (>64)
alarm: CPU >=90% busy for 10 consecutive seconds
```

在深潜任何"慢"或"怪"的机器之前，运行：

```
ush> health              # one snapshot: cpu/mem/heap/tasks/disk/net
ush> health watch        # live monitor until ENTER
ush> health export       # write the report to a file
```

告警还作为额外的喂狗源接入 watchdog 槽 0
（"system-heartbeat"）：活着但告警的系统持续喂狗；
死了的停止，WP-11 panic 路径（转储 + crashlog + 策略）
触发。所以一台"静默"重启的机器很可能先告过警 —
下次启动的 `health` 加 `/var/log/kernel.log` 会讲述
故事。

## 5. 日志级别过滤 — 你从未见过的消息

klog 有 5 个级别（DEBUG 0 … FATAL 4），带逐级别发射
开关、活动过滤器和全局级别。如果某行从不出现：

- **级别被关**：`logctl status` 显示活动级别与
   逐级掩码；`logctl level -1` 完全静音。用
   `logctl enable debug`（或你需要的级别）重新启用。
- **过滤器**吃掉了它：`logctl filter` 设置作用于活动
   通道以及 dmesg/logcat 回放。`logctl filter clear` 重置。
- **环形已回绕**：1024 条，覆盖被计数 — dmesg
   页脚 `-- <shown> lines (ring <n>/1024, dropped <d>) --` 显示
   丢弃。`dmesg 1024` 看整个窗口。
- 你看错了通道：`logctl flush` 写
   `/var/log/kernel.log`（rotate 保留 3 份：kernel.log.1 ...）。

快速解除隐藏：

```
ush> logctl level 0           # everything
ush> logctl test              # emits one line per level (module klogtest)
ush> logcat -l debug -f       # follow everything live; ENTER exits
```

`logctl test` 是校准工具：如果它的 5 行出现，
管线正常，你的目标消息确实没有被发出。

## 6. crashlog — 你忘了它存在的 32 槽历史

`crashlog` 将每次 panic 记入 32 条环形（字段含
vector、rip、rsp、cr2、tid/pid、任务名），刷写到
`/var/log/crash.log`；核心转储去 `/var/crash/core.dump`。
任何不明复位之后：

```
ush> crashlog          # were there panics you never saw?
ush> dumpinfo          # core dump buffer status
ush> dmesg 100         # this boot's klog
ush> cat /var/log/crash.log
```

空输出打印 `no dump captured yet (trigger: crashtest panic|exc|pf)` — 这也是在健康机器上验证该机制的方法。
#DF panic 刻意最小化（一条静态串口行，无记录/转储/刷写 —
KNOWN_ISSUES 中已声明）：如果机器带着单行横幅死去，
检查**串口**，不是 crashlog。

## 7. 静默不更新的 OTA — 六因链

`update`/`checkupdate` 安静地失败是经典的静默失败，
因为链路多半在代码内而非控制台文本中失败。按序
走完六因：

| # | 原因 | 如何看见 | 修复 |
|---|-------|---------------|-----|
| 1 | `update_url` 错误（坏 scheme/拼写） | `config get update_url` | `config set update_url http://host/path`（必须 http:// 或 https://） |
| 2 | DNS 无法解析更新主机 | `dns <host>` → `DNS resolution failed` | 修复 DNS 服务器（重跑 `dhcp`，或设置 `ip <a> <m> <g>` 并检查 DNS 字段） |
| 3 | 无 A/B 磁盘 | `update status` 缺 `A/B disk: present` | `lsblk` → `abdisk <dev>`（destructive；向导防护） |
| 4 | 签名/校验失败 | 重跑 `update` — `<why>` 为 `SHA256 mismatch` / `package (tar.gz) invalid` | 重新下载；核对服务器的 package_sha256 |
| 5 | 版本比较认为已是最新 | `checkupdate` 打印 `Current version is up to date.` | 不是失败 — 源的版本字段必须高于你的；检查服务器的 update.json |
| 6 | 网络完全不通 | `ip` 显示 0.0.0.0；`ping <gw>` 失败 | `dhcp`（或静态 `ip`），然后复查 |

两条额外的安静路径：配置中的 `online_update=no` 使
**autoupdate** 后台路径静默跳过（`[autoupdate] skipped: network not ready` 仅在 klog） — `config get online_update`；
`auto_check=no` 同样禁用启动时检查。

5 步 `update` 向导（无参数）按序走因 1→3→6
（IP 探测 → DHCP 提议 → A/B 磁盘探测 → DNS → 检查 → 安装），
因此拿不准时直接无参数运行 `update` 并阅读失败的
`[n/5]` 步。

## 8. 串口回显与被吃掉的 `"e"` — 容错阅读

串口控制台既是输入也是输出通道；当客户机在打印的
同时回显你的按键，回显可能与在途输出重叠。经典
伪影：`[ocse]` 这样的带括号标签变成 `[ega]` 等等 —
准确地说：串口回显重叠**吃掉 `ega]` 形片段的
前导 `e`**（工具链称之为 `ega]` 现象）。你在屏幕上
看到的可能是客户机所打印内容的字符错位版本。

容错阅读规则（E2E 工具链用的同一套）：

- grep 引导输出时把 `[tag]` 括号视为**可选** —
  匹配内部词，不匹配标点。
- 匹配前剥离回显的命令行（客户机回显你输入的，
  然后打印结果）。
- 预期 ANSI CSI 序列交错；正则前剥离。
- 串口链路无法承载的按键（方向键、Home/End）必须经
  QEMU monitor 的 `sendkey` 送入，而不是敲转义码 —
  CSI 字节经 TCP 套接字在引导菜单时刻到达得太晚。
- 已知抖动：QEMU 在 HMP screendump 之后偶发丢弃串口
  RX 字节（经内核内 RX 追踪验证） — 绝不在功能轮次
  中途截图；抖动的检查要隔离重跑。

如果唯一的症状是一个怪字符，先重跑命令再调试
OS：通常是链路，不是内核。

## 9. "什么都没发生"检查单

1. `health` — 有告警吗？（内存/任务/CPU）
2. `dmesg 100` — 该家族有任何 WARN/ERROR 吗？（留意
   级别过滤器：`logctl status`）
3. `crashlog` + `dumpinfo` — 早先有什么死掉了吗？
4. 重新运行命令并观察**返回路径**：静默的 `-1`
   通常意味着刻意拒绝（保护盘、guard 通道、
   已禁用特性 — 见 §2）。
5. 输出是被**捕获**的吗（§1）？用裸命令而非
   向导运行该 op，或反过来。
6. OTA 形状的问题 → 六因链（§7）。
7. 单个乱码字符 → 串口回显（§8）；先重跑。
8. 仍然什么都没有：捕获串口（`-serial file:boot.log`）并检查
   klog 环形中从未到达屏幕的 FATAL 行 —
   串口控制台是权威。

## 相关页面

- [error-codes.md](error-codes.md) — 安静拒绝背后的代码
- [error-catalog.md](error-catalog.md) — 按子系统排列的
  大声失败
- [../manual/hidden-commands.md](../manual/hidden-commands.md) —
  令内核路径打印判定的桩
- [qemu-debugging.md](qemu-debugging.md) — 捕获技术
