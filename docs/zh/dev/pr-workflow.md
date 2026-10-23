<!-- SPDX-License-Identifier: Apache-2.0 -->
# 合入门槛（PR 规范）

PR 必须按顺序通过以下审核：

1. **干净构建**：在本地机器和 CI 上均通过 `make kernel` 且带上 `-Wall -Wextra -Werror` 参数（CI 的工具链通常更严格；multiboot2 打包转换与 rdrand setc 修复的存在，正是因为运行器上的更严格的 GCC/GAS 发现了本地构建所接受的问题）。
2. **宿主测试**：`make test` - 零失败。
3. **启动测试**：`make run-bios` 和 `make run-uefi` - 两者都应到达 `oc>` 提示符并显示 `boot complete ... OK`。
4. **针对你所修改内容的深度测试** - [CONTRIBUTING.md](../root/CONTRIBUTING.md) 中的矩阵（存储 -> fstest；加密 -> cryptotest + HTTPS E2E；看门狗/klog/健康监控 -> WP-12 E2E；启动路径 -> 28 项回归检查）。
5. **可复现性检查**：`make reproducible-check` 输出 PASS。
6. **文档同步**：新增命令 -> 生成其参考页（`make api-docs` 会刷新 `docs/manual/commands/` 和 `INTERFACES.md`）；新增接口 -> 更新接口面表格；行为变更 -> 更新对应的用户手册页面。
7. **CI 通过**：工作流在每次 PR 上都会运行构建、ISO 打包、宿主测试和可复现性门禁 - 仅在全部通过时才允许合并。

## PR 描述撰写

- 变更内容与原因 - 提交信息已包含详细说明。
- 你运行了哪些测试（命令 + 结果行，直接粘贴）。
- 你刻意未完成的事项（范围边界）。

## 审核预期

请准备好回答关于错误路径、DMA 边界以及硬件撒谎时会发生什么（截断的表、卡住的 NAK、返回垃圾数据的设备）的问题。项目自身的历史记录 - BUG-0136..0238 - 正是这些问题的目录；阅读其中几个修复提交是最好的准备方式。
