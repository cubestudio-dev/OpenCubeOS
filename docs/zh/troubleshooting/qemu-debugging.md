<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 调试技术

## 捕获一切

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none \
    -netdev user,id=n1 -device e1000,netdev=n1
```

串口日志**就是**启动叙事——每个子系统一行 `OK`。

## 监视整机，而不只是客户机

```
-d int,cpu_reset,guest_errors -D qemu.log    (interrupts/resets/errors)
-monitor stdio                               (QEMU monitor alongside)
```

## GDB 接入内核

```
qemu-system-x86_64 ... -s -S        # gdb stub, frozen at reset
gdb build/opencube_base.elf          # the pre-strip ELF has symbols
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

`build/opencube_base.elf` 正是为此被 `make kernel` 保留——最终
的 `build/opencube.elf` 是剥离过的。

## 确定性复现

构建是可复现的——同一棵代码树产出逐字节相同的 ELF，因此
你能复现两次的崩溃，两次都是同一批字节。配合 QEMU 自带的
`-icount` 可让网络测试的时序保持稳定。

## 设备矩阵一览

点亮各驱动家族的 QEMU 参数表列在
[驱动手册](../drivers/storage.md)中——精确挂上你正在调试的那个
设备。
