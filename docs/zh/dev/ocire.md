<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCIRE - OpenCubeOS 独立恢复环境

状态：**3.0.0-WP-02-ocire_recovery**（本工作包交付恢复特性：
带看门狗式启动失败检测的启动监督器，以及带 syscheck / repair
的完整恢复 shell）。

## OCIRE 是什么

OCIRE 是第二个 L0 内核，与 Open Cube OS（OCOS）内核完全独立。
它住在自己的源码树（`ocire/`）里，不与 OCOS 共享任何头文件，
用自己的链接脚本（`ocire/linker_ocire.ld`）链接，产出自己的
启动镜像（`build/ocire/ocire.iso`）。双内核架构：

- **L0 = 提供服务** — OCOS 仍是全服务内核。OCIRE 是一个 L0
  *对等体*：在 OCOS 无法启动时启动的恢复内核。
- **独立性** — OCIRE 必须能在不触达、不依赖任何 OCOS 代码、
  数据或分区的情况下启动、运行并对机器做电源循环。

## WP-01 交付内容

| 领域 | WP-01 | WP-02（本包） | WP-03 |
|---|---|---|---|
| 启动 + 最小 shell | **已完成** | — | — |
| help / shutdown / reboot | **已完成** | — | — |
| 恢复特性、FAT12、自检、修复 | — | **此处** | — |
| 启动失败检测、1 分钟超时 | — | **此处** | — |
| OCOS <-> OCIRE 接口、进程保护 | — | — | 此处 |

## WP-02 交付内容

1. **启动监督器**（`ocire.mode=sup`，双启动菜单默认项）：扫描
   磁盘、运行邮箱状态机、为 60 秒启动窗口布防 i6300esb 看门狗、
   分阶段加载并对 ring-3 日志代理做摘要校验，然后 kexec OCOS
   内核并把邮箱 / 日志 / wdt 事实放到命令行上
   （`oc.log= oc.logsecs= oc.mb= oc.wdt= oc.agent=1`）。
2. **启动失败检测**：一次看门狗复位把监督器落在一个仍是
   `WAIT_BOOT_SIGNAL` 的邮箱上，意味着 OCOS 从未起来 - 监督器
   打印崩溃通告、递增 `fail_count`、设置 `ENTER_FULL` 并重启
   进入完整恢复 shell。已在 QEMU 中用真实 i6300esb 设备
   （`-device i6300esb`）端到端验证：布防 -> kexec -> 60 秒
   到期 -> 复位 -> 崩溃通告 -> 恢复 shell。
3. **完整恢复 shell**（菜单项 2 或 `ocire.mode=full`）：
   `logread`（转储启动日志环）、`syscheck`（十点完整性自检）、
   `repair quick`（修复 MBR / 邮箱 / 日志头 + 恢复任何摘要与
   黄金清单不一致的系统文件）、`repair full`（对系统卷做破坏性
   重格式化 + 完整黄金重写，需输入 YES 确认）、`return`
   （正常启动邮箱 + 重启），外加 WP-01 的
   `help/shutdown/reboot`。
4. **FAT12 读 / FAT32 读写**：流式辅助函数
   （`ocire_fat_read_range`、`ocire_fat_hash_file`、
   `ocire_fat_copy_stream`）让恢复 shell 无需缓冲即可对数 MB
   的文件做哈希与恢复；分配带有预留标记，因此两次连续分配绝不
   会交出同一个簇，不符合 8.3 形式的名称会被拒绝而非静默截断。

### 诚实的边界（WP-03 范围，属设计如此）

闭环的 OCOS 一侧**不**在本工作包内：OCOS 尚不解析 `oc.*`
命令行事实，不写 `SIGNAL_OK` 邮箱状态，不解防/喂狗 i6300esb，
也不拉起注入的代理（代理调用的 `L1SVC_CAT_OCIRE` 通道尚不
存在）。后果（已测量并声明）：

- 在带 i6300esb 的真实硬件上，一次到达 OCOS 的受监督启动在
  交接后 60 秒仍会硬复位（没人解防窗口）- 项 3 "Open Cube OS
  (direct boot)" 是日常使用的过渡路径。
- 没有看门狗时（QEMU 默认配置），仅邮箱的信号机制意味着每个
  第二次连续的受监督启动都会降级进恢复 shell（无信号 = 推定
  崩溃）。恢复 shell 的 `return` 把邮箱复位为 NONE。

## 源码布局（ocire/）

```
ocire/
  ocire_mb2_header.S    multiboot2 header (80x25 text mode request)
  ocire_boot.S          32-bit GRUB entry -> GDT + 4 GiB identity map
                        -> long mode -> ocire_main; exception stubs
  ocire_main.c          kernel entry: console, IDT, ACPI, shell
  ocire_console.c/.h    dual console: COM1 115200 + VGA text 0xB8000
  ocire_input.c/.h      serial + PS/2 polled input (shift/caps/backsp)
  ocire_idt.c/.h        minimal IDT: 32 exception vectors, faults
                        print a diagnostic instead of triple-faulting
  ocire_acpi.c/.h       RSDP/XSDT/FADT (checksums verified), tiny \_S5
                        package reader, PM1a S5 + RESET_REG + 8042
  ocire_mb2.c/.h        multiboot2 info parsing (framebuffer/cmdline/
                        modules - WP-02 group 1)
  ocire_font.c/.h       embedded 8x16 font for the LFB console
  ocire_ata.c/.h        ATA PIO driver (IDENTIFY, LBA28 read/write)
  ocire_sha256.c/.h     SHA-256 (golden/agent/kernel digests)
  ocire_fat.c/.h        FAT12 read + FAT32 read/write (mount, lookup,
                        streaming read/hash/copy, create/mkdir/format)
  ocire_wdt.c/.h        Intel 6300ESB watchdog (PCI 8086:25ab probe,
                        arm/disarm/ping, 1 kHz two-stage window)
  ocire_bootsvc.c/.h    the boot supervisor (mailbox state machine,
                        agent staging, kexec hand-over)
  ocire_kexec.c/.h      OCOS ELF staging + mb2 info rebuild + jump
  ocire_kexec_jump.S    64-bit kexec trampoline
  ocire_disk.c/.h       recovery-disk discovery, mailbox, golden image
  ocire_recover.c/.h    the full recovery shell commands (logread/
                        syscheck/repair/return)
  ocire_shell.c/.h      the command loop (8 commands)
  ocire_util.c/.h       freestanding mem/string helpers
  ocire_io.h            port I/O inline helpers
  ocire_types.h         base scalar types
  agent/ocire_agent.c   the ring-3 log agent (OCOS L1 ABI, injected
                        at kexec; its L1SVC channel is WP-03)
  ocire.mk              build targets (included from the top Makefile)
  linker_ocire.ld       own link layout (1 MiB load address)
  grub-ocire.cfg        standalone ISO menu (single entry)
  grub-dual.cfg.in      dual-boot disk menu TEMPLATE (3 entries:
                        supervisor default, full shell, direct OCOS)
```

构建目标：`make ocire-kernel`、`make ocire-iso`、`make ocire-disk`、`make ocire-test`、`make ocire-dual-test`、`make ocire-shell-test`（运行目标 `ocire-run-bios/uefi[-gui]` 同样
可用）。编译使用与 OCOS 内核相同的 `-Wall -Wextra -Werror`
零警告门禁；CI 在每次 push 时运行 OCIRE 构建。OCIRE 的目标
文件在 ocire.mk 中携带自己的 `-include $(OCIRE_OBJ_C:.o=.d)`
头依赖跟踪（顶层 Makefile 只包含 OCOS 内核的 .d 文件 - ocire/
中的头文件改动曾静默链接过期对象；由 WP-02 批次现场发现并
修复）。

## 两个操作系统如何共存（WP-01 布局）

```
hd0 (ocire-boot.img, built by tools/make_ocire_disk.sh)
  MBR + GRUB (BIOS: boot.img + core.img in the post-MBR gap;
              UEFI: /EFI/BOOT/BOOTX64.EFI on the FAT32 partition)
  p1 FAT32: /boot/opencube.elf (OCOS kernel)
            /boot/ocire.elf    (copy of the OCIRE kernel)
            /boot/grub/grub.cfg (the dual menu)
  -> GRUB menu entry 1 boots OCOS (default)
  -> GRUB menu entry 2 boots OCIRE

hd1 (ocire-hd.img) - the OCIRE disk
  MBR with ONE entry, type 0xEE ("GPT protective shadow"), no GPT
  header behind it; the raw OCIRE kernel image is stored at the
  partition start (LBA 2048).
  -> the GRUB menu loads it with a whole-disk blocklist:
     multiboot2 (hd1)2048+128
  -> OCOS refuses the entire partition table (0xEE makes its parser
     switch to GPT mode, which fails without a GPT header), so the
     OCIRE partition has no device node and no filesystem path in
     OCOS - see "Partition isolation" below.
```

点亮过程留下的两条教训，记录在此：

1. GRUB 2.12 的 msdos 分区表**跳过** 0xEE 项 — 即便同一个项
   在磁盘上完全合法，`(hd1,msdos1)` 这样的分区路径对 GRUB 也
   不存在。因此菜单使用整盘块清单。
2. GRUB 菜单没有数字热键；自动化通过串口菜单终端上的 ANSI
   光标下移序列选择项。

## 分区隔离（WP-01 验收测试）

在挂着两块磁盘的运行中 OCOS 里测得，见
`evidence/3_0_0_wp01_ocire/isolation.log`：

- `partscan: 1 partition device(s) registered` — OCIRE 磁盘
  贡献**零**个子设备（只有双启动盘的 p1 注册）。
- `parted hda`（OCIRE 磁盘）→ `no partition table found`。
- `mkfs.fat32 hdap1 confirm` → `device not found` — 没有可写
  入的节点。
- `disk_rw_test hdap1` → `device lookup FAIL` — 没有可读取的
  节点。
- `df` — 只挂载了 OCOS 卷；来自 OCIRE 磁盘的一无所有。

诚实的边界（同样已测，`isolation_rawdecl.log`）：整盘设备节点
仍然存在 — `disk_rw_test hda` 与 `mkfs.fat32 hda confirm` 在
一次性副本上都能工作，恰如带 `/dev/sdb` 的 Linux root。要阻止
管理员级的整盘操作需要 OCOS 块层策略 — 这是本工作包明确禁止
的 OCOS 内核改动 — 属于 WP-03（进程保护）。

## 测试

- `tools/run_ocire_tests.py`（`make ocire-test`）— 14 个用例，
  BIOS + UEFI：启动到 shell 提示符、`help`、未知命令提示、
  退格编辑、`shutdown`（ACPI S5；QEMU 真的退出）和 `reboot`
  （8042/RESET_REG；带 `-no-reboot` 时 QEMU 真的退出）。
- `tools/run_ocire_dual_tests.py`（`make ocire-dual-test`）—
  11 个用例：在一次性磁盘副本上跑四种双启动组合（BIOS/UEFI x
  菜单默认监督器->kexec->OCOS / 菜单选择 OCIRE 完整 shell）
  （监督器有状态 - 见 WP-03 边界）、五项隔离探针和两项整盘声明
  探针。
- `tools/run_ocire_shell_tests.py`（`make ocire-shell-test`）—
  WP-02 恢复批次，在一次性副本上的 4 个用例：`shell_healthy`
  （在 pristine 磁盘上 syscheck 10/10 + 幂等的 quick 修复）、
  `shell_quick_repair`（内核 ELF 翻转一个字节 -> syscheck 抓住
  摘要不匹配 -> repair quick 恢复并验证 -> 重新健康）、
  `shell_full_repair`（p1 BPB 被清零 -> 从独立 ISO 启动，hd0
  自己的 GRUB 已读不出它的配置 -> repair full 从黄金镜像重写
  该卷 -> 恢复健康）以及 `shell_wdt_timeout`（挂上真实
  i6300esb：监督器布防 60 秒窗口 -> kexec -> OCOS 启动但从不
  发信号 -> 看门狗复位 -> 崩溃通告 -> 完整恢复 shell）。
- 在一棵含 OCIRE 但零 OCOS 改动的树上跑完整 OCOS 回归矩阵：
  `evidence/3_0_0_wp01_ocire/regression/`（宿主机测试、
  reproducible-check、wp12、wp10_tui、chinese、tuiqa x2、
  native x2、l1_per_app、wait4 全新启动批次 20/20、启动门禁）
  — 全绿且与 2.2.0 发布基线一致。
