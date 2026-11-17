<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE：9 步安装向导与 OCSECL 命令字典

安装器为**同一个**命令面提供两扇门：图形向导（默认）与 OCSECL
shell。向导**自身不实现任何安装逻辑**——它执行的每一个变更动作
都是一条真实的 OCSECL 命令行，经 `ocse_cl_exec` 运行，与 shell
执行的完全一致。命令失败时，错误页会显示捕获的输出，并提供
Retry/Back。

## 九个步骤

| # | 页面 | 内容 |
|---|------|------|
| 1 | 欢迎 + 语言 | 选择语言（英文 / 简体中文）；整个 UI 包括向导在内实时跟随切换 |
| 2 | 目标磁盘 | `lsdisk` 列出可安装目标；已能启动 `opencube.elf` 的磁盘被拒绝（既有系统检查） |
| 3 | 分区 | `lspart <disk>` 列出分区与空闲间隙；向导预选**最大空闲间隙**并构建 `mkpart <disk> <startMiB> <endMiB>`；高级选项可删除分区（`rmpart ... confirm`） |
| 4 | 格式化 | `mkfs.fat32 <device> confirm`——FAT32 是 OCBL 引导分区契约；页面说明格式化仅销毁该分区上的数据 |
| 5 | 用户账户 | 名字、全名与口令；口令在**离开页面前**即散列为 64 位十六进制 SHA-256——之后写入的记录是 `name:full:sha256`，绝不是明文口令 |
| 6 | 系统配置 | 主机名、语言、更新 URL、自动更新检查 |
| 7 | 确认 | 上述全部内容一屏汇总，此时尚未写入任何东西 |
| 8 | 安装 | 逐阶段执行真实命令序列（见下） |
| 9 | 完成 | 结束或重启进入已安装的系统 |

## 第 8 步：六个安装阶段

每个阶段都是一条真实命令行；进度条按阶段推进，首次失败即跳转
错误页，携带失败命令与其捕获输出。

| 阶段 | 构建的命令 | 说明 |
|---|---|---|
| 1. 挂载 | `mount <device> /mnt` | 此前失败重试残留的挂载先被丢弃（`umount /mnt`）再重试一次挂载 |
| 2. 安装 | `install <disk> <n> confirm` | 完整安装状态机：介质检查、恢复分区划分、载荷解包、引导代码、引导标志 |
| 3. 配置 | `config set hostname <h>`，随后 `config set lang <en\|zh>`、可选 `config set update_url <u>`、`config set auto_check <0\|1>` | 写入已安装配置的四个键 |
| 4. 用户记录 | `write /mnt/etc/users <name>:<full>:<sha256>` | SHA-256 口令散列；装不下的记录被拒绝，绝不截断 |
| 5. 同步 | `sync` | 刷块缓存 |
| 6. 卸载 | `umount /mnt` | 目标保持干净 |

## OCSECL 命令字典（40 条）

shell 随时可从面板（热键）与串行控制台进入。`help` 列出全部；
`help <cmd>` 显示单条命令。

| 命令 | 用途 |
|---|---|
| `help` | 显示全部命令或某条命令的帮助 |
| `lsdisk` | 列出可安装目标磁盘 |
| `lspart <disk>` | 列出某盘的分区 |
| `parted <disk>` | 显示原始分区表 |
| `mkpart <disk> <startMiB> <endMiB>` | 创建分区 |
| `rmpart <disk> <n> confirm` | 删除 #n 分区（需 `confirm`） |
| `setflag <disk> <n> boot [on\\|off]` | 设置分区标志 |
| `mkfs.fat32 <device> confirm` | 格式化为 FAT32（销毁数据；需 `confirm`） |
| `mkfs.exfat <device> confirm` | 格式化为 exFAT（销毁数据；需 `confirm`） |
| `mkfs.ext4 <device> confirm` | 格式化为 ext4（销毁数据；需 `confirm`） |
| `mount <device> <path>` | 挂载（文件系统自动识别） |
| `umount <path>` | 卸载 path 处的文件系统 |
| `ls [path]` | 列目录 |
| `cd [path]` | 切换目录（默认 `/`） |
| `pwd` | 显示当前目录 |
| `cat <file>` | 打印文本文件 |
| `write <file> [text ...]` | 写入一行 |
| `touch <file>` | 创建空文件 |
| `cp <src> <dst>` | 复制文件 |
| `mv <src> <dst>` | 移动或重命名文件 |
| `rm <file>` | 删除文件 |
| `mkdir <path>` | 创建目录 |
| `rmdir <path>` | 删除空目录 |
| `tar -t <arc>` / `tar -x <arc> [dir]` | ustar 列出 / 解包 |
| `dd if= of= [bs= count= skip= seek=] [confirm]` | 块复制 |
| `sha256sum <file>` / `<dev> <sectors>` | 文件或磁盘区段的 SHA-256 摘要 |
| `install <disk> <n> confirm` | 把 OCOS 安装到某分区 |
| `ocbl-install <disk> confirm` | 写入 OCBL 引导代码 |
| `config [set <k> <v>]` | 显示/编辑已安装配置 |
| `sync` | 把块缓存刷入磁盘 |
| `reboot` | 刷盘并重启 |
| `shutdown` | 刷盘并关机 |
| `hwcheck` | 重新运行四项硬件检查 |
| `disks` | 原始磁盘清单 |
| `lang [en\\|zh]` | 显示或设置语言 |
| `serial_encoding [utf8\\|gbk]` | 显示/设置串行线路编码 |
| `clear` | 清屏 |
| `click <x> <y>` | 探测 GUI 按钮几何（诊断用） |
| `gui` | 切回图形面板 |
| `halt` | 停机（不走 ACPI 路径） |

下一篇：[安装失败与恢复](ocse-recovery.md)。
