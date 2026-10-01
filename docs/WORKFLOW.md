<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — 固定交付流程（每批次必走）

> 本文档是**硬性流程**。每个开发批次（新功能 / 修复 / 文档 / 网站更新）都必须
> 按以下顺序走完全程，缺一不可。所有输出必须真实执行、原样粘贴，禁止编造。

---

## 0. 行为规范（七条硬性，优先级高于一切）

1. 每条命令一次只做一件事。
2. 输出原样贴、不解读。
3. 不许"顺手补全"——只做规格内的事；发现其他问题如实记录，另行立项。
4. 不确定就说"我不确定"。
5. 没跑就说"我没跑"。
6. 发现自己在编造，立即停下承认。
7. **不要只满足最低限度，而是主流可用。**

**第 ⑦ 条说明：**

- 不是"能跑通就行"，是"能兼容主流"。
- 不是"能过测试就行"，是"能连主流服务器"。
- 不是"最小可用"，是"主流可用"。
- 如果只能做到"最低限度"，不许说"完成"，要说"这是最低限度，主流可用需要 XXX"。
- 不许用"设计限制"包装。

**语言要求：所有回复一律用中文。**

---

## 1. 制作前

沙箱会重置（git config 丢失、clone 落后、工具链丢失），所以每次制作前必须：

1. `git fetch origin`
2. `git log --oneline HEAD..origin/main` — 查看远程是否有新 commit
3. 如果有新 commit：`git pull origin main`
4. `git config user.name` — 必须是 `cubestudio-dev`
5. `git config user.email` — 必须是 `cubestudio@qq.com`
6. 如果不是：`git config user.name "cubestudio-dev"` &&
   `git config user.email "cubestudio@qq.com"`（修复后再动手）
7. 检查工具链（新沙箱）：
   - 工具链丢失时用私密仓 `tools/install-toolchain.sh` 重建（117 个包，
     apt-get download + dpkg-deb -x，无 sudo）；
   - `source /home/z/opt/env.sh`（env.sh 持久化于私密仓 `tools/env.sh`）；
   - `make iso` 需 `OC_TOOLS=/home/z/opt/extract`。

## 2. 制作中

1. 逐个修 bug / 逐项实现，不做批间跳跃。
2. 每修完一个：编译（0 errors 0 warnings）+ QEMU 实测 + **贴实际输出**。
3. 不许"顺手补全"。
4. **每个 bug 修复后，必须加回归测试**（回归清单须覆盖所有已知 bug；
   审查发现而测试漏掉的 bug，修复时要补上能抓住它的测试项）。

## 3. 提交前

1. `git config user.name`
2. `git config user.email`
3. 确认是 `cubestudio-dev` / `cubestudio@qq.com`，否则回到第 1 节修复。

## 4. 提交后

1. `git log -1 --format="%H %an %ae"`
2. 确认 author 是 `cubestudio-dev <cubestudio@qq.com>`（历史残留名如
   `Z User` / `Cube Studio` 均不允许）。
3. 如果不是：`git commit --amend --reset-author`（config 正确时）后
   `git push --force`。
4. 有代码变更时创建 Release（tag 规范 `WP-XX-batch-N` / `WP-XX-fixN`），
   **每个 Release 必须有资产（ISO + SRC zip 两个文件）**，无资产的
   Release 视为未交付。
5. 同步私密仓 `archive-repo`（源码镜像 + worklog 五段格式追加），
   commit + push 后用 `git log --oneline origin/main -5`（私密仓）验证
   远程包含新 commit。
6. `git push origin main`，**贴出实际输出**。

### 批量检查（每批次完成后，覆盖 author + committer）

1. `git log --format="%H %an <%ae>" | grep -v "cubestudio-dev <cubestudio@qq.com>"`
   （注意：必须用 `%an <%ae>` 带尖括号的格式，否则 grep 模式匹配不到任何行，
   `grep -v` 会误报全部行；committer 同法再查一遍：`%cn <%ce>`。）
2. 如果有残留：条件性 `git filter-branch` 修复 —— 只改命中残留名的
   author + committer，其他 commit 元数据不动：
   ```
   FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --env-filter '
   if [ "$GIT_AUTHOR_NAME" != "cubestudio-dev" ] || [ "$GIT_AUTHOR_EMAIL" != "cubestudio@qq.com" ]; then
     export GIT_AUTHOR_NAME="cubestudio-dev"; export GIT_AUTHOR_EMAIL="cubestudio@qq.com"
   fi
   if [ "$GIT_COMMITTER_NAME" != "cubestudio-dev" ] || [ "$GIT_COMMITTER_EMAIL" != "cubestudio@qq.com" ]; then
     export GIT_COMMITTER_NAME="cubestudio-dev"; export GIT_COMMITTER_EMAIL="cubestudio@qq.com"
   fi
   ' --tag-name-filter cat -- --branches --tags
   ```
3. tag 指向被改写 commit 时自动重指（`--tag-name-filter cat` 已处理；
   lightweight tag 直接重指，annotated tag 重建对象）。本地与远程用
   `git ls-remote origin refs/tags/<TAG>` vs `git rev-parse <TAG>` 抽查一致。
4. `git push --force-with-lease origin main`；tags 因无 remote-tracking ref
   会报 `stale info`（fetch 又会 `would clobber existing tag`），故对 tags
   用 `git push origin --tags --force`（两步输出均需贴出）。
5. 清理 filter-branch 备份：`git for-each-ref --format="%(refname)" refs/original
   | while read r; do git update-ref -d "$r"; done`（不清会导致 `--all`
   终查误报残留）。

## 5. 完整检查（提交/发布前必须全部通过，按顺序执行并留存实际输出）

1. **隐私扫描（公开仓 SRC zip）**
   - 按 `Makefile dist` 排除规则打包，扫描：SSH 密钥 / PAT / 服务器地址 /
     本地绝对路径 / worklog 等内部文档 / 临时文件 / 调试代码 / 测试脚本。
   - 命中即修（或排除出发布包），修完重扫，直到零命中。
2. **编译 0 errors 0 warnings**
   - `make clean && make`（`-Wall -Wextra -Werror` 下零输出）；
   - `make iso` 生成 ISO，记录字节数与 SHA256。
3. **回归测试 18/18**（单次 QEMU 会话）：boot 横幅 + `uname -a` + 12 个
   用户程序（hello / fork_test / exec_test / pipe_test / signal_test /
   select_test / mmap_test / dyn_hello / so_test / dlsym_test / pie_test /
   reloc_test）+ p3_test + heaptest + l1test + crashlog（3 条 #DE/#UD/#PF）。
4. **dhtest 5/5 + cryptotest 3/3**。
5. **SSH 双向**（paramiko ↔ 内核 sshd；内核 ssh → paramiko server）。
6. **HTTPS E2E**（内核 TLS 客户端 ↔ tools/https_test_server.py）。
7. **三处 SHA256 一致**：本地 build / GitHub Release 资产（下载回读）/
   线上网站 `/downloads/`（下载回读），字节级一致。注意：ISO 每次重打
   SHA 都变（xorriso 时间戳非确定性），以最终交付构建为准，构建后不得再动。
8. **协议检查**：LICENSE（Apache-2.0）+ NOTICE 存在且版权行为
   `cubestudio-dev <cubestudio@qq.com>`；kernel/ boot/ userprogs/ 的 SPDX
   头覆盖 100%。
9. **所有 Release 有资产**：GitHub API 列出全部 Release，逐个核对
   `assets >= 2`（ISO + SRC zip），发现无资产 Release 立即补传或删除。
10. **文档一致性**：README.md / docs/STATUS.md / docs/KNOWN_ISSUES.md 与
    代码行为一致（命令数、统计行数、FIXED/OPEN 状态、行为声明与实测相符）。
    **命令注册检查**：shell `help` 列出的每条命令必须真实注册——有名无实
    的命令要么注册、要么从 help 删除，两条路都必须实测验证。
11. **扩展接口**：`l1test` PASS；docs/EXTENSIONS*.md 与 kernel/ext.h 一致。
12. **OPEN bug 追踪**：所有未修复 bug 必须记录在 `docs/KNOWN_ISSUES.md`，
    每条含：**位置（文件/函数）、复现方法、当前状态**；无追踪的 OPEN bug
    视为遗漏。

## 6. 制作后

1. `git log --oneline origin/main -5` — 确认 origin/main 包含新 commit。
2. **更新网站**：`website/lib/site.ts`（SHA256 / 大小 / 版本号 / 统计）+
   `lib/i18n.ts`（badge 版本）+ 替换 `website/public/downloads/` 两个下载
   文件（用第 5 节最终构建产物，不得重新构建）。
3. `bash website/build-local.sh` 构建；`bash website/rw2_ghpages.sh` 推送
   gh-pages，贴出实际输出。
4. **验证网站**：实测线上 URL（至少 /、/zh/、/en/、/zh/docs/、/en/docs/、
   两个 /downloads/ 文件）全部 HTTP 200，页面显示版本号与 SHA256 与
   Release 一致（下载回读比对）。
5. **同步私密仓库**：rsync 源码 + 资产 + worklog 追加，commit + push；
   `git log --oneline origin/main -5`（私密仓）验证远程包含新 commit。

## 7. 报告（最后一步，做完停等用户确认）

1. 贴出所有实际输出（命令原样回显，不解读、不省略）。
2. 贴出所有 Release URL。
3. 贴出网站 URL。
4. 贴出三处 SHA256 对照表。
5. 贴出完整检查结果（第 5 节 12 项逐项：通过 / 未通过 / 未跑）。
6. **诚实说明**：哪些没跑、哪些不确定、哪些曾经编造过（如无则明说"无"）。
7. **做完，停，等用户确认。**

---

## 历史备注

- 2026-10-01：第 5 节（原"完整检查"）由用户指令新增——33-bug 重审修复期间
  发现"编译过 + 服务器起"不等于"网站/发布物可用"，故将隐私扫描、回归、
  三方 SHA 对照、协议与文档一致性列为固定检查项。
- 2026-10-02：第 1/3/4 节（git config 与同步检查）与第 5 节第 12 项由用户
  指令新增——发现 14 个（公开仓）+ 4 个（私密仓）历史 commit author 为
  `Z User <z@container>`（沙箱重置后 config 丢失所致），已用
  `git filter-branch` 全部改写为 `cubestudio-dev <cubestudio@qq.com>` 并
  force push；同期把工具链重建脚本与 env.sh 持久化到私密仓 `tools/`。
- 2026-10-02（同日二轮）：另发现 18 个公开仓 commit author 为
  `Cube Studio <cubestudio@qq.com>`（email 正确、名字不同），一并改写；
  累计改写公开仓 32 + 私密仓 4 个 commit、重指 31 个 tag 两轮；改写点
  之前的 tag（如 WP-08a/WP-08b）也需随 e045bcf4 重指。由此把上面
  "批量检查"固定进第 4 节：残留名 grep → 条件性 filter-branch → tag 重指
  → force push → refs/original 清理。
- Python 一律 `python3.13`（paramiko / pexpect 安装于该解释器）。
- QEMU 版本 10.0.13；内核基线内存 698 页。
