<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - OTA updates (15)

### ab_partition_test

| Field | Value |
|---|---|
| Name | ab_partition_test |
| Category | OTA updates |
| Function | A/B partition test |
| Usage | `ab_partition_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | checkupdate |
| Category | OTA updates |
| Function | check for OTA updates |
| Usage | `checkupdate` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
checkupdate
```

### checkupdate_test

| Field | Value |
|---|---|
| Name | checkupdate_test |
| Category | OTA updates |
| Function | OTA check test |
| Usage | `checkupdate_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
checkupdate_test
```

### config

| Field | Value |
|---|---|
| Name | config |
| Category | OTA updates |
| Function | read/write system config |
| Usage | `config list|get|set` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
config
```

### config_test

| Field | Value |
|---|---|
| Name | config_test |
| Category | OTA updates |
| Function | config backend test |
| Usage | `config_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
config_test
```

### real_update_test

| Field | Value |
|---|---|
| Name | real_update_test |
| Category | OTA updates |
| Function | OTA real-flow test |
| Usage | `real_update_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
real_update_test
```

### rollback

| Field | Value |
|---|---|
| Name | rollback |
| Category | OTA updates |
| Function | roll back to the other slot |
| Usage | `rollback` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
rollback
rollback: no A/B disk
ush> 
```

### update_check_test

| Field | Value |
|---|---|
| Name | update_check_test |
| Category | OTA updates |
| Function | update check unit test |
| Usage | `update_check_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_check_test
```

### update_download_test

| Field | Value |
|---|---|
| Name | update_download_test |
| Category | OTA updates |
| Function | OTA download test |
| Usage | `update_download_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_download_test
```

### update_install_test

| Field | Value |
|---|---|
| Name | update_install_test |
| Category | OTA updates |
| Function | OTA install test |
| Usage | `update_install_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_install_test
```

### update_local_test

| Field | Value |
|---|---|
| Name | update_local_test |
| Category | OTA updates |
| Function | OTA local-file test |
| Usage | `update_local_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_local_test
```

### update_pkg_test

| Field | Value |
|---|---|
| Name | update_pkg_test |
| Category | OTA updates |
| Function | update package test |
| Usage | `update_pkg_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_pkg_test
```

### update_rollback_test

| Field | Value |
|---|---|
| Name | update_rollback_test |
| Category | OTA updates |
| Function | OTA rollback test |
| Usage | `update_rollback_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_rollback_test
```

### update_status_test

| Field | Value |
|---|---|
| Name | update_status_test |
| Category | OTA updates |
| Function | OTA status test |
| Usage | `update_status_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_status_test
```

### update_verify_test

| Field | Value |
|---|---|
| Name | update_verify_test |
| Category | OTA updates |
| Function | OTA verify test |
| Usage | `update_verify_test` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update_verify_test
```
