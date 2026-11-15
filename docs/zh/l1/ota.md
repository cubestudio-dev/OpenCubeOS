<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用侧的 OTA / A-B 服务

更新机制住在 selfhost 服务（`ocos/services/selfhost/`）里；
应用通过 L1SVC "ota" 类别和配置存储触达它。

## L1SVC 表面

| Op | 命令 | 作用 |
|----|---------|--------------|
| 1 | `checkupdate` | 拉取并解析清单、比较（数值化构建号比较，BUG-0347） |
| 2 | `config` | list / get / set / restore / path - /etc/opencube.conf 存储 |
| 5 | `update` | `status` / `--local <pkg>` / 裸调用 = 在线链 |
| 6 | `rollback` | 把启动标志翻到另一个槽 |
| 7-16 | `update_pkg_test` .. `real_update_test` | 客户机内测试批次 |

`update status` 打印向导探测的机器画像：

```
update: current version: 3.0.0-...
update: current boot: ISO          (or slot A / slot B)
update: next boot: (default)
update: available: none (up to date)
update: online update: enabled
update: A/B disk: present          <- the wizard anchor
```

## 链条（在线流程做什么）

check（从配置的 `update_url` 取清单）-> download -> verify
（清单 SHA256 在 install 内强制执行）-> 安装到*非活动*槽 ->
设置启动标志 -> "reboot required"。

离线：`update --local /path/to/package.tar.gz` 从挂载卷运行同
一条安装路径。

## A/B 盘

两个槽 + 启动标志，由 `abdisk <dev>`（或 `disk-tool` 的 A/B
管理器向导）创建。GRUB 读标志；菜单提供强制选槽项。`abcfg`
显示这些命令写出的确切 grub.cfg。安装目标是非活动槽
（BUG-0110：由 `update_target_slot()` 决定，不由调用者决定）。

## 配置键（/etc/opencube.conf）

```
update_url=https://cubestudio-dev.github.io/OpenCubeOS/update.json
package_url=https://github.com/cubestudio-dev/OpenCubeOS/releases/latest
auto_check=no
online_update=yes
shell.lang=en
```

`config get update_url` 就是更新向导 DNS 步骤在解析之前拆解的
东西（scheme -> host）。

## 大应用

`update`（无参数）= 5 步向导；其余一切参数形态都是逐字操作。
操作员视图见用户手册的
[../manual/update-ota.md](../manual/update-ota.md)，锚点纪律见
[wizard-library.md](wizard-library.md)。

## 客户机内测试

`ab_partition_test`、`update_*_test` 批次、`checkupdate_test`、
`config_test` - 全部是 sysdiag/update 表的经典操作，全部在
注册表里。
