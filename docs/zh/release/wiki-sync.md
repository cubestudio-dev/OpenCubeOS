<!-- SPDX-License-Identifier: Apache-2.0 -->
# Wiki 同步机制

`tools/sync_wiki.sh`——docs/ 树同时活在 GitHub Wiki 上。

## 同步什么

Wiki 得到的是精选页面集（Home、Getting Started、Building、
Architecture Overview、Debugging & QEMU、L1 Extension Guide、
User Manual、FAQ、Release Notes，外加各领域手册），由同一批
`docs/` 源渲染——一个写作面，两个输出（网站上的文档中心、
GitHub 上的 Wiki）。

## 怎么做

1. 脚本把 `docs/` 页面映射到 Wiki 页面名（映射表在
   脚本里）。
2. 把相对链接改写为 Wiki 页面链接。
3. 克隆 `{repo}.wiki`，写入页面，提交，推送。

## 验证

- 每个被映射的 Wiki 页面都存在（脚本会打印清单）。
- 链接检查：映射集之内没有红链。
- 内容相等：Wiki 页面与文档页面渲染同一
   份 markdown 源（脚本推送前会对它们做 diff）。

## 何时运行

发布时（docs/ 变更落地之后），以及仅文档更新时按需
运行。
