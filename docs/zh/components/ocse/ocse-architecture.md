<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE——OpenCubeOS 安装环境：架构与职责

OCSE 是 Open Cube OS 的**安装环境**：一个从安装介质启动、把一台
空白机器变成可启动的 Open Cube OS 系统的独立小内核。它拥有自己的
源码树（`ocse/`）、自己的链接脚本（`ocse/linker_ocse.ld`），与运行
系统（OCOS）和恢复环境（OCIRE）不共享任何代码。

## 为什么需要专门的安装内核

安装必须在一台**什么都没有**的机器上成功——没有操作系统、没有
驱动、没有 shell。因此 OCSE 自带了它需要的一切的最小实现：

| 模块 | 文件 | 职责 |
|---|---|---|
| 内核核心 | `kernel/`、`boot/` | 独立启动、GUI/输入循环调度 |
| 屏幕 | `screen_fb` | 帧缓冲控制台 + GUI 控件层 |
| 输入 | 键盘驱动 | 菜单按键、文本框、热键 |
| 磁盘访问 | `ocse_disk.c`、`ocse_atapi.c` | ATA/ATAPI 与 BIOS/UEFI 磁盘 I/O |
| 分区 | `ocse_part.c`、`ocse_detect.c` | MBR/GPT 解码、空闲间隙检测、既有系统检测 |
| 文件系统 | `fs/`、`ocse_mkfs.c`、`ocse_ext4w.c` | FAT32/exFAT/ext4 的创建与读写 |
| 安装载荷 | `ocse_iso.c`、`ocse_inst.c` | ISO9660 读取、安装状态机 |
| 完整性 | `ocse_sha256.c` | SHA-256 摘要（介质检查、口令散列） |
| 用户界面 | `ocse_gui.c`、`ocse_wizard.c`、`ocse_cl.c` | 面板、9 步向导、OCSECL shell |
| 本地化 | `ocse_i18n.c` | 英文 / 简体中文，实时切换 |

## 安全模型

OCSE 在设计上**不可能意外破坏已有的 Open Cube OS 安装**：

- 对已经能启动 `opencube.elf` 的目标**直接拒绝**（既有系统
  检查）——向导显示的是拒绝提示，而不是分区列表。
- 分区只使用目标盘上的**最大空闲间隙**；从不搬迁或删除他人
  分区来腾地方。
- 格式化只作用于**同一向导会话中 OCSE 自己创建的分区**。
- 所有破坏性 OCSECL 命令（`mkpart`、`rmpart`、`mkfs.*`、
  `install`、`ocbl-install`、`dd`）都要求末尾带字面 `confirm`。

## 内存纪律：.ocsework 区

全部可变安装状态都放在专门的 `.ocsework` NOLOAD 段——镜像的
其余部分是纯代码与常量。环境是独立实现（无 libc、无 printf）：
所有文本缓冲都经有界字符串助手构建，装不下的行会被明确拒绝，
绝不溢出（这是向导命令构建器缓冲溢出修复后确立的纪律）。

## 启动与识别

安装介质是携带一个字节的 `/ocse.marker` 文件的 ISO9660 卷——
WP-06 安装介质契约。OCBL 引导加载器识别该标记并把介质列为
启动菜单中的 OCSE 条目。启动后 OCSE 运行四项硬件检查
（`hwcheck`），随后进入图形面板；OCSECL shell 随时可用一个
热键唤出。

## 与其他组件的关系

| 组件 | 角色 | 参见 |
|---|---|---|
| OCBL | OCSE 所安装的引导加载器（`ocbl-install`） | [OCBL 总览](../ocbl/index.md) |
| OCOS | OCSE 安装到目标上的系统 | 主手册 |
| OCIRE | 恢复环境，安装在 A/B 恢复盘上 | 开发者手册 |

下一篇：[9 步安装向导与 OCSECL 命令字典](ocse-wizard.md)。
