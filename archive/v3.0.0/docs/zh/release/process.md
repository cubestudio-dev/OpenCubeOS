<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布流程（SOP）

发布走过的确切序列。每一步都有门；门失败即停。

## 0. 预检

```
git fetch && git status        （干净树、正确分支）
git tag --list 'v*'            （新 tag 尚不存在）
git config user.name / user.email   （作者 = cubestudio-dev <cubestudio@qq.com>）
```

## 1. 构建

```
make clean && make -j$(nproc) all
```

门：0 错误 0 警告（`-Wall -Wextra -Werror` 在编译参数里）
**且本地工具链与 GitHub Actions runner 双双通过**——2.0.0 发布过
本地能编译、CI 编不过的源码（tui_core.c 端口约束 P0；见
KNOWN_ISSUES.md）。CI 绿是硬性发布门，不是锦上添花。
产出：`build/opencube.iso`、`dist/OpenCubeOS-<ver>-src.zip`、
`dist/OpenCubeOS-<ver>-update.tar.gz`。

## 2. 可复现

```
make reproducible-check
```

门：`PASS - ELF byte-identical across two clean builds`。

## 3. 启动门

```
make run-bios      （横幅 + Ring-3 ush> + boot complete）
make run-uefi      （同上，走 OVMF）
```

默认启动落在 Ring-3 的 `ush>`；`exit` 之后（或启动器失败时）出现
`oc>`——恢复姿态。

## 4. 深度测试门（在构建出的 ISO 上）

2.1.0 验证矩阵——每套都驱动真实 QEMU 启动、走串口控制台；把真实
输出（含 skip 数）贴进发布证据：

| 门 | 命令 | 证明什么 |
|----|------|----------|
| Host 单测 | `make test` | 17 项 PASS（无 openssl fixture 时 +2 SKIP-FIXTURE——如实申报 skip） |
| 看门狗/日志/健康 E2E | `tools/wp12_watchdog_logging_e2e.py` | 14 场景（S1-S14，含 Intel TCO 真复位） |
| TUI 命令面 E2E | `tools/wp10_tui_e2e.py` | v2 启动流上的 L0 TUI 命令 |
| 中文控制台 E2E | `tools/wp10_chinese_e2e.py` | CJK 层端到端 |
| TUI 桌面应用 E2E | `tools/wp10_tuiqa_apps_e2e.py` | 从 `ush>` 起步的桌面五应用流程 |
| 原生桌面 E2E | `tools/wp10_native_e2e.py` | 原生编辑器/文件管理器流程 |
| L1 逐应用记录 | 从 ush 经 `run` 跑每个 `l1apps/` 程序 | ush + 每个嵌入程序真实执行（2.1.0 新增；见 docs/dev/e2e-suites.md） |

## 5. Tag + Release

```
git tag -a v<版本>            # 只打在 CI 绿的提交上
git push origin main --tags
```

从 tag 建 GitHub Release，说明含：变更摘要、新接口、已知问题、
三个 SHA256。附三份资产。发布脚本（`tools/build_release.sh --deep`）
自动完成清单 + 说明 + 上传。

**发布门（2.1.0）：tag 必须指向 CI 绿的提交。绝不在红的提交上打
tag。**

## 6. 网站 + wiki

```
tools/update_website.sh    （版本/哈希/下载）
tools/sync_wiki.sh         （docs/ -> GitHub Wiki）
```

门：线上站点显示新版本、三个 SHA256 与 Release 一致、下载 HTTP 200
且哈希校验通过、站点 stats 块**从 `docs/data/*.json` 再生成**
（绝不从旧块照搬——2.0.0 的站点就是那样带着陈旧的 130,011 行数
发出去的）。

## 7. 私密镜像

把三份资产 + 发布说明 + 镜像的网站文件拷进私密仓（`oc-os/` 树 +
仓级 audit 文档）；推送。门：两个仓的 `git ls-remote` 显示预期的
HEAD。

## 8. 12 点检查

1. 两仓远端 HEAD 与本地一致。
2. tag 在公开仓存在且指向 CI 绿的提交。
3. 每个发布提交的作者身份。
4. gh-pages 已更新（线上站点服务新版本）。
5. update.json：version + build + package_sha256（双副本——
   website/ 顶层与 website/public/——字节一致）。
6. 三方资产哈希：本地 = Release API digest = 实际下载。
7. 网站数字（命令/接口/行数）与 `docs/data/*.json` 一致（再生成，
   不是照搬）。
8. 第 4 步的门全绿（贴输出，申报 skip 数）。
9. token 卫生（只显示 last-4）。
10. 全仓数字一致性：251 命令归属（99+133+19）/ 17 类 / 158 个
    L1SVC 操作 / 54 syscall / 当前 stats.json 行数（每次发布重算；
    绝不带陈旧数字）。
11. 私密镜像完整。
12. CI 在发布提交（及被 tag 的提交）上绿。

## 9. 报告

交付报告引用：构建证据（本地 + CI）、全部门输出、推送输出（公开 +
私密）、Release URL、站点 URL、三个 SHA256、以及诚实偏差清单。
