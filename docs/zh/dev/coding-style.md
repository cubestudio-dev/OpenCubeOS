<!-- SPDX-License-Identifier: Apache-2.0 -->
# 编码风格

这棵树是自洽的；与它保持一致。

## C

- C11（`-std=gnu11`），4 空格缩进，K&R 花括号，.c/.h 中没有
  制表符。
- 类型：内核的定宽集合（`u8/u16/u32/u64/s8/...`、`types.h`） -
  而非 uint8_t 拼法。
- 类型要紧处每行一个声明；能初始化就初始化。
- 每个文件以 SPDX + 版权注释块开篇，并带一段说明该文件为何
  存在的 Purpose 段落。未来的读者（以及文档生成器）依赖它。
- 函数注释：契约、参数、返回码 - 尤其是每一个负数返回值。
- 错误处理：检查、以清晰的消息报告并回退 - 绝不泄漏半初始化
  的设备注册。

## 汇编

- `.S` 文件（经预处理）：boot.S 与各架构蹦床是参考。在每个
  入口点注释 ABI。

## shell 命令

- 用 `shell_register_command_ex` + 工作包标签注册，让
  `help -w` 保持真实。
- 一行帮助，参数错误时打印 `usage:`，失败时非零退出。
- 仅 ASCII。

## 命名

- 文件：`kernel/<area>/core_<thing>.c`、`drivers/<bus>/driver_<hw>.c`、
  `shell/shell_cmds_<domain>.c`、`fs/fs_<fs>.c` - 树的布局就是
  分类法（文档生成器按它分组）。
- 符号：`<module>_<verb>_<object>`（如 `driver_block_flush`、
  `klog_write`、`ota_ab_install`）。静态辅助函数保持文件内
  私有。
- 带模块前缀的全大写常量（`OC_MB2_TAG_END`、
  `SHELL_MAX_COMMANDS`）。

## 限额是设计的一部分

当你新增一张有界表时，限额常量要带一条解释这个数字的注释
（见 shell.h 里的 `SHELL_MAX_COMMANDS` - 232->256 的历史记录在
定义处）。保持这个纪律：下一个调整它的人需要这个故事。

## 不要做的事

- 不留"以备后用"的死代码 - 删掉；git 记得。
- 不做静默截断 - 带消息拒绝（alias 值是榜样：要么完整存储要么
  拒绝）。
- 不写不带 BUG- 编号或工作包引用的 `// TODO`。
- 不引入新的第三方代码：这棵树是原创的、Apache 2.0，并保持
  如此。
