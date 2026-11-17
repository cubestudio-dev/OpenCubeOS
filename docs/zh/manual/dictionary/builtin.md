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
ush> 
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
export DICTVAR=1
ush> env
PATH=/bin:/
HOME=/
PS1=ush> 
DICTVAR=1
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
history
1 echo DICHECK_87
2 hda
3 echo DICHECK_88
4 hda_test
5 echo DICHECK_89
6 head
7 echo DICHECK_95
8 help
9 echo DICHECK_96
10 help_a_test
11 echo DICHECK_97
12 help_default_test
13 echo DICHECK_98
14 help_wp_test
15 echo DICHECK_99
16 history
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
