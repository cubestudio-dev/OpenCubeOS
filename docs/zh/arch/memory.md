<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内存管理

## PMM - 物理页存储

`kernel/mem/mem_pmm.{c,h}`：基于 multiboot2 内存映射构建的 4 KiB 页框位图。保留区域（内核镜像、帧缓冲、MBI 本身）在初始化时被标记为已使用。O(1) 分配和释放。`memtest` / `pmmrace` 在客户机内对其进行测试。

## VMM - 四级分页

`kernel/mem/mem_vmm.{c,h}`：内核保留 boot.S 对低 4 GiB 的恒等映射；VMM 为用户进程创建和编辑额外的地址空间。保护标志映射到读/写/nx/用户位。`vmtest`、`vmmap`、`vmkernelpt`、`cr3test` 实时检查它。

## 堆 - kmalloc/kfree

`kernel/mem/mem_heap.{c,h}`：空闲链表、首次适配、合并；块按 16 字节对齐（头文件恰好 32 字节 - 一个历史性的交替奇偶校验错误导致负载对齐依赖于分配奇偶性，这进而移动了 ATA-DMA PRDT 窗口并破坏了 FAT32 挂载；头文件大小现在是静态断言的）。16 MiB 快速失败上限；`heaptest` + `heapbounds` 是测试，`heap`/`frag` 是检查工具。

## 通过异常实现增长

栈增长和堆增长 #PF 在异常路径中处理：任务栈指针下方且在其栈区域内的故障会映射一个新页面；堆增长会映射支持页面；其他任何情况都会升级到崩溃路径。`exc` 每次启动都证明陷阱机制。

## DMA 约束

将物理地址编程到设备结构中的驱动程序验证对齐，并且对于 32 位 DMA 掩码，验证地址低于 4 GiB（xHCI 在探测和每次传输时都拒绝高于 4GiB 的 DMA；ATA-DMA 掩蔽 PRDT）。这就是为什么堆的对齐保证是承重的。
