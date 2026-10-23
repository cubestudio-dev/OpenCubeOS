<!-- SPDX-License-Identifier: Apache-2.0 -->
# 贡献者移植性须知

本地使用 GCC 14.x 构建，CI 上使用更新的工具链构建。
已有三类差异造成问题 - 请牢记：

## 1. 新版 GCC 更严格的警告类别

`-Waddress-of-packed-member`（自 GCC 9 起包含在 `-Wall` 中，新版执行更严格）：在 `-Werror` 下，将打包结构体指针转换为自然对齐类型是错误。multiboot2 标签类型正是因此原因全部声明为 `__attribute__((packed))` - 包括 mmap 标签（4 个 u32，无布局变化）和 BASIC_MEMINFO 读取器，它通过打包的标签类型而非裸 `u32*` 转换来访问。

规则：当转换来自打包内存的指针时，目标类型应为打包类型。

## 2. 更严格的汇编器（binutils/GAS）

`setcc`/`setc` 仅接受字节寄存器。rdrand 包装器将其标志声明为 `u32`，约束为 `"=q"` - 较旧 GAS 会静默截断到字节子寄存器，新版 GAS 拒绝操作数大小。现在是 `u8`。

规则：setcc 样式的标志是 `u8`，约束为 `"=q"`。

## 3. 工具链布局假设

`OC_TOOLS` 可能指向可重定位树（`<root>/usr/{bin,lib}`）或系统根（`/usr`，其下直接有 `{bin,lib}`）。embed_grub 生成器探测两种布局 - 不要假设其中任何一种。

## 编译标志约定

内核标志在 Makefile 中固定：
`-ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone
-mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel
-fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g
-std=gnu11`。新标志应放在 Makefile（一处）中，而不是分散在各文件中。

## 交叉构建

`make ARCH=arm64` / `ARCH=riscv` 使用 aarch64/riscv64 交叉工具链和它们自己的链接规则（`--hash-style=sysv
--build-id=none`；`-n` 和 PHDR 布局不兼容）。点亮 Shell 是真实的（PL011/NS16550A 除法器、定时器计数器、心跳），但故意保持最小 - x86_64 是功能完整的架构。
