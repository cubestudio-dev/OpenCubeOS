<!-- SPDX-License-Identifier: Apache-2.0 -->
# 系统总览：四个组件及其协作方式

Open Cube OS 不是一个程序。四个独立链接的二进制文件共享同一个仓库、
同一个版本字符串，并通过固定的物理内存契约彼此传递控制权。本页是
全景视图：每个组件是什么、协作图、版本链、把各方绑定在一起的物理
内存地图，以及系统支持的每一种引导模式。深度解析：加载器链见
[boot-flow.md](boot-flow.md)，跨组件的合谋机制见
[hidden-mechanisms.md](hidden-mechanisms.md)，文件树见
[tree-map.md](tree-map.md)。

## 四个组件

| 组件 | 位置 | 身份 |
|---|---|---|
| **OCBL** | `ocbl/` | 引导加载器（BIOS 与 UEFI 两种形态）。在 3.0.0-WP05 B5-7 中取代了发布 ISO 上的 GRUB。扫描每块磁盘，分类每个可引导卷，运行 10 秒菜单，并向所选内核递交与 GRUB 完全一致的 multiboot2 状态。 |
| **OCSE** | `ocse/` | "OpenCubeOS Setup Environment"——安装器。一个小型独立内核，**从 git tag v1.0.0 vendor 内嵌而来**（143 个文件，有意冻结：见 `ocse/VENDORING.md`），自带一套驱动（ATA/AHCI/NVMe/virtio/USB/ACPI）、一个 40 命令 shell（OCSECL）、一个 9 步 GUI 向导和一个 8 步安装状态机。 |
| **OCOS** | `ocos/` | 主系统：L0 服务内核（`ocos/kernel/`）、驱动层（`ocos/drivers/`）、24 个服务家族（`ocos/services/`）、ring 3 API 契约（`ocos/interface/`）以及 284 个内嵌 ring 3 应用程序（`ocos/l1apps/`）。运行中系统的一切。 |
| **OCIRE** | `ocire/` | 独立恢复内核。OCOS 的 L0 *对等体*——自有 include 路径、自有链接脚本、不含任何 OCOS 头文件（WP-01 独立性规则）。自有 ATA 驱动、自有 FAT12/32 代码、自有 SHA-256、自有控制台、自有 IDT、自有 ACPI、自有 6300ESB 看门狗。以监督者（`ocire.mode=sup`）或完整恢复 shell 引导，kexec OCOS，并在 OCOS 无法引导时修复机器。 |

源代码层面只有三样东西共享：`include/ocbl_state.h`（OCBL、OCOS、
OCIRE 三方逐字节共享的引导状态契约）、中文字体数据（OCIRE 的
`ocire/generated/ocire_cn_font_data.c` 由 `ocse/chinese/chinese_font_data.c`
生成）以及 OCIRE 日志代理编译所依据的 L1 ABI（是 ABI，不是 include）。

## 协作图

```
 power-on
    |
    v
 +--------------------------------------------------------------+
 | OCBL  (BIOS stage1 @0x7C00 -> core @0x7E00  /  UEFI PE app)  |
 |  * 6-class volume scanner: OCSE OCOS OCIRE WINDOWS LINUX     |
 |    GENERIC  (+ synthesized Shutdown row)                     |
 |  * 3-key 10 s menu (Up/Down/Enter only), serial-mirrored     |
 |  * A/B slot judgment on the first OCOS volume (flag files)   |
 |  * boot-state monitor: reads 0x98000/0x9B000, arms 6300ESB   |
 |  * auto-route: pending BOOT_FAILED record -> OCIRE one-shot  |
 +----+----------------+----------------+----------------+------+
      |                |                |                |
   OCOS             OCIRE             OCSE          foreign VBR/ESP
      |                |                |                |
      v                v                v                v
 mb2 handoff      mb2 handoff      mb2 handoff      chainload
 EAX=0x36D76289   EAX=0x36D76289   EAX=0x36D76289   (0x7C00 jump /
 EBX=MBI@0x50000  EBX=MBI@0x50000  EBX=MBI@0x50000   LoadImage)
 32-bit, pg off    32-bit, pg off   32-bit, pg off
      |                |                |
      |         OCIRE kexec           +--> installs OCOS disks:
      |         OCOS ELF -> [1MiB,8MiB)     ocbl-install writes the
      |         MBI rebuilt @0x80000        OCBL MBR flavor, mkfs.fat32,
      |         agent module appended       tar payload, boot flags
      |                |
      v                v
 +-------------+  +--------------------+
 | OCOS kmain  |  | OCIRE supervisor   |
 | boot chain  |  | mailbox WAIT_SIGNAL|
 | 1/4 kernel  |  | arm wdt 60 s       |
 | 2/4 drivers |  | kexec OCOS ------->|  (OCOS boots with
 | 3/4 service |  +--------------------+   oc.* embed facts)
 | 4/4 iface   |
 | L1 ush      |  OCOS first seconds:
 |  ...        |   ocembed_boot_confirm(): mailbox SIGNAL_OK,
 +-------------+   disarm i6300esb, spawn ocire_guard agent,
                   write-protect the whole recovery disk.
```

反向边与正向边同样重要：当 OCOS 在引导过程中死亡时，6300ESB 看门狗
复位机器，OCBL 读取 0x9B000 事后记录，依据 0x98000 阶段孤岛判定失败，
并在无任何菜单交互的情况下把下一次引导路由进 OCIRE（完整链路见
[crash-recovery.md](crash-recovery.md)）。

## 版本链

同一个 `OC_RELEASE_VERSION` 和同一个 `OC_KERNEL_BUILD` 流入全部四个
横幅——四个组件在构造上即步调一致地发布（`Makefile` 第 90/103-104 行）：

```make
OC_RELEASE_VERSION ?= 3.0.0-NAK
OC_KERNEL_BUILD_OFFSET := 21
OC_KERNEL_BUILD ?= $(shell expr $$(git rev-list --count HEAD ...) + 21)
```

| 组件 | 版本戳来源 | 说明 |
|---|---|---|
| OCOS | `-D OC_RELEASE_VERSION/-D OC_KERNEL_BUILD` + `ocos/kernel/generated/version_gen.h`（`OC_VERSION_STAMP`，由 cmp 守护，版本号一变即强制重编译——`-D` 标志对 `-MMD` 依赖跟踪不可见，一个实测发现的过期横幅 bug） | 横幅：`Open Cube OS  [3.0.0-...]` |
| OCBL | `-DOCBL_RELEASE=\"$(OC_RELEASE_VERSION)\"` + `-DOCBL_BUILD=$(OC_KERNEL_BUILD)`（`ocbl/ocbl.mk`） | 源码中的后备字面量 `3.0.0-WP05`/`0`；无独立版本号 |
| OCIRE | `OCIRE_VERSION ?= $(OC_RELEASE_VERSION)` + `ocire/generated/ocire_version_gen.h`（`OCIRE_STAMP`） | BUG #92 修复——它过去携带自己的字面量，落后了四个里程碑 |
| OCSE | `-DOCSE_RELEASE=\"$(OC_RELEASE_VERSION)-ocse\"` + `OCSE_BUILD=$(OC_KERNEL_BUILD)` | **唯一**带有独立后缀（`-ocse`）的组件，因为它是一个披着当前发布号的 v1.0.0 vendor 内嵌内核 |

`+21` 偏移使公开构建序列在已发布的 build 274 之上保持单调。各组件的
WP 标签（WP-05、WP-07a……）只存在于文件头注释中，从不出现在交付
横幅里。

## 物理内存契约

四个组件从不共享代码，但共享物理地址。这些是承重常量（定义于
`ocbl/common/ocbl_boot.h`、`ocbl/common/ocbl_types.h`、
`include/ocbl_state.h` 以及三份链接脚本）：

```
   0x00500-0x0051F   OCBL A20 测试区
   0x00520           stage1 事实：{boot drive, flavor, entry CS}
   0x007C00          OCBL stage1（512 B，两种形态共用）
   0x007E00..        OCBL core（linker_bios.ld；入口 core_head）
   0x30000           64 KiB 读取弹跳窗口；EFI：64->32 交接 blob
   0x40000-0x4FFFF   实模式跳板 + VBE/几何参数暂存区
                     （EBDA 与 0x8F000+ 重叠时的后备 PT 块）
   0x50000           multiboot2 信息构建缓冲区（OCBL_MBI_BASE；
                     OCIRE kexec 时改在 0x80000 重建其 MBI）
   0x60000-0x61FFF   E820 / GetMemoryMap 缓冲区
   0x62000 / 0x62800 OCBL fs 扇区窗口 / 128 KiB FAT 缓存
   0x82800-0x8DFFF   OCBL core 栈（栈顶 0x8E000 - WP06f VMware 修复）
   0x8F000-0x94FFF   OCBL 恒等页表（2 MiB 大页，4 GiB）
   0x98000  4 KiB    OCST 状态区      魔数 "OCS1" - 内核阶段/
                      心跳，由 OCOS 盲写，由 OCBL/OCIRE 读取
   0x9B000  4 KiB    OCST 保留区      魔数 "OCB1" - 事后
                      引导记录（BOOT_FAILED / USER_REQUEST）
   0x9FC00            EBDA（SeaBIOS）

 载荷放置：
   0x100000 (1 MiB)   OCOS 内核 PT_LOAD（链接基址）。OCSE 亦然
                      （KERNEL_LMA 0x100000）。两个镜像都必须
                      保持在 0x400000 以下（两份脚本中均有
                      linker ASSERT）
   0x400000-0x600000  “恒等窗口”——用户地址空间把这一虚拟
                      范围重映射到空 PT，因此内核镜像永远
                      不得长入其中（见 memory.md）
   0x800000           mb2 "self" 模块：OCOS 内核 ELF 的原样
                      副本（安装器载荷模式）
   0x01000000 (16 MiB) OCIRE 链接基址（OCIRE_LMA）——与 OCOS 的
                      1 MiB 区间不相交，因此监督者把 OCOS kexec
                      到 [0x100000, 0x800000) 不会覆盖自身
```

两座监视孤岛在 OCBL 构建的 mb2 内存映射中被划为 **E820 保留**，
因此任何 OS 都不会分配它们；实测表明它们能在 QEMU 热复位中幸存
（这正是全部意义所在——看门狗复位不得抹掉证据）。这两个块的完整
故事见 [hidden-mechanisms.md](hidden-mechanisms.md)，字节布局位于
`include/ocbl_state.h`。

## 引导模式

OCBL 从 `ocbl_menu_run()` 伸出八条不同的路径；前五条才是严格意义上
的“引导模式”：

1. **BIOS / El Torito（ISO）**——整个 El Torito 镜像由 SeaBIOS
   预加载；0x7C00 处的 stage1 发现 core 已在 0x7E00，打印
   `OCBL stage1 ok`，启用 A20（先 int 15h AX=2401，再端口 0x92，
   并做两次独立的别名写入测试——两次都必须失败才判定 A20 关闭），
   然后经由自己的 GDT 跳入 32 位 `core_head`。
2. **BIOS / MBR（已安装磁盘）**——静默的 512 字节 stage1 变体。
   位于镜像偏移 `+0x1A0` 的补丁块（`'OCBL'` 魔数、core LBA、
   扇区数——由 `ocbl-install` 写入）驱动 int 13h AH=42h 把 core
   读入 0x7E00。重试循环有意从不复位驱动器：复位会清除 SeaBIOS
   的 PIO 回退状态并导致死循环（实测发现的 WP06b b5）。镜像中
   446..509 字节保持为零，以便安装时把真实分区表拷贝到其上。
3. **UEFI**——`BOOTX64.EFI`，一个由树内 `ocbl/elf_to_efi.py`
   转换器构建的 PE/COFF 应用（从 `ld --emit-relocs` 导出完整的
   `.reloc` 目录；直接用 `ld -m i386pep` 的输出只会导出不完整的
   目录，OVMF 会错误重定位它）。扫描器、菜单与交接相同，外加
   GOP/GetMemoryMap/ExitBootServices 替换以及位于 0x30000 的
   64->32 降级 blob。
4. **VBR 链式加载（BIOS）**——外来 Windows/Linux/通用卷：读取
   卷引导记录，校验 0xAA55，拷贝到 0x7C00，降回实模式并跳转
   （GRUB 的 `chainloader +1` 契约）。只带 `BOOTX64.EFI` 的 ESP
   会被**如实拒绝**（“需要 UEFI 形态”）。
5. **UEFI 链式加载**——对外来 ESP 加载器执行 `LoadImage`/
   `StartImage`，boot services 保持存活，并先解除 OVMF 看门狗。
6. **关机**——通过 OCBL 自有的 FADT/\_S5 遍历执行 ACPI S5。
7. **自动恢复路由**——一条待处理的 BOOT_FAILED 记录使 OCBL
   一次性引导 OCIRE 条目，无需菜单交互。
8. **A/B 槽位选择**——第一个 OCOS 卷上的标志文件协议。

### A/B 槽位与自动回滚

槽位布局（`tools/make_ab_disk.sh`，文档见 [ota-ab.md](ota-ab.md)）：
分区 1 = 承载 `/boot/next_B`、`/boot/bootfail_B`、`/boot/ok_B` 的
FAT32 标志卷；分区 2 = 槽位 A；分区 3 = 槽位 B。OCBL 的
`judge_volume()` 实现与 `grub.cfg` 和 `grub-ab.cfg` 相同的语义
（三份实现中的标志契约逐字节一致）：

- `next_B` 存在且 `bootfail_B` 不存在 -> 引导分区 3（槽位 B）；
- `next_B` 与 `bootfail_B` 同时存在 -> 引导分区 2（槽位 A），标签
  后缀 `" rollback"`——**自动回滚**：槽位 B 内核在引导时悲观地
  写入 `bootfail_B`，只有当 `ota_update_confirm_boot()` 宣告系统
  完全起来后才将其删除，因此在引导中途死掉的 B 内核会在下一次
  复位时把机器回滚到 A；
- `ok_B` 存在 -> 槽位 B；
- 否则 -> 槽位 A。

所选槽位的卷会被重新挂载，条目也重新指向它；该磁盘的分区 2/3
已被“槽位占用”，不能再注册自己的条目。同时携带
`/boot/opencube.elf` 与 `/boot/ocire.elf` 的卷也会暴露 OCIRE 恢复
条目（WP06g3-4.4——扫描器的短路逻辑过去会把它藏起来）。

## 接下来去哪里

- 加载器链、扫描器类别与交接细节：[boot-flow.md](boot-flow.md)
- 目录树中每个目录里有什么：[tree-map.md](tree-map.md)
- 跨组件机制（状态孤岛、看门狗接力、守卫通道）：
  [hidden-mechanisms.md](hidden-mechanisms.md)
- ring 3 API 契约：[private-headers.md](private-headers.md)
- OCIRE 深入：[../dev/ocire.md](../dev/ocire.md)；面向用户的一面：
  [../manual/boot-modes.md](../manual/boot-modes.md)、
  [../manual/recovery.md](../manual/recovery.md)
