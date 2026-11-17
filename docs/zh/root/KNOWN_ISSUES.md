<!-- SPDX-License-Identifier: Apache-2.0 -->
# 已知问题

已知缺陷及其状态的滚动记录。2.0.0 发布带着下面十七条问题
（由 2.0.0 postcheck 发现，`audit/2.0.0-postcheck/` 在私有
镜像中）；**全部十七条在 2.1.0 修复**。v1 时代的问题档案
在 `docs/archive/KNOWN_ISSUES.md`。

## 2.0.0 问题 - 全部在 2.1.0 修复

| # | 类别 | 问题 | 2.1.0 修复方式 |
|---|-------|-------|-------------------|
| P-01 | P0 CI/发布门禁 | v2.0.0 的发布代码在 CI runner 上编译失败（tui_core.c 对端口 0x3F8/0x3FD 使用 imm8 "Nd" 约束的 in/out——端口 >= 0x100 必须经 DX）。全部 15 次 v2.0.0 CI 运行都是红的；发布以本地绿/CI 红出厂，未声明。 | 端口 I/O 强制经 `%dx`（"d" 约束、u16 操作数）；加上途中发现并修复的另外两个 CI 环境陷阱：runner 的 GRUB（2.12+）丢掉了 `efi_uga.mod`（模块清单现在对照已安装的 GRUB 过滤），以及 gcc >= 15 默认 C23，`typedef u8 bool` 与原生 `_Bool` 关键字冲突（core/types.h 在 C23 下保留编译器的 bool，gnu11 下用 u8）。CI 重新变绿并成为硬发布门禁。 |
| P-02 | P1 发布数据 | 发布说明的数字自相矛盾（"251 条命令中的 134 条已迁移" vs "251 = 99+133+19"；99+134+19=252）。 | 2.1.0 发布说明使用自洽的账目：251 = 99 L0 + 133 SVC + 19 内建；v1 基线 233，全部映射（INTERFACES.md Part 1c）。 |
| P-03 | P1 兼容性契约 | v1->v2 映射表为 233 条命令的基线只有 231 行：`health export` 和 `health watch` 被一个无空格解析正则静默丢弃。 | 生成器已修复；Part 1c 映射全部 233 条，零 MISSING。 |
| P-04 | P1 接口真相 | 没有逐条目的 162->158 对照；"register 类接口已消失"是过度概括。 | INTERFACES.md Part 2c：基于完整 v1 Part 2 真相的逐条目表（278 条声明：267 公开、6 内化、5 消失）；interfaces.json 报告当前的 17/158 面及 v1 命运字段。 |
| P-05 | P1 删除 | 没有删除登记表；两处真实删除从未声明。 | INTERFACES.md Part 2d 声明全部五处删除（init/probe 管线）及其验证过的 v2 替代；register 类的陈述精确化。 |
| P-06 | P1 文档 | ARCHITECTURE.md 描述的是 v1 树（kernel/、userprogs/、233、53 个系统调用、162、"默认启动进 oc>"）。 | 按真实树重写（两个环、54 个系统调用、17/158、ush 默认入口），EN + zh 镜像。 |
| P-07 | P1 流程文档 | versioning.md（无 +21 偏移）、process.md（WP- tag、v1 门禁）、dev/build.md、dev/e2e-suites.md、manual/index.md、BUILDING.md L141 全是 v1 时代内容。 | 全部更新到 v2 现实（v-tag、六套件矩阵 + L1 逐应用门禁、+21 偏移成文、当前数字）。 |
| P-08 | P1 测试覆盖 | 没有逐应用 L1 测试；v1 的 28 项用户程序回归测试组缺席 2.0.0 矩阵。 | 2.1.0 矩阵新增 L1 逐应用记录（ush + 每个内嵌程序被执行）；见 docs/dev/e2e-suites.md。 |
| P-09 | P1 注释 | Makefile 注释声称 tui/chinese "已重写为 L1 Ring-3 应用"，而两棵树仍编译进内核。 | 注释已更正（tui/chinese 保持 L0 驻留；l1apps/ 只装 ush + 内嵌程序）。 |
| P-10 | P2 网站数据 | site-data.js 的 stats.lines=130,011 vs 真实 130,495；update_website.sh 把过期统计块原样前移。 | update_website.sh 从 docs/data/*.json 重新生成统计块；站点数字始终与数据文件一致。 |
| P-11 | P2 资产 | 公开仓库 main 树不含发布 ISO/SRC（只有私有镜像 + gh-pages 有），与发布说明矛盾。 | 2.1.0 资产提交进公开 main（website/public/downloads/），与 Release/gh-pages 逐字节一致。 |
| P-12 | P2 资产 | website/public/update*.json 仍携带 WP-13/build-259 清单（顶层数对是 2.0.0/302）。 | update_website.sh 逐字节写入两份副本（顶层 + public/）。 |
| P-13 | P2 证据 | 2.0.0 证据 README 引用的五份套件日志在两个仓库中都不存在；asset_hashes.txt 里的 SRC 文件名是错的。 | 五份日志是 2.1.0 运行重新生成的真实文件（evidence/），asset_hashes.txt 命名实际发布的文件。 |
| P-14 | P2 计数口径 | commands.json 的 count_l1_svc=133 vs 真实的 142 行 ush SVC 表——9 个双宿主名被静默归给 L0；interfaces.json 仍携带 v1 的 162 官方计数。 | 口径在 commands.json + INTERFACES.md Part 1b 中精确陈述（142 行、9 个双宿主、账目 251 不变）；interfaces.json 报告 17/158。 |
| P-15 | P2 文档 | docs/manual/commands 是 v1 产物（234 页，"Registered in: shell/..." 路径）；23 条 v2 新命令没有页面。 | 完全重新生成：251 页（EN + zh），真实 v2 归属、幽灵页清除、帮助提示取自 l1svc_defs.h。 |
| P-16 | P2 治理 | 私有镜像的 SYNC-RULES.md 描述的树布局已不存在；2.0.0 的过程日志不在仓库里。 | SYNC-RULES.md 按当前布局重写；过程记录延伸到 2.1.0（都在私有镜像中）。 |
| P-17 | P3 文本缺陷 | docs/dev/index.md 一条链接被标记为 "ost-tests (missing h)"；wp14_site_v2 证据目录名；379 文件口径注记。 | 该链接缺陷查明是误报（字节级检查显示 `[host-tests]` 一直是对的——postcheck 证据中的显示伪影吃掉了 `[h` 序列；已如实记录）。证据目录命名在 2.1.0 更正，文件数口径写入数据注记。 |

## 2.1.0 遗留问题 - 全部在 2.2.0 修复

| # | 类别 | 问题 | 2.2.0 修复方式 |
|---|-------|-------|-------------------|
| O-01 | 内核 | wp10_tui 套件在收尾的 checkupdate SVC 轮之后 #DF（2.0.0 与 2.1.0 相同；已在 2.1.0 交付卷宗中声明）。根因：最深的真实内核调用链（SYS_L1SVC -> ota_update_check -> http_get_body 6000 B 帧 -> net_tls_https_get 2144 -> tls13_do_handshake -> crypto_x509_verify_chain 1872 -> crypto_rsa_verify_pss 2704 -> crypto_bn_mod_exp 4160 -> mont_mul 1200 -> mont_redc 1856；-fstack-usage 合计约 23.6 KB）加上同栈上的 timer/e1000 IRQ 嵌套，超出了 20 KiB 的可用 kthread 栈（24 KiB 分配减去未映射的守卫页）。记录到的崩溃：rip=crypto_bn_mont_redc+0x2A，rsp 低于栈顶 20,880 B、落在守卫页内。 | OC_KTHREAD_STACK_PAGES 6 -> 10（40 KiB 窗口 / 36 KiB 可用，比实测最坏链 + IRQ 嵌套富余约 8 KiB；代价是 ~505 MiB PMM 池中的 512 KiB）。损坏的 #DF panic 横幅（"andled" -> "handled"）也修复了。验证：确切崩溃的那块（wp10_tui 尾部 + ush SVC 轮，含经活动 HTTPS 的 checkupdate）现在跑到 "12/12 checks passed"，带真实 checkupdate 报告、无 panic。提交 4293e94。 |
| O-02 | 内核 | wait4 对 fork 的时序竞态抖动（2.1.0 遗留项）。2.1.0 时误诊为中断时序竞态；实际是**确定性的**簿记问题：进程死亡时，其子进程的 g_procs 条目仍保留 parent_tid = 死亡 tid；tid 释放并被复用后，新任务的 wait4(any-child) 收走了一个陈旧僵尸的退出码，并在自己的子进程被调度前返回（g6_exec_a 打印 "child status = 142" = 128+SIGSEGV，来自 pf_test 被 #PF 击杀、无人 wait 的子进程，判决跑在子进程首次输出之前）。修复前在同启动马拉松和新鲜启动（僵尸源排在受害者之前）中都复现（2/2 次启动）。 | user_process_reap_resources 中的孤儿脱钩（每个仍指向死亡 tid 的条目 -> 哨兵 -1，任何任务都无法认领），加上 sys_fork 的半成品尸体路径（地址空间复制失败 / 任务创建失败）同样脱钩——活着的父进程永不收走 POSIX 说从未创建的孩子。验证：新鲜启动测试组 20/20 次启动全 CLEAN（修复前 2/2 命中异常）；同启动马拉松 g6_exec_a 两轮都 = 138；同启动压力（僵尸源 + wait4 家族 x 8 轮 = 80 次运行）零异常，8/8 状态 138。提交 11d2127。 |

## 遗留问题

下表是 WP-07 收尾时声明的三项已知限制（每行：现象 / 影响
范围 / 当前处置 / 后续归属）。三项都不是 WP-07 引入的回归，
均为收尾时如实申报，构成 3.0.0 恢复生命周期的诚实边界。

| # | 现象 | 影响范围 | 当前处置 | 后续归属 |
|---|------|----------|----------|----------|
| K-01 | `make test` 中 19 个宿主侧单测程序构建失败（该单测组早于 3.0.0 目录重构，相关程序仍引用重构前的路径与头文件）。 | 仅宿主侧单测。内核、全部 L1 应用、六套件运行时矩阵（31/31）、dhtest、SSH E2E、HTTPS E2E 均不受影响且全绿。 | 接受为基线遗留——如实申报，未静默跳过。CI 实际强制的发布门禁（编译 0 错误 0 警告、六套件运行时矩阵、L1 每应用记录）不含 `make test`。 | 后续工作包按当前目录布局刷新宿主单测组；在此之前回归权重由运行时套件承担。 |
| K-02 | 挂接 i6300ESB PCI 看门狗设备时，UEFI（OVMF）下 OCBL 载荷路径在槽位扫描后停止；对未修改的 WP07 前加载器的对照重构建以字节一致的方式复现，说明该缺陷早于 WP-07。 | 仅 UEFI 看门狗链路。不挂该设备时整条 UEFI 监控路径已实测验证；BIOS 形态承载完整且经过验证的看门狗机制（菜单空闲复位、挂死判定、判定失败的计数、[R]/[C] 与 [W]/[R]/[C] 接管菜单）。 | 接受为已知平台限制：BIOS 链路必须全通（实际全通）；不带 i6300ESB 的 UEFI 引导已验证并受支持。 | 后续工作包排查 OCBL-EFI 与 i6300ESB 的共存问题（UEFI BlockIo/固件服务与 PCI 看门狗的交互）。 |
| K-03 | 更新站 CDN 可能继续按旧资产文件名提供缓存副本，导致客户端在发布后一段时间内拉取新改名的清单/资产时收到 404。 | 仅 OTA 客户端，且仅发生在改名后的缓存传播窗口内。 | 双命名兜底：每个改名的资产同时发布旧、新两个文件名变体，OTA 拉取无论命中缓存名还是传播名都能成功。 | 今后避免改名；双命名兜底在旧 CDN 副本可能仍被使用的期间持续保留。 |

更早的边界申报（已关闭或已被取代）保留如下备查。WP-02
声明的诚实边界（OCIRE 内嵌的 OCOS 侧未实现：无 oc.* 事实
解析、无 SIGNAL_OK、无看门狗解除、无代理通道）已由 WP-03
G4 关闭（`src/L0/services/ocembed/`，监督测试组 3/3 在
`evidence/3_0_0_wp03_g4_ocembed/`）。WP-03 之后声明的限制
（设计使然，非缺陷）：

- ush 的 `kill` 在对 ocire_guard 代理的 kill 被拒绝时报告
  "no such process"——syscall ABI 只携带单一
  错误码，所以拒绝文本是泛化的；真实原因在内核日志里
  （klog WARN "kill refused: ocire_guard process"）。
- G5 之前裸的 `sti;hlt` 驻留仍留在四条已证实的路径中
  （sys_read 的键盘等待、wp8cd 的前台等待、sdhci 的定时
  等待、OTA 安顿循环）：它可能导致的被抢占停机冻结已为
  电源睡眠修复（定时器回调 + 阻塞），且在那四条路径上从未
  观测到；真正的定时阻塞唤醒原语是未来的调度器工作。
- 直接启动的 GRUB 条目（"Open Cube OS (direct boot)"）不携带
  oc.* 事实：按定义直接启动不受监督（无信号、
  无保护）——监督者条目是唯一受监督的路径。

## 3.0.0-WP06g2 注记

WP06g1 收尾的 "make all 不重建 userprogs" 缺口
（06g1 的 catch-2）由 WP06g2 关闭：`src/L0/core/userprogs_data.h`
现在是真正的 make 目标（任何比该头新的 L1 源、i18n 资源或
生成器脚本都会重新生成它；`make all` 和 `make iso`
都携带该前置条件；发射是幂等的——写前逐字节比较，未变时
保留 mtime）。双向实测验证：一个打标的应用经 `make all`
进入 ISO，与全新构建逐字节一致；撤销打标则重新生成原始
的头。

声明的 WP06g2 边界（诚实限度，非缺陷）：

- L0 诊断/测试类服务体（*_test 和 wp 风格
  程序的捕获输出）保留英文技术文本；面向用户的句子类
  （文件工具的 usage/错误/描述、注册表提示、编辑器、IME、
  shell）经 i18n 资源双语化。列头与状态
  词刻意保持英文（zh_CN locale 约定——CJK
  宽度会破坏对齐）。
- OCSE 安装向导的 GUI 文本在 WP-06c 时就已双语
  （它自己的词典）；TUI 桌面页面本批仍为英文。
- IME 词库是 622 个手工编写的高频词；单字一如既往覆盖全部
  3755 个一级汉字。

WP06g2 交付修复附记（收尾之后，同批）：

- 收尾推送从未达到绿 CI：词库
  生成器 import 了 pypinyin，而干净的 CI 环境（按设计无 pip
  包）在 ISO 步骤以 ModuleNotFoundError 失败。
  生成器现在自包含——拼音从 pinyin_data.c 自己的
  单字表合成（引擎自身匹配的那些音节），十个多音字词在
  清单中携带其词典读音作为显式对；发射的
  表与 pypinyin 构建的逐字节一致。拼写错误的
  显式对无法通过合法音节分解检查。
- wp06g1 站点携带的 site-data.js 在 dev 块后有一个多余的
  `},`——一个 SyntaxError，window.SITE_DATA 从未加载，
  app.js 填充层（统计、下载卡片、历史表）自 wp06g1 起静默
  跑在静态 HTML 回退上。结构已修复、数据同趟更正：稳定线
  重新是 2.2.0/316（收尾曾把里程碑推进
  稳定槽位，那会让 OTA 频道指向一个从未上传的
  包），dev 块携带真实的 WP06g2 数对（build 403）。
- WP06g3 更正——过去写在这里的 "branches: ain]" 条目是
  幻影。字节级审计（对 fef5f41、0226e8c 和 HEAD 逐字节
  od -c）显示工作流过滤器自文件创建之日起就一直是
  `branches: [main]`；"ain]" 的读法是终端誊录伪影
  （shell 输出显示层把字面 '[m' 当作
  ANSI SGR 复位序列吃掉），从来不是真实笔误。工作流从未
  需要改动；该条目退役。

- tools/run_fix4_regression.py（v2.x 时代的 28 项测试组）断言
  旧启动流（`oc>` L0 恢复 shell 提示符、v1.0.0
  横幅、"233 commands"）——主 ISO 许多个批次以来直接启动进
  L1 shell（`ush>`），测试组无法按原样通过，发布 SOP 不带
  --deep 运行。它需要断言刷新；wp06g3 测试组（serial_enc、
  ocse_enc、editor_rework、ocire_menu、install_ocire）和
  https e2e 7/7 扛起了本批的回归重担。
