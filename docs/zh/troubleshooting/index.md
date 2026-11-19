<!-- SPDX-License-Identifier: Apache-2.0 -->
# 排查

| 页面 | 覆盖内容 |
|------|----------------|
| [common-errors](common-errors.md) | 高频的症状→原因→修复表 |
| [error-codes](error-codes.md) | 全部 61 个 errno 家族代码、11 个 panic 位点、30 个 static assert、日志格式 |
| [error-catalog](error-catalog.md) | 按子系统罗列的每条失败消息 + KNOWN_ISSUES |
| [silent-failures](silent-failures.md) | 不打印任何内容的失败：捕获、如实跳过、OTA 链、串口回显 |
| [boot-failures](boot-failures.md) | OCBL 六类扫描器、OCIRE 接管、A/B 回滚、/HANGAT |
| [network-issues](network-issues.md) | DHCP/DNS 链、TLS 自签名拒绝、逐 NIC init failed 模板 |
| [storage-issues](storage-issues.md) | 挂载拒绝、文件系统能力矩阵、A/B 磁盘、64 槽 LRU 缓存 |
| [qemu-debugging](qemu-debugging.md) | 捕获、monitor、gdb、sendkey、QEMU 勘误清单 |
