<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：崩溃转储

`kernel/core/core_crash.c` + `arch_exceptions.c` 中的异常处理路径。

## 恐慌序列

1. 异常存根构建帧（向量、RIP、寄存器，以及相关的 CR2/CR3）。
2. 转储器将其打印到串口 - 即报告所需的确切行。
3. 崩溃日志记录写入 /etc - 可在重启后保留。
4. 机器停止运行（或者如果已启用硬件看门狗，则看门狗会重置它 - 参见 [watchdog.md](watchdog.md)）。

## 重启后

```
oc> crashlog      (the record list)
oc> dumpinfo      (the decoded dump: vector, fault address, registers)
```

## IST 为何在此处重要

#DF（双重错误）和 #MC 在 IST1 上运行 - 使用它们自己的栈 - 因此在故障处理程序期间发生的故障仍能到达转储器。异常退出路径绝不会在 IST2 上进行上下文切换（BUG-0139）：在 IST 上进行上下文切换会破坏下一个嵌套故障的异常帧。

## 测试方法

`crashtest` = 故意引发恐慌，然后 `dumpinfo` 证明往返过程。回归测试套件在每次运行时检查 `crashlog`。
