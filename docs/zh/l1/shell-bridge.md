<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：Shell 桥

模块代码如何成为 Shell 接口面。

## 从 L1 注册命令

```
l1_ext_shell_register_command_ex(name, fn, help, wp_tag)
```

`fn` 接收参数字符串，返回 0/非零。标签显示在 `help -w` 中。表格最多容纳 256 条命令（目前已注册 233 条）——当表格已满时，注册会被拒绝并给出明确错误，绝不会静默处理。

## 枚举（TUI 模式）

```
l1_ext_shell_list_commands_a_z()
l1_ext_shell_list_commands_by_wp()
```

TUI 命令桥（`tui/tui_cmd.c`）通过完全相同的方式枚举该表格，并渲染启动器——这是参考实现。任何 L1 模块都可以这样做。

## 内置命令与任务（l1_wp8cd.h）

`shell_register_builtin` / `shell_unregister_builtin`（自测路径使用这些）、`shell_run`（交互循环）、用于后台任务的 `job_create / job_list / job_control`，以及 `tool_register` 用于嵌入 ELF 载荷供 Shell `run`。

## 契约

- 名称必须是 ASCII 小写，允许使用点号或多词组合（`mkfs.fat32`、`health export` 是真实注册的例子）。
- 帮助信息为一行；参数错误时打印 `usage: ...`。
- 命令一旦发布绝不会被移除（[CONTRIBUTING.md](../root/CONTRIBUTING.md)）。
