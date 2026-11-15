<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：OTA 与 A/B 槽位

自托管/更新故事：内核可以仅用自己的驱动
（net+TLS+fs+block+crypto）加上 `/etc/opencube.conf` 配置
存储，检查、下载、校验、安装并回滚自己的下一个版本。
实现：`ocos/services/selfhost/` ——
`interface/svc_selfhost_{api,ab,update}.h`、
`src/svc_selfhost_{ab,update,cmds_config,cmds_test,l1}.c`。
安装路径解剖：
[../dev/call-chains.md](../dev/call-chains.md) 第 6 节。
用户指南：[../manual/update-ota.md](../manual/update-ota.md)。

## 1. A/B 槽位布局（逐字摘自 svc_selfhost_ab.h）

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

常量：`OC_AB_SLOT_ISO 0 / A 1 / B 2`；挂载点 `/ab/boot /ab/a /ab/b /data`；标志 `/boot/next_B`、`/boot/ok_B`、
`/boot/bootfail_B`；包默认 `/data/update-pkg.tar.gz`（临时
`/tmp/update-pkg.tar.gz`）。当前槽位经 multiboot2 命令行
`oc.slot=A|B|ISO` 到达。

## 2. 启动标志语义

启动选择（grub.cfg 逻辑）：

| `next_B` | `bootfail_B` | `ok_B` | 启动 |
|---|---|---|---|
| 存在 | 不存在 | - | **B**（一次新安装待定） |
| 存在 | 存在 | - | **回滚到 A** |
| 不存在 | - | 存在 | **B**（已确认良好） |
| 不存在 | - | 不存在 | **A**（默认） |

- **悲观标记**："B 内核在启动时悲观地写 bootfail_B，在
  系统完全起来之后才移除它"（kmain 步骤 16c 的
  `ota_update_confirm_boot()` 清除 bootfail 标记）。一个
  从未到达"完全起来"的槽位会保留标记 -> 下次启动回滚 ——
  这就是全部的崩溃安全故事。
- **启动尝试预算**（BUG-0305/A5-16）：GRUB 环境块
  `/boot/grub/grubenv`（1024 字节，`ab_attempts` 变量）、
  `OC_AB_MAX_ATTEMPTS 3`；内核在 A 完成启动或 B 确认时
  重置计数器 —— 循环重启的槽位在 3 次尝试后放弃并回退。

## 3. 更新流程

`update`（oc> update；向导走的是同样的 op）：

1. **检查** —— 经 HTTP 或 HTTPS 抓取 `update.json`
   （version + package_url + package_sha256 +
   package_size）；比较 version + build（数字构建比较，
   BUG-0347）。
2. **下载** —— 流式下载到 /data。
3. **校验** —— 包 SHA256 对照清单。
4. **解压 + 解包** —— RFC 1952/1951 充气（校验
   CRC32+ISIZE）+ 流式 ustar（校验和验证、GNU 'L' 长名、
   pax 跳过），直接写入**非活动槽位**。
5. **写启动标志** —— 打印 "reboot required"。
6. **重启**到新槽位；回滚清除标志。

失败的写永远不碰运行中的槽位 —— 活动系统一直服务到翻转
那一刻。

## 4. 16 个 L1 op

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

`update` 的隐藏语法：裸 = 在线流程；`status` /
`--status` = A/B + 版本状态视图（打印 `A/B disk: present`
锚点）；`--local <path>` / `local <path>` = 从包 tar.gz
离线安装（清单校验和已验证；需要 A/B 磁盘）。`config`
的语法：`config list | get <key> | set <key> <value> | restore | path`，作用于 `/etc/opencube.conf`。

依赖（自托管的证明）：block（`svc_block_drivers.h`、
`svc_block_part.h`）、fs（`svc_fs_vfs.h`）、net
（`svc_net_core.h`、`svc_net_tls.h`）、display
（`svc_display_lang.h` —— `config set shell.lang` 同步
实时控制台语言）。

## 5. 错误锚点（原文照录）

`update: status unavailable`、`update: download failed: `、
`update: cannot determine target slot (not booted from A/B?)`、`update: install failed: `、`update: setting boot failed`、`update: check failed: `、`rollback: failed`、
`config: cannot read /etc/opencube.conf`、
`: key not found`、`config: restore failed`。

更新向导的决策锚点（脚本会 grep 的稳定英文输出）：
"A/B disk: present"、"New version available."、
"Current version is up to date."、"DHCP success"、
"DNS resolution failed"、"reboot required"、"IP:"。

## 6. 磁盘工具

- `abdisk <dev>`（block op 20）以破坏性方式创建布局，
  随后重跑 `ota_ab_rescan()`，让运行中的内核立即看到它。
- `abcfg`（block op 23）显示 A/B 启动配置。
- `install <dev>` / `grub-install <dev>`（block op 21/22）
  用内嵌的 GRUB 载荷写可启动系统
  （`tools/embed_grub.py` 把 boot.img + grub-mkimage 的
  core.img 作为生成的 C 数组嵌进内核 —— "规则 9 自托管"
  接口面：零外部文件的可启动磁盘）。
- 宿主侧：`tools/make_ab_disk.sh` 构建 `build/abdisk.img`
  （p1 boot/flags 64M + 槽位 A 128M + 槽位 B 128M + data
  128M，仅 mtools、无 loop、无 root），
  `tools/make_update_pkg.sh` 构建 OTA 包（带版本 + 文件 +
  按归档顺序拼接的载荷体 sha256 的 `manifest.json`；
  WP05 起无 boot cfg 成员 —— OCBL 动态扫描），
  `tools/update_server.py` 为测试提供清单服务。

## 7. 验证

单元家族（op 3-4、7-16）覆盖每个阶段；E2E 的
`tools/fix1_checkupdate_e2e.py` 证明发布的 ISO 的
checkupdate 能看到真实在线源并诚实上报（版本反转回归）；
更新向导的步骤 1-5 由 mega 应用冒烟演练；
`ab_partition_test` 断言标志语义。E2E 更新测试构建一个
刻意更旧的 `OC_RELEASE_VERSION` 覆盖，使在线源看起来
更新。
