<!-- SPDX-License-Identifier: Apache-2.0 -->
# 贡献者可移植性注记

这棵树在本地以 GCC 14.x 构建，在 CI 上以更新的工具链构建。已有
三类差异咬过人 - 把它们记在心里：

## 1. 更新 GCC 上更严格的警告类

`-Waddress-of-packed-member`（自 GCC 9 起属于 `-Wall`，在更新
的版本中执行得更严）：把 packed 结构体指针转换成自然对齐的类型
在 `-Werror` 下是错误。multiboot2 的标签类型全部声明为
`__attribute__((packed))`，正是出于这个原因 - 包括 mmap 标签
（4x u32，布局不变）和经由 packed 标签类型而非裸 `u32*` 转换的
BASIC_MEMINFO 读取器。

规则：当你转换一个来自 packed 内存的指针时，目标也要是 packed
类型。

## 2. 更严格的汇编器（binutils/GAS）

`setcc`/`setc` 只接受 BYTE 寄存器。rdrand 包装曾把标志声明为
`u32` 并使用 `"=q"` 约束 - 旧 GAS 静默截断到字节子寄存器，新
GAS 拒绝该操作数大小。现在它是 `u8`。

规则：setcc 类标志用 `u8`，约束 `"=q"`。

## 3. 工具链布局假设

`OC_TOOLS` 可以指向一棵可重定位的树（`<root>/usr/{bin,lib}`）
或一个系统根（`/usr` 下直接有 `{bin,lib}`）。embed_grub 生成器
会探测两种布局 - 不要假设任何一种。

## 编译标志契约

内核标志固定在 Makefile 中：
`-ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g -std=gnu11`。新标志属于 Makefile（一处），不要散落在各个文件
里。

## 交叉构建

`make ARCH=arm64` / `ARCH=riscv` 使用 aarch64/riscv64 交叉
工具链和它们自己的链接规则（`--hash-style=sysv --build-id=none`；`-n` 与 PHDR 布局不兼容）。点亮用的 shell 是
真实的（PL011/NS16550A 除数、定时器计数、心跳）但有意保持
最小 - x86_64 才是功能完备的架构。
