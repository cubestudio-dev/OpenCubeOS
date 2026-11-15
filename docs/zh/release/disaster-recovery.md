<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布部署与灾备（维护者手册）

产物如何到达世界、沙盒重置之后工作环境如何重建、以及沙盒的
出口断掉时如何推送。配套页面：[process](process.md)（SOP）、
[ci-gates](ci-gates.md)（门禁）、[website-update](website-update.md)
和 [wiki-sync](wiki-sync.md)（两个同步机制的深入剖析）。

## 1. 部署面

| 通道 | 内容 | 机制 |
|---------|---------|-----------|
| GitHub 仓库 `main` | 事实来源 | git push（或 §6 的 API 重放通道） |
| GitHub Releases | ISO + src zip + OTA tar.gz + 带三个 SHA256 的说明 | 从打好 tag 的 CI 绿提交经 Release UI / API |
| `gh-pages` 分支 | 网站：门面 + 文档中心 + OTA 清单 | `website/build.sh` 输出推送到该分支 |
| 自定义域名 | `ocos.cubestudio.cn` | 部署根中的 CNAME（GitHub Pages cname） |
| GitHub Wiki | 精选的双语文档视图 | `tools/sync_wiki.sh` |
| 私有镜像仓 | 资产 + 发布说明 + 镜像网站文件 + 审计文档 | 仓对仓复制（见下） |

## 2. 双仓同步规则

项目保持一个公共仓库（`cubestudio-dev/OpenCubeOS`）和一个
私有镜像仓。镜像规则（规范表述是 [process](process.md) §7 +
镜像仓自己的 SYNC-RULES.md）：

1. **同步什么**：三个发布资产、发布说明、镜像网站文件和
   仓库级审计文档（postcheck 报告、工作日志）。镜像仓是有案
   可查的审计轨迹——每次发布都在那里归档一份完整 postcheck。
2. **不同步什么**：构建产物（`build/`、`dist/`、`iso/`、
   `releases/`）和网站构建树（`website/out`、
   `website/.next`、`website/node_modules`、
   `website/public/downloads`）——与 `Makefile` 的
   `SRC_EXCLUDE` 施加于源 zip 的同一排除哲学。镜像仓持有源、
   证据和资产，绝不含瞬态构建状态。网站*输出*经 gh-pages
   到达用户，不经镜像仓。
3. **门禁**：对两个仓库 `git ls-remote` 都显示期望的 HEAD
   （12 项检查第 1 条；见 [ci-gates](ci-gates.md) §4）。
   还原进镜像仓的资产对照 Release 做 SHA 校验（2.0.0 资产
   就是从某个 Release 重新抓取并这样哈希核验的）。
4. **字节一致承诺**：Release、gh-pages 下载和镜像仓持有
   每个资产的字节一致副本（P-11 教训：公共 main 树也必须在
   `website/public/downloads/` 下携带它们）。

## 3. 沙盒重置——标准恢复流程

规范的沙盒布局（工具链所假定的那个）：`/home/z/ocos` =
仓库，`/home/z/opt/extract` = 迁移式工具链，
`$HOME/.cache/opencube-cc` = 编译缓存。沙盒重置之后按此
顺序重建环境：

1. **还原仓库**——克隆（或从 bundle 重放，§5）到
   `/home/z/ocos`。确认工作树干净且在期望分支上；构建 id
   推导需要完整历史（`git rev-list --count HEAD`），因此
   绝不还原浅副本。
2. **解开工具链（无需 root）**——
   `bash tools/install-toolchain.sh`：分三批下载 Debian .deb
   （构建工具 / qemu / grub+固件，含依赖约 117 个包）并
   `dpkg -x` 解到 `$OC_OPT`（默认 `/home/z/opt/extract`）：
   nasm 2.16.03、qemu-system-x86 10.0.13（+ 数据 + 库）、
   xorriso 1.5.6、grub-pc-bin + grub-efi-amd64-bin +
   grub-common、mtools、ovmf、seabios。gcc 本身来自系统
   （Debian gcc 14.2.0 是发布编译器）。
3. **加载环境**——`source tools/env-sandbox.sh`
   （仓库的沙盒环境脚本）：导出
   `OC_TOOLS=/home/z/opt/extract`、把 `$OC_TOOLS/usr/bin`
   前置到 PATH、把 `LD_LIBRARY_PATH` 设为 extract 的库目录
   （迁移后的 QEMU 需要它们）。每个驱动 QEMU 的工具也尊重
   `OC_TOOLS`。
4. **QEMU/OVMF 位置检查**——`OVMF_FD` 默认为
   `$(OC_TOOLS)/share/ovmf/OVMF.fd`，`QEMU_DATADIR` 默认为
   `$(OC_TOOLS)/share/qemu`，`SEABIOS_DIR` 默认为
   `$(OC_TOOLS)/share/seabios`。对新构建的 ISO 用
   `make run-bios` 验证；若 runner 的 OVMF 构建需要拆分的
   pflash 对，`build_release.sh` 的 UEFI 门禁使用
   `OVMF_CODE_4M.fd` + `OVMF_VARS_4M.fd`（在当前 OVMF 构建
   上，朴素的 `-bios` 会静默不运行载荷）。
5. **证据基线**——还原/重建门禁消费的 `evidence/` 抓取：
   `evidence/help_a_runtime.txt`（`make api-docs` 背后的
   运行时 `help -a` 抓取）和发布门禁日志。然后跑便宜的
   验证阶梯：`make clean && make all`（0 警告门禁）→
   `make reproducible-check` → `make run-bios` /
   `make run-uefi` 横幅门禁。完整恢复由
   `tools/build_release.sh <ver> --deep` 端到端通过来证明。
6. **编译缓存**——可选；擦除或保留
   `~/.cache/opencube-cc`（`make cache-clear`）；命中字节
   一致，因此缓存永不让门禁失效。

## 4. 网站更新流程

`website/build.sh`（作为 `build_release.sh` 的最后一步运行，
在 `tools/update_website.sh` 写完发布数字之后）：

1. 一次性安装文档中心依赖（`npm install` → vitepress）。
2. 经 `rsync -a --delete`（排除 `archive/` 和 `data/`）把
   仓库 `docs/` 树同步进构建期副本 `docs-src/docs/`——仓库
   的 docs/ 是唯一事实来源；该副本被 git 忽略。
3. 构建文档中心（`npm run docs:build`；VitePress，EN 根 +
   `zh/` 区域，按区域的搜索索引；约 642 页，
   WP-l1apps-merge 全量重建时 777 页）。
4. 组装部署根 `out/`：门面页
   （`index/architecture/download/about.html`）、`assets/`、
   `out/docs/`，外加三个费工夫才到位的部署关键文件：
   - `out/CNAME`——内容 `ocos.cubestudio.cn`（自定义域名；
     此前只在 gh-pages 分支上，因此全树重部署可能静默丢掉
     它——WP06d-site_fix），
   - `out/.nojekyll`——空文件；阻止 GitHub Pages 对
     VitePress 输出运行 Jekyll，
   - `out/update.json` + `out/update-v2.json`——由
     `update_website.sh` 写入的 OTA 清单（发布管理的内容），
     复制过去使内核的 checkupdate 和下载页页脚链接永不
     404。
5. `out/` 树提交到 `gh-pages` 并推送（通道见 §5–§6）。
   Pages 重建；在线验证（留一分钟）：文档中心、一个已知的
   深层页（与本地构建字节比较）、门面的版本标记。唯一容忍
   的线上与本地差异是 CDN 的邮箱混淆注入。

`update_website.sh <ver> <build> [date] [changes]` 必须在
构建之前运行：它读 dist 资产并写
`website/assets/site-data.js` + `website/update.json` +
`update-v2.json`——站点上没有任何东西是手工编辑的（细节见
[website-update](website-update.md)）。

## 5. Wiki 同步

`tools/sync_wiki.sh [--dry-run]`（环境：带 wiki 推送权限的
`GH_TOKEN`）把 `docs/` + `docs/zh/` 映射到 GitHub Wiki
（`cubestudio-dev/OpenCubeOS.wiki.git`）：

- 中文是映射名之下的规范页；英文作为 `<Page>-EN.md` 发布；
  每个页都带链接到其对应页的语言横幅。
- `map_page()` 表就是契约：约 83 个映射的 Wiki 页名
  （Home、Getting-Started、Mega-Apps、Boot-Modes、
  Shell-Basics、Filesystems、Networking、Update-and-OTA、
  TUI-Guide、Chinese-Console、Recovery、User-Manual、
  Command-Reference、Building-from-Source、Architecture-*、
  L1-*、Drivers-*、Internals-*、Troubleshooting-*、
  Versioning、Release-Process、…）加上
  `Release-Notes = GENERATED` 哨兵。不在表中的页只能经文档
  中心到达——Wiki 是精选视图，文档中心是全树。
- 链接按语言改写为 `[[Wiki-Links]]`（zh → 规范名，en →
  `-EN` 后缀），目录链接折叠为分节索引页，锚点被丢弃（zh
  标题已本地化），相对资源被剥离。
- 发布时在 docs/ 变更落地之后运行，docs-only 更新时按需
  运行。验证：每个映射页存在、映射集合中无红链，且脚本在
  推送前对 Wiki 与 docs 做差异（见
  [wiki-sync](wiki-sync.md)）。

## 6. 受限出口的推送通道（api.github.com 重放）

**故障**：在发布沙盒中，smart-HTTP 上的 `git push` 持续死于
GnuTLS 错误（-110，连接被终止 / TLS 记录溢出一类）。因此
git 通道恰好在必须发布的时候不可用。

**一线绕行**（尽量继续用 git）：
`deploy_with_token.sh <GH_TOKEN>`——全新 `git init` 草稿仓、
`git config http.version HTTP/1.1`、抓取远端基准 ref、从瘦
`*.bundle` 文件抓取准备好的提交（bundle 只携带新提交；
前置条件 = 当前远端尖端，因此这些推送都是快进）、推送
`main` + `gh-pages`，然后经 REST API PATCH Release 正文。
不需要仓库的本地克隆。当连这个推送也死掉时（观察到的
情况），落到：

**API 重放通道——`api_push3.py`**（构建在 Git Data API 之上
的分层分块累积树构建器）：

```
python3 api_push3.py <repo-path> <slug> <token> <branch> <base> <head> \
    [--threads 6] [--chunk 400] [--state file.tsv]
```

它为什么存在（两个替代方案都失败）：
- 平坦的全树 POST 在约 2600 条的载荷上被 GitHub 422 拒绝
  （"request timed out / input too large"）——不可重试；
- `base_tree` + null-sha 删除会静默保留 null-sha 条目（在
  wiki/部署工作中观察到）。

它对每个被重放的提交如何工作：
1. **先做基准校验**：远端分支头的树 SHA 必须等于本地
   `<base>` 的树 SHA——不匹配即以
   `FATAL: ... replay base mismatch` 中止（exit 2），因此
   重放永远不会落在未知的远端状态之上。
2. **变更文件的多线程 blob 上传**：内容 SHA 已存在于远端树
   的任何位置时跳过；否则 POST `/git/blobs`（base64）；校验
   返回的 SHA 等于本地（`blob sha drift` 致命）；进度和完成
   情况在锁下追加到 `--state` TSV——中断之后的可续跑检查点。
3. **脏目录树重建，最深优先**：每个脏目录从其最终子集在 ≤
   `--chunk`（默认 400）次累积遍历中重建（第一遍朴素、后续
   遍历 `base_tree=<prev> + 下一块`）。删除永不出现，因为
   子集是最终的。**每个目录得到的树 SHA 都与本地 git 树
   SHA 硬校验**——任何层级不匹配即中止（`FATAL: dir '<d>' tree <sha> != local <sha>`，exit 3）。
4. **根树校验 == 本地提交的树 SHA**，然后创建提交并保留
   作者 + 提交者（名字、邮箱、ISO 日期——父链匹配时是位
   精确的重放）。
5. **ref 更新**：`PATCH /git/refs/heads/<branch>`，带
   `force: false`——仅快进。重试策略：502/503/504/429 带
   退避重试，其余一律致命并附响应体。

已知的坑（都已在工具中修复或记录）：
- 约 2600 条的全树 POST → 422（api_push2.py，平坦版本，仅
  作为该失败的档案保留）；
- 打印锁不可重入 → 工作线程内的嵌套日志调用死锁整个运行；
  现在是 `RLock`；
- 中断的运行从 state 文件续跑——运行中绝不删除它，且总是
  用同一 `--state` 路径重跑。

已记录的使用（WP-l1apps-merge 收尾，2026-11-15）：`main`
（树 `e964148f`）和 `gh-pages`（树 `3cd102cb`）都经此方式
重放并在线验证——文档中心、大应用页、发布页与本地构建字节
一致；截图归档在部署套件的 `online-proof/` 之下。

## 7. 持久灾备目录

沙盒会重置；有些东西必须活得比它久。约定：一个不在沙盒
重置范围内的 `download/` 目录持有发布生存套件：

- **bundle**：`main-new.bundle` / `gh-pages-new.bundle`——
  只携带新提交的瘦 git bundle。若远端基准没有动，它们可
  直接重放（`git fetch <bundle> <branch>` 后推送，或部署
  脚本的流程）。
- **部署脚本本身**：`deploy_with_token.sh`（git 通道）、
  `api_push2.py`（平坦树档案）、`api_push3.py`（在用的
  通道）。
- **状态 + 日志**：`state-*.tsv`（blob 续跑检查点）、
  `push-*.log`（执行日志）——与写出它们的工具放在一起。
- **发布正文**：`release-body.md`——确切的说明文本，因此
  无需重新构造即可再创建或再 PATCH 一个 Release。
- **证据**：`online-proof/` 截图和 `preview-*.png`——交付
  报告的"已上线"记录。

目录的规则：它只镜像发布链产出的东西（资产住在
Releases/gh-pages，不在这里）；每个文件都可从仓库 + Release
再造，但保留它把 2 小时的恢复变成 10 分钟的。每次发布之后
刷新套件（提交动了就换新 bundle、新 state 文件、新证明），
并清剪早于最近两次发布的任何东西。

## 8. 恢复清单（一页）

```
# environment
bash tools/install-toolchain.sh          # once; $OC_OPT=/home/z/opt/extract
source tools/env-sandbox.sh              # OC_TOOLS + PATH + LD_LIBRARY_PATH

# prove the tree
cd /home/z/ocos && git status            # clean, full history
make clean && make -j all                # 0-warning gate
make reproducible-check                  # byte-identical ELF
make run-bios && make run-uefi           # banner gates

# release data + site
tools/update_website.sh <ver> <build> <date>
website/build.sh                         # out/ = deploy root

# push (pick the channel that works)
git push origin main                     # normal
./deploy_with_token.sh $GH_TOKEN         # bundle + HTTP/1.1 + API PATCH
python3 api_push3.py <repo> <slug> $GH_TOKEN main <base> <head> --state s.tsv

# verify
tools/sync_wiki.sh
# ...then the 12-point check (ci-gates.md §4)
```
