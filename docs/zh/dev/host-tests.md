<!-- SPDX-License-Identifier: Apache-2.0 -->
# 宿主侧单元测试

`make test` 运行 `tools/run_host_tests.py`：它在宿主上编译并执行 `tests/host_*.c` 中的测试桩，使用它们包含的实际内核源文件。

## 运行内容

运行程序从每个测试桩的头文件中提取文档化的 gcc 命令行，重映射重构前的路径（`kernel/aead.c` -> `kernel/crypto/crypto_aead.c` 等），使用宿主工具链编译（测试桩要求时使用 ASAN），然后执行它。需要外部固件（openssl 生成的密钥文件）的测试桩会编译但报告 SKIP-FIXTURE 而非失败。

覆盖率亮点（每个族一个测试桩）：

- crypto：NIST AES/SHA/HMAC 向量、RSA、X.509 解析、AEAD
- tar / gzip / string：lib_* 基础组件
- FAT 结构与 LFN 编码
- multiboot2 解析（包含损坏 MBI 情况，ASAN）
- 堆与 VMM 不变性

## 何时运行

随时运行。它是反馈循环中最快的信号 - 无需启动，能在几秒内捕获纯逻辑回归（校验和链、头文件对齐、边界）。

## 添加测试桩

1. 创建 `tests/host_<name>.c`，包含标准头文件注释，注明确切的编译命令行（标志 + 包含路径）。
2. 统计检查次数，按失败数量退出。
3. `make test` 会自动拾取它。

来自 [CONTRIBUTING.md](../root/CONTRIBUTING.md) 的规则：切勿削弱测试以使其通过。如果测试有误，修复测试 *并在提交中说明*。
