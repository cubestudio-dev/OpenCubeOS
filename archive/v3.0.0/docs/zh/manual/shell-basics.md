<!-- SPDX-License-Identifier: Apache-2.0 -->
# Shell 基础

`oc>` shell 是一个带有 Unix 风格的命令行 - 支持管道、重定向、后台作业、环境变量、别名和小型脚本语言。本文档介绍其实际功能集。

## 行编辑

- 历史记录：上下箭头键浏览历史命令。
- 行内编辑：`Ctrl+U` 删除整行。
- 输入源：PS/2 键盘和 COM1 串口输入共享同一队列 - 在 QEMU 中使用 `-serial mon:stdio` 可通过串口输入。

## 命令链与作业

```
oc> dhcp && ping 10.0.2.2      (run ping only if dhcp succeeded)
oc> cat a.txt ; cat b.txt      (sequence regardless)
oc> health || dmesg            (second only if first failed)
oc> ping 10.0.2.2 &            (background job)
oc> g6_test &                  (long test in the background)
```

## 管道与重定向

```
oc> ls | grep log              (pipe stdout of ls into grep)
oc> cat /mnt/f.txt > /tmp/g.txt     (write stdout to a file)
oc> cat /mnt/f.txt >> /tmp/g.txt    (append)
oc> wget 10.0.2.2 < in.txt     (stdin from a file)
```

## 通配符

`*` 和 `?` 在命令执行前由 shell 展开：

```
oc> ls /mnt/*.txt
oc> cat /mnt/report?.txt
```

## 环境变量

```
oc> export NAME=value          (set)
oc> echo $NAME                 (use - $VAR expands in the line)
oc> env                         (list all)
```

环境变量存储限制：32 个变量，名称最长 32 字节，值最长 256 字节（`shell/shell.h`）。

## 别名

```
oc> alias ll ls -l
oc> ll
oc> unalias ll
```

别名值可包含完整管道 - 最多存储 512 字节，超出则拒绝（不会静默截断）。共 32 个别名槽位。

## 当前工作目录

```
oc> cd /mnt
oc> pwd
```

## 脚本编程

脚本是包含多行命令的文本文件；使用 shell 的脚本运行器执行（具体命令名称请参考你的构建版本中的 `help -a`）。`editor` 命令提供一个访客内全屏文本编辑器，用于编写这些脚本。

## 命令帮助

```
oc> help            (short list, one line per command)
oc> help -a         (all 233 commands, alphabetical, with descriptions)
oc> help -w         (the same commands grouped by the work package
                     that introduced them)
```

完整的索引参考（每命令一页）请见 [commands/index.md](commands/index.md)。

## 约定

命令可跨版本新增，永不删除；已发布命令的名称和整体行为保持稳定（参见 [../CONTRIBUTING.md](../root/CONTRIBUTING.md)）。命令表容量为 256，目前已注册 233 个。
