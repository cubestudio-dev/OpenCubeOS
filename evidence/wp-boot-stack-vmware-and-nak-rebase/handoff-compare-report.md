<!-- SPDX-License-Identifier: Apache-2.0 -->
# 引导层寄存器与硬件状态移交对比报告（WP-boot-stack-vmware-and-nak-rebase 4.1）

版本：2026-11-23
作者：cubestudio-dev <cubestudio@qq.com>
范围：三版本（A：最后 GRUB 引导 / B：首次 OCBL / C：当前主干）引导层移交状态全量对比，反推 VMware 启动崩溃的根因差异，输出 4.2 差异补齐清单。

---

## 一、三版本锚点（git log 实证）

| 版本 | 提交 | 说明 | 引导路径 |
|---|---|---|---|
| A（最后 GRUB） | `1b8c563b` WP-01-tree | GRUB 2.12-9（multiboot2）引导 | GRUB（gfxterm+serial 双通道）→ ocos 内核 `_start`（32 位） |
| B（首次 OCBL） | `ba98aa1c` WP-02-ocbl | 手写 BIOS+UEFI 引导器取代 GRUB | OCBL stage1(512B) → stage2a(32 位 trampoline) → stage2b+64 位 C → 内核 `_start` |
| C（当前主干） | `da77ad06` | OCRMECL 兼容层 + OCBL + 完整链 | OCRMECL stage1 → OCRMECL core（实模式 flatten）→ OCBL stage1 → OCBL core_head（长模式）→ OCBL 64 位 C → 内核 `_start` |

A/B 快照从 `git archive` 导出（本地 `workspace/snapA`、`snapB`）；C 为工作树本身。

## 二、移交点定义

- 版本 A 移交点：GRUB `multiboot2 /boot/kernel.elf` + `boot`（snapA/ocos/grub.cfg L12-13）执行后跳内核 `_start`（snapA/ocos/kernel/core_boot_multiboot2.S L90）。
- 版本 C 移交点：`ocbl_handoff(entry, OCBL_MBI_BASE)`（ocbl/common/ocbl_main.c L408）→ ocbl/bios/ocbl_bios_core_head.S L177-211 降级到 32 位 PG off → `jmp ecx`（内核 `_start`，ocbl/bios/ocbl_bios_core_head.S L211）。
- 两版本内核入口均为 ocos/kernel/core_boot_entry.S `_start`（C 版，L70）——内核侧对移交状态的期待一致。

## 三、六类核心状态对比表（A=GRUB 移交 vs C=OCBL 移交）

### 3.1 通用寄存器与段寄存器

| 项 | A（GRUB 移交时） | C（OCBL 移交时） | 判定 |
|---|---|---|---|
| EAX | `0x36D76289`（mb2 magic） | `0x36D76289`（ocbl_bios_core_head.S L209） | 等价 |
| EBX | mbi 物理地址 | mbi 物理地址（ocbl_bios_core_head.S L210，`OCBL_MBI_BASE=0x50000`） | 等价 |
| CS:EIP | 平坦 32 位代码段（GRUB GDT），_start | code32 选择子 0x08（OCBL core GDT），_start | 等价 |
| DS/ES/FS/GS/SS | 平坦数据段（base 0，limit 4G） | data32 选择子 0x10（base 0，limit 4G） | 等价 |
| SS:ESP | GRUB 自设栈 | OCBL 栈（BIOS 轨 stage1 已重建 0:0x7C00） | 等价（内核重建栈，不依赖） |
| EFLAGS | IF=0（GRUB 移交前 cli） | IF=0（ocbl_handoff L178 cli） | 等价 |

### 3.2 控制寄存器 CR0 / CR3 / CR4

| 位/寄存器 | A | C | 判定 |
|---|---|---|---|
| CR0.PE | 1 | 1（handoff32 未动 PE） | 等价 |
| CR0.PG | **0** | **0**（handoff32 L202 `btc eax,31` 清 PG） | 等价 |
| CR0.WP | BIOS 遗留（Phoenix 常为 1） | **1 残留**（BACK32 设 WP，handoff32 只清 PG；ocbl_bios_core_head.S L146 设置） | 见差异 D2 |
| CR0 其余（AM/CD/NW/NE/MP/TS/ET） | BIOS 遗留 | BIOS 遗留（整链未清） | 等价（GRUB 也不清） |
| CR3 | 任意（PG=0 下无效值） | **0**（handoff32 L208 `xor eax,eax; mov cr3`） | 等价 |
| CR4.PAE | 0 | **0**（handoff32 L205 `and eax,~0x20` 清 PAE） | 等价 |
| CR4 其余 | BIOS 遗留（通常 0） | BIOS 遗留 | 等价 |

### 3.3 MSR IA32_EFER（0xC0000080）——核心差异 D1

| 位 | A | C | 判定 |
|---|---|---|---|
| LME (bit 8) | **0**（GRUB 从不进入长模式；移交时 EFER=0） | **1 残留**（ocbl_bios_core_head.S L143 `or eax,(1<<8)|(1<<11)` 设置，ocbl_handoff 降级链只清 PG/PAE/CR3，**从未清 EFER**） | **不等价** |
| NXE (bit 11) | 0 | **1 残留**（同上） | **不等价** |
| SCE/LMA 等 | 0 | LMA=1（PG on 时随 LME 联动；PG off 后 LMA 自动清 0，LME/NXE 保留） | **不等价** |

D1 评注：`PG=0 且 LME=1` 是合法的"长模式已武装的兼容态"（x86 手册：LME 只能在 PG=0 时改写；此状态本身合法）。内核 core_boot_entry.S L126-129 重新 `rdmsr/or LME/wrmsr`，流程兼容。**因此 D1 不是崩溃根因，但它是 C 相对 A 的真实移交状态差异，且属于未定义行为的近邻状态（任何中间代码触发 #GP/#UD 向量化时，异常路径对 EFER 状态敏感）。本包将其按"标准化移交"对齐清零。**

### 3.4 中断控制器（8259A PIC / IOAPIC）

| 项 | A | C | 判定 |
|---|---|---|---|
| 8259A 向量基 | BIOS 遗留（0x08/0x70，Phoenix/SeaBIOS POST 均如此） | 0x08/0x70（OCRMECL core 显式 remap，ocrmecl_bios_core.asm L137-154） | 等价 |
| 8259A IMR | BIOS 遗留（部分 IRQ 开放，如 IRQ1 键盘、IRQ8 RTC） | **全屏蔽 0xFF**（L152-154） | C 更严（更接近"无中断假设"） |
| IOAPIC RTE | BIOS 遗留 | **全 mask**（L173-203，unreal-mode MMIO） | C 更严 |
| 交付后行为 | 内核自 remap IRQ0-15→vec 32-47（内核 serial.log L20 "8259 PIC remap OK"） | 同（内核相同代码） | 等价 |

评注：GRUB 时代的移交把 PIC 留在"BIOS 状态"；版本 C 的 flatten 更严格。内核两种都兼容（自 remap）。**此差异方向正确，非崩溃根因。**

### 3.5 GDT / IDT

| 项 | A | C | 判定 |
|---|---|---|---|
| GDTR | GRUB 平坦 GDT（3 描述符） | OCBL core GDT（ocbl_bios_core_head.S L272-289：null/code32/data32/code64/code16/data16） | 等价（内核 L80 `lgdt` 自建） |
| IDTR | **BIOS 实模式 IVT（{0,0x3FF}）——在 32 位下语义无效**（GRUB 赌移交瞬间无异常） | **OCBL 自家 256 gate 64 位 IDT**（ocbl/common/ocbl_idt.c L68-99，异常报告串口+halt；BIOS 轨 trampoline 每次穿越恢复） | **C 更严**（4.5 H1 修复引入） |
| 异常→后果 | 32 位下异常经无效 IVT → #GP → triple fault | 异常经有效 gate → 串口报告 → 有标记 halt | C 更严 |

### 3.6 串口 COM1 / A20 / framebuffer / 显示

| 项 | A | C | 判定 |
|---|---|---|---|
| COM1 | GRUB serial 模块已初始化（115200 8N1，serial.log 有 GRUB 菜单文本实证） | OCRMECL stage1 ser_init（同参数，ocrmecl_bios_stage1.asm L67/L78-100） | 等价 |
| COM1 轮询 | GRUB 驱动层 | **全链 THR-empty 轮询无超时**（见 D3） | **不等价（健壮性）** |
| A20 | GRUB 开 | OCRMECL 强制开+双 pattern 验证（core L86-104） | C 更严 |
| VGA 文本进度 | GRUB gfxterm 有屏幕输出（GFXTERM_INIT） | **OCRMECL 静默 VGA（无任何文本标记）**（见 D4） | **呈现层退步** |
| framebuffer | **移交前已设 VBE 800x600x32**（GRUB gfxterm 模式，mb2 fb tag） | OCBL B4 阶段 `ocbl_vbe_set_800x600x32`（ocbl_main.c L324）移交前设置 | 等价（时机更晚） |

## 四、核心差异清单（含代码行号）

- **D1（EFER.LME/NXE 残留）**：ocbl/bios/ocbl_bios_core_head.S L141-144 设置、L177-211 的 ocbl_handoff 降级链不清除 → 移交时 EFER≠0，与 GRUB（EFER=0）不等价。判定：非直接根因；按标准化对齐修复。
- **D2（CR0.WP 残留）**：ocbl_bios_core_head.S L146 设 WP，handoff32 L202 只清 PG。GRUB 移交 WP=BIOS 值。判定：无害（内核 L133 自设 WP），仅申报。
- **D3（串口轮询无超时——静默死循环根因）**：
  - ocrmecl/bios/ocrmecl_bios_stage1.asm `ser_str .w`（L108-112）
  - ocrmecl/bios/ocrmecl_bios_core.asm `ser_str .w`（L289-293）与 `ser_dec .w2`（L321-324）
  - ocbl/bios/ocbl_bios_stage1.asm `ser_str .w`（L301-305）
  - ocbl/common/ocbl_serial.c `ocbl_serial_putc`（L41-43）
  QEMU 的虚拟 16550 LSR.THR-empty 恒置位（宽容）；真实 16550/VMware 在设备未打开、流控、host 侧写失败等状态下 LSR 可能持续为 0 → 无限 `in al,dx` 死循环。**这是与"无串口输出、无 hlt 弹窗、无 fault 弹窗、无设备活动"的附件证据完全自洽的唯一结构性缺陷**（vmware-1/vmware-2：resize 720x400 后 12 秒完全静默直至用户手动关机）。
- **D4（VGA 进度标记缺失——可观测性退步）**：版本 B 的 stage2a 携带 VGA_TRACE 宏（snapB/ocbl/boot/core_boot_bios_stage2.S L53-75，每完成一步在 0xB8000 行 24 写一个字符 '2'/'3'/'5'/'6'）；版本 C 全链删除了该机制。VMware 用户串口异常时屏幕完全不可观测 → 无法定位死点。
- **D5（历史差异，已在 4.5 修复并保持）**：own-IDT（H1）、El Torito boot drive 0x9F 门控（H2）、BDA EBDA 读取（H3）、栈 0x8E000+页表基址重定位（H4）。本包不重复改动，仅在回归中验证。

## 五、附件证据判读（/home/z/my-project/upload/）

| 会话（时间） | 挂载 ISO | 判读 |
|---|---|---|
| vmware-2.log（11:38:44 启动） | runtime-hardening ISO（版本 C） | 弹窗暂停期间换盘（11:38:47.962 连接 ISO）→ VM 恢复 → **resize 720x400（11:38:48.594，El Torito 载入+跳转发生）** → 12 秒完全静默（vcpu-0 无任何设备事件、无 monitorevent 弹窗、无 CLIHLT）→ 手动关机（11:39:02.5） |
| vmware-1.log（11:39:29 启动） | 同上（版本 C） | 同模式：resize 11:39:32.275 → 静默 12 秒 → 手动关机 11:39:44.9 |
| vmware-0.log（11:40:32 启动） | opencubenak-WP-02-ocbl.iso（版本 B） | resize 11:40:38.075 → **CLIHLT 事件 + "The CPU has been disabled by the guest operating system"（11:40:38.650）**（版本 B 的 int 13h 读失败 hlt 路径，该缺陷已被 WP06b b5 DAP 修复） |
| vmware.log + serial.log（11:41:16 启动） | OpenCubeOS-WP-10-chinese_support-fix3.iso（2.x GRUB 时代） | GRUB 菜单正常（gfxterm+serial 正常输出）→ 内核跑到 `[00:00:00.920] FAT32 (R/W)...OK`（71 行）后**戛然而止**（无 ush>）——2.x GRUB 轨在 VMware 的内核设备探测阶段死点（USB/virtio 探测区），不在本 WP 引导链范围，申报为遗留问题 |

**诚实申报（与工作包背景描述的差异）**：工作包背景称"VMware 启动不到 0.1 秒即三重故障重启"。该现象真实存在于 2026-11-14 的历史证据（evidence/wp-tui-input-encoding-hardening/vmware/vmware-reasoning.md：15 次 Triple fault 循环，串口有 OCBL 输出），4.5 修复（H1-H4）后 triple fault 循环消除。**本次附件（2026-11-22）显示的现象是版本 C 在 VMware 上"完全静默死循环"（无 triple fault 记录、无重启循环记录）**——即 4.5 修复后 OCRMECL 层（699a3d5b 引入）带来了更早的死点。串口文件被 11:41 会话 replace 覆盖，11:38/11:39 会话的串口增量内容无法恢复——这是证据缺口，如实记录。

## 六、反推结论与 4.2 差异补齐清单

**结论**：GRUB 的移交状态（六类）与版本 C 理论移交状态几乎全量等价或更严（3.4/3.5 更严）。版本 C 在 VMware 的静默死循环不在"移交状态"本身，而在**链路对"宽容串口"的隐性依赖**：任何一个早期串口轮询点在 VMware 环境不返回 THR-empty，整链即原地死循环——GRUB 时代内核从不在引导链轮询串口（GRUB 驱动有自己的实现与超时），OCRMECL/OCBL 把串口变成引导链的第一道关键路径却未设超时，这是与 GRUB 行为的本质差异，也是"对 QEMU 宽容环境的隐性依赖"的确切实例。

**补齐清单（4.2 实施）**：
1. F1 串口轮询全链超时（有限迭代上限，超时丢字符并标记串口退化，后续输出跳过——串口死不能挡引导）。
2. F2 COM1 存在性探测（SCR 写读回校验，失败则整条串口路径静默跳过——最快跳过，F1 兜底）。
3. F3 VGA 文本进度标记恢复（OCRMECL stage1/core、OCBL stage1 每阶段在 0xB8000 写阶段字符——恢复版本 B 的可观测性，VMware 无串口可见死点）。
4. F4 EFER 标准化清零（ocbl_handoff 降级链 PG off 后 `wrmsr` 清 EFER → 移交状态与 GRUB 完全一致 EFER=0）。
5. F5 回归验证：BIOS/UEFI 双轨默认+严格矩阵（-cpu qemu64,enforce -machine vmport=off -boot strict=on）稳定至 ush>。

**遗留申报（不在本包范围）**：2.x GRUB 轨内核在 VMware 的 FAT32 注册后死点（附件 serial.log 71 行处）属于内核设备探测层，需要独立工作包；本包修复引导链后用户在 VMware 复测时，VGA 进度标记将把新死点（若有）直接暴露在屏幕上。

## 七、证据文件

- 三版本快照：workspace/snapA（1b8c563b）、workspace/snapB（ba98aa1c）、工作树（da77ad06）
- QEMU 基线：默认参数冒烟（/tmp/smoke/bios-default.log，ush> 到达）；严格矩阵双轨 GREEN（evidence/wp-runtime-hardening-and-experience-rebase/strict/）
- VMware 附件：/home/z/my-project/upload/{vmware.log,vmware-0.log,vmware-1.log,vmware-2.log,serial.log}
- 历史 VMware 证据：evidence/wp-tui-input-encoding-hardening/vmware/vmware-reasoning.md
