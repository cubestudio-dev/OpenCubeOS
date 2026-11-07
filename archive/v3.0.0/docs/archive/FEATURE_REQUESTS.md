<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Feature Requests

## WP-09: Version Check (版本检查)

### 功能

- **服务器端**: `GET /latest.json` 返回当前最新版本信息（版本号、SHA256、下载 URL、发布日期）
- **ISO 端**: `checkupdate` 命令 — 从配置的服务器 URL 获取 `/latest.json`，与当前版本比较，提示用户是否有新版本
- **配置文件**: `/etc/version-server` — 用户可修改服务器地址，不硬编码
- **L1 接口**: `version_server_set(url)` / `version_server_get()` — 允许 L1 扩展修改/读取服务器地址

### 服务器

- **域名**: `https://cubestudio-dev.github.io/OpenCubeOS/`
- **API**: `GET /latest.json`
- **响应格式**:
  ```json
  {
    "version": "WP-08",
    "iso_sha256": "...",
    "iso_url": "https://cubestudio-dev.github.io/OpenCubeOS/downloads/opencube.iso",
    "src_sha256": "...",
    "src_url": "https://cubestudio-dev.github.io/OpenCubeOS/downloads/opencube-src.zip",
    "release_date": "2026-09-26"
  }
  ```

### 客户端 (checkupdate 命令)

```
oc> checkupdate
Checking for updates...
Current version: WP-08
Latest version:  WP-08
You are up to date.

oc> checkupdate
Checking for updates...
Current version: WP-08
Latest version:  WP-09 Batch 1
Update available! Download from: https://cubestudio-dev.github.io/OpenCubeOS/downloads/opencube-wp09.iso
```

### 为什么 WP-09

- 需要 **HTTPS**（TLS 支持）— WP-09 做传输层安全
- 属于"网络应用"范畴 — 利用 WP-06 的 TCP/IP 栈 + WP-09 的 TLS
- 配置文件 `/etc/version-server` 需要文件系统写支持（WP-05/WP-07 已有）

### 实现计划

1. TLS 支持（WP-09 核心）
   - 实现 TLS 1.2 ClientHello/ServerKeyExchange/Finished
   - 或集成 mbedTLS/tinyTLS（需评估 Apache 2.0 兼容性）
2. `checkupdate` 命令
   - 读取 `/etc/version-server` 获取服务器 URL（默认 `https://cubestudio-dev.github.io/OpenCubeOS`）
   - TCP 连接 + TLS 握手
   - 发送 `GET /latest.json HTTP/1.1\r\nHost: ...\r\n\r\n`
   - 解析 JSON 响应
   - 与当前版本比较并输出
3. L1 接口
   - `version_server_set(const char *url)` — 写入 `/etc/version-server`
   - `version_server_get(char *buf, u64 size)` — 读取 `/etc/version-server`
4. 服务器端
   - 部署 `latest.json` 到 `https://cubestudio-dev.github.io/OpenCubeOS/`
   - 每次 release 更新 JSON
