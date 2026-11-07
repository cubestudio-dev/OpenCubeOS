<!-- SPDX-License-Identifier: Apache-2.0 -->
# 启动模式：BIOS、UEFI 与 A/B 槽位菜单

## ISO 是混合启动镜像

`build/opencube.iso`（发布资源）包含两条启动路径：

- **Legacy BIOS**：一个 El Torito 启动镜像（GRUB i386-pc，由
  `tools/build_iso.sh` 通过 `grub-mkimage` + `xorriso` 构建）。
- **UEFI**：同一个 ISO 上的一个 EFI 启动镜像（`x86_64-efi`）。

两条路径都加载相同的内核 ELF，并将内核自身作为标记为 `self` 的 multiboot2 模块附加 - 这是主流的安装程序模式：`install` 和 `abdisk` 命令将*当前正在运行的精确内核*复制到目标磁盘，因此已安装的系统始终与其安装程序匹配。

## GRUB 菜单

```
* Open Cube OS (slot ISO)
  Open Cube OS (slot A, force)
  Open Cube OS (slot B, force)
```

- **slot ISO** - 默认选项。从启动介质引导内核，并读取 A/B 启动标志以决定要链接哪个磁盘槽位（当标志不存在时回退到 ISO 内核）。
- **slot A / slot B 强制** - 忽略标志并直接引导该磁盘槽位。这是当糟糕的更新导致活动槽位无法启动时的恢复路径：从菜单中选择另一个槽位。

菜单在 5 秒后自动引导高亮条目。

## GRUB 之后内核的操作

启动顺序（这确实是串行日志）：

```
multiboot2 handshake OK
long mode entry OK
framebuffer 800x600x32 OK
console grid / 8x16 font engine OK
IDT + GDT + TSS (256 gates) OK
8259 PIC remap (IRQ0-15 -> vec 32-47) OK
PIT @ 100 Hz + tick counter OK
PS/2 keyboard OK
COM1 serial RX OK
console input line editor OK
interrupts enabled (sti) OK
exception self-test: 3/3 (#DE/#UD/#PF) OK
PMM / VMM / heap OK
... drivers, VFS, network ...
shell command system (233 commands) OK
boot complete OK
crash recovery armed (panic + dump + crashlog) OK
```

multiboot2 解析器（`kernel/arch/x86_64/arch_multiboot2.c`）读取 GRUB 从固件转发的帧缓冲标签、内存映射、命令行和 ACPI RSDP 标签（类型 14/15）- 在 UEFI 启动中 RSDP 通过此路径传递，在 BIOS 启动中 ACPI 驱动也可以扫描内存。

## 启动参数

GRUB 传递内核命令行；`grub.cfg` 使用它来传递 `BOOT_SLOT=ISO|A|B` 加上控制台偏好。在运行中的系统内部，`uname -a` 显示版本和构建信息，`abcfg` 打印 `abdisk`/`install` 生成的确切 grub.cfg 文件。

## 持久化：/etc 卷

系统查找要挂载到 `/etc` 的 FAT32 卷。当你引导普通 ISO 时不存在这样的卷-配置是临时的。`install` 在目标磁盘上创建一个（它包含 `opencube.conf`、SSH 主机密钥、`kernel.log*`、崩溃转储）。如果你只想获得持久性而不进行安装，请手动创建该卷：

```
oc> abdisk <dev>       (A/B disk with /etc volume - destroys data!)
```

## 引导真实硬件

将 ISO 写入 USB 闪存盘（`dd if=opencube.iso of=/dev/sdX` 或你喜欢的工具）。要求很简单：x86_64 CPU，512 MiB RAM。根据你的喜好通过 BIOS 或 UEFI 固件引导-两个镜像都在该闪存盘上。
