<!-- SPDX-License-Identifier: Apache-2.0 -->
# OTA 与 A/B 架构

## 槽位与标志

`kernel/ota/ota_ab.{c,h}`：两个槽位 + 一个启动标志 + 每槽位状态（成功 / 启动中 / 失败）。GRUB 的菜单脚本读取该标志；`update` 在分阶段安装后写入它；`rollback` 将其翻转回去。

## 更新链

`kernel/ota/ota_update.{c,h}`：检查（获取并解析清单）、下载（通过 HTTP/HTTPS 到文件）、验证（SHA256 + 签名）、安装（到非活动槽位）、设置启动（标志）、回滚，以及状态查询。L1 接口面 1:1 地镜像了这些操作（`l1_ext_check_update`、`l1_ext_update_download` 等）。

## 清单

发布站点上的 `update.json`：版本、构建、时间、变更、软件包 URL、软件包 SHA256、软件包大小。`checkupdate` 从 `/etc/opencube.conf` 读取该 URL 并精确解析此内容。

## 软件包

`opencube-<version>-update.tar.gz`：新的内核 ELF + 清单。在构建日内是确定性的（清单嵌入了 UTC 日期）；ELF 本身是完全可复现的。

## 客户端内测试

`ab_partition_test`、`update_*_test`（整个链路，打包为命令）、`checkupdate_test`（URL/JSON 单元 + 实时探测），以及执行检查→下载→验证→安装→重启真实流程的发布门禁 E2E。
