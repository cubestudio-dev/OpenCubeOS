<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 扩展指南：总览

L0 不附带任何 L1。整个架构的意义在于 L0 暴露的扩展接口面，以便 L1 *可以* 存在：162 个编号的扩展接口，以及驱动/OTA/看门狗/健康/内核日志（klog）头文件家族，还有一个共享对象加载器，使模块能够在运行时加载。

## 各层架构，再次说明

```
L1  your module: .so loaded by the in-tree ld.so, or a built-in
    compiled against the surface headers
     |
    162 numbered interfaces + driver/OTA/watchdog/health/klog
    families (INTERFACES.md = the full generated tables)
     |
L0  Open Cube OS kernel + oc> shell (this repo)
```

## 扩展接口面的位置

| 头文件 | 涵盖内容 |
|--------|----------|
| `l1/l1_ext.h` | 帧缓冲信息、字体引擎、控制台钩子、配置读写、更新链、电源、Shell 桥、自检 |
| `l1/l1_wp8a.h` | 进程/文件描述符/信号/mmap/chdir - 类 Unix 的用户 API |
| `l1/l1_wp8b.h` | 动态加载器（elf_load_dynamic、so_load、dlsym_impl、reloc_apply...） |
| `l1/l1_wp8cd.h` | Shell 内建命令、工具嵌入、任务 |
| `tui/tui_ext.h` | 37 个 TUI 声明（窗口/菜单/表单/面板/文本） |
| `drivers/block/driver_block_blk.h` | 块设备注册 + I/O |
| `drivers/nic/driver_nic.h` | 网卡注册 + I/O |
| `drivers/snd/driver_snd.h` | 声卡注册 + 播放 |
| `kernel/ota/ota_ab.h`, `ota_update.h` | 槽位与更新链 |
| `kernel/core/core_watchdog.h`, `core_health.h` | 看门狗槽位与健康指标 |
| `kernel/lib/lib_klog.h` | 内核日志 |

完整的声明表（包含所有 278 个声明的文件、行号和签名）由 `make api-docs` 生成并输出到 [INTERFACES.md](../root/INTERFACES.md)。

## 关键计数

- **162 个编号的 L1 接口** - README 中的公式
  57+8+13+7+26+11+7+9+8+16，这是整个仓库的一致性数量。
- 13 个扩展接口头文件中共有 278 个可调用声明
  （包含内部辅助函数和未编号的文档化分组）。

## 模块加载方式

三种真实路径：

1. **编译进内核的 L1** - 你的模块编译进内核并直接调用扩展接口
   （TUI 命令桥的工作方式）。
2. **运行时的共享对象** - `libs/ld_so.c` 加载 ELF .so 文件，
   解析符号并应用重定位：
   [solib-loader.md](solib-loader.md)。
3. **嵌入式工具** - `tool_register` 嵌入 ELF 负载，
   Shell 可以 `run` 它。

## 学习顺序

1. 本页面。
2. [surface-map.md](surface-map.md) - 按 162 个接口的功能分组。
3. 各系列页面：[driver-block.md](driver-block.md)、
   [driver-nic.md](driver-nic.md)、[driver-snd.md](driver-snd.md)、
   [ota.md](ota.md)、[watchdog-health.md](watchdog-health.md)、
   [shell-bridge.md](shell-bridge.md)。
4. [tui-ext.md](tui-ext.md) - 用 L1 构建用户界面。
5. [writing-modules.md](writing-modules.md) - 完整的模块，
   从源码到 `run`。
