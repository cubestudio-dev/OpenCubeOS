<!-- SPDX-License-Identifier: Apache-2.0 -->
# 端到端测试套件

门住发布的深度测试。全部经串口控制台驱动真实 QEMU 启动；下面是
2.1.0 矩阵——发布证据引用的是真实输出（申报 skip 数）。

## 2.1.0 验证矩阵

| # | 门 | 工具 |
|---|-----|------|
| 1 | Host 单测（`make test`） | `tools/run_host_tests.py` |
| 2 | 看门狗/日志/健康 E2E | `tools/wp12_watchdog_logging_e2e.py` |
| 3 | TUI 命令面 E2E | `tools/wp10_tui_e2e.py` |
| 4 | 中文控制台 E2E | `tools/wp10_chinese_e2e.py` |
| 5 | TUI 桌面应用 E2E | `tools/wp10_tuiqa_apps_e2e.py` |
| 6 | 原生桌面 E2E | `tools/wp10_native_e2e.py` |
| 7 | L1 逐应用记录（2.1.0） | 从 ush 经 `run` 执行每个 `l1apps/` 程序，一应用一行记录 |

## Host 单测 - tools/run_host_tests.py

19 个测试宿主侧编译 L0 组件并运行：multiboot2 解析器、密码学
（BN/EC/RSA/X.509/AEAD/X25519）、TLS 1.3 密钥调度、TCP 拥塞控制、
块层 512 门、SCSI、红外、摄像头、wifi、蓝牙……无 openssl fixture 时
**17 PASS + 2 SKIP-FIXTURE**（rsa/x509；fixture 齐备时真跑——如实
申报当时的状态）。

## 看门狗/日志/健康 E2E - tools/wp12_watchdog_logging_e2e.py

14 个场景（S1-S14）。最重要的两个：

- **S12（Intel TCO）**：q35 + `-no-reboot`；断言 TCO 发现行
  （`Intel TCO found: PMBASE=... TCOBASE=...`）、武装看门狗、饿死
  它、断言真实复位与第二次启动。
- **S13（ACPI WDAT）**：同一形态，走 WDAT 表项。

另含 klog 写/冲/轮转/过滤链（阈值 4096 -> 实际生成 `kernel.log` +
`kernel.log.1`）、看门狗策略击杀（饿死任务被杀并留日志、内核存活）、
健康（JSON 导出合法性、3-spinner CPU 告警 `CPU-LOCKED` 且内核
存活）。

## TUI / 中文 / 桌面应用 / 原生套件

`tools/wp10_tui_e2e.py` 从 `oc>` 驱动 L0 TUI 命令面；
`tools/wp10_chinese_e2e.py` 驱动 CJK 控制台；tuiqa/native 套件从
Ring-3 的 `ush>` 提示符起步驱动桌面应用（文件/编辑器/面板/终端 +
原生编辑器/文件管理器升级）——2.x 启动流适配。

## L1 逐应用记录（2.1.0 新增）

正式 L1 应用集小而诚实：**ush**（唯一的 `OC_L1_APP_DECL` 应用）加
`l1apps/` 的 36 个嵌入测试程序。逐应用记录启动发布 ISO、从 ush 经
`run <名字>` 启动每个程序并记录观察到的输出。证据与套件日志一起放
在 `evidence/`。

## 回归电池（v1 时代，参考）- tools/run_fix4_regression.py

1.0.0 时代的 28 项检查：启动横幅、启动完成、异常自测 3/3、命令计数
233（v1 单提示符世界）、uname tag 形态、heaptest、fstest、
cryptotest、crashlog、l1test、以及每个 `run <userprog>`。其命令
计数与提示符流断言早于 v2 的 Ring-3 拆分——保留作参考，其
`run <userprog>` 部分由 L1 逐应用记录覆盖。

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

## SSH / HTTPS 套件（可用，非常规门）

`tools/ssh_mainstream_test.py` + `tools/sshd_test.py`（paramiko 客户
端对访客内 sshd，主机密钥经 `host_keygen_etc` 种到 /etc）与
`tools/run_fix4_https_e2e.py`（对 `tools/https_test_server.py`）。
需要种好的 `etc.img`（`make etc.img`）与网络 fixture；动网络栈时
跑它们。

## /etc 卷 fixture

SSH 与 HTTPS 套件假设有种好的 `etc.img`。每个测试周期构建一次：

```
make etc.img          # 32 MiB FAT32 + opencube.conf
# 然后首次启动后：host_keygen_etc（写 SSH 密钥）
```

QEMU 的 `run-*` make 目标会自动挂上它。
