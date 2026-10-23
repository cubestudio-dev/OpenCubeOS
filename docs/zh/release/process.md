<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布流程（SOP）

发布所遵循的确切流程。每一步都有一个门禁；失败的门禁会中止发布。

## 0. 发布前检查

```
git fetch && git status        (clean tree, correct branch)
git tag --list 'WP-*'          (the new tag does not exist yet)
```

## 1. 构建

```
make clean && make -j$(nproc) all
```

门禁：0 个错误 0 个警告（标志中包含 `-Wall -Wextra -Werror`）。
输出：`build/opencube.iso`、`dist/OpenCubeOS-<ver>-src.zip`、`dist/OpenCubeOS-<ver>-update.tar.gz`。

## 2. 可复现性检查

```
make reproducible-check
```

门禁：`PASS - 两次干净构建的 ELF 字节完全一致`。

## 3. 启动门禁

```
make run-bios      (banner + 233 commands + oc> + boot complete)
make run-uefi      (same, through OVMF)
```

## 4. 深度测试门禁（在构建的 ISO 上）

| 门禁 | 命令 | 必须满足 |
|------|---------|------|
| 回归测试套件 | `tools/run_fix4_regression.py` | 28/28 |
| 看门狗/日志/健康监控端到端测试 | `tools/wp12_watchdog_logging_e2e.py` | 14/14 |
| dhtest + cryptotest | 在客户机内 | PASS |
| SSH 端到端测试 | paramiko 套件（已设置种子的 /etc） | 5/5 |
| HTTPS 端到端测试 | 参考服务器 | 7/7 |

## 5. 标签 + 发布

```
git tag WP-<stage>
git push origin main --tags
```

从标签创建 GitHub Release，注释中包含：变更摘要、新接口、已知问题、三个 SHA256。附加三个资源。发布脚本（`tools/build_release.sh`）自动完成清单 + 注释 + 上传。

## 6. 网站 + Wiki

```
tools/update_website.sh    (versions/hashes/downloads on the site)
tools/sync_wiki.sh         (docs/ -> the GitHub Wiki)
```

门禁：实时网站显示新版本，三个 SHA256 与 Release 匹配，下载解析为 HTTP 200 且通过哈希验证。

## 7. 私有镜像

将三个资源 + 发布注释 + 镜像的网站文件复制到私有仓库；推送。门禁：两个仓库上的 `git ls-remote` 显示预期的 HEAD。

## 8. 12 点检查清单

1. 两个仓库的远程 HEAD 与本地匹配。
2. 标签存在于公共仓库。
3. 每个发布提交的作者身份。
4. gh-pages 已更新（实时网站提供新版本）。
5. update.json：版本 + 构建 + package_sha256。
6. 三方资源哈希：本地 = Release API 摘要 = 实时下载。
7. 网站数字（命令/接口/行数）与 `docs/data/*.json` 匹配。
8. 步骤 4 中的所有门禁均为绿色（粘贴输出）。
9. Token 卫生（仅显示后 4 位）。
10. 整个仓库数字一致性（233 / 162 / 140,672 级）。
11. 私有镜像完成。
12. 发布提交的 CI 为绿色。

## 9. 报告

交付报告引用：构建证据、所有门禁输出、推送输出（公共 + 私有）、Release URL、网站 URL、三个 SHA256，以及诚实偏离列表。
