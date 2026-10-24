<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 接口面：逐组解析

编号 162，映射到每个组声明所在的位置。
（随时可重新生成原始表格：`make api-docs` ->
INTERFACES.md - 每个声明都带有文件和行号。）

## 组 1-57 - 核心基础面

原始扩展接口面：帧缓冲访问
(`l1_ext_fb_get_info`)，字体引擎注册协议
(`l1_ext_register_font_engine` + `screen_font_engine` ops
结构体)，控制台钩子 (`l1_ext_set_console_hook` /
`l1_ext_console_hook`)，配置
(`l1_ext_config_read/write/get_all`)，更新链入口
(`l1_ext_check_update` + 异步)，电源（关机/挂起/停止/重启
- 挂起在实时 S3 恢复后返回 0），以及 Shell 桥
(`l1_ext_shell_register_command_ex`, list_commands_a_z /
by_wp)。全部在 `l1/l1_ext.h` 中。

## +8 - 块设备驱动项

`driver_block_register / read / write / flush` (+ set_ops)，
`driver_block_ahci_init`，`driver_block_nvme_init`，
`driver_block_ata_dma_init`（每个都接收已发现的 pci_dev）。
`drivers/block/driver_block_blk.h`。

## +13 - 网卡驱动项

`driver_nic_register / send / recv / link_status / get_mac` 加上
各系列初始化入口点（e1000e, igb, ixgbe, rtl8139, rtl8168,
rtl8125, rtl810x, bcm57xx）。`drivers/nic/driver_nic.h`。

## +7 - A/B 更新项

ota_ab 系列：槽位、标志、验证、安装。
`kernel/ota/ota_ab.h`。

## +26 - 声卡驱动项

`driver_snd_register / play / stop / set_rate / set_volume /
get_caps` + probe_all/make_tone + 各硬件初始化入口点（hda, ac97,
sb16, es1370, virtio）+ 查找/列表助手。
`drivers/snd/driver_snd.h`。

## +11 - 动态加载

`elf_load_dynamic`，`ldso_run`，`so_load/so_unload`，
`symbol_resolve`，`dlsym_impl/dlopen_impl/dlclose_impl`，
`reloc_apply`，`elf_get_needed`。`l1/l1_wp8b.h`。

## +7 - Shell 内建命令与作业

`shell_run`，`shell_register_builtin / unregister_builtin`，
`tool_register / tool_list`，`job_create / job_list / job_control`。
`l1/l1_wp8cd.h`。

## +9 - 进程/文件描述符/内存 API

`sys_proc_exec / sys_proc_exit`，`pipe_create`，`fd_dup / fd_dup2`，
`signal_send / signal_return`，`sys_mem_mmap / munmap / mprotect /
brk`，`sys_chdir` - 这是 Unix 形状的接口面，`userprogs/`
构建其上。`l1/l1_wp8a.h`。

## +8 - TUI 扩展项

`tui/tui_ext.h` 中的首批 TUI 声明。

## +16 - 看门狗/日志/健康监控

klog 6 (`klog_write / set_level / set_level_enabled / flush /
rotate / filter_set`，`kernel/lib/lib_klog.h` 第 72-106 行) +
看门狗 5 (`register / start / feed / stop / set_timeout`，
`kernel/core/core_watchdog.h` 第 74-83 行) + 健康 5 (`get_cpu /
get_mem / get_tasks / get_io / export`，
`kernel/core/core_health.h` 第 76-85 行)。

## 编号之外（已记录，真实存在）

- 配置/检查更新：5 个接口。
- rule-9 自托管：5 个接口（安装/abdisk/grub-install 背后的内核内 GRUB 有效载荷）。
- 编号 8 之外的完整 TUI 组件接口面（`tui/tui_ext.h` 中总共 37 个声明）。
