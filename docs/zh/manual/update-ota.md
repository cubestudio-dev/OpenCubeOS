<!-- SPDX-License-Identifier: Apache-2.0 -->
# A/B 槽位、安装与 OTA 更新

## 更新向导

预期路径是向导——它用五个受检步骤走完"把本机带到最新版本"这一意图：

```
ush> update
```

| 步骤 | 做什么 | 能修复什么 |
|------|--------------|-----------------|
| 1/5 网络 | 读取 IP 状态 | 地址为 0.0.0.0 时提供 DHCP 修复，然后复查 |
| 2/5 A/B 盘 | 读取 `update status` | 盘缺失时：显示 `lsblk`，引导你完成双重确认的 `abdisk <dev>`，然后复查 |
| 3/5 DNS | 对从实时 `config get update_url` 取值解析出的更新主机做域名解析 | - |
| 4/5 检查 | `checkupdate` | 已是最新则以 OK 结束向导 |
| 5/5 安装 | 经确认的在线更新 | 经确认重启进入新槽位 |

每一步都会打印判定（`[OK]` / `[!!]`），前提条件无法修复时干净中止——没有单独的 `y`，任何破坏性操作都不会运行。

## A/B 机制

selfhost 服务维护两个槽位（A 与 B）外加一个启动标志：

- GRUB 读取标志并引导活动槽位；菜单提供强制槽位项作为手动覆盖。
- 更新安装到*非活动*槽位，期间系统照常运行。
- 下次启动切换到更新后的槽位；如果你不认可它，`update rollback` 会切回去。

`update status` 展示全貌：当前版本、当前启动槽位、下次启动、可用更新、在线更新标志，以及 A/B 盘是否存在。

## 安装到磁盘

从运行中的 ISO 引导系统出发：

```
ush> dskstat                (pick a target disk, e.g. hd0)
ush> install hd0            (partition + copy the running kernel +
                             GRUB boot files + /etc volume)
```

或者走向导：`disk-tool` -> A/B 更新盘管理器 -> 创建（会询问设备名，重输并确认）。

`install` 把 multiboot2 模块 `self`——也就是此刻正在运行的那个内核 ELF——复制到磁盘，因此已安装的系统始终与其安装程序匹配。`abdisk <dev>` 做同样的事，只是做成 A/B 更新盘的形态（双槽位 + 标志；会销毁数据）。`abcfg` 显示这些命令写出的确切 grub.cfg 文件。

## 检查更新

```
ush> checkupdate            (reads the update URL from /etc/opencube.conf
                             - http or https - and parses the manifest)
ush> config get update_url  (the configuration key it uses)
```

清单就是网站上的 `update.json`：版本、构建号、包 URL、包 SHA256、大小。构建号比较是数值式的（BUG-0347），因此即使版本字符串相同，构建号更高的重构建也会胜出。

## 更新源与镜像

更新源就是 `update_url` 配置键——一个指向 `update.json` 清单的普通 HTTPS URL。向导第 3 步读取实时值（`config get update_url`）并在任何网络动作之前先解析它的主机名。要让机器指向另一个源，用普通的配置写入器：

```
ush> config set update_url <url>
```

已知可用值（它们都从 gh-pages 分支提供*同一份*清单，选你网络里最快的一个即可）：

| 源 | URL |
|--------|-----|
| 官方（GitHub Pages） | `https://cubestudio-dev.github.io/OpenCubeOS/update.json` |
| GitHub raw 镜像 | `https://raw.githubusercontent.com/cubestudio-dev/OpenCubeOS/gh-pages/update.json` |
| jsDelivr CDN 镜像 | `https://cdn.jsdelivr.net/gh/cubestudio-dev/OpenCubeOS@gh-pages/update.json` |

自定义值也可用——必须以 `http://` 或 `https://` 开头，并指向同样形状的清单（否则 `checkupdate` 会打印解析错误）。镜像在你某个主机被屏蔽或很慢时有用：清单携带绝对包 URL，所以切换源后，包下载跟随清单自身的 `url` 字段。

## 应用更新

在线流程（向导第 5 步确认后所运行的，也是裸 `update direct` 所做的）：

```
ush> update direct          check -> download -> verify -> install
ush> reboot                 boot the updated slot
```

离线/物理隔离的机器：

```
ush> update --local /path/to/package.tar.gz
```

OTA 包（`opencube-<version>-update.tar.gz`）携带新的内核 ELF 和一份清单。校验是密码学层面的：清单 SHA256 在安装过程中强制校验——未通过的包会被拒绝。

大包从不压迫内存：下载器以 4 KiB 块流式写盘（`DL_CHUNK`，`ocos/services/selfhost/src/svc_selfhost_ab.c` 的 `ota_update_download`），因此包远大于内存也能完成更新；内存里只会有 SHA256 状态和一个块缓冲区。

## 回滚

切换后如果某个槽位表现异常：

1. 在 GRUB 菜单中选择另一个槽位（强制项），或者
2. 在运行中的系统里执行 `ush> update rollback`。

`ab_partition_test` 在一块临时盘上演练槽位发现、标志与槽位切换，不会碰你真实的启动盘。

## 更新相关命令

`checkupdate`、`update`（向导 + 上面的链）、`rollback`、`config`（更新 URL 相关键）、`abdisk`、`abcfg`、`ab_partition_test`。它们同时也是 `update` / `disk-tool` 应用的经典操作。完整列表（每条命令一页）见[命令索引](commands/index.md)——"OTA 更新与 A/B 槽位"领域。

## 内部机制

关于标志如何存储、install 如何向非活动槽位分阶段写入，以及与 GRUB 的启动标志握手，见 [../internals/ota-ab.md](../internals/ota-ab.md)。
