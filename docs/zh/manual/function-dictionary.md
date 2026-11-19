<!-- SPDX-License-Identifier: Apache-2.0 -->
# 功能字典 — Open Cube OS 3.0.0 的全部命令

本页是 3.0.0 用户面的完整、无遗漏索引：
**311 条命令 = 300 个已注册程序 + 11 个 ush 内建命令**。
下列每个程序都可以在 `ush>` 提示符下按拼写原样输入
（它位于内核注册表 `OC_USERPROGS[]` 中，由
`tools/gen_userprogs.py` 从 `ocos/l1apps/` 生成）。

每条命令的详细说明位于 [commands/](commands/index.md)（293 个页面，
每命令一页）。本字典补充了那些页面没有在每条目旁逐一写明的
内容：**隐藏语法** —— 能够工作但注册表提示或 `help`
中没有展示的 flag、子命令与参数形式。

## 目录

- [如何阅读本页](#如何阅读本页)
- [用户面从何而来](#用户面从何而来)
- [mega (8)](#mega-8--合并后的向导应用) — 各向导与 `direct` 门
- [l1 (35)](#l1-35--shell-本身--ring-3-扩展家族)
- [core (25)](#core-25--内核核心诊断)
- [block (24)](#block-24--块服务--存储)
- [display (22)](#display-22--显示--cjk--tui--ime)
- [nic (20)](#nic-20--nic-服务--以太网驱动)
- [fs (20)](#fs-20--文件服务命令)
- [input (18)](#input-18--acpi--总线--芯片组)
- [snd (17)](#snd-17--声音服务--音频驱动)
- [ush (16)](#ush-16--shell-工具带文本进程文件信息)
- [ota (16)](#ota-16--更新--a-b--配置)
- [net (16)](#net-16--网络服务)
- [camera (10)](#camera-10--摄像头--遗留端口历史标签名)
- [usb (9)](#usb-9--usb-服务)
- [power (8)](#power-8--电源服务)
- [crypto (4)](#crypto-4--加密--ssh)
- [recovery (7)](#recovery-7--杂项历史命令)
- [watchdog (5)](#watchdog-5--klog--watchdog--health)
- [time (4)](#time-4--时钟源)
- [12 个 ush 内建命令](#12-个-ush-内建命令)
- [参数如何到达其语法](#参数如何到达其语法)
- [命令名怪癖](#命令名怪癖)
- [隐藏参数总清单](#隐藏参数总清单)
- [help 不会强调的内容](#help-不会强调的内容)

## 如何阅读本页

| 标记 | 含义 |
|--------|---------|
| `⚑` | **隐藏参数面**：该语法可用，但注册表提示、`help` 或应用自身的 usage 行没有提到它（或仅在某个子命令的 `usage:` 中提及）。 |
| *（无）* | 该命令不接受参数（或其参数已被完整公示）。 |
| *destructive* | 会破坏数据；向导用"重新输入设备名 + 裸 `y` 确认"加以防护，classic/direct 分发则立即执行。 |

12 个 ush 内建命令在此概述，完整文档见
[ush-reference.md](ush-reference.md)。测试/诊断桩（300 个中约 93 个）
有各自的深入页面：[hidden-commands.md](hidden-commands.md)。

## 用户面从何而来

- 276 条历史命令已被回归门冻结
  （`tools/run_fix4_regression.py` 测得注册表 ≥ 276；3.0.0
  发布 300 条）。任何命令都不能被悄悄移除。
- 8 个 **mega 应用** 在 3.0.0 中合并而成（`update`、`disk-tool`、
  `net-tool`、`sound-tool`、`files`、`sysdiag`、`hwinfo`、`power`）；
  历史的单命令应用仍保持注册，因此每个 op 至少有两种拼写
  （见 [mega-apps.md](mega-apps.md)）。
- `ush` 和 `ld_so` 已注册但从未是 v2.2.0 的 shell 命令
  （标签 `l1`）。

### help — 三种模式

```
ush> help            built-in summary + "Registry: 300 programs - 'help -a'
                     lists them all, 'help -w' groups by work package."
ush> help -a         built-ins + every registry entry, A-Z, tagged
ush> help --all      (same)
ush> help -w         built-ins + registry grouped by work package;
ush> help --wp       the mega group renders FIRST
```

`help -a` 和 `help -w` 是多数用户从未发现的 flag；独立的
[help](commands/help.md) 应用接受相同的 flag。行格式为
`  name  [tag]  hint`（hint 跟随当前 locale，zh 回退到 en）。

### 19 个类别标签（来自生成器的 WP_TAGS，已冻结）

| tag | n | tag | n | tag | n |
|-----|---|-----|---|-----|---|
| l1 | 35 | ush | 16 | camera | 10 |
| core | 25 | ota | 16 | usb | 9 |
| block | 24 | net | 16 | power | 8 |
| display | 22 | input | 18 | mega | 8 |
| nic | 20 | snd | 17 | recovery | 7 |
| fs | 20 | | | watchdog | 5 |
| | | | | time | 4, crypto | 4 |

类别标签是历史产物且刻意古怪：ACPI/I2C/PCI/MSI
工具归在 `input` 下，SCSI/gameport/parallel/IR 归在 `camera`
下，`lspci` 是 `net`，`ssh`/`sshd` 是 `crypto`，`reboot`
是 `ota` 而 `reboot_test` 是 `power`。不要从标签中读出语义；
它们仅是分组辅助。

---

## mega (8) — 合并后的向导应用

每个 mega 应用都有**三扇门**：无参数 → 交互式向导；
`<app> <op> [args]` → classic 分发（与历史输出逐字节一致）；
`<app> direct <op> [args]` → 逐字透传。字面词 `direct`
被跳过，其余部分到达历史 op。

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [update](commands/update.md) | OTA 更新向导（5 步线性流程） | ⚑ `update direct` = 裸在线流程；⚑ `update status` / `update --status` — A/B 槽位 + 版本状态（打印 `A/B disk: present` 锚点）；⚑ `update --local <path>` / `update local <path>` — 从包 tar.gz 离线安装 |
| [disk-tool](commands/disk-tool.md) | 磁盘 / 文件系统向导 | ⚑ `direct` 透传 30 个逐字 op 表（块 23 + 文件系统 7）：ahci, nvme, ata, dskstat, lsblk, parted, mkfs.fat32, mkfs.exfat, mkfs.ext4, fsck, sync, ahci_test, nvme_test, ata_dma_test, virtio_blk_test, disk_rw_test, partition_test, fs_mount_test, real_hw_test, abdisk, install, grub-install, abcfg, mount, mounts, umount, fatmount, fatstat, tree |
| [net-tool](commands/net-tool.md) | 网络向导 | ⚑ `direct` 透传 16 个 op：arp, dhcp, dns, dnstest, firewall, ip, lspci, nf_test, route, tcpcc_test, tcpstats, tcptest, ssh, sshd, wifi, bt |
| [sound-tool](commands/sound-tool.md) | 音频向导 | ⚑ `direct` 透传 17 个 op：sound, hda, ac97, sb16, es1370, virtiosnd, usbaudio, play, volume + 6 个驱动自测 + audio_rw_test, sample_rate_test |
| [files](commands/files.md) | 文件管理器向导（唯一 fork/execve 的 mega 应用） | ⚑ `direct` 启动真实程序：ls, tree, df, du, cp, mv, rm, edit, stat, chmod |
| [sysdiag](commands/sysdiag.md) | 系统诊断向导 | ⚑ `direct` 透传 46 个 op 行（含双词拼写 `health export`、`health watch`）；crashtest 只能通过 classic/direct 触达 —— 刻意不设为向导按钮 |
| [hwinfo](commands/hwinfo.md) | 只读硬件清单向导 | ⚑ `direct` 透传 26 个只读 op（display/time/pci/acpi/usb/camera/input 家族） |
| [power](commands/power.md) | 电源向导 | ⚑ `direct` 透传 9 个 op：shutdown, poweroff, reboot, halt, sleep, poweroff_test, suspend_test, halt_test, reboot_test |

`direct` 关键字在所有注册表提示中均未记载 —— 只在
各应用的 `usage()` 文本中记录。`update`、`disk-tool lsblk`
和 `disk-tool direct lsblk` 是同一服务 op 的三种等价拼写。

## l1 (35) — shell 本身 + ring-3 扩展家族

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [ush](commands/ush.md) | ring-3 shell（默认启动项） | 裸 ELF，未 gzipped；见 [ush-reference.md](ush-reference.md) |
| ld_so | 动态加载器本身（映射于 0x10000000） | 平坦模型 `ld_so.c` + `ld_so.ld`；不打算被输入 |
| [ldd](commands/ldd.md) | 动态依赖列表器 | ⚑ 读取任意注册表程序的 PT_INTERP + DT_NEEDED：`ldd <program>` |
| l1test | L1 作业/IPC/动态链接自测 | 无参数；PASS/FAIL 判定块 |
| hello | hello world（平坦 asm） | 作业控制固定件 |
| loop | 空闲循环（平坦 asm） | 作业测试目标；永久忙等（见 GitHub Issues） |
| badapp | 故意行为不端的应用（平坦 asm） | 崩溃路径固定件 |
| dyn_hello | 动态链接测试程序 | 打印 `hello from dynamic program` |
| main_dyn | 动态主镜像测试 | 动态链接固定件 |
| pie_test | PIE 加载测试 | 打印其加载基址 `loaded at 0x...` |
| reloc_test | 重定位测试 | 打印 `foo_add=30 foo_global=42 fp=300` |
| so_test | 共享库调用测试 | 打印 `foo_add(2,3)=5`（libfoo.c 是内嵌 solib） |
| dlsym_test | dlsym 解析测试 | 打印 `foo_add(7,8)=15` |
| exec_test | execve 面探针 | 平坦的内核面探针 |
| fork_test | fork 面探针 | 平坦 |
| fdref_test | fd 引用计数（dup/fork 继承）探针 | 平坦 |
| pipe_test | pipe 系统调用探针 | 平坦 |
| select_test | select 系统调用探针 | 平坦 |
| select_zero_test | 零 fd 的 select 探针 | 平坦 |
| signal_test | 信号投递探针 | 平坦 |
| sigsegv_test | ring-3 中故意的 SIGSEGV | 内核存活，进程被杀 |
| sse_test | SSE 状态保存探针 | 平坦 |
| stackovf | 故意的用户栈溢出 | 遏制固定件 |
| int3_user | ring-3 #BP 触发 | 遏制固定件 |
| priv_test | 特权指令拒绝探针 | 平坦 |
| pf_test | ring-3 缺页探针 | 平坦 |
| p3_test | 优先级调度探针 | 平坦 |
| mmap_test | mmap 系统调用探针 | 平坦 |
| mmap_multi | 多重映射探针 | 平坦 |
| mprotect_test | mprotect 探针 | 平坦 |
| g6_test | G6 进程组里程碑测试 | 完整链；异步汇总 `failed tests = 0` |
| g6_exec_a / g6_exec_b | G6 exec 两半 | 由 g6_test 运行 |
| g6_oom | G6 OOM 路径测试 | 平坦 |
| g7mmap | G7 mmap 里程碑测试 | 平坦 |

## core (25) — 内核核心诊断

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [cr3test](commands/cr3test.md) | CR3 寄存器审计 | 无参数 |
| [crashlog](commands/crashlog.md) | 崩溃转储记录（32 槽环形） | 无参数；空 → `no dump captured yet (trigger: crashtest panic\|exc\|pf)` |
| [frag](commands/frag.md) | PMM 碎片报告 | 无参数 |
| [heap](commands/heap.md) | 内核堆统计 | 无参数 |
| [heapbounds](commands/heapbounds.md) | 堆边界检查 | 无参数 |
| [heaptest](commands/heaptest.md) | 堆压力测试 | 无参数 |
| [irqabitest](commands/irqabitest.md) | IRQ 亲和性测试 | 无参数 |
| [kill](commands/kill.md) | 终止一个进程 | `kill <pid>` — 恒为 SIGKILL (9)；ocire_guard 代理无法被杀 |
| [mem](commands/mem.md) | 物理内存统计 | 无参数 |
| [memtest](commands/memtest.md) | 内存服务测试组 | 无参数 |
| [multi](commands/multi.md) | 多核唤醒测试（3 个交错任务） | 无参数 |
| [nice](commands/nice.md) | 优先级演示 | ⚑ `nice <tid> <prio>` — 是 tid 不是 pid；0 = 最高 … 31 = 最低 |
| p0leak | 用户指针校验围栏测试（平坦 asm） | 打印 `all kernel-address probes fenced` |
| [p2test](commands/p2test.md) | live-fix 探针 | ⚑ `p2test utf8` / `p2test read0` / `p2test pipe` — 三个不同探针（见 [hidden-commands.md](hidden-commands.md)） |
| [pftest](commands/pftest.md) | 缺页处理测试 | 无参数 |
| [pmmrace](commands/pmmrace.md) | PMM 竞态检测器 | 无参数 |
| [ps](commands/ps.md) | 列出进程 | 无参数 |
| [run](commands/run.md) | 带参数运行一个程序 | `run <name> [args...]` — 对任意注册表名 fork+execve |
| [sched](commands/sched.md) | 调度器统计 | 无参数 |
| [spawn](commands/spawn.md) | 后台孵化演示 | 无参数 |
| [syncstat](commands/syncstat.md) | 同步原语统计 | 无参数 |
| [synctest](commands/synctest.md) | 同步原语测试 | 无参数 |
| [vmkernelpt](commands/vmkernelpt.md) | 内核页表转储 | 无参数 |
| [vmmap](commands/vmmap.md) | 地址空间映射转储 | 无参数 |
| [vmtest](commands/vmtest.md) | VMM 单元测试 | 无参数 |

## block (24) — 块服务 + 存储

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [abcfg](commands/abcfg.md) | A/B 槽位配置 | 无参数 |
| [abdisk](commands/abdisk.md) | A/B 槽位磁盘工具 *（destructive，重新分区）* | `abdisk <device>` — 向导中有防护，classic 形式立即执行 |
| [ahci](commands/ahci.md) | AHCI 控制器状态 | 无参数 |
| [ahci_test](commands/ahci_test.md) | AHCI 端口测试 | 无参数 |
| [ata](commands/ata.md) | ATA/IDE 状态 | 无参数 |
| [ata_dma_test](commands/ata_dma_test.md) | ATA DMA 测试 | 无参数 |
| [disk_rw_test](commands/disk_rw_test.md) | 磁盘读写测试 | `disk_rw_test <device>` |
| [dskstat](commands/dskstat.md) | 磁盘 I/O 统计 | 无参数 |
| [fs_mount_test](commands/fs_mount_test.md) | 文件系统挂载测试 | `fs_mount_test <device>` — **会格式化该设备** |
| [fsck](commands/fsck.md) | 检查文件系统（仅 FAT32） | `fsck <device>` |
| [grub-install](commands/grub-install.md) | 安装 OCBL 引导链 | `grub-install <device>` |
| [install](commands/install.md) | 将 Open Cube OS 安装到磁盘 *（destructive）* | `install <device>`（需要 ≥ 64 MiB） |
| [lsblk](commands/lsblk.md) | 列出块设备 | 无参数 |
| [mkfs.fat32](commands/mkfs-fat32.md) | 创建 FAT32 文件系统 *（destructive）* | `mkfs.fat32 <device>` |
| [mkfs.exfat](commands/mkfs-exfat.md) | 创建 exFAT 文件系统 *（destructive）* | `mkfs.exfat <device>`（需要 ≥ 65 扇区 + FAT + 堆） |
| [mkfs.ext4](commands/mkfs-ext4.md) | 创建 ext4 文件系统 *（destructive）* | `mkfs.ext4 <device>` |
| [nvme](commands/nvme.md) | NVMe 控制器状态 | 无参数 |
| [nvme_test](commands/nvme_test.md) | NVMe 命名空间测试 | 无参数 |
| [parted](commands/parted.md) | 分区表编辑器（查看） | ⚑ `parted [device]` — 可选的设备参数容易被忽略 |
| [partition_test](commands/partition_test.md) | 分区扫描测试 | `partition_test <device>` |
| [real_hw_test](commands/real_hw_test.md) | 真实硬件磁盘测试 | 无参数（含一节 NIC 内容） |
| [sync](commands/sync.md) | 将块缓存刷写到磁盘 | 无参数（64 槽 LRU 写回缓存 + 每设备 FLUSH） |
| [virtio_blk_test](commands/virtio_blk_test.md) | virtio-blk 测试 | 无参数 |
| [write](commands/write.md) | 将 stdin 写入文件 | `write <file>`，here-tagin；EOF 结束 |

## display (22) — 显示 / CJK / TUI / IME

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [chinese](commands/chinese.md) | CJK 控制台演示 | 无参数 |
| [chinesetest](commands/chinesetest.md) | CJK 字体/编码测试组 | 无参数 |
| [clear](commands/clear.md) | 清屏 | 无参数 |
| [encoding](commands/encoding.md) | 控制台编码状态/设置 | 无参数 |
| encoding-test | 编码编解码器测试 | 无参数 |
| [font](commands/font.md) | 字体引擎状态 | 无参数 |
| font-test | 字体渲染器测试 | 无参数 |
| [ime](commands/ime.md) | 拼音 IME 状态 | ⚑ `ime -i` — 交互式组词行（可选开启，以保证脚本化串口会话保持纯净）；数字 1-9 选择，空格 = 第一个，PgUp/PgDn 或 `-`/`=` 翻页，Backspace 退一字，Enter 提交原始串，Esc 取消 |
| ime-test | IME 引擎 + 会话测试组 | 无参数（含词组/翻页/数字选择的协议断言） |
| [lang](commands/lang.md) | 显示/设置控制台语言 | `lang [en\|zh]` — 每个由 tr_() 驱动的界面实时切换 |
| [serial_encoding](commands/serial_encoding.md) | 串口线路编码 | ⚑ `serial_encoding [utf8\|gbk]` |
| [stats](commands/stats.md) | 显示管线统计 | 无参数 |
| [tui](commands/tui.md) | TUI 桌面 | 全屏；热键见 [ush-reference.md](ush-reference.md) §TUI |
| tui-drv | TUI 驱动页 | 全屏页面 |
| tui-ext | TUI 扩展页 | 全屏页面 |
| tui-hw | TUI 硬件页集 | 全屏页面（system/CPU、PCI、sound、storage、network） |
| tui-net | TUI 网络页 | 全屏页面 |
| tui-sys | TUI 系统页 | 全屏页面 |
| tui-sysinfo | TUI sysinfo 页 + 共享帮助屏 | 全屏页面 |
| tui-time | TUI 时钟页 | 全屏页面 |
| tui-test | TUI 自测组 | 经捕获路径的判定块 |
| [uname](commands/uname.md) | 系统标识 | ⚑ `uname -a` — hwinfo 向导弹调用的正是它 |

## nic (20) — NIC 服务 + 以太网驱动

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [bcm57xx](commands/bcm57xx.md) | Broadcom NIC 状态 | 无参数 |
| [e1000e](commands/e1000e.md) | Intel e1000e NIC 状态 | 无参数 |
| [igb](commands/igb.md) | Intel igb NIC 状态 | 无参数 |
| [ixgbe](commands/ixgbe.md) | Intel ixgbe NIC 状态 | 无参数 |
| [rtl810x](commands/rtl810x.md) | Realtek rtl810x NIC 状态 | 无参数 |
| [rtl8125](commands/rtl8125.md) | Realtek rtl8125 NIC 状态 | 无参数 |
| [rtl8139](commands/rtl8139.md) | Realtek rtl8139 NIC 状态 | 无参数 |
| [rtl8168](commands/rtl8168.md) | Realtek rtl8168 NIC 状态 | 无参数 |
| bcm57xx_test … rtl8168_test (8) | 每驱动自测 | 无参数 |
| [bt](commands/bt.md) | 蓝牙状态 | ⚑ 子词：`bt usb`（HCI-USB 传输）、`bt uart`（H4 UART）、`bt test`（流程自测） |
| [nic_rw_test](commands/nic_rw_test.md) | NIC 环回测试 | 无参数 |
| [other_nic_test](commands/other_nic_test.md) | 其他 NIC 驱动测试（3c59x/nForce/Atheros/Marvell） | 无参数 |
| [wifi](commands/wifi.md) | 无线状态/扫描 | ⚑ `wifi scan`（被动信标扫描；QEMU 中如实报告 `no radio`）、`wifi test`（核心 + 每家族自测）、`wifi sdio`（SDHCI 总线状态 + 自测） |

## fs (20) — 文件服务命令

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [cat](commands/cat.md) | 打印文件内容 | 无 flag |
| [cp](commands/cp.md) | 复制文件 | `cp <src> <dst>`（无同文件防护——见 GitHub Issues） |
| [df](commands/df.md) | 文件系统剩余空间 | 无参数 |
| [du](commands/du.md) | 文件空间占用 | `du [path]` |
| [edit](commands/edit.md) | 经典行编辑器界面 | 与 nano 完全相同（第二种拼写）；F2 = IME |
| [fatmount](commands/fatmount.md) | 挂载 FAT32 卷 | `fatmount [dev] [path]`；不会预先创建挂载点 |
| [fatstat](commands/fatstat.md) | FAT32 卷统计 | `fatstat [dev]` — 显示解析出的 BPB |
| [fstest](commands/fstest.md) | VFS 压力测试 | 无参数 |
| [grep](commands/grep.md) | 搜索文本 | stdin 过滤器，仅单一模式 — 无文件参数，无正则 flag |
| [ls](commands/ls.md) | 列出目录 | `ls [path]`；条目标注 `d`/`c`/`-`；**不做通配符展开** |
| [mkdir](commands/mkdir.md) | 创建目录 | `mkdir <path>` |
| [mount](commands/mount.md) | 挂载 / 列出挂载 | ⚑ `mount` 列出；`mount <type> <device> <path>` 挂载（会预先创建挂载点） |
| [mounts](commands/mounts.md) | 列出已挂载文件系统 | 无参数 |
| [mv](commands/mv.md) | 移动/重命名 | `mv <src> <dst>` |
| [pwd](commands/pwd.md) | 工作目录 | 无参数 |
| [rm](commands/rm.md) | 删除文件 | `rm <path>` |
| [rmdir](commands/rmdir.md) | 删除空目录 | `rmdir <path>` |
| [touch](commands/touch.md) | 创建空文件 / 更新 mtime | `touch <path>` |
| [tree](commands/tree.md) | 目录树 | `tree [path]`；深度上限 16，深度 ≥ 12 处显示 `…` |
| [umount](commands/umount.md) | 卸载 | `umount <path>` |

## input (18) — ACPI + 总线 + 芯片组

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [acpi](commands/acpi.md) | ACPI 表摘要 | 无参数 |
| [acpidev](commands/acpidev.md) | ACPI 设备树 | 无参数 |
| [acpipow](commands/acpipow.md) | ACPI 电源状态 | 无参数 |
| [acpitest](commands/acpitest.md) | ACPI 解析器测试 | 无参数 |
| [exc](commands/exc.md) | 异常报告状态 | 无参数 |
| [i2cdetect](commands/i2cdetect.md) | 扫描 I2C 总线 | 无参数 |
| [i2cget](commands/i2cget.md) | 读取 I2C 寄存器 | ⚑ `i2cget <bus> <addr> [reg]` — 全为十六进制 |
| [i2cset](commands/i2cset.md) | 写入 I2C 寄存器 | ⚑ `i2cset <bus> <addr> <reg> <val>` — 十六进制 |
| [i2ctest](commands/i2ctest.md) | I2C 总线测试 | 无参数 |
| [msitest](commands/msitest.md) | MSI 中断测试 | 无参数；在一块 e1000 上执行**真实 MSI 触发**（见 [hidden-commands.md](hidden-commands.md)） |
| [pcibars](commands/pcibars.md) | PCI BAR 分配检查 | 无参数 |
| [pcidump](commands/pcidump.md) | 转储 PCI 配置 | ⚑ `pcidump <bus:dev.fn>` 例如 `pcidump 0:1f.3`；无参数 = 摘要 |
| [pcie](commands/pcie.md) | PCIe 链路状态 | 无参数 |
| [smbus](commands/smbus.md) | SMBus 状态 | 无参数 |
| [smbustest](commands/smbustest.md) | SMBus 测试 | 无参数 |
| [timer](commands/timer.md) | 定时器测试 | 无参数 |
| [touchpad](commands/touchpad.md) | 触摸板状态 | 无参数 |
| [touchtest](commands/touchtest.md) | 触摸板测试 | 无参数 |

## snd (17) — 声音服务 + 音频驱动

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [sound](commands/sound.md) | 音频子系统状态（设备列表） | 无参数 |
| [hda](commands/hda.md) | Intel HD Audio 状态 | 无参数 |
| [ac97](commands/ac97.md) | AC'97 状态 | 无参数 |
| [sb16](commands/sb16.md) | SoundBlaster 16 状态 | 无参数 |
| [es1370](commands/es1370.md) | Ensoniq ES1370 状态 | 无参数 |
| [virtiosnd](commands/virtiosnd.md) | virtio-snd 状态 | 无参数 |
| [usbaudio](commands/usbaudio.md) | USB 音频状态 | 无参数 |
| hda_test / ac97_test / sb16_test / es1370_test / virtio_snd_test / usb_audio_test (6) | 每驱动播放自测 | 无参数 |
| [audio_rw_test](commands/audio_rw_test.md) | PCM 读写环回测试 | 无参数 |
| [sample_rate_test](commands/sample_rate_test.md) | 采样率精度测试 | 无参数（如实给出 QEMU 44.1 kHz 的限制说明） |
| [play](commands/play.md) | 播放 WAV 文件 / 测试音 | ⚑ `play [device] [rate]` — rate 4000..96000；裸调用 = 440 Hz 音 |
| [volume](commands/volume.md) | 混音器音量控制 | ⚑ `volume [device] [0-100]` — 服务端做钳制 |

## ush (16) — shell 工具带（文本/进程/文件信息）

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [awk](commands/awk.md) | 模式扫描 | 仅支持 `{print ...}` 脚本；否则 `awk: missing {print ...}` |
| [chmod](commands/chmod.md) | 更改文件权限 | `chmod <mode> <path>`（八进制） |
| [chown](commands/chown.md) | 更改文件属主 | `chown <owner> <path>` |
| [date](commands/date.md) | 来自 RTC 的日期/时间 | 无参数；回退时打印 `ms since boot (no RTC driver)` |
| [free](commands/free.md) | 内存使用摘要 | 无参数 |
| [head](commands/head.md) | 文件的前几行 | `head <file>` |
| [ln](commands/ln.md) | 创建链接 | ⚑ `ln -s <abs-target> <linkpath>` — 符号链接（目标必须为绝对路径）；默认 = 硬链接 |
| [nano](commands/nano.md) | nano 风格编辑器 | F2 切换中文 IME；完整按键表见 [ush-reference.md](ush-reference.md) |
| [sed](commands/sed.md) | 流编辑器 | ⚑ `sed s/old/new/[g]`、`sed -n Np`、`sed Nd` — 替换 / 打印第 N 行 / 删除第 N 行，作用于 stdin 或可选的尾随文件 |
| [sort](commands/sort.md) | 行排序 | stdin → stdout |
| [stat](commands/stat.md) | 文件元数据 | `stat <path>` |
| [tail](commands/tail.md) | 文件的最后几行 | `tail <file>` |
| [top](commands/top.md) | 实时进程总览 | ⚑ `top -n N` — 刷新 N 次（钳制 1..10），间隔 1 s；**不在 usage 文本中** |
| [uniq](commands/uniq.md) | 去除相邻重复行 | stdin → stdout |
| [vi](commands/vi.md) | 模式化 vi 编辑器 | i/a/o 插入，dd/yy/p，`:w`/`:q`/`:wq`/`:q!`；F2 = IME |
| [wc](commands/wc.md) | 统计行/词/字节 | `wc <file>` |

## ota (16) — 更新 / A-B / 配置

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [ab_partition_test](commands/ab_partition_test.md) | A/B 分区测试 | 无参数 |
| [checkupdate](commands/checkupdate.md) | 检查 OTA 更新 | 无参数；`Current version is up to date.` 或 `New version available.` |
| [checkupdate_test](commands/checkupdate_test.md) | OTA 检查测试 | 无参数 |
| [config](commands/config.md) | 系统配置存储 | ⚑ `config list` \| `config get <key>` \| `config set <key> <value>` \| `config restore` \| `config path` |
| [config_test](commands/config_test.md) | 配置后端测试 | 无参数 |
| [real_update_test](commands/real_update_test.md) | 完整真实 OTA 流程测试 | 无参数 |
| [reboot](commands/reboot.md) | 重启机器 | 无参数（标签为 `ota`，不是 `power`） |
| [rollback](commands/rollback.md) | 回滚到另一个 A/B 槽位 | 无参数 |
| update_check_test / update_download_test / update_install_test / update_local_test / update_pkg_test / update_rollback_test / update_status_test / update_verify_test (8) | 更新单元测试家族 | 无参数 |

## net (16) — 网络服务

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [arp](commands/arp.md) | ARP 表（只读，16 条目） | 无参数 |
| [dhcp](commands/dhcp.md) | 获取 DHCP 租约 | 无参数；打印 `DHCP success: IP=...` 锚点；超时非零退出 |
| [dns](commands/dns.md) | 解析主机名 | ⚑ `dns <name> [aaaa\|cname\|mx\|txt\|ns\|srv]` — 记录类型选择器可选 |
| [dnstest](commands/dnstest.md) | DNS 解析器测试组 | 无参数（内建向量含 10.0.2.2/update.json 固定件） |
| [firewall](commands/firewall.md) | 包过滤规则 | ⚑ `firewall`（列表） \| `firewall add [in\|out\|fwd] <drop\|accept\|reject> <tcp\|udp\|icmp\|any> [port <n>] [src <ip>] [dst <ip>] [state <new\|est\|any>]` \| `firewall del <n>` \| `firewall policy <in\|out\|fwd> <accept\|drop>` \| `firewall ct`（conntrack） \| `firewall reset` |
| [ifconfig](commands/ifconfig.md) | 接口状态（只读） | 无参数 |
| [ip](commands/ip.md) | IP 配置 | ⚑ `ip` 显示；`ip <ip> <mask> <gw>` 设置 |
| [lspci](commands/lspci.md) | 列出 PCI 设备（标签 `net`） | 无参数 |
| [netstat](commands/netstat.md) | 套接字/连接统计 | 无参数 |
| [nf_test](commands/nf_test.md) | netfilter 测试 | 无参数 |
| [ping](commands/ping.md) | ICMP 回显 | `ping <host\|ip>`；先做 DNS 解析；4 次探测 @ 3000 ms |
| [route](commands/route.md) | 路由表 | ⚑ `route` 显示；`route add <dst> <mask> <gw>`；`route del <dst> <mask>` |
| [tcpcc_test](commands/tcpcc_test.md) | TCP 拥塞控制测试 | 无参数 |
| [tcpstats](commands/tcpstats.md) | 每连接 TCP 统计 | 无参数（cwnd/ssthresh/rto/sack/...） |
| [tcptest](commands/tcptest.md) | TCP 环回测试 | 无参数 |
| [wget](commands/wget.md) | 通过 HTTP(S) 下载 | ⚑ `wget https://...` → TLS（默认端口 443，否则 80）；`wget <http://host:port/path>` URL 形式；遗留 3 参数形式 `wget <host> [port] [path]` |

## camera (10) — 摄像头 + 遗留端口（历史标签名）

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [gameport](commands/gameport.md) | 游戏端口状态 | 无参数 |
| [gameporttest](commands/gameporttest.md) | 游戏端口测试 | 无参数 |
| [ir](commands/ir.md) | 红外接收器状态 | 无参数 |
| [irtest](commands/irtest.md) | IR 测试（NEC/RC5/RC6/SIRC） | 无参数 |
| [parallel](commands/parallel.md) | 并行端口状态 | 无参数 |
| [paralleltest](commands/paralleltest.md) | 并行端口测试 | 无参数 |
| [scsi](commands/scsi.md) | SCSI 设备扫描 | 无参数 |
| [scsitest](commands/scsitest.md) | SCSI 测试 | 无参数 |
| [uvc](commands/uvc.md) | UVC 摄像头状态 | 无参数 |
| [uvctest](commands/uvctest.md) | UVC 摄像头测试 | 无参数 |

## usb (9) — USB 服务

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [usb](commands/usb.md) | USB 主机状态 | 无参数 |
| [usbdev](commands/usbdev.md) | USB 设备详情 | 无参数 |
| usb_core_test / usb_hotplug_test / usb_hub_test / usb_kbd_test / usb_mouse_test / usb_serial_test / usb_storage_test (7) | USB 测试家族 | 无参数 |

## power (8) — 电源服务

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [halt](commands/halt.md) | 停机 CPU | 无参数 |
| [poweroff](commands/poweroff.md) | 关机（ACPI） | 无参数 |
| [shutdown](commands/shutdown.md) | 关机（第二种拼写） | 无参数 |
| [sleep](commands/sleep.md) | 挂起 N 秒 | ⚑ `sleep <seconds>` — 允许小数值（`sleep 0.5`） |
| halt_test / poweroff_test / reboot_test / suspend_test (4) | 电源路径探针 | 各自执行真实路径且可能终结会话 — 电源向导会警告 + 确认 |

## crypto (4) — 加密 + ssh

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [cryptotest](commands/cryptotest.md) | 加密原语测试（SHA-256、AES-128、HMAC） | 无参数 |
| [dhtest](commands/dhtest.md) | Diffie-Hellman 测试 | 无参数 |
| [ssh](commands/ssh.md) | SSH 客户端 | ⚑ `ssh <ip\|host> [port=2222] [user=oc] [password=oc]`；密码 **`-` = 公钥认证**，使用 `/etc/ssh_client_key`（密钥缺失时回退到密码） |
| [sshd](commands/sshd.md) | SSH 服务器 | ⚑ `sshd [port]` — 默认 22；同时仅一个会话 |

## recovery (7) — 杂项历史命令

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [crashtest](commands/crashtest.md) | 崩溃处理测试触发器 | ⚑ `crashtest panic\|exc\|pf\|assert\|wd\|ovf\|dump\|policy [halt\|reboot]` — 8 种模式（见 [hidden-commands.md](hidden-commands.md)） |
| [dumpinfo](commands/dumpinfo.md) | 核心转储状态 | 无参数（缓冲区大小、使用量） |
| [echo](commands/echo.md) | 打印参数 | 支持 `$VAR` 展开（ush 的 expand_env 先行运行） |
| [help](commands/help.md) | 内建 + 注册表列表 | ⚑ `help -a` / `--all`，`help -w` / `--wp` |
| help_a_test / help_default_test / help_wp_test (3) | 注册表列表不变量测试 | 无参数（见 [hidden-commands.md](hidden-commands.md)） |

## watchdog (5) — klog + watchdog + health

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [dmesg](commands/dmesg.md) | 内核日志 | ⚑ `dmesg [N]` — N 行，默认 30，上限 1024；页脚 `-- <shown> lines (ring <n>/1024, dropped <d>) --` |
| [health](commands/health.md) | 健康快照 | ⚑ `health watch`（实时监视直到 ENTER）/ `health export [path]` |
| [logcat](commands/logcat.md) | 过滤后的内核日志流 | ⚑ `logcat [-l <level>] [-m <module>] [-k <keyword>] [-t <seconds>] [-n <count 1..1024>] [-f]` — `-f` 跟随实时输出（ENTER 退出） |
| [logctl](commands/logctl.md) | 内核日志级别控制 | ⚑ 11 个子命令：`status\|level [-1..4]\|enable\|disable <level>\|clear\|flush\|rotate\|threshold <bytes>\|export [path]\|filter <clear\|[level] [module] [keyword]>\|test` |
| [watchdog](commands/watchdog.md) | watchdog 控制 | ⚑ `watchdog [status\|slots\|hw <status\|start\|stop\|feed\|timeout <2..63s>>\|start\|stop\|feed <id>\|timeout <id> <ms 100..3600000>\|register <name> <ms> <0=panic 1=reboot 2=kill 3=log>\|test <kill\|panic\|log>]` |

## time (4) — 时钟源

| 命令 | 用途 | 隐藏 / 扩展语法 |
|---------|---------|--------------------------|
| [hpet](commands/hpet.md) | HPET 定时器状态 | 无参数 |
| [pmtimer](commands/pmtimer.md) | ACPI PM 定时器状态 | 无参数 |
| [rtc](commands/rtc.md) | CMOS RTC 状态 | 无参数 |
| [tsc](commands/tsc.md) | TSC 计数器状态 | 无参数 |

---

## 参数如何到达其语法

上面大多数命令是服务 op 之上的薄封装，其参数处理方式
解释了几个令用户意外的行为：

- 标准应用骨架（`l1_lib.h`、`l1svc_main`）将 `argv[1..]`
  以单空格连接成一条 **256 字节参数行**，
  通过 `sys_l1svc(cat, op, arg, out, 32768)` 发送，
  并打印结果。因此多词语法（`firewall add in drop tcp port 22`）
  按输入原样工作 —— 但超过 256 字节的部分会丢失。
- 输出缓冲是静态 **32 KiB**。`help -a` 的完整注册表
  列表（约 12 KiB）放得下；栈截断变体是很久前已修复的
  bug。
- rc 非零时封装打印 `<app>: service call failed (rc=-1)` — 300 个应用中约 150 个共享的这一模板（该
  i18n 键总计被 250 个应用共享）。看到它时，真实情况在
  `dmesg` 里：统一排查见
  [../troubleshooting/error-catalog.md](../troubleshooting/error-catalog.md)
  §7.1。
- 少数命令不是封装：`files` 和 `run` fork+execve 真实
  程序；`ush`、各编辑器以及平坦的 `l1` 探针自己做
  系统调用。
- `dmesg` 连接其 argv 时不加空格（单一 token）—— 出于
  历史兼容保留的怪癖：始终用 `dmesg 100`，绝不要
  `dmesg -n 100`。

## 命令名怪癖

值得了解的已冻结历史拼写：

| 怪癖 | 解释 |
|-------|-------------|
| `edit` == `nano` | 逐字节相同的界面；`edit` 是 v2.2.0 拼写 |
| `shutdown` == `poweroff` | 两者拼写同一个电源 op |
| `rtl8168` 内部打印 `rtl8169: ...` | 驱动家族名；命令界面是 `rtl8168` |
| `mounts` == 裸 `mount` | 两者都是列出；只有 `mount <type> <dev> <path>` 执行挂载 |
| `reboot` 标签为 `ota`，`reboot_test` 为 `power` | 已冻结的类别标签，无语义 |
| `lspci` 标签为 `net` | 它共享 net 家族的 op 编号 |
| `ssh`/`sshd` 标签为 `crypto` | 加密服务拥有它们的 op |
| ACPI/I2C/PCI/MSI 工具标签为 `input` | 历史的"芯片组/输入"桶 |
| SCSI/gameport/parallel/IR 标签为 `camera` | 遗留端口桶 |
| commands/ 中是 `mkfs-fat32.md` 而提示符下是 `mkfs.fat32` | 文档树避免文件名中的点 |
| `help` 既存在为内建也存在为应用 | 相同的 12 行 + 注册表计数，相同 flag |

## mega 应用菜单清单

作为参考，各向导共暴露 **113 个单键菜单项**：
update 5 步、disk-tool 7+3+5+3 = 18、net-tool 9+4+4+4 = 21、
sound-tool 5+6+3 = 14、files 8、sysdiag 7+4+5 = 16、hwinfo 8+4+4+6 =
22、power 5+4 = 9 —— 外加 **155 个逐字 op/程序门**（30 + 16 +
5 + 17 + 10 + 46 + 26 + 9 + update 的 1）。每条破坏性向导路径
都使用"重新输入设备名 + 裸 `y`"双重确认；classic 与
`direct` 分发按设计跳过提示（脚本保持历史语义）。
`sysdiag` 刻意不让 `crashtest` 出现在其菜单上。

## 12 个 ush 内建命令

| 内建 | 用途 | 隐藏 / 注意 |
|----------|---------|------------------|
| `cd [dir]` | 切换目录（shell 状态） | 无参数 = `/` |
| `help [-a\|-w]` | 帮助文本 | ⚑ `-a`/`--all`、`-w`/`--wp` |
| `export VAR=val` | 设置环境变量 | 32 个变量，名 32 / 值 128 字节 |
| `alias name=val` | 设置命令别名 | 裸 `alias` 列出；`alias name` 打印单个；也支持 `alias name value...` |
| `unalias name` | 删除别名 | 无参数调用时报 usage 错误 |
| `env` | 显示环境变量 | |
| `set` | 显示一个变量 | `set VAR` 打印 `VAR=value` |
| `history` | 命令历史 | 16 条，每条 256 字节 |
| `jobs` | 列出后台作业 | 16 个作业槽 |
| `fg [job]` | （被识别，拒绝执行） | 打印 `ush: job control requires external programs` |
| `bg` | （被识别，拒绝执行） | 相同消息 |
| `exit` | （被识别，拒绝执行） | `ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart` |

你可能记得的其他所有"内建"（`ls`、`cat`、`ps`、`kill`、
`nano` ...）现在都是经 fork+execve 到达的注册表应用。

---

## 隐藏参数总清单

`help` 提示从不展示的一切，集中在一处（35+ 个面）：

1. `help -a` / `--all` / `-w` / `--wp`（ush 内建与该应用皆可）
2. `top -n N` — 刷新次数（1..10）
3. `ln -s <abs-target> <linkpath>` — 符号链接
4. `sed s/old/new/[g]`、`sed -n Np`、`sed Nd`、可选尾随文件
5. `p2test utf8|read0|pipe` — 探针选择器
6. `health watch` / `health export [path]`
7. 8 个 mega 应用的 `direct` 关键字
8. `update status` / `--status` / `--local <path>` / `local <path>` / 裸在线流程
9. `crashtest` 8 种模式 + `policy halt|reboot`
10. `watchdog` 完整子命令语法（status/slots/hw/start/stop/feed/timeout/register/test）
11. `logctl` 11 个子命令
12. `logcat -l -m -k -t -n -f`
13. `dmesg [N]`
14. `wifi scan|test|sdio`
15. `bt usb|uart|test`
16. `dns <name> [aaaa|cname|mx|txt|ns|srv]`
17. `wget https://...`（TLS）与 3 参数形式
18. `ssh` 密码 `-` = 公钥；`sshd [port]`
19. `firewall add/del/policy/ct/reset` 语法
20. `route add/del`
21. `ip <ip> <mask> <gw>` 设置形式
22. `config list|get|set|restore|path`
23. `play [device] [rate 4000-96000]`、`volume [device] [0-100]`
24. `sleep` 小数秒
25. `uname -a`
26. `serial_encoding [utf8|gbk]`
27. `ime -i`
28. `i2cget <bus> <addr> [reg]`、`i2cset <bus> <addr> <reg> <val>`（十六进制）
29. `pcidump <bus:dev.fn>`
30. `nice <tid> <prio 0..31>`
31. 块设备参数：`fsck/abdisk/install/grub-install/disk_rw_test/partition_test/fs_mount_test <device>`；`mount <type> <device> <path>`；`umount <path>`；`fatmount [dev path]`
32. `ldd <program>`
33. `parted [device]` 可选参数
34. `echo` 的 `$VAR` 展开
35. 约 93 个测试桩（都可裸运行 — [hidden-commands.md](hidden-commands.md)）

## help 不会强调的内容

- **上述 35 个隐藏面没有一个出现在注册表提示里。**
  `help -a` 为每个程序打印一行提示；子命令语法只能通过
  带一个错误参数运行该命令（多数会打印 `usage:` 行）或
  阅读 [commands/](commands/index.md) 看到。
- **测试桩**（约 93 个程序）像其他命令一样列出 ——
  没有任何标记表明它们是测试。它们可以安全运行，但少数
  刻意制造破坏（`crashtest`、电源路径探针、
  `stackovf`、`badapp`）；见 [hidden-commands.md](hidden-commands.md)。
- `help -w` 中的类别标签是历史产物，非语义（见上文
  怪癖说明）。
- `fg`/`bg` 出现在内建表中但**未实现**
  （它们打印拒绝消息）；`jobs` 与 `cmd &` 是真实的。
- 提示文本本身是双语的（每个应用的
  `i18n/{en,zh}.json` 中的 `_hint` 键，构建时消费）；
  缺失翻译是构建违规，因此你永远不会看到原始键作为提示。

## 运行时发现语法

当你无法查阅本手册时：

1. 带一个故意错误的参数输入该命令 — 多数语法会
   打印其 `usage:` 行（`crashtest x`、`watchdog x`、`logctl x`、
   `firewall x`、`config x`）。
2. 先 `help -w` 找到家族，再用该家族的状态命令
   （裸调用几乎总是状态形式）。
3. 向导兼任语法教师：它们提出的问题与位置参数语法
   一一对应（net-tool 的静态 IP 路径按 `ip <a> <m> <g>`
   的确切顺序询问地址 → 掩码 → 网关）。

## 相关页面

- [ush-reference.md](ush-reference.md) — shell、行编辑器、管道、
  重定向、编辑器与 TUI 按键表
- [hidden-commands.md](hidden-commands.md) — 约 93 个测试桩、
  3 个 help 门、msitest 的真实 MSI 注入
- [mega-apps.md](mega-apps.md) — 8 个向导应用的菜单
- [../troubleshooting/error-catalog.md](../troubleshooting/error-catalog.md) —
  这些命令中任何一个失败时该怎么办
