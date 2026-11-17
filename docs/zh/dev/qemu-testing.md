<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 测试

项目实际上如何在 QEMU 中测试 —— 发布门禁所用的同一套配方，
外加那些若靠踩坑学会会耗费数小时的 QEMU 特有行为（勘误、
固件路径、输入注入）。取证方法论见
[debug-handbook.md](debug-handbook.md)；套件机制见
[e2e-suites.md](e2e-suites.md)。

## 1. 交互式启动

```
make run-bios          # default: 512M, e1000 + user net, serial on stdio
make run-uefi          # OVMF via a pflash copy of OVMF_FD
make run-bios-gui      # GTK window (PS/2 keyboard receives scancodes)
make run-uefi-gui
make run-bios-persist  # -snapshot OFF: /etc edits persist in build/etc.img
make shot-bios         # VNC + monitor screendump -> PNG (tools/qemu_shot_vnc.py)
```

`run-*` 目标会构建 ISO 和一个全新的 32 MiB FAT32 `/etc`
卷（`build/etc.img`），因此持久化测试从干净状态开始；
persist 变体则刻意保留你的编辑。

## 2. OVMF 路径

- 默认：`OVMF_FD = $(OC_TOOLS)/share/ovmf/OVMF.fd` —— 由
  工具链根推导（系统安装时为 `/usr`，
  `tools/install-toolchain.sh` 的免 root 独立目录中为
  `<tree>/share/ovmf/OVMF.fd`）。
- `run-uefi` 目标把 `OVMF.fd` 复制到一个 pflash *工作*文件
  （`-drive if=pflash,...OVMF_WORK.fd`），这样固件变量镜像
  可写，而你原始的 `OVMF.fd` 永不被修改。
- 发布启动门禁在每次运行时都使用 FRESH 的 mktemp 日志、
  通过 pflash CODE+VARS 运行 OVMF（BUG #63 假绿修复 ——
  向旧日志追加会让死掉的启动看起来是绿的）。
- `QEMU_DATADIR`/`SEABIOS_DIR` 以同样方式跟随 `OC_TOOLS`，
  分别供给 QEMU `-L` 与 BIOS 查找。

## 3. 确定性启动日志测试

```
python3 tools/run_fix4_regression.py build/opencube.iso
```

在 `-serial mon:stdio` 上跑 pexpect（latin-1、去 NUL、
去 ANSI、提示符 `(?:ush>|oc>)\s`），然后执行带逐命令分节
断言的固定命令组。启动到 `ush>` 提示符约需 2 秒墙上时间；
完整的 31 项检查在远不到一分钟内跑完。完整检查清单：
[e2e-suites.md](e2e-suites.md) 第 2 节。

## 4. QEMU 特有行为与勘误

与真实硬件不同的行为（均已在源码或内核内 trace 中验证）：

- **AHCI/NVMe 跳板缓冲是 QEMU 可见的契约**：AHCI 通过
  每端口一个 4 KiB 页对齐跳板缓冲执行 READ/WRITE DMA EXT，
  按至多 8 扇区的分块切分（每块一条 PRDT 项）；NVMe 使用
  一个带 64 位 PRP 的页跳板缓冲。若要改动存储驱动，请保持
  跳板纪律 —— QEMU 的 DMA 引擎对 PRDT 对齐的严格程度
  是某些真实控制器不具备的。
- **PIIX3 prog-if 怪癖**：ATA BMDMA 探测以任意 prog-if
  （掩码 0xFFFF00）匹配 IDE 类 0x0101，因为 QEMU 的 PIIX3
  报告 0x80 而真实芯片报告 0x00/0x8A/... —— 仅按基类探测
  会错误地锁到 IDE 控制器上。
- **TCO 的存在性**：Intel TCO 看门狗位于 **q35** 机型的
  LPC 桥（ICH9-LPC，PCI 00:1f.0）上；默认 `pc` 机型
  （PIIX3）没有 —— 探测会诚实地上报"无"。看门狗 E2E
  场景以 `-no-reboot` 启动 q35。
- **HDA + 44100 会冻住 VM**：驱动在真实硬件上编程正确，
  但在 QEMU 下整台 VM 冻结 —— 因此 `sample_rate_test`
  只对该组合报告 caps 位。这一点被诚实上报，而非掩盖。
- **HMP 截屏后串口 RX 字节丢失**：QEMU 在 monitor 截屏
  之后会间歇性丢失串口输入（已用内核内 RX trace 验证）。
  因此功能测试轮次从不截图；抖动的检查会被隔离重跑
  （`wp10_native_manual.py`）。
- **4 GiB 以上的 xHCI DMA 被拒绝**，发生在探测和每次传输时 ——
  若 xHCI 设备"消失"，检查你的 `-m` 大小。
- **Virtio-blk legacy 容量**在配置偏移 0x14 处读取
  （旧的 "capacity = 0" bug 的修复）。
- **e1000 恢复**：`net_e1000_resume()` 在 ACPI S3 之后
  重放 legacy e1000 寄存器编程（QEMU 会在挂起时复位 NIC）。
- **显示**：MB2 头请求 800x600x32；UEFI-GOP 交接 RGBX 而
  BIOS-VBE 交接 BGRX —— `screen_fb_rgb` 从 GRUB tag 推导
  掩码，因此两者都能工作。
- **热复位保留 DRAM**：启动状态孤岛 0x98000/0x9B000 能
  在 QEMU 热复位后存活（E820 保留，经实证）；冷启动会清除
  它们，且每个读取方都会先校验魔数。

## 5. 驱动 OCBL 启动菜单（sendkey）

OCBL 启动菜单（树内引导加载程序，10 秒倒计时）读取
PS/2 按键 —— 它先于内核、先于任何串口控制台存在。在
无头 QEMU 中你通过 HMP monitor 驱动它：

- 以 `-monitor stdio`（或测试装置所用的 unix-socket
  monitor）连同 `-serial stdio` 启动 QEMU。
- `sendkey down` / `sendkey up` 移动高亮；`sendkey ret`
  启动所选条目；倒计时 10 秒后启动默认项。
- 在菜单中选择 OCIRE 条目会写入一条 USER_REQUEST 记录
  （这正是 OCIRE 上报的区别："由用户请求" vs 判定为故障
  并给出修复建议）—— WP04/WP07a 证据记录中的交互式
  sendkey 会话驱动的正是这一点。
- 同一条 `sendkey` 通道也是各套件把串口链路无法承载的
  按键（方向键、Home/End）送入*客户机*的方式（行编辑器
  与编辑器测试）；指针输入走 QMP `input-send-event`
  （`-display none` + QEMU 10 上 HMP mouse_move 已失效）。

## 6. 截图

```
make shot-bios     # boot screenshot to build/shot-bios.png (VNC + PIL)
make shot-uefi
```

帧缓冲断言（回归检查 1）要求 PPM 截屏显示 >= 2 种颜色
和可测的前景比例 —— 曾经有一道仅看串口的门禁在死面板上
通过，因此截图是活着的显示的*唯一*被接受的证明，而功能
轮次因上述串口 RX 勘误避开截图。

## 7. 与驱动对应的 QEMU 设备

| QEMU 标志 | 内核驱动 |
|-----------|---------------|
| `-device e1000`（默认网络） | e1000 NIC（内建路径） |
| `-device e1000e` 等 | WP-10b 框架家族 |
| `-device virtio-blk-pci` | virtio-blk（`vda`） |
| `-device nvme` | NVMe（`nvme0`） |
| `-device ahci` + SATA 磁盘 | AHCI（`sda`...） |
| IDE 磁盘 | ATA PIO + Bus-Master DMA（`hda`..） |
| 经 xHCI 的 `-device usb-storage` | USB MSC（注册块设备） |
| `-device AC97` / `-device intel-hda` + `-device hda-output` | 声音 |
| `-device virtio-sound-pci` | virtio-snd |
| q35 机型 | Intel TCO 看门狗 |
| `-bios OVMF.fd` / pflash | UEFI 启动路径 |
| LSI 53C895A（`-device lsi`） | SCSI（相机族 `scsi` op） |
| `-acpitable file=wdat.bin` | ACPI WDAT 看门狗（tools/make_wdat_table.py） |

## 8. 速度

- 每次自动化运行都用 `-display none -serial mon:stdio`。
- 在回归环境中启动到 `ush>` 提示符约需 2 秒墙上时间。
- 编译缓存（`tools/oc_cc.sh`）让*构建*内循环保持热态；
  见 [build.md](build.md) 第 7 节。
- `-icount` 为网络测试提供时序稳定性；可重现构建意味着
  两次复现的崩溃两次都是相同字节。
