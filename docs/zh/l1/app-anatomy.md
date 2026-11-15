<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用解剖 - main.c 由什么构成

每个注册表程序都是同样的五段形状。本页用树内真实示例走一遍
各段。

## 1. 文件夹

```
ocos/l1apps/<name>/
  main.c             app_main(int argc, char **argv)
  i18n/en.json       English strings + the "_hint" registry line
  i18n/zh.json       Chinese mirror (every key present)
```

文件夹名就是程序名。额外 `.c` 文件会一起编译链接（多文件应用
没问题）。平铺的旧式程序（`ocos/l1apps/*.c`，v2.x 测试家族）
保留它们的 v2.x 镜像模式；其 i18n 住在
`ocos/l1apps/_i18n/<name>/`。

## 2. 入口

```c
#include "l1_lib.h"
#include "l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_OTA, L1SVC_OTA_UPDATE); }
```

这就是合并前时代完整的 `update` 应用：crt0 蹦床（`_shared`
风格，由构建链接）把 argc/argv 交给 `app_main`；`l1svc_main()`
是逐字的服务包装体 - 把 argv[1..] 用单个空格连接成一条 256 字节
参数行，调用 `sys_l1svc(cat, op, arg, out, 32768)`，打印
`out`，失败时打印 `<name>` + `tr_("svc_call_failed")`。

## 3. i18n 资源

```json
{
  "_hint": "check for OTA updates",
  "svc_call_failed": ": service call failed (rc=-1)\n"
}
```

- `_hint` 是保留键：注册表/帮助行（构建时由
  `tools/gen_userprogs.py` 消费）。
- 其余每个键都是运行时文本：应用说 `tr_("key")`，活动语言
  （`lang en|zh`，每次调用实时探测）选表，未命中回退 en ->
  键本身。一个未翻译的应用永远不会崩溃 - 它把键显示出来，
  显而易见。
- 各表按键排序生成到
  `build/userprogs_i18n/<name>_i18n.c`；`tr_()` 对其做二分
  查找。
- 缺 en.json 或 zh.json 时构建失败。

## 4. 不止于包装

当一个应用需要交互时，它有两个输入原语：

- `syscall2(SYS_READLINE, buf, cap)` - 带回显 + 退格的阻塞行
  输入（最长 255，返回长度，NUL 结尾）；
- `sys_getch()` - 非阻塞单原始键（无键时 -1）- 菜单的 ush
  自旋等待模式。

还有一个启动器模式（fork + execve + wait4）- 防御性写法见
`ocos/l1apps/run/main.c` 和 `files` 大应用（静态 argv 存储、
显式的子进程退出）。

## 5. 向导层

```c
#include "wizard_lib.h"

static char g_out[WIZ_OUT_CAP];      /* 32 KiB, BSS not stack */

int app_main(int argc, char **argv)
{
    if (argc < 2) return my_wizard();          /* the intent door */
    ...
    return l1svc_main(argc, argv, CAT, OP);    /* the classic door */
}
```

`wizard_lib.h`（仅头文件，每个应用编译自己的副本）提供：
`wiz_svc` / `wiz_svc_quiet`（带 32 KiB 静态缓冲的 L1SVC）、
`wiz_find`（锚点探测）、`wiz_token_after`（某键后的第一个词 -
IP 提取器）、`wiz_ask` / `wiz_confirm` / `wiz_pause`（基于
READLINE）、`wiz_menu`（单键 1..9 菜单，q/ESC 退出）、
`wiz_banner` / `wiz_step` / `wiz_ok` / `wiz_fail`。完整契约：
[wizard-library.md](wizard-library.md)。

## 6. 注册

无需做任何事：`make userprogs`（属于 `make all`）发现文件夹、
编译它、生成 blob 与 `OC_USERPROGS[]` 行（名字、wp 标签、提示、
镜像），内核的 `core_userprog_lookup()` 按名字服务 execve。在
`tools/gen_userprogs.py` 的 `WP_TAGS` 里加上 wp 标签（未知名字
默认为 "l1"）。
