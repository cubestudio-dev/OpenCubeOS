<!-- SPDX-License-Identifier: Apache-2.0 -->
# 调试

## 先读启动日志

每个子系统在启动阶段打印一行 `.... OK`。最后一行没有打印出来
的就是你的故障子系统。捕获它：

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none
```

## 客户机内工具

```
ush> dmesg            (kernel ring buffer)
ush> logcat           (live follow of the persistent log)
ush> logctl           (log levels + filters)
ush> health           (CPU/mem/tasks/IO + alarms)
ush> exc              (exception self-test - proves the trap path)
ush> crashlog         (past crash records on /etc)
ush> dumpinfo         (crash dump details)
```

## panic 路径

内核 panic 会把异常帧（寄存器 + 故障地址）转储到串口，并把一条
crashlog 写入 /etc。`crashtest` 触发一次故意的 panic - 在你
真正需要之前，先用它验证你的 /etc 卷与串口捕获接线正确。

## 宿主机侧的内核调试

- 调试符号：流水线以 `-g` 链接，然后剥离最终 ELF；
  `build/opencube_base.elf`（`strip` 之前、在 `make kernel`
  期间产出）是你的符号载体。
- 在 QEMU 中：`-d int,cpu_reset -D qemu.log` 记录中断与复位；
  `-s -S` 加 gdb（`target remote localhost:1234`）给你经典的
  远程调试循环。
- 可复现构建意味着复现两次的崩溃是*同一批字节* - 不会在多次
  尝试之间出现海森 bug。

## 宿主机单元测试

`make test` 针对真实的内核源码编译并运行 `tests/host_*.c` 下的
测试桩（加密向量、tar、gzip、string、FAT 结构、在宿主机编译器
上开 ASAN 的 mbi 解析）。在这里迭代回归比启动循环快得多。

## 已知的锋利边缘（来自审计记录）

- 4 GiB 以上的 xHCI DMA 会在探测和每次传输时被拒绝 - 如果一块
  xHCI 设备消失了，检查你的 QEMU 内存大小。
- ATA-DMA 对齐依赖堆的 32 字节块头；如果你改了 `mem_heap.c`，
  运行 `fstest` + 一次 FAT32 挂载测试。
- multiboot2 解析器会校验标签大小；测试里"手搓"的 mbi 必须遵守
  每个标签的最小值，否则解析会提前停止。
