<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：健康监控

`kernel/core/core_health.{c,h}` - 8 个声明；编号为 5 的：

```
health_get_cpu(out)
health_get_mem(out)
health_get_tasks(out)
health_get_io(out)
health_export(path)
```

## 指标监控内容

- **CPU**：每个核心在 1 秒窗口内的忙碌度统计 - 三个自旋任务会明显占用核心（实时观测到 100%；连续 10 次采样中维持 >= 90% 即为告警阈值）。
- **内存**：PMM/堆状态。
- **任务**：进程表普查。
- **IO**：块设备/网络计数器。

## 告警

阈值越界时会写入内核日志：当某个核心持续被占用时写入 `CPU-LOCKED`（E2E S11 运行 3 个自旋任务并在内核响应时触发告警），内存压力，IO 阻塞。

## 导出

`health export` 会将有效的 JSON 写入 `/var/log/health.json`（E2E 中标准指标集的 386-389 字节）- 任何监控工具都可解析，包括重启后从 /etc 卷解析。

## 命令

`health`（摘要），`health watch`（实时 1 秒），`health export`。
