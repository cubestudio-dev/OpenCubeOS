<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 架构

本文档描述 3.0.0-WP-l1apps-merge 时点的代码树——下文点名的
每个组件都是你能打开的真实文件。行数与逐目录明细在
`docs/data/stats.json`（重新生成：`make api-docs`）。

## 两个环

Open Cube OS 是一个双环系统：

- **L0——提供服务。** 纯服务内核：核心机制、驱动、根
  服务和接口面。3.0.0-WP03 彻底删除了 L0 用户面——`oc>`
  shell、每条 shell 命令、TUI 框架和输入法引擎都不见了；
  L0 在 `L0 ready, waiting for L1` 完成启动，把机器交给默认
  的 Ring-3 入口。
- **L1——使用服务。** 经扩展面（`SYS_L1SVC` 操作表
  和经典 syscall ABI）触达 L0 能力的 Ring-3 用户应用。
  3.0.0-WP04 把每条命令都移进了 L1：**`ocos/l1apps/` 下的
  284 个程序**（含 8 个合并大应用）——一条命令 = 一个应用 =
  一个文件夹。默认启动项是 Ring-3 的 `ush` shell
  （`ocos/kernel/core_boot_l1launch.c` 中的
  `OC_L1_APP_DECL("ush", ..., dflt=1)`）；**296 条命令 = 284 个
  注册表应用 + 12 个 ush 内建，L0 里零条**。

```
              OCBL - the in-tree bootloader (hybrid BIOS + UEFI ISO)
              512-byte real-mode stage 1 (El Torito / MBR) or the EFI
              entry -> 16 -> 32 -> 64-bit long mode, dynamic disk scan,
              boot menu with a 10 s countdown (no GRUB in the chain)
        +-------------------------+-------------------------+
        |              ocos/kernel/core_boot_entry.S                  |
        |  32-bit entry -> GDT -> 2 MiB identity map of    |
        |  the first 4 GiB -> PAE+LME+PGE -> long mode     |
        +---------------------------------------------------+
                               |  kmain (ocos/kernel/core_boot_main.c)
        +-------------------------+-------------------------+
        |  PHASE 1  kernel core mechanisms                  |
        |    core/console (serial/fb platform text layer),  |
        |    IDT/PIC/exceptions, PIT, PMM/VMM/heap,         |
        |    scheduler/sync, userspace, registry,           |
        |    ACPI/PCIe/watchdog-hw/chipset, RTC/HPET        |
        |  PHASE 2  drivers + driver-free services          |
        |    VFS + ramfs (driver-free); block/USB/wireless/ |
        |    SD/BT/camera/misc driver families              |
        |  PHASE 3  driver-dependent services               |
        |    FAT/exFAT/ext4 + config + A/B (after block),   |
        |    wireless (after fs-mounted firmware),          |
        |    net (after NIC), snd (after the snd family)    |
        |  PHASE 4  interfaces ready                        |
        |    crash dump/crashlog/soft wdt, health monitor,  |
        |    update check, the OCIRE embed confirmation     |
        +---------------------------------------------------+
                               |
                     "L0 ready, waiting for L1"
                               |
                    l1 launcher -> ush (ring 3)
                               |
          exit / failure -> L0 halts honestly (reboot or the
          OCBL menu; the 2.x oc> recovery fallback is gone)
```

（启动会为中段各阶段打印 `boot chain N/4` 标记；
`tools/check_deps.py` 为下面的层方向把关。）

## L0 树（3.0.0-WP03 布局）

```
ocos/kernel/
  boot/     the composition root: boot.S, long mode entry, the
            multiboot2 header/stub, kmain.c, the L1 launcher and the
            boot self-tests. May depend on every layer.
  core/     kernel mechanisms: scheduling, sync, timers, usermode,
            the panic slots (arch_exceptions), the declaration
            registry, the captured-output primitive, mem/, lib/
            (string/log/klog-ring/config-kv), console/ (the mb2
            framebuffer platform text layer), arch/{x86_64,arm64,
            riscv} and generated/.  Depends on nothing above it.
  drivers/  hardware drivers, one folder per family (block, nic,
            snd, usb, input, pci, acpi, i2c, time, wireless,
            bluetooth, sd, camera, misc, scsi).  No services
            dependencies.
  services/ root services - the folder is the interface facet:
            fs block net audio font encoding display loader
            selfhost ocembed | sysapi ipc klog health crash
            (+ time power crypto camera usb bodies).  May depend on
            core/ and drivers/ (and, where a real dependency
            exists, on other services - fs before selfhost, for
            example).
ocos/l1apps/     the ring-3 world: 275 programs (242 app folders + 8 flat
            .c + 25 flat .asm) - ush (the only OC_L1_APP_DECL default
            entry), the TUI desktop family, the editor, the Chinese
            console + IME, and one command per app (built by
            `make l1apps`).
ocbl/       OCBL, the in-tree bootloader (BIOS stage 1 + EFI entry,
            the boot menu, the ocbl-install target) - a separate
            freestanding build (ocbl/ocbl.mk). NOT part of L0.
ocire/      the independent recovery kernel (a separate freestanding
            binary, own build in ocire/ocire.mk).  NOT part of L0.
```

**依赖规则**（由 `tools/check_deps.py` 强制，跑在测试矩阵中）：
`core/` 不得依赖 `drivers/` 或 `services/`；`drivers/` 不得依赖
`services/`。WP-03 G5 为此要求的倒置：平台控制台栈住在
`core/console`；klog 环是核心机制，盘上汇聚点作为发射钩子
安装自 `services/klog`；崩溃捕获住在 `services/crash`，把自己的
处理程序装进核心持有的 panic 槽；配置存储把键值机制留在
核心，fs 后端安装自 `services/fs`。

## 加一个文件即扩展

没有东西通过运行时调用注册进全局表。驱动、服务或 L1 应用
通过 `OC_*_DECL` 宏（`core_registry.h`）在**实现它的那个
文件里**声明身份；每个宏把一个 const 描述符放进专门的
链接器节（`.oc_drvs`、`.oc_svcs`、`.oc_apps`），
`core_registry_collect()` 在启动时走查节边界。新增一个驱动
或服务 = 在正确的文件夹里加一个 `.c` 文件再加一行声明——
不改启动文件，没有注册调用点（G3 证据中实测：一个带
`OC_DRIVER_DECL` 的新文件在运行时被收集，随后移除）。

v1 的 register 类 L1 接口（`driver_block_register`、
`driver_nic_register`、……）随节机制一同消亡；它们的操作类
兄弟（read/write/send/recv/…）仍是表面（完整表格和
v1->v2->v3 命运记录见 INTERFACES.md）。

## 接口面

Ring 3 经两扇门触达 L0：

- 经典 syscall ABI
  （`ocos/kernel/services/sysapi/core_syscall.h`，
  54 个系统调用），含 `SYS_L1SVC(110)`：
  `(category, op, arg, out, cap)` 由注册表路由到所属服务的
  `l1_dispatch`。
- categories/ops 是
  `ocos/kernel/services/sysapi/l1svc_defs.h` 中冻结的 ABI 表：
  **20 个类别 / 190 个编号条目**（类别号 -> 所属的 services/
  文件夹；类别 18 "ocire" 是声明的例外——OCIRE 日志代理契约
  的 ABI 名，由 `services/ocembed/` 实现）。操作是纯 ASCII
  文本渲染器——Ring-3 侧不格式化任何东西。

## 启动状态监视器（3.0.0-WP07a）

内核运行期间 OCBL 无法执行，因此 WP07a 启动状态监视器用
一台硬件看门狗中继（6300ESB，与 OCIRE 监督循环所用的同一
PCI 设备）强制启动超时：

```
OCBL arm(30s) -> handoff -> kernel stage N: write stage + arm(next
budget) -> ... -> L1_READY: 1 Hz tick pets the 15 s runtime budget
                |
                v (a hang stops the feeding)
6300ESB resets the machine (warm reset, DRAM kept)
                |
OCBL boots again: reads the state area -> silence >= 8 s and no
CLEAN marker -> JUDGED: writes the BOOT_FAILED record (stall point,
timestamp, measured silence) -> screen notice -> 8042 pulse
                |
OCBL boots again: pending unrouted record -> auto-boots OCIRE
                |
OCIRE: reads the record -> prints the stall point + repair advice
```

契约住在 `include/ocbl_state.h`（OCBL、OCOS 和 OCIRE 逐字节
共享）：

- **状态区**位于 0x98000——内核正在做什么（阶段、心跳、
  最近一次进展的 RTC 时钟）。内核只写它（盲写，从不回读）；
  OCBL 读写；OCIRE 只读。L1 应用够不到它：这些页面位于每个
  用户地址空间的管理者专属半区（在 L1_READY 由一次 CR3
  走查 + badapp 金丝雀类实测验证）。
- **保留区**位于 0x9B000——事后启动记录（BOOT_FAILED 与
  USER_REQUEST、停滞阶段、判定时间戳、测得的静默、反码
  校验和）。它在热复位中存活（这两个地址是 QEMU/SeaBIOS
  复位擦除中经实证的幸存孤岛——见
  `evidence/3_0_0_WP07a/`）；冷启动会清掉它，且每个读取者
  都先验证魔数。
- **六个状态点**：KM_AIN_ENTERED（kmain 入口）、
  SCHED_READY（核心机制就绪）、DRIVERS_READY（驱动家族已
  探测）、SERVICES_READY（根服务就绪）、L1_LAUNCHED（ush
  已启动）、L1_READY（ush 在服务——它的第一次系统调用）。
- **阶段预算**：30 秒（启动开始 -> 阶段 1）、中段每阶段
  10 秒、30 秒（L1_LAUNCHED -> L1_READY）、15 秒运行时
  心跳。
- **判定**：OCBL 计算静默时长（RTC 现值减去内核最近一次
  进展）；无 CLEAN 标记且 >= 8 秒 = 判定失败（健康的启动
  每秒都有进展，因此手动热复位测得 < ~4 秒，不算失败）。
- **干净重启**：内核在有意的 8042 重启之前把状态区标记为
  CLEAN，因此正常的 `reboot` 永不产生失败记录。
- **用户请求**：在 OCBL 菜单中选择 OCIRE 条目会写入
  USER_REQUEST 记录——这正是 OCIRE 报告时区分的两种情形
  （"requested by the user" 与带修复建议的判定失败）。

整条链由 `tools/wp07a_state_e2e.py` 实测驱动（真实看门狗
复位、真实 8042 脉冲、两种记录、每个阶段预算、运行时
挂起、手动复位甄别）；誊录归档在 `evidence/3_0_0_WP07a/`
之下。

## OCIRE 内嵌（3.0.0-WP03 G4）

当 OCIRE 恢复监督者（见 `ocire/`）经 kexec 把机器交给 OCOS
时，它在命令行上传递启动事实
（`oc.mb= oc.log= oc.logsecs= oc.wdt= oc.agent=1`）并把它的
日志代理作为 multiboot2 模块挂载。`services/ocembed/` 在
OCOS 侧闭合回路：它解析事实、向恢复邮箱写 SIGNAL_OK、在
受监督的 60 秒窗口内解除 i6300esb 看门狗布防、把代理生成
为受保护的 `ocire_guard` 进程（kill 被 sys_kill 拒绝）、服务
guard 专属的 `L1SVC_CAT_OCIRE` 通道（启动日志追加 + 内核
日志快照）并在恢复盘上布防整盘写保护。正常 OCBL 启动时
没有任何事实存在，服务保持休眠。五条回归性质（分区隔离、
启动信号、双向接口、进程保护、整盘硬保护）由
`tools/run_ocire_supervised_tests.py` 端到端测试。

## 留下来的内核能力

内核日志环（+ `/var/log/kernel.log` 汇聚点）、健康监视器
（1 秒采样 + 告警）、崩溃捕获（核心转储 + crashlog + panic
路径）、软看门狗槽和硬件看门狗（TCO/WDAT）都是 L0 侧
机制——过去驱动它们的 L0 COMMAND 条目随用户面一同消亡；
机制本身在启动时布防、可测试（测试组在
`evidence/3_0_0_wp03_*`）。
