<!-- SPDX-License-Identifier: Apache-2.0 -->
# 端到端测试套件

用于发布门禁的深度测试。所有测试都通过 pexpect 在串口控制台上驱动真实的 QEMU 启动过程。

## 回归测试套件 - tools/run_fix4_regression.py

28 项检查：启动横幅、启动完成、异常自测 3/3、命令计数 233、uname 标签格式、heaptest、fstest、cryptotest、crashlog、l1test，以及所有 `run <userprog>`（hello、fork、exec、pipe、signal、select、mmap 变体、动态链接：so_test、dlsym_test、pie_test、reloc_test、p3、g6、g7...）。

```
python3 tools/run_fix4_regression.py build/opencube.iso out.log
```

## 看门狗/日志/健康 E2E - tools/wp12_watchdog_logging_e2e.py

14 个场景（S1-S14）。其中最重要的两个：

- **S12（Intel TCO）**：q35 + `-no-reboot`；断言 TCO 发现行（`Intel TCO found: PMBASE=... TCOBASE=...`）、启用看门狗、使其饥饿、断言真实复位和第二次启动。
- **S13（ACPI WDAT）**：通过 WDAT 表条目以相同方式实现。

此外还有 klog 写入/刷新/轮转/过滤链（阈值 4096 -> `kernel.log` + `kernel.log.1` 实际生成）、看门狗策略终止（一个饥饿任务被终止并记录日志，而内核存活）、以及健康监控（JSON 导出有效性、3 个旋转的 CPU 警报 `CPU-LOCKED` 且内核存活）。

## SSH E2E

客户机内的 `sshd` 需要在 /etc 卷上拥有主机/客户端密钥 - 测试套件（或您）使用 `host_keygen_etc` 对它们进行初始化。然后来自主机的 paramiko 客户端执行完整的登录 + 会话 + 命令往返：5/5 全部通过即为门禁。

## HTTPS E2E

客户机内部使用 `wget https://<reference-host>/` 访问参考 TLS 服务器：握手成功、收到 GET、正文保存、close_notify 清晰。7 项检查（握手、证书链、正文字节、关闭语义、计时）。

## /etc 卷固件

SSH 和 HTTPS 套件假定已初始化的 `etc.img`。每个测试周期构建一次：

```
make etc.img          # 32 MiB FAT32 + opencube.conf
# then inside a first boot: host_keygen_etc (writes SSH keys)
```

QEMU 的 `run-*` make 目标会自动附加它。
