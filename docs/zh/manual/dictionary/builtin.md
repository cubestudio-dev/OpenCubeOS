<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - ush 内建命令 (8)

### alias

| 字段 | 值 |
|---|---|
| 名称 | alias |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `alias` |
| 返回码 | 1 |

**实际执行示例**：

```
alias ll=ls
ush> alias
ll='ls'
ush> 
```

### cd

| 字段 | 值 |
|---|---|
| 名称 | cd |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `cd` |
| 返回码 | 1 |

**实际执行示例**：

```
cd /
ush> pwd
/
ush> 
```

### env

| 字段 | 值 |
|---|---|
| 名称 | env |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `env` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
env
PATH=/bin:/
HOME=/
PS1=ush> 
```

### exit

| 字段 | 值 |
|---|---|
| 名称 | exit |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `--all`, `--wp`, `-a`, `-w`, `exit`, `help` |
| 返回码 | 1 |

**实际执行示例**：

```
exit
ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart
ush> 
```

(备注: exit drops to the L0 recovery shell; 'run ush' returns)

### export

| 字段 | 值 |
|---|---|
| 名称 | export |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `export` |
| 返回码 | 1 |

**实际执行示例**：

```
exit
ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart
ush> run ush
started pid=10

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

ush> 
```

### history

| 字段 | 值 |
|---|---|
| 名称 | history |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `history` |
| 返回码 | 1 |

**实际执行示例**：

```
help_a_test
help_a_test: 'help -a' == default A-Z view, ordering verified: PASS
ush> 
```

### set

| 字段 | 值 |
|---|---|
| 名称 | set |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `set` |
| 返回码 | 1 |

**实际执行示例**：

```
set
ush> 
```

### unalias

| 字段 | 值 |
|---|---|
| 名称 | unalias |
| 分类 | ush 内建命令 |
| 功能 | ush built-in command |
| 用法 | `unalias name` |
| 返回码 | 1 |

**实际执行示例**：

```
alias ll=ls
ush> unalias ll
ush> alias
='ls'
ush> 
```
