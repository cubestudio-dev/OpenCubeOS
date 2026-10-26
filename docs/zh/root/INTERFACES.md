<!-- GENERATED mirror of the repository-root INTERFACES.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 接口参考（中文导读）

`INTERFACES.md` 是**由生成器维护**的逐条参考（`make api-docs` 从真实
代码树再生成：L0 命令直取 `OC_SHELL_CMD` 注册点、Ring-3 面取 ush 的
`SVC_CMDS` 表与 `services/l1/l1svc_defs.h`、内核侧声明取各服务头）。
逐条表格体量庞大且随每次再生成而变化，英文版
[INTERFACES.md](https://cubestudio-dev.github.io/OpenCubeOS/docs/root/INTERFACES.html)
是唯一权威文本；本页给出中文导读与关键口径。

## 关键口径（2.1.0）

- **v1.0.0 命令基线：233 条**——每一条都有新归属（Part 1c 对照表，
  233 行、零遗漏）。历史上 2.0.0 的表只映射了 231 条，漏掉
  `health export` 与 `health watch`——2.1.0 已补全。
- **2.1.0 命令归属：共 251 个唯一名** = L0 恢复 Shell（`oc>`）99 条
  + Ring-3 SYS_L1SVC 服务命令 133 条 + ush 内建 19 条。
- **SVC 计数口径**：ush 的 `SVC_CMDS` 表本身有 **142 行**；其中 9 条
  双归属名（rtc/hpet/pmtimer/tsc/shutdown/poweroff/reboot/halt/
  sleep）在 L0 与 ush 两侧都注册，按"先归 L0"规则计入 L0——两个
  Shell 都能调用它们。唯一名账目（99+133+19=251）不受影响。
- **L1SVC 服务面：17 个类别 / 158 个编号操作**（类别号选
  `services/<类别>/` 文件夹，操作号选命令；编号只增不改）。
- **系统调用：54 个**（`core/core_syscall.h`；SYS_L1SVC(110) 与
  SYS_NETCMD(101) 是 2.0.0 的 Ring-3 服务桥）。
- **内核侧声明**：各服务头共 144 条。
- **源代码行数**：130,572 行 / 379 个文件（README 验证命令口径：
  .c/.h/.S，排除生成的数据块）。

## 各部分导读

| 部分 | 内容 |
|------|------|
| Part 1a | L0 恢复 Shell（`oc>`）的 99 条命令：名称、一行帮助、类别 |
| Part 1b | Ring-3 服务命令（ush）133 个唯一名 + 19 个内建 + 双归属口径 |
| Part 1c | **v1.0.0 → 2.1.0 命令映射（233 行）**——兼容性契约 |
| Part 2 | L1SVC 服务面：类别表 + 各服务的编号操作表 + 内核侧声明 |
| Part 2c | **v1 → 2.1.0 接口面逐条对照（278 条）**：267 公开 / 6 内化 / 5 删除 |
| Part 2d | **删除申报清单**：5 个真删条目（init/probe 管道），各带替代者；register 类的精确表述 |
| Part 3 | 系统调用面（54）与 ABI |

## "register 类接口没了"的精确表述（2.1.0 更正）

- 三个驱动家族的 register 挂钩**没有删除**，只是从公开 v1 L1 头移入
  各家族内部头 `services/*/drv_internal.h`（"internal since 2.0.0"，
  Part 2c 中标记为"内化"的 6 条：block 家族 4 条 + nic/snd 各 1 条）。
- **仍然公开**的 register 类声明：`tui_*_register` 家族
  （`tui/tui_ext.h`）、`watchdog_register`
  （`services/watchdog/core_watchdog.h`）、`signal_register`
  （`services/l1/l1_wp8a.h`）、`shell_register_builtin`
  （`services/l1/l1_wp8cd.h`）、`l1_ext_shell_register_command_ex`
  （`services/l1/l1_ext.h`）。
- v1 接口面上的**真实删除只有 5 条**（Part 2d 全部申报）：block/nic/
  snd 的 `*_init` 与 nic/snd 的 `*_probe_all`——都是初始化/探测管道，
  功能在 v2 服务模型内重建（`*_svc_init` / `*_svc_probe_all`）。

英文版逐条表请见仓库根
[INTERFACES.md](https://github.com/cubestudio-dev/OpenCubeOS/blob/main/INTERFACES.md)
或文档中心英文页。
