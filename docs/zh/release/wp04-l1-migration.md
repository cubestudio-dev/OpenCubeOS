<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# 3.0.0-WP04：L1 迁移记录

本工作包完成 3.0.0 重构：v2.2.0 基线的每条命令现在都从
Ring 3 运行。L0 侧保持纯服务内核——驱动、服务和编号的
L1SVC ABI——用户面则是内嵌程序注册表：**一条命令 = 一个
应用 = 一个文件夹**（`ocos/l1apps/<name>/main.c`），由
`tools/gen_userprogs.py` 机械地发现进 `OC_USERPROGS[]` 注册表。

## 251 条 v2.2.0 命令去了哪里

| 批次 | 内容 | 数量 |
|-------|---------|-------|
| G3-G5 | 140 个 SVC 包装 + 31 个文件工具 + nano/vi 编辑器壳 | 171 |
| G6 | health / dmesg / logcat / logctl / watchdog | 5 |
| G7 | l1 家族：run、l1test、ldd、help（+ help_*_test 不变量） | 7 |
| G8 | vitals 5（mem/frag/heap/sched/syncstat）+ crash 3 | 8 |
| G9 | 18 个 Ring-0 自检（diag 操作 9-26） | 18 |
| G10 | 13 个芯片组诊断（diag 操作 27-39） | 13 |
| G11a | ssh + sshd（RFC 4251-4254 引擎对，逐字复活） | 2 |
| G11b | ime + ime-test（拼音引擎，DISP 操作 11/12 复活） | 2 |
| G11c | 九个全屏 TUI 页面（类别 "tui"，30 个文件约 13k 行） | 9 |
| 留在 ush | shell 状态内建（cd/export/alias/...）——子进程无法改写 shell 自身的进程状态 | 9 |
| 子命令 | `health export` / `health watch` 住在 health 应用内部 | 2 |

迁移总计：**251 中的 235**；其余各行是上面记载的
shell 状态/`subcommand` 类（外加一次审计误报和双宿主计数
口径）。

## 对等性证明

G13 验收矩阵在 QEMU 中把 34 条命令的测试组对着真实的
v2.2.0 构建（同一工具链）并排运行——结构逐行一致；只有
易变状态（tick 计数、空闲字节、堆块计数）和版本字符串
不同。逐批证据（逐字誊录、交互式 sendkey 会话、VNC
页面截图、VLM 读出的桌面菜单树、带 paramiko 的 SSH e2e）
存放在 `evidence/3_0_0_wp04_*` 之下。

## 新 ABI 面（只追加）

- 类别 **19 "diag"**——39 个操作（vitals、crash、自检、
  芯片组）
- 类别 **20 "tui"**——9 个操作（桌面 + 经典页面）
- net 操作 **13/14**（ssh/sshd）、display 操作 **11/12**
  （ime/ime-test，回到它们的 v2.2.0 编号）
- `tools/gen_l1svc_ops.py` 把编号镜像到 Ring-3 侧

## 值得保留的工程注记

- **恒等窗口纪律**：内核镜像必须保持在 0x400000 边界之下
  （P3-1 安全不变量）。GB2312 字体表（约 292 KiB）和 274
  个内嵌程序（515 KiB 的 ELF 载荷）都移进了逐对象的 gzip
  容器，惰性解压进内核堆缓存——镜像缩小的同时 TUI 回来
  了。
- **确定性构建**：相隔几分钟的两次干净构建逐字节一致
  （gzip `mtime=0`、xorriso 日期/GUID 钉死、分阶段
  树纪元归一化、FAT 日期擦洗，以及一道把 xorriso 硬编码为
  汇编时刻的 593 个 Rock Ridge TF ctime 戳归零的后处理）。
- **交互式操作绕过文本捕获**：全屏页面（tui）、会话服务器
  （sshd）和活动 IME 组词行在控制台/串口上实时渲染；只有
  纯文本操作经 4096 字节的 SVC 结果缓冲返回。

途中发现并修复的活 bug 记录在各批次的提交信息和证据
文件夹里（F6 IRQ 上下文开中断、F7 指向加载器内存的过期
RSDP 指针、F8 一个偏离其 v2.2.0 源的 health 格式）。
