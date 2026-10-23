<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：OTA 与 A/B

## 两个头文件

- `kernel/ota/ota_ab.h`（21 个声明）：槽位发现、标志、
  槽位状态、安装到槽位、回滚 - 编号的 +7。
- `kernel/ota/ota_update.h`（12 个声明）：链式操作 - 检查、
  下载、验证（SHA256 + 签名）、安装、设置启动、
  获取状态、回滚。

## L1 镜像

`l1/l1_ext.h` 为模块代码镜像了链式操作：
`l1_ext_check_update(_async)`、`l1_ext_update_check_pkg`、
`l1_ext_update_download`、`l1_ext_update_verify`、
`l1_ext_update_install`、`l1_ext_update_rollback`、
`l1_ext_update_set_boot`、`l1_ext_update_get_status`。

## 从模块中使用

标准流程（`update` 命令所运行的）：

```
l1_ext_check_update(&info);            // manifest from /etc config
l1_ext_update_download(url, path);
l1_ext_update_verify(path, sha256);
l1_ext_update_install(path, "B");      // inactive slot
// next boot flips; l1_ext_update_rollback() flips back
```

`l1_ext_update_info_t.version[64]`/`changes[256]`
完全镜像了 `ota_update_info_t`（32 位到 64 位的扩展是一个真实 bug：
32 位截断了 44 字符的官方版本字符串）。

## 客户机内测试

`ab_partition_test`、`update_*_test` 测试套件、`checkupdate_test`。
