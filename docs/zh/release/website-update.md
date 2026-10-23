<!-- SPDX-License-Identifier: Apache-2.0 -->
# 官网更新机制

`tools/update_website.sh` - 在资源文件存在后于发布时运行。它的功能（每一步都可验证）：

1. 从 `dist/` 读取三个资源文件，计算 SHA256 和大小。
2. 根据这些值重新生成网站的数据文件（`site.ts` 常量：RELEASE / ISO / SRC / OTA 名称 + 哈希 + 大小）- 无需在任何地方手动编辑版本号。
3. 根据 manifest 模板和发布说明重新生成 `update.json` + `update-v2.json`（版本、构建时间、变更文本、包 URL/SHA/大小）。
4. 运行哈希门禁：在推送任何内容之前，下载目录的文件必须与常量匹配。
5. 构建静态网站并推送 gh-pages。

运行后的验证：

- `https://cubestudio-dev.github.io/OpenCubeOS/update.json` 显示新版本/构建/哈希。
- 下载页面的三个哈希值与 Release 的三个哈希值相等。
- 对每个资源运行 `curl -sL <asset-url> | sha256sum`，结果等于其声明的 SHA256。

该脚本对每次发布都是幂等的：使用相同的 dist/ 重新运行会产生相同的页面（确定性模板）。
