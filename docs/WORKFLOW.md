<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — 固定交付流程（每批次必走）

> 本文档是**硬性流程**。每个开发批次（新功能 / 修复 / 重建）完成后，
> 必须按以下五步走完全程，缺一不可。所有输出必须真实执行、原样粘贴。

## 第 1 步：制作 / 修复

- 按批次规格改代码。
- 不许"顺手补全"——只改本批规格内的内容；发现其他问题如实记录，另行立项。

## 第 2 步：完整检查（本批新增，必须全部通过）

按顺序执行并留存实际输出：

1. **公开仓库源码包隐私泄露检查**
   - 按 `Makefile dist` 的排除规则打包 SRC zip。
   - 扫描：SSH 密钥 / PAT / 服务器地址 / 本地绝对路径 / worklog 等内部文档 / 临时文件 / 调试代码 / 测试脚本。
   - 命中即修（或排除出发布包），修完重扫，直到干净。
2. **编译 0 errors 0 warnings**
   - `make clean && make`（`-Wall -Wextra -Werror` 下必须零输出）。
   - `make iso` 生成 ISO，记录字节数与 SHA256。
3. **回归测试**
   - 18/18 QEMU 全量回归（单次 QEMU 会话）：boot 横幅 + `uname -a`
     + 12 个用户程序（hello / fork_test / exec_test / pipe_test /
     signal_test / select_test / mmap_test / dyn_hello / so_test /
     dlsym_test / pie_test / reloc_test）+ p3_test + heaptest + l1test
     + crashlog（3 条开机自检故意异常 #DE/#UD/#PF）。
   - `dhtest`（5/5）+ `cryptotest`（3/3）。
   - SSH 双向（paramiko ↔ 内核 sshd；内核 ssh → paramiko server）。
   - HTTPS E2E（内核 TLS 客户端 ↔ tools/https_test_server.py）。
   - 如批次涉及网络：wget HTTP 大文件（≥100 KB）全量保存验证。
4. **三处 SHA256**
   - 本地 build / GitHub Release 资产（下载回读）/ 线上网站 `/downloads/`
     （下载回读），三处字节级一致。
   - 注意：ISO 的 SHA 每次重打 ISO 都会变（xorriso 时间戳非确定性），
     必须以**最终交付构建**为准，构建后不得再动。
5. **协议检查**
   - LICENSE（Apache-2.0）+ NOTICE 存在且版权行为
     `cubestudio-dev <cubestudio@qq.com>`。
   - `kernel/ boot/ userprogs/` 源文件 SPDX 头覆盖 100%（当前 127/127）。
6. **文档一致性**
   - README.md / docs/STATUS.md / docs/KNOWN_ISSUES.md 与代码行为一致：
     命令数、统计行数、修复记录（FIXED / OPEN 状态）、行为声明（如
     quoting）与实测相符。
   - 未修的根因如实记录 OPEN（例：KNOWN_ISSUES 4.1 挂载点偶发消失），
     不许"顺手修"或含糊其辞。
7. **扩展接口**
   - `l1test` PASS；docs/EXTENSIONS*.md 与 kernel/ext.h 接口一致。

## 第 3 步：编译检查所有文件，上传 GitHub

- `git commit`（描述本批变更 + 验证证据）。
- `git push origin main`，**贴出实际输出**。
- 有代码变更时创建新 Release（tag 规范：`WP-XX-batch-N` / `WP-XX-fixN`），
  上传 ISO + SRC zip 两个资产，**贴出 Release URL**。

## 第 4 步：检查文件后，更新网页

- 更新 `website/lib/site.ts`（SHA256 / 大小 / 版本号 / 统计）与
  `lib/i18n.ts`（badge 等版本字符串），替换 `website/public/downloads/` 下
  两个下载文件。
- `bash website/build-local.sh` 构建；`bash website/rw2_ghpages.sh` 推送
  gh-pages，**贴出实际输出**。
- 实测线上 URL（至少：/、/zh/、/en/、/zh/docs/、/en/docs/、两个
  /downloads/ 文件），全部 HTTP 200，且页面显示的 SHA 与 Release 一致。

## 第 5 步：私密仓同步 + 最后报告

- 私密仓 `archive-repo`：rsync 源码 + 资产 + worklog 更新，commit + push，
  **贴出实际输出**。
- 最后报告中必须包含：真实开发情况、实际执行的验证清单与结果、
  三处 SHA256 对照表、所有 URL 与 push 输出。
- **做完，停，等用户确认。**

---

### 历史备注

- 本流程第 2 步由 2026-10-01 的用户指令新增：33-bug 重审修复期间发现
  "编译过 + 服务器起"不等于"网站/发布物可用"，故将隐私扫描、回归、
  三方 SHA 对照、协议与文档一致性列为每批次固定检查项。
- 工具链：无 sudo 沙箱，`/home/z/opt/install-toolchain.sh`（apt-get
  download + dpkg-deb -x），`source /home/z/opt/env.sh`；
  `make iso` 需 `OC_TOOLS=/home/z/opt/extract`。
- Python 一律 `python3.13`（paramiko / pexpect 安装于该解释器）。
