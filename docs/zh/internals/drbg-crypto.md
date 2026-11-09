<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：熵与加密核心

`kernel/crypto/crypto_core.c` - RNG 以及协议所构建的哈希/MAC/AEAD 原语。

## 种子

基于 HMAC-SHA-256 的 HMAC_DRBG (NIST SP 800-90A)，种子来源：

- **RDRAND**（CPU 熵 - 通过 CPUID 验证），
- **RDTSC**，每次生成调用后重新混合，因此部分状态
  攻击无法持久。

（rdrand 包装器的 `setc` 标志是更严格的 CI 汇编程序强制修正的正确性 - 可移植性说明参见 [../dev/portability.md](../dev/portability.md)。）

## 原语族

- AES、SHA-2 族、HMAC - NIST 向量验证（`cryptotest`）。
- RSA、NIST 曲线、curve25519 风格、Ed25519 风格 - TLS/SSH
  密钥交换（`dhtest` 计时并验证 group-14 模幂）。
- AEAD (GCM)、X.509 解析 + 链验证（RFC 6125 策略）。

## 形成它的审计轨迹

旧的 LCG 已被替换（对观察者可见重复）；TCP RX 校验和链错误（双补码）和 TLS
握手中途主机名错误是两个著名的加密相关修复
- 现在都已通过回归测试。
