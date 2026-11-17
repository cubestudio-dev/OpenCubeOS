<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL——Open Cube OS 引导加载器：架构与职责

OCBL 是 Open Cube OS 的原生引导加载器。它在 Open Cube OS 的各类
介质上取代 GRUB：`ocbl-install` 写入的引导代码、恢复盘的 stage1、
ISO 的引导镜像都是 OCBL。它位于 `ocbl/`，从一份共享核心构建出
**两种固件形态**。

## 一份核心，两种固件形态

| 形态 | 入口 | 实模式管线 | 控制台 | 磁盘 | 视频 |
|---|---|---|---|---|---|
| BIOS | `bios/stage1.asm` | 16 位跳板（`tramp16.asm`），int13 DAP 经 1 MiB 以下弹跳窗读取 | VGA 文本 + 串行 | int13 | VBE（`ocbl_vbe.c`） |
| UEFI | `efi/efi_entry.c` | 无——从头就是原生 64 位 | GOP 控制台 + 串行 | EFI BlockIo（**驱动器编号从零开始**） | GOP（`ocbl_gop_efi.c`） |

两种形态编译同一份 `common/` 核心：条目扫描器（`ocbl_scan.c`）、
菜单（`ocbl_menu.c`）、文件系统（`ocbl_fs.c`）、分区解码
（`ocbl_part.c`）、ELF 加载器（`ocbl_load.c`）、启动状态监视器
（`ocbl_statemon.c`）与看门狗驱动（`ocbl_wdt.c`）。

## 移交契约

OCBL 加载内核并按与 GRUB 完全一致的方式移交——32 位保护模式、
分页关闭、`EAX = 0x36D76289`（multiboot2 魔数）、`EBX` = MBI
物理地址、跳转到内核的 32 位入口。BIOS 形态加载的固定物理布局：

| 地址 | 内容 |
|---|---|
| `0x30000`-`0x3FFFF` | 64 KiB 读取弹跳窗（int13 DAP 必须落在 1 MiB 以下） |
| `0x50000` | multiboot2 信息（64 KiB） |
| `0x400000+` | 内核 PT_LOAD 段（内核链接基址） |
| `0x800000+` | mb2 "self" 模块的内核 ELF 副本 |
| `0x1000000+` | OCIRE 载荷（菜单条目 OCBL_ENT_OCIRE） |

UEFI 形态保持同样的 MBI 契约，但以退出启动服务代替降级回实模式。

## OCBL 扫描什么

扫描器遍历每个 BIOS 驱动器与启动光盘，解码分区表、挂载它认识的
文件系统，产出菜单渲染用的条目表。七类条目：

| 类 | 识别依据 |
|---|---|
| OCOS | 任何携带 multiboot2 魔数头部的 `/boot/opencube.elf` 卷；A/B 标志协议选择槽位 |
| OCIRE | 携带 `/boot/ocire.elf` 的 0xEE 影子卷（或任意卷）；WP-07 恢复盘上是两个独立槽分区 |
| OCSE | 携带 `/ocse.marker` 的 ISO9660 卷（安装介质契约） |
| WINDOWS | 携带 `/EFI/Microsoft/Boot/bootmgfw.efi` 的卷 |
| LINUX | 带外来 EFI 加载器的 ESP（`/EFI/<vendor>/` 下的 `grubx64.efi` / `shimx64.efi` / `systemd-bootx64.efi`）——vendor 目录名即条目名 |
| GENERIC | 带不匹配上述任何一项的 `/EFI/BOOT/BOOTX64.EFI` 的 ESP |
| SHUTDOWN | 合成条目，永远排最后 |

已声明的限制：3.0.0 的 OCBL 不带 ext2/3/4 驱动，Linux 系统经其
ESP 加载器识别、绝不读取 ext 根上的 `/boot`；Windows 同理（不解析
NTFS——识别基于 ESP，引导基于链式加载）。

## 加载之外的职责

OCBL 还是可靠性链的**第一响应者**：

- 每次受监视载荷移交前布防引导阶段看门狗预算（i6300ESB 接力）；
- 复位后从状态区诊断上一次启动；
- 记录启动失败、累计连续失败数，达到阈值时把机器路由进 OCIRE。

这些机制详见[双固件引导与 A/B 槽判定](ocbl-ab.md)与
[故障排查与状态码](ocbl-troubleshooting.md)。
