<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布流程（SOP）

一次发布走过的确切序列。每一步都有门禁；门禁失败即停止
发布。

## 0. 预检

```
git fetch && git status        (clean tree, correct branch)
git tag --list 'v*'           (the new tag does not exist yet)
git config user.name / user.email   (author = cubestudio-dev <cubestudio@qq.com>)
```

## 1. 构建

```
make clean && make -j$(nproc) all
```

门禁：0 错误 0 警告（`-Wall -Wextra -Werror` 在参数里）
**本地工具链和 GitHub Actions runner 两边都要满足**——2.0.0
发布过本地能编译、CI 上不能的源码（tui_core.c 端口约束 P0；
见 KNOWN_ISSUES.md）。CI 绿是硬发布门禁，不是锦上添花。
产物：`build/opencube.iso`、`dist/OpenCubeOS-<ver>-src.zip`、
`dist/OpenCubeOS-<ver>-update.tar.gz`。

## 2. 可复现性

```
make reproducible-check
```

门禁：`PASS - ELF byte-identical across two clean builds`。

## 3. 启动门禁

```
make run-bios      (banner + ring-3 ush> + boot complete)
make run-uefi      (same, through OVMF)
```

默认启动落进 Ring-3 的 `ush>` shell；2.x 的 `oc>` 提示符在
`exit` 之后出现（或启动器失败时出现——恢复姿态）。

## 4. 深度测试门禁（针对构建出的 ISO）

2.1.0 验证矩阵——每个套件都在串口控制台上驱动一次真实的
QEMU 启动；把真实输出（含声明的跳过计数）贴进发布证据：

| 门禁 | 命令 | 证明什么 |
|------|---------|----------------|
| 宿主机单元测试 | `make test` | 17 个测试桩 PASS（无 openssl 夹具时 +2 SKIP-FIXTURE——声明跳过） |
| 看门狗/日志/健康 E2E | `tools/wp12_watchdog_logging_e2e.py` | 14 个场景（S1-S14，含 Intel TCO 真实复位） |
| TUI 命令面 E2E | `tools/wp10_tui_e2e.py` | v2 启动流上的 L0 TUI 命令 |
| 中文控制台 E2E | `tools/wp10_chinese_e2e.py` | CJK 层端到端 |
| TUI 桌面应用 E2E | `tools/wp10_tuiqa_apps_e2e.py` | 从 `ush>` 出发的桌面五应用流 |
| 原生桌面 E2E | `tools/wp10_native_e2e.py` | 原生编辑器/文件管理器流程 |
| L1 逐应用记录 | 从 ush 经 `run` 运行每个 `l1apps/` 程序 | ush + 每个内嵌程序真实执行（2.1.0 新增；见 docs/dev/e2e-suites.md） |

## 5. 打 tag + 发布

```
git tag -a v<version>          # ONLY on a CI-green commit
git push origin main --tags
```

从该 tag 创建 GitHub Release，说明中包含：变更摘要、
新接口、已知问题、三个 SHA256。附上三个资产。发布脚本
（`tools/build_release.sh --deep`）自动完成清单 + 说明 +
上传。

**发布门禁（2.1.0）：tag 必须指向 CI 绿的提交。
绝不给红色提交打 tag。**

## 6. 网站 + wiki

```
tools/update_website.sh    (versions/hashes/downloads on the site)
tools/sync_wiki.sh         (docs/ -> the GitHub Wiki)
```

门禁：线上站点显示新版本，三个 SHA256 与 Release 一致，
下载解析为 HTTP 200 且哈希校验通过，且站点统计块从
`docs/data/*.json` 重新生成（绝不从过期块复制——2.0.0 的
站点就这样带着过期的 130,011 行数上线过）。

## 7. 十二点检查

1. 公开仓库的远端 HEAD 与本地一致。
2. tag 存在于公开仓库且指向 CI 绿的提交。
3. 每个发布提交都有作者身份。
4. gh-pages 已更新（线上站点服务新版本）。
5. update.json：version + build + package_sha256（两份
   副本——website/ 顶层 AND website/public/——逐字节一致）。
6. 三方资产哈希：本地 = Release API 摘要 = 线上
   下载。
7. 网站数字（命令/接口/行数）与
   `docs/data/*.json` 一致（重新生成，而非复制）。
8. 第 4 步的门禁全绿（贴出输出，声明
   跳过计数）。
9. 令牌卫生（只显示过末四位）。
10. 全仓数字一致性：311 条命令（300 个 Ring-3 + 11 个
    内建）/ 20 个类别 / 221 个 L1SVC 操作 / 54 个系统调用 /
    当前的 stats.json 行数（每次发布重新计算；绝不用过期
    数字）。
11. 发布提交（以及打 tag 的提交）CI 绿。

## 9. 报告

交付报告引用：构建证据（本地 + CI）、全部门禁
输出、推送输出（公开 + 私有）、Release URL、站点
URL、三个 SHA256，以及诚实偏差清单。
