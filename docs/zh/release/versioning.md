<!-- SPDX-License-Identifier: Apache-2.0 -->
# 版本号规则

## 版本方案

版本标识符是**工作包阶段名称**：
`WP-14-docs_release`、`WP-13-build_system`... 这是项目的 SemVer 等价物：阶段名称就是兼容性契约的体现（每个工作包都是一个拥有自身功能集和门禁的发布线）。

此外，每次构建还携带：

- **构建号** = 构建时的提交计数（`git rev-list --count HEAD`）- 单调递增，在 `uname -a` 和更新清单的 `build` 字段中可见。
- **版本横幅** = `git describe --tags`（或显式的 `OC_RELEASE_VERSION=` 覆盖）- 烘焙到内核横幅、uname、OTA 清单和 ISO 中。

## 各数字的含义

| 位置 | 示例 | 含义 |
|-------|---------|---------|
| `uname -a` | `Open Cube OS WP-13-build_system build 259` | 运行中内核的阶段 + 构建 |
| update.json | `version: WP-13-build_system, build: 259` | 已发布的清单 |
| 发布标签 | `WP-13-build_system` | 资源所构建自的 git 标签 |
| gh-pages | 下载页面中的相同两个数字 | 网站镜像了发布版本 |

一次发布 = 一个标签 = 一个构建号 = 一个清单 = 一组覆盖本地 / 发布版本 / gh-pages / 私有镜像的 SHA256。

## 兼容性承诺

- Shell 命令永不删除；增加是常态（基线上移：176 -> 192 -> ... -> 233）。
- L1 编号接口永不重新编号；新组追加。
- OTA 包仅更新内核；/etc 卷内容向前携带。

## 选择下一个版本

下一个工作包的阶段名称（在开发计划中确定），`build` = 发布当天的提交计数 - 绝不手动挑选。
