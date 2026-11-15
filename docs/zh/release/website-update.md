<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网站更新机制

公开站点是本仓库的 **gh-pages 分支**
（https://ocos.cubestudio.cn/，GitHub Pages）。门面是静态的：
`assets/site-data.js` 携带版本数据，`download.html` /
`index.html` 渲染它，`downloads/` 存放资产副本。

## 发布时的更新（每次发布）

1. 从 `dist/` 读取资产，计算 SHA256 + 大小。
2. **门面**：`assets/site-data.js`——更新 `dev` 频道块
   （版本 / 构建 / 日期 / 资产四元组：名称、url、sha、
   大小），并把这次发布插入 `history` 数组头部。
3. **页面**：`download.html` + `index.html`——就地替换先前的
   版本字符串（gh-pages 的 HTML 与归档的 release HTML
   存在结构差异——不要整文件覆盖，只替换字符串）。
4. **OTA 清单**：分支根的 `update.json`（版本、
   构建、时间、变更文本、包 URL/sha/大小）——系统内
   `checkupdate` 解析的就是它。
5. **downloads/**：把四个发布资产复制进来（小写
   `opencube-<ver>.*` 规范名）。
6. 两个提交（门面 + downloads）并推送分支。

## 运行后的验证

- `https://ocos.cubestudio.cn/update.json` 显示新的
  版本/构建/sha。
- 下载页的四个哈希与 Release 的四个相等。
- 每个资产上 `curl -sL <asset-url> | sha256sum` 的结果等于
  其声明的 SHA256。
- OTA E2E（`tools/run_fix4_regression.py` 的 update 节 +
  checkupdate 流程）在以较低构建号构建的机器上看到
  "New version available."。

## 数字从哪里来

`tools/update_website.sh <version> <build> [date-iso] [changes]`
从 `dist/` 计算 SHA256/大小集合并写入数据文件（任何地方都
不手改版本号）——GitHub Release 正文与私有镜像审计记录
携带的是同一组数字，因此网站 / Release / 镜像持有逐字节
一致的副本。
