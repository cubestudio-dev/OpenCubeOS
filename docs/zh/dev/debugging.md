<!-- SPDX-License-Identifier: Apache-2.0 -->
# 调试（入口页）

简短的导览。完整的汇编 —— 串口取证、klog 环形缓冲区、
崩溃转储与寄存器快照、静默失败方法论，以及故障注入工具 ——
现在位于 [debug-handbook.md](debug-handbook.md)；测试程序与
隐藏开关的清单位于
[debug-interfaces.md](debug-interfaces.md)。本页保留日常
必备要点并指引后续阅读。

## 先读启动日志

每个子系统在启动时都会打印一行 `.... OK`。最后一条
没有打印的行就是出故障的子系统。捕获它：

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none
```

## 客户机内工具

```
ush> dmesg            (kernel ring replay; dmesg N for more lines)
ush> logcat -f        (live follow of the kernel log)
ush> logctl status    (levels, filters, file state)
ush> health           (CPU/mem/tasks/IO + the three alarm bits)
ush> exc              (exception self-test - proves the trap path)
ush> crashlog         (past crash records)
ush> dumpinfo         (core dump status)
ush> crashtest dump   (capture a dump WITHOUT panicking - wiring check)
```

各处的行格式统一为：`[SSS.mmm] LEVEL module: message`。

## panic 路径

内核 panic 会把异常帧（寄存器 + 故障地址 + 回溯）转储到
串口并写入 crashlog 环形缓冲区；挂载了文件系统时，
`/var/crash/core.dump` 携带 OCDUMP01 镜像。`crashtest`
触发一次刻意制造的 panic —— 在真正需要之前，
用它验证你的捕获接线是否可用。每类异常下各寄存器的含义：
[debug-handbook.md](debug-handbook.md) 第 3 节。

## 没有报错但也不工作？

沿静默失败阶梯排查：健康告警 -> 日志*环形缓冲区*
（不是实时控制台）-> kcap 捕获陷阱（被捕获的 op 会吞掉
实时输出 —— 检查路由方式）-> 返回值约定（L1SVC 调用只要
路由成功就"成功"；失败文本位于 `out` 之内）-> 识别诚实跳过
字符串。完整方法论：[debug-handbook.md](debug-handbook.md)
第 4 节。

## 在宿主侧调试内核

- 调试符号：流水线以 `-g` 链接，然后 strip 最终 ELF；
  `build/opencube_base.elf`（`strip` 之前、`make kernel`
  期间产出）是你的符号载体。
- 在 QEMU 中：`-d int,cpu_reset -D qemu.log` 记录中断与
  复位；`-s -S` 加 gdb（`target remote localhost:1234`）
  提供经典的远程调试循环。
- 可重现构建意味着两次复现的崩溃是*完全相同的字节* ——
  多次尝试之间不存在海森堡式构建差异
  （`make reproducible-check`）。

## 宿主单元测试

`make test` 针对 real 内核源码编译并运行 `tests/host_*.c`
下的测试套件（加密向量、tar、gzip、字符串、FAT 结构、
MB2 解析，宿主编译器上开 ASAN）。这里的一次回归比
一次启动循环的迭代快得多。清单：[host-tests.md](host-tests.md)。

## 已知的锋利边缘（来自审计记录）

- 4 GiB 以上的 xHCI DMA 会在探测和每次传输时被拒绝 ——
  如果 xHCI 设备消失，检查你的 QEMU 内存大小。
- ATA-DMA 对齐依赖堆的 32 字节块头；如果你改动
  `mem_heap.c`，运行 `fstest` + 一个 FAT32 挂载测试。
- multiboot2 解析器校验 tag 尺寸；测试中"手工拼造"的 mbi
  必须遵守每个 tag 的最小值，否则解析会提前停止。
- QEMU 在 HMP 截屏之后会立刻丢弃串口 RX 字节 ——
  功能测试轮次从不截图
  （[qemu-testing.md](qemu-testing.md) 第 4 节）。
- 被捕获的 L1SVC op 在运行期间会同时压制帧缓冲和串口
  tee —— 停驻或占用实时会话的 op 必须直连路由
  （kcap 陷阱，[debug-handbook.md](debug-handbook.md) 4.3）。
- `#DF` panic 是刻意最小化的（一条静态串口行，
  无记录/转储/刷写）—— 不要指望双重故障会产生转储。

## 症状 -> 第一步（速查）

| 症状 | 第一步 | 原因 |
|---|---|---|
| 启动在阶梯中途停止 | 读串口启动日志；最后缺失的 `OK` 行 = 出故障的子系统 | 每个子系统在启动时打印一行 `.... OK` |
| 机器挂死、控制台无响应 | 是否 `/HANGAT` 之类？复位后检查看门狗状态 + `crashlog` | 15 秒软看门狗 + 6300ESB 继电器产出的是证据，不是沉默 |
| 内核 panic | 抄下串口横幅 + 寄存器转储；重启后 `crashlog`/`dumpinfo` | OCDUMP01 镜像 + 32 槽位环形缓冲区会留存 |
| 用户程序行为异常 | 用 `run <name>` 运行它；前后各 `ps` 一次；检查 `exc` | ring-3 故障是 kill 而不是 panic —— 内核继续运行 |
| 命令"什么都没做" | 检查路由方式 + 返回约定；寻找诚实跳过字符串 | L1SVC 调用只要路由成功就"成功"；失败文本在 `out` 内 |
| 输出在操作中途消失 | kcap 捕获陷阱 | 捕获会压制实时通道 |
| 怀疑存储损坏 | `fsck <dev>`、`disk_rw_test <dev>`、`lsblk` 中的缓存统计 | 写回缓存只在写回成功后释放脏槽位 |
| 网络停滞 | `netstat`、`tcpstats`（RTO/cwnd/SACK 状态）、`net_rx_bad_checksum` | 连接表会告诉你哪一层停滞了 |
| 疑似熵/加密问题 | `cryptotest`、`dhtest` | 对 TLS/SSH 所用原语的 NIST 向量门禁 |

## 提交可调试的报告

在报告内核侧问题之前，收集这份清单 —— 每一项都是代码树
已经为你提供的机制：

1. **构建标识**：`uname -a` 输出（版本字符串 + OTA 比对
   所用的数字构建 id）—— 可重现构建意味着这两个数字
   就能锁定精确字节。
2. **完整串口日志**（`-serial file:boot.log`），而不是尾部
   的截图 —— 启动阶梯最后缺失的 `OK` 行指出出故障的
   子系统。
3. **klog 环形日志尾部**：`dmesg 200`（加上 `logctl status`
   以了解过滤器/级别）；注意页脚
   `-- <shown> lines (ring <count>/1024, dropped <n>) --`。
4. **崩溃证据（若有）**：串口的 panic 横幅 + 寄存器转储，
   重启后再取 `crashlog`（以及 `crashlog flush`）加上
   `dumpinfo`；挂载了文件系统时取
   `/var/crash/core.dump` 的 OCDUMP01 镜像。
5. **健康快照**（`health`）—— 告警行与五个指标区段可以
   把常见嫌疑人排除或锁定。
6. **复现步骤**，在干净的 `make run-bios` 启动上执行，
   并说明你的树上 `make reproducible-check` 是否通过
   （两次复现的崩溃是相同字节）。
7. QEMU 场景：精确的命令行（机型很重要 —— TCO 只在
   q35 上存在）以及 QEMU 版本。

## 下一步去哪

| 需求 | 页面 |
|---|---|
| 串口/klog/崩溃证据、寄存器解读、故障注入 | [debug-handbook.md](debug-handbook.md) |
| 约 93 个测试程序、诊断深度、隐藏开关 | [debug-interfaces.md](debug-interfaces.md) |
| 内部调用路径（execve、L1SVC、RX、块 IO、崩溃、OTA） | [call-chains.md](call-chains.md) |
| 内核结构体及谁在操作它们 | [data-structures.md](data-structures.md) |
| QEMU 勘误、sendkey、OVMF | [qemu-testing.md](qemu-testing.md) |
| 子系统深入（klog、崩溃、健康、看门狗、加密、OTA、OCIRE 嵌入） | [../internals/index.md](../internals/index.md) |
