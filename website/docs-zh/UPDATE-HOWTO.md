<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/UPDATE-HOWTO.md (website-provided, for
     reading convenience). The English original in the repository is
     authoritative. Commands, outputs, paths and identifiers are kept
     verbatim. Structurally re-aligned with the current English original
     at WP-10-AUDIT_P2-fix3 (fix3 G5, BUG-0270). -->

# Open Cube OS - 系统内更新（OTA）指南

WP-10u 更新系统的分步用户指南。下面每一步都给出准确命令与
你应该看到的输出。如果你的输出不同，见文末"故障排查"。

完成之后你将得到：一个会检查服务器新版本、下载、校验、安装到
slot B、引导进入并随时能回滚 slot A 的 Open Cube OS 系统
——全部在运行中的系统内完成。

## 0. 你需要什么

| 项目                          | 位置                         |
|-------------------------------|------------------------------|
| Open Cube OS ISO（WP-10u+）   | GitHub Releases / 官网       |
| QEMU（6.0+）或真实硬件        | 发行版软件包                 |
| 仓库的 tools 目录             | 本仓库 `tools/`              |
| mtools（mformat/mcopy/mdir）  | make_ab_disk.sh 需要         |

除注明"在客户机内"的步骤外，所有命令都在宿主机运行。

## 1. 创建 A/B 盘

一条命令。可选传入一个内核 ELF 预置 slot A，使磁盘可独立引导：

```sh
bash tools/make_ab_disk.sh build/opencube.elf
```

预期输出（节选）：

```
[make_ab_disk] image: build/abdisk.img (920576 sectors)
[make_ab_disk] MBR written: starts=[2048, 133120, 395264, 657408]
[make_ab_disk] formatted p1 (OCS_BOOT, 131072 sectors)
[make_ab_disk] formatted p2 (OCS_A, 262144 sectors)
[make_ab_disk] formatted p3 (OCS_B, 262144 sectors)
[make_ab_disk] formatted p4 (OCS_DATA, 262144 sectors)
[make_ab_disk] p1: /boot/grub/grub.cfg installed
[make_ab_disk] p2 (slot A): /boot/opencube.elf seeded from build/opencube.elf
[make_ab_disk] done: /home/.../build/abdisk.img
```

布局（与 kernel/ota/ota_ab.h 一致）：p1 = boot/flags（64 MiB），
p2 = slot A，p3 = slot B，p4 = data（各 128 MiB，全部 FAT32）。

## 2. 挂上 A/B 盘引导 ISO

```sh
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso -boot d \
  -drive if=ide,format=raw,file=build/abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

（或把 `-drive if=ide,format=raw,file=build/abdisk.img` 加到你已有的
任何配置；内核自动发现 A/B 布局。）

在客户机内，确认磁盘被识别：

```
oc> update --status
update: current version: WP-10-AUDIT_P2-fix2b-7-g98a8dc7
update: current boot: A
update: next boot: A
update: available: unknown (run update or checkupdate)
update: online update: enabled
update: A/B disk: present
```

（`current version` 行是烘焙进内核的构建期 `git describe --tags`
串——Makefile `OC_RELEASE_VERSION`——你的构建报告的是它自己的
源码树，不是上面这个字面值。）

`A/B disk: absent` 表示没找到布局——检查镜像是否由 make_ab_disk.sh
构建，并且是以硬盘（而非光盘）挂载的。

A/B 层的完整自测：

```
oc> ab_partition_test
[ab_partition_test] A/B disk + slots
[partition devices hdap1..p4] PASS
[slot mounts /ab/boot /ab/a /ab/b /data] PASS
[abp2 boot sector (FAT32 BPB)] PASS
[boot slot known (A/B/ISO)] slot=A PASS
[boot flags readable] next_B=no ok_B=no bootfail_B=no PASS
[set_boot(B) -> flags -> rollback clears] PASS
[set_boot(X) rejected] PASS
[ab_partition_test] 7/7 PASS
```

## 3. 构建更新包

```sh
bash tools/make_update_pkg.sh WP-10d build/opencube.elf build/pkg.tar.gz
```

预期输出：

```
[make_update_pkg] package: build/pkg.tar.gz
[make_update_pkg] payload sha256 (manifest): 494c16ea...
[make_update_pkg] package sha256 (.tar.gz): d1ce3772...
[make_update_pkg] package size: 314813
```

.tar.gz 内含 manifest.json + kernel/opencube.elf + boot/grub.cfg
+ etc/opencube.conf + docs/。这里涉及两个 SHA256 值：
- `package sha256 (.tar.gz)`：写入 update.json（`package_sha256`）；
- `payload sha256 (manifest)`：在包内，流式安装时校验。

## 4. 提供 manifest 与包

一条命令提供全部（manifest、可选 v2 manifest、包）：

```sh
python3 tools/update_server.py --mode http --port 8008 \
  --json '{ "version": "WP-10d", "time": "2026-10-20",
            "changes": "new release",
            "changes_v2_url": "http://10.0.2.2:8008/update-v2.json",
            "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
            "package_sha256": "<step 3 的 64 位十六进制>",
            "package_size": 314813 }' \
  --json2 '{ "version": "WP-10d", "time": "2026-10-20",
             "changes": "完整长 changelog 写在这里",
             "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
             "package_sha256": "<同上>",
             "package_size": 314813 }' \
  --pkg build/pkg.tar.gz
```

`10.0.2.2` 是 QEMU 用户态网络视角的宿主机。`package_sha256` 与
`package_size` 必须与第 3 步打印的值一致——否则内核拒绝该包。

`changes` 保持短（<= 127 字节），旧内核（WP-09）也能读；理解
`changes_v2_url` 的内核会从 /update-v2.json 拉取长 changelog
（见 docs/CONFIG.md 6.1）。

## 5. 执行更新（在客户机内）

```sh
oc> update
update: checking https://.../update.json
update: reading /etc/opencube.conf
New version available.
Version: WP-10d
Changes: new release
update: downloading http://10.0.2.2:8008/pkg.tar.gz
update: downloaded 314813 bytes
update: SHA256 OK
update: installing into slot B
update: manifest payload digest OK
update: installed 4 file(s)
update: boot flags set: next boot = B
update: done. Reboot to start the new version.
```

然后重启——GRUB 读取 `next_B` 标志并引导 slot B：

```sh
oc> reboot
```

slot B 起来后会写 `ok_B`（确认启动成功）。检查：

```sh
oc> update --status
update: current boot: B
update: next boot: B
```

## 6. 回滚

任意时刻（从 slot A 或 B）：

```sh
oc> rollback
update: next boot set to slot A
oc> reboot
```

如果 slot B 始终没能走到桌面横幅，它在启动时悲观写入的
`bootfail_B` 标志会留着，GRUB 在下次上电时自动回退 slot A
——无需任何命令。

## 7. 离线更新（无网络）

把包放到 data 分区（或任何已挂载的 FAT32 卷）并运行：

```sh
oc> update --local /data/pkg.tar.gz
```

其余一切（校验、安装、boot 标志）完全相同。

## 8. 只检查不安装

```sh
oc> checkupdate          # 只拉取 + 显示
oc> update --status      # 当前/下次启动槽位、A/B 在位、可用性
```

## 命令参考

| 命令                | 用途                                       |
|---------------------|--------------------------------------------|
| update              | 检查 + 下载 + 校验 + 安装到 B              |
| update --local PATH | 从包文件离线安装                           |
| update --status     | 当前版本、启动槽位、可用性                 |
| checkupdate         | 只检查 + 显示（不下载）                    |
| rollback            | 下次启动回 slot A                          |
| reboot              | 冲刷设备并重启                             |

## 自测套件

| 测试                 | 覆盖                                |
|----------------------|-------------------------------------|
| update_pkg_test      | gzip/DEFLATE + tar（12 项）         |
| ab_partition_test    | A/B 发现、挂载、标志（7 项）        |
| update_check_test    | manifest 解析含 v2（4 项）          |
| update_download_test | 流式下载（3 项）                    |
| update_verify_test   | SHA256 + 反例（4 项）               |
| update_install_test  | 安装到 slot B（6 项）               |
| update_rollback_test | 标志切换 + 回滚（4 项）             |
| update_local_test    | 离线流程（4 项）                    |
| update_status_test   | 状态报告（5 项）                    |
| real_update_test     | 完整端到端第一阶段（7 项）          |

## 故障排查

| 信息                                          | 含义 / 修复                             |
|-----------------------------------------------|-----------------------------------------|
| `update: A/B disk: absent`                    | 盘未挂载或不是 make_ab_disk.sh 构建的   |
| `config file missing or unreadable`           | /etc/opencube.conf 缺失；运行 `config restore` |
| `invalid URL prefix`                          | update_url 必须是 http:// 或 https://   |
| `DNS resolution failed` / `connect failed`    | 网络不通；先 `dhcp` 再重试              |
| `JSON parse failed`                           | 服务器未返回 manifest                   |
| `response too large`                          | manifest 超过接收缓冲                   |
| `SHA256 mismatch`                             | update.json 的 package_sha256 不对      |
| `package is not valid gzip/tar`               | 上传损坏；用 make_update_pkg.sh 重建    |
| `invalid slot name`                           | 只能是 A 或 B                           |
| `online update disabled`                      | 在 /etc/opencube.conf 设置 online_update=yes |

错误码（kernel/ota/ota_update.h）：-1..-8 传输（WP-09-fix5 契约），
-9 disabled，-10 无 A/B 盘，-11 SHA 不匹配，-12 坏 gzip/tar，
-13 I/O，-14 槽位，-15 参数。

## 文件

- kernel/ota/ota_ab.h/.c - A/B 框架 + 更新器
- kernel/ota/ota_update.h/.c - manifest 检查（Plan D v2 changelog）
- tools/make_ab_disk.sh - A/B 磁盘镜像构建器（宿主）
- tools/make_update_pkg.sh - 更新包构建器（宿主）
- tools/update_server.py - manifest + v2 + 包测试服务器（宿主）

接口详情：docs/EXTENSIONS_WP10u.md。配置键：docs/CONFIG.md。
