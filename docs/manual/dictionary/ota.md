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
checkupdate: reading /etc/opencube.conf
checkupdate: DNS resolution failed
ush> 
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
usage: config list | get <key> | set <key> <value> | restore | path
ush> 
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
[real_update_test] end-to-end phase 1 (download -> install -> set_boot)
[A/B disk present] FAIL
ush> 
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
[update_check_test] package manifest check
[json_uint parses package_size] PASS
[json_uint rejects non-number] PASS
[live check_pkg (OK/NEW)] rc=18446744073709551613 version=1613 FAIL
[manifest has package fields] url= size=0 FAIL
[update_check_test] 2/4 FAIL
ush> 
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
[update_download_test] streaming package download
[manifest check] FAIL
[update_download_test] need package_url (run on a network with the update server)
ush> 
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
[update_install_test] install package into slot B
[A/B disk present] FAIL
[update_install_test] attach the A/B disk (build/abdisk.img)
ush> 
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
[update_local_test] offline package flow
[A/B disk present] FAIL
ush> 
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
[update_rollback_test] boot flags + rollback
[A/B disk present] FAIL
ush> 
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
[update_verify_test] SHA256 verification
[package file present] FAIL
[update_verify_test] run update_download_test first
ush> 
```
