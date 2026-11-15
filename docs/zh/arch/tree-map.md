<!-- SPDX-License-Identifier: Apache-2.0 -->
# 目录树地图：每个目录是什么，以及它为何存在

对仓库的逐层解读。下面每个目录都以一段文字回答两个问题：**里面
有什么**、**它为何存在**。四个组件的系统级视图见
[system-map.md](system-map.md)；本页是其在文件树一侧的对应物。

```
ocos/
  Makefile  include/  etc/  evidence/  website/  docs/  tools/  tests/
  .github/  grub.cfg  grub-ab.cfg
  ocbl/   <- 引导加载器            (BIOS + UEFI 两种形态)
  ocire/  <- 恢复内核              (独立 L0 对等体)
  ocse/   <- 安装环境              (自 v1.0.0 vendor 内嵌)
  ocos/   <- 主系统                (内核 + 驱动 + 服务 + L1 应用)
```

## 根目录

| 条目 | 它是什么 / 为何存在 |
|---|---|
| `Makefile` | WP-13 构建组合根。`OC_RELEASE_VERSION ?= 3.0.0-NAK`（第 90 行）与 `OC_KERNEL_BUILD ?= git rev-list --count HEAD + 21`（第 103-104 行）是每个组件共同消费的唯一版本源（见 [system-map.md](system-map.md)）。目标：`make`/`kernel`（ELF）、`all`（内核 + ISO + 源码 zip + OTA）、`iso`、`run-bios`/`run-uefi`/`shot-*`、`reproducible-check`、`dist`、`clean`。按依赖顺序包含各组件 makefile：`ocire/ocire.mk` -> `ocbl/ocbl.mk` -> `ocse/ocse.mk`。还承载 `ARCH` 分派（默认 `x86_64`；`arm64`/`riscv` 仅构建 bring-up 外壳）。 |
| `ARCHITECTURE.md` | 根级架构声明：两个环（L0 提供服务 / L1 使用服务）、组件清单、指向 `docs/data/stats.json` 的指针（由 `make api-docs` 重新生成）。 |
| `README.md`、`BUILDING.md`、`CONTRIBUTING.md`、`FAQ.md` | 标准门面文档（同时镜像于 `docs/root/` 之下）。 |
| `INTERFACES.md` | 历史接口登记册（编号式 L1 扩展面 1-138 的时代）；现行的 ring 3 契约是 `ocos/interface/`（[private-headers.md](private-headers.md)）。 |
| `KNOWN_ISSUES.md` | 已声明的、有意为之的限制（例如“#DF panic 按设计保持最小”）。 |
| `bug-list.md` | 3.0.0 缺陷普查：141 个 GitHub issue + WP-06h 审计发现，为修复工作包导出。 |
| `VERIFICATION_REPORT.md`、`MANIFEST.md`、`ARCHIVE-README.md` | 发布证据：验证笔录、交付文件清单，以及归档材料的指引 readme。 |
| `LICENSE`、`NOTICE` | Apache-2.0 文本与署名声明。 |
| `build.ps1` | Windows PowerShell 构建包装器（主路径是 Makefile）。 |
| `grub.cfg` | **可移动介质/ISO 介质**的 GRUB 菜单（前 OCBL 时代，仍随 GRUB 引导的介质交付）：加载 gfxterm/font/serial 模块栈，实现 `(hd0)` 上的 A/B 标志选择，提供槽位 A/B/ISO 条目，并把内核 ELF 作为 `self` 模块附加。 |
| `grub-ab.cfg` | 同一套选择逻辑的**已安装 A/B 磁盘**变体——极简（无 gfxterm、无 ISO 回退），每次更新安装都会刷新为分区 1 上的 `/boot/grub/grub.cfg`。两个文件并存是因为两种介质需求不同，但二者实现逐字节相同的 `next_B`/`bootfail_B`/`ok_B` 标志语义——与 OCBL 原生扫描器所实现的是同一契约（第三份实现）。GRUB 本体在 3.0.0-WP05 B5-7 离开了发布 ISO；它作为遗留磁盘契约存活，并作为 `install`/`abdisk`/`grub-install` 命令的内核内嵌引导数据（`tools/embed_grub.py`）。 |
| `include/` | 恰好一个文件：`ocbl_state.h`——WP07a 的独立（freestanding）引导状态契约，由 OCBL、OCOS 与 OCIRE 逐字节共享（魔数、阶段枚举、`ocst_state_t`/`ocst_record_t` 布局、预算值）。它位于仓库根部，因为三个顶层树都要 include 它，而任何一个都不许拥有它。 |
| `etc/` | 烧入镜像的主机侧配置输入：`opencube.conf`（默认的 `/etc/opencube.conf`：update_url、auto_check、package_url、log.level、serial_encoding、pmtimer.width……）以及 `i18n/l0/{en,zh}.json`——由 `tools/gen_l0_i18n.py` 编译进 `ocos/kernel/core_lib_i18n_data.c` 的 L0 侧 i18n 表。 |
| `evidence/` | 冻结的验收笔录：`help_a_runtime.txt`（实测捕获的完整 `help -a` 注册表清单）与 `v1_command_list.txt`（为对等检查保留的 v1.0.0 命令清单）。 |
| `website/` | 项目站点（`index.html`、`about.html`、`architecture.html`、`download.html`、assets、CNAME）外加 `public/downloads/`——系统内 `checkupdate` 实际抓取的更新工件，以及 `public/update.json` / `update-v2.json`（OTA 清单）。 |
| `docs/` | 文档树（见下）。 |
| `tools/` | 117 个构建/测试/维护文件（见下）。 |
| `tests/` | 48 个主机侧测试文件（见下）。 |
| `.github/workflows/build.yml` | CI：在零警告门禁（`-Wall -Wextra -Werror`，由每个组件的 makefile 强制执行）下构建完整的 WP-13 流水线（`make all`），校验工件，并检查 `make clean` 不留残余。 |

## `docs/`——文档树

| 子树 | 内容 |
|---|---|
| `docs/arch/` | 本套：系统级深度解析（你正在阅读的页面，加上 [boot-flow.md](boot-flow.md)、[memory.md](memory.md)、[scheduler.md](scheduler.md)、[interrupts.md](interrupts.md)、[syscall-ring3.md](syscall-ring3.md)、[vfs.md](vfs.md)、[network-stack.md](network-stack.md)、[driver-model.md](driver-model.md)、[tui-engine.md](tui-engine.md)、[chinese-layer.md](chinese-layer.md)、[crash-recovery.md](crash-recovery.md)、[ota-ab.md](ota-ab.md)、[hidden-mechanisms.md](hidden-mechanisms.md)、[private-headers.md](private-headers.md)）。 |
| `docs/l1/` | ring 3 开发者视图：`overview.md`、`writing-apps.md`、`app-anatomy.md`、`surface-map.md`、`wizard-library.md`、`solib-loader.md`、各领域驱动指南、`watchdog-health.md`、`ota.md`、`tui-ext.md`。 |
| `docs/drivers/` | 按家族的驱动页面：`pci-pcie.md`、`acpi.md`、`storage.md`、`network.md`、`wireless.md`、`usb.md`、`sound.md`、`input.md`、`i2c-smbus.md`、`time.md`、`bluetooth.md`、`sd-camera.md`、`misc-scsi.md`、`display.md`。 |
| `docs/internals/` | 机制专著：`klog.md`、`crash-dump.md`、`watchdog.md`、`health.md`、`ota-ab.md`、`drbg-crypto.md`。 |
| `docs/manual/` | 用户手册（外加 `commands/` 下每命令一页），包括从用户侧映照 arch 页面的 `boot-modes.md` 与 `recovery.md`。 |
| `docs/dev/` | 贡献者文档：`build.md`、`environment.md`、`coding-style.md`、`portability.md`、`host-tests.md`、`e2e-suites.md`、`qemu-testing.md`、`debugging.md`、`pr-workflow.md`、`ocire.md`。 |
| `docs/release/` | 发布流程、版本管理、逐版说明、网站更新与 wiki 同步操作手册。 |
| `docs/troubleshooting/` | 按症状组织的页面：引导失败、QEMU 调试、网络/存储/常见错误。 |
| `docs/root/` | 随镜像交付的根级文档副本。 |
| `docs/archive/` | 历史上的 EXTENSIONS_WP*.md 工作包报告、CONFIG/STATUS/WORK_LOG 快照——目录树走到今天的书面轨迹。 |
| `docs/data/` | 机器生成的文档输入：`stats.json`（按目录的行数统计）、`commands.json`、`interfaces.json`——由 `tools/gen_api_docs.py`（`make api-docs`）产出。 |
| `docs/zh/` | manual/arch/l1/internals/troubleshooting 的完整中文镜像。 |

## `ocos/`——主系统

### `ocos/kernel/`——L0 内核（扁平，55 个条目 + `arch/` + `generated/`）

命名法则：每个文件都是 `core_<area>_<name>.(c|h)`，全部位于同一个
扁平目录——历史上的 `kernel/core/`、`kernel/mem/`、`kernel/lib/`
路径仅存活于 "File:" 头部注释中。各区域：

| 前缀 | 文件 | 区域职责 |
|---|---|---|
| `core_boot_*` | `entry.S`、`longmode.S`、`mb2header.S`、`main.c`（751 行，kmain 组合根）、`state.c`（引导状态监视器，内核侧）、`grubstub.c`/`grubdata.h`（两阶段 GRUB 内嵌）、`selftest.c`（L1 扩展金丝雀）、`l1launch.c`（拉起 ush） | 引导：multiboot2 入口、32->64 过渡、手工编排的 bring-up 序列 |
| `core_mm_*` | `pmm.c`（位图 PMM）、`vmm.c`（四级页表）、`heap.c`（空闲链表堆） | 内存管理——[memory.md](memory.md) |
| `core_sched.c` | | 32 槽位抢占式优先级+RR 调度器——[scheduler.md](scheduler.md) |
| `core_sync.c` | | 自旋锁/信号量/互斥体/条件变量 |
| `core_timer.c` | | PIT @ 100 Hz、毫秒节拍、16 槽位软定时器 |
| `core_usermode.c` | | 系统调用表 + `int 0x80` 分派、ELF64 加载器（静态 + ET_DYN + ASLR）、fork、信号、统一收割器——[syscall-ring3.md](syscall-ring3.md) |
| `core_kcap.c` | | 内核输出捕获——L1SVC 文本面的头号承重路径（[hidden-mechanisms.md](hidden-mechanisms.md)） |
| `core_registry.c` | | `.oc_apps` L1 应用注册表收集器 |
| `core_console_*` | `fb.c`、`font.c`、`font_data.c`（由 `tools/gen_font.py` 生成）、`screen.c` | framebuffer 原语、8x16 字体、带唯一输出钩子槽位的文本网格控制台 |
| `core_lib_*` | `config.c`（/etc/opencube.conf 存储）、`gzip.c`（RFC 1951/1952 inflate）、`i18n.c`+`i18n_data.c`（L0 i18n）、`klog.c`（4 通道内核日志）、`log.c`（引导阶段日志器）、`string.c`、`tar.c` | 独立（freestanding）运行时库 |
| generated | `userprogs_data.h`（2.7 万行：**284 个 gzip 压缩的 ring 3 程序** + `OC_USERPROGS[]` 注册表，来自 `tools/gen_userprogs.py`）、`solib_data.h`（内嵌 ld.so + libfoo.so） | 构建生成的载荷 |

`ocos/kernel/arch/x86_64/` 是真正的架构目录：`arch_idt.*`、
`arch_exceptions.*`、`arch_irq.*`、`arch_lapic.*`、`arch_pic.h`、
`arch_multiboot2.*`、`arch_context_switch.S`、`arch_enter_ring3_fork.S`、
`arch_exc_exit.S`（IST2 退出栈）、`arch_abi_probe.S`、`arch_acpi_s3*.S`、
`linker.ld`（含 `.oc_drvs/.oc_svcs/.oc_apps` 链接段边界与
`__end_of_kernel <= 0x400000` ASSERT）。`arch/riscv/` 与 `arch/arm64/`
是 WP-13 bring-up 外壳（UART 横幅 + WFI 心跳，停放次要 hart）——
不是完整内核，且如实标注。`kernel/generated/` 存放 `version_gen.h`
（`OC_VERSION_STAMP` 重编译触发器）、`config_gen.h`
（`OC_CONFIG_NVME/AHCI/VIRTIO_BLK` WP-13 配置门）和
`grub_boot_data.c`（1 MB 的内嵌 GRUB 第二阶段载荷）。

### `ocos/drivers/`——11 个条目：9 个类别目录 + 框架一对

| 目录 | 其中声明的家族（经 `OC_DRIVER_DECL`） |
|---|---|
| `drv_framework.{c,h}` | 自注册机制本体：`.oc_drvs` 链接段、`drv_framework_collect()` 收进 256 槽位表——[driver-model.md](driver-model.md) |
| `storage/` | block：`ata`、`ata_dma`、`ahci`、`nvme`、`virtio_blk`、`scsi`（+ SDHCI 主机 + LSI 53C895A 后端） |
| `net/` | nic：`e1000e`、`igb`、`ixgbe`、`rtl8168`、`rtl8125`、`rtl810x`、`rtl8139`、`bcm57xx`、`other`；wireless：`iwlwifi`、`ath9k/10k/11k`、`b43`/`brcmfmac`、`rtlwifi`；sdio：`brcmfmac-sdio` |
| `usb/` | usb：`hid-kbd`、`hid-mouse`、`usb-msc`、`cdc-acm`、`ftdi-serial`、`usb-audio`；四个主机控制器（UHCI/OHCI/EHCI/XHCI）也在此 |
| `audio/` | snd：`hda`、`ac97`、`es1370`、`sb16`、`virtio` |
| `bus/` | PCI/PCIe（CF8/CFC + ECAM + MSI/MSI-X）、ACPI（+ 2077 行的 AML 解释器）、i2c/i801/smbus/DesignWare |
| `input/` | PS/2 键盘、COM1 串口 RX 桥（带 ANSI CSI 解码器）、i2c-hid 触摸板 |
| `time/` | HPET、TSC、ACPI PM 定时器、CMOS RTC（PIT 位于内核核心） |
| `misc/` | 蓝牙协议栈（HCI/H4/USB + L2CAP/RFCOMM/SDP/SPP/HID）、摄像头核心 + UVC + MJPEG 解码器、红外（NEC/RC-5/RC-6/SIRC）、并口、游戏口，以及 `drv_misc_placeholder.c`——对注册机制的有意实测 |
| `display/` | 只有一个 README 标记：为未来 GPU 驱动保留的 NAK 槽位（当前显示流水线是 `ocos/kernel/core_console_*` 中的固件 framebuffer 控制台） |

### `ocos/services/`——24 个家族 + `svc_framework.{c,h}`

每个目录拥有一个 L1SVC 类别（或是仅内核机制的属主）。框架
（`OC_SERVICE_DECL`/`OC_SERVICE_DECL_L1` 把描述符放进 `.oc_svcs`，
`svc_framework_collect()` 收进 64 条目表）文档见
[hidden-mechanisms.md](hidden-mechanisms.md)；完整的类别/op 编号见
[syscall-ring3.md](syscall-ring3.md)。家族及其类别：`time`(1)、
`block`(2)、`net`(8) + `nic`(3)、`audio`->`snd`(4)、`power`(5)、
`selfhost`->`ota`(6)、`fs`(7)、`health`(9)、`klog`(10)、`watchdog`(11)、
`crypto`(12)、`l1`(13)、`display`(14) + `input`(15)、`camera`(16)、
`usb`(17)、`ocembed`->`ocire`(18)、`diag`(19)、`tui`(20)——外加仅
内核的机制属主 `crash`、`encoding`、`font`、`ipc`、`loader`、
`sysapi`。220 个文件，约 6.7 万行；ring 3 面总计 20 个类别 208 个
op。

### `ocos/interface/`——8 个私有的 ring 3 头文件

`crt0.asm`、`oc_l1_api.h`、`l1_lib.h`、`l1svc_ops.h`、`fs_lib.h`、
`i18n.h`、`wizard_lib.h`、`editor_core.h`——ring 3 应用程序唯一可以
include 的头文件集（由 L1 编译 `-I` 路径在构建期强制）。专门页面：
[private-headers.md](private-headers.md)。

### `ocos/l1apps/`——284 个内嵌 ring 3 程序

一条命令 = 一个应用 = 一个目录，内含 `main.c` +
`i18n/{en,zh}.json`（252 个目录，其中 `_i18n/` 存放共享表；注册表
计数为 `OC_USERPROGS_COUNT = 284`）。目录树中可见的类别：`ush`
shell 及其内建命令；按设备的状态命令（`ahci`、`hda`、`e1000e`、
`wifi`……）；`*_test` 测试组（50+）；构建于 `wizard_lib.h` 之上的
8 个合并巨型应用（`update`、`disk-tool`、`net-tool`、`sound-tool`、
`files`、`sysdiag`、`hwinfo`、`power`）；编辑器（`nano`、`vi`、
`edit` 共享 `editor_core.h`）；文本工具（`cat`、`grep`、`sed`、
`awk`、`sort`、`wc`……）。另有约 34 个扁平的顶层源文件：遗留的
`.asm` 测试程序（`fork_test.asm`、`g6_*.asm`……）、动态链接测试集
（`ld_so.c`、`libfoo.c`、`main_dyn.c`、`pie_test.c`）以及 `user.ld`
（ring 3 链接脚本，加载地址 0x400000）。为什么每应用一个目录：
`tools/gen_userprogs.py` 遍历目录树，仅针对 `ocos/interface/` 编译
每个目录，gzip 压缩 ELF 并连同双语提示追加进 `userprogs_data.h`
（[hidden-mechanisms.md](hidden-mechanisms.md) 解释惰性解压；
[../l1/app-anatomy.md](../l1/app-anatomy.md) 讲应用形态）。

## `ocbl/`——引导加载器（59 个文件）

围绕一个固件无关的 core，三个后端：

| 目录 | 文件数 | 内容 |
|---|---|---|
| `bios/` | 16 | `stage1.asm`（512 字节扇区，一份源出两种形态：`-D OCBL_ELTORITO` / `-D OCBL_MBR`）、`core_head.S`（32->64 core 入口，0x8F000 处的恒等页表，EBDA 守卫）、实模式跳板一对、int 10h/13h/14h/15h/16h/1Ah 后端、VBE、RSDP 扫描、VBR 链式加载 |
| `efi/` | 10 | 独立（freestanding）的 UEFI 类型定义（无 EDK2）、`efi_entry.c`（MS-ABI 入口、固定窗口保留、EBS 重试循环）、BlockIo 磁盘后端、GOP、GetMemoryMap->E820、EFI 链式加载、64->32 交接 blob |
| `common/` | 23 | 六类扫描器、只读的 FAT12/16/32+ISO9660 卷层、MBR/GPT 遍历（含 0xEE 影子情形）、ELF 加载器 + mb2 构建器、3 键菜单、引导状态监视器、ACPI S5、6300ESB 看门狗、轮询式 16550 串口，以及形态契约头文件 |

顶层：`ocbl.mk`（全部 make 目标）、`linker_bios.ld`（core 位于
0x7E00）、`linker_efi.ld`（中间 ELF 位于 0x11000），以及 Python
构建包：`build_image.py`、`build_test_iso.py`（所有 ISO 的 ISO
打包器——双 El Torito、G13 确定性、有意不加
`-boot-info-table`）、`elf_to_efi.py`、`make_scan_disks.py`（8 镜像
扫描器测试舰队）、`make_chain_disks.py`、`menu_drive.py`（QEMU
monitor sendkey 驱动）、`qemu_snap.py`。深度解析：
[boot-flow.md](boot-flow.md)。

## `ocire/`——恢复内核（49 个文件）

扁平的源码树（一切自有：`ocire_ata.c`、`ocire_fat.c`、
`ocire_sha256.c`、`ocire_console.c`、`ocire_idt.c`、`ocire_acpi.c`、
`ocire_wdt.c`……）外加三个特殊角落：

- `ocire/agent/ocire_agent.c`——ring 3 日志代理，像 l1apps 程序一样
  编译（freestanding + `ocos/l1apps/user.ld` @ 0x400000 +
  `int 0x80`），作为独立 ELF 交付在恢复卷上，并在 kexec 时作为
  受保护的 `ocire_guard` 进程注入。它自成子目录，因为它是唯一一个
  依据 OCOS ABI 构建的 OCIRE 文件。
- `ocire/generated/`——`ocire_version_gen.h`（`OCIRE_STAMP` 横幅）
  与 `ocire_cn_font_data.c`（由 sed include 重写规则从 OCSE 的字体
  数据生成，因此字体的重新 vendor 会自动流入）。
- `ocire/grub-dual.cfg.in`——仅存的那条 GRUB 链（双引导修复目标盘）
  的模板，其 `@KERN_LBA@`/`@KERN_SECTORS@` 块清单占位符由
  `tools/make_ocire_disk.sh` 填充。

另有 `ocire_bootsvc.c`（监督者决策循环）、`ocire_kexec.c` +
`ocire_kexec_jump.S`（OCOS 重启）、`ocire_shell.c` / `ocire_recover.c`
（双语恢复 shell：`logread`、`syscheck` 十点完整性检查、
`repair quick|full`、`return`、`shutdown`、`reboot`、`lang`）。
深度解析：[../dev/ocire.md](../dev/ocire.md) 与
[crash-recovery.md](crash-recovery.md)。

## `ocse/`——安装环境（179 个文件）

**从 git tag v1.0.0 vendor 内嵌**（commit bdc47047，重构前最后一个
单体内核——`ocse/{kernel,drivers,fs,l1,boot,chinese}` 下 143 个内嵌
文件）。为何 vendor：OCSE 在任何 OCOS 服务存在之前运行，v1.0.0 已
包含安装器所需的全部驱动，而冻结副本使安装器在主内核演进时保持
可引导。重新 vendor + 补丁循环：`tools/vendor_ocse.sh`（PRESERVE
列表保留 PS/2 鼠标驱动等 OCSE 原生文件）+ `tools/ocse_edits.py`
（**E1-E20** 处编辑，内嵌基底唯一被允许的变更方式——绝不手改
`ocse/{kernel,drivers,fs,l1,boot,chinese}/*.c`）。这些编辑剥掉
安装器不该有的东西：E1 OTA/更新 API、E4/E5/E6 ring 3 崩溃/kill/
int 0x80（无用户面）、E7 USB MSC/串口轮询器、E12 用 24 级引导
梯子的直接调用取代 OC_DRIVER_DECL 收集。在内嵌基底之上是 OCSE
原生部件：`ocse_main.c`（GUI<->CLI 模式循环）、`ocse_cl.c`
（OCSECL，1919 行，40 命令）、`ocse_gui.c` + `ocse_wizard.c` +
`ocse_wzctl.c`（9 步向导 + 控件工具包）、`ocse_inst.c`（安装器
引擎：OCBL MBR 写入器 + 8 步安装状态机）、`ocse_mkfs.c` /
`ocse_ext4w.c`（真正的 FAT32/exFAT/ext4 格式化器，ext4 甚至可写）、
`ocse_atapi.c` / `ocse_iso.c`（安装介质）、`ocse_detect.c`（四项
硬件检查）、`ocse_i18n.c`，以及 `linker_ocse.ld`（1 MiB 基址、
`.ocsework` NOLOAD 向导状态区、同样的 0x400000 ASSERT）。
[boot-flow.md](boot-flow.md) 讲交接；E1-E20 清单在
`ocse/VENDORING.md`。

## `tools/`——117 个文件

大致分六类：

| 类别 | 示例 | 用途 |
|---|---|---|
| 构建与打包 | `build.sh`、`build_iso.sh`、`build_ocse_iso.sh`、`build_ocire_iso.sh`、`build_release.sh`、`install-toolchain.sh`、`oc_cc.sh`、`env-sandbox.sh`、`oc_menuconfig.sh`、`make_ab_disk.sh`、`make_ocire_disk.sh`、`make_ocos_tar.py`、`make_update_pkg.sh`、`make_wdat_table.py`、`iso_zero_ctime.py`、`fat_zero_dates.py` | WP-13 流水线，从干净目录树到签名形态的 ISO + OTA 包；确定性垫片（时间戳清零） |
| 内嵌器 | `embed_grub.py`（把 GRUB boot.img+core.img 嵌进内核）、`embed_roots.py`（CA 根 -> crypto）、`embed_userprog.py`、`build_solib.py`、`build_c_userprog.py` | 内核镜像内随船的一切 |
| 生成器 | `gen_userprogs.py`（284 应用注册表）、`gen_font.py` / `gen_font_gz.py` / `gen_chinese_font.py`、`gen_pinyin_phrase.py`、`gen_l0_i18n.py`、`gen_l1svc_ops.py`（冻结的 ABI 镜像——[private-headers.md](private-headers.md)）、`gen_api_docs.py`（docs/data/*.json）、`gen_svc_api_headers.py`、`gen_gzip_vectors.py`、`apply_i18n_hints.py` / `apply_tool_i18n.py` | 把源数据变成编译进内核的表 |
| E2E 驱动 | `wp01_*` .. `wp12_*`、`p0*`、`fix1_*`、`run_ocire_*`、`run_l1_per_app.py`、`wp10_native_e2e.py`、`wp12_watchdog_logging_e2e.py`、…… | 引导真实镜像并 grep 串口笔录的 QEMU 测试组（验收方法论） |
| 对端与服务器 | `https_test_server.py`、`p0_evil_tls_server.py`、`p0_redir_server.py`、`paramiko_sshd.py`、`ssh_g11a_e2e.py`、`bt_test_peer.py`、`update_server.py`、`qemu_runner.py`、`qemu_shot.py` / `qemu_shot_vnc.py` | 网络测试的线路对端；QEMU 管道 |
| 维护与重构 | `nak_migrate.py` / `nak_fix_refs.py` / `nak_map.tsv` / `restructure_*.py`（5 个）、`fix_file_headers.py`、`check_bss.py`、`check_deps.py`、`v2_registry_gate.py`、`sync_wiki.sh`、`update_website.sh`、`github_release_wp08.sh`、主机 C 测试（`host_gzip_test.c`、`ocire_fat_host_test.c`）、数据（`ime_phrases.txt`、`update_test_vectors.h`） | 让 15 万行的目录树保持诚实的脚本 |

## `tests/`——48 个文件

- **25 个 `host_*.c` 文件**——针对足够纯净、可离目标链接的内核代码
  的主机编译单元测试：`host_mb2_test.c`（multiboot2 解析器）、
  `host_tls13_ks_test.c`、`host_x25519_test.c`、`host_bn_ec_test.c`、
  `host_aead_test.c`、`host_rsa_test.c`、`host_x509_test.c`、
  `host_tcpcc_test.c`（CUBIC）、`host_fix4_tar_test.c` /
  `host_fix4_crypto_test.c`、`host_scsi_test.c`、`host_camera_test.c`
  （MJPEG 解码器对双精度参考实现）、`host_ir_test.c`、
  `host_i2chid_touch_test.c`、`host_wifi_test.c`、`host_bt_test.c`、
  `host_gcm_probe.c`，外加测试桩（`host_registry_stub.c`、
  `host_pmm_stub.c`……）。由 `tools/run_host_tests.py` 运行；该纪律
  的文档见 [../dev/host-tests.md](../dev/host-tests.md)。
- **23 个 Python 测试组**——OCSE/安装器与 i18n 的 QEMU 验收套件：
  从 `ocse_t1_neg.py`（无盘负向验收——check4 FAIL 必须让机器诚实
  停机）到 `ocse_t7_regress.py`、`ocsecl_b2_part.py`..`b6_i18n_flow.py`
  （OCSECL 命令测试组）、`ocse_w6_gui.py`（无头 GUI 点击探针）、
  `ocse_wp06f_*`（PS/2 探测 + 修复测试组）、`wp06g1_font_audit.py` /
  `wp06g2_*_verify.py`（字体/IME/i18n/编辑器验证）。

客户机内的测试面（故事的另一半）根本不在 `tests/` 里：它是
`ocos/l1apps/` 下 50+ 个 `*_test` L1 应用，加上 L1SVC diag 类别的
39 个 op，均可从已引导的系统触达——见
[syscall-ring3.md](syscall-ring3.md)。
