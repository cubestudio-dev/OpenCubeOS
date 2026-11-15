<!-- SPDX-License-Identifier: Apache-2.0 -->
# Shell 基础

`ush` 是启动 shell——一个真正的 ring-3 程序（唯一的 `OC_L1_APP_DECL` 应用），作为整个系统的前端。

## 提示符

```
ush>
```

## 运行程序

输入注册表程序名（共有 284 个——`help -a` 会全部列出）。8 个大应用是预期的操作面；历史单命令也继续可用：

```
ush> hwinfo
ush> lsblk
ush> run hello &            (the '&' suffix runs in background)
```

`run <program>` 是显式形式——它会打印所启动的 pid 并等待（或加 `&` 转入后台）。

## 管道与重定向

```
ush> cat /etc/opencube.conf | grep url
ush> help -a > registry.txt
ush> echo hello >> notes.txt
ush> wc < notes.txt
```

管道（`|`）、截断重定向（`>`）、追加重定向（`>>`）与输入重定向（`<`）在所有注册表程序间通用。

## 内置命令与程序

只有进程状态类命令内置于 ush：`cd`、`pwd`、`exit`、`help`、`export`、`alias`、`unalias`、`history`、`lang`、`clear`。其余一切——包括 `ls`、`cat`、`echo`、`write`——都是经 fork 运行的注册表程序。这正是 `ls` 在管道中以真实进程出现的原因。

行编辑器：上/下方向键翻阅历史，Tab 补全程序名，Ctrl+C 取消当前行。

## 别名与环境变量

```
ush> alias dt disk-tool
ush> alias
ush> unalias dt
ush> export EDITOR=edit
```

## 脚本

用 `;` 串联多条命令：

```
ush> dhcp; ping 10.0.2.2; dns example.com
```

本版本没有基于文件的脚本解释器——"脚本"指的是粘贴进去的一串命令行，或你在构建期写入注册表的程序（见开发者文档）。

## 语言

```
ush> lang zh        (the whole UI - shell help + every wizard -
                     switches to Chinese live)
ush> lang           (shows the current language)
```

## 大应用契约（回顾）

```
ush> <app>                wizard (menus, single-key picks, q quits)
ush> <app> direct <op>    explicit verbatim passthrough
ush> <app> <op> [args]    classic dispatch (historical spelling)
```

完整契约与逐应用详解见 [mega-apps](mega-apps.md)。
