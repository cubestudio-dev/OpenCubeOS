<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - 系统内更新（OTA）操作指南（UPDATE-HOWTO 中文版）

WP-10u 更新系统的分步用户指南。下面每一步都给出确切命令与你应当
看到的输出。如果你的输出不一致，见文末"故障排查"。

完成本指南后你将得到：一个能检查服务器新版本、下载、校验、安装到
slot B、引导进入新版本、并能回滚到 slot A 的 Open Cube OS 系统——
全部在运行中的系统内完成。

## 0. 你需要什么

| 项目                          | 来源                         |
|-------------------------------|------------------------------|
| Open Cube OS ISO（WP-10u+）   | GitHub Releases / 本站       |
| QEMU（6.0+）或真实硬件        | 发行版软件包                 |
| 本仓库 tools 目录             | 本仓库，`tools/`             |
| mtools（mformat/mcopy/mdir）  | make_ab_disk.sh 需要         |

除标注"客户机内"的步骤外，所有命令都在宿主机运行。

## 1. 创建 A/B 磁盘

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

布局（与 kernel/ab_update.h 一致）：p1 = boot/flags（64 MiB）、
p2 = slot A、p3 = slot B、p4 = data（各 128 MiB，全部 FAT32）。

## 2. 挂上 A/B 磁盘引导 ISO

```sh
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso -boot d \
  -drive if=ide,format=raw,file=build/abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

（或在你已有的任何启动配置上追加
`-drive if=ide,format=raw,file=build/abdisk.img`；内核会自动发现
A/B 布局。）

客户机内，确认磁盘已被识别：

```
oc> update --status
update: current version: WP-10c
update: current boot: A
update: next boot: A
update: available: unknown (run update or checkupdate)
update: online update: enabled
update: A/B disk: present
```

`A/B disk: absent` 表示未找到该布局——检查镜像是否由
make_ab_disk.sh 生成，并且是作为硬盘（而非光盘）挂载的。

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

## 3. 制作更新包

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
+ etc/opencube.conf + docs/。涉及两个 SHA256 值：
- `package sha256 (.tar.gz)`：填入 update.json（`package_sha256`）；
- `payload sha256 (manifest)`：在包内，安装流式传输时逐字节校验。

## 4. 发布 manifest 与更新包

一条命令发布全部内容（manifest、可选 v2 manifest、更新包）：

```sh
python3 tools/update_server.py --mode http --port 8008 \
  --json '{ "version": "WP-10d", "time": "2026-10-20",
            "changes": "new release",
            "changes_v2_url": "http://10.0.2.2:8008/update-v2.json",
            "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
            "package_sha256": "<第 3 步输出的 64 位十六进制>",
            "package_size": 314813 }' \
  --json2 '{ "version": "WP-10d", "time": "2026-10-20",
             "changes": "完整长度的变更日志写在这里",
             "package_url": "http://10.0.2.2:8008/pkg.tar.gz",
             "package_sha256": "<同上>",
             "package_size": 314813 }' \
  --pkg build/pkg.tar.gz
```

`10.0.2.2` 是 QEMU 用户态网络看到的宿主机地址。
`package_sha256` 与 `package_size` 必须与第 3 步输出一致——否则
内核拒绝该包。

`changes` 保持短（<= 127 字节），老内核（WP-09）也能读取；理解
`changes_v2_url` 的内核会从 /update-v2.json 拉取长变更日志（见
docs/CONFIG.md 6.1）。

## 5. 执行更新（客户机内）

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

slot B 启动成功后会写入 `ok_B`（引导确认）。检查：

```sh
oc> update --status
update: current boot: B
update: next boot: B
```

## 6. 回滚

任何时候都可以（从 slot A 或 B）：

```sh
oc> rollback
update: next boot set to slot A
oc> reboot
```

如果 slot B 始终没能走到正常启动横幅，它在引导时悲观写入的
`bootfail_B` 标志会保留下来，GRUB 在下次上电时自动回退到
slot A——无需任何命令。

## 7. 离线更新（无网络）

把更新包放到 data 分区（或任何已挂载的 FAT32 卷），运行：

```sh
oc> update --local /data/pkg.tar.gz
```

其余步骤（校验、安装、引导标志）完全相同。

## 8. 只检查不安装

```sh
oc> checkupdate          # 拉取并显示，不下载
oc> update --status      # 当前/下次引导槽位、A/B 在位状态、可用性
```

## 命令参考

| 命令                | 用途                                      |
|---------------------|-------------------------------------------|
| update              | 检查 + 下载 + 校验 + 安装到 B             |
| update --local PATH | 从包文件离线安装                          |
| update --status     | 当前版本、引导槽位、可用性                |
| checkupdate         | 只检查并显示（不下载）                    |
| rollback            | 下次引导回到 slot A                       |
| reboot              | 刷写设备并重启                            |

## 自测套件

| 测试                 | 覆盖范围                            |
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

| 消息                                        | 含义 / 处理                            |
|---------------------------------------------|----------------------------------------|
| `update: A/B disk: absent`                  | 磁盘未挂载或不是 make_ab_disk.sh 生成的 |
| `config file missing or unreadable`         | /etc/opencube.conf 缺失；运行 `config restore` |
| `invalid URL prefix`                        | update_url 必须是 http:// 或 https://  |
| `DNS resolution failed` / `connect failed`  | 网络不通；先 `dhcp` 再重试             |
| `JSON parse failed`                         | 服务器未返回 manifest                  |
| `response too large`                        | manifest 超过接收缓冲区                |
| `SHA256 mismatch`                           | update.json 的 package_sha256 不对     |
| `package is not valid gzip/tar`             | 上传损坏；用 make_update_pkg.sh 重做   |
| `invalid slot name`                         | 只能用 A 或 B                          |
| `online update disabled`                    | 在 /etc/opencube.conf 设置 online_update=yes |

错误码（kernel/update.h）：-1..-8 传输层（WP-09-fix5 契约）、
-9 已禁用、-10 无 A/B 磁盘、-11 SHA 不匹配、-12 gzip/tar 损坏、
-13 I/O、-14 槽位、-15 参数。

## 相关文件

- kernel/ab_update.h/.c - A/B 框架 + 更新器
- kernel/update.h/.c - manifest 检查（方案 D v2 变更日志）
- tools/make_ab_disk.sh - A/B 磁盘镜像制作（宿主机）
- tools/make_update_pkg.sh - 更新包制作（宿主机）
- tools/update_server.py - manifest + v2 + 更新包测试服务器（宿主机）

接口细节：docs/EXTENSIONS_WP10u.md。配置键：docs/CONFIG.md。
