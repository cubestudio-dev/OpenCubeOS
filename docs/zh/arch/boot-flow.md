<!-- SPDX-License-Identifier: Apache-2.0 -->
# 启动流程：逐指令详解

## 第 0 阶段 - 固件 + GRUB

ISO 映像包含两个启动镜像（传统 El Torito + EFI）。无论哪种方式，
GRUB 2.12 都会运行，显示槽位菜单（ISO / A / B - 标志位于目标磁盘上），
并使用 multiboot2 协议加载 `boot/opencube.elf`。GRUB 还将内核 ELF 本身作为模块附加，
命令行参数为 `self` - 这是安装器负载的模式。

## 第 1 阶段 - boot.S（32 位 -> 64 位）

`boot/boot.S` 是 32 位入口点：GRUB 在保护模式下调用它，
分页关闭，`eax=0x36d76289`，`ebx=mbi 指针`。它执行以下操作：

1. 设置它自己的 GDT。
2. 构建页表，使用 2 MiB 页面对前 4 GiB 进行恒等映射。
3. 启用 PAE、LME、PGE。
4. 跳转到 64 位长模式并调用 `kmain`。

## 第 2 阶段 - multiboot2 解析

`kernel/arch/x86_64/arch_multiboot2.c` 遍历标签列表。标签是打包类型
（所有标签 - 通用标签头、帧缓冲、内存映射、内存信息、字符串、模块、RSDP 载体）。
解析器对恶意输入进行了加固（BUG-0142 修复）：total_size 边界检查、每个标签的最小长度、
字符串 NUL 终止、拒绝截断标签 - 结构错误的 mbi 会优雅降级，
而不是读取超出结构范围的内容。

它提取以下信息：帧缓冲几何结构、物理内存映射、命令行、模块列表和 ACPI RSDP
（标签 14/15 - ACPI 驱动程序会在该地址重新验证签名）。

## 第 3 阶段 - kmain 点亮

`kernel/main.c` 按启动日志顺序执行：帧缓冲控制台 + 8x16 字体、
IDT/GDT/TSS（256 个门、IST1 恐慌栈、IST2 调试栈）、8259 重映射、
PIT 100 Hz、PS/2 键盘、COM1 RX 到键盘队列、行编辑器、`sti`，
以及在任何其他组件信任陷阱路径之前进行实时异常自测
（3/3：#DE #UD #PF 被捕获，内核存活）。

然后：PMM（基于 mmap 的位图）、VMM、堆；驱动域；
带有 ramfs 根的 VFS；网络；Shell 注册（233 条命令）；
崩溃恢复已激活；`oc>` 提示符。

## 两阶段链接

内核分两阶段链接（`Makefile`）：第一阶段链接除 GRUB 启动镜像外的所有内容；
然后 `tools/embed_grub.py` 生成 `kernel/generated/grub_boot_data.c`，
嵌入 512 字节的 boot.img 和由 `grub-mkimage` 构建的 core.img
（磁盘上 grub.cfg 所需的模块集 - biosdisk、part_msdos、fat、multiboot2、search、
serial console...）；第二阶段将其链接进去。这就是系统内的 `install`/`abdisk`/`grub-install` 命令
可以在没有外部文件的情况下写入可引导磁盘的原因：引导加载器位于内核内部。
