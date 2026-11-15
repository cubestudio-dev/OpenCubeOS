<!-- SPDX-License-Identifier: Apache-2.0 -->
# 报错库 — 按子系统罗列的每条失败消息

这是全仓失败消息清单：你实际看到的字符串，
按组件与文件组织，每条附**触发 → 排查 → 修复**。代码树中存在
约 1700 条不同的错误语义字符串；重复模板在此合并并附文件
清单，因此本页是完整地图而非样本。数值代码在
[error-codes.md](error-codes.md)；不打印*任何*内容的失败在
[silent-failures.md](silent-failures.md)。

排查命令按 `ush>` 提示符拼写。万用的阶梯：`health` →
`dmesg 100` → 该家族的状态命令 → 该家族的 `_test` 桩
（[../manual/hidden-commands.md](../manual/hidden-commands.md)）。

---

## 1. OCBL — 引导加载器（5 类）

### 1.1 BIOS 载荷路径（`ocbl/common/ocbl_main.c`）

- `[boot] FATAL: payload volume remount failed` — 内核加载前的
  /boot 重挂载失败。排查：引导 ISO 的另一个条目；
  捕获串口（`-serial file:`）。修复：从可正常引导的环境重新执行
  `install <dev>` 或 `grub-install <dev>`。
- `[boot] VBE FAILED (kernel will refuse)` — 无 VBE 模式；OCOS
  要求 framebuffer。修复：启用 VGA/VBE 设备（QEMU：默认 VGA；
  真实硬件：BIOS 视频设置）。
- `[boot] FATAL: kernel load failed` — `/boot/opencube.elf` 的
  ELF 加载失败（缺失/损坏）。修复：重新安装，或
  引导槽位 B / ISO 并修复该卷。
- `[boot] ACPI S5 shutdown` / `[boot] S5 did not power off; halting` —
  菜单关机路径；第二行表示主板忽略 S5。请使用
  物理/宿主机电源控制。

### 1.2 UEFI 变体（`ocbl/efi/efi_entry.c` + 辅助文件）

- `[efi] FATAL: fixed low-memory windows not ...` — 0x98000/0x9B000
  的 OCST 岛被固件占用。修复：固件设置中释放
  低内存的选项，或引导 BIOS 变体。
- `[boot] FATAL: chainload volume remount failed`（含载荷变体）
  — 目标 ESP 不可读。修复：在另一台机器上校验该 ESP。
- `[boot] GOP FAILED (kernel will refuse)` — UEFI GOP 未给出模式。
  修复：固件视频设置；OVMF 默认值可用。
- `[boot] FATAL: kernel load failed` — 同 §1.1。
- `[efi] FATAL: memory map failed` / `[efi] GetMemoryMap failed: 0x<hex>` — EFI 内存服务拒绝。重试；固件 bug 类。
- `[efi] EBS retry (0x<hex>)` / `[boot] FATAL: ExitBootServices refused` — ExitBootServices 重试后仍致命：某个引导驱动仍
  持有过期的 map key。更新固件 / 使用 BIOS 变体。
- Chainload 辅助：`[boot] FATAL: chainload buffer alloc failed`、
  `chainload target not readable: <path>`、`[boot] LoadImage failed: 0x<hex>` — 外来加载器缺失/非 PE。对非 PE 桩测试盘属于
  预期；修复目标 ESP。
- `[boot] GOP: SetMode failed, keeping current` — 良性；使用
  当前模式。

### 1.3 Chainload（`ocbl/bios/ocbl_chain.c`）

- `[chain] VBR read failed` — 所选卷的引导记录
  不可读。修复：该卷实际不可引导；选择另一个
  条目。

### 1.4 引导失败判定器（`ocbl/common/ocbl_statemon.c`）

- 屏幕：`Open Cube OS failed to boot at stage: <STAGE>` +
  `Rebooting to recovery...` — OCBL 判定 RTC 静默 ≥ 8 s 超出
  阶段预算（启动 30 s / 每阶段 10 s）。下一次引导
  自动单次路由到 OCIRE。
- 串口：`[state] previous boot judged FAILED at stage <STAGE_NAME> (silence <n>s) - BOOT_FAILED recorded`。
- 如实的非失败（信息性）：`previous boot shut down cleanly (seq <n>)`、`warm reset with recent progress (silence <n>s) - not a failure`、`previous boot armed no payload - no failure`。
- `[state] BOOT_FAILED record pending (stall <n>s) - detection degrades to the next reset` — 无硬件 watchdog 强制预算；判定
  改在下次复位时落地。
- `[state] USER_REQUEST recorded - recovery session` — 你在菜单中
  选择了 OCIRE。
- `[rtc] clock frozen - skipping the dwell` — RTC 不走字；静默
  无法测量。修复：RTC 电池 / 启动后运行 `rtc`。
- ` (the kernel will halt after the stage)` — /HANGAT 诊断已
  武装；见 [boot-failures.md](boot-failures.md)。

### 1.5 扫描/菜单标签（`ocbl/common/ocbl_scan.c`）

不是错误，而是扫描的可见结果：`Open Cube OS (slot B|A, <size> KiB)`（当 `next_B`+`bootfail_B` 强制槽位 A 时附
` rollback`）、`OpenCubeOS Setup (OCSE)`、`OCIRE recovery`、
`EFI loader`、`<volume> (cd|hd<N> gpt|p<N>)`。串口轨迹：
`[scan] entries: <N>`。缺少条目意味着未找到标记文件 — 见
[boot-failures.md](boot-failures.md) §scanner。

## 2. OCIRE — 恢复内核（5 类）

### 2.1 监督器（`ocire_bootsvc.c`）

- `Reset failed on this machine; halting.` — 8042/CF9 复位均
  无效。使用宿主机电源控制。
- `Supervisor: no OCIRE recovery volume found ` — hd1 0xEE+FAT12
  卷缺失。修复：带恢复分区重新安装
  （OCSE 安装器会创建）。
- `Supervisor: recovery volume malformed (mailbox/log missing). Entering the full shell.` — 影子卷存在但其
  邮箱损坏。运行 `syscheck`，然后从完整 shell 执行
  `repair quick`。
- `Supervisor: mailbox write failed - skipping the reboot to avoid a reset loop; entering the full ...` — 防复位循环防护
  （BUG-47 修复）。邮箱不可靠；重启前先修复它。
- `WARNING: log agent digest mismatch` + `The agent will NOT be injected (tamper guard).` — 代理镜像与
  邮箱摘要不匹配。按篡改或损坏处理：`syscheck`、
  `repair`。
- `Supervisor: /BOOT/OPENCUBE.ELF not found on the ...` / `OCOS kernel too large (<n> ...)` — kexec 源缺失/超大。
  修复：`syscheck` 然后 `repair` 从黄金集重建系统卷。
- `Supervisor: FATAL - cannot write the mailbox. ` — 嵌入通道
  已死；恢复路由降级。`repair`。
- `Watchdog arm FAILED - continuing without it ` — 6300ESB 缺失；
  恢复在没有硬件缰绳的情况下继续。
- `Supervisor: kexec refused the image (rc <n>)` — OCOS ELF 未通过
  校验。`syscheck`。

### 2.2 syscheck / repair（`ocire_recover.c`）

卷校验判定（检查 1-10）：`FAIL`、`BAD (magic/state invalid)`、`BAD (magic/cursor invalid)`、`MISMATCH (agent image vs mailbox digest)`、`FAIL (cannot parse)`、`... MISSING/UNREADABLE`、
`... DIGEST MISMATCH`、`I/O ERROR`、`4-10. skipped (volumes incomplete)`、`8-10. skipped (no manifest)` — 各自指名一个损坏
部件；`repair` 是它们共同的应对。
- `logread: recovery volume not found (run syscheck).` /
  `logread: cannot read /LOG/BOOT.LOG.` / `logread: BAD ring header (magic/cursor) - use 'repair quick' to reset it.` — 引导日志
  通道；`repair quick` 重置环形缓冲。
- 修复流程失败：`REBUILD FAILED`、`RESET FAILED`、
  `FAILED - aborting (volume left as-is).`、`failed (re-run or use another golden set)`、`cannot verify files. Aborting.` — 重新运行
  repair；若重复出现，则是黄金集本身损坏。
- `WARNING: this REFORMATS the OpenCubeOS system volume; all data on it will be lost.  Type YES to continue:` — 刻意的
  破坏性确认；必须准确输入 `YES`。

### 2.3 停滞解释（`ocire_ocbl.c`）

`The kernel failed to start or stalled in its earliest ...`（3 种阶段
变体，均以 `retry the boot.` 结尾）、`The default L1 application (ush) failed to come up. Run syscheck to verify the system image, then retry ...`、`Unknown stall point. Run syscheck ...` — 这些是
OCBL 判定的用户面映射；遵循其中嵌入的建议。
引导记录回显行（`Recovery session requested by the user (seq <n>)`、`No boot failure was judged - this is a manual recovery session.`、`Previous boot FAILED at stage <STAGE>`）说明你为何
处于恢复环境。

### 2.4 异常（`ocire_idt.c`）

`<vec #XX name>` + ` error 0x<hex>` + `System halted.` — OCIRE
自身内部的未处理故障（7 个向量名：#DE/#OF/#UD/#TS/#GP/#CP，
回退 `unknown vector`）。捕获串口；这是 bug 类。

### 2.5 Shell / 引导如实性（`ocire_shell.c`、`ocire_main.c`）

- `Unknown command: <cmd>` — 恢复 shell 很小；`help` 列出
  全部命令。
- `Power off not supported on this machine; halting.` /
  `Reset failed on this machine; halting.` — 平台限制。
- `ACPI: not found - legacy power ports will be used` — 如实
  降级，不是错误。
- `=== ocire-agent: klog snapshot refused ===` — 仅限 guard 的
  klog op 拒绝了非 guard 调用方；探测该通道时属于预期。

## 3. OCSE — 安装器环境（6 类）

### 3.1 阶段失败（`ocse/kernel/main.c`、`ocse_main.c`）

- `[ocse] STAGE <n> FAILED: <what>` — 安装器内核的某个引导
  阶段失败（串口 + VGA + 引导日志）。阅读 `<what>`；镜像的
  内核消息见下文 §5/§6。
- `[ocse] FATAL: no usable disk - halting` — 无安装目标。
- `... B, ocblmbr.bin MISSING` / `[ocse] medium check: ocos.tar MISSING` — 安装介质不完整（刻录/下载损坏）。
  重新下载 ISO；核对大小/SHA。
- `direct rendering (alloc failed)` — 安装器 GUI 无法分配
  其 framebuffer。

### 3.2 ocsecl — 命令行（`ocse_cl.c`）

导航/文件：`ls: bad path`、`cd: bad path`、`cat: cannot open `、
`write: cannot create `、`touch: cannot create `、`mkdir: cannot create (exists, or parent missing)`、`rmdir: cannot remove  (not found, not a directory, or not empty)`、`rm: cannot remove `、`cp: bad source name`、`cp: destination path too long`、`cp: copy failed (err <n>)`、
`mv: moving a directory across parents is not supported` — 标准的
路径/参数错误；先 `ls`/`pwd` 目标。
分区：`mkpart: bad start` / `bad end`、`rmpart: bad partition number`、`setflag: bad partition number`、`install: bad partition number` — 数值防护。`rmpart: WARNING: deleting a partition DESTROYS ITS DATA!` 和 `: WARNING: formatting DESTROYS ALL DATA on the target device!` 是破坏性确认。
tar/dd/sha：`tar: cannot open `、`tar: out of memory`、`tar: archive error (<n>)`、`dd: cannot open input: <d>`、`dd: I/O error (<n>)`、
`sha256sum: read error`、`sha256sum: bad sector count` — 介质或
内存问题。
杂项：`' - unknown command (try 'help')`、`click: bad coordinate`。

### 3.3 向导字典（`ocse_i18n.c`）

`disk not found: `、`partition not found`、`invalid range (need start < end <= disk size)`、`format failed`、`... failed (busy or not a mount point)`、`install: FATAL - install medium incomplete (OCOS.TAR / OCBLMBR.BIN)`、`config: invalid key or value`、`A command failed:` —
GUI 向导的失败文案（EN/zh 成对）；每条映射到与其同名的
ocsecl 步骤。

### 3.4 安装器（`ocse_inst.c`）— 步骤链

`ocbl-install: cannot open <f>`、`ocbl-install: stage1 verify FAILED`、
`ocbl-install: core verify FAILED`、`install: recovery partition skipped - `、`recovery kernel unreadable`、`mkpart failed`、
`created partition not found`、`partition device missing`、
`mkfs.fat32 failed`、`mount failed`、`cannot create /boot/ocire.elf on it`、`recovery kernel short read`、`install: FATAL - no such partition: <p>`、`install: FATAL - extraction failed`、
`install: FATAL - /boot/opencube.elf missing after extraction`、
`install: FATAL - ocbl-install failed`、`install: FATAL - could not set the boot flag`、`config: cannot write the configuration` — 该链
是顺序的；第一条消息指名损坏的步骤。几乎全部
归结为：目标磁盘损坏（先运行磁盘检查）或介质损坏
（§3.1 的 MISSING 类）。

### 3.5 输入防护（`ocse_wizard.c`）

`[wizard] refuse: existing ...`、`mount (too long)`、`install (too long)`、`write /mnt/etc/users (record too long)`、`The user record (name + full name) is too long for the ...` — 长度防护；缩短
输入。

### 3.6 mkfs + 镜像池（`ocse_mkfs.c`）

`: device name too long` — mkfs 参数防护。OCSE 的其余
驱动/文件系统/内核错误文件逐字节镜像了 §5/§6 的 OCOS 字符串
（相同的 gzip/tar/config 枚举，相同的 fs/驱动消息） —
一个共享池；按字符串去重，不按文件。

## 4. OCOS 内核（7 类）

### 4.1 引导 FATAL 与阶段 FAIL（`core_boot_main.c`）

硬失败（停机）：
- `[oc] FATAL: bad multiboot2 magic` — 未经 OCBL/GRUB mb2 引导。
  修复：通过 ISO 的 OCBL 菜单引导。
- `[oc] FATAL: multiboot2 parse failed` — mbi 标签遍历遇到非法
  标签。捕获串口；加载器 bug 类。
- `[oc] FATAL: fb_init failed` — 未传入 GOP/VBE 模式。修复
  引导加载器的视频交接。

阶段 FAIL 判定（`[HH:MM:SS.mmm] <stage>: FAIL`）：
- `WP-01 extension self-test` — 4 个 L1 扩展金丝雀之一
  失败。内核面回归；报告。
- `exception self-test` — #UD/#PF/#BP 自测计数器错误。报告。
- `ACPI: RSDP not found (no ACPI power paths)` — 无 RSDP；电源
  功能降级为遗留端口。
- `CMOS RTC: probe inconclusive (VRT not set)` — RTC 电池缺失
  （VM 恒如此）；`date` 回退为毫秒自启动。
- `HPET: no usable ACPI HPET table` / `ACPI PM timer: no usable PM_TMR_BLK` — 定时器源降级；TSC/PIT 继续。
- `kmalloc/kfree self-test` — 堆往返失败。报告。

### 4.2 L1 启动（`core_boot_l1launch.c`）

- `l1 launcher: no default L1 app declared; entering recovery shell` —
  构建损坏（注册表为空）。报告。
- `l1 launcher: failed to start the default L1 app; entering recovery shell` — ush 启动失败。引导 ISO，运行 `help_a_test`。
- `l1 launcher: default L1 app exited; L0 halting (reboot or use the boot menu)` — ush 返回（会话结束；`shutdown`
  后属正常）。

### 4.3 OCST 状态区（`core_boot_state.c`）

- `absent - stage recording only, no hardware reset ` — 无 6300ESB
  watchdog：引导失败检测降级为下次复位判定。
- `[ocst] /HANGAT diagnostic: will halt after stage ` / `[ocst] HANGAT: deliberate halt with interrupts off ` / `[ocst] HANGAT runtime: deliberate halt with ` — /HANGAT 文件存在！从载荷卷删除
  `/HANGAT`（见
  [boot-failures.md](boot-failures.md)）。
- `[ocst] L1 ISOLATION FAILURE: a user page covers 0x<hex>` — 某个
  用户 PTE 映射了仅限管理者的 OCST 邮箱窗口。安全检查
  触发；立即报告。

### 4.4 ELF 加载器，静态路径（`core_usermode.c`）

- `[user] no address space` — 进程创建时 PMM/VMM 耗尽。
  `mem`、`free`；停止吃内存的作业。
- `[user] not an ELF file` / `[user] not ELF64` — exec 目标
  不是有效 ELF64。对 `badapp` 类固定件属预期。
- `user: process table full (max <N> processes)` — 并发进程
  过多。`ps`，`kill <pid>` 清理掉队者。

### 4.5 动态 exec（`core_usermode.c`，`dynlink:` 前缀）

`PT_INTERP: (offset out of bounds)`、`dynlink: PT_INTERP not present`、
`dynlink: no embedded ld.so for path`、`dynlink: ld.so too small` /
`not an ELF file` / `not ELF64` / `has no PT_LOAD segment`、
`dynlink: main image failed ELF header checks`、`dynlink: main PT_LOAD out of user window`、`dynlink: failed to map main PT_LOAD`、
`libfoo` 孪生 — 动态链断裂；几乎总是构建或
注册表完整性问题。用 `ldd <program>` 验证；重新运行
`l1test`。

### 4.6 程序头校验（两条路径）

`[user] bad phentsize`、`[user] program-header table out of bounds`、
`[user] PT_LOAD file range out of bounds`、`[user] PT_LOAD vaddr+memsz overflow`、`[user] PT_LOAD memsz < filesz`、`[user] PT_LOAD vaddr in kernel space`、`[user] PT_LOAD segments overlap` —
被加载的 ELF 畸形/敌意。如果是随系统发布的程序，
报告；如果是你自己的，修复链接脚本。

### 4.7 调度器 + 配置（`core_sched.c`、`core_lib_config.c`）

- `PANIC: idle task stack allocation failed - halting` — 调度器初始化
  OOM。增加内存（512 MiB 从容）。
- klog WARN `kthread %s: guard page unmap FAILED at 0x%lx (stack protection absent)` — 2 MiB 大页拆分失败；该任务
  在无栈溢出防护下运行（bug-list BUG-4）。无害但
  值得报告。
- `config: backend bring-up FAILED` — /etc 无法挂载；
  配置降级为 ramfs（重启后变更丢失）。见
  [storage-issues.md](storage-issues.md)。
- `config: invalid auto_check value - using no (safe default)` /
  `config: invalid online_update value - using yes (default)` —
  配置文件含垃圾值；已应用默认值。用 `config set` 修复。

## 5. OCOS 服务（21 类，按家族）

### 5.1 audio

- `play: out of memory` / `play: device error` — 分配或设备
  失败。`sound` 列出设备；`mem` 检查内存。
- `: unknown device '<name>` — `play -d`/`volume` 目标未
  注册。使用 `sound` 中的名字。
- `[audio_rw_test] kmalloc(48000) failed => FAIL` — 环回测试
  期间 OOM。
- `[audio_rw_test] no sound cards registered => SKIPPED` — 如实
  跳过；接入音频设备（QEMU `-device intel-hda`）。
- `driver rejected the rate` / `(unsupported, correctly rejected) => PASS` — 负面固定件：非法速率就应当被拒绝。
- QEMU-10 的 `hda` 模型在 44.1 kHz 上死循环 — 已知模型限制；
  真实硬件编程正常。

### 5.2 block — 缓存 + 设备保护

- `blkcache: alloc FAILED - cache disabled (raw I/O)` — 64 槽
  LRU 写回缓存无法分配；系统继续
  无缓存运行（更慢）。`mem`；若持续则报告。
- `blk: refusing device '<name>` — 注册几何非法的设备。
  驱动层问题；报告。
- `blk: write to protected device <name> REFUSED (ocire recovery disk)` / `blk: cached write to protected device REFUSED (ocire recovery disk)` — 整盘 OCIRE 保护已武装；对恢复卷的写入
  **按设计**被阻断。要修改该卷请使用 OCIRE 自己的
  `repair`。

### 5.3 block — parted/mkfs/fsck 命令

- `parted: device not found` / `: device name too long` — 设备
  字符串错误。先 `lsblk`。
- `mkfs.fat32: device not found` / `mkfs.fat32: device too small` /
  exfat 变体（`device too small (need at least 65 sectors, got <N>)`、`device too small for FAT + heap`）/ `mkfs.ext4: device not found` — 目标检查。从 `lsblk` 挑一个真实且足够大的设备。
- `WARNING: formatting DESTROYS ALL DATA on the target device!` —
  确认提示；裸 `y` 继续。
- fsck（仅 FAT32）：`fsck: cannot read boot sector`、`fsck: invalid boot signature (not 0x55AA)`、`fsck: not a FAT32 volume (fsck supports FAT32 only; got invalid BPB)`、`fsck: unsupported bytes_per_sector (fsck reads raw 512-byte sectors; got <N>)`、
  `fsck: out of memory`、`fsck: FAIL (<detail>)`、`fsck: FAIL (cluster count mismatch: counted <A> ...` — 该卷不是可读的
  FAT32。`fatstat <dev>` 显示解析的 BPB；若数据可弃则重新
  格式化。

### 5.4 block — install/abdisk/grub-install 链

- `grub-install: MBR gap too small for <N>` — MBR 后间隙放不下
  core.img。用 ≥ 1 MiB 间隙重新分区（现代布局）或使用
  GPT。
- `grub-install: failed to write boot sector` / `failed to write core.img` — 写入失败；检查介质。
- abdisk 链：`abdisk: device not found`、`device too small (need <N>)`、`WARNING - all data on <dev>`、`failed to write partition table`、`failed to register partition`、`mkfs.fat32 failed on p<N>`、
  `mount p1 failed`、`failed to write grub.cfg`、`mount p2 failed`、
  `failed to write slot A kernel`、`grub-install failed` — 顺序的；
  第一处失败才是真凶。磁盘损坏或目标过小
  覆盖多数情形。
- install 链：`install: device not found`、`device too small (need >= 64 MiB ...)`、`disk too large for MBR (>2TiB) or too small`、
  `failed to write partition table`、`failed to register p1`、
  `mkfs.fat32 failed`、`mount p1 failed`、`failed to write /boot/opencube.elf`、`failed to write grub.cfg`、`grub-install failed` — 同一解读规则。
- `(build system error)` — 构建中缺失某个内嵌资产。
  重新构建。

### 5.5 camera（UVC/IR/parallel/gameport/SCSI）

- `nec corrupted rejected`、`mjpeg truncated rejected`、`set format unknown rejected`、`set frame mismatch rejected`、`capture refused before start` — 负面固定件；它们 PASS 才是正确行为。
- `no parallel port present - skipping live steps`、`no stick (timeout)`、`<N> failures - PASS|FAIL` — 如实的硬件缺失
  报告。

### 5.6 crash

- `crashlog: flush FAILED (no filesystem mounted?)` — 无 VFS 挂载可写
  /var/log。挂载一个卷，重新运行 `crashlog`。
- panic 时的 `crash log: ... FAILED (no filesystem)` — panic
  本身发生在文件系统之前/没有文件系统；串口控制台
  携带该记录。

### 5.7 crypto

- 自测判定 `SHA-256: FAIL`、`AES-128: FAIL`、
  `HMAC-SHA256: FAIL`、`HMAC-SHA384: FAIL` — 加密原语损坏：
  构建/工具链问题；报告。
- `DIFFERENT (memory corruption!)` — 堆金丝雀检查；立即
  报告。
- `RSA modulus too small`、`pss: bad DB padding at %d: %02x` — RSA-PSS
  填充不合规范（敌意或损坏的对端）。
- X509 校验字符串：`bad certificate format`、`unsupported algorithm`、`signature verification failed`、`hostname mismatch`、
  `issuer certificate missing`、`unknown error` — 见
  [error-codes.md](error-codes.md) 中的 X509_E_*。

### 5.8 diag（自测控制台，121 条字符串 — 精选）

- `usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]` — 语法；见
  [../manual/hidden-commands.md](../manual/hidden-commands.md)。
- `crashtest: #UD returned unexpectedly` / `unmapped write returned unexpectedly` / `assert returned unexpectedly` — 注入的故障
  竟然存活，这本身就是失败。报告。
- `crashtest: dump FAILED (no boot-time buffer)` — 启动期转储
  缓冲未分配（内存受限的启动）。
- nice 防护：`nice: tid too large`、`nice: invalid tid`、`nice: priority out of range (0..31)`、`nice: invalid tid or prio (0..31)`、
  `nice: invalid tid (not in use)` — 记住它是 TID（来自 `ps`）。
- VFS 回归行 `BUG-0178 legal O_RDWR write: FAIL`、
  `BUG-0179 case-sensitive ramfs: FAIL`、`BUG-0180 hole is zeros: FAIL`、`BUG-0182 mode-0000 refused: FAIL`、`BUG-0184 umount / refused: FAIL` — 这些标记已修复的 bug；此处 FAIL 即回归。
  附 fstest 输出报告。
- `  FAIL: kernel PT frame was FREED by user-AS destroy (BUG-0136)` —
  同类（回归标记）。
- 堆回绕防护：`FAIL: wrap size 0xFFFFFFFFFFFFFFF1 was served`、
  `FAIL: 64 GiB size was served`、`FAIL: legitimate 4096 B alloc refused` — 分配器加固回归；报告。
- i2c：`i2cget: error (<code>)`、`i2cset: error`、
  `controller: i801/PIIX4 not present FAIL` — 控制器缺失（或
  i801 状态字：NACK/bus error/transaction failed/timeout）。

### 5.9 display

- `chinesetest: FAILURES PRESENT` / `encoding-test: FAIL` /
  `ime_test_fail` — CJK 测试组失败；报告（字体 blob 或编解码器
  回归）。
- touchtest 判定块：`touchtest: FAIL` + 每手势 `descriptor parse FAIL`、`TAP gesture FAIL`、`SCROLL gesture FAIL`、`PINCH gesture FAIL`、`SWIPE gesture FAIL` — 描述符或手势引擎问题。

### 5.10 font

- `chinese_font gz layout mismatch - regenerate chinese_font_data.c` —
  构建期防护（同时是 _Static_assert）；重新生成字体数据。
- 负面固定件（`zone 95 rejected`、`wei 0 rejected`、
  `surrogate ED A0 80 rejected`、...） — 敌意输入；每次拒绝
  都是一次 PASS。

### 5.11 fs

- VFS：`vfs_init: failed to allocate global root`、`vfs_mount: invalid path`、`vfs_mount: unknown fs type`、`vfs_mount: fs mount failed`、
  `vfs: cannot unmount the root filesystem`、`vfs_mount: parent path does not exist`、`vfs_mount: mount point exists and is not a directory` — 挂载参数问题；`mounts` 显示现存内容。
- `ramfs: mount at / failed` — 启动时致命（无 rootfs）；报告。
- L1 界面：`tree: bad path`、`mount: failed`、`umount: bad path`、
  `umount: cannot unmount the root filesystem`、`umount: target is busy - close open files first`、`umount: unmount nested mounts first`、`umount: failed (not mounted?)`、`... FAILED - no FAT32 partition on device or drive not present` — 关闭打开的文件、
  先卸载子挂载（`mounts` 显示嵌套）。
- FAT32：`fat32: invalid device string`、`fat32: read boot sector failed rc=<N>`、`fat32: invalid BPB`、`fat32: not a FAT32 volume (FAT12/16 detected)`、`fat32: ext_flags active FAT out of range`、
  `fat32: FAT cache alloc failed`、`fat32: FAT read failed` — 该
  卷不是可挂载的 FAT32；`fatstat` 显示解析的 BPB。
- exFAT：`exfat: invalid number_of_fats`、`exfat: warning - volume was not cleanly unmounted`（脏标志 — 挂载继续），其余同
  FAT32。
- ext4（只读）：`ext4: not an ext4 volume (bad magic)`、
  `ext4: unsupported INCOMPAT feature bits 0x<hex> - mount refused`、
  `ext4: unsupported block size (log_block_size > 6)`、`ext4: invalid inode size (must be 128..1024, power of two)`、`ext4: read root inode failed` — 该卷使用了 RO 驱动拒绝的特性；
  在别处只读挂载以检查。

### 5.12 health

- `health: export FAILED` / `health: export FAILED (no filesystem mounted?)` — 导出需要可写目标；挂载一个卷。
- 告警（`alarm: memory >90% used`、`alarm: <n> active tasks (>64)`、
  `alarm: CPU >=90% busy for 10 consecutive seconds`） — 见
  [silent-failures.md](silent-failures.md) §health-first。

### 5.13 ipc / l1 / loader

- `unknown program: <name>` — `run`/注册表查找失败。`help -a`
  核对拼写。
- `not a dynamic executable (unknown type)` — 对静态程序运行
  `ldd`；属预期。
- 加载器负面固定件（`so_unload(0) = -1 (NULL handle rejected) - PASS`、`elf_get_needed(truncated 48B) = 0 (rejected) - PASS`、...） —
  敌意输入拒绝；正确行为。

### 5.14 klog

- `logcat: bad level (debug/info/warn/error/fatal or 0-4)` /
  `logcat: unknown option <o>` — 参数防护。
- `logctl: bad level` / `usage: logctl enable|disable debug|info|warn|error|fatal` — 同上。
- `klog: flush FAILED (no filesystem mounted?)` / rotate / export
  变体 — 需要 /var/log；先挂载一个卷。
- `logctl: bad level ('-' = any)` — 过滤语法说明。

### 5.15 net — 核心

- `virtio TX timeout: avail_idx=<N> used_idx=<N> ISR=0x<hex> STATUS=0x<hex> desc_addr=0x<hex>` — 唯一真正的 TX 超时模板；
virtio NIC 停止消费描述符。QEMU：用
`-device virtio-net-pci` 重启；若持续则报告。
- `[tcp] send fail: sock=<n>` — TCP 发送错误；检查链路状态
  （`ifconfig`）。
- DNS 家族：`DNS resolution failed`、`DNS resolution failed (timeout)`、`timeout`、`MX: no mail exchanger (or timeout)` 及
  AAAA/CNAME/NS/SRV/TXT 变体 — 见 [network-issues.md](network-issues.md)。
- `DHCP failed (timeout)` — DISCOVER→OFFER→REQUEST→ACK 链
  无应答；见 [network-issues.md](network-issues.md)。
- `  UDP rx dropped (buffer full): <n>` / `  IP frag dropped: <n>` —
  套接字缓冲/重组丢弃计数器；重负载症状。
- `firewall reset (rules, conntrack, policies)` — `firewall reset`
  之后的信息性输出。
- `rule not found` — 用过期索引执行 `firewall del <n>`；
  `firewall` 重新列出。
- wget：`cannot resolve host`、`out of memory`、`socket failed`、
  `connect failed`、`TLS failed (code <n>)`、`no response (timeout)`、
  `wget: cannot create file, printing to console:` — 目标文件
  无法创建时正文打印到控制台。
- `tcptest: alloc failed` — OOM。

### 5.16 net — ssh 客户端

- 传输：`[ssh] net_socket failed`、`net_connect failed`、`failed to send version banner`、`failed to receive server banner`、
  `[ssh] server banner too long or unterminated` — 连通性/对端
  问题。
- kex：`failed to send KEXINIT` / `receive KEXINIT` / `send KEX_ECDH_INIT` / `receive KEX_ECDH_REPLY`、`[ssh] x25519 shared secret computation failed`、`[ssh] x25519 shared secret is all-zero (invalid server key)`、`[ssh] exchange hash input exceeds 4096B page` — 损坏或敌意的对端。
- 主机信任：`[ssh] TOFU: cannot write /etc/ssh_known_hosts (read-only fs?)`（ramfs /etc 上属预期）、`[ssh] host signature missing`、`[ssh] signature length mismatch`、`[ssh] HOST SIGNATURE INVALID - disconnecting`、`[ssh] WARNING: HOST KEY CHANGED! (possible MITM)`、`[ssh] connection refused (delete the entry in /etc/ssh_known_hosts to re-trust)` — TOFU 模型在说话；仅当你
  知道密钥为何变化时才删除该条目。
- 认证：`[ssh] USERAUTH_FAILURE — public key rejected`、`[ssh] USERAUTH_FAILURE — wrong password`、`[ssh] publickey unavailable (no /etc/ssh_client_key), retrying with password` — 凭据问题。
- 会话：`[ssh] failed to send CHANNEL_OPEN`、`CHANNEL_OPEN failed`、
  `[ssh] rekey failed - dropping session`、`[ssh] timeout reading channel data`。
- 完整性：`[ssh] MAC verify FAIL on incoming encrypted packet`、
  `[ssh] invalid packet_length in encrypted packet`、`[ssh] unexpected msg type <n>` — 篡改或版本偏差；捕获串口。

### 5.17 net — sshd 服务器

- `host key file corrupt; regenerating` — 自愈的首次启动路径。
- `ERROR: cannot obtain a per-installation host key - refusing to start sshd (embedded universal key removed)` — 无密钥且无法
  生成密钥（无熵/内存）。通过
  `tools/host_keygen_etc.py` 预置 `etc.img`（见 [network-issues.md](network-issues.md)）。
- `ERROR: cannot spawn the sshd-hkgen thread` — 线程表满；
  `ps`。
- `recv_unenc: timeout reading 4-byte length`、`recv_unenc: bad pkt_len=<n>`、`recv: bad packet_length=<n>`、`recv: bad blocking, remaining=<n>` — 敌意或损坏的客户端组帧。
- `MAC verify FAIL on incoming packet` — 篡改。
- `publickey signature INVALID`、`USERAUTH_FAILURE sent (bad credentials)`、`too many authentication failures` / `auth failure cap reached - disconnecting` — 认证路径。
- `second connection during active session - refusing it`、`too many connections (one session at a time)` — 按设计一次一会话。
- `listen socket alloc failed`、`tcp_listen failed` — 端口被占 /
  OOM。
- `curve25519 shared secret is all-zero (invalid client key)`、
  `exchange hash input too large` — 敌意 kex。

### 5.18 net — TLS（HTTPS）

- `ServerHello recv/parse failed`、`ServerHello legacy version mismatch`、`EncryptedExtensions failed`、`Certificate failed`、
  `CertificateVerify recv failed`、`server Finished verify_data mismatch` — 握手步骤失败；步骤名即诊断。
- `[tls] x509 verify failed: <why>` + ` - connection REFUSED` —
  信任决策（见 X509_E_*）。自签名链默认
  被拒绝。
- `record decrypt failed (AEAD open)`、`transcript overflow (certificate chain too large)`、`tls12 hs_log overflow (chain too large)`、`[tls-dbg] PSS verify failed: transcript_len=<n>` — 对端
  加密损坏/敌意。

### 5.19 net — 无线 + BT

- `WEP (not supported)`、`WPA2 (not supported)`、`WPA1 (not supported)` — 如实状态机：拒绝与这些
  安全方式关联。
- `associate failed (honest state machine result)`、`connection failed (honest HCI result)` — 硬件/驱动现实原样上报。
- 驱动模拟固定件（`ath9k non-atheros rejected`、`iwl ucode bad magic honest`、...） — 正确的拒绝。

### 5.20 ocembed（OCIRE 嵌入通道）

- `mailbox magic mismatch at lba=%u - no signal written` /
  `log ring magic mismatch at lba=%u - append refused` — 目标
  块不是有效邮箱（OCIREMB2/OCIRELOG1）。
- `L1SVC ocire op=%u refused: caller is not ocire_guard` +
  `ocire: refused (guard-only channel)` — 只有 OCIRE 日志代理可以
  调用 cat-18 op 4/5；对其他任何人属预期。
- `no block device covers the mailbox lba=%u` / `SIGNAL_OK write failed` — 无恢复卷；嵌入不活跃（普通安装上的常态）。
- `ocire embed: boot signal FAILED - see log` / `ocire: append FAILED (ring io)` — 嵌入通道断裂；OCIRE 监督降级为
  无监督。
- `ocire embed: whole-disk write protection ARMED (<range> bytes, protected)` — 信息性：对恢复磁盘的写入已关闭。
- `i6300esb disarm: device absent` — OCIRE/OCSE 引导：无 TCO 可
  解除；无害。
- `ocire embed: agent spawn FAILED - the recovery log channel stays unused` — 进程表满或镜像缺失。

### 5.21 power

- `System halted.` — 正常 `halt`。
- `S3 resume failed.` — 此机器的挂起/恢复路径损坏。
- `Power off not supported. System halted.` — 所有端口穷尽
  （OC_POWER_E_UNSUPPORTED）；使用宿主机电源。
- `Suspend not supported (ACPI required).` — 此机器无 ACPI。
- `sleep: invalid time` — `sleep` 需要正数（允许
  小数）。
- `poweroff_test: no power-off port took effect on this machine - power-off NOT observed : FAIL` — 探针在 VM 中的如实判定。

### 5.22 selfhost（update + config + autoupdate）

- `update: download failed: <why>` / `download size mismatch` /
  `update: cannot determine target slot (not booted from A/B?)` /
  `update: install failed: <why>` / `update: setting boot failed` /
  `update: check failed: <why>` / `rollback: failed` — `<why>` 是
  OC_UPDATE_E_* 文本；见 [error-codes.md](error-codes.md) 和
  [silent-failures.md](silent-failures.md) §OTA-chain。
- `abupdate: boot slot B (bootfail marker written)` — 刻意的
  故障切换标记；下次引导回滚到 A。
- config：`config: cannot read /etc/opencube.conf`、`: config file missing`、`: key not found`、`: read error`、`config: write rejected (empty, non-ASCII, too long, or invalid value)`、`config: restore failed` — 值防护按设计从严；`config list` 查看
  有效键。
- autoupdate：`[autoupdate] skipped: network not ready`、
  `[autoupdate] check failed (code <n>)` — 按设计静默；见
  [silent-failures.md](silent-failures.md)。

### 5.23 sysapi

- klog WARN `kill pid=%d refused: ocire_guard process` — OCIRE 日志
  代理按设计不可杀；ring 3 只看到"no such process"
  （开放边界）。

### 5.24 time

- `rtc pie: invalid rate/count (rate 3..15, count 1..100000)`、
  `alarm test: internal error`、`counter test: FAIL (non-monotonic)`、
  `tsc cal: FAIL (drift > 10%)`、`tsc delay: FAILED (no calibration available)` — 校准失败；运行 `tsc`/`hpet`/`pmtimer`
  状态。
- 如实家族：`hpet: legacy route not available (honest skip)`、
  `oneshot: arm failed (honest FAIL)`、`calibration: FAILED (no samples)` — 硬件缺失被如实上报，不是 bug。

### 5.25 tui

- 桌面：`page error`、`page '%s' failed (rc=%d)`、`tui: registry alloc failed`、`tui: desktop init failed`、`tui: desktop window failed`、`out of memory`、`command failed` — 桌面内部；
  附 `dmesg` 报告。
- 编辑器：`open %s failed (rc=%d)`、`%s: partial load, save refused`、
  `partial buffer: saving refused`、` [PARTIAL: save refused]` —
  256 行 × 128 字节限制；该拒绝保护你的文件。
- 文件：`stat %s failed`、`open %s failed`、`unknown fs type: %s (ramfs/fat32/exfat/ext4)`。
- Sysinfo/update：`check failed`、`manifest has no package fields (download refused)`、`download failed (rc=%d)`、`verify failed (rc=%d) - package discarded`、`install failed (rc=%d)`、`installed but set-boot failed (rc=%d)`、`rollback failed` — OC_UPDATE
  代码的 TUI 镜像。
- 时间页：`value out of range (1980-2099, valid M/D/H/M/S)`、
  `RTC write failed (SET-window readback mismatch)`。
- 安全字符串 `WEP/WPA1/WPA2 (not supported)` — 如实。
- 扩展自测：`menu: FAIL (`、`page: FAIL`、`widget: FAIL`、
  `cmd: FAIL`、`theme: FAIL`、`key: FAIL`、`event: FAIL`、`selftest FAILED (see log)` — 运行 `tui-test`，然后 `dmesg`。

### 5.26 usb

- `blk_read_sectors failed`、`too few`、`data mismatch`、
  `short write / error` — USB MSC 读/写测试失败；重新
  插拔 U 盘后再试 `usb_storage_test`。

### 5.27 watchdog

- `watchdog: task create FAILED` — `test` 注入器无法
  孵化；进程表满。
- `watchdog hw: start FAILED (bad timeout?)` / `stop FAILED` / `feed FAILED (not armed?)` / `set FAILED` — hw API 防护；先检查
  `watchdog hw status`。
- `watchdog: register FAILED (table full?)` — 最多 8 槽；`watchdog slots` 检查。
- `watchdog: bad slot id` — 对已死 id 执行 `feed <id>`。
- usage 行回显确切范围（`hw timeout <2..63 seconds>`、
  `timeout <id> <ms 100..3600000>`）。

## 6. OCOS 驱动（6 类）

### 6.1 音频驱动

- `es1370: block IRQ timeout` / `es1370: IRQ registration failed - device disabled` — 阻塞等待错过 IRQ / IRQ 线丢失。
- `sb16: block IRQ timeout` / `sb16: IRQ 5 registration failed - device disabled` — IRQ 5 上的同一模板。
- `hda: verb timeout verb=0x<hex>` / `hda: controller reset failed` /
  `hda: codec enumeration failed` — HDA verb 通道断裂；QEMU
  `-device intel-hda` 或真实硬件 BIOS 设置。
- `virtio-snd: control timeout`、`required PCI capabilities not found`、
  `not enough virtqueues`、`queue setup failed` — virtio-snd 装配；
  检查 `-device virtio-sound-pci`。

### 6.2 bus（ACPI / AML / SMBus）

- `ACPI: table with bad length skipped` / `ACPI: table checksum mismatch skipped` / `ACPI: no valid RSDP found` / `ACPI: RSDT/XSDT invalid` — 固件表；系统降级（无 ACPI 电源
  路径）。
- AML 解释器防护（敌意 DSDT 保护）：`AML: unsupported op 0x<hex>`、`node pool allocation failed`、`op budget exceeded`、
  `while-loop guard tripped`、`table length out of range` —
  解释器拒绝在坏 DSDT 上挂死。固件问题。
- i801 SMBus：`NACK/device error`、`bus error/collision`、
  `transaction failed`、`timeout`、`10-bit not supported by i801-class SMBus`、`bad address`、`unsupported combined transfer` — 状态
  解码；多为设备侧。

### 6.3 misc（BT HCI / MJPEG）

- HCI 状态名 `unknown-cmd`、`unknown-conn`、`hw-fail`、
  `timeout` — 控制器应答。
- MJPEG 解码器拒绝（25 个防护：`missing SOI`、`bad segment length`、`progressive JPEG not supported (baseline only)`、
  `lossless/arithmetic JPEG not supported`、...） — 仅内核日志；
  UVC 层向上游报告采集失败。

### 6.4 网络驱动 — 合并的 `init failed` 模板

每个 NIC 驱动在其探针失败时打印 `<name>: init failed`：

`e1000e: init failed`、`igb: init failed`、`ixgbe: init failed`、
`rtl8139: init failed`、`rtl8169: init failed`（rtl8168 界面）、
`bcm57xx: init failed`。

统一排查（见 [network-issues.md](network-issues.md)）：
`lspci`（卡在总线上吗？） → `pcidump <b:d.f>`（BAR 正常吗？） →
家族状态命令（`e1000e`、...） → 家族自测。
附加：`bcm57xx: DMA memory above 4GiB - refused` — NIC 无法
触及其 DMA 缓冲；该机器的内存布局问题。

### 6.5 存储驱动

- AHCI：`ahci: issue fail (PxIS.TFE) IS=0x<hex>`、`ahci: issue fail (PxTFD.ERR) TFD=0x<hex>`、`ahci: issue TIMEOUT CI=0x<hex>`、
  `<port>: ATAPI (not supported)`、`<port>: failed to stop engine`、
  `<port>: out of memory`、`<port>: IDENTIFY failed`、`ahci: BAR5 invalid`、`<dev>) - controller skipped`。
- ATA：`ata: PIO read DRQ timeout lba=<n>`；`atadma: xfer FAIL rc=<n>`；`ata_dma: init re-entered with live controllers - refusing, ...`（双重初始化防护）。
- NVMe：`nvme: BAR0 invalid`、`nvme: disable timeout, rewriting CC=0 and retrying`、`nvme: ready timeout, resetting and retrying`、
  `nvme: out of memory (admin queue|io queue|bounce)`、
  `nvme: Identify Controller failed`、`nvme: Identify Namespace failed`、`nvme: unsupported formatted LBA size (only 512B supported; got <n>)`、`nvme: Create I/O CQ/SQ failed`。
- virtio-blk：`virtio-blk: no legacy PCI device found (modern-only devices not supported)`、`virtio-blk: BAR0 invalid`、`invalid queue size`、`out of memory`、`WARNING: capacity read failed (legacy config not available?)`、`used-ring poll timeout`、`virtio-blk: write rejected (device is read-only)` — 最后一条是 QEMU
  `-drive file=...,readonly=on` 的语义，不是 bug。

### 6.6 USB 主机 + 类驱动

- 枚举：`usb: GET_DESCRIPTOR(8) failed`、`SET_ADDRESS failed`、
  `GET_DESCRIPTOR(18/config9/config) failed`、`SET_CONFIGURATION failed`、`hub GET_DESCRIPTOR failed` — 设备未枚举；
  重新插拔，检查 hub。
- DMA 可达性（>4 GiB 机器上按设计）：`usb: UHCI probe failed - driver state above 4GiB, 32-bit DMA`、`UHCI probe failed - no 32-bit-reachable frame-list page`（及 schedule/ISO/bounce
  变体）、`usb: EHCI probe failed - driver state above 4GiB, 32-bit DMA`、`EHCI probe failed - DMA frame at/above 4GiB`、`usb: OHCI probe failed - pool incomplete or above 4GiB (32-bit DMA HC)`。
- xHCI：`xhci: ENABLE_SLOT failed`、`ADDRESS_DEVICE(BSR) failed`、
  `xhci: port reset fail ccs=0 w=0x<hex>`、`controller reset timeout`、
  `scratchpad page alloc failed`、`probe failed, allocated frames released`。
- EHCI：`ehci: port reset fail ccs=0 w=0x<hex>`。
- OHCI：`ohci: reset timeout`。
- MSC：`msc: CBW bulk out failed`、`msc: data phase failed`、`msc: CSW bulk in failed rc=<n>`、`msc: CSW bad signature or tag`、
  `usb-msc: device not ready`、`usb-msc: inquiry failed`、`usb-msc: read capacity failed`、` NOT registered (blk layer rejected block size <n>)`。
- 音频类：`usbaudio: SET_INTERFACE failed`、`device rejected SET_CUR(SAM_FREQ)`、`SET_CUR(MUTE) rejected`、`SET_CUR(VOLUME) rejected`。
- 串口：`usb-serial: configure failed`。

## 7. l1apps（5 类）

### 7.1 共享 i18n 模板（250 个应用）

- `<app>: service call failed (rc=-1)`（i18n 键 `svc_call_failed`） —
  L1SVC 桥返回 -1。**统一排查**：(1) 重新运行 —
  瞬态？(2) `health` + `dmesg 100`（服务的 klog 行讲述
  真相）；(3) 运行该家族的 `_test` 桩；(4) 若服务
  本身报告健康，则是 op 参数问题 — 在
  [../manual/function-dictionary.md](../manual/function-dictionary.md) 中查该命令的语法。
- `service call failed`（向导变体，`wiz_svc_failed`） — mega
  向导的副本；同一排查。
- `[save failed]` / `[save failed - stay in editor]` — 编辑器保存
  失败；通常是只读文件系统。

### 7.2 经典逐应用错误

- `cat: cannot open `（同模板：head/tail/sort/uniq/wc/awk） —
  对路径执行 `ls`。
- `cp: cannot open '<src|dst>'` / `mv: ...` — 同上。
- `rm: cannot remove '<path>'` / `rmdir: cannot remove '<path>'` —
  文件忙或非空。
- `mkdir: cannot create '<path>'` — 已存在或无父目录。
- `ln: cannot create symlink (target must be an absolute path)` /
  `ln: hard link failed (rc=<n>)` — 符号链接规则是仅限绝对路径。
- `chmod: cannot change mode` / `chown: cannot change owner` /
  `stat: cannot stat '<path>'`。
- `write: cannot open ` / `write: write failed on `。
- `awk: missing {print ...}` — 微型 awk 只支持
  `{print ...}`。
- `sed: unknown script` — 只有 `s///`、`-n Np`、`Nd`。
- `date: ms since boot (no RTC driver)` — 回退提示（VM）。
- `free: SYS_MEMINFO unavailable (kernel too old)` /
  `ps: SYS_PS unavailable (kernel too old)` — 版本偏差。
- `run: fork failed` / `run: unknown program: <name>`。
- `help: unknown option '<o>'` / `(registry listing failed)`。
- ush：`ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart`、`ush: syntax error near '>'` / `near '<'`、
  `ush: alias expansion too long`、`ush: fork failed`、`ush: command not found: <cmd>`。

### 7.3 Mega 应用分发错误

- `disk-tool: unknown op: <op>`（及 files/net-tool/sound-tool/
  sysdiag/hwinfo/power 变体）、`files: unknown program: <p>` —
  逐字表未命中；`help -w` 列出现存内容。
- update 向导状态（i18n 键 `net_probe_failed`、`ab_probe_failed`、
  `ab_missing`、`ab_mismatch`、`ab_warn`、`dns_fail`、`check_unknown`、
  `install_skipped`、`install_failed`、`reboot_skipped`） — 各自映射
  一个向导步骤结局；打印的文本指名失败步骤。
- `WARNING: this destroys all data on that device.`（disk-tool
  `dt_warn`） — 重输设备名防护路径。

### 7.4 ld_so（ring-3 动态加载器）

- `': invalid ELF (magic/ELF64/ET_DYN/phdr bounds)`、`ld.so: bad main base 0x<hex>`、`ld.so: bad libfoo.so base 0x<hex>` — 加载失败。
- `ld.so: symbol reloc with bad index in '<so>'`、`ld.so: COPY reloc with bad index in '<so>'`、`ld.so: unknown reloc type <n>`。
- `ld.so: mprotect W^X failed for '<seg>'` — W^X 强制失败。
- `ld.so: bad DT_NEEDED name in '<so>'`、`ld.so: cannot load '<lib>'`、
  `ld.so: libfoo.so missing DT_SYMTAB or DT_STRTAB`、`ld.so: failed to relocate libfoo.so` / `main program` — 动态链损坏；重新运行
  `l1test`、`so_test`、`dlsym_test`。

### 7.5 测试应用

- p2test：`UTF8-FAIL short write`、`READ0-FAIL`、`PIPE-FAIL round=<n>`（+ `pipe()=-1 (slot table exhausted?)`） — live-fix 探针；
FAIL 即其所守护修复的真实回归。
- help 门（`help_a_test` 等）：FAIL = 注册表列表形态
  变化 — 构建或 i18n 损坏。
- g6_oom / stackovf / pf_test / sigsegv_test / int3_user / badapp —
  刻意崩溃者；它们的"失败"输出是内核的
  遏制通告（§kernel）。

---

## 8. 已知 bug — bug-list.md，1 个 P0 + 35 个 P1（3.0.0 导出）

### P0

- **BUG-AUD-2a-001**（内核/系统调用安全） — ring 3 可通过
  标准 read/write 系统调用触及
  内核内存（细节因安全策略从略）。修复前：
  将每个 ring-3 程序视为潜在特权；不要运行不可信二进制。

### P1（症状 → 原因 → 应对）

1. **BUG-3** OCIRE — 一次 `core_sched_task_states` 调用后
   中断被永久关闭（flags 恢复习语）。症状：
   恢复内核在一次调度器调用后死寂。树中已修复；
   旧构建复现则报告。
2. **BUG-4** 内核栈守卫页解除映射在 2 MiB 大页上是空操作
   （+ 过期全局 TLB）。症状：klog WARN
   `kthread %s: guard page unmap FAILED` 且栈溢出防护
   变弱。已知；在跟踪。
3. **BUG-5** ush kill 竞态 — 受害者地址空间在 EXITED 标记前被
   销毁；定时器节拍可能把受害者调度到已释放的
   CR3 上。症状：`kill` 之后偶发
   崩溃。已修复；`wp01_killrace_test.py` 守护它。
4. **BUG-6** exec 失败/OOM 路径泄漏整个地址空间
   （`user_process_create`）。症状：失败的
   exec 使内存缓慢消失。`mem`；重启清除。
5. **BUG-7** `sys_execve` 在新地址空间建成前销毁旧
   地址空间；exec 无法优雅失败。症状：exec 失败硬杀
   进程。已知。
6. **BUG-20** ACPI AML OpRegion 访问无边界检查 —
   恶意 DSDT 可获任意物理读写。不要引导不可信
   固件表。
7. **BUG-21** TLS 证书链校验失败仅警告并
   继续。**现在默认拒绝**（HTTPS E2E 第 0 项）；
   显式选择加入是 `config set tls.insecure 1`。
8. **BUG-22** DNS 事务 ID 硬编码 0x1234，应答
   未校验 — 离线投毒可能。症状：敌意 LAN 上
   离奇的解析结果。已知。
9. **BUG-24** IRQ 关闭定时器上下文中的 TCP RTO 重传可能在
   ARP 中忙等；过期 ARP 项令机器死锁。
   症状：丢包下的硬冻结。前后对比 `tcpstats`。
10. **BUG-31** ush 命令未找到路径无界拼接进
    `path[64]`（栈粉碎）。已修复；仅在
    当前构建上输入垃圾命令。
11. **BUG-32** Tab 补全向 `line[512]` 无界追加。
    已修复（有界追加）。
12. **BUG-33** 别名展开向 `newcmd[512]` 无界 strcat。
    已修复。
13. **BUG-34** 编辑器：打开超限文件后保存 = 静默
    毁灭。**现在保存拒绝**（`[PARTIAL: save refused]`）。
    绝不强行保存部分缓冲。
14. **BUG-35** `write` 应用以 RDONLY 打开后写入；失败
    被忽略（静默空操作）。症状：`write <file>` 无
    输出。3.0.0 已修复。
15. **BUG-36** `cp x x` 截断、`mv x x` 删除（无同文件
    防护）。**仍然在世**：绝不把文件 cp/mv 到自身。
16. **BUG-46** OCBL ELF 加载器边界检查可经整数
    回绕绕过。已修复（防溢出数学）。
17. **BUG-47** OCIRE 监督器邮箱写失败形成
    无限重启循环。已修复（即你看到的
    `skipping the reboot to avoid a reset loop` 防复位循环防护）。
18. **BUG-63** 发布链：引导门使用可过期的 /tmp 文件 +
    `|| true` 吞掉 QEMU 失败。CI 级假绿。
19. **BUG-64** 深回归门断言结构上恒真。
    CI 级。
20. **BUG-65** E2E 脚本未传播失败退出码。
    CI 级。
21. **BUG-66** 屏幕内容从未机器断言。CI 级。
22. **BUG-90** 文档版本/状态声明在 README 等处过期。
23. **BUG-91** 网站卡在旧构建（site-data.js）。
24. **BUG-92** OCIRE 横幅宣称错误版本。已修复
    （`ocire_version_gen.h`）。
25. **BUG-99** 网站 ↔ gh-pages 不同步。
26. **BUG-102** README 记录了错误的构建命令。
27. **BUG-104** README 发布说明 SHA256 声明未实现。
28. **BUG-107** 已发布 ISO 本地副本大小/SHA 不匹配。
29. **BUG-109** 整机静默冻结竞态 — 快速连打 'a'/'u'
    或运行 `loop` 可令系统冻结且无 panic、无
    watchdog 接管。**最恶劣的在世 P1**：若复现，
    捕获 `-serial file:` 并报告；规避方法就是不要
    与控制台输入赛跑。
30. **BUG-110** L1 应用 stdout 中文在屏幕上渲染为豆腐块
    （串口正常） — 控制台回退缺少 UTF-8 修复
    （BUG-130 是根因）。
31. **BUG-111** nano/vi 退出不清屏（残影
    与提示符重叠）。外观问题；退出后 `clear`。
32. **BUG-112** 忙等 `loop`：35 秒以上无抢占、无健康
    告警、无 watchdog 击杀 — 宣告的遏制链在实机
    失效。症状：`loop` 运行期间机器形似死机。从
    另一路径 `kill` 或等它跑完；在跟踪。
33. **BUG-113** OCSE GUI 鼠标点击全部无效（悬停渲染，
    点击不触发）。使用 CLI（Ctrl+Alt+F2）或 `click <x> <y>` 命令。
34. **BUG-129** OCSE 安装器↔内核配置方言分裂（向导
    写入死键 `lang/hostname/boot.slot`；内核读取
    `shell.lang/sys.hostname`）。症状：首次启动时安装器设置
    被忽略；内核重写配置。从
    运行中的系统再次设置键（`config set shell.lang zh`）。
35. **BUG-130** ush `sys_write2` 控制台回退缺少 UTF-8
    修复 — 屏幕上所有 ush 输出变豆腐块（BUG-110 的
    根因）。随 WP06g1 修复
    一并解决。

（62 个 P2 + 41 个 P3 计入 bug-list.md 但不在此详述。）

## 9. KNOWN_ISSUES 事后剖析（19 个已修复）+ 3 个开放边界

### 2.0.0 后检 — 全部在 2.1.0 修复（17 个）

P-01 代码树在 CI 上不能编译（imm8 端口约束 ≥ 0x100；
GRUB efi_uga 模块；C23 `bool` 冲突） → 经 `%dx` 的端口
I/O、模块过滤、C23 感知类型。P-02/P-03 发布说明算术 +
231/233 命令映射行（正则漏掉 2 行） → 台账重新生成。
P-04/P-05 接口真值表 + 删除登记。P-06/P-07 文档
重写至 v2 现实。P-08 逐应用 L1 测试矩阵。P-09
Makefile 注释。P-10..P-16 网站
数据/资产/证据/计数卡尺/治理。P-17 误报文本缺陷如实
记录。

### 2.1.0 开放 — 全部在 2.2.0 修复（2 个）

O-01 wp10_tui 在一轮 checkupdate 后 #DF：最深的调用链
（SYS_L1SVC→ota→https→tls→x509→rsa→bn mont，约 23.6 KB）+ IRQ 嵌套
超出 20 KiB kthread 栈 → `OC_KTHREAD_STACK_PAGES 6→10`
（40 KiB 窗口）；损坏的 #DF 横幅（"andled"）也已修复。如果
你见到乱码 panic 横幅，怀疑栈压力。
O-02 wait4 与 fork 的抖动竞态 — 实为确定性（死亡父进程的
tid 被复用，过期僵尸退出码被提前收割） → 孤儿脱离 +
fork 尸体路径脱离；验证 20/20 干净启动。

### 3.0.0-WP03 时的开放边界（3 个，属设计）

1. ush 对 ocire_guard 进程的 `kill` 报告泛化的"no such
   process"；真实原因仅在 klog WARN（`kill refused: ocire_guard process`）中。当一次 kill"离奇"
   失败时使用 `dmesg`。
2. 裸 `sti;hlt` 停车仍存在于 4 条已证路径（sys_read 键盘
   等待、wp8cd fg 等待、sdhci 定时等待、OTA 稳定
   循环） — 若中断丢失则是硬冻结风险类；已记录，尚未
   重构。
3. 直接引导的 GRUB 条目不携带任何 oc.* 事实 — 定义上
   无监督（要完整状态机请经 OCBL 引导）。

## 相关页面

- [error-codes.md](error-codes.md) — 61 个数值代码、11 个
  panic 位点、日志格式
- [silent-failures.md](silent-failures.md) — 无消息的失败
- [boot-failures.md](boot-failures.md) /
  [network-issues.md](network-issues.md) /
  [storage-issues.md](storage-issues.md) — 分领域深潜
- [common-errors.md](common-errors.md) — 你最先伸手去翻的
  短清单
