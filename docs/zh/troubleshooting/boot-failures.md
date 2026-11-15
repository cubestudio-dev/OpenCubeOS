<!-- SPDX-License-Identifier: Apache-2.0 -->
# 引导失败排查

四个程序参与一次引导：**OCBL**（加载器）、**内核**
（OCOS）、引导状态契约（**OCST**）与 **OCIRE**（恢复）。
本页按此顺序走遍每类失败，然后是 A/B 槽位逻辑，再是
刻意失败工具（/HANGAT）。

## 1. 把引导日志当检查单读

内核按装配顺序为每个子系统打印一行，格式为
`[HH:MM:SS.mmm] <stage>: OK|FAIL`。安静级别 1 抑制非 FAIL
行；任何含 `FAIL` 的行对状态判定器都是失败。
OCST 阶梯（也是 OCBL 监视的对象）：`BOOT`、
`KM_AIN_ENTERED`、`SCHED_READY`、`DRIVERS_READY`、
`SERVICES_READY`、`L1_LAUNCHED`、`L1_READY`，运行时 255 =
`RUNTIME`。

按位置排查：

1. **串口上什么都没有** — OCBL 没运行或 ISO 条目
   错误。引导 ISO 的另一个条目；试 BIOS 与 UEFI；用
   `-serial file:boot.log` 捕获。
2. **停在 multiboot2 握手之前** — mb2 交接失败
   （`[oc] FATAL: bad multiboot2 magic` / `multiboot2 parse failed`） —
   捕获串口并检查 mbi 标签解析；你到底有没有经
   OCBL/GRUB 引导？
3. **`[oc] FATAL: fb_init failed`** — 没有交接 framebuffer
   （OCBL 的 `[boot] VBE FAILED`/`GOP FAILED` 在它之前）。
   修复引导加载器的视频模式。
4. **停在某个驱动行** — 该家族的探针在你的硬件上
   失败；其上的消息指名设备。移除该设备（或 QEMU
   flag）以确认。
5. **到达 `L0 ready, waiting for L1` 但无提示符** — 启动器
   失败：`l1 launcher: no default L1 app declared`（构建损坏）或
   `failed to start the default L1 app`（ush 损坏 — 引导 ISO 并
   运行 `help_a_test`）。
6. **提示符之后重启** — panic（策略 REBOOT）、watchdog、或
   OCBL 判定停滞。第二次启动后：`crashlog`、`dumpinfo`、
   `dmesg`、`abcfg`（引导菜单尝试了什么）、`/var/log/crash.log`。

引导自测已经证明：异常路径（计数器 3/3）、
注册表计数（284）、崩溃恢复已武装。如果这些行打印了
OK，平台是健全的 — 看具体特性，别怀疑整台
机器。

## 2. OCBL 的六类扫描器 — 当某个条目缺失

`ocbl_scan_all()` 枚举驱动器，将 CD 类限制到 2048 字节
扇区，扫过分区，然后回退到整盘
"superfloppy" 检测。每个卷按短路优先级判定：

| 优先级 | 类 | 检测 | 菜单标签 |
|----------|-------|-----------|------------|
| 1 | OCSE | 携带 `/ocse.mrk` 的 ISO9660 卷（最先判定 — 安装介质绝不会被当作 OCOS 引导，即使上面有内核） | `OpenCubeOS Setup (OCSE)` |
| 2 | OCOS | 任意带 `/boot/opencube.elf` 且其前 8 KiB 含 mb2 魔数 `0xE85250D6` 的卷 | `Open Cube OS (slot A\|B, <size> KiB)` |
| 3 | OCIRE | 任意带 `/boot/ocire.elf` 的卷（含 0xEE 影子与 ISO） | `OCIRE recovery` |
| 4 | WINDOWS | `/EFI/Microsoft/Boot/bootmgfw.efi` | `<volume> ...` |
| 5 | LINUX | 带 `/EFI/<vendor>/{grubx64,shimx64,systemd-bootx64}.efi` 的 ESP | `<volume> (hd<N> gpt\|p<N>)` |
| 6 | GENERIC | `/EFI/BOOT/BOOTX64.EFI` | `EFI loader` |
| — | SHUTDOWN | 合成项，恒为最后 | `Shutdown` |

已声明的限制：OCBL 中没有 ext2/3/4 驱动 — Linux/Windows
经其 ESP 检测，绝不经 ext/NTFS 根。

如果你的卷没出现：

- 标记文件缺失/损坏（不带 mb2
  魔数的 `/boot/opencube.elf` 注册为无）。重新安装。
- 同时携带 `/boot/opencube.elf` 与 `/boot/ocire.elf` 的卷
  也会暴露 OCIRE 条目（短路不再隐藏它）。
- A/B 磁盘的分区 2/3 被"槽位占用" — 它们无法
  注册自己的条目。
- 串口轨迹 `[scan] entries: <N>` 计数所发现的内容。

## 3. OCBL 自身的失败

- `[boot] FATAL: payload volume remount failed` — 内核加载前的
  /boot 重挂载；重新安装。
- `[boot] FATAL: kernel load failed` — ELF 未加载（缺失、
  损坏，或段超出 `[want_base, 0x40000000)`；
  防溢出边界即 BUG-46 修复）。
- `[efi] FATAL: fixed low-memory windows not ...` — 0x98000/0x9B000
  的 OCST 岛被固件占用；释放低内存或用 BIOS。
- `[efi] FATAL: memory map failed` / `ExitBootServices refused` —
  固件内存服务故障；重试，更新固件。
- `[chain] VBR read failed` / `LoadImage failed` — chainload
  目标不可引导（对非 PE 桩测试盘属预期）。
- `[boot] ACPI S5 shutdown` 然后 `[boot] S5 did not power off; halting`
  — 主板忽略 S5。

## 4. 引导状态契约（OCST）与判定器

两块固定的低内存块在复位后存活：

- **0x98000**（魔数 `OCS1`）：活动阶段 + 心跳 + kflags —
  内核盲写它。
- **0x9B000**（魔数 `OCB1`）：事后记录
  `{kind, stall, seq, ts_unix, silence_sec, routed, checksum}` — kind
  为 BOOT_FAILED 或 USER_REQUEST。

预算：启动 30 s（BOOT→KM_AIN_ENTERED），每阶段 10 s，L1
等待 30 s，运行时心跳 15 s。判定阈值：**RTC 静默 ≥ 8 s**
= 引导失败。判定失败时 OCBL 写入 BOOT_FAILED，
显示 `Open Cube OS failed to boot at stage: <STAGE>`（驻留 4 秒），
脉冲 8042 复位 — 下次引导**自动路由到 OCIRE，
单次**（`ocbl_statemon_route_pending()`）。

如实的非失败（不要追查这些）：`previous boot shut down cleanly (seq <n>)`、`warm reset with recent progress (silence <n>s) - not a failure`、`previous boot armed no payload - no failure`。当
不存在硬件 watchdog 时：`BOOT_FAILED record pending ... detection degrades to the next reset`。RTC 冻结：`[rtc] clock frozen - skipping the dwell`（静默不可测 — 检查 RTC 电池）。

## 5. OCIRE 接管 — 恢复何时发生、它说什么

以下情形你会落入 OCIRE：(a) OCBL 判定 BOOT_FAILED（自动路由，
经 `routed` 位严格单次）；(b) 你选择了 OCIRE 菜单
条目（USER_REQUEST）；(c) 监督器是磁盘默认
（`ocire.mode=sup`）。

OCIRE 的第一屏自我解释（`ocire_ocbl_report()`）：

- `Recovery session requested by the user (seq <n>)` — 你的菜单选择。
- `Previous boot FAILED at stage <STAGE>` + 逐阶段建议：
  RESET/KM_AIN/SCHED → 运行 `syscheck`；DRIVERS → `syscheck` + 复查
  驱动；SERVICES → `repair`；L1_LAUNCHED → `syscheck`；L1_READY 与
  运行时 → `logread` + `repair`。
- `No boot failure was judged - this is a manual recovery session.`

然后是恢复阶梯：`syscheck`（对卷的 10 项检查 —
判定如 `BAD (magic/state invalid)`、`MISSING/UNREADABLE`、
`DIGEST MISMATCH`）、`logread`（OCBL 引导日志；`BAD ring header`
→ `repair quick`）、`repair`（从黄金集重建；`Type YES`
确认会重新格式化系统卷）。监督器级
失败（`mailbox write failed - skipping the reboot to avoid a reset loop`）意味着重启前必须先修复邮箱 — 该防护
是 BUG-47 修复。

## 6. A/B 槽位与回滚

磁盘上的 A/B 布局：p1 引导/标志（FAT32，32 MB：`/boot/next_B`、
`/boot/ok_B`、`/boot/bootfail_B`、grub.cfg），p2 槽 A（128 MB），p3
槽 B（128 MB），p4 /data。`judge_volume()` 中的槽位选择：

- `next_B` 存在且 `bootfail_B` 缺失 → 引导 **B**（分区 3）。
- `next_B` + `bootfail_B` 同时存在 → **回滚到 A**（分区 2），菜单
  标签追加 ` rollback` — 这就是自动回滚触发器：
  B 内核在引导时悲观地写入 `bootfail_B`，仅在系统完全
  起来后才删除它。
- `ok_B` 存在 → B；否则 → A。
- 当前槽位经 mb2 cmdline `oc.slot=A|B|ISO` 到达；
  引导尝试预算存于 `/boot/grub/grubenv`（`ab_attempts`，
  上限 3；A 完成引导或 B 确认时内核重置它）。

命令：`update status`（槽位 + 版本 + `A/B disk: present`
锚点）、`abcfg`（引导配置）、`rollback`（清除标志）、
`ab_partition_test`（协议测试）。过期标志令菜单选错
槽位：从 OCBL 菜单强制另一槽位，然后从运行中的系统
执行 `update rollback`。

## 7. /HANGAT — 刻意停滞与排除法

`/HANGAT` 是载荷卷上的一个 1 字节诊断文件（'1'..'7'）；
OCBL 静默地向内核 cmdline 追加 `oc.hang=N`，
随系统发布的内核刻意在阶段 N 后停机（'7' = L1_READY 之后的
运行时挂起）。目的：从发布镜像演练完整的
watchdog→复位→OCBL 判定→OCIRE 路由链，**无需测试构建的
内核**。

存在时的症状（而你并非有意为之）：内核在某个
阶段关中断停机；串口显示 `[ocst] /HANGAT diagnostic: will halt after stage <N>` 与 `[ocst] HANGAT: deliberate halt with interrupts off `；OCBL 显示 ` (the kernel will halt after the stage)`。

**排除法**：如果引导在硬件干净的前提下持续"失败"于同一
阶段，先检查载荷卷上的 `/HANGAT` — 从 OCIRE 或另一台
机器挂载该卷并删除它。没有该文件时完全无操作。

## 8. OCSE 引导失败（安装介质）

- `[ocse] FATAL: no usable disk - halting` — 接入一块目标磁盘。
- `ocblmbr.bin MISSING` / `ocos.tar MISSING` — 安装介质
  不完整：重新下载 ISO，核对大小/SHA（BUG-107 是
  历史上的不匹配）。
- `[ocse] STAGE <n> FAILED: <what>` — 安装器内核自身的
  装配；镜像的 OCOS 消息适用。
- 安装器步骤链及其修复：
  [error-catalog.md](error-catalog.md) §3.4。

## 相关页面

- [error-codes.md](error-codes.md) — panic 位点、阶段格式
- [error-catalog.md](error-catalog.md) §1-§4 — 每条引导侧消息
- [../manual/boot-modes.md](../manual/boot-modes.md) — 八种引导
  模式
- [../manual/recovery.md](../manual/recovery.md) — OCIRE 用法
- [qemu-debugging.md](qemu-debugging.md) — 捕获、三重故障
  调试
