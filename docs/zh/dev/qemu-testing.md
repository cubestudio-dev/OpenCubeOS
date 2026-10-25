<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU 测试

项目如何在 QEMU 中实际测试 - 与发布门禁使用的相同测试配方。

## 交互式启动

```
make run-bios      # default: 512M, e1000 + user net, serial on stdio
make run-uefi      # q35 + OVMF
make run-bios-gui  # display on (for the TUI)
```

`run-*` 目标会构建 ISO 和一个全新的 `etc.img`（32 MiB FAT32 /etc 分区），这样持久化测试就能从干净状态开始。

## 确定性启动日志测试

回归测试套件（`tools/run_fix4_regression.py`）通过 pexpect 经由 `-serial mon:stdio` 驱动 QEMU：

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

它等待 `ush>` 提示符出现（v2 默认启动项；如需 `oc>`，先发 `exit`），然后执行固定的命令集（uname、help -a、heaptest、fstest、cryptotest、crashlog、l1test，以及整个 `run *test` 用户程序集），并对组合输出执行 28 项检查 - 启动横幅、自检、命令计数 233、每个测试的 PASS 行。零失败是门槛。

## 看门狗 / 日志 E2E

```
python3 tools/wp12_watchdog_logging_e2e.py build/opencube.iso
```

14 个场景：klog 写入/刷新/轮转/过滤链，看门狗状态/槽位/策略终止，健康组 + JSON 导出 + CPU 警报，以及两个*真实硬件复位*场景 - S12 启动带 `-no-reboot` 的 q35 机器，发现 Intel TCO，激活它，饥饿它，并断言第二次启动；S13 通过 ACPI WDAT 执行相同操作。

## 网络 E2E

- **SSH E2E**：启动客户机内的 `sshd`，然后通过 paramiko 从主机登录 - 完整密钥交换、会话、命令往返。需要使用主机密钥（`host_keygen_etc`）预置的 /etc 分区。
- **HTTPS E2E**：对参考 TLS 服务器执行 `wget https://<host>` - 握手、证书验证、正文保存、close_notify。

两个套件都是发布门禁；参见 [e2e-suites.md](e2e-suites.md) 了解其完整机制。

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
| `-device usb-storage` 通过 xHCI | USB MSC |
| `-device AC97` / `-device intel-hda` | 声卡 |
| q35 机器 | Intel TCO 看门狗 |
| `-bios OVMF.fd` | UEFI 启动路径 |

## 速度

- 自动化每次运行都使用 `-display none -serial mon:stdio`。
- 在回归环境中启动到 `oc>` 提示符约需 2 秒壁钟时间；完整的 28 项检查套件运行远低于一分钟。
