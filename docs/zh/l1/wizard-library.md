<!-- SPDX-License-Identifier: Apache-2.0 -->
# 向导库（wizard_lib.h）

8 个合并应用背后的共享原语。仅头文件，位于
`ocos/interface/wizard_lib.h` - 每个应用编译自己的副本
（`_shared` 风格）；逐字直通路径从不触碰它（它们保留历史的
`l1svc_main()` 语义）。

## 交互契约

- **输出缓冲**：调用者持有一个 `WIZ_OUT_CAP`（32768）的
  **静态**缓冲 - BSS，不是栈（l1svc_main 注记：用户栈上 32 KiB
  是风险，BSS 每次执行都映射）。
- **行输入**：`SYS_READLINE` - 内核行编辑器、回显 + 退格、
  阻塞、仅可打印 ASCII（没有 Ctrl 字符：绝不在 READLINE 提示上
  承诺"Ctrl+C 退出"）。
- **单键选择**：`SYS_GETCH` 非阻塞轮询（ush 自旋等待模式）。
  数字 1..9 选择，`q`/`Q`/ESC 中止，越界数字被忽略。
- **字符串**：每一行人类可见文本都经 `tr_()` 渲染 - 整个向导
  实时跟随 `lang en|zh`。

## API

| 原语 | 作用 |
|-----------|--------------|
| `wiz_svc(cat, op, arg, out)` | 运行一个 L1SVC 操作并打印其文本；失败时打印 `tr_("wiz_svc_failed")`；返回原始 rc |
| `wiz_svc_quiet(cat, op, arg, out)` | 同样的调用，不打印 - 由调用者探测 `out` |
| `wiz_find(hay, needle)` | 子串探测（1/0）- 锚点测试 |
| `wiz_token_after(hay, key, dst, cap)` | 复制 `key` 之后的第一个词（`"IP: 10.0.2.15"` + `"IP:"` -> `"10.0.2.15"`）；键不存在时返回 NULL |
| `wiz_ask(prompt, buf, cap)` | 提示 + 一行 READLINE（总是 NUL 结尾；空输入 = ""） |
| `wiz_confirm(what)` | 一次性 y/N - 只有裸 `y`/`Y` 算作 yes |
| `wiz_pause()` | `tr_("wiz_pause")` + ENTER |
| `wiz_menu(title, items, n)` | 基于可 tr_() 翻译键的编号单键菜单（1..9）；返回 1 起始的选择，q/ESC 返回 0 |
| `wiz_banner(title)` / `wiz_step(n, total, what)` | 框架行 |
| `wiz_ok(msg)` / `wiz_fail(msg)` | `[OK]` / `[!!]` 判定行 |

## 锚点纪律

向导读的是服务自己的英文输出行 - 与脚本解析的相同行 - 绝不是
第二种"结构化"拼法：

```c
wiz_svc_quiet(L1SVC_CAT_OTA, L1SVC_OTA_UPDATE, "status", out);
if (wiz_find(out, "A/B disk: present")) { ... }
```

锚点是兼容性表面的一部分（见
[surface-map.md](surface-map.md)）：重命名它们会同时破坏向导、
E2E 套件和用户文档。

## 安全模型

- 破坏性操作（`mkfs.*`、`abdisk`、`install`、`rm`、电源动作）
  **绝不止一次按键**：先问目标名，让用户重新输入一遍，再一个
  裸 `y` - 三次刻意步骤。经典/直通门保持逐字（脚本保留其无
  提示行为）。
- 数字字段（卷、睡眠秒数、chmod 模式）在触达任何服务之前先做
  仅数字校验。
- 跨领域修复正是意义所在：`update` 向导在检查更新之前，先通过
  DHCP（net 服务）修复缺失的 IP，通过 lsblk + abdisk（block
  服务）修复缺失的 A/B 盘。

## 共享 i18n 键

每个大应用的资源都带：`wiz_title`、`wiz_select`、`wiz_quit`、
`wiz_pause`、`wiz_svc_failed`（+ 它自己的键）。缺失的键回退到
英文，再回退到键字符串 - 向导绝不会因翻译缺口崩溃。
