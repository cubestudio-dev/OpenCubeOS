<!-- SPDX-License-Identifier: Apache-2.0 -->
# 拉取请求门槛

一个 PR 必须按顺序通过：

1. **干净构建**：`make kernel` 带 `-Wall -Wextra -Werror` - 在
   你的机器上*并且*在 CI 上（CI 的工具链常常更严格；multiboot2
   packed-cast 与 rdrand setc 修复的存在，恰恰是因为运行器上更
   严格的 GCC/GAS 抓住了本地构建接受的东西）。
2. **宿主机测试**：`make test` - 零失败。
3. **能启动**：`make run-bios` 和 `make run-uefi` - 两者都带着
   `boot complete ... OK` 到达 `ush>` 提示符。
4. **针对你所改动内容的深度测试** - [CONTRIBUTING.md](../root/CONTRIBUTING.md)
   中的矩阵（存储 -> fstest；加密 -> cryptotest + HTTPS E2E；
   看门狗/klog/健康 -> WP-12 E2E；启动路径 -> 28 项回归）。
5. **可复现**：`make reproducible-check` 打印 PASS。
6. **文档随同一个 PR**：新命令 -> 生成其参考页
   （`make api-docs` 刷新 `docs/manual/commands/` 和
   `INTERFACES.md`）；新接口 -> 表面表；行为变化 -> 对应的
   手册页。
7. **CI 绿**：工作流在每个 PR 上运行构建、ISO、打包、宿主机
   测试和可复现性门禁 - 只有全绿才合并。

## 推送与版本规则（交叉引用）

本门槛之上还有两条常设规则，正文在
[CONTRIBUTING.md](../root/CONTRIBUTING.md)：**提交与推送纪律**（一次
验证过的批量 → 一次推送，覆盖双仓，全量电池跑完后推送，推送后验证
gh-pages）与**版本号纪律**（3.0.0 阶段版本主干固定为
`3.0.0-WP-<工作包名>`，版本变更属全仓对账，绝不只是一处文件的修改）。

## 撰写 PR 描述

- 改了什么、*为什么* - 细节由提交信息承载。
- 你运行了哪些测试（命令 + 结果行，原样粘贴）。
- 你有意不做的事（范围边界）。

## 评审预期

预期会收到关于错误路径、DMA 边界，以及硬件撒谎时会发生什么
（被截断的表、卡死的 NAK、应答乱码的卡）的问题。这棵树自己的
历史 - BUG-0136..0238 - 正是这类问题的目录；读几个那样的修复
提交是最好的准备。
