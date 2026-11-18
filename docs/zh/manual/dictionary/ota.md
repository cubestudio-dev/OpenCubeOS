<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - OTA 更新 (15)

### ab_partition_test

| 字段 | 值 |
|---|---|
| 名称 | ab_partition_test |
| 分类 | OTA 更新 |
| 功能 | A/B 分区测试 |
| 用法 | `ab_partition_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
ab_partition_test
[ab_partition_test] A/B disk + slots
[partition devices hdap1..p4] FAIL
[slot mounts /ab/boot /ab/a /ab/b /data] FAIL
[hdap2 boot sector (FAT32 BPB)] FAIL
[boot slot known (A/B/ISO)] slot=ISO PASS
[boot flags readable] next_B=no ok_B=no bootfail_B=no FAIL
[set_boot(B) -> flags -> rollback clears] FAIL
[set_boot(X) rejected] FAIL
[ab_partition_test] 1/7 FAIL
ush> 
```

### checkupdate

| 字段 | 值 |
|---|---|
| 名称 | checkupdate |
| 分类 | OTA 更新 |
| 功能 | 检查 OTA 更新 |
| 用法 | `checkupdate` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
checkupdate
checkupdate: reading /etc/opencube.conf
checkupdate: DNS resolution failed
ush> 
```

### checkupdate_test

| 字段 | 值 |
|---|---|
| 名称 | checkupdate_test |
| 分类 | OTA 更新 |
| 功能 | OTA 检查测试 |
| 用法 | `checkupdate_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
checkupdate_test
[checkupdate_test] https:// prefix -> TLS: PASS
[checkupdate_test] http:// prefix -> HTTP: PASS
[checkupdate_test] ftp:// prefix rejected: PASS
[checkupdate_test] JSON manifest parses: PASS
[checkupdate_test] broken JSON rejected: PASS
[checkupdate_test] 300-byte changes parses into 1 KiB buffer: PASS
[checkupdate_test] 300-byte changes rejected by 128-byte buffer (-8): PASS
[checkupdate_test] changes_v2_url extracts from manifest: PASS
[checkupdate_test] changes_v2 missing/rejected URLs keep short changes: PASS
[checkupdate_test] v2 long-changelog accepts changes and changes_full: PASS
[checkupdate_test] oversized v2 changelog reports -8: PASS
[checkupdate_test] describe-descendant shapes: PASS
[checkupdate_test] numeric build-id compare: PASS
[checkupdate_test] live probe (DNS resolution failed): FAIL
[checkupdate_test] 13/14 FAIL
ush> 
```

### config

| 字段 | 值 |
|---|---|
| 名称 | config |
| 分类 | OTA 更新 |
| 功能 | 读写系统配置 |
| 用法 | `config list|get|set` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
config
usage: config list | get <key> | set <key> <value> | restore | path
ush> 
```

### config_test

| 字段 | 值 |
|---|---|
| 名称 | config_test |
| 分类 | OTA 更新 |
| 功能 | 配置后端测试 |
| 用法 | `config_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
config_test
[config_test] write+read custom key: PASS
[config_test] write auto_check=YES (normalized to yes): PASS
[config_test] reject invalid auto_check=maybe: PASS
[config_test] reject non-ASCII value: PASS
[config_test] get_all contains test_key and update_url: PASS
[config_test] delete file -> read reports missing: PASS
[config_test] restore defaults (4 keys): PASS
[config_test] package_url write/read: PASS
[config_test] online_update yes/no/invalid: PASS
[config_test] final restore: PASS
[config_test] 10/10 PASS
ush> 
```

### real_update_test

| 字段 | 值 |
|---|---|
| 名称 | real_update_test |
| 分类 | OTA 更新 |
| 功能 | OTA 真实流程测试 |
| 用法 | `real_update_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
real_update_test
[real_update_test] end-to-end phase 1 (download -> install -> set_boot)
[A/B disk present] FAIL
ush> 
```

### rollback

| 字段 | 值 |
|---|---|
| 名称 | rollback |
| 分类 | OTA 更新 |
| 功能 | 回滚到另一分区 |
| 用法 | `rollback` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
rollback
rollback: no A/B disk
ush> 
```

### update_check_test

| 字段 | 值 |
|---|---|
| 名称 | update_check_test |
| 分类 | OTA 更新 |
| 功能 | 升级检查单元测试 |
| 用法 | `update_check_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_check_test
[update_check_test] package manifest check
[json_uint parses package_size] PASS
[json_uint rejects non-number] PASS
[live check_pkg (OK/NEW)] rc=18446744073709551613 version=1613 FAIL
[manifest has package fields] url= size=0 FAIL
[update_check_test] 2/4 FAIL
ush> 
```

### update_download_test

| 字段 | 值 |
|---|---|
| 名称 | update_download_test |
| 分类 | OTA 更新 |
| 功能 | OTA 下载测试 |
| 用法 | `update_download_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_download_test
[update_download_test] streaming package download
[manifest check] FAIL
[update_download_test] need package_url (run on a network with the update server)
ush> 
```

### update_install_test

| 字段 | 值 |
|---|---|
| 名称 | update_install_test |
| 分类 | OTA 更新 |
| 功能 | OTA 安装测试 |
| 用法 | `update_install_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_install_test
[update_install_test] install package into slot B
[A/B disk present] FAIL
[update_install_test] attach the A/B disk (build/abdisk.img)
ush> 
```

### update_local_test

| 字段 | 值 |
|---|---|
| 名称 | update_local_test |
| 分类 | OTA 更新 |
| 功能 | OTA 本地文件测试 |
| 用法 | `update_local_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_local_test
[update_local_test] offline package flow
[A/B disk present] FAIL
ush> 
```

### update_pkg_test

| 字段 | 值 |
|---|---|
| 名称 | update_pkg_test |
| 分类 | OTA 更新 |
| 功能 | 升级包测试 |
| 用法 | `update_pkg_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_pkg_test
[update_pkg_test] gzip/DEFLATE + tar unit tests
[gunzip stored block] exp=1280 got=1280 PASS
[gunzip fixed huffman block] exp=1280 got=1280 PASS
[gunzip dynamic huffman block] exp=1280 got=1280 PASS
[gunzip tiny (no matches)] exp=6 got=6 PASS
[gzip header FNAME] exp=1280 got=1280 PASS
[gzip header FEXTRA] exp=1280 got=1280 PASS
[gzip header FEXTRA+FNAME] exp=1280 got=1280 PASS
[corrupted CRC32 rejected] PASS
[truncated stream rejected] PASS
[streaming resume (1-byte chunks)] out=1280 PASS
[ustar parse (name/size/content)] files=1 name=etc/opencube.conf PASS
[ustar bad checksum rejected] PASS
[update_pkg_test] 12/12 PASS
ush> 
```

### update_rollback_test

| 字段 | 值 |
|---|---|
| 名称 | update_rollback_test |
| 分类 | OTA 更新 |
| 功能 | OTA 回滚测试 |
| 用法 | `update_rollback_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_rollback_test
[update_rollback_test] boot flags + rollback
[A/B disk present] FAIL
ush> 
```

### update_status_test

| 字段 | 值 |
|---|---|
| 名称 | update_status_test |
| 分类 | OTA 更新 |
| 功能 | OTA 状态测试 |
| 用法 | `update_status_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_status_test
[update_status_test] status struct coverage
[status returns 0] PASS
[current_version filled] PASS
[current_boot known (A/B/ISO)] PASS
[online_update flag 0/1] PASS
[ab_present flag 0/1] PASS
[update_status_test] version=3.0.0-NAK boot=ISO next=(default) online=yes ab=no
[update_status_test] 5/5 PASS
ush> 
```

### update_verify_test

| 字段 | 值 |
|---|---|
| 名称 | update_verify_test |
| 分类 | OTA 更新 |
| 功能 | OTA 校验测试 |
| 用法 | `update_verify_test` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

```
update_verify_test
[update_verify_test] SHA256 verification
[package file present] FAIL
[update_verify_test] run update_download_test first
ush> 
```
