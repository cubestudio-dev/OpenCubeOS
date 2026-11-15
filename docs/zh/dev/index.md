<!-- SPDX-License-Identifier: Apache-2.0 -->
# 开发者手册

从一台全新机器到合并 PR —— 3.0.0 树（ocos/ 服务内核、284 个
ring-3 程序、8 个已合并的大应用）。

| 页面 | 内容 |
|------|------|
| [environment](environment.md) | 环境要求、目录布局、QEMU 配方 |
| [build](build.md) | 构建目标、编译缓存、确定性、交叉构建 |
| [qemu-testing](qemu-testing.md) | 交互式与自动化运行、设备矩阵 |
| [debugging](debugging.md) | 启动日志方法、panic 路径、gdb、宿主机测试 |
| [host-tests](host-tests.md) | `make test` 测试桩 |
| [e2e-suites](e2e-suites.md) | 深度门禁：回归 31/31、SSH 5/5、HTTPS 8/8、大应用冒烟 |
| [pr-workflow](pr-workflow.md) | PR 门槛，按顺序 |
| [portability](portability.md) | 更严格工具链的分类 + 编译标志契约 |
| [coding-style](coding-style.md) | C、汇编、命名、限额纪律 |
| [ocire](ocire.md) | 独立恢复内核（第二个 L0） |

## 各目录位置

```
ocos/kernel/       L0 core (boot, sched, mm, usermode)
ocos/services/     one folder per service facet (block, net, ...)
ocos/drivers/      one folder per hardware class
ocos/interface/    the ring-3 headers (l1_lib, l1svc_ops, i18n,
                   wizard_lib, fs_lib, editor_core)
ocos/l1apps/       one command = one app = one folder (284 programs)
ocire/             the independent recovery kernel
tools/             build / test / docs generators
docs/              this documentation (en + zh mirror)
```

应用侧指南（程序如何编写、L1SVC 表面、向导库）有它自己的索引：
[../l1/index.md](../l1/index.md)。
