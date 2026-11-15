<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 测试

项目在 QEMU 中实际如何测试 - 与发布门禁使用的相同配方。

## 交互式启动

```
make run-bios      # default: 512M, e1000 + user net, serial on stdio
make run-uefi      # q35 + OVMF
make run-bios-gui  # display on (for the TUI)
```

`run-*` 目标会构建 ISO 和一个全新的 `etc.img`（32 MiB 的
FAT32 /etc 卷），让持久性测试从干净状态开始。

## 确定性的启动日志测试

回归套件（`tools/run_fix4_regression.py`）通过 pexpect 在
`-serial mon:stdio` 上驱动 QEMU：

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

它等待 `ush>` 提示符，然后运行固定的命令组（uname、help -a、
heaptest、fstest、cryptotest、crashlog、l1test，以及整套
`run *test` 用户程序），并在合并输出上断言 28 项检查 - 启动
横幅、自检、L0 命令数、每个测试的 PASS 行。零失败是门槛。

## 看门狗 / 日志 E2E

```
python3 tools/wp12_watchdog_logging_e2e.py build/opencube.iso
```

14 个场景：klog 写入/刷新/轮转/过滤链、看门狗状态/槽位/策略
击杀、健康分组 + JSON 导出 + CPU 告警，以及两个*真实硬件复位*
场景 - S12 以 `-no-reboot` 启动一台 q35 机器，发现 Intel TCO、
布防它、饿死它并断言第二次启动；S13 通过 ACPI WDAT 做同样的
事。

## 网络 E2E

- **SSH E2E**：启动客户机内的 `sshd`，然后从宿主机通过
  paramiko 登录 - 完整的密钥交换、会话、命令往返。需要 /etc 卷
  预置主机密钥（`host_keygen_etc`）。
- **HTTPS E2E**：对一台参考 TLS 服务器执行 `wget https://<host>`
  - 握手、证书验证、正文保存、close_notify。

两个套件都是发布门禁；其完整机制见
[e2e-suites.md](e2e-suites.md)。

## 截图

```
make shot-bios     # boot screenshot to build/shot-bios.ppm
make shot-uefi
```

## 映射到驱动的 QEMU 设备

| QEMU 标志 | 内核驱动 |
|-----------|---------------|
| `-device e1000`（默认网络） | e1000 网卡 |
| `-device virtio-blk-pci` | virtio-blk |
| `-device nvme` | NVMe |
| 经 xHCI 的 `-device usb-storage` | USB MSC |
| `-device AC97` / `-device intel-hda` | 声音 |
| q35 机型 | Intel TCO 看门狗 |
| `-bios OVMF.fd` | UEFI 启动路径 |

## 速度

- 每次自动化运行都使用 `-display none -serial mon:stdio`。
- 在回归环境中启动到 `ush>` 提示符约需 2 秒墙上时间；完整的
  28 项检查组远在一分钟内跑完。
