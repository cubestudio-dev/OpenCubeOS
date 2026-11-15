<!-- SPDX-License-Identifier: Apache-2.0 -->
# ring-3 应用 - 总览

L0 是一个纯服务内核：它以启动 `ush`（ring-3 shell）完成启动，
而每条用户可见的命令本身就是一个 ring-3 程序。这套架构的全部
要点在于：L0 暴露的表面小、编号化且完备 - 这样应用才能存在。

## 分层

```
L1  ush (the boot shell) + 284 registry programs
    in ocos/l1apps/<name>/main.c - freestanding C against
    ocos/interface/*.h
     |
    SYS_L1SVC(cat, op, arg, out, cap)   20 categories, ~245 ops
    SYS_* (open/read/write/fork/execve/wait4/readline/getch/...)
     |
L0  Open Cube OS service kernel
    (ocos/kernel + ocos/services + ocos/drivers)
```

## 表面住在哪里

| 头文件（ocos/interface/） | 覆盖内容 |
|---------------------------|----------------|
| `l1_lib.h` | 系统调用包装、字符串/IO 辅助、`l1svc_main()` - 逐字的服务包装体 |
| `l1svc_ops.h` | 类别/操作编号契约（从 L0 头文件自动镜像） |
| `i18n.h` | `tr_()` + 每应用双语表 |
| `wizard_lib.h` | 共享向导原语（菜单、询问、确认、锚点探测） |
| `fs_lib.h` | 文件工具共享的路径辅助 |
| `editor_core.h` | `edit` 与 TUI 编辑器背后的编辑器引擎 |

服务侧声明住在 `ocos/services/*/interface/`（每个侧面一个
文件夹）；编号镜像用 `tools/gen_l1svc_ops.py` 重新生成。

## 关键数字

- **284 个注册表程序** - 276 个历史命令（接口冻结下限；命令
  只增不减）加上 8 个合并的大应用。
- **20 个 L1SVC 类别**（time/block/nic/snd/power/ota/fs/net/
  health/klog/watchdog/crypto/l1/display/input/camera/usb/ocire/
  diag/tui），约 245 个编号操作。
- 每个程序都附带 `i18n/{en,zh}.json` - 缺资源则构建失败
  （一个没有翻译的程序是交付违规，不是警告）。

## 一条命令如何运行

```
ush> disk-tool lsblk
  ush forks; child execve("disk-tool", ["disk-tool","lsblk"])
  app_main(argc, argv) sees argv[1] = "lsblk"
  -> classic dispatch: table lookup -> sys_l1svc(BLOCK, LSBLK, ...)
  -> service output printed verbatim
  ush wait4()s the child, prints the next prompt
```

历史单命令是同一机制再简单一层：`lsblk/main.c` 就是
`l1svc_main(argc, argv, CAT, OP)` - 与 v2.2.0 的 ush SVC runner
语义逐字节相同。

## 阅读顺序

1. 本页。
2. [surface-map.md](surface-map.md) - 按组分类的类别/操作。
3. [app-anatomy](app-anatomy.md) - main.c 由什么构成。
4. [writing-apps](writing-apps.md) - 一个完整应用，从源码到
   `run`。
5. 各家族页：驱动、OTA、加载器、TUI、看门狗/健康。
