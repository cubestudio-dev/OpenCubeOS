<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCIRE - OpenCubeOS 独立恢复内核

状态：**3.0.0-WP-01-ocire-core**（本工作包交付核心：OCIRE 能独立引导并运行最小 Shell）。

## OCIRE 是什么

OCIRE 是与 Open Cube OS（OCOS）内核完全独立的第二个 L0 内核：独立源码树
（`ocire/`），不共享任何 OCOS 头文件，使用自己的链接脚本
（`ocire/linker_ocire.ld`），产出自己的引导镜像（`build/ocire/ocire.iso`）。

- **L0 = 提供服务** —— OCOS 仍是全服务内核；OCIRE 是 L0 的对等体：当 OCOS
  无法启动时负责恢复的内核。
- **独立** —— OCIRE 必须能够在不依赖任何 OCOS 代码、数据或分区的情况
  下完成引导、运行与开关机。

## WP-01 交付范围

| 领域 | WP-01（本包） | WP-02 | WP-03 |
|---|---|---|---|
| 引导 + 最小 Shell | **本包** | — | — |
| help / shutdown / reboot | **本包** | — | — |
| 恢复能力、FAT12、自检、修复 | — | 本包 | — |
| 启动失败检测、1 分钟超时 | — | 本包 | — |
| OCOS<->OCIRE 双向接口、进程保护 | — | — | 本包 |

Shell 命令集固定为三条：`help`、`shutdown`、`reboot`，本包不多做。

## 源码结构（ocire/）

见 `docs/dev/ocire.md`（英文版，与本镜像内容一致）的源码布局、构建目标
与源文件说明。编译使用与 OCOS 内核相同的 `-Wall -Wextra -Werror`
零警告门，CI 在每次 push 上编译 OCIRE。

## 双系统磁盘布局与分区隔离

```
hd0（ocire-boot.img，tools/make_ocire_disk.sh 生成）
  GRUB 双菜单（BIOS：MBR + post-MBR gap；UEFI：FAT 分区上的
  /EFI/BOOT/BOOTX64.EFI）
  -> 菜单第 1 项（默认）引导 OCOS
  -> 菜单第 2 项引导 OCIRE

hd1（ocire-hd.img）—— OCIRE 独立盘
  MBR 仅一个条目，type 0xEE（"GPT 保护阴影条目"），其后没有 GPT 头；
  OCIRE 内核镜像以原始形式存放在分区起点（LBA 2048）。
  -> GRUB 菜单用整盘 blocklist 加载：multiboot2 (hd1)2048+128
  -> OCOS 侧：0xEE 使其分区解析器切换到 GPT 模式，没有 GPT 头则
     解析失败 -> 整表拒绝 -> OCIRE 分区在 OCOS 里没有任何设备节点
     与文件系统路径。
```

实测证据：`evidence/3_0_0_wp01_ocire/isolation.log`（在 OCOS 内运行：

- `partscan: 1 partition device(s) registered`（OCIRE 盘贡献 0 个子设备）
- `parted hda` → `no partition table found`
- `mkfs.fat32 hdap1 confirm` → `device not found`（无写入路径）
- `disk_rw_test hdap1` → `device lookup FAIL`（无读取路径）
- `df` → 只挂载 OCOS 自己的卷

诚实边界（同样实测，`isolation_rawdecl.log`）：整盘设备节点仍然存在，
`disk_rw_test hda` 与 `mkfs.fat32 hda confirm` 在一次性副本上均可执行，
与 Linux root 对 `/dev/sdb` 的语义相同。阻止管理员级整盘操作需要修改
OCOS 块层策略——本包被明确禁止修改 OCOS——该硬化归 WP-03（进程保护）。

## 测试

- `make ocire-test` —— 14 例（BIOS+UEFI：引导到 Shell、help、未知命令
  提示、退格编辑、shutdown 真关机、reboot 真重启）。
- `make ocire-dual-test` —— 11 例（四组双引导组合 + 五项隔离探测 +
  两项整盘边界申报）。
- OCOS 全回归矩阵在"含 OCIRE、零 OCOS 改动"的树上重跑：
  `evidence/3_0_0_wp01_ocire/regression/`（host tests、可复现构建、
  wp12、wp10_tui、chinese、tuiqa 双固件、native 双固件、l1_per_app、
  wait4 fresh-boot 电池 20/20、boot gates）全部与 2.2.0 发布基线一致。
