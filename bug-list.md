<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# Open Cube OS 3.0.0 缺陷清单（bug-list.md）

> 3.0.0-WP00-environment_restore 导出，供 **WP01-fix** 使用。
> 数据源（真实抓取，无手工编造）：
> 1. GitHub Issues（公开仓，state=open，141 条：带优先级前缀 138 条 + 非缺陷 3 条——#1/#2 用户评论、#23 权限探测残留）；
> 2. WP-06h 全覆盖独立审查报告（私密仓 `audit/3.0.0-WP06h/`：MASTER.md + AUD-2a~2i + FINAL-REPORT.md + PRIVATE-SECURITY.md）。

## 一、统计与对账

| 来源 | P0 | P1 | P2 | P3 | 小计 |
|---|---|---|---|---|---|
| GitHub open issues（带前缀） | 0 | 35 | 62 | 41 | 138 |
| WP-06h 审计私密高危（未开公开 issue） | 1 | 0 | 1 | 0 | 2 |
| **合计（当前待修）** | **1** | **35** | **63** | **41** | **140** |

- WP-06h 审计原始口径（MASTER.md §二）：NEW P0=1 / P1=21 / P2=59 / P3=129（合计 210，撤案 2 后净 208）；issue 号 #28-#127 为审计并行流提交、#129-#141 为主 AI 补交、#3-#27 为更早批次存量。
- 用户规约"约 190 个缺陷"与上表口径差异来源：审计分组统计数字口径失真（190/208 本身即缺陷 #121/#126 主题）、部分发现共用 issue 号（如 #112+#131）、#33/#34/#48 为旧 issue 评论补充新证据。
- **closed issues = 0**：wp-audit-01 修复轮（p0fix1/2、p1fix1-4）修复后未关闭对应 issue，如实申报（修复史见 git tag `wp-audit-01-*` 与私密仓 audit 报告）。

### 1.1 口径对账：open [P1]=35 vs 审计 NEW P1=21（WP00-verify1 复核补充）

两个数字不是同一口径，且 35 **不含任何 WP00 重新定级或新发现**：

- **21 = WP-06h 审计统计表的 "NEW P1 发现" 计数**（9 个静态组求和 + 运行时组 4，是"发现条目"粒度）；
- **35 = GitHub 当前 open 且标题带 `[P1]` 前缀的 issue 数**（WP00 机械实抓，零人工定级）。

复核实证（数据源：141 条 open issues 全量 JSON + 私密仓 AUD-2a~2i/MASTER.md 逐条匹配）：

1. 35 条 `[P1]` issue 的 issue body **全部**带"来自 WP06h 全覆盖独立审查"签名——全部由
   WP-06h 工作包开出（含 #3-#24 早期流），不存在 WP00 新开/重定级的 issue。
2. 21 条 NEW P1 发现 → `[P1]` issue 的落位不是 1:1：
   - **三合一**：AUD-2g-002/003/004（三个"验证脚本恒 exit 0"发现）合并开成一单 #65；
   - **未开公开 issue（4 条）**：AUD-2b-001（TCP 对端通告 MSS>1480 静默丢数据）、
     AUD-2e-001/002/003（OCSE rmpart 稠密序号当槽位号 / 向导 mkpart 计数启发式指错分区 /
     向导第 8 步无界 strcpy 链溢出 wz_line1[96]）——141 条 open issues 中无对应单；
   - **落位为 `[P2]` 前缀（4+1 条）**：AUD-2b-002→#27（e1000 TX 超时计为已发送）、
     AUD-2d-001→#48（OCBL 菜单看门狗从不喂狗）、AUD-2h-002/003→#124（docs/manual+FAQ
     残留 2.x 口径）；运行时根因单 #131（health 监视器无条件喂狗）同为 `[P2]`——而
     MASTER.md Top12 将 #48/#131 均定为 [P1]（审计报告定级与 issue 前缀双轨漂移）；
   - **双路命中**：#130（sys_write2 根因）与 #129（配置方言分裂）在静态组与运行时/补交
     流各计一次发现、只开一单。
3. 35 条 `[P1]` issue 中其余约 24 条为 WP-06h 对存量缺陷的复核确认单（ME-01~05 →
   #109-#113 实机取证单 + 并行流确认单 #31/#32/#35/#36/#46/#47/#63/#64/#66/#91/#92/
   #99/#102/#104/#107），不进 NEW 21 统计。
4. MASTER.md §一自认"全项目数字口径系统性失真"（190/208、274/275 等，即缺陷 #121/#126
   主题），其统计表与 issue 库本就不是互相精确对账的账本。

**WP01-fix 行动建议**：以本清单 35 条 P0/P1 为准（issue 前缀实抓 + 审计私密高危补充），
不追溯 MASTER 统计表数字；上述 4 条未开单的 NEW P1 发现应在 WP01-fix 开单补录；落位
`[P2]` 的 #27/#48/#124/#131 四条建议按 MASTER 定级提级复核。

P1 模块分布：构建/发布链 8 条；L1 应用/ush 7 条；OCIRE 恢复内核 3 条；L0 核心 3 条；OCSE 安装环境 3 条；L0 服务 2 条；L0 网络 3 条；L1 应用 2 条；L0 平台/ACPI 1 条；OCBL 引导器 1 条；文档/官网 1 条；L0 服务/看门狗 1 条；L0 调度 1 条

## 二、P0（1 条——高危安全发现，不公开细节）

### BUG-AUD-2a-001（P0 · L0 核心/syscall 安全）

- **编号**: WP-06h 审计 AUD-2a-001（**未开公开 issue**，按审计红线"高危安全漏洞不公开"处理）
- **优先级**: P0
- **模块**: L0 核心（用户指针校验 + 用户/内核地址空间布局）
- **缺陷描述**: 用户指针校验缺口（脱敏口径；影响：ring3 可经标准读写 syscall 触达内核内存）
- **复现步骤 / 涉及文件 / 完整技术细节**: **不公开**——全文见私密仓 `audit/3.0.0-WP06h/PRIVATE-SECURITY.md`（WP01-fix 在私密仓环境取用）
- **备注**: 同文件另含 AUD-2b-004（P2 · 内核堆 UAF，crafted ACPI 表可触发内核堆损坏）

## 三、P1（35 条，全字段）

### BUG-3（P1 · OCIRE 恢复内核）

- **编号**: #3（GitHub Issue）
- **优先级**: P1
- **模块**: OCIRE 恢复内核
- **缺陷描述**: core_sched_task_states 的 flags 恢复惯用法被写坏——任务上下文调用一次后全局中断永久关闭
  - - 位置: `src/L0/core/core_sched.c:143-162` int core_sched_task_states(u32 *running, u32 *ready, u32 *blocked, u32 *total) {
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/core/core_sched.c:143`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/3

### BUG-4（P1 · L0 核心）

- **编号**: #4（GitHub Issue）
- **优先级**: P1
- **模块**: L0 核心
- **缺陷描述**: 内核栈 guard page 对 2MiB 大页的 unmap 是空操作 + 分裂后全局 TLB 脏映射——WP-11 栈保护实际未生效
  - - 位置: `src/L0/core/core_sched.c:395-404`（guard page 创建）、`core_sched.c:833-843`（reap/destroy 重映射）、`src/L0/core/mem/mem_vmm.c:146-176`（walk_pt 大页分支）、`src/L0/boot/boot.S:106-116`（4 GiB 全部 2MiB 大页 + G 标志） /* core_sched.c (create) */
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/core/core_sched.c:395`, `src/L0/core/mem/mem_vmm.c:146`, `src/L0/boot/boot.S:106`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/4

### BUG-5（P1 · L1 应用/ush）

- **编号**: #5（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: sys_kill(SIGKILL/默认动作) 竞态：先销毁受害者地址空间、后标记任务 EXITED，中间的 timer tick 可把受害者调度到已释放的 CR3 上
  - - 位置: `src/L0/services/sysapi/core_syscall.c:545-562`（SIGKILL 分支）、`:564-579`（默认动作分支） if (sig == SIGKILL) {
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/services/sysapi/core_syscall.c:545`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/5

### BUG-6（P1 · L0 核心）

- **编号**: #6（GitHub Issue）
- **优先级**: P1
- **模块**: L0 核心
- **缺陷描述**: user_process_create 的 exec 失败/OOM 路径泄漏整个地址空间（fork 已修，exec 未修）
  - - 位置: `src/L0/core/core_usermode.c:1015`（`proc->as = create_user_address_space()`）与所有后续早退点：1047-1049、1098-1100、1172、1181、1195、1200、1205、1211、1223、1257、1317-1318 proc->as = create_user_address_space();
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/core/core_usermode.c:1015`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/6

### BUG-7（P1 · L0 服务）

- **编号**: #7（GitHub Issue）
- **优先级**: P1
- **模块**: L0 服务
- **缺陷描述**: sys_execve 在新地址空间尚未建成前就销毁旧 AS；map_user_pages 失败被忽略——exec 无法优雅失败
  - - 位置: `src/L0/services/sysapi/core_syscall.c:1703-1712, 1726-1728` mem_vmm_as_t old_as = proc->as;
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/services/sysapi/core_syscall.c:1703`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/7

### BUG-20（P1 · L0 平台/ACPI）

- **编号**: #20（GitHub Issue）
- **优先级**: P1
- **模块**: L0 平台/ACPI
- **缺陷描述**: AML 解释器 OpRegion 内存/IO 访问无任何边界检查（恶意 DSDT → 任意物理内存读写）
  - - 文件：`src/L0/drivers/acpi/driver_acpi_aml.c` - 行号：449-452（读 SystemMemory）、521-527（写 SystemMemory）、455-460（SystemIO）、1355-1361（OpRegion 声明解析，region_len 存了但从未使用）
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/drivers/acpi/driver_acpi_aml.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/20

### BUG-21（P1 · L0 网络）

- **编号**: #21（GitHub Issue）
- **优先级**: P1
- **模块**: L0 网络
- **缺陷描述**: TLS 证书链验证"失败仅告警继续"——主动 MITM 只需一张自签证书
  - - 文件：`src/L0/services/net/net_tls.c`（1024-1038 TLS1.3 路径；1553-1570 TLS1.2 路径）、`src/L0/services/crypto/crypto_x509.c`（523-592） - 代码（net_tls.c:1030-1037）：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/services/net/net_tls.c`, `src/L0/services/crypto/crypto_x509.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/21

### BUG-22（P1 · L0 服务）

- **编号**: #22（GitHub Issue）
- **优先级**: P1
- **模块**: L0 服务
- **缺陷描述**: DNS 事务 ID 硬编码 0x1234 且响应不做 ID/源校验——离线伪造即可投毒
  - - 文件：`src/L0/services/net/net_core.c` - 行号：4121（`h->id = htons(0x1234);` 固定值）、4155-4157（固定源端口 1077 + `net_udp_send`）、3987-3992（`net_dns_handler` 不检查 id、问题域名、源 IP）
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/services/net/net_core.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/22

### BUG-24（P1 · L0 网络）

- **编号**: #24（GitHub Issue）
- **优先级**: P1
- **模块**: L0 网络
- **缺陷描述**: TCP RTO 重传在关中断的定时器上下文里落入 ARP 解析忙等——ARP 项过期即整机死锁
  - - 文件：`src/L0/services/net/net_core.c` - 行号：2722-2724（`net_tcp_check_rto` 开头 `cli`）、2760-2765（RTO 重传→`net_tcp_send_raw`）、1329（`net_ip_send`→`net_arp_resolve`）、1195-1211（ARP 忙等循环）、4497-4498（`net_poll` 重入保护，重入直接 return）、974（ARP TTL 300s）、6153-6157（`net_timer_cb`→`net_poll`）
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L0/services/net/net_core.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/24

### BUG-31（P1 · L1 应用/ush）

- **编号**: #31（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: [HIGH-RISK] ush：command-not-found 路径把用户输入无界拼进 `path[64]` —— 栈 smashing
  - - 文件: `src/L1/ush/main.c:786`（redirect 路径）与 `:837-840`（常规路径） - 片段:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/ush/main.c:786`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/31

### BUG-32（P1 · L1 应用/ush）

- **编号**: #32（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: [HIGH-RISK] ush：Tab 补全对 `line[512]` 无界追加 —— 越界写
  - - 文件: `src/L1/ush/main.c:1385-1391、1394-1397`（注册表/内建候选）、`:1469-1479`（文件名候选） - 片段:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/ush/main.c:1385`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/32

### BUG-33（P1 · L1 应用/ush）

- **编号**: #33（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: [HIGH-RISK] ush：alias 展开对 `newcmd[512]` 无界 strcat —— 栈 smashing（两个调用点）
  - - 文件: `src/L1/ush/main.c:746-748`（redirect 路径）、`:804-806`（常规路径） - 片段:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/ush/main.c:746`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/33

### BUG-34（P1 · L1 应用/ush）

- **编号**: #34（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: 编辑器：打开超限文件再保存 = **无提示销毁文件内容**
  - - 文件: `src/L1/_shared/editor_core.h:508-546`（`unano_load`，`UNANO_MAX_LINES=256`/`UNANO_LINE_CAP=128`） - 片段:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/_shared/editor_core.h:508`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/34

### BUG-35（P1 · L1 应用）

- **编号**: #35（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用
- **缺陷描述**: `write` 应用自发布起就是坏的：以 RDONLY 打开再写，写失败被忽略 —— 静默 no-op
  - - 文件: `src/L1/write/main.c:20-30` - 片段:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/write/main.c:20`, `src/L0/services/fs/fs_vfs.h:45`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/35

### BUG-36（P1 · L1 应用）

- **编号**: #36（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用
- **缺陷描述**: `cp`/`mv` 无同文件防护：`cp x x` 清空文件、`mv x x` 删除文件
  - - 文件: `src/L1/cp/main.c:14-33`、`src/L1/mv/main.c:10-31` - 片段（mv）:
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/cp/main.c:14`, `src/L1/mv/main.c:10`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/36

### BUG-46（P1 · OCBL 引导器）

- **编号**: #46（GitHub Issue）
- **优先级**: P1
- **模块**: OCBL 引导器
- **缺陷描述**: OCBL ELF 加载器边界检查可被整数回绕绕过（越界写）
  - - 文件：`ocbl/common/ocbl_load.c` - 行号/片段：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `ocbl/common/ocbl_load.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/46

### BUG-47（P1 · OCIRE 恢复内核）

- **编号**: #47（GitHub Issue）
- **优先级**: P1
- **模块**: OCIRE 恢复内核
- **缺陷描述**: OCIRE supervisor：mailbox 写失败路径形成无限重启循环
  - - 文件：`ocire/ocire_bootsvc.c` L135-149 - 片段：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `ocire/ocire_bootsvc.c`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/47

### BUG-63（P1 · 构建/发布链）

- **编号**: #63（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: Release 启动门使用可被旧日志污染的固定 /tmp 文件，`|| true` 吞掉 QEMU 失败 → 可假绿
  - - **文件**：`tools/build_release.sh:59-87` - **代码**：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `tools/build_release.sh:59`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/63

### BUG-64（P1 · 构建/发布链）

- **编号**: #64（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: Release 深度回归门的断言结构性地"永真"（DOTALL + 命令回显匹配）
  - - **文件**：`tools/run_fix4_regression.py:44-114, 139-163` - **代码**：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `tools/run_fix4_regression.py:44`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/64

### BUG-65（P1 · 构建/发布链）

- **编号**: #65（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: 当前里程碑（WP06g3）全部 E2E 验证脚本不传播失败退出码
  - - **文件**：`tools/wp06g3_test_serial_enc.py:53-56, 68-70, 82-85`；`tools/wp06g3_test_ocse_enc.py:135-138, 117-119`；`tools/wp06g3_test_editor_rework.py:89-92, 150-152`；`tools/wp06g3_test_ocire_menu.py`；`tools/wp06g3_repro_nano_scroll.py`；`tools/wp06g3_repro_serial_enc.py` - **代码**（serial_enc 为例）：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `tools/wp06g3_test_serial_enc.py:53`, `tools/wp06g3_test_ocse_enc.py:135`, `tools/wp06g3_test_editor_rework.py:89`, `tools/wp06g3_test_ocire_menu.py`, `tools/wp06g3_repro_nano_scroll.py`, `tools/wp06g3_repro_serial_enc.py`, `tools/run_ocire_tests.py`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/65

### BUG-66（P1 · 构建/发布链）

- **编号**: #66（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: 屏幕内容从未被机器断言——WP-06g2 假绿通道结构上原样保留
  - - **文件**： - `tools/qemu_runner.py:132-133`（`-display none`，串口 mon:stdio——所有 run_*/wp10_*/wp12_* E2E 的公共底座，纯串口）
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `tools/qemu_runner.py:132`, `tools/wp06g3_test_ocse_enc.py:98`, `tools/wp06g3_test_editor_rework.py`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/66

### BUG-90（P1 · 文档/官网）

- **编号**: #90（GitHub Issue）
- **优先级**: P1
- **模块**: 文档/官网
- **缺陷描述**: README.md:8/39/123、ARCHITECTURE.md:4/22/25/92、MANIFEST.md:8/73/121、INTERFACES.md
  - - **F1 [P1]** README.md:8/39/123、ARCHITECTURE.md:4/22/25/92、MANIFEST.md:8/73/121、INTERFACES.md:3（生成标题）、docs/index.md、docs/root/* 镜像 全部停留在 `3.0.0-WP06c`。tag 已是 WP06g3。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `docs/index.md`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/90

### BUG-91（P1 · 构建/发布链）

- **编号**: #91（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: main 分支 website 整体停在 WP06g2 build 403：`site-data.js:41`（403）、`index.html:10,11,5
  - - **F2 [P1]** main 分支 website 整体停在 WP06g2 build 403：`site-data.js:41`（403）、`index.html:10,11,54,63,65,81,126,158`、`download.html:5-6,14,63-86`，且 `website/public/downloads/` **无任何 WP06g3 资产**（gh-pages 有）。main website 与 gh-pages 不同步。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/91

### BUG-92（P1 · OCIRE 恢复内核）

- **编号**: #92（GitHub Issue）
- **优先级**: P1
- **模块**: OCIRE 恢复内核
- **缺陷描述**: `ocire/generated/ocire_version_gen.h:4` 恢复内核横幅 `OCIRE 3.0.0-WP-02 (build 414)`
  - - **F3 [P1]** `ocire/generated/ocire_version_gen.h:4` 恢复内核横幅 `OCIRE 3.0.0-WP-02 (build 414)` —— 发布物内部自报版本 WP-02。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `ocire/generated/ocire_version_gen.h:4`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/92

### BUG-99（P1 · 构建/发布链）

- **编号**: #99（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: main `website/` ↔ gh-pages 不同步：gh-pages = g3/build416 + g3 四资产 + VitePress `docs
  - - **F10 [P1]** main `website/` ↔ gh-pages 不同步：gh-pages = g3/build416 + g3 四资产 + VitePress `docs/` 构建产物（2423 文件）；main = g2/403、无 g3 资产、无 docs/ 产物。commit 信息与内容自洽，但 main 树里的"官网源"是旧的。 - gh-pages `downloads/` g3 资产 blob 哈希实测 = GitHub Release API digest（ocse 0abb622c/5038080、opencube 34bcccdb/4210688）✓。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/99

### BUG-102（P1 · 构建/发布链）

- **编号**: #102（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: README:96 `./build.sh iso`
  - - **F13 [P1]** README:96 `./build.sh iso` —— **仓库根没有 build.sh**（只有 `tools/build.sh` 与 `build.ps1`）；BUILDING.md:33 也写 `./build.sh --install`，与同文 `tools/build.sh` 混用。Quick start 按原文执行必失败。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `tools/build.sh`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/102

### BUG-104（P1 · 构建/发布链）

- **编号**: #104（GitHub Issue）
- **优先级**: P1
- **模块**: 构建/发布链
- **缺陷描述**: README:126 "Each release note carries the SHA256 of every asset"
  - - **F15 [P1]** README:126 "Each release note carries the SHA256 of every asset" —— 实测 `docs/release/3.0.0-WP06g2.md` 与 `3.0.0-WP06g3.md` **均无 SHA256/size 表**（模板 `release-notes-template.md:27` 和 process.md:85 门禁明确要求）；发布流程自身门禁未被执行。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `docs/release/3.0.0-WP06g2.md`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/104

### BUG-107（P1 · OCSE 安装环境）

- **编号**: #107（GitHub Issue）
- **优先级**: P1
- **模块**: OCSE 安装环境
- **缺陷描述**: `/home/z/audit/release/ocse-3.0.0-WP06g3.iso` 本地下载副本 = **1269760 B / sha256 09cd
  - - **F18 [P1]** `/home/z/audit/release/ocse-3.0.0-WP06g3.iso` 本地下载副本 = **1269760 B / sha256 09cdd8df…**，而 GitHub Release API digest = **5038080 B / 0abb622c…**（gh-pages 文件与 site-data 均为 0abb622c）。opencube g3 副本 34bcccdb ✓ 一致。即本地 ocse 副本与 Release 资产**不是同一字节流**（需重下或追查谁错）。另：任务提到的 `/home/z/audit/release/SHA256SUMS` 实际不存在（目录仅 2 个 ISO）。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/107

### BUG-109（P1 · L0 服务/看门狗）

- **编号**: #109（GitHub Issue）
- **优先级**: P1
- **模块**: L0 服务/看门狗
- **缺陷描述**: 整机静默冻结竞态：连续键入 'a'/'u' 或运行 loop 应用 → 全系统死机，无 panic、无看门狗接管
  - - 复现（6+ 次）：ush 提示符下连续键入 8~56 个 'a'（概率性，最快第 1 键 'u' 即死）；nano 内第 5 个 'a'；OCSE 命令行 mkfs 成功后键入 "i"。 - 证据：串口回显在字符中途戛然而止（如 `ush> aaaaaaa` 后 0 字节）；screendump 显示冻结画面（`ush> aaaaa` 光标静止）；QEMU monitor 仍响应（宿主模拟器正常， guest 内核已死）。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/109

### BUG-110（P1 · L0 核心）

- **编号**: #110（GitHub Issue）
- **优先级**: P1
- **模块**: L0 核心
- **缺陷描述**: L1 应用 stdout 的中文全部渲染为豆腐块（串口合法 UTF-8 / 屏幕不可读）——WP-06g2"串口合法掩盖屏幕乱码"模式在 WP06g3 复现
  - - lang zh 后 `help -a`：屏幕上每条描述的中文渲染为空心方框（截图 run_C2/C3/C4），每汉字 2 框 = 按 GBK 双字节解码 UTF-8 流的形态。 - 司法鉴定：同屏内容的串口字节为合法 UTF-8（`e7 94 a8 e6 88 b7` = 用户），serial_encoding [utf8|gbk] 的 WP06g3 修复只修了串口侧，帧缓冲路径仍按 GBK 解码。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/110

### BUG-111（P1 · L1 应用/ush）

- **编号**: #111（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: nano/vi 退出不清屏：编辑器残影与 shell 提示符重叠成乱屏
  - - Ctrl+X 退出后屏幕同时残留：编辑器顶栏、底栏状态条、行内容，"ush>" 提示符打印在残留行中间（截图 run_H3/run_H4）。 - 串口流（ANSI 语义）干净，屏幕（帧缓冲）脏 → 又一处"串口绿/屏幕烂"。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/111

### BUG-112（P1 · L0 调度）

- **编号**: #112（GitHub Issue）
- **优先级**: P1
- **模块**: L0 调度
- **缺陷描述**: loop 忙等应用 35 秒+ 无抢占、无 health 报警、无看门狗击杀——宣称的"抢占调度 + cpu>=90%x10s 报警 + 看门狗杀无响应应用"链路实测失灵
  - - `loop` 启动后整机无响应（与 ME-01 同现象或同根因），串口静默 35s+，期间健康监控（1s 采样）与 15s 看门狗均未产生任何动作。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/112

### BUG-113（P1 · OCSE 安装环境）

- **编号**: #113（GitHub Issue）
- **优先级**: P1
- **模块**: OCSE 安装环境
- **缺陷描述**: OCSE GUI 鼠标点击全部无效：悬停渲染正常、按键事件不触发按钮
  - - 实测：真实 PS2 鼠标（QEMU 标准模拟）移动+悬停高亮正常（中文/Re-check/Command-line install/Install now 四按钮）；按下+释放点击后 UI 无任何变化（多组时序重试）。 - 对照：Ctrl+Alt+F2 命令行的 `click <x> <y>` 调试命令以相同坐标命中（action=wizard/toggle-lang/goto-cl）→ 按钮几何与处理存在，真实鼠标按键事件未到达点击判定。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: （见 issue 正文）
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/113

### BUG-129（P1 · OCSE 安装环境）

- **编号**: #129（GitHub Issue）
- **优先级**: P1
- **模块**: OCSE 安装环境
- **缺陷描述**: 安装器↔内核配置方言分裂：向导/OCSECL 写 lang/hostname/boot.slot（死键），内核只读 shell.lang/sys.hostname，且安装器配置在首次启动被内核整体重写
  - - 位置: `ocse/ocse_inst.c:477-506`（conf_key_valid 认 `lang`/`boot.slot`/`hostname`）；`ocse/ocse_wizard.c:854-870`（向导写 `config set hostname/lang`）；`OCOS.TAR:etc/opencube.conf`（65B，`hostname=opencube/lang=en/boot.slot=A`）vs `src/L0/core/lib/lib_config.h:60-73`（内核键：`shell.lang`/`sys.hostname`/`update_url`/`auto_check`/`package_url`/`online_update`，无任何兼容映射） - **实机证据链**：
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `ocse/ocse_inst.c:477`, `ocse/ocse_wizard.c:854`, `etc/opencube.c`, `src/L0/core/lib/lib_config.h:60`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/129

### BUG-130（P1 · L1 应用/ush）

- **编号**: #130（GitHub Issue）
- **优先级**: P1
- **模块**: L1 应用/ush
- **缺陷描述**: sys_write2 控制台回退未移植 WP06g1 UTF-8 修复——ush 全部输出（含输入回显）中文渲染豆腐块，串口完美（#110 的精确根因与修复点）
  - - **#110 标题"L1 应用 stdout 全部豆腐"范围过宽需修正**：实测 `echo 切换目录测试ABC`、`cat`（sys_write 路径）屏幕渲染**真汉字**；豆腐仅出现在 **ush 自身输出**（help/help -a/help -w、命令回显）。 - 根因: ush 所有输出走 `SYS_WRITE2(72)`（`src/L1/ush/main.c:7,30,112`），而 `sys_write2` 的 fd1/2 控制台回退 `src/L0/services/sysapi/core_syscall.c:2261` 仍逐字节 `screen_console_putc`；WP06g1 的 `console_cn_puts_enc` 修复只落在 `sys_write`（:805-818）。
- **复现步骤**: 见 issue 正文与评论（WP-06h 审计附实机取证：串口字节级 / VGA 截图 / 像素级 VLM 验证）
- **涉及文件**: `src/L1/ush/main.c:7`, `src/L0/services/sysapi/core_syscall.c:2261`
- **链接**: https://github.com/cubestudio-dev/OpenCubeOS/issues/130

## 四、P2/P3（不展开，仅计数）

按本包规约仅导出 P0/P1。P2 共 62 条、P3 共 41 条，明细见 GitHub Issues（`is:open`）与私密仓 `audit/3.0.0-WP06h/FINAL-REPORT.md`。

## 五、使用说明（给 WP01-fix）

1. 修复顺序建议：P0（AUD-2a-001，私密仓取全文）→ P1 按模块聚簇（同文件/同机制一起修，上表模块分布即聚簇依据）。
2. 每条 issue 正文含"位置/代码/根因/修复"结构与实机证据，修复前必读原文。
3. #33/#34/#48 的关键证据在**评论区**（WP-06h 补充），不在正文。
4. 修复后应关闭对应 issue（上轮 wp-audit-01 未关闭是流程缺陷，勿复发）。
