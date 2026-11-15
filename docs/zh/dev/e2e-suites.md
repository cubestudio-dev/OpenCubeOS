<!-- SPDX-License-Identifier: Apache-2.0 -->
# 端到端套件

把关发布的深度测试。它们全部通过串口控制台上的 pexpect 驱动
一次真实的 QEMU 启动；下表就是发布证据引用的矩阵（真实输出、
声明的跳过计数）。

## 验证矩阵

| # | 门禁 | 工具 | 必须打印的行 |
|---|------|------|------------------------|
| 1 | 宿主机单元测试（`make test`） | `tools/run_host_tests.py` | 所有测试桩 PASS（SKIP-FIXTURE 已声明） |
| 2 | 完整回归 31/31 | `tools/run_fix4_regression.py` | `=== 31/31 checks passed` |
| 3 | SSH E2E | `tools/ssh_g11a_e2e.py` | `[g11a] ALL PASS`（5 项检查） |
| 4 | HTTPS E2E | `tools/run_fix4_https_e2e.py` | `=== 8/8 HTTPS E2E checks passed` |
| 5 | 看门狗/日志/健康 E2E | `tools/wp12_watchdog_logging_e2e.py` | S1-S14 |
| 6 | TUI / 中文 / 桌面 E2E | `tools/wp10_*.py` | 按套件 |
| 7 | 大应用冒烟 | WP-l1apps-merge 冒烟脚本 | 21/21（经典分发 + 向导打开/退出 + 菜单操作 + 更新步骤） |

## 回归门禁（31/31）- tools/run_fix4_regression.py

启动 ISO，运行命令清单（uname、help -a、经 `run` 的自检程序），
并把每条命令的断言模式限定在该命令自己的输出段内（BUG #64：
旧的整日志匹配在结构上恒为真）。它还断言：

- **活动的帧缓冲**，通过一次真实的 HMP 截屏（BUG #66 - 仅串口
  的门禁曾在一块死面板上通过）；
- **注册表下限**：`help -a` 必须列出 >= 276 个程序（含 8 个
  合并应用实测 284）- 接口冻结规则落到可执行层面。

## SSH E2E（5/5）- tools/ssh_g11a_e2e.py

在真实 TCP 栈上的客户端与服务器两个方向：ssh-auth、ssh-exec、
sshd-listen、sshd-exec、sshd-capture-log。能帮你省下数小时的
注记：

- TCG RSA-2048 主机密钥生成很慢（>500 s）- 用 mtools 把密钥
  （n||d，各 256 B，e=65537）预置进 `build/etc.img`
  （`ssh_host_key`）；加载器会走"密钥已在磁盘上"的快路径。
- 在服务器测试前先用一个客户端方向的包暖 ARP 缓存（冷缓存的
  SYN-ACK 停摆）。

## HTTPS E2E（8/8）- tools/run_fix4_https_e2e.py

真实 OpenSSL 支撑的测试服务器 + 内核 TLS 客户端：握手、
TLS1.2 + 密码套件协商、加密 GET、规范正文、正文保存、
close_notify、截断守卫（及时检测到硬关闭的服务器）、自签名
证书链拒绝。

## 大应用冒烟（21/21）

阶段 A - 经典分发逐字节兼容：每个大应用运行它的一个历史操作
（`disk-tool lsblk`、`net-tool ip`、`sound-tool sound`、
`sysdiag health`、`hwinfo uname`、`power sleep 1`、`files df`、
`update status`、`disk-tool direct lsblk`、未知操作 usage）。

阶段 B - 向导：每个应用的无参菜单渲染出来并在 `q` 上退出；
`disk-tool` 经菜单键 1 + ENTER 暂停运行 lsblk；`update` 真实
走完步骤 1-2（网络检查、A/B 缺盘处理）并干净地回到 ush。

## 宿主机单元测试 - tools/run_host_tests.py

测试桩在宿主机侧编译并运行 L0 组件：multiboot2 解析器（ASAN）、
crypto（NIST AES/SHA/HMAC、RSA、X.509、AEAD）、TLS 密钥日程、
TCP 拥塞控制、block-512 门禁、FAT 结构、堆/VMM 不变量……
当 openssl 夹具存在时，SKIP-FIXTURE 测试桩会真实运行 - 声明
状态即可。

## 看门狗 / TUI / 中文套件

`tools/wp12_watchdog_logging_e2e.py`（S12：Intel TCO 真实复位 +
第二次启动；S13：ACPI WDAT；klog 轮转链；策略击杀；健康
JSON）、`tools/wp10_tui_e2e.py`、`wp10_chinese_e2e.py`、
`wp10_tuiqa_apps_e2e.py`、`wp10_native_e2e.py` - 机制未变，
动到它们的领域时运行它们。
