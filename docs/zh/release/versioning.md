<!-- SPDX-License-Identifier: Apache-2.0 -->
# 版本管理

## 方案

自 v1.0.0 公开发布起，版本标识符是 **SemVer**：
`1.0.0`、`2.0.0`、`2.1.0`……在那之前，工程阶段使用工作包
阶段名（`WP-14-docs_release`、`WP-13-build_system`、
……）——相当于按特性集开行的内部发布列车。自 v2.0.0 起，
tag 是纯粹的 `v<version>`（`git tag v2.1.0`），`WP-*` tag 只
存在于历史中。

此外，每个构建携带：

- **构建号** = 构建时的提交计数（`git rev-list --count HEAD`）
  **加上永久的 +21 对齐偏移**（Makefile 中的
  `OC_KERNEL_BUILD_OFFSET := 21`）。该偏移始于 v1.0.0：已
  公布的构建号烘焙自一棵 git 历史比公开仓库 main 长 21 个
  提交的工程树（WP-14-docs_release 以公开计数 253 发布了
  构建 274），而 OTA 更新检查按**数值**比较构建号
  （BUG-0347）——所以公开序列必须接续在已发布的 274 之上。
  一提交一构建的单调性得以保持：v1.0.0 tag 位于公开计数
  257 -> 构建 278；v2.0.0 位于计数 281 -> 构建 302。
- **版本横幅** = Makefile 中冻结的 `OC_RELEASE_VERSION ?=`
  字面量（显式 `OC_RELEASE_VERSION=` 覆盖仍然优先——E2E
  更新测试正是靠它工作的）——烘焙进内核横幅、uname、OTA
  清单和 ISO。更早的构建从 `git describe --tags` 推导它；
  v1.0.0 把它冻结为字面量，横幅从此不再依赖本地 git 状态。

## 数字合在一起的意义

| 位置 | 示例 | 含义 |
|-------|---------|---------|
| `uname -a` | `Open Cube OS 2.1.0 x86_64` | 运行中内核的版本 |
| update.json | `version: 2.1.0, build: <n>` | 已发布的清单 |
| Release tag | `v2.1.0` | 资产构建所用的 git tag |
| gh-pages | 下载页上的同样两个数字 | 网站镜像 Release |

一次发布 = 一个 tag = 一个构建号 = 一份清单 = 一组
SHA256，横跨本地 / Release / gh-pages / 私有镜像。tag 必须
指向 CI 绿的提交（2.1.0 发布门禁）。

## 兼容性承诺

- Shell 命令绝不无声移除，删除必须在删除登记表中声明；新增
  才是常态。v1.0.0 的 233 条命令基线完整保留：每一条都映射
  在 `INTERFACES.md` Part 1c 兼容性契约中（251 个 v2 命令
  归属 = 99 L0 + 133 SVC + 19 内建）。
- L1SVC 操作号绝不重排；新操作只追加（2.1.0 时为
  17 个类别 / 158 个操作）。退役的 v1 编号面的下场逐条
  记录：`INTERFACES.md` Part 2c/2d。
- OTA 包只更新内核；/etc 卷内容向前延续。
- 从此遵循 SemVer：破坏性变更升 MAJOR，新命令或新
  L1SVC 操作升 MINOR，修复升 PATCH。

## 选择下一个版本号

按上面的 SemVer 规则；`build` = 发布日的提交计数 + 21——
从不手挑。

## 版本号纪律（交叉引用）

3.0.0 阶段的约束性规则见
[CONTRIBUTING.md](../root/CONTRIBUTING.md#版本号纪律300-阶段)：74 个工作包
走完之前版本主干固定为 `3.0.0-WP-<工作包名>`，`OC_RELEASE_VERSION` 是唯一
真源，版本变更属全仓对账并以 `git grep` 验证，绝不只是一处文件的修改。

## 提交与推送纪律（交叉引用）

[批量交付规则](../root/CONTRIBUTING.md#提交与推送纪律)：一次验证过的
批量 → 一次推送，同时覆盖公开仓与私密镜像仓，推送后线上验证 gh-pages。
