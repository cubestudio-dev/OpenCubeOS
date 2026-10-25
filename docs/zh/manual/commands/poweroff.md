<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- 由 tools/gen_api_docs.py --pages-zh 生成。数据源与英文页一致：
     OC_SHELL_CMD 注册点 + ush 的 SVC_CMDS/内建表 +
     处理函数源码中的 usage 字符串。命令帮助文本保留系统
     原文（英文）。 -->

# poweroff

`poweroff` - alias of shutdown

家：L0 恢复壳（`oc>`）—— 注册于 `core/main.c`（OC_SHELL_CMD）
双归属：同时是 ush 的 SVC_CMDS 行（`l1apps/ush.c`），两个 shell 都可调用。

领域：[内核核心命令](index.md#kernel-core)

完整命令索引：[commands](index.md) —— 或在运行中的系统内输入 `help -a` / `help -w`。
