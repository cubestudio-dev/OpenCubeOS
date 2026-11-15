<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 调试技术

捕获一切、盯住整机（而不只是客户机）、挂上
gdb、并了解勘误 — QEMU 自身的怪癖在"OS bug"中占了
惊人的份额。

## 捕获一切

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none \
    -netdev user,id=n1 -device e1000,netdev=n1
```

串口日志就是引导叙事 — 每个子系统一行 `OK`、panic
横幅、寄存器转储、crashlog 刷写。串口控制台是
**权威**（VGA 尽力而为：panic 单行用 `Registers and call trace on the serial console.` 把你指回串口）。

对三重故障 / 复位循环：

```
-d int,cpu_reset,guest_errors -D qemu.log
```

日志显示最后一次中断/复位；boot.log 中"最后一条
没有打印 OK 的自测行"指名死去的阶段。

## 交互式串口 + monitor

```
qemu-system-x86_64 ... -serial stdio -monitor stdio
```

在同一个终端上给出客户机控制台与 QEMU monitor（E2E
运行器用 `mon:stdio`；前缀 `@` 的命令送往
monitor）。要送入串口链路无法承载的按键：

```
(qemu) sendkey ctrl-alt-f2      # OCSE GUI<->CLI switch
(qemu) sendkey up               # menu navigation (PS/2 path)
(qemu) sendkey f2               # editor IME toggle
(qemu) screendump shot.ppm      # VGA proof (see errata first!)
```

`sendkey` 是方向键/F 键的注入路径：TCP 串口套接字上的
CSI 字节在引导菜单时刻到达得太晚。指针输入
需要 QMP `input-send-event`（HMP `mouse_move` 在
`-display none` + QEMU 10 上已死）。

## GDB 进入内核

```
qemu-system-x86_64 ... -s -S        # gdb stub, frozen at reset
gdb build/opencube_base.elf          # the pre-strip ELF has symbols
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

`build/opencube_base.elf` 由 `make kernel` 保留正是为了
这个 — 最终的 `build/opencube.elf` 已被 strip。

## 确定性重放

构建是可复现的（`make reproducible-check`）：同一棵树
产出逐字节相同的 ELF，因此你能复现两次的崩溃
两次都是同样的字节。配合 `-icount` 获得网络测试的时序
稳定。

## QEMU 勘误清单

针对本项目验证过的怪癖 — 责怪内核之前先查这些：

| 勘误 | 影响 | 规避 |
|---------|--------|------------|
| QEMU 10 的 `intel-hda` 模型在 44.1 kHz 上死循环 | 该采样率下音频冻结（真实硬件正常；源内有记载） | 其他采样率，或 `-device AC97`/`-device sb16` |
| QEMU 在 HMP `screendump` 之后丢弃串口 RX 字节 | 随后几秒输入抖动（经内核内 RX 追踪验证） | 绝不在功能轮次中途截图；抖动的检查隔离重跑 |
| HMP `mouse_move` 在 `-display none` + QEMU 10 上已死 | GUI 测试无法移动指针 | QMP `input-send-event` |
| 串口回显与客户机输出重叠 | 吃掉 `e` 的 `ega]` 伪影；错位片段 | 容错匹配（把 `[tag]` 括号视为可选，剥离回显行）；见 [silent-failures.md](silent-failures.md) §8 |
| TCG（无 KVM）RSA-2048 超过 500 s | sshd 首启密钥生成看似挂起 | 预置 `etc.img`（`tools/host_keygen_etc.py`）；宽裕超时（横幅 420 s） |
| 默认 `pc` 机器无 Intel TCO | `watchdog hw: none`（如实） — q35 的 ICH9-LPC 有 | TCO 测试用 `-machine q35` |
| user-net 无射频 | `wifi scan` 如实报告无射频 | 跳过，或物理硬件 |
| El Torito `-boot-info-table` | 将引导镜像的字节 8..0x47 清零 — OCBL stage1 入口代码被毁（静默挂起 + 串口上 5 MB 的 0x00） | 绝不对 OCBL ISO 启用它（`build_test_iso.py` 刻意省略它） |

## 设备矩阵速览

点亮各驱动家族的 QEMU flag 已制表于
[驱动手册](../drivers/storage.md) — 挂上你正在调试的那个确切设备。常备优选：

- `-device e1000,netdev=n1` + `-netdev user,id=n1` — 测试最充分的 NIC。
- `-device virtio-net-pci` — virtio 路径（当心 TX 超时）。
- `-drive file=hd.img,format=raw` — 磁盘；`readonly=on` 产生如实的
  `virtio-blk: write rejected`（按设计）。
- `-device intel-hda` / `AC97` / `sb16` — 音频。
- `-machine q35` — TCO watchdog、ICH9 LPC。

## 双实例链路

共享 `-netdev socket,multicast=...` 为
NIC/SSH 测试提供真实双节点链路；端口转发（`-netdev user,id=n1,hostfwd=tcp::2223-:22`）让宿主 paramiko 客户端接到
客户机 `sshd 22`。

## 截图管线

`make shot-bios` / `make shot-uefi`（VNC + monitor screendump → 经
PIL 转 PNG，`tools/qemu_shot_vnc.py`）；`tools/qemu_snap.py` 启动
ISO、对 VGA 截屏、把 80×25 网格渲染为 ASCII。屏幕内容
断言必须检查 ≥ 2 种颜色与可测量的前景占比
（BUG #66：没有机器断言就绝不信"它渲染出来了"）。

## monitor 套接字与裸串口

脚本化控制时，把 monitor 和串口放在 unix 套接字上：

```
qemu-system-x86_64 ... \
    -serial unix:serial.sock,server,nowait \
    -monitor unix:mon.sock,server,nowait
```

- 裸控制字节（`fix1_before_*` 捕获）需要 unix 套接字
  chardev 才能让字节**原样**到达 — TCP 串口链路会破坏
  时序并在引导菜单时刻吃字符。
- `socat - UNIX-CONNECT:mon.sock` 给你 monitor；
  `sendkey`、`screendump`、`pmemsave`、设备热插拔都在那里可用。
- 事后剖析的内存转储：`(qemu) pmemsave 0xb8000 4000 vga.bin`（VGA 文本单元 — OCIRE 滚动证明的原理，
  无需 OCR）。
- 从 monitor 做设备排查：`info pci`、`info block`、`info usb`；热插拔可在不重启宿主机的情况下二分
  探针失败。

## sendkey 手册

```
(qemu) sendkey up            # OCBL menu: entry up (also serial CSI ESC[A)
(qemu) sendkey down           # entry down
(qemu) sendkey ret            # select
(qemu) sendkey f2             # editor IME toggle / TUI save-config
(qemu) sendkey f5             # TUI repaint
(qemu) sendkey esc            # leave a TUI page
(qemu) sendkey ctrl-alt-f1    # OCSE GUI <-> CLI switch
(qemu) sendkey ctrl-alt-f2    # the other direction
```

规则：PS/2 是注入路径（串口上的 CSI 字节在菜单
时刻到达得太晚）；组合键用连字符（`ctrl-alt-f2`）；
OCBL 菜单本身按契约只认 Up/Down/Enter，外加
供无头自动化使用的串口 CSI 镜像。

## 解读 boot.log — 一个实例

```
[    0.000] BOOT: OK
[    0.412] KM_AIN_ENTERED: OK
[    1.208] SCHED_READY: OK
[    2.017] DRIVERS_READY: FAIL      <-- died here
```

FAIL 行指名阶段；其上最后一条驱动行指名
设备。对照 [boot-failures.md](boot-failures.md) §1 的阶段
阶梯；OCBL 侧按同样的阶段判定这次
引导（30 s 启动 / 10 s 阶段预算，RTC 静默 ≥ 8 s =
失败）。同一日志中的 panic 横幅意味着
内核回收了一条记录 — 下次启动检查 `crashlog`。

## 确定性与可复现性

- `make reproducible-check` 验证逐字节相同的输出。
- 同一棵树 + 同样 flag = 同样的崩溃：按构建二分，不要
  猜。
- 时序稳定的网络轮次用 `-icount`（TCP 套件依赖
  它）；搭配 `-rtc base=utc` 让依赖时钟的测试保持清醒
  （否则 X509_E_EXPIRED / NOT_YET 就是陷阱）。

## 替你驱动 QEMU 的 make 目标

| 目标 | 作用 |
|--------|--------------|
| `make run-bios` / `run-uefi` | 以串口 stdio 引导 ISO（BIOS / OVMF） |
| `make run-bios-gui` / `run-uefi-gui` | GTK 窗口让 PS/2 键盘获得真实扫描码 |
| `make run-bios-persist` / `run-uefi-persist` | 关闭 `-snapshot` 使 /etc 修改持久落在 `build/etc.img` |
| `make shot-bios` / `shot-uefi` | VNC + monitor screendump → PNG（`tools/qemu_shot_vnc.py`） |

它们都假定 `build/opencube.iso` 已存在（`make iso`）。

## bug 报告要捕获什么

1. `boot.log`（会话完整的 `-serial file:` 捕获）。
2. 若开了 `-d int,cpu_reset` 则附 `qemu.log`。
3. 确切的 QEMU 命令行与 `-version`。
4. 客户机内（可能时）：`health`、`dmesg 1024`、`crashlog`、
   `dumpinfo`、`abcfg`（A/B 状态）、`update status`。

## 相关页面

- [boot-failures.md](boot-failures.md) — 解读引导叙事
- [silent-failures.md](silent-failures.md) — 串口回隐容错、
  捕获路由
- [../dev/qemu-testing.md](../dev/qemu-testing.md) — 完整测试
  工具
- [../dev/debugging.md](../dev/debugging.md) — 开发者技术
