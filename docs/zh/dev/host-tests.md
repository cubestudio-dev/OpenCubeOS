<!-- SPDX-License-Identifier: Apache-2.0 -->
# 宿主机侧单元测试

`make test` 运行 `tools/run_host_tests.py`：它在*宿主机上*编译
并执行 `tests/host_*.c` 中的测试桩，针对它们所包含的真实内核
源文件。

## 运行什么

运行器从每个测试桩的头部注释提取文档化的 gcc 命令行，重映射
重构前的路径（`kernel/aead.c` -> `kernel/crypto/crypto_aead.c`，
...），用宿主机工具链编译（在测试桩要求时开 ASAN）并执行。
需要外部夹具（openssl 生成的密钥文件）的测试桩会编译通过但
报告 SKIP-FIXTURE 而不是失败。

覆盖亮点（每个家族一个测试桩）：

- crypto：NIST AES/SHA/HMAC 向量、RSA、X.509 解析、AEAD
- tar / gzip / string：lib_* 原语
- FAT 结构与 LFN 编码
- multiboot2 解析（含畸形 mbi 场景，ASAN）
- 堆与 VMM 不变量

## 什么时候运行

始终运行。它是整个循环里最快的信号 - 不需要启动，几秒钟内就
能抓住纯逻辑回归（校验和链、头对齐、边界）。

## 新增一个测试桩

1. 创建 `tests/host_<name>.c`，带标准头部注释，其中包含确切的
   编译命令行（标志 + 包含路径）。
2. 数清你的检查数，并以失败数作为退出码。
3. `make test` 会自动发现它。

[CONTRIBUTING.md](../root/CONTRIBUTING.md) 的规则：绝不为通过
而削弱测试。如果测试本身错了，修测试，*并在提交里说明*。
