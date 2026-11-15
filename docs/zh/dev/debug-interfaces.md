<!-- SPDX-License-Identifier: Apache-2.0 -->
# 隐藏的调试接口

已注册为命令但未作为用户工具宣传的调试面清单：约 93 个
测试桩程序、diag 类别的 ring-0 自检与芯片组操作、注册表
门禁、在线修复探针，以及每驱动的直通测试面。每个条目都是
真实的已注册程序或 L1SVC op —— 在 `ush>` 提示符下输入
它的名字即可。关于如何*读*输出，见
[debug-handbook.md](debug-handbook.md)；完整 op 编号见
[interface-dictionary.md](interface-dictionary.md)。

## 1. 如何运行其中任何一个

- 在 `ush>` 提示符下输入程序名（它们是注册表条目，由
  `help -a` 呈现）。
- 参数（如有注明）与任何命令完全一样地传递
  （`disk_rw_test hda`）；包装程序把 argv 拼进该 op 的
  参数行。
- 从宿主侧：`python3 tools/run_l1_per_app.py` 为每个程序
  启动一次 ISO 并记录各自输出（每行一个全新进程表）；
  `tools/run_fix4_regression.py` 运行固定命令组
  （见 [e2e-suites.md](e2e-suites.md)）。
- 语法由服务侧拥有：对 l1svc 包装的应用而言，程序只是一根
  透明管道 —— "隐藏"开关就是注册表提示永不显示、但服务
  会解析的开关。

## 2. 测试桩程序（约 93 个）

按所演练的内容分组。"运行"即调用方式；无参数者只写
裸名字。

### 2.1 OTA / 更新流水线（11 个 + config_test）

| 程序 | 测试内容 | 运行 |
|---|---|---|
| `ab_partition_test` | A/B 槽位 + 标志语义 | 裸 |
| `checkupdate_test` | 更新检查单元测试 | 裸 |
| `update_check_test` | 检查阶段单元测试 | 裸 |
| `update_download_test` | 下载阶段单元测试 | 裸 |
| `update_install_test` | 安装阶段单元测试 | 裸 |
| `update_local_test` | 离线包安装 | 裸 |
| `update_pkg_test` | 包清单/校验和 | 裸 |
| `update_rollback_test` | 回滚阶段单元测试 | 裸 |
| `update_status_test` | 状态阶段单元测试 | 裸 |
| `update_verify_test` | SHA256 校验阶段 | 裸 |
| `real_update_test` | 完整真实流程 | 裸 |
| `config_test` | /etc 配置后端 | 裸 |

配套驱动：`tools/make_update_pkg.sh`（打包）、E2E 的
`tools/fix1_checkupdate_e2e.py`。

### 2.2 块设备 / 存储（8 个）

| 程序 | 测试内容 | 运行 |
|---|---|---|
| `ahci_test` | AHCI 端口读 | 裸 |
| `nvme_test` | NVMe 命名空间读 | 裸 |
| `ata_dma_test` | ATA bus-master DMA 与 PIO 时序对比 | 裸 |
| `virtio_blk_test` | virtio-blk 往返 | 裸 |
| `disk_rw_test` | 原始扇区写/读/比较 | `disk_rw_test <device>` |
| `partition_test` | 分区扫描 | `partition_test [device]` |
| `fs_mount_test` | 格式化 + 挂载往返 | `fs_mount_test <device>`（**会格式化它**） |
| `real_hw_test` | 真实硬件磁盘 + NIC 普查 | 裸 |

### 2.3 NIC 驱动（10 个）

`e1000e_test`、`igb_test`、`ixgbe_test`、`bcm57xx_test`、
`rtl810x_test`、`rtl8125_test`、`rtl8139_test`、
`rtl8168_test`、`nic_rw_test`（帧环回）、`other_nic_test`
（尽力而为的家族探测）。全部裸调用。这些是各家族驱动的
*直通*调试面 —— 每个都打印该驱动自己的寄存器/链路/MAC
状态，而非通用摘要。

### 2.4 音频（8 个）

`hda_test`、`ac97_test`、`sb16_test`、`es1370_test`、
`virtio_snd_test`、`usb_audio_test`（每驱动 500 ms 音调
自检；hda/ac97/virtio 在 48000，sb16/es1370 在 44100）、
`audio_rw_test`（PCM 环回，48 KB 缓冲区）、
`sample_rate_test`（caps 位图与 set_rate 协商；无卡时
诚实 SKIPPED；HDA+44100 仅在 QEMU 下报告 caps 位）。
全部裸调用。

### 2.5 USB（7 个）

`usb_core_test`（枚举 + 控制传输）、`usb_hotplug_test`
（插拔，额外参数）、`usb_hub_test`（外部集线器）、
`usb_kbd_test`（HID 键盘）、`usb_mouse_test`（HID 指针）、
`usb_serial_test`（CDC-ACM）、`usb_storage_test`
（MSC BOT —— 注册块设备）。全部裸调用。

### 2.6 相机 + 遗留端口（6 个）

`uvctest`（UVC 捕获）、`scsitest`（块层之上的 SCSI，
LSI 53C895A）、`gameporttest`、`paralleltest`（SPP 字节）、
`irtest`（NEC/RC5/RC6/SIRC）、`touchtest`（i2c-hid 手势）。
全部裸调用。

### 2.7 电源路径（4 个）

`poweroff_test`、`suspend_test`（真实 ACPI S3）、
`halt_test`、`reboot_test` —— 每个都演练真实路径且可能
结束会话（电源向导在运行它们之前警告 + 确认；
halt/reboot 测试诚实地 SKIP 活体观察）。

### 2.8 显示 / CJK（5 个）

`chinesetest`、`font-test`、`encoding-test`、
`ime-test`（引擎 + 会话电池：词组/翻页/数字选字协议
断言）、`tui-test`（TUI 自检结论块，约 4.4 KiB）。
全部裸调用。

### 2.9 内核核心自检（16+）

| 程序 | 测试内容 |
|---|---|
| `memtest` | 内存服务电池 |
| `heaptest` | 堆压力（分配/释放/合并） |
| `heapbounds` | 堆边界条件 |
| `fstest` | VFS 压力 |
| `vmtest` | VMM 单元测试 |
| `vmkernelpt` | 内核页表转储 |
| `vmmap` | 地址空间映射转储 |
| `cr3test` | CR3 寄存器审计 |
| `pftest` | 缺页处理程序 |
| `pmmrace` | PMM 分配/释放竞态检测器 |
| `irqabitest` | IRQ 入口 ABI 探针（rsp mod 16、DF=0） |
| `msitest` | MSI/MSI-X 向量投递 |
| `timer` | 软定时器 |
| `synctest` | 自旋锁/信号量/互斥锁/条件变量 |
| `multi` | 3 个交错任务唤醒测试 |
| `spawn` | 后台孵化演示 |
| `exc` | 异常自检报告（与启动计数器共享） |
| `nice` | 优先级演示 —— `nice <tid> <prio 0..31>` |

外加恢复二人组 `crashtest`（故障注入，见
[debug-handbook.md](debug-handbook.md) 5.1）与 `dumpinfo`、
加密二人组 `cryptotest`/`dhtest`、`l1test`（作业/IPC/
动态链接），以及网络测试 `nf_test`/`tcpcc_test`/
`tcptest`/`dnstest`。

### 2.10 ring-3 扩展家族（扁平程序，标签 l1）

编译为扁平遗留程序的内核接口面探针：

`fork_test`、`exec_test`、`fdref_test`、`pipe_test`、
`select_test`、`select_zero_test`、`signal_test`、
`sigsegv_test`、`sse_test`（切换间的 FPU 状态）、
`stackovf`（用户栈守卫）、`int3_user`（ring 3 来的 #BP）、
`priv_test`（IOPB 全拒 —— ring-3 端口访问必须
故障）、`pf_test`、`p3_test`（优先级）、`mmap_test`、
`mmap_multi`、`mprotect_test`、`g6_test`（进程组；异步
汇总 `failed tests = 0`）、`g6_exec_a`、`g6_exec_b`、
`g6_oom`、`g7mmap`（BUG-0281），以及动态链接家族
`pie_test`、`reloc_test`、`so_test`、`dlsym_test`、
`dyn_hello`、`main_dyn`，外加 `hello`、`loop`（作业测试
目标）和 `badapp`（故意行为不端 —— 异常 kill 的金丝雀
类别）。

### 2.11 自验证 help 测试（3 道注册表门禁）

| 程序 | 断言 |
|---|---|
| `help_default_test` | 默认清单的计数/形状 |
| `help_a_test` | A-Z 排序不变量 |
| `help_wp_test` | 实时注册表文本的工作包分组 |

它们解析注册表文本本身（与 `help` 所用的同一份
`L1SVC L1/APPS` 渲染），因此兼任生成 `OC_USERPROGS[]`
表的运行时回归门禁。

### 2.12 在线修复探针（1 个）

`p2test <utf8|read0|pipe>`：

- `utf8` 把 174 个汉字（522 字节，一次系统调用）推过两条
  写路径（控制台 + 串口编码）；
- `read0` 检查 POSIX `read(0, buf, 0) == 0`；
- `pipe` 对管道槽位复用做 100 轮锤击。

由 `tools/wp_p2_batch_a_test.py` 经 QEMU 串口控制台驱动
（批次 A 在线探针：read0 #14、pipe 槽位泄漏 #9）。

## 3. diag 类别的隐藏深度（39 个 op）

在生命体征（1-8）之外，还有普通用户永远遇不到的两组：

- **G9 ring-0 自检（op 9-26）**：cr3test、exc、pftest、
  pmmrace、vmtest、vmkernelpt、vmmap、memtest、heaptest、
  heapbounds、irqabitest、timer、msitest、spawn、multi、
  nice、synctest、fstest —— 与上面的应用相同的函数体，
  经 L1SVC 到达而无需包装程序。其中五个
  （vmmap/pftest/cr3test/vmkernelpt）运行在一个
  **切换到内核 AS 的包裹**内：它们的 v2.2.0 语义依赖
  内核地址空间为当前空间，因此分发器从 ring-3 系统调用
  重建该上下文，并在返回前恢复孵化者的 AS。
- **G10 芯片组操作（op 27-39）**：acpi、acpidev、acpipow、
  acpitest（AML 解析器）、pcidump（`pcidump <bus:dev.fn>`
  例如 `0:1f.3` —— 完整配置空间）、pcibars、pcie、
  i2cdetect、i2cget（`i2cget <bus> <addr> [reg]`，十六进制）、
  i2cset（`i2cset <bus> <addr> <reg> <val>`）、i2ctest、
  smbus、smbustest（字节/字/块往返）。

## 4. 值得了解的隐藏参数

注册表提示永不显示这些；由服务解析：

| 命令 | 隐藏参数 | 效果 |
|---|---|---|
| `top` | `-n N` | 刷新 N 次（钳位 1..10），间隔 1 秒 |
| `ln` | `-s <target> <linkpath>` | 符号链接而非硬链接 |
| `sed` | `s/old/new/[g]`、`-n Np`、`Nd` | 替换 / 打印第 N 行 / 删除第 N 行 |
| `p2test` | `utf8 \| read0 \| pipe` | 探针选择器（见上） |
| `health` | `watch` / `export [path]` | 不同的 L1SVC op |
| 8 个 mega 应用 | `direct` | 跳过字面词，原样直通 |
| `help` | `-a` / `--all`、`-w` / `--wp` | 完整 A-Z / 分组清单 |
| `update` | `status`、`--status`、`--local <path>`、`local <path>` | 状态视图 / 离线安装 |
| `dns` | `[aaaa\|cname\|mx\|txt\|ns\|srv]` | 记录类型选择器 |
| `wget` | `https://...` 或三参数 `host port path` | TLS 路径 / 遗留位置形式 |
| `ssh` | `[port=2222] [user=oc] [password=oc\|"-"]` | `-` = 经 /etc/ssh_client_key 的公钥 |
| `ime` | `-i` | 交互式拼音合成行（数字选字、空格 = 首选、PgUp/PgDn 或 -/=/= 翻页、Backspace 退一字、Enter 提交原文、Esc 取消） |
| `dmesg` | `[N]` | 行数 1..1024（默认 30） |
| `uname` | `[-a]` | 完整身份字符串 |
| `serial_encoding` | `[utf8\|gbk]` | COM1 tee 编码 |
| `sleep` | 小数秒 | `sleep 0.5` = 500 ms 延迟（不是挂起） |

## 5. NIC 直通面（wifi / bt）

| 命令 | 模式 |
|---|---|
| `wifi` | 裸 = 适配器 + 家族注册表状态；`wifi scan` = 被动信标扫描（QEMU 下诚实的 "no radio"）；`wifi test` = 802.11 核心 + 各家族自检（无硬件的家族按名列出）；`wifi sdio` = SDHCI 总线状态 + 自检 |
| `bt` | 裸 = 传输 + 控制器状态；`bt usb` = HCI-USB 传输；`bt uart` = H4 UART 传输；`bt test` = 流自检（经 COM2 H4 字节进入 `tools/bt_test_peer.py` 的真实 E2E） |

## 6. USB 测试 op（9 个 op 的调试面）

类别 17 把整个 USB 栈的自检暴露为一等 op：`usb`（状态）、
`usbdev [slot]`（逐设备细节），然后是 `usb_core_test`、
`usb_kbd_test`、`usb_mouse_test`、`usb_storage_test`、
`usb_serial_test`、`usb_hotplug_test [...]`、
`usb_hub_test`。内核侧轮询线程（`driver_usb_poll_start`）
在 kmain 中启动并常驻。

## 7. 启动期自检（非命令）

有两个在 shell 存在之前运行 —— 它们的行出现在每份启动
日志里：

- `l1_ext_self_test()` —— "WP-01 ext self-test: N/4 points
  reachable"（探测 4 个扩展点：fb 访问、渲染器、字体
  引擎、控制台钩子）。
- `arch_exc_tests()` —— "exception self-test: N/3 (#DE/#UD/
  #PF) caught, kernel alive" —— 与 `exc` 命令共享通过
  计数器。

## 8. 让这一切保持诚实的注册表门禁

- `make userprogs-check` —— 任何 l1apps 源文件 / 接口头
  文件 / i18n json 比生成的 `userprogs_data.h` 新超过
  2 秒时让 `make dist` 失败。
- `tools/run_fix4_regression.py` 检查 6 —— 注册表下限：
  `help -a` 必须列出 >= 276 个程序（实测 284）。
- `help_*_test`（2.11 节）—— 运行时形状断言。
- `tools/gen_api_docs.py` —— 从*实时* `help -a` 捕获再生成
  `docs/data/*.json` 与 292 个命令页，容忍串口回显重叠
  （`[tag]` 括号是可选的，因为回显重叠可能吃掉 `"ega]"`
  的前导 `"e"`）。
