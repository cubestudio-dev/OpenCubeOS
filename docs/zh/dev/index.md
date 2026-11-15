<!-- SPDX-License-Identifier: Apache-2.0 -->
# 开发者手册

从一台全新机器到合入 PR —— 3.0.0 树（ocos/ 服务内核、
284 个 ring-3 程序、8 个已合并的 mega 应用）。

| 页面 | 内容 |
|------|------|
| [environment](environment.md) | 工具链契约、依赖门禁、免 root 沙箱、目录布局 |
| [build](build.md) | 目标、隐藏变量、-D 宏、0x400000 窗口与 -Os/-O0 覆盖、两阶段链接、确定性 |
| [interface-dictionary](interface-dictionary.md) | L1SVC 接口面：协议规则 + 全部 20 个类别 / 208 个 op |
| [syscall-reference](syscall-reference.md) | int 0x80 ABI、全部 54 个系统调用、SYS_L1SVC 细节 |
| [data-structures](data-structures.md) | 内核结构体原文 + 谁在何时读写它们 |
| [call-chains](call-chains.md) | execve、L1SVC、网络 RX、块设备 IO、崩溃、OTA —— 六次解剖 |
| [writing-services](writing-services.md) | OC_SERVICE_DECL 逐步讲解（echo 示例）+ OC_DRIVER_DECL |
| [debug-handbook](debug-handbook.md) | 完整调试手册：串口、klog、崩溃转储、静默失败 |
| [debug-interfaces](debug-interfaces.md) | 约 93 个测试程序、诊断深度、隐藏开关 |
| [debugging](debugging.md) | 调试页面的简短入口 |
| [qemu-testing](qemu-testing.md) | 交互式 + 自动化运行、QEMU 勘误、sendkey、OVMF |
| [host-tests](host-tests.md) | `make test` 测试套件 |
| [e2e-suites](e2e-suites.md) | 深度门禁：回归 31/31、SSH 5/5、HTTPS 8/8、mega 应用冒烟 |
| [pr-workflow](pr-workflow.md) | PR 门槛，按顺序 |
| [portability](portability.md) | 更严格工具链的类别 + 编译开关契约 |
| [coding-style](coding-style.md) | C、汇编、命名、限额纪律 |
| [ocire](ocire.md) | 独立恢复内核（第二个 L0） |

## 各部分位置

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

应用侧指南（程序如何编写、L1SVC 接口面、
向导库）有它自己的索引：
[../l1/index.md](../l1/index.md)。子系统内部机制（klog、
崩溃转储、健康、看门狗、加密、OTA/A-B、OCIRE 嵌入）
位于 [../internals/index.md](../internals/index.md)。
