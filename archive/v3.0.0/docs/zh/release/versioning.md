<!-- SPDX-License-Identifier: Apache-2.0 -->
# 版本方案

## 方案

自 v1.0.0 公开发布起，版本号采用 **SemVer**：`1.0.0`、`2.0.0`、
`2.1.0`……在此之前，工程阶段使用工作包阶段名
（`WP-14-docs_release`、`WP-13-build_system`……）——按功能集划分的
发布列车的内部等价物。自 v2.0.0 起 tag 就是 `v<版本>`
（`git tag v2.1.0`），`WP-*` tag 只存在于历史。

此外每个构建携带：

- **构建号** = 构建时的提交计数（`git rev-list --count HEAD`）
  **加永久的 +21 对齐偏移**（Makefile 里的
  `OC_KERNEL_BUILD_OFFSET := 21`）。偏移源自 v1.0.0：已发布的构建号
  曾在一个 git 历史比公开仓 main 长 21 个提交的工程树上烙制
  （WP-14-docs_release 在公开计数 253 时发布了 build 274），而 OTA
  更新检查对构建号做**数值**比较（BUG-0347）——公开序列必须继续
  高于已发布的 274。一提交一构建的单调性由此保持：v1.0.0 tag 位于
  公开计数 257 -> build 278；v2.0.0 位于计数 281 -> build 302。
- **版本横幅** = Makefile 里冻结的 `OC_RELEASE_VERSION ?=` 字面量
  （显式 `OC_RELEASE_VERSION=` 覆盖仍然有效——E2E 更新测试就是这么
  跑的）——烙入内核横幅、uname、OTA 清单与 ISO。更早的构建从
  `git describe --tags` 推导；v1.0.0 起冻结为字面量，横幅不再依赖
  本地 git 状态。

## 数字如何对应

| 位置 | 示例 | 含义 |
|------|------|------|
| `uname -a` | `Open Cube OS 2.1.0 x86_64` | 运行中内核的版本 |
| update.json | `version: 2.1.0, build: <n>` | 已发布的清单 |
| Release tag | `v2.1.0` | 资产构建所用的 git tag |
| gh-pages | 下载页同样的两个数字 | 网站镜像 Release |

一个发布 = 一个 tag = 一个构建号 = 一个清单 = 一组跨
本地 / Release / gh-pages / 私密镜像一致的 SHA256。tag 必须指向
CI 绿的提交（2.1.0 发布门）。

## 兼容性承诺

- Shell 命令不经申报不删除；增加是常态。v1.0.0 的 233 条命令基线
  完整保留：每一条都在 `INTERFACES.md` Part 1c 兼容契约里有映射
  （v2 命令归属 251 = L0 99 + SVC 133 + 内建 19）。
- L1SVC 操作号永不重编；新操作只追加（2.1.0 为 17 类 / 158 操作）。
  退役的 v1 编号面的逐条归宿：`INTERFACES.md` Part 2c/2d。
- OTA 包只更新内核；/etc 卷内容向前携带。
- 此后按 SemVer：破坏性变更升 MAJOR，新命令或新 L1SVC 操作升
  MINOR，修复升 PATCH。

## 选择下一个版本

按上述 SemVer 规则；`build` = 发布日的提交计数 + 21——绝不手工
挑选。
