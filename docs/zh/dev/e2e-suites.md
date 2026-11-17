<!-- SPDX-License-Identifier: Apache-2.0 -->
# 端到端套件

为发布把关的深度测试。它们全部通过 pexpect 在串口控制台上
驱动一次真实的 QEMU 启动；下表是发布证据所引用的矩阵
（真实输出、声明的跳过计数）。CI 本身只做构建 —— 这些在
本地/发布时运行（`tools/build_release.sh --deep`）。

## 1. 验证矩阵

| # | 门禁 | 工具 | 必须打印的行 |
|---|------|------|------------------------|
| 1 | 宿主单元测试（`make test`） | `tools/run_host_tests.py` | 所有套件 PASS（SKIP-FIXTURE 已声明） |
| 2 | 完整回归 31/31 | `tools/run_fix4_regression.py` | `=== 31/31 checks passed` |
| 3 | SSH E2E | `tools/ssh_g11a_e2e.py` | `[g11a] ALL PASS`（5 项检查） |
| 4 | HTTPS E2E | `tools/run_fix4_https_e2e.py` | `=== 8/8 HTTPS E2E checks passed` |
| 5 | 看门狗/日志/健康 | `tools/wp12_watchdog_logging_e2e.py` | S1-S14 |
| 6 | TUI / 中文 / 桌面 | `tools/wp10_*.py` | 按套件 |
| 7 | mega 应用冒烟 | WP-l1apps-merge 冒烟脚本 | 21/21 |

## 2. 回归门禁 —— 全部 31 项检查

`tools/run_fix4_regression.py` 启动 ISO（512M、
`-serial mon:stdio`、e1000 user 网络、供帧缓冲断言使用的
HMP monitor unix socket），等待 `ush>`，运行 24 条命令，
然后检查。每个模式都限定在唯一一条命令的输出分节内
（BUG #64 修复：全日志匹配在结构上永远为真），并剥离回显
的命令行，使含名字的模式永远无法自欺满足。

**启动组（6）**

1. 屏幕帧缓冲存活（BUG #66）—— HMP 截屏，>= 2 种颜色 +
   可测的前景比例（仅看串口的门禁曾在死面板上通过）
2. 启动横幅就绪 —— `Open Cube OS User-space Shell \(3\.0\.0`
3. 启动提示 —— `Type 'help' for built-in commands`
4. 启动完成 OK —— `boot complete\.+ OK`
5. 异常自检 3/3
6. 命令数 >= 276 —— 从 `help -a` 注册表行动态统计
   （WP-l1apps-merge 树上实测 **284**）；接口冻结下限

**系统组（2）**

7. uname 当前树 —— 版本正则，含 WP-10/11/12/13/14 标记
8. crashlog 干净启动 —— `no exceptions recorded|0 entries|N entries`

**自检程序组（17）**

9. heaptest 通过 —— `\bPASS\b`
10. fstest 通过 —— `VFS self-test.*PASS`
11. cryptotest NIST 通过
12. l1test wp8b 加载器 14/14 —— `ext_wp8b selftest 14/14 - PASS`
13. l1test 作业步骤 PASS
14. l1test 总体 PASS
15. hello 输出
16. fork_test
17. exec_test
18. pipe_test
19. signal_test
20. select_test
21. mmap_test
22. mmap_multi
23. g7mmap（BUG-0281）
24. p3_test（优先级）
25. g6_test 全链 —— 异步汇总 `failed tests = 0`
    （单独等待 300 秒）

**动态链接组（5）**

26. dyn_hello —— `hello from dynamic program`
27. so_test —— `foo_add(2,3)=5`
28. dlsym_test —— `foo_add(7,8)=15`
29. pie_test —— `loaded at 0x...`
30. reloc_test —— `foo_add=30 foo_global=42 fp=300`

**安全组（1）**

31. p0leak 围栏通过 —— `all kernel-address probes fenced`

退出码 = 失败检查数；发布矩阵引用 31/31。

## 3. SSH E2E（5/5）—— tools/ssh_g11a_e2e.py

方向 A（客户机客户端 -> 宿主 :2222 上的
`tools/paramiko_sshd.py`）：

1. `ssh-auth` —— `[ssh] authenticated (USERAUTH_SUCCESS)`
2. `ssh-exec` —— `[ssh] exec output: hello-from-OpenCubeOS-kernel-ssh`

方向 B（客户机 `sshd 22 oc oc` <- 宿主 paramiko 客户端，
经 hostfwd tcp::2223-:22）：

3. `sshd-listen` —— `listening on port 22`
4. `sshd-exec` —— 客户端 `uname` 返回 `Open Cube OS`
5. `sshd-capture-log` —— `exec: captured N bytes of output`

固化在工具里的运维要点：

- TCG 下 RSA-2048 密钥生成 > 500 秒 —— 用
  `tools/host_keygen_etc.py` 把宿主密钥（n||d，各 256 字节，
  e=65537）预播种进 `build/etc.img`；加载器走"密钥已在
  磁盘上"的快速路径（`OC_ETC_IMG` 选择预播种镜像）。
- 宽裕的 paramiko 超时（横幅 420 秒）—— TCG 下每个握手
  阶段都要花几十秒。
- 在服务器测试之前用一个客户端方向的包预热 ARP 缓存
  （冷缓存 SYN-ACK 停滞）。

## 4. HTTPS E2E（8/8）—— tools/run_fix4_https_e2e.py

针对 `tools/https_test_server.py`（与内核客户端匹配的
单请求 OpenSSL 服务器：仅 TLS 1.2、0x0067
DHE-RSA-AES128-SHA256、1024 位 RFC 3526 DH、自签名；
`observe` 模式 = 原始字节 close_notify 记录器；`eof`
模式 = 硬关闭截断服务器）：

0. **自签名链默认被拒绝**（BUG #21 验收）——
   `config set tls.insecure 1` 是兼容性轮次所用的显式
   选择加入
1. TLS 握手 —— 内核日志 `Connecting ... (TLS)`
2. 协商出 TLS 1.2 + 密码套件 DHE-RSA-AES128-SHA256
   （0x0067）（服务器侧）
3. 收到加密的 `GET /hello.txt`（服务器侧）
4. 提供规范正文（`body=24`）
5. 内核保存了正文（`wget` + `cat /wget_https.html` 匹配）
6. 观察到内核 close_notify（原始字节记录器模式）
7. **截断守卫** —— 硬关闭服务器（`eof` 模式）让内核
   迅速失败（< 45 秒，错误文本）而不是挂起

证书/DH 夹具在运行中生成：openssl 自签名 RSA-2048 +
钉在 RFC 3526 1024 位 MODP 组上的 DH 参数（由服务器
自身的 `P_HEX` 以 ASN.1 构造）。

## 5. mega 应用冒烟（21/21）

阶段 A —— 经典分发字节兼容：`disk-tool lsblk`、
`net-tool ip`、`sound-tool sound`、`sysdiag health`、
`hwinfo uname`、`power sleep 1`、`files df`、
`update status`、`disk-tool direct lsblk`，以及未知 op
的用法行。

阶段 B —— 向导：每个应用的无参菜单都能渲染并在 `q`
上退出；`disk-tool` 经菜单键 1 + ENTER 暂停运行 lsblk；
`update` 真实走完步骤 1-2（网络检查、A/B 缺席处理）并
干净地返回 ush。

注意：树中没有提交 `tools/test_megaapps.py` —— 冒烟
装置以"WP-l1apps-merge 冒烟脚本"的名义被引用（发布说明
中引用 21/21）；复现该矩阵的维护者按上面的阶段 A/B
清单重新搭建它。

## 6. 宿主单元测试 —— tools/run_host_tests.py

各套件在宿主侧编译 L0 组件并运行：multiboot2 解析器
（ASAN）、加密（NIST AES/SHA/HMAC、RSA、X.509、AEAD）、
对照 RFC 8448 的 TLS 1.3 密钥调度、TCP 拥塞控制
（CUBIC）、block-512 门禁，以及各子系统电池（BT、WiFi、
相机、SCSI、IR、触摸板）。SKIP-FIXTURE 套件在 openssl
夹具存在时真实运行 —— 状态永远被声明。完整清单：
[host-tests.md](host-tests.md)。

## 7. 看门狗 / TUI / 中文套件

`tools/wp12_watchdog_logging_e2e.py` —— 14 个场景
S1-S14：klog 级别/开关、logcat 过滤器、dmesg 回放、
logctl、持久化 + 轮转 `.1` 链、全局过滤器、
**S12：Intel TCO 真实复位 + 第二次启动**（q35、
`-no-reboot`、发现、布防、饿死、断言第二次启动）、
**S13：ACPI WDAT**（用 `tools/make_wdat_table.py` 合成
但合法的 WDAT 表经 `-acpitable` 注入，驱动真实 q35 TCO
寄存器）、策略 kill、健康 JSON。

`tools/wp10_*.py` —— TUI/中文/桌面/芯片组/定时器/
无线/相机家族；人工验证者把抖动的检查作为隔离的全新
QEMU 场景重跑（QEMU 在 HMP 截屏后丢弃串口 RX 字节 ——
已用内核内 RX trace 验证；因此功能轮次从不截图）。

## 8. pexpect 机制与串口回显容忍

每个套件继承的共享模式（`tools/qemu_runner.py`）：

- `pexpect.spawn(QEMU, [...], encoding="latin-1", codec_errors="replace")` —— 用 latin-1 让每个字节都
  存活；正则之前剥离 NUL。
- 提示符正则 `(?:ush>|oc>)\s`（可用 `OC_PROMPT` 覆盖）；
  v2 流程适配器 `boot_to_recovery()` 在 `ush>` 处发送
  `exit` 以到达 `oc>` 恢复提示符。
- `strip_ansi()` 在匹配前移除 CSI 序列。
- **串口回显容忍** —— "ega]" 现象：当客户机回显输入的
  命令时，回显可能与同一命令的*输出*重叠，吃掉 `[tag]`
  括号的前导 `"e"`，因此 `gen_api_docs.py` 把 `[tag]`
  括号当作可选，而回归门禁把每个模式限定在其自己的命令
  分节并先剥离回显行。
- 串口链路无法承载的按键（方向键/Home/End）经 HMP
  monitor 的 `sendkey` 送入；指针输入经 QMP
  `input-send-event`（`-display none` + QEMU 10 上 HMP
  mouse_move 已失效）；截屏经 unix monitor socket 上的
  HMP `screendump`；控制字节必须原样到达时用 unix-socket
  chardev 走原始串口。
- 已知的抖动来源：QEMU 在 HMP 截屏后间歇性丢弃串口
  RX -> 功能轮次从不截图；抖动的检查被隔离重跑
  （`wp10_native_manual.py`）。
