<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 文件与文本工具 (21)

### awk

| 字段 | 值 |
|---|---|
| 名称 | awk |
| 分类 | 文件与文本工具 |
| 功能 | 模式扫描与文本处理 |
| 用法 | `awk '{print $N\|NF\|NR}' [/pat/] [file]` |
| 返回码 | 0 |

**实际执行示例**：

```
awk
usage: awk '{print $N|NF|NR}' [/pat/] [file]
ush> 
```

### cat

| 字段 | 值 |
|---|---|
| 名称 | cat |
| 分类 | 文件与文本工具 |
| 功能 | 打印文件内容 |
| 返回码 | 0 |

**实际执行示例**：

```
cat
ush> 
```

(备注: reads standard input; closed with Ctrl+D)

### chmod

| 字段 | 值 |
|---|---|
| 名称 | chmod |
| 分类 | 文件与文本工具 |
| 功能 | 修改文件权限 |
| 用法 | `chmod <mode> <path>   (mode in octal, e.g. 644` |
| 参数 | `mode`, `path` |
| 返回码 | 0 |

**实际执行示例**：

```
chmod
usage: chmod <mode> <path>   (mode in octal, e.g. 644)
ush> 
```

### chown

| 字段 | 值 |
|---|---|
| 名称 | chown |
| 分类 | 文件与文本工具 |
| 功能 | 修改文件属主 |
| 用法 | `chown <uid>[:<gid>] <path>` |
| 参数 | `uid`, `gid`, `path` |
| 返回码 | 0 |

**实际执行示例**：

```
chown
usage: chown <uid>[:<gid>] <path>
ush> 
```

### cp

| 字段 | 值 |
|---|---|
| 名称 | cp |
| 分类 | 文件与文本工具 |
| 功能 | 复制文件 |
| 用法 | `cp <src> <dst>` |
| 参数 | `src`, `dst` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
cp
usage: cp <src> <dst>
ush> 
```

### df

| 字段 | 值 |
|---|---|
| 名称 | df |
| 分类 | 文件与文本工具 |
| 功能 | 报告文件系统剩余空间 |
| 返回码 | 0 |

**实际执行示例**：

```
df
Filesystem     Mount     Size     Used    Avail  Use%
ramfs          /         0        5 files (kernel df for full mount table)
ush> 
```

### du

| 字段 | 值 |
|---|---|
| 名称 | du |
| 分类 | 文件与文本工具 |
| 功能 | 报告文件占用空间 |
| 返回码 | 0 |

**实际执行示例**：

```
du
3712 bytes  3 files  /
ush> 
```

### grep

| 字段 | 值 |
|---|---|
| 名称 | grep |
| 分类 | 文件与文本工具 |
| 功能 | 在文件中搜索文本 |
| 用法 | `grep <pattern>` |
| 参数 | `pattern` |
| 返回码 | 0 |

**实际执行示例**：

```
grep
usage: grep <pattern>
ush> 
```

### head

| 字段 | 值 |
|---|---|
| 名称 | head |
| 分类 | 文件与文本工具 |
| 功能 | 显示文件开头几行 |
| 返回码 | 0 |

**实际执行示例**：

```
head
ush> 
```

### ln

| 字段 | 值 |
|---|---|
| 名称 | ln |
| 分类 | 文件与文本工具 |
| 功能 | 创建链接 |
| 用法 | `ln <old> <new> \| ln -s <abs-target> <linkpath>` |
| 参数 | `old`, `new`, `linkpath` |
| 返回码 | 0 |

**实际执行示例**：

```
ln
usage: ln <old> <new> | ln -s <abs-target> <linkpath>
ush> 
```

### ls

| 字段 | 值 |
|---|---|
| 名称 | ls |
| 分类 | 文件与文本工具 |
| 功能 | 列出目录内容 |
| 返回码 | 0 |

**实际执行示例**：

```
ls
d var
d bin
d dev
d tmp
d etc
ush> 
```

### mkdir

| 字段 | 值 |
|---|---|
| 名称 | mkdir |
| 分类 | 文件与文本工具 |
| 功能 | 创建目录 |
| 用法 | `mkdir <path>` |
| 参数 | `path` |
| 返回码 | 0 |

**实际执行示例**：

```
mkdir
usage: mkdir <path>
ush> 
```

### mv

| 字段 | 值 |
|---|---|
| 名称 | mv |
| 分类 | 文件与文本工具 |
| 功能 | 移动/重命名文件 |
| 用法 | `mv <src> <dst>` |
| 参数 | `src`, `dst` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
mv
usage: mv <src> <dst>
ush> 
```

### rm

| 字段 | 值 |
|---|---|
| 名称 | rm |
| 分类 | 文件与文本工具 |
| 功能 | 删除文件 |
| 用法 | `rm <file>` |
| 参数 | `file` |
| 返回码 | 0 |

**实际执行示例**：

```
rm
usage: rm <file>
ush> 
```

### rmdir

| 字段 | 值 |
|---|---|
| 名称 | rmdir |
| 分类 | 文件与文本工具 |
| 功能 | 删除空目录 |
| 用法 | `rmdir <path>` |
| 参数 | `path` |
| 返回码 | 0 |

**实际执行示例**：

```
rmdir
usage: rmdir <path>
ush> 
```

### sort

| 字段 | 值 |
|---|---|
| 名称 | sort |
| 分类 | 文件与文本工具 |
| 功能 | 排序行 |
| 返回码 | 0 |

**实际执行示例**：

```
sort
ush> 
```

### stat

| 字段 | 值 |
|---|---|
| 名称 | stat |
| 分类 | 文件与文本工具 |
| 功能 | 显示文件元数据 |
| 用法 | `stat <path>` |
| 参数 | `path` |
| 返回码 | 0 |

**实际执行示例**：

```
stat
usage: stat <path>
ush> 
```

### tail

| 字段 | 值 |
|---|---|
| 名称 | tail |
| 分类 | 文件与文本工具 |
| 功能 | 显示文件末尾几行 |
| 返回码 | 0 |

**实际执行示例**：

```
tail
ush> 
```

### touch

| 字段 | 值 |
|---|---|
| 名称 | touch |
| 分类 | 文件与文本工具 |
| 功能 | 创建空文件/更新时间戳 |
| 用法 | `touch <file>` |
| 参数 | `file` |
| 返回码 | 0 |

**实际执行示例**：

```
touch
usage: touch <file>
ush> 
```

### uniq

| 字段 | 值 |
|---|---|
| 名称 | uniq |
| 分类 | 文件与文本工具 |
| 功能 | 去除相邻重复行 |
| 返回码 | 0 |

**实际执行示例**：

```
uniq
ush> 
```

### wc

| 字段 | 值 |
|---|---|
| 名称 | wc |
| 分类 | 文件与文本工具 |
| 功能 | 统计行数/单词/字节 |
| 返回码 | 0 |

**实际执行示例**：

```
wc
0 0 0
ush> 
```
