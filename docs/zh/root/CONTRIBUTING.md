<!-- GENERATED mirror of the repository-root CONTRIBUTING.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# 为 Open Cube OS 做贡献

感谢您对做出贡献的兴趣！Open Cube OS 是一个从零开始的 x86_64 内核，每一份贡献——代码、文档、驱动点亮、错误报告——都受到欢迎。

## 基本规则

1. **内核字符串必须是 ASCII 英文。** 系统自身打印的所有内容（命令名、提示符、启动消息、帮助文本）必须保持纯 ASCII。非 ASCII 文本可以出现在用户数据和文档中，绝不能出现在内核可见的字符串中。
2. **命令接口面只增不减。** 已经发布的 Shell 命令可以在内部进行重构，但其名称和整体行为必须保持不变；删除会破坏接口契约。使用 `shell_register_command_ex()` 和工作包标签来注册新命令。
3. **零警告。** 构建时使用 `-Wall -Wextra -Werror` 编译。引入警告的变更就是破坏性的变更，无论是在您的机器上还是在 CI 上。
4. **无存根，无假数据。** 如果一个功能是部分的，请在代码和文档中明确说明——绝不要掩盖它。一个仅探测并报告"在此机器上不支持"的驱动是可以的；一个假装工作的驱动则不行。
5. **全面使用 Apache 2.0。** 每个新的源文件都以 SPDX 头部开始：

   ```
   /* SPDX-License-Identifier: Apache-2.0 */
   /* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
   ```

## 开发流程

### 1. 开始构建

遵循 [BUILDING.md](BUILDING.md)。在 Linux 上的简短版本：

```
./build.sh iso          # kernel + bootable ISO
make test               # host-side unit tests
```

您需要 `gcc, nasm, grub-mkimage, xorriso, mtools, python3, zip`
（`tools/build.sh` 会检查并告诉您具体缺少哪些，或使用 `--install` 安装它们）。

### 2. 进行您的修改

- 保持修改的专注性：每个拉取请求只包含一个驱动、一个修复、一个文档章节。
- 每个内核源文件都带有 SPDX 头部——请保留它。
- 新命令：在相应的 `shell/shell_cmds_*.c` 中注册，添加帮助行，并扩展回归预期（见 `tools/run_fix4_regression.py`，命令计数基线）。

### 3. 推送前测试

最低要求，全部执行：

```
make kernel             # 0 errors, 0 warnings (-Werror enforced)
make test               # host unit suite (tools/run_host_tests.py)
make iso                # hybrid BIOS+UEFI ISO builds
make reproducible-check # two clean builds -> byte-identical ELF
```

然后启动它：

```
make run-bios   # or: qemu-system-x86_64 -cdrom build/opencube.iso
make run-uefi   # OVMF path
```

如果您的修改涉及以下任何内容，还需要运行相应的深度测试：

| 您修改了...                  | 还需运行                                                 |
|-----------------------------|----------------------------------------------------------|
| 存储 / 文件系统             | 客户机中的 `fstest`、`dhtest`                            |
| 加密 / TLS / SSH            | 客户机中的 `cryptotest`、HTTPS E2E                       |
| 看门狗 / klog / 健康        | `tools/wp12_watchdog_logging_e2e.py build/opencube.iso`   |
| 完整启动路径                | 回归测试套件（`tools/run_fix4_regression.py`）           |
| A/B 槽位或 OTA              | 客户机中的 `ab_partition_test`、OTA 安装测试            |

CI（`.github/workflows/build.yml`）会对每个 PR 运行构建、打包、宿主测试和可复现门禁——合并前必须全部通过。

### 4. 提交风格

- 每个逻辑步骤一个提交；每个提交信息回答*做了什么*和*为什么*，而不仅仅是*做了什么*。
- 使用领域作为前缀：`usb: ...`、`fs: fat32: ...`、`docs: ...`、`build: ...`。
- 如果您修复了被跟踪的审计错误，请在信息中引用其编号（`BUG-0136`）。

### 5. 拉取请求

1. 从 `main` 分叉或创建分支。
2. 推送您的分支，针对 `cubestudio-dev/OpenCubeOS` 打开 PR。
3. 填写 PR 模板中的检查清单：
   - [ ] 使用 0 个警告构建（`-Wall -Wextra -Werror`）
   - [ ] 宿主测试通过（`make test`）
   - [ ] 在 QEMU 中启动（BIOS 和 UEFI）
   - [ ] 相关深度测试通过（上表）
   - [ ] 文档已更新（`docs/` 下的手册页，如果 L1 接口面有变化则更新接口列表）
4. 期待关于边界情况、DMA 边界和错误路径的审查意见——这对于内核代码来说是正常的。

## 报告错误

通过以下方式打开问题：
- 客户机内 `uname -a` 的构建标识符，
- 精确的命令序列，
- 串口日志（`-serial file:log.txt` 是您的朋友），
- 是否在 BIOS、UEFI 或两者上都能复现。

安全敏感报告：见 README 中的 `SECURITY` 说明——不要公开打开问题；请通过电子邮件联系维护者。

## 文档贡献

`docs/` 下的文档树是产品，而不是事后考虑。如果您添加了命令或接口，相应的手册/参考页必须包含在同一 PR 中。参考页从实际树生成（`make api-docs`）——不要手动编辑生成的表格。

## 许可证

通过提交贡献，您同意将其根据 Apache 2.0 许可（存储库根目录的 `LICENSE` 文件）进行许可，并且版权行可以包含项目持有者。

感谢您与我们一同构建 Open Cube OS。
