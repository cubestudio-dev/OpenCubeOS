<!-- SPDX-License-Identifier: Apache-2.0 -->
# 编写服务（与驱动）

如何为 L0 内核添加后台服务：`OC_SERVICE_DECL` 自注册框架、
`.oc_svcs` 链接节、kmain 收集、分发路由 —— 以一个完整的
实操示例（最小 echo 服务）呈现，外加由
`drv_misc_placeholder.c` 证明的驱动侧 `OC_DRIVER_DECL`
故事。既有接口面的编号见
[interface-dictionary.md](interface-dictionary.md)；ring-3
应用侧见 [../l1/writing-apps.md](../l1/writing-apps.md)。

## 1. "服务"是什么

这里**没有服务进程**。服务是 `ocos/services/<category>/`
下的一个目录，它拥有 (a) 一组机制和 (b) 一个经唯一系统调用
（`SYS_L1SVC`）到达的可选 ring-3 接口面。服务是链接期注册的
内核模块；启动时孵化的仅有的进程是 ush（默认 L1 入口），
以及在受监督启动时的 OCIRE 日志代理。

## 2. 框架（原文照录）

来自 `ocos/services/svc_framework.h`：

```c
typedef void (*oc_service_init_fn_t)(void);
typedef int  (*oc_service_late_fn_t)(void);   /* 0 = probed something */
typedef int  (*oc_service_l1_fn_t)(u32 op, const char *arg, char *out, int cap);

typedef struct {
    const char         *category;   /* == services/<category> folder */
    const char         *desc;
    oc_service_init_fn_t init;      /* early bring-up, may be NULL */
    oc_service_late_fn_t late_init; /* probe pass, may be NULL */
    oc_service_l1_fn_t  l1_dispatch; /* SYS_L1SVC entry, may be NULL */
} oc_service_decl_t;

#define OC_SERVICE_DECL(cat, dsc, ini, late) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (oc_service_l1_fn_t)0 }

#define OC_SERVICE_DECL_L1(cat, dsc, ini, late, disp) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (disp) }
```

收集 + 分发（`ocos/services/svc_framework.c`）：

```c
int svc_framework_dispatch(const char *cat, u32 op,
                            const char *arg, char *out, int cap)
{
    if (!cat || !out || cap <= 0) return -1;
    for (int i = 0; i < s_nsvcs; i++) {
        const oc_service_decl_t *s = s_svcs[i];
        const char *a = s->category, *b = cat;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (!s->l1_dispatch) return -1;   /* kernel-only service */
            return s->l1_dispatch(op, arg, out, cap);
        }
    }
    return -1;
}
```

来自头文件的规则：

- category 字符串与 `ocos/services/<category>/` 目录名
  一一相等，只有一个已声明的例外：category `"ocire"`
  （OCIRE 日志代理契约的冻结 ABI 名）由 `services/ocembed/`
  实现；
- 启动顺序仍是 kmain 组合根的显式序列（手工策展的依赖
  链）；收集到的表（`s_svcs[64]`）只是身份/分发面；
- NAK 规则："一切皆可通过添加一个文件来扩展"。

## 3. 实操示例：最小 echo 服务

目标：一个新类别 `echo`，两个 op —— `hello`（op 1，
按名字问候）和 `shout`（op 2，把参数转为大写）。新 op
编号**只可追加**（既有编号是冻结的 ABI）。

### 3.1 op 编号（ABI 步骤）

向 `ocos/services/sysapi/interface/svc_sysapi_l1svc.h` 添加：

```c
#define L1SVC_CAT_ECHO      21   /* services/echo     */
#define L1SVC_ECHO_HELLO    1    /* hello [name]       */
#define L1SVC_ECHO_SHOUT    2    /* shout <text>       */
```

运行 `python3 tools/gen_l1svc_ops.py` —— 它把单行
`#define L1SVC_*` 块原样镜像到
`ocos/interface/l1_svc_ops.h`（ring-3 应用唯一可以 include
的头文件集；`L1SVC_CAT_NAME` 函数宏被跳过）。ush 侧的
常量表携带一份逐字副本；整个编号体系只追加，因此任何
既有消费者都不会移位。

### 3.2 服务实现

`ocos/services/echo/src/svc_echo_l1.c`：

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The echo service: the smallest OC_SERVICE_DECL_L1 example.
 * Mechanisms: none. Ring-3 surface: two buffer-render ops. */
#include "types.h"
#include "core_lib_string.h"
#include "svc_sysapi_l1svc.h"

static int echo_put(char *out, int cap, int off, const char *s)
{
    while (*s && off < cap - 1)
        out[off++] = *s++;
    out[off] = 0;                 /* ALWAYS terminate (WP06g2 bug) */
    return off;
}

static int echo_l1_dispatch(u32 op, const char *arg, char *out, int cap)
{
    int off = 0;
    switch (op) {
    case L1SVC_ECHO_HELLO:
        off = echo_put(out, cap, off, "hello, ");
        off = echo_put(out, cap, off, (arg && *arg) ? arg : "world");
        off = echo_put(out, cap, off, "\n");
        return 0;
    case L1SVC_ECHO_SHOUT: {
        if (!arg || !*arg) {
            echo_put(out, cap, 0, "usage: shout <text>\n");
            return 0;             /* failure text, successful call */
        }
        for (const char *p = arg; *p && off < cap - 1; p++)
            out[off++] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        out[off++] = '\n';
        out[off] = 0;
        return 0;
    }
    default:
        echo_put(out, cap, 0, "echo: unknown op\n");
        return -1;
    }
}

OC_SERVICE_DECL_L1("echo", "echo service (greeting + uppercase + ring-3 surface)",
                   0, 0, echo_l1_dispatch);
```

本示例所演示契约的要点：

- **缓冲区渲染风格**：分发器用自己的辅助函数写入 `out`
  并且总是以 NUL 结尾（WP06g2 现场捕获的 bug：不终止的
  `put()` 让 `strlen(kout)` 读到堆垃圾）。
- **失败文本 vs 路由失败**：坏参数把 `usage:` 打进 `out`
  并返回 0 —— 系统调用仍然成功（只有不可路由的 cat/op
  才返回 -1）。
- **直连 vs 捕获**：本服务直接渲染。如果你的 op 体是一个
  经 `screen_console_puts` 打印的旧命令体，就把它包进
  `core_kcap_run_fn(fn, arg, out, cap)`；如果它占有实时
  会话或停驻（sshd、TUI、`power sleep`），就直连路由
  `out[0] = 0; return fn(arg);` —— 以捕获方式运行实时
  会话会吞掉控制台输出（见
  [debug-handbook.md](debug-handbook.md) 4.3 的 kcap
  陷阱）。
- 长 op 可以休眠：`sys_l1svc` 把分发包在 sti/cli 里，
  因此 tick 驱动的等待是合法的。

### 3.3 注册类别名

sysapi 头文件中的 `L1SVC_CAT_NAME(n)` 把编号映射到目录
字符串；添加 `case 21: return "echo";`。该字符串必须与
目录名完全相等。

### 3.4 Makefile 集成

**没有每服务的 Makefile，也没有手工编辑的源文件清单**。
顶层 Makefile 的 `KERNEL_DIRS` 通配 `ocos/services/*/src`，
因此把 `ocos/services/echo/src/svc_echo_l1.c` 放进树里
就是全部集成；多 `-I` 纯基名 include 策略
（`services/*/interface` 在路径上）能找到
`svc_sysapi_l1svc.h`。启动顺序无需任何改动：没有谁从
kmain 调用你服务的 init（只有需要探测轮时才用
`init`/`late_init` 槽位 —— 见 `snd`/`nic`/`block`，
它们使用 `snd_svc_init` + `snd_svc_probe_all` 风格的
辅助函数）。

`make kernel` 之后，启动行 `registry: drv=N svc=N app=N`
会数到你的服务，`sys_l1svc(21, ...)` 会路由到它。

### 3.5 ring-3 包装应用

`ocos/l1apps/echo/main.c`：

```c
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_ECHO, L1SVC_ECHO_HELLO); }
```

`l1svc_main`（位于 `ocos/interface/l1_api_syscall.h`）把 argv 拼成
256 字节参数行，以静态 32 KiB BSS 缓冲区调用
`sys_l1svc(cat, op, arg, out, 32768)`，打印 `out` 加上
缺失的换行。`tools/gen_userprogs.py` 在下一次
`make userprogs` 时自动拾取该目录（一个新目录就是一条
新命令 —— 没有手工注册这回事）。随附至少含保留 `_hint`
对的 `i18n/{en,zh}.json`，否则构建失败（"没有翻译的程序
是交付违规"）。

## 4. 添加驱动（OC_DRIVER_DECL 侧）

来自 `ocos/drivers/drv_framework.h`：

```c
typedef struct {
    const char *family;
    const void *ops;        /* const drv_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }
```

最小步骤：

1. 创建 `ocos/drivers/<class>/drv_<class>_<name>.c`。
2. 填一个家族 ops 表（例如 `blk_drv_ops_t` —— 一个扫描
   并经 `blk_dev_add()` 添加设备的探测回调；或设备自身的
   `driver_block_ops_t` read/write/flush）。
3. 用一行 `OC_DRIVER_DECL("block", "mydrv", &g_probe, "description")` 声明身份。
4. 完成：`drv_framework_collect()`（kmain）遍历
   `.oc_drvs`（边界来自 linker.ld）；所属家族管理器
   （block/nic/snd/usb/wireless/sdio）在探测轮中经
   `drv_framework_at(i)` 遍历该表，并在家族字符串匹配时
   调用你的探测。

这就是"真的只需添加一个文件"的证据，逐字摘自
`ocos/drivers/misc/drv_misc_placeholder.c`：

```c
/* Minimal ops table: a marker constant.  Family managers match on
 * the family string ("block"/"nic"/"snd"/"usb"/"wireless"/"sdio");
 * "placeholder" owns no manager, so this descriptor is collected and
 * reported but never probed. */
static const int drv_placeholder_ops_marker = 0x504C4143; /* "PLAC" */

OC_DRIVER_DECL("placeholder", "placeholder",
               &drv_placeholder_ops_marker,
               "NAK registration-mechanism test driver (collected, never probed)");
```

该文件的头注陈述了验收项：添加一个驱动除了在
`ocos/drivers/` 里放一个 .c 文件 + 一行 OC_DRIVER_DECL
之外必须一无所求 —— 不改全局表、没有注册调用点、不动
核心文件。占位驱动在启动时被收集（启动注册行中的驱动
计数包含它），其余一切不变：没有探测运行、没有设备
出现。

## 5. 纪律清单

- **编号只追加** —— 永不重排、永不复用；cat 18 的 op
  相对 OCIRE 代理 ABI 是冻结的。
- **category 字符串 == 目录名**（"ocire" 例外是已声明
  且冻结的）。
- **输出即 ABI**：纯 ASCII 英文、稳定锚点；ring 3 不做
  任何格式化；向导会 grep 你的句子。
- **总是以 NUL 结尾** `out`；遵守 `cap` 纪律；大缓冲区在
  ring-3 侧用静态/BSS（32 KiB 上栈是 bug），内核侧用
  kmalloc（镜像被钉在 0x400000 以下 —— 把大缓冲区移出
  .bss/.data）。
- **被捕获的函数体不得休眠或占有实时会话** —— 改为直连
  路由。
- **零警告**：`-Wall -Wextra -Werror` 是项目 lint；每个
  文件都带 SPDX 头。
- **依赖方向**：kernel 不得 include `drivers/`/`services/`
  头文件（例外：kmain 组合根）；drivers 不得 include
  services；服务之间可以互相 include 对方的*机制*接口，
  绝不 include 对方的 `l1_dispatch`（由
  `tools/check_deps.py` 检查）。
- **测试钩子属于接口面**：L1 面就是测试面（见
  [debug-interfaces.md](debug-interfaces.md) 中约 93 个
  桩）；诚实的 SKIP 优于虚假的 PASS。
