<!-- SPDX-License-Identifier: Apache-2.0 -->
# ush — 完整 shell 参考

`ush` 是 ring-3 shell 与默认启动项（内核的
`l1_launcher_boot_default()` 孵化它；它退出时 L0 停机）。它位于
`ocos/l1apps/ush/main.c` — 一个裸 ELF（唯一非 gzipped 的注册表
程序），输入用 SYS_READLINE(73)/SYS_GETCH(74)，程序用
fork+execve+wait4，管道用 SYS_PIPE(20)+SYS_DUP2(51)，
重定向用 SYS_OPEN/CLOSE。

## 12 个内建命令

只有这十二个词由 shell 自身解释。其余一切都是注册表程序。

| 内建 | 语法 | 行为 |
|----------|--------|----------|
| `cd` | `cd [dir]` | 切换 shell 的工作目录；无参数 = `/` |
| `help` | `help [-a\|-w]` | 内建摘要 + 注册表计数行；`-a`/`--all` = 完整 A-Z 列表，`-w`/`--wp` = 按工作包分组（mega 组最先） |
| `export` | `export VAR=val` | 设置环境变量（最多 32 个，名 ≤ 32，值 ≤ 128 字节） |
| `alias` | `alias name=val` / `alias name value...` | 设置别名（最多 16 个，名 ≤ 32，值 ≤ 64）；裸 `alias` 列出全部；`alias name` 打印单个 |
| `unalias` | `unalias name` | 删除别名；无参数调用时打印 usage 错误 |
| `env` | `env` | 打印全部环境变量 |
| `set` | `set [VAR]` | 裸 `set` 显示变量；`set VAR` 打印 `VAR=value` |
| `history` | `history` | 带编号历史，1..n |
| `jobs` | `jobs` | 列出后台作业表条目 |
| `fg` | `fg [job]` | **不可用** — 打印 `ush: job control requires external programs` |
| `bg` | `bg` | **不可用** — 相同消息 |
| `exit` | `exit` | **被拒绝** — `ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart` |

注：

- `fg`/`bg` 是被识别的词，但作业**恢复**未实现。
  `jobs` 列表与 `cmd &` 后台启动是真实的（16 个作业槽；
  父进程打印 `[n] <pid>`）。
- `exit` 被拒绝是因为无处可退 —— ush 就是启动项；
  它的退出即会话结束（内核打印 `l1 launcher: default L1 app exited; L0 halting`）。
- v2.x 中曾是内建的词（`ls`、`cat`、`wc`、
  `grep`、`head`、`tail`、`mkdir`、`rmdir`、`touch`、`rm`、`cp`、`mv`、
  `sort`、`uniq`、`ps`、`top`、`kill`、`date`、`uname`、`free`、`df`、
  `stat`、`du`、`ln`、`chmod`、`chown`、`sed`、`awk`、`ping`、`wget`、
  `netstat`、`ifconfig`、`nano`/`vi`/`edit`）现在是经
  execve 到达的普通应用。

## 行编辑器

所有编辑都发生在原始 SYS_GETCH 码上；行长上限 511 字节。

| 键 | 码 | 动作 |
|-----|------|--------|
| Up | 0x80 | 上一条历史（草稿保留 — 见下文） |
| Down | 0x81 | 下一条历史；越过最新条目后恢复你的草稿 |
| Left / Right | 0x82 / 0x83 | 按字形移动光标（UTF-8 感知：`u8_prev`/`u8_next`） |
| Home / Ctrl+A | 0x84 / 0x01 | 行首 |
| End / Ctrl+E | 0x85 / 0x05 | 行尾 |
| Delete | 0x89 | 删除光标处字形 |
| Backspace | 0x08 / 0x7F | 删除光标前字形 |
| Ctrl+U | 0x15 | 删除至行首 |
| Ctrl+K | 0x0B | 删除至行尾 |
| Ctrl+W | 0x17 | 删除前一个词 |
| Ctrl+C | 0x03 | `^C` — 取消该行 |
| Tab | 0x09 | 补全（见下文） |
| 0x100..0x1FF | — | 串口粘贴的 UTF-8 字节：COM1 桥的 +0x100 偏置被剥离，因此粘贴的 CJK 逐字节累积 |
| 可打印 ASCII | — | 在光标处插入 |

**历史**：16 条，每条 256 字节，连续重复去重，
满时 FIFO 滑动。`history` 打印带编号的列表。

## Tab 补全 — 由注册表驱动

候选集是**活动注册表** —— `help` 打印的确切文本，经
L1SVC `l1/apps` op 获取（跳过页脚 `--` 行，上限
290 条）—— 加上 12 个真正的内建。

- 单一匹配：补全该词并追加一个空格。
- 多重匹配：补全公共前缀。
- 无命令匹配且该词包含 `/`（或是行中靠后的任意词）：
  **VFS 文件名补全**启动 —— 目录部分经 sys_readdir
  读取（扫描 64 条目，公共前缀逻辑，
  有界追加）。

补全从不枚举命令*参数* —— 子命令语法记录于
[function-dictionary.md](function-dictionary.md)。

## 管道、重定向、后台

| 特性 | 语法 | 限制 |
|---------|--------|--------|
| 管线 | `cmd1 \| cmd2 \| ...` | 最多 **8 段**；管道预先创建，每段一次 fork；第一个子进程保留控制台 stdin，最后一个保留控制台 stdout |
| 重定向出 | `cmd > file`（截断）、`cmd >> file`（追加） | 可在命令任意位置解析；原 fd 被 dup() 并恢复；内建命令也能看到重定向 |
| 重定向入 | `cmd < file` | 同一机制作用于 fd 0 |
| 后台 | `cmd &` | fork 不等待；作业表条目；打印 `[n] <pid>` |

空重定向目标是语法错误：`ush: syntax error near '>' `（以及 `near '<'`）。

## 变量替换 — 以及 ush 没有的东西

`$VAR` 展开是**唯一**的替换（`expand_env`，512 字节
结果；`echo $PATH`、`export X=...; run echo $X` 均可）。

该 shell 刻意**不**实现：

- **通配符/globbing** — `ls *` 把字面 `*` 传给 `ls`，而
  `ls` 同样没有 glob。任何地方都没有 `?`、没有 `[...]`。
- `&&` / `||` 链接
- `$?` 与任何其他特殊变量
- 引号（没有 `'...'` / `"..."`）
- heredoc
- 函数、算术、循环 —— 脚本化就是每行一条命令

命令未找到打印 `ush: command not found: <cmd>`；fork 失败
打印 `ush: fork failed`；别名展开溢出打印 `ush: alias expansion too long`。

---

## 编辑器

一个共享引擎（`ocos/interface/l1_editor_core.h`），三张面孔：
**nano** 与 **edit** 是逐字节相同的 nano 风格面孔（edit
是 v2.2.0 拼写）；**vi** 是同一引擎之上的真实模态层。
限制：256 行 × 每行 128 字节；超出限制的文件
部分加载（`unano_file_truncated` / `partial load`）且**保存
拒绝覆盖** —— 状态栏显示 `[PARTIAL: save refused]`。

### nano / edit 按键

| 键 | 动作 |
|-----|--------|
| 方向键 | 按字形移动 |
| 键入 | 插入 UTF-8 |
| Backspace / Delete | 删除整个字形 |
| ENTER | 拆分该行 |
| **F2** | 切换**系统 IME**（拼音；引擎位于 L0 显示服务 —— 编辑器只负责切换 + 送键 + 渲染预编辑条）。组词中：数字 1-9 选择候选，空格 = 第一个，PgUp/PgDn 或 `-`/`=` 翻页，Backspace 退一字，Enter 提交原始字母，Esc 取消 |
| Ctrl+K (0x0B) | 剪切行 |
| Ctrl+R (0x12) | 复制行 |
| Ctrl+U (0x15) | 在下方粘贴剪贴板行（UTF-8 安全，单行剪贴板） |
| Ctrl+C (0x03) | 报告光标位置 |
| Ctrl+O (0x0F) | 保存 |
| Ctrl+X (0x18) | 退出（有改动时询问） |
| Ctrl+G | 帮助 |

状态栏是差分式固定底行（ANSI
`\033[s\033[999;1H\033[K...\033[u`；未变化时零输出）：
`[* ]file 1/3 IME:on [preedit]`。

### vi 按键

COMMAND 模式（默认）：

| 键 | 动作 |
|-----|--------|
| `h j k l` / 方向键 | 移动（h/l 按字形） |
| `0` / `$` | 行首 / 行尾 |
| `x` | 删除光标处字形 |
| `dd` | 剪切行 |
| `yy` | 复制行 |
| `p` | 在下方粘贴 |
| `i` / `a` / `o` | 进入 INSERT（在光标处 / 其后 / 下方新行） |
| `:` | ex 行：`:w` 保存，`:q` 退出，`:wq` 两者，`:q!` 丢弃 |

INSERT 模式：键入即插入（UTF-8 字形感知），Backspace/Delete
删除整个字形，**F2 切换系统 IME**（与
nano 同一引擎），`Esc` 返回 COMMAND。状态栏：`[* ]file 1/3 CMD|INSERT`
+ 预编辑条。

---

## TUI 家族及其热键

`tui`（桌面）与七个页面应用（`tui-sys`、`tui-sysinfo`、
`tui-hw`、`tui-drv`、`tui-net`、`tui-time`、`tui-ext`）全屏
渲染并绕过文本捕获（它们的输出从不进入
L1SVC 捕获缓冲）。`tui-test` 经
捕获提交其判定。所有页面共享一个按键循环：

| 键 | 动作 |
|-----|--------|
| `Tab` | 下一页（标签栏显示 `[current]`） |
| `Up` / `Down` | 滚动页面（长页如 PCI caps 可读到首屏之后） |
| `+` / `=` | 音量 +10（Sound 页；将 `snd.volume` 持久化到 config） |
| `-` | 音量 -10 |
| `F1` | 帮助（共享的 sysinfo 帮助屏） |
| `F2` | 保存配置（`/etc/opencube.conf` 或默认值） |
| `F5` | 重绘 |
| `Esc` / `F10` / `q` | 离开页面，回到提示符 |

## 配方

```
ush> help -w                          # find commands by group
ush> top -n 5                         # five refreshes, then exits
ush> cat /etc/opencube.conf           # no pager; pipe into head
ush> dmesg 100 > /tmp/boot.txt        # capture the last 100 klog lines
ush> grep dhcp < /tmp/boot.txt        # grep filters stdin
ush> play 440 &                       # background job: [1] 12
ush> jobs                             # see it
ush> ime -i                           # interactive pinyin compose line
```

## 相关页面

- [function-dictionary.md](function-dictionary.md) — 全部 311 条命令
- [shell-basics.md](shell-basics.md) — 新手导览
- [getting-started.md](getting-started.md) — 首次启动
- [../troubleshooting/silent-failures.md](../troubleshooting/silent-failures.md) —
  为何某些控制台输出从不出现（捕获路由）
