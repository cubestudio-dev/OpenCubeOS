<!-- SPDX-License-Identifier: Apache-2.0 -->
# 为 Open Cube OS 做贡献

感谢你有意贡献！Open Cube OS 是一个从零写起的 x86_64 内核，
每一类贡献——代码、文档、驱动上线、bug 报告——都受欢迎。

## 基本规则

1. **内核字符串是 ASCII 英文。** 系统自身打印的一切（命令
   名、提示符、启动消息、帮助文本）必须保持纯 ASCII。非
   ASCII 文本可以出现在用户数据和文档里，绝不进入内核可见
   的字符串。
2. **命令面只增不减。** 已发布的 shell 命令可以在内部重做，
   但其名字与总体行为必须保留；移除会破坏接口契约。用
   `shell_register_command_ex()` 和一个工作包标签注册新命令。
3. **零警告。** 构建以 `-Wall -Wextra -Werror` 编译。引入
   警告的变更就是坏变更——在你的机器上是，在 CI 上也是。
4. **不要桩，不要假数据。** 若特性不完整，在代码和文档里
   明说——绝不掩饰。一个只做探测并报告"此机不支持"的驱动
   没问题；一个假装能工作的驱动不行。
5. **处处 Apache 2.0。** 每个新源文件以 SPDX 头开始：

   ```
   /* SPDX-License-Identifier: Apache-2.0 */
   /* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
   ```

## 开发流程

### 1. 把构建跑起来

遵循 [BUILDING.md](BUILDING.md)。Linux 上的短版本：

```
./build.sh iso          # kernel + bootable ISO
make test               # host-side unit tests
```

你需要 `gcc, nasm, grub-mkimage, xorriso, mtools, python3, zip`
（`tools/build.sh` 会检查并告诉你确切缺哪些，或用
`--install` 安装它们）。

### 2. 做你的修改

- 保持修改聚焦：每个拉取请求一个驱动、一个修复或一个文档
  章节。
- 每个内核源文件都携带 SPDX 头——保留它。
- 新命令：在对应的 `shell/shell_cmds_*.c` 里注册、
  添加帮助行，并扩展回归期望
  （见 `tools/run_fix4_regression.py`，命令计数基线）。

### 3. 推送前测试

最低门槛，全部要过：

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

若你的修改碰到下表所列内容，对应的深度测试同样必须跑：

| 你碰了…                  | 还要跑                                              |
|---------------------------------|-------------------------------------------------------|
| 存储 / 文件系统           | 客户机里的 `fstest`、`dhtest`                       |
| 加密 / TLS / SSH              | 客户机里的 `cryptotest`、HTTPS E2E                  |
| 看门狗 / klog / health        | `tools/wp12_watchdog_logging_e2e.py build/opencube.iso` |
| 完整启动路径              | 回归套件（`tools/run_fix4_regression.py`） |
| A/B 槽位或 OTA                | 客户机里的 `ab_partition_test`、OTA 安装测试    |

CI（`.github/workflows/build.yml`）在每个 PR 上运行构建、
打包、宿主机测试和可复现性门禁——合并前必须绿。

### 4. 提交风格

- 每个逻辑步骤一个提交；每条提交信息回答*什么*和
  *为什么*，而不只是*什么*。
- 用领域作前缀：`usb: ...`、`fs: fat32: ...`、`docs: ...`、
  `build: ...`。
- 若你修复了一个被跟踪的审计 bug，在信息里引用它的
  编号（`BUG-0136`）。

### 5. 拉取请求

1. 从 `main` 分叉或开分支。
2. 推送你的分支，向 `cubestudio-dev/OpenCubeOS` 开 PR。
3. 填写 PR 模板的核对清单：
   - [ ] 以 0 警告构建（`-Wall -Wextra -Werror`）
   - [ ] 宿主机测试通过（`make test`）
   - [ ] 在 QEMU 里启动过（BIOS 与 UEFI）
   - [ ] 相关深度测试通过（上表）
   - [ ] 文档已更新（`docs/` 下的手册页，L1 面变更时还有
         接口清单）
4. 预期会收到关于边界情形、DMA 界限和错误路径的评审
   意见——对内核代码来说这很正常。

## 版本号纪律（3.0.0 阶段）

**一个阶段，一个版本主干。** 在 3.0.0 阶段的 74 个工作包走完之前，
发布版本号固定为 `3.0.0-WP-<工作包名>`。minor 与 patch 号**不得**
自行变动：任何贡献者、任何单个工作包都不得在本阶段抬升 patch 号、
minor 号或 major 号——主干始终是 `3.0.0`。工作包只把自己写进后缀，
绝不抬升主干。

**`Makefile` 里的字面量是唯一真源。** `Makefile`（`Makefile:102`）中的
`OC_RELEASE_VERSION` 是版本串的唯一权威——内核横幅、`uname`、OTA 清单
与 ISO 全部取自它。改动这个字面量属于**发布动作**而非代码改动：必须
由工作包授权，并且会牵动整棵代码树。

**版本串分布在多处，改动必须全覆盖。** 同一串版本号出现在 `Makefile`、
`README.md`、`docs/release/`、`website/` 静态页与
`website/assets/site-data.js`、`update.json` / `update-v2.json` 清单、
git tag 名，以及资产文件名（`.iso`、`-src.zip`、`-update.tar.gz`）中。
只改一处从来不算版本变更——版本变更是全仓对账。验收是机械的：
`git grep -n '<旧版本号>'` 必须查无残留，只允许有据可查的非版本号误命中
（上游 npm 包版本号、文档保留 IP、归档证据文件名）。

**违反即视为发布事故。** 未经授权的版本号跃升按发布事故处理，而不是
笔误。回滚流程为：全仓替换版本串 → 重打 tag → 重传资产 → 重部署
gh-pages——按此顺序，每一步之间 `git grep` 检查必须为绿。

参考：`docs/release/versioning.md`（版本管理规则、构建号与兼容承诺）。

### 提交与推送纪律

这条适用于**每一个**任务，不只是发布任务。

**一次验证过的批量，一次推送。** 只有批量内*全部*任务完成并验证通过后，
才提交并推送。禁止边做边推，禁止推送半成品：一次推送即声明代码树能构建、
能启动、且门禁全绿。

**提交前以下各项必须全部成立：** `make all` 0 错误 0 警告、全量回归通过、
`git status` 无遗漏的生成物（版本戳、`docs/data/*.json`、生成的镜像文件）、
且没有仍在后台运行的构建或测试进程。

**双仓，一次推送。** 公开仓 `cubestudio-dev/OpenCubeOS` 与私密镜像仓
**同时**推送——一次推送覆盖两者。只到达其中一个仓的提交属于不完整交付。

**Author 身份是机器硬门。** git author 必须是
`cubestudio-dev <cubestudio@qq.com>`；`tools/build_release.sh`（`:44-45`）
以机械方式强制该身份，不符即构建失败。

**gh-pages 跟随推送。** 只在推送落地后才部署 gh-pages，并线上验证——
部署后的页面是交付的一部分，不是后续补充。

相关：`docs/dev/pr-workflow.md`（本批量纪律所依托的拉取请求门槛）。

## 报告 bug

开一个 issue，附上：

- 客户机里 `uname -a` 的构建标识、
- 确切的命令序列、
- 串口日志（`-serial file:log.txt` 是你的朋友）、
- 在 BIOS、UEFI 还是两者上复现。

安全敏感的报告：见 README 中的 `SECURITY` 注记——不要
开公开 issue；改为邮件联系维护者。

## 文档贡献

`docs/` 下的文档树是产品本身，不是事后补充。若你新增
一条命令或一个接口，对应的手册/参考页必须在同一个 PR 里
落地。参考页从真实代码树生成（`make api-docs`）——不要
手改生成的表。

## 许可

提交贡献即表示你同意它以 Apache 2.0 授权（仓库根的
`LICENSE` 文件），且版权行可以携带项目的持有者。

感谢你和我们一起构建 Open Cube OS。
