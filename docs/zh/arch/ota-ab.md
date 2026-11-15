<!-- SPDX-License-Identifier: Apache-2.0 -->
# OTA 与 A/B：槽位、标志、更新链与回滚

自举（self-hosting）故事位于 `ocos/services/selfhost/`
（`svc_selfhost_{ab,update,cmds_config,cmds_test,l1}.c`）：内核可以
仅凭自己的驱动（net + TLS + fs + block + crypto）加上
`/etc/opencube.conf` 配置存储，检查、下载、校验、安装并回滚自己的
下一个版本。标志协议的引导加载器一侧（OCBL 的判定链与两份 GRUB
配置）见 [boot-flow.md](boot-flow.md)；磁盘布局工具见
[driver-model.md](driver-model.md)。

## 槽位布局（逐字引自 `svc_selfhost_ab.h`）

```
 *     +--------------------+
 *     | p1 boot/flags 32MB |  FAT32: /boot/next_B, /boot/ok_B,
 *     |                    |         /boot/bootfail_B, /boot/grub/grub.cfg
 *     +--------------------+
 *     | p2 slot A     128M |  FAT32: /boot/opencube.elf, /etc, /docs, ...
 *     | p3 slot B     128M |  FAT32: same structure as slot A
 *     | p4 /data     rest  |  FAT32: user data, downloaded packages
 *     +--------------------+
```

常量：`OC_AB_SLOT_ISO 0 / A 1 / B 2`；挂载 `/ab/boot`、`/ab/a`、
`/ab/b`、`/data`；标志 `/boot/next_B`、`/boot/ok_B`、
`/boot/bootfail_B`；包默认 `/data/update-pkg.tar.gz`（临时
`/tmp/update-pkg.tar.gz`）。`tools/make_ab_disk.sh` 构建该布局
（MBR、p1 64 MiB 标志区、p2/p3 128 MiB 槽位、p4 数据；基于
mtools，无 loop 设备，无需 root）。

## 标志协议

标志分区上的三个标记文件决定下一次引导：

| 存在的标志 | 引导 | 含义 |
|---|---|---|
| `next_B`，无 `bootfail_B` | 槽位 B | 已暂存一个更新 |
| `next_B` + `bootfail_B` | **槽位 A（" rollback"）** | B 内核启动了但从未确认——自动回滚 |
| `ok_B` | 槽位 B | B 已确认健康 |
| 无 | 槽位 A | 默认 |

悲观标记是方案的核心：**槽位 B 内核在引导时写入 `bootfail_B`，
仅在系统完全起来后才移除它**（kmain 中的
`ota_update_confirm_boot()`，它同时复位引导尝试计数器）。在引导
中途 panic 的 B 内核会把标记留在原处，下一次复位——无论 OCBL
判定、GRUB 菜单还是 `grub-ab.cfg`——都回落到槽位 A。当前槽位经
multiboot2 命令行 `oc.slot=A|B|ISO` 到达（OCBL 写入；内核在扫描
之前用 `ota_ab_set_boot_slot_arg` 读取）。

**引导尝试预算**（BUG-0305/A5-16）：GRUB env 块
`/boot/grub/grubenv`（1024 字节，`ab_attempts` 变量）配
`OC_AB_MAX_ATTEMPTS 3`；内核在 A 完成引导或 B 确认时复位计数器
——因此在抵达确认之前就复位的内核不能无限重试。

同一语义的三份实现：`grub.cfg`（ISO 介质）、`grub-ab.cfg`（已
安装磁盘，每次更新安装作为 `boot/grub.cfg` 包文件刷新）、OCBL
原生的 `judge_volume()`。

## 更新流程（`oc> update`）

1. **check**：从配置的更新 URL 抓取 `update.json`——版本、
   package_url、package_sha256、package_size（`checkupdate` 命令只
   做这一步；`/etc/opencube.conf` 中的 `auto_check` 使 kmain 在
   引导时异步运行它）。
2. **download**：HTTP 或 HTTPS，流式写入 `/data`。
3. **verify**：对照清单做 SHA-256。
4. **install**：gunzip（RFC 1952/1951——`core_lib_gzip.c`）+
   untar（ustar——`core_lib_tar.c`），**装入槽位 B**（非活动槽位；
   系统全程继续从 A 运行）。
5. **set boot**：写标志，打印 "reboot required"。

`rollback` 清除标志。包本身（`tools/make_update_pkg.sh`，
`opencube-<version>-update.tar.gz`）在构建日内确定性生成；ELF 完全
可复现（`make reproducible-check` 对两次干净构建做字节比对）。

## 磁盘安装路径——8 步状态机

裸机安装经 OCSE 的 `ocse_inst_install(disk, part, confirm, work, sz)`（`ocse/ocse_inst.c`）——八步，每一步都是真实的失败点：

1. **介质**——校验安装介质（OCOS.TAR + `ocblmbr.bin` 自检）。
2. **目标设备**——所选磁盘必须存在且可用。
3. **目标文件系统**——**仅限 FAT32**：OCBL 读 FAT12/16/32 +
   ISO9660，因此可引导的安装目标必须是 FAT32；被诚实地强制
   （`mkfs.fat32` 以字面 `confirm` 令牌作为 BUG-0239 数据丢失
   门禁）。
4. **挂载**目标。
5. **确认门禁**——键入的确认。
6. **解包**——`tar -xf /install/ocos.tar`。
7. **引导代码**——`ocbl-install`：stage1 位于 LBA 0，保留磁盘
   当前分区表的第 446..509 字节；补丁块（魔数 'OCBL'、
   core_lba=1、扇区数）写在镜像偏移 0x1A0；core 落在分区前间隙
   LBA 1..N（mkpart 对齐于 LBA 2048，留出 2047 个扇区；core 约需
   62 个）；完整读回校验。错误码：-1 介质、-2 无分区表、-3 无
   空间、-4 写入、-5 校验。
8. **引导标志**——设置活动分区（MBR 0x80 / GPT legacy-boot
   属性）。

安装的内容（`tools/make_ocos_tar.py`——“安装 OCOS 意味着什么”的
唯一事实来源）：`boot/opencube.elf`（OCOS 内核，OCBL 扫描的名
字）、`boot/ocire.elf`（恢复内核，当存在未分配尾部空间时额外拷贝
到专用恢复分区）、`etc/opencube.conf`。系统内命令 `install`、
`abdisk` 与 `grub-install` 驱动同一批层（后两者经内嵌 GRUB 引导
数据，`tools/embed_grub.py`）。OCSE 的 9 步向导
（[boot-flow.md](boot-flow.md)）正是这一面上的控制器——“向导自身
不实现任何安装逻辑”。

## L1 面：16 个 op

```c
#define L1SVC_OTA_CHECKUPDATE      1  /* checkupdate           */
#define L1SVC_OTA_CONFIG           2  /* config list|get|set   */
#define L1SVC_OTA_CONFIG_TEST      3  /* config_test           */
#define L1SVC_OTA_CHECKUPDATE_TEST 4  /* checkupdate_test      */
#define L1SVC_OTA_UPDATE           5  /* update [...]          */
#define L1SVC_OTA_ROLLBACK         6  /* rollback              */
#define L1SVC_OTA_PKG_TEST         7  /* update_pkg_test       */
#define L1SVC_OTA_AB_TEST          8  /* ab_partition_test     */
#define L1SVC_OTA_CHECK_TEST       9  /* update_check_test     */
#define L1SVC_OTA_DOWNLOAD_TEST   10  /* update_download_test  */
#define L1SVC_OTA_VERIFY_TEST     11  /* update_verify_test    */
#define L1SVC_OTA_INSTALL_TEST    12  /* update_install_test   */
#define L1SVC_OTA_ROLLBACK_TEST   13  /* update_rollback_test  */
#define L1SVC_OTA_LOCAL_TEST      14  /* update_local_test     */
#define L1SVC_OTA_STATUS_TEST     15  /* update_status_test    */
#define L1SVC_OTA_REAL_TEST       16  /* real_update_test      */
```

`config [set <k> <v>]` 查看/校验/改写 `/etc/opencube.conf`
（`config set shell.lang` 同步实时控制台语言）。关键错误串：
`"update: cannot determine target slot (not booted from A/B?)"`、
`"update: download failed: "`、`"update: install failed: "`、
`"update: setting boot failed"`、`"rollback: failed"`、`"config: cannot read /etc/opencube.conf"`。

## 各部件如何读写槽位

读写路径就是普通块 I/O：A/B 槽位写入 = blk ops -> 64 槽位 LRU 写回
缓存 -> 分区子设备（边界强制）-> 父磁盘 ops（ata_dma/ahci/nvme/
virtio/scsi/usb-msc）。更新安装在从槽位 A 运行的同时写槽位 B；
`abdisk` 在创建布局后重跑 `ota_ab_rescan()`。在 OCIRE 监督的引导
中，这一切运行之前整块恢复盘已被写保护
（见 [hidden-mechanisms.md](hidden-mechanisms.md)）。

客户机内测试：`update_*_test` 链（每个阶段打包为一条命令——
`ab_partition_test`、`checkupdate_test`、`download`、`verify`、
`install`、`rollback`、`local`、`status`、`real_update_test`）外加
真正走完 check -> download -> verify -> install -> reboot 的发布
门禁 E2E。`update_test_vectors` 位于
`src/svc_selfhost_update_test_vectors.inc`。

面向用户的指南：
[../manual/update-ota.md](../manual/update-ota.md)、
[../l1/ota.md](../l1/ota.md)；内部机制孪生页：
[../internals/ota-ab.md](../internals/ota-ab.md)。
