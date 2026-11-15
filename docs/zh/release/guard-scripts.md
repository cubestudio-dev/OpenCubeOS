<!-- SPDX-License-Identifier: Apache-2.0 -->
# 架构守护脚本（维护者手册）

把结构不变量变成硬失败的检查脚本。下面每个守护都按
**输入 → 检查逻辑 → 失败输出 → 在哪里强制执行**记录。相关
页面：[build-reference](build-reference.md)（承载这些守护的
规则）、[ci-gates](ci-gates.md)（CI 侧门禁）、
[tool-inventory](tool-inventory.md)（完整 tools/ 清单）。

本仓库的守护哲学：曾经在现场失败过的不变量（各节都引用了
该事故）变成一个让构建/发布失败的脚本，因此同类 bug 永远
不会再无声发布。

## 1. tools/check_deps.py — 依赖方向门禁

**守护什么**：`ocos/` 树的分层。

| 层 | 不得依赖 |
|-------|-------------------|
| `ocos/kernel/` | `ocos/drivers/`、`ocos/services/`（机制层之上的任何东西） |
| `ocos/drivers/` | `ocos/services/` |
| `ocos/l1apps/` | `ocos/kernel/`、`ocos/drivers/`、`ocos/services/`——仅限 `ocos/interface/` 属主的头文件 |

**输入**：无（从仓库根遍历工作树）。

**检查逻辑**：
1. 通过遍历 `ocos/{kernel,drivers,services,interface}` 中的
   `.h`/`.c` 文件，构建"基名 → 所属层"映射。include 是纯基名，
   针对这份真实清单解析——与构建的多 `-I` 策略解析它们的
   方式完全一致。
2. 对受守护层下的每个 `.c`/`.h`，读取每行
   `#include "x.h"`（仅带引号的 include，无路径分隔符）并
   针对清单解析 `x.h`。
3. 跳过组合根：`kernel/core_boot_*.c` 按设计可以依赖每一层
   （它是链条顶端）；两个注册框架按设计互相包含对方属主的
   头文件。

**失败输出**（exit 1）：

```
check_deps: N DEPENDENCY-DIRECTION VIOLATIONS:
  kernel/foo.c:12: includes 'bar.h' -> drivers/baz/bar.h (forbidden direction)
```

成功时打印扫描文件数和
`0 violations (kernel -> nothing above it; drivers -> no services; l1apps -> interface only)`。

**强制执行位置**：手动 / 发布前卫生检查运行
（`python3 tools/check_deps.py`）。它没有接入 CI，也没有接入
`make`——在结构变更前和发布前运行它。注意 l1apps 约束已经
被构建的 `-I` 集合半强制（当应用伸出 `ocos/interface/` 之外时
编译失败）；该门禁对其做静态双重检查。

## 2. tools/check_bss.py — 引导区 .bss 门禁

**守护什么**：任何常规 `.bss` 变量都不得落入引导结构区。
`core_boot_entry.S` 把页表（PML4/PDPT/PD）和引导栈保留在
`.bss` 内部；链接器会欣然把其他对象放在它们下面。WP-06b b4
会话现场抓到了后果：一个 64 KiB 的 CLI 缓冲落在了活动的
PML4 上，第一次超过 448 字节的 ISO 读取破坏了页表，引发
三重故障。

**输入**：一个参数——已构建的 ELF（`check_bss.py <elf>`）。

**检查逻辑**：
1. 运行 `nm -S -n <elf>`；收集每个 `b`/`B` 符号的地址和
   大小（3 列行取大小 0）。
2. 定位布局标记 `__boot_pml4`（引导区起点）、
   `__ocse_work_start`、`__end_of_kernel`（WP-06c 为 GUI
   向导状态加入的 `.ocsework` NOLOAD 工作区）。
3. 任一标记缺失 → `FATAL: layout markers not found`，exit 1
   （链接错误的 ELF 本身就是失败）。
4. 符号与 `[__boot_pml4, __ocse_work_start)` 重叠且本身不是
   `__boot_*`/`__ocse_work_start`/`__end_of_kernel` 标记时
   即为违规。完全位于工作区内的符号是合法的（加载器经
   `p_memsz` 把它清零）。

**失败输出**（exit 1）：

```
[check_bss] FATAL: variables overlap the boot zone (0x...0..0x...1):
  cli_buffer @0x...0+0x10000
```

成功时打印最后一个常规变量与引导区之间的字节余量——
`.bss` 增长时要盯的数字。

**强制执行位置**：`tools/build_ocse_iso.sh` 在打包前对 ELF
运行它——每次 `make ocse-iso` 都被门禁覆盖。未来任何溢出
都是构建错误，而不是启动期的 heisenbug。

## 3. tools/v2_registry_gate.py — v2 命令面冻结

**守护什么**：v2.0.0 B 组接口冻结——当收集机制本身变化时，
按声明收集的命令注册表不得变形。

**输入**：一个参数——要启动的 ISO
（`python3 tools/v2_registry_gate.py <iso>`）。

**检查逻辑**（经 pexpect 启动 QEMU BIOS 512M，`OC_TOOLS`
环境变量指向迁移后的 QEMU，然后发送 `help -a`、`help -w`、
`help`）：
1. 启动到达 `oc>` 提示符（TIMEOUT → `FAIL: no shell prompt`）。
2. 注册表收集日志行
   `registry: file-local declarations collected` 必须存在。
3. `help -a` 在锚点 `Available commands (A-Z):` 和
   `Commands by work package:` 之间解析——恰好 231 行命令
   （233 个命令；health export/watch 计为子命令）。计数
   错误 → `FAIL: expected 231 lines, got N`。
4. `help -w` 页脚必须打印 `Total: 233 commands.`。
5. 服务组视图必须渲染（recovery、watchdog、`=== l1 (`）。

**失败输出**：每个未通过的检查一行 `FAIL:`；完整串口日志
转储到 `/tmp/vcheck/v2_gate_out.txt` 供分诊。任一失败即
非零退出。

**强制执行位置**：v2 时代的发布时门禁。该计数不变量在 3.x
的继任者是 31 项检查回归中的动态下限（见
[ci-gates](ci-gates.md) §5）：命令只能新增，不能移除——下限
从 276 起（g3 基线 275 + p0leak），当前树实测 284。

## 4. tools/gen_userprogs.py — 注册约束

**守护什么**：内嵌 ring-3 程序注册表
（`ocos/kernel/userprogs_data.h`，由 `core_userprog_lookup()`
消费）。内核中没有任何名字是手工注册的。

**输入**：无（从树中发现；需要 PATH 上有 `nasm` + `gcc`）。
由 `make userprogs`、`make all`（第一步）和
`$(USERPROGS_HDR)` 文件规则调用。

**检查逻辑 / 约束**：
1. **发现是机械化的**：目录应用是含 `main.c` 或 `main.asm`
   的 `ocos/l1apps/<name>/`（程序名 = 目录名；目录中每个
   `*.c` 链接在一起）；遗留平铺程序是
   `ocos/l1apps/*.{c,asm}`（名字 = 基名，带其 v2.x 构建模式）。
   `_shared/` 是纯头文件库，永远不是应用。`libfoo.c` 走
   `tools/build_solib.py`（`solib_data.h`，不是这个头文件）。
   `ld_so.c` 用 `ld_so.ld` @ 0x10000000 构建。
2. **第一个动作**：经 `gen_l1svc_ops.py` 再生
   `ocos/interface/l1svc_ops.h`，使封装应用永远按当前 L1SVC
   编号编译（只追加契约；那里失败则以
   `gen_l1svc_ops.py failed` 中止）。
3. **重名即致命**：目录 + 平铺程序全局 A-Z 排序后，
   `fail("duplicate program names (folder vs flat?): [...]")`——目录应用不得遮蔽平铺程序。
4. **严格 i18n 门禁**：每个应用必须带 `i18n/{en,zh}.json`
   资源（经 `i18n_load` 加载）；生成的每应用表随构建走。
5. **模式校验**：`verify_elf()` 按构建模式
   （exec/pie/pie-foo/ld_so）检查 `e_type`/`PT_INTERP`——
   链接错误的程序会中止生成。
6. **孤儿清除**：全量再生会丢弃源已不存在的 blob（删除
   应用即把它从注册表移除）。
7. **幂等产出**：头文件以字节比较守护方式写入，因此未变化
   的树不再生任何东西。

**失败输出**：`gen_userprogs: ERROR: <reason>` + exit 1
（重名、缺资源、编译/链接失败）。

**强制执行位置**：`make all`（先重建头文件——WP06g1
catch-2 缺口）、`make userprogs`，以及接入 `make dist` 的
过期门禁 `make userprogs-check`（任一源比头文件新超过
2 秒即以 "STALE embedded user programs (fix1 int3_user
incident class)" 消息让 dist 失败）。

## 5. make reproducible-check — 字节一致重建门禁

**守护什么**：内核 ELF 的干净构建确定性（G13 门禁；ISO 级
确定性栈见下）。

**输入**：当前树；运行两次完整子 make。

**检查逻辑**：`clean` → `kernel` → `sha256sum` → 把 ELF 复制
到 `/tmp/.oc_repro_1.elf` → 再次 `clean` → 再次 `kernel` →
`cmp -s`。目标注释核实的确定性来源：树中没有
`__DATE__`/`__TIME__`/`__TIMESTAMP__`，OCBL ISO 打包器固定每个
时间戳/GUID/卷序列号，gcc/ld 对相同输入+参数是确定性的，
链接顺序由通配展开固定，`strip --strip-debug` 移除带路径的
节，且调用方导出时尊重 `SOURCE_DATE_EPOCH`。

**失败输出**：`reproducible-check: FAIL - ELF differs:`、
两个 SHA256、`cmp` 前 3 个差异字节，以及 `exit 1`。

**强制执行位置**：`tools/build_release.sh` 第 2 步（FAIL 时
发布无法继续），以及发布 SOP 第 2 步（见
[process](process.md)）。

## 6. tools/fat_zero_dates.py — FAT 日期清洗器

**守护什么**：相隔几分钟的构建之间 FAT 载荷字节一致。
`mformat`/`mmd`/`mcopy` 把当前墙上时钟盖进目录项，会把构建
时间泄漏进 ISO。

**输入**：一个参数——mtools FAT12/16 镜像
（`fat_zero_dates.py <img>`；原地改写）。

**检查逻辑**：解析 BPB（每扇区字节数、每簇扇区数、FAT
起点/大小、根目录位置）；遍历每个目录项，仅把日期/时间字段
清零——创建十分之一秒（字节 13）、创建时间（14–15）、创建
日期（16–17）、最后访问日期（18–19）、写入时间（22–23）、
写入日期（24–25）——名字、属性、簇链和文件大小不动。FAT12
链逐项跟随以遍历子目录。

**失败输出**：设计上无（畸形镜像会抛异常）；消费该镜像的
门禁（ISO 打包器的确定性封印）会抓到下游漂移。

**强制执行位置**：安装/更新工具链在镜像被内嵌或打包之前
使用的 FAT 镜像构建路径。它在 ISO 侧的兄弟是
`tools/iso_zero_ctime.py`（把每个 ISO9660 目录记录中的
Rock Ridge TF ctime/atime 戳改写为 1970 纪元——xorriso 把
ctime 硬编码为组装时间且 `-alter_date` 不提供 ctime 旋钮；
目录 atime 泄漏在 WP04-fix1 修复）。与打包器固定的卷日期、
节点 mtime/atime、GPT 磁盘 GUID 和 ESP 卷序列号一起，它们
构成由 `reproducible-check` + 发布 SHA256 三方检查验证的
G13 确定性栈。

## 7. tools/fix_file_headers.py — 过期 `* File:` 注释改写器

**守护什么**：WP-10 project_restructure 之后的头注释真实性
——文件移动了（如 `kernel/snd.c` →
`drivers/snd/driver_snd.c`）但其 `* File:` 注释仍指向重构
前的路径。

**输入**：无（遍历仓库）。

**检查逻辑**：遍历每个 `.c`/`.h`/`.S`/`.asm`（跳过 `.git`、
`build`、`website`、`archive`、`iso`，以及
`tools/embed_grub.py`——它匹配到的行是它写出的模板头，
不是它自己的头）；在前 15 行内匹配
`^(\s*\*\s*File:\s*)(\S+)\s*$`；把路径改写为该文件在仓库中
的实际相对位置。仅注释——无语义影响。

**失败输出**：无（卫生工具）；打印已更改 / 已正确 / 无路径
文件的清单。

**强制执行位置**：重构后一次性运行；未来任何全树移动后
重跑它，使 `grep '* File:'` 保持可靠的索引。

## 8. 门禁汇总表

| 守护 | 输入 | 强制执行者 | 失败模式 |
|-------|-------|-------------|--------------|
| `check_deps.py` | 树 | 手动 / 发布前 | exit 1 + file:line 清单 |
| `check_bss.py` | ELF | `build_ocse_iso.sh`（每个 OCSE ISO） | exit 1 + 符号/地址清单 |
| `v2_registry_gate.py` | ISO | 发布时（v2 时代） | FAIL 行 + 非零退出 |
| `gen_userprogs.py` 约束 | 树 | `make all` / `make userprogs` / `make dist` | 带原因中止 |
| `userprogs-check`（make） | 树 | `make dist` | exit 1，STALE 消息 |
| `reproducible-check`（make） | 树 | `build_release.sh` 第 2 步 / SOP 第 2 步 | exit 1 + 差异字节 |
| `fat_zero_dates.py` | FAT 镜像 | 确定性栈 | 下游哈希不匹配 |
| `iso_zero_ctime.py` | ISO | 确定性栈 | 下游哈希不匹配 |
| `fix_file_headers.py` | 树 | 一次性卫生检查 | 仅报告 |
| `ocse-vendor-check`（make） | 树 | `make ocse-elf` | exit 1 + 重新 vendor 指引 |

vendor 侧完整性（不是脚本而是规则）：`ocse/` 只是
vendor+edits 的输出——`ocse-vendor-check` 在
`ocse/kernel/types.h` 缺失时失败，而 `tools/ocse_edits.py` 是
每个 vendored 源编辑的唯一事实来源（E1 OTA API 剥离、E2 GRUB
封装移出、E3 网络栈不 vendor、E4 崩溃管道打桩、E5 ring-3
kill 移除）。绝不手工编辑
`ocse/{kernel,drivers,fs,l1,boot,chinese}/*.c`——重新 vendor
会把它抹掉。
