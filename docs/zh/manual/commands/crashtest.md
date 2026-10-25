<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- 由 tools/gen_api_docs.py --pages-zh 生成。数据源与英文页一致：
     OC_SHELL_CMD 注册点 + ush 的 SVC_CMDS/内建表 +
     处理函数源码中的 usage 字符串。命令帮助文本保留系统
     原文（英文）。 -->

# crashtest

`crashtest` - trigger kernel crash tests (panic/exc/pf/assert/wd/dump/policy)

家：L0 恢复壳（`oc>`）—— 注册于 `recovery/shell_cmds_crash.c`（OC_SHELL_CMD）

领域：[崩溃恢复与转储](index.md#shell-crash)

完整命令索引：[commands](index.md) —— 或在运行中的系统内输入 `help -a` / `help -w`。
