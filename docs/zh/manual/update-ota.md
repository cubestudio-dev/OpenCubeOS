<!-- SPDX-License-Identifier: Apache-2.0 -->
# A/B 槽位、安装与 OTA 升级

## A/B 方案

`kernel/ota/ota_ab.c` 维护两个槽位（A 和 B）以及一个启动标志：

- GRUB 读取标志并启动活动槽位；菜单提供强制槽位条目作为手动覆盖。
- 更新在您继续运行时安装到*非活动*槽位。
- 下次启动将切换到更新后的槽位；如果拒绝，`update rollback` 会切换回来。

## 安装到磁盘

从正在运行的 ISO 启动：

```
oc> dskstat                (pick a target disk, e.g. ata0)
oc> install ata0           (partition + copy the running kernel +
                            GRUB boot files + /etc volume)
```

`install` 将 multiboot2 模块 `self`——即当前正在运行的内核 ELF——复制到磁盘，因此安装的系统始终与其安装程序匹配。`abdisk <dev>` 执行相同操作，但格式为 A/B 更新磁盘（两个槽位 + 标志；会销毁数据）。

`abcfg` 显示这些命令写入的确切 grub.cfg 文件。

## 检查更新

```
oc> checkupdate            (reads the update URL from /etc/opencube.conf
                            - http or https - and parses the manifest)
oc> config get update_url  (the configuration key it uses)
```

清单是网站的 `update.json`：版本、构建、包 URL、包 SHA256、大小。

## 应用更新

```
oc> update download        (fetch the OTA package)
oc> update verify          (SHA256 + signature check)
oc> update install a       (install onto slot A/B - the inactive one)
oc> reboot                 (boot the updated slot)
```

OTA 包（`opencube-<version>-update.tar.gz`）携带新的内核 ELF 和一个清单。验证是密码学性质的：哈希加签名——任一项失败的包都会被拒绝。

## 回滚

如果槽位在切换后行为异常：

1. 从 GRUB 菜单选择另一个槽位（强制条目），或
2. 在运行中的系统内执行 `oc> update rollback`。

`ab_partition_test` 在一个临时磁盘上测试槽位发现、标志和槽位切换，不会触碰您的真实启动磁盘。

## 更新相关命令

`checkupdate`、`checkupdate_test`、`update`（上述流程）、`ab_partition_test`、`abcfg`、`abdisk`、`config`（更新 URL 键）。完整列表每条命令一页：
[命令索引](commands/index.md) - "OTA 更新与 A/B 槽位" 领域。

## 内部机制

有关标志如何存储、安装如何准备非活动槽位、以及与 GRUB 的启动标志握手机制，详见
[../internals/ota-ab.md](../internals/ota-ab.md)。
