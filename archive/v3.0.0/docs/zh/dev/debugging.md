<!-- SPDX-License-Identifier: Apache-2.0 -->
# 调试

## 先读启动日志

每个子系统在点亮时都会打印一行 `.... OK`。最后一条没有打印出来的，就是你的故障子系统。捕获它：

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none
```

## 客户机内工具

```
oc> dmesg            (kernel ring buffer)
oc> logcat           (live follow of the persistent log)
oc> logctl           (log levels + filters)
oc> health           (CPU/mem/tasks/IO + alarms)
oc> exc              (exception self-test - proves the trap path)
oc> crashlog         (past crash records on /etc)
oc> dumpinfo         (crash dump details)
```

## panic 路径

内核 panic 会把异常现场（寄存器 + 故障地址）转储到串口，并向 /etc 写入一条崩溃日志。`crashtest` 会故意触发一次——用它来验证你的 /etc 卷与串口捕获在真正需要之前就已接好。

## 宿主侧调试内核

- 调试符号：流水线先带 `-g` 链接、再对最终 ELF 做 strip；`build/opencube_base.elf`（`strip` 之前、在 `make kernel` 过程中产出）就是你的符号载体。
- 在 QEMU 里：`-d int,cpu_reset -D qemu.log` 记录中断与复位；`-s -S` 配合 gdb（`target remote localhost:1234`）就是经典的远程调试回路。
- 可复现构建意味着同一崩溃复现两次就是*同一份字节*——不会在重试之间出现“海森堡构建”。

## 宿主单元测试

`make test` 会在宿主编译器上针对真实内核源码编译并运行 `tests/host_*.c` 下的测试架（加密向量、tar、gzip、字符串、FAT 结构、带 ASAN 的 mbi 解析）。在这里回归，比反复启动虚拟机迭代快得多。

## 已知的锋利边缘（来自审计记录）

- 高于 4 GiB 的 xHCI DMA 会在探测与每次传输时被拒绝——如果 xHCI 设备消失，先检查你的 QEMU 内存大小。
- ATA-DMA 对齐依赖堆的 32 字节块头；如果你动了 `mem_heap.c`，请运行 `fstest` 外加一次 FAT32 挂载测试。
- multiboot2 解析器会校验 tag 尺寸；测试里“手搓”的 mbi 必须遵守每个 tag 的最小值，否则解析会提前停止。
