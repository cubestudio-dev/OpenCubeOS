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
```
