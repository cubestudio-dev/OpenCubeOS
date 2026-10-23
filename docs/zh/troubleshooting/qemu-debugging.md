<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 调试技巧

## 捕获全部内容

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none \
    -netdev user,id=n1 -device e1000,netdev=n1
```

串行日志就是启动过程记录 - 每个子系统都会输出一个 `OK`。

## 观察宿主机，而不仅仅是虚拟机

```
-d int,cpu_reset,guest_errors -D qemu.log    (interrupts/resets/errors)
-monitor stdio                               (QEMU monitor alongside)
```

## 使用 GDB 调试内核

```
qemu-system-x86_64 ... -s -S        # gdb stub, frozen at reset
gdb build/opencube_base.elf          # the pre-strip ELF has symbols
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

`make kernel` 命令特意保留了 `build/opencube_base.elf` 文件，就是为了这个目的 - 最终的 `build/opencube.elf` 文件已被剥离调试符号。

## 确定性回放

构建是可复现的 - 相同的源代码树会产生字节完全相同的 ELF 文件，因此一个可以复现两次的崩溃，两次产生的字节也是相同的。在网络测试中，可以结合 QEMU 自身的 `-icount` 参数来获得时间稳定性。

## 一目了然的设备矩阵

点亮各个驱动家族所需的 QEMU 参数在[驱动手册](../drivers/storage.md)中已制表呈现 - 请精确附加你要调试的设备。
