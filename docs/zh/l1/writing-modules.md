<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：从零写一个模块

完整流程：源码 → 接口调用 → 加载 → 运行。

## 步骤 1 - 选择你的加载路径

| 路径 | 何时 | 如何 |
|------|------|-----|
| 编译进内核 | 你的模块随内核构建一同发布 | 编译进内核，从 kmain 或内置 Shell 调用你的初始化函数 |
| 共享对象 | 无需重建内核即可部署 | 构建 .so 文件，通过 ld.so 接口面加载 |
| 嵌入式工具 | 发送 ELF 载荷 | `tool_register(name, elf, size)` 然后 `run name` |

## 步骤 2 - 你所构建的接口面

对于一个带有 Shell 命令和部分功能的模块：

```
#include "l1_ext.h"          /* shell bridge, config, ... */
#include "l1_wp8cd.h"        /* shell_register_builtin, jobs */

static int cmd_mymod(const char *args) {
    /* parse args, do the work, print through the console hook */
    return 0;
}

void mymod_init(void) {
    l1_ext_shell_register_command_ex(
        "mymod", cmd_mymod, "my module - does the thing", "L1-demo");
}
```

对于 UI：添加 `tui_menu_register` / `tui_page_register`。对于存储：
`driver_block_register`。参见各接口面页面。

## 步骤 3 - 构建

- 编译进内核：你的 .c 文件放入对应的目录树；Makefile 通过通配符自动拾取；从 `mymod_init` 注册。
- 共享对象：针对接口面头文件构建 ELF .so，然后在客户机内（或从你的加载器中）执行 `so_load(name, flags)` +
  `symbol_resolve`/`dlsym_impl` 获取初始化符号 - 完整机制参见
  [solib-loader.md](solib-loader.md)。

## 步骤 4 - 测试

- `run <yourprog>` / 调用你的命令；检查 `help -w` 是否显示你的标签。
- 完整的检查条目来自 [CONTRIBUTING.md](../root/CONTRIBUTING.md)：构建保持 0 警告，回归测试仍通过 28/28，
  命令计数基线正好增加你的命令数。
- `l1test` 和 `l1_ext_self_test()` 测试接口面本身。

## 规则须知

- 系统输出的所有内容仅使用 ASCII 字符串。
- 有界表格需命名限制常量 + 明确拒绝越界。
- 文档：你的命令页面通过 `make api-docs` 自动生成；你所属领域的手册章节是 PR 的一部分。
