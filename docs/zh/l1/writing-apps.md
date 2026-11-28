<!-- SPDX-License-Identifier: Apache-2.0 -->
# 编写一个应用 - 从源码到 `run`

一次完整走查：我们先加一个 `sysdiag` 风格的操作包装，再把它扩
成一个小向导。这里的一切都是机械化的 - 没有需要手工注册的表。

## 第 1 步 - 文件夹

```
ocos/l1apps/hello2/
  main.c
  i18n/en.json
  i18n/zh.json
```

## 第 2 步 - 最小应用（一次逐字服务包装）

`main.c`：

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED); }
```

`i18n/en.json`：

```json
{
  "_hint": "scheduler stats, verbatim",
  "svc_call_failed": ": service call failed (rc=-1)\n"
}
```

`i18n/zh.json` 镜像每个键（中文文本，键相同）。

## 第 3 步 - 构建并运行

```
make userprogs kernel iso     # or: make all
make run-bios
ush> hello2                   (the scheduler stats appear)
ush> run hello2               (same thing, explicitly)
```

`tools/gen_userprogs.py` 发现了文件夹、编译了它，注册表长了一行
（`help -a` 显示 `hello2 [l1]`）。

## 第 4 步 - 给它一个 wp 标签

`tools/gen_userprogs.py` 的 `WP_TAGS` - 加上 `"hello2": "core",`
（按字母序）。标签驱动 `help -w` 分组。未知名字默认为 "l1"，
诚实但未分组。

## 第 5 步 - 做成向导

```c
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
#include "l1_wizard_api.h"

static char g_out[WIZ_OUT_CAP];

static int hello2_wizard(void) {
    static const char *const items[] = { "h2_sched", "h2_mem" };
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 2);
        if (c == 0) return 0;
        (void)wiz_svc(c == 1 ? L1SVC_CAT_DIAG : L1SVC_CAT_DIAG,
                      c == 1 ? L1SVC_DIAG_SCHED : L1SVC_DIAG_MEM,
                      0, g_out);
        wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return hello2_wizard();
    return l1svc_main(argc, argv, L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED);
}
```

再在两个 json 文件里加上 `wiz_*` 键。这就是完整的三门模式：
无参向导、经典逐字，以及（如果你想要显式形式）`direct` 前缀
跳过 - 从 `ocos/l1apps/disk-tool/main.c` 抄分发形状。

## 第 6 步 - 你的应用必须通过的门禁

- `make all` 带 `-Wall -Wextra -Werror`（独立环境编译、无栈
  保护、无 SSE）- 始终零警告。
- 两份 i18n 文件齐备且完整（否则构建失败）。
- 过期门禁：编辑文件夹下任何源码或 json 都会在下一次
  `make all` 时重新生成 `userprogs_data.h` - 永不提交过期的
  内嵌头。
- 接口冻结规则：只加程序，永不删除
  （[CONTRIBUTING.md](../root/CONTRIBUTING.md)）。
- 如果你改动了 L0 服务输出行，锚点
  （[surface-map.md](surface-map.md)）和 E2E 套件就是你变更的
  一部分。

## 第 7 步 - 像项目一样测试它

```
python3 tools/run_fix4_regression.py build/opencube.iso log.txt
```

门禁启动 ISO、运行命令清单、断言每命令模式与注册表下限
（>= 276）。大应用时代为向导流程增加了 `tools/` 下的 E2E 脚本
（见 [../dev/e2e-suites.md](../dev/e2e-suites.md)）。
