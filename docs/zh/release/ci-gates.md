<!-- SPDX-License-Identifier: Apache-2.0 -->
# CI/CD 与发布链门禁（维护者手册）

`.github/workflows/build.yml` 在每次推送时强制什么、发布链在
tag 移动前检查什么、回归下限如何强制。配套页面：
[process](process.md)（这些门禁所挂靠的 SOP）、
[build-reference](build-reference.md)（CI 构建的流水线）、
[guard-scripts](guard-scripts.md)（静态守护）。

## 1. 工作流——形态与出处

`.github/workflows/build.yml` 是仓库唯一的 CI 工作流：

- **名称** `build`；**触发器**：push 到 `main`、PR 到
  `main`、`workflow_dispatch`；**权限**：`contents: read`；
  一个作业 `3.0.0 build (0-warning gate)`，运行于
  `ubuntu-latest`、`timeout-minutes: 40`。
- **诚实的出处说明**（声明在文件头）：3.0.0 主线在其最后
  一个主线提交时没有 CI 工作流——没有可恢复的东西。本文件
  是全新的 WP00 基础设施，使 "CI 绿" 仍是可强制的门禁。
  **它构建；它不启动**——每次 QEMU 运行都是仅限本地环境的
  验收步骤。
- 为什么仅构建的 CI 在这里可以接受：零警告门禁加确定性
  构建以机械化方式抓住大多数回归，而启动/E2E 矩阵（§5）
  在发布本地环境中经 `tools/build_release.sh --deep` 运行。流程
  用一条硬规则补偿：**tag 必须指向 CI 绿的提交——绝不给
  红色提交打 tag**（见 [process](process.md) 第 5 步）。

## 2. 六个 CI 步骤逐个说明

### 第 1 步——带 `fetch-depth: 0` 的 Checkout

`actions/checkout@v4`，完整历史。**强制项而非装饰**：构建
id 是 `git rev-list --count HEAD + 21`（`Makefile` 的
`OC_KERNEL_BUILD` 推导），且 OTA 更新检查按数值比较构建 id
（BUG-0347：仅 remote > local）。浅克隆会把构建 id 烘焙到远
低于已发布的（278/321/414/416），每台已安装机器都会把该
发布当作"降级"静默忽略。一个错误但绿的 CI 构建比没有构建
更糟——因此完整深度是第零步。

### 第 2 步——安装工具链

```
sudo apt-get install -y -qq build-essential nasm xorriso mtools \
  grub-pc-bin grub-efi-amd64-bin grub-common
```

正是 `tools/build.sh` 探测的包集合（外加提供 gcc/ld 的
build-essential）。缺任何一个都会让下一步以编译器/打包器
自己的错误失败。

### 第 3 步——选择编译器（优先 gcc-14）

安装 `gcc-14`（尽力而为，`|| true`），然后当
`command -v gcc-14` 成功时输出 `cc=gcc-14`，否则 `cc=gcc`。
gcc-14 是发布编译器（Debian gcc 14.2.0——发布构建一直使用的
编译器）；回退使工作流在 runner 镜像变化时不硬失败，代价是
略有不同的代码生成——这本身会被第 5 步的产物断言抓住。

### 第 4 步——构建（`make all`）——CI 的 "lint"

<code v-pre>make all CC=${{ steps.cc.outputs.cc }}</code> 运行完整 WP-13
流水线：userprogs → 内核（两阶段链接）→ 双固件 OCBL ISO →
确定性 src zip → OTA 包。lint 就是构建：每个组件 Makefile
（内核、OCIRE、OCBL、OCSE、agent）都带
`-Wall -Wextra -Werror`，因此任何地方的任何警告都会让
`make` 以编译器诊断和非零退出失败。这正是抓住 2.0.0 时代
"本地能编译、CI 上不能"一类问题的门禁（tui_core.c 端口约束
P0）——这也是 SOP 要求本地工具链和 runner 两边都 0 警告的
原因。

### 第 5 步——验证产物

```
file build/opencube.elf            → grep 'ELF 64-bit'
file build/opencube.iso            → grep 'ISO 9660'
test -s build/opencube.elf         (non-empty)
test -s build/opencube.iso
xorriso -indev ... -report_el_torito plain
  → grep 'El Torito boot img : 1 BIOS y none'
  → grep 'El Torito boot img : 2 UEFI y none'
test -s dist/OpenCubeOS-src-3.0.0-NAK.zip
test -s dist/OpenCubeOS-3.0.0-NAK-update.tar.gz
```

**三个硬断言**（每个都 grep 真实产物，因此形状错误的产物
无法通过）：

1. **类型断言**——ELF 必须是 `ELF 64-bit`，ISO 必须是
   `ISO 9660`，且两者都非空。截断或链接错误的产物在这里
   失败。
2. **双固件 El Torito 断言**——xorriso 报告必须同时显示
   `El Torito boot img : 1 BIOS y none` 和
   `: 2 UEFI y none`。这是同一 ISO 同时携带 BIOS 和 UEFI
   启动镜像（OCBL 双固件承诺）的机器检查；仅 BIOS 或仅
   UEFI 的 ISO 即便"构建正常"也在这里失败。
3. **完整性断言**——src zip 和 OTA tar.gz 必须存在且非空
   （流水线没有静默跳过一条腿）。

### 第 6 步——验证 `make clean` 不留产物

```
make clean
left=$(find build dist -type f 2>/dev/null | wc -l)
test "$left" -eq 0
```

`build dist` 下不得残留任何文件。这抓住规则卫生回归：写到
`$(BUILD)`/`$(DIST)` 之外的产物，或 clean 规则遗忘的文件，
都会让 `left > 0` 并使该步失败。

## 3. CI 不运行什么（以及这些门禁住在哪里）

`make test`（宿主机单元测试桩）、每次 QEMU 启动、31 项检查
回归和 E2E 套件从不在 CI 中运行。它们是发布时在本地环境中的
门禁：

- `tools/build_release.sh <ver> --deep`（SOP 第 1–5 步：干净
  构建 → reproducible-check → BIOS+UEFI 启动横幅门禁 → 深度
  门禁 → 文档/网站数据）——启动门禁每次运行 grep 一个全新
  的 mktemp 日志（BUG #63 修复：旧的固定 `/tmp/rel_boot.log`
  让过期日志满足 grep——一个假绿）。
- 验证矩阵（`make test`、`run_fix4_regression.py` 31/31、
  `ssh_g11a_e2e.py` 5 项检查、`run_fix4_https_e2e.py` 8 项
  检查、`wp12_watchdog_logging_e2e.py` S1–S14、`wp10_*` 套件、
  大应用冒烟 21/21）——工具见
  [tool-inventory](tool-inventory.md)，矩阵见
  [process](process.md) 第 4 步。

## 4. 发布链——12 项检查逐条说明

在资产上线之后运行（来自 [process](process.md) §8）。每一项
都是带具体验证动作的通过/失败问题：

1. **两个仓库的远端 HEAD 与本地一致**——对公共仓库和私有
   镜像仓 `git ls-remote` 都显示期望的 SHA（推送确实落地了，
   没有哪里是半更新的）。
2. **tag 存在于公共仓库且指向 CI 绿的提交**——该确切 SHA
   的 Actions 运行为绿；绝不给红色提交打 tag。
3. **每个发布提交的作者身份**——`cubestudio-dev <cubestudio@qq.com>`；否则 `build_release.sh` 拒绝启动。
4. **gh-pages 已更新**——线上站点提供新版本（抓取站点，
   检查版本标记）。
5. **`update.json` 一致性（两份副本）**——version + build +
   `package_sha256` 齐全，且两份副本（website/ 顶层和
   `website/public/`）字节一致。
6. **三方资产哈希**——见下文 §5。
7. **网站数字从 `docs/data/*.json` 再生**——命令/接口/行数
   计数器与新生成的 JSON 一致，绝不用复制来的过期块（2.0.0
   站点就这样发布过一个过期的 130,011 行计数）。
8. **第 4 步门禁全绿**——把真实输出贴进发布证据，跳过计数
   已声明。
9. **令牌卫生**——任何令牌只有最后 4 个字符出现在日志/
   报告中。
10. **全仓库数字一致性**——命令首页 / 分类 / L1SVC 操作 /
    系统调用计数和 stats.json 行数每次发布都重新计算，绝不
    沿用。
11. **私有镜像仓完整**——三个资产 + 发布说明 + 镜像网站
    文件都在镜像仓中（镜像仓规则由维护者在公开文档之外
    维护）。
12. **发布提交和被 tag 的提交都 CI 绿**——同一 SHA 检查
    两次，因为 tag 可能移动。

## 5. SHA256 三方验证

发布说明公布三个资产（ISO、src zip、OTA tar.gz）各自的
SHA256。该检查证明相同字节到达了每个通道：

1. **本地**：对三个 `dist/`/`build/` 产物做 `sha256sum`
   （`build_release.sh` 在门禁化运行结束时打印它们）。
2. **Release API**：经 API 抓取 GitHub Release 资产元数据，
   与平台在上传时计算的摘要比较——抓住截断或错误版本的
   上传。
3. **线上下载**：从用户会使用的公共 URL 下载资产并再次
   哈希——抓住 Release 对象与用户实际所得之间的 CDN/缓存/
   重定向漂移。

每个资产的三方必须相等。同样的纪律延伸到 `update.json`
（OTA 清单携带 `package_sha256`，因此内核自己的更新器在
安装时重新校验）和站点（`website/public/update.json` 与
`website/update.json` 字节一致）。

## 6. 回归下限：最低 276，实测 284

`tools/run_fix4_regression.py` 启动构建出的 ISO（512M、
`-serial mon:stdio`、e1000 用户网络、一个用于帧缓冲断言的
HMP monitor unix socket），等待 `ush>`，运行 24 条命令并为
**31 项检查**打分；退出码等于失败检查数（发布矩阵引用
31/31）。

命令计数检查（31 项中的第 6 项）是**接口冻结下限**：

- **逻辑**：计数从 `help -a` 注册表清单（两空格缩进的
  `name  [tag]  description` 行）动态测量——绝不是过期字面量
  （旧门禁 grep `Total: 233 commands.`，一个在输出中已根本
  不存在的字符串）。
- **下限**：`n >= 276`。276 = g3 基线（275）+ p0leak（随门禁
  搭车的 WP-fix-critical-security P0 栅栏探针，使每个未来
  发布重新证明它）。
- **当前实测**：WP-l1apps-merge 树上 284 条命令——下限比
  实测面低 8，因此新增命令永远没问题，哪怕移除一个也会让
  门禁失败。
- **背后的规则**：命令只能新增，不能移除。当实测计数永久
  上升时，下限字面量在加入命令的同一提交中上调（绝不提前）。

烤进同一门禁的防假绿机制（下限为什么不可能被偶然满足）：

- 每个模式都限定在一条命令的输出节内（BUG #64 修复：模式
  曾匹配整个合并日志，因此命令回显就能满足带名字的模式，
  任何地方的任何 PASS 都能满足每个检查——该门禁在结构上
  恒真）。
- 匹配前剥离回显的命令行。
- 帧缓冲检查要求 HMP screendump 有 ≥ 2 种颜色和可测的前景
  比例（BUG #66）。
- `g6_test` 异步计数有自己的 300 秒等待窗口。

## 7. build_release.sh——带门禁的正门

`tools/build_release.sh <version> [--deep]` 自动化 SOP 第
1–5 步，并在以下情况拒绝运行：tag `<version>` 已存在，或
git 身份不是 `cubestudio-dev`。序列：干净的 `make -j all`
（0 警告门禁）→ `make reproducible-check` → BIOS 启动门禁 →
UEFI 启动门禁（pflash CODE + 可写 VARS 副本；在当前 OVMF
构建上，朴素的 `-bios` 形式会静默不运行载荷）→ 可选
`--deep` 门禁（`run_fix4_regression.py`、
`wp12_watchdog_logging_e2e.py`）→ `gen_api_docs.py --pages --pages-zh --md` + `update_website.sh` + `website/build.sh` →
打印三个 SHA256。它不推送任何东西——审查输出，然后手动
运行发布链（有意的不对称；推送通道操作手册由维护者在
公开文档之外维护）。

较老的 `tools/github_release_wp08.sh`（WP-08 时代）推送
commit+tag 并经 API（带 repo + workflow 权限的
`GITHUB_TOKEN`）创建带 ISO 资产的 Release——为历史保留；
现代流程从门禁化产物出发手工创建 Release。
