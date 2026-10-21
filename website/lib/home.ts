// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Bilingual home-page data. zh strings are verbatim from lib/site.ts
// (which itself mirrors README.md / docs/INTERFACES.md / real build
// outputs); en strings are their translations — no numbers changed.

import type { Locale } from "@/lib/i18n";

export type Bi<T> = { zh: T; en: T };

export function pick<T>(bi: Bi<T>, locale: Locale): T {
  return bi[locale];
}

function fmtBytes(n: number): string {
  return n.toLocaleString("en-US");
}
export { fmtBytes };

// Current stats (same caliber as the README Stats section, re-verified at
// WP-10-tui-native: 140,897 lines = all git-tracked .c/.h/.S minus the four
// generated data files:
// git ls-files | grep -E '\.(c|h|S)$' | grep -vE 'userprobs_data|chinese_font_data|pinyin_data|grub_boot_data' | xargs wc -l
// 53 syscalls via grep -c '#define SYS_' kernel/core/core_syscall.h,
// 138 L1 interfaces (closed numbered set; the native batch touched no
// numbered registry — tui_dt_editor and friends are application layer) /
// 224 commands (shell/ untouched by the native batch) per README +
// docs/INTERFACES.md, 25 work packages = the latest counted total per
// README Stats lineage (22 at chinese_support-fix2 + tui-unified + tui-qa
// + tui-native). This is the RENDERED source (HomeView imports from
// here) - lib/site.ts carries the release assets.
export const STATS: Bi<{ value: string; label: string }[]> = {
  zh: [
    { value: "137,495", label: "行源码" },
    { value: "146", label: "L1 扩展接口" },
    { value: "53", label: "系统调用" },
    { value: "226", label: "shell 命令" },
    { value: "26", label: "工作包" },
    { value: "41/41", label: "P0 修复测试" },
  ],
  en: [
    { value: "137,495", label: "lines of source" },
    { value: "146", label: "L1 extension interfaces" },
    { value: "53", label: "system calls" },
    { value: "226", label: "shell commands" },
    { value: "26", label: "work packages" },
    { value: "41/41", label: "P0 fix tests" },
  ],
};

// WP-10-tui-native (native TUI completion + zh localization) leads the
// list, then the tui-qa/unified/fix lineage
export const FEATURES: Bi<
  { name: string; tag: string; desc: string }[]
> = {
  zh: [
    {
      name: "崩溃恢复：统一 Panic 捕获 + 内核转储 + 崩溃日志 + 恢复策略 + 栈护栏 + SIGSEGV + TUI 隔离",
      tag: "WP-11-crash_recovery",
      desc: "内核崩了，系统不再彻底死透：统一 panic 路径（横幅 + 16 寄存器快照 + 调用栈回溯，串口与 VGA 红字双通道；L1 观察者链；OC_ASSERT）；内核转储持久化到 /var/crash/core.dump（无文件系统时串口十六进制输出，dumpinfo 解码）；32 项崩溃日志环 + /var/log/crash.log 持久化；HALT/REBOOT 恢复策略（QEMU 实测 panic→复位→二次启动）；软看门狗；内核栈保护页 + 调度边界检查 + KERNEL STACK OVERFLOW 归因；Ring 3 SIGSEGV 真实投递（3 次乒乓保护，run sigsegv_test/stackovf/badapp）；TUI 崩溃隔离（代码窗口内 ring-0 异常 longjmp 回存活桌面，Crash test 页一键验证）。新命令 crashtest + dumpinfo，crashlog 补全（224→226）；L1 接口 138→146。门禁（发布 ISO 实测）：WP-11 E2E 14/14；回归 28/28；dhtest；SSH 5/5；HTTPS 7/7；BIOS+UEFI；0E0W。#DF 边界如实申报（KNOWN_ISSUES 15.13）。137,495 行源码。",
    },
    {
      name: "TUI 应用原生补全与全面中文化：原生编辑器 + 文件管理器完成体 + 交互缺陷修复",
      tag: "WP-10-tui-native",
      desc: "对统一桌面五大应用的自查补全轮，组 A/B/C 全部交付。组 A：原生 TUI 编辑器（1089 行：多行缓冲 + 光标/剪贴板 + F4 拼音 IME 中文输入 + ^O 保存/F12 另存/重载往返，cat 逐字节 UTF-8 回读）+ 文件管理器完成体（新建/重命名/复制/移动/删除全路径 + 确认弹窗 + 属性页 + 内置查看器 + 挂载页）。组 B：数字 1-9 快开对齐 + 焦点/生命周期修复。组 C：全局中文化扫尾（终端横幅×3、桌面初始化错误×3、扩展实验室 20 处双话）+ 交互缺陷修复（Files 删除确认取消后不再退出整个应用）。新增 E2E 资产：36 检查原生 E2E ×2 固件 + 隔离场景验证器（编辑器 ASCII+CJK 全链往返/zh 终端横幅字节级/delete-cancel/tz.persist）。门禁：0E0W；回归 28/28；tui-test 93/93；原生 E2E 31/36×2（5 项长会话 QEMU 串口丢字节隔离复跑 6/6 全 PASS，内核功能无损，KNOWN_ISSUES 15.12）；73 张截图。140,897 行源码。",
    },
    {
      name: "统一桌面全量质检：22 项确认修复 + 五应用深驱动 E2E + 中文渲染像素级验证",
      tag: "WP-10-tui-qa",
      desc: "对统一桌面的全量质量轮，九组修复（A–I）全部提交：P1×2（LSI SCRIPTS 缓冲溢出、X509 根搜索悬垂指针——现代真实证书链崩溃，ASAN 复现修复后真链验证）；P2×4（入向 SACK 映射、exFAT FAT 定容、版本串重编译触发器、串口 TUI 输入一字节滞后）；DHCP server-id / ACPI \_WAK / IR 与蓝牙 COM2 互斥 / fsck 诚实三态 / GPT fixture CRC / bt 掩码次序 / wifi 设备表迭代 / power 测试诚实判定 / ush / ld_so / l1_wp8b 健壮性 / cn_add 宽度 / 中文孪生页复活 / 护栏 4 深栈。新测试资产：五应用深驱动 E2E 34 检查×双启动（Files 真 VFS 操作由 ls 复核、Editor 真 nano 往返由 cat 复核、ring-3 ush 受控回收、控制面板全分支、F2 热切换）；截图门禁 4×2 + 8x/12x 放大 + VLM 亲读底部像素；tui-test 90→93。误报撤回 1 项（kill 128+SIGKULL 为全仓惯例）；无线 RX 入 KNOWN_ISSUES 15.10。门禁：0E0W；回归 28/28；TUI E2E 37/37×2；中文 E2E 21/21×2；深驱动 34/34×2；截图 4/4×2；host 18/0/1；dhtest 5/5；priv_test #GP；SSH 5/5；HTTPS 7/7。",
    },
    {
      name: "统一 TUI 字符桌面 + 五大桌面应用 + Ring 3 I/O 围栏",
      tag: "WP-10-tui-unified",
      desc: "四部分交付：1）统一入口：tui 为唯一 TUI 入口（tui-cn 启动器移除，命令 225→224，tui-* 应用命令保留），en/zh 标签由 shell.lang 驱动、F2 运行时切换并持久化；2）全动态 6 层菜单注册表（tui_dt_node_add/remove/find/depth，重复/孤儿/深度 7 全拒绝）驱动一切条目；3）五大桌面应用走真实子系统：Files（VFS 文件管理器：readdir/stat/改名/删除/挂载表/剪贴板）、Processes（活 g_procs 表 + 真 PMM 统计 + kill-by-tid 通用 L1 API）、Editor（真 nano 走 shell 命令路径，^O 保存 ^X 退出）、Terminal（Ring 3 ush，纯 syscall）、System Info；另有控制面板/中文输入板/命令桥（shell_cmd_enum + shell_execute_captured）/扩展实验室；数字 1-9 快开 + 状态栏提示；4）异常隔离双向闭环：页面崩溃由 tui_dt_guard setjmp 拦截（菜单存活），Ring 3 端口 I/O 全面封锁——TSS I/O 许可位图偏移字段错放在 0x64（SDM 表 8-2 规定 0x66），CPU 读到 0 指向全零 TSS 头部致 WP-02 以来所有端口对用户态放行（priv_test 实测 out 0x70 不 fault 而后修复），修复后 run priv_test 实测 #GP(0) at cs=0x33、进程被杀内核继续（BIOS+UEFI）。门禁：0E0W；tui-test 90/90×2；回归 28/28；中文 E2E 21/21×2（tui-cn 检查改走桌面拼音板+数字快开）；TUI E2E 37/37×2（reenter 回归改桌面根）。138,458 行源码。",
    },
    {
      name: "行编辑修复 + TUI 第二前端 + L1 扩展注册表（tui-time/tui-sys/tui-drv/tui-ext）",
      tag: "WP-10-chinese_support-fix4",
      desc: "双缺陷用户实锤修复 + TUI 补全为系统第二前端 + L1 扩展注册表。1）OC 提示符行编辑（WP-08 补全时代缺陷，方向键/历史一按即乱）：interactive_loop 自行打印 oc> 而 lineedit 会话持有空 prompt，每次重绘从列 0 起步覆盖提示符（截图实锤 help_/_lp）——shell_lineedit_readline 现将 $PS1 同步进会话并统一打印；第二缺陷：擦除宽度只盖当前行尾，长历史切短行留残影（hello world t.txt_）——会话记 last_drawn_w，重绘擦 max(当前尾, 上次尾)。CJK 行编辑端到端实测：输入你好世界→左×2→Delete 删整字世→缓冲=你好界（串口字节级验证）。2）渲染：echo/ls 用户内容改走 console_cn 路径（ASCII 字节不变，UTF-8 渲染真字形）；系统性验证：120 个底部墨迹密集字上屏与字库位图逐像素比对，30720 像素 0 错配——fix3 时代『下半缺 1/5』报告实为截图裁剪伪影。3）TUI：tui-time（时钟/时间源/真 CMOS RTC 写/时区）、tui-sys（8 设置页，与 shell 同 lib_config 键）、tui-drv（12 硬件页含新增 Display）、tui-ext（L1 扩展实验室）；L1 扩展注册表 7 组接口（menu/page/widget/command/theme/hotkey/event），自测 ALL PASS 上屏。Dvorak 键盘布局（kbd.layout 运行时切换，PS/2 q 回显 '）；log.level 静默档；shell 命令表容量修复（224→232）。命令 221→225，help 三模式不变。门禁：0E0W；回归 28/28（基线 225）；中文 E2E 21/21×2；TUI E2E 37/37×2；chinesetest 101/101；dhtest 5/5；SSH 5/5；HTTPS 7/7；host 17/0/2。135,538 行源码。",
    },
    {
      name: "截图驱动修复批次：zh 消息屏幕渲染 / PCI 页 ID / 配置持久化真修复",
      tag: "WP-10-chinese_support-fix3",
      desc: "以截图审查代替 ANSI 断言的修复批次：QEMU screendump→PNG 逐页人审，发现并修复三个串口断言看不见的真缺陷。1）zh 系统消息（横幅/help 头/未知命令）在帧缓冲上渲染为空心方框：shell_lang.c/main.c/shell.c 走 screen_console_puts（8×16 ASCII 字体）而头注释声称 console_cn——全部 shell_msg() 输出点改走 console_cn_puts/putcw（ASCII 路由不变，CJK 16×16 渲染），help -w 尾部本地化（en 列字节不变）。2）TUI PCI 页（英文 tui-hw 与中文 tui-cn 孪生）打出字面 2x:2x,0 4x:4x class 2x2x：页面本地格式化器不支持 %02x/%04x 零填充宽度（WP-10-tui 时代缺陷走查现形）——双修后打印真实 ID（00:00.0 8086:1237 … 00:03.0 8086:100e）。3）跨重启持久化声明为假：ATA DMA 驱动按控制器/单元注册 hda..hdd 而 lib_config_init 硬编码挂 ata0 别名，静默回退 ramfs 且 lib_config_write 对内存副本 rc=0——lib_config 现先试别名再枚举全部已注册块设备，lang 按后端诚实报告（FAT32=已持久化 / ramfs=仅内存重启丢失）。真实盘 snapshot-off 端到端实测：boot1 写 zh→reboot→boot2 中文横幅+Shell 输出语言：zh+宿主 mtype 从 etc.img 读回 shell.lang=zh。用户决定落实：suspend/suspend_test 隐藏（222→220）、新增 lang（→221）；shell.lang=en|zh 为校验过的配置键，shell.lang=zh 时 tui 直开中文启动器。门禁：0E0W；中文 E2E 21/21 BIOS+21/21 UEFI（挂配置盘）；TUI E2E 37/37×2；chinesetest 101/101；全量回归 28/28（基线 221）；全中文 TUI 页面+更新页截图走查。128,053 行源码。",
    },
    {
      name: "用户报告修复批次：全字形渲染 + TUI 会话卫生 + 中文页对齐 + 真实模式 S3",
      tag: "WP-10-chinese_support-fix2",
      desc: "chinese_support 发布后的用户报告修复批次：每个 CJK 字形完整渲染（WIDE_CONT 单元不再被背景色重绘吞掉右半）；三条 TUI 会话泄漏路径关闭（tui/tui-cn/chinesetest 退出后屏幕与光标恢复）；CSI 解码器孤立 ESC 直达而非吞掉；硬件页真实滚动；音量/时钟偏好被真实消费；鼠标事件达菜单；中文更新页跑真实 OTA 流程（检查/下载/SHA256/安装/回滚结果行在操作后保持可见）；中文页与英文页对齐（进程优先级+滚动、监控自动采样、窗口 resize）；S3 恢复跳板重写为真 16 位 big-real-mode 代码（SeaBIOS farcall16big 进入，旧 32 位代码首指令 #UD 风暴）且恢复链全部重新武装（定时器/PCI 命令寄存器/legacy e1000/UART RX）；fsck 经 blk 注册表（mkfs 后的 sda/vda 可 fsck）；ping/dhcp 返回诚实退出码、shell 支持 $?。门禁：0E0W；TUI E2E 37/37×2；CN E2E 17/17×2；渲染像素 E2E ALL PASS；suspend E2E 双 QEMU 代（10.0.13+8.1.5）ALL PASS；system E2E 15/15；回归 28/28；chinesetest 101/101。127,616 行源码。",
    },
    {
      name: "chinese_support 发布后实测修复：会话死亡根因 + 持久化/E2E 收尾",
      tag: "WP-10-chinese_support-fix1",
      desc: "发布 ISO 上 1c-B 会话死亡实测 8/12 后定位三处根因并修复：TUI 会话计数器/池复用缺陷、字体槽池回收时序、串口镜像抑制状态残留；修复后发布 ISO 实测连续会话全绿。同步补齐：site 时间线缺失批次补录、update 清单与下载资产三方一致复核。门禁：0E0W；中文 E2E 与 TUI E2E 双模式全绿；chinesetest 91/91（当时口径，fix2 扩至 101/101）；回归 28/28。",
    },
    {
      name: "中文支持：GB2312 字库 + 编码 + CJK 渲染 + 中文 TUI + 拼音输入法",
      tag: "WP-10-chinese_support",
      desc: "中文支持进入 L0：chinese/ 模块内嵌 GB2312 16×16 点阵字库（6763 汉字 16-87 区 = 216,416 B 紧凑存储 + 682 符号 1-9 区 = 21,824 B，tools/gen_chinese_font.py 从 GNU Unifont 矩形轮廓精确栅格化；区/Unicode 双向索引表）；GB2312 + UTF-8 编解码（严格 RFC 3629：超长/代理对/越界/截断拒绝；经 Unicode 枢纽任意方向批量转换；启发式检测 ASCII/UTF-8/GB2312/混合）；可插拔字体注册表（font_register/font_select/font_get_bitmap/font_get_metrics）+ 混排宽度渲染器（控制台 cell 保持 8×16——汉字跨两格——cols/rows/滚动/ASCII 输出全部不变）；控制台 cn 写入路径（console_cn_putcw/puts_utf8，原生 putc 字节路径不动）；UTF-8 行编辑（字符边界光标移动/退格/删除/Ctrl+W + 显示宽度重绘；串口桥以 +0x100 偏置放行 8-bit 字节，粘贴中文直达行编辑器）；UTF-8 感知 cat；TUI 中文层（tui_cell_t 本就存 u32 码点；tui_print 解码 UTF-8，宽字格带 TUI_WIDE_CONT）+ 三个中文应用（硬件配置/无线网络/系统信息——与其英文孪生应用同源实时驱动数据，中文标签与描述）+ 拼音输入法（GB2312 一级字 3755 字/396 无调音节，数字键选候选，tools/gen_pinyin_table.py 生成）。9 条新命令（chinese/chinesetest/font/font-test/encoding/encoding-test/ime/ime-test/tui-cn，213→222 只增不删）；L1 编号保持 1-138 封闭——34 个新接口为驱动 API 风格单列于 EXTENSIONS_WP10-chinese_support.md。门禁：0 errors 0 warnings；chinesetest 内核内 91/91；中文 E2E 14/14 BIOS + 14/14 UEFI；TUI E2E 32/32 BIOS + 32/32 UEFI；全量回归 28/28（命令基线 222）；dhtest 5/5；SSH E2E 5/5；HTTPS E2E 7/7；宿主套件 17/0/2；像素级验证 CJK 渲染（截屏 4/4 字形块 + 572 标题像素）。125,776 行源码。",
    },
    {
      name: "TUI 框架 + 硬件配置 / 无线网络 / 系统信息三应用",
      tag: "WP-10-tui",
      desc: "终端用户界面（TUI）进入 L0：tui/ 12 文件、双真渲染后端（framebuffer 字符控制台 + ANSI COM1 串口终端，16 色 CGA/VGA 调色板 + 256 色最近项映射）、窗口系统、文本/属性、输入（OC_KEY 映射 + 真 ANSI CSI 解码器 + SGR 鼠标 + 触摸板路径）、panels、menus、forms；硬件配置 TUI（11 域走实时驱动查询 API：CPU/内存/存储/网络/声卡/USB/PCI/输入/摄像头/无线/定时器，9 类配置经 lib_config 持久化到 /etc/opencube.conf：tui.backend/tui.clock_24h/tui.tz_offset/snd.volume）；无线网络 TUI（WiFi 扫描/列表/连接走真 STA 状态机 + 蓝牙管理走真 HCI 栈）；系统信息 TUI（概览/进程走活任务表/日志环查看器/实时监控/电源/更新/时间/帮助）。5 条新 shell 命令（tui/tui-test/tui-hw/tui-net/tui-sysinfo），总数 208→213（只增不删）；ring3 socket API（SYS 103-109，syscall 总数 46→53）；EXTENSIONS_WP10-tui.md 59 接口契约（头文件/注释/默认实现/文档）。附带交接修复批 12 项全部处置：WiFi→driver_nic 桥（wl0）+ net_poll 全卡遍历、USB 常驻轮询线程真启动、OTA 验签入 KNOWN_ISSUES（密钥托管决策）、fs/_raw 缓存旁路入档、access_ok_str 16 页上限、fork/MAX_TASKS=32 入档、SB16 mixer/UAC1 真音量、make test 宿主套件接入（17/0/2）、174 处 File: 头修正、P3 258 条入账 KNOWN_ISSUES §15.1。门禁：0 errors 0 warnings；TUI E2E 32/32 BIOS + 32/32 UEFI；全量回归 28/28；SSH 5/5；HTTPS 7/7。121,890 行源码。",
    },
    {
      name: "摄像头 / 并口 / 游戏端口 / 红外 / SCSI 五域落地",
      tag: "WP-10-camera_misc",
      desc: "五个硬件域作为裸金属驱动进入 L0：UVC 摄像头（WP-10d USB 栈上的 USB Video Class 1.1 描述符解析、Probe/Commit 控制、备用设置切换、基线 JPEG 解码器 + YCbCr→RGB、20 接口 camera 注册表）；IEEE 1284 并口（SPP 正/反向、EPP 1.7/1.9 地址/数据周期、ECP 模式+FIFO、LPT1-LPT3）；传统游戏端口（0x200/0x201 RC 充放电时间轴测量 + 校准、4 按钮）；红外（CIR 走 ACPI + SIR 走第二 16550 环回实测，NEC/RC-5/RC-6/SIRC 编解码）；SCSI（SAM 风格命令核心：CDB 构建器/响应解析器/每设备队列/UNIT-ATTENTION 重试，LSI 53C895A HBA 后端经 PCI I/O BAR + mini-SCRIPTS 编程含异步断开/重选协议；直存目标以 sda.. 落入 blk 层走 MBR/GPT）。10 条新命令（uvc/uvctest/parallel/paralleltest/gameport/gameporttest/ir/irtest/scsi/scsitest），总数 198→208（只增不删）。门禁：0 errors 0 warnings；批次 E2E 12/12 BIOS + 12/12 UEFI（LSI + scsi-hd + 并口字符设备实测）；全量回归 28/28；SSH 5/5；HTTPS 7/7；host_camera_test 30/30（-Os）；host_ir_test 21/21；host_scsi_test 49/49。",
    },
    {
      name: "无线 + 蓝牙落地（802.11 七族 / SDIO / HCI-L2CAP-RFCOMM-SDP-SPP-HID）",
      tag: "WP-10-wireless_bluetooth",
      desc: "802.11 核心与七族无线驱动进入 L0，外加完整蓝牙栈，并在 QEMU 实测：802.11 核心（帧控制/信息元素/信标解析、开放系统认证+关联状态机、LLC/SNAP 数据桥接）；Intel iwlwifi（7000/8000/9000/AX200/AX201/AX210：CSR 寄存器组、ucode 容器解析（legacy 'UCOD' 五段 + TLV 0x0a4c4657）、FH 固件加载）；Realtek rtl8188/8192/8821（PCIe + USB vendor-request 寄存器访问、8188E 寄存器映射、efuse、8051 固件下载、TXDESC）；Atheros ath9k/ath10k/ath11k（AR9002/9003 MAC 寄存器与收发描述符；CE 描述符/HTC/BMI/WMI-TLV；MHI 环元素/QMI TLV）；Broadcom b43（SSB BAR0 核心窗口）与 brcmfmac（PCIe + 经真实 SDHCI/SDIO 总线的 SDIO 客户端）；蓝牙：HCI 核心、H4 UART 传输（COM2 0x2f8/IRQ3，16550 环回探测 + 中断 RX 环，唯一 FIFO 消费者避免经典竞态）、HCI-USB btusb 类驱动、L2CAP 信令（响应信箱 + 幂等 connect）、RFCOMM TS 07.10 复用器（真实 CRC-8 FCS、PN/MSC）、SDP 客户端（真实 (attr-id,value) 对遍历器）、SPP 与 HID profile。新增 2 条 shell 命令（wifi / bt），总数 196→198（只增不删），L1 编号保持 1-138 封闭，shell wp 标签字段 24→32 字节。无线各族按工作包测试矩阵做 datasheet 对照级验证（QEMU 无 802.11 模型：字节级向量走生产代码 + 枚举路径真实执行；厂商固件不随附，缺固件诚实 DOWN）；蓝牙在真实 16550 模拟硬件上端到端实测（tools/bt_test_peer.py 主机侧 H4 控制器对端），本批 E2E 自查发现并修复 8+ 个真实缺陷（HCI 异步事件完成与参数偏移、ACL/L2CAP 双帧发送、16550 FIFO 竞态、L2CAP CFG_REQ 越界、RFCOMM 体偏移与 C/R 门控、SDP 遍历器布局、夹具未清零、wp 字段差一字节）。门禁：0 errors 0 warnings；无线/蓝牙 E2E 32/32 BIOS + 32/32 UEFI；全量回归 28/28；dhtest 5/5；SSH 5/5；HTTPS 7/7；host_wifi_test 38/38；host_bt_test 22/22。121,697 行源码。",
    },
    {
      name: "时间源四域落地（CMOS RTC / HPET / ACPI PM Timer / TSC）",
      tag: "WP-10-timer_rtc",
      desc: "四个硬件时间源进入 L0 并在 QEMU BIOS+UEFI 双启动实测：CMOS RTC（状态寄存器 A/B/C/D 解码：UIP/周期频率/PIE/AIE/UIE/BCD/24 小时制/VRT/FADT 世纪寄存器 + IRQ8 更新结束/周期/闹钟三种真实中断测试，测试后无条件恢复 Reg A/B）；HPET（消费 ACPI HPET 表 + MMIO 驱动，64 位主计数器重读高位、GCAP 能力解码（QEMU 0xfed00000、100 MHz），timer 1 经 LEG_RT legacy 路由到 IRQ8 的单次/周期比较器测试，测试期限用 HPET 计数器自身兜底）；ACPI PM Timer（FADT PM_TMR_BLK 解析，SeaBIOS 0x608 / OVMF 0xb008，24 位有效掩码回绕算术 + 64 位回绕扩展 + 与 PIT 对比校准，pmtimer.width=32 配置覆盖）；TSC（cpuid 序列化 rdtsc、不变 TSC 探测、PIT/HPET/PMTimer 三参照校准 + 最优参照自动校准 + TSC 忙等延迟）。新增 4 条 shell 命令（rtc hpet pmtimer tsc），总数 192→196（只增不删），L1 编号保持 1-138 封闭。实测中根因修复 4 个上电缺陷（acpi_hpet_t 缺表头且 block ID 拆 6 字节——该结构首个真实消费者；PM timer 24 位回绕被误读为倒退 3.6 秒；TSC-vs-HPET 校准在冻结计数器上死循环；HPET 周期测试期限未随次数缩放）。门禁：0 errors 0 warnings；时间源 E2E 29/29 BIOS + 29/29 UEFI；全量回归 28/28；dhtest 5/5；SSH E2E 5/5；HTTPS E2E 7/7。RTC UIE 3/3、PIE 32/32、闹钟触发；HPET 计数器实测 100,113,820 Hz（标称 100,000,000）、单次 1/1、周期 3/3；PMT 校准 3,576,824 Hz（偏差 7 bp）；TSC 双跑漂移 86 bp、HPET 互校 0 bp、延迟 100/100 ms。114,593 行源码。",
    },
    {
      name: "输入设备 + 芯片组五域落地（触摸板/PCIe/ACPI/SMBus/I2C）",
      tag: "WP-10-input_chipset",
      desc: "五大功能域进入 L0 并在 QEMU BIOS+UEFI 双启动实测：触摸板 I2C HID 全协议栈（描述符解析 + type-B 多点解码 + TAP/双指滚动/双指捏合/三指滑动手势引擎）；PCI/PCIe（CF8/CFC + MMCONFIG 扩展配置空间、设备缓存扫描、能力遍历、BAR 尺寸/分配、经 xAPIC 投递路径的 MSI/MSI-X 消息编程）；ACPI（RSDP 取自 Multiboot2 tag 14/15、RSDT/XSDT/FADT/MADT/MCFG/HPET 解析、最小 AML 解释器、S5 关机 + S3 挂起经 PM1a_CNT + \_S5/\_S3 包 + FACS 唤醒向量、32 位恢复跳板、SCI(IRQ9)+GPE）；SMBus（quick/byte/word/block 读写，[count,payload] 自描述布局）；I2C（PIIX4 cfg0x90 + i801 class-0C05 cfg0x20 + DesignWare 后端，block 传输在 AUX_CTL 0x0D bit1 下自动递增）。新增 16 条 shell 命令（acpi acpidev acpitest acpipow msitest pcidump pcibars pcie i2cdetect i2cget i2cset i2ctest smbus smbustest touchpad touchtest），总数 176→192（只增不删）。实测中根因修复 11 个上电缺陷（I2C 总线注册清零 xfer、QUICK 探测被核心拒绝、字写误路由、AUX_CTL 偏移/位/时序、block 读计数丢失、LAPIC 基址掩码保留 BSP 位、LINT0 ExtINT 被掩蔽致串口失联、自 IPI shorthand 11=all-excluding-self、HID Input 旗标误读 item 头、fixture 页切换、设备路径缺 \ 根前缀）。门禁：0 errors 0 warnings；五域 E2E 28/28 BIOS + 28/28 UEFI；全量回归 28/28；dhtest 5/5；cryptotest 4/4；SSH E2E 5/5；HTTPS E2E 7/7。许可证：LICENSE 恢复为官方 Apache-2.0 逐字节文本（仅附录版权行为 cubestudio-dev）、NOTICE 单列 GRUB unicode.pf2 字体（GPL-3.0 + 字体例外）、SPDX 全覆盖（0 缺失）、112,589 行源码。",
    },
    {
      name: "P2 第二批 53 条复核属实 + 嵌入链三缺陷修复完成",
      tag: "WP-10-AUDIT_P2-fix2b",
      desc: "实测复核 fix2 全量载荷：53/53 条 P2 修复在树（8 条深度抽查属实）+ 4/4 findings（git describe 版本串实测生效、HTTPS 双向 close_notify 断言 PASS、INTERFACES.md 176、docs/verification/ 转公开）；修复嵌入链三缺陷（make userprogs 在 fix2 树上复现 ERROR: userprog_libfoo block not found）：libfoo.c 共享库改走独立 solib 链（新 tools/build_solib.py，-Wl,-soname,libfoo.so + DT_SONAME 校验，重建 l1/solib_data.h）；六个动态链接程序（dyn_hello/so_test/dlsym_test/pie_test/reloc_test/main_dyn）ET_DYN/PIE 配方落地（build_c_userprog.py 新增 pie/pie-foo 模式 + e_type/PT_INTERP 硬校验防静默降级 + 缺块自动追加）；libs/ld_so.c 纳入 USERPROG_SRCS（原 ld_so 分支为死代码）；main_dyn 补嵌入并在 main.c/core_syscall.c 双 exec 表注册。验证：make userprogs 端到端 EXIT=0；QEMU 六 dyn 程序全 PASS（main_dyn 输出 main: foo_add(2,3)=5）；21 条回归全绿（dhtest 10/10 + cryptotest + fstest p2 ALL PASS）；SSH E2E 5/5；HTTPS E2E + CLOSE_NOTIFY_ASSERT PASS + eof 不挂死；mkfs confirm 门双侧实测；help 自报 176 命令；统计 101,150 行。",
    },
    {
      name: "完整安全审查 + P0 全部修复 + P1 全部 94 条 + P2 第一批 53 条修复完成",
      tag: "WP-10-AUDIT_P2-fix1",
      desc: "18 路逐行审查 607 条发现（P0 41 / P1 94 / P2 214 / P3 258）；41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21，逐条有 QEMU FAIL-before/PASS-after、宿主 ASAN 或构造级路径证据）；p1fix1 修复前 31 个 P1（BUG-0042..0072）：每任务 FPU/SSE fxsave 上下文 + CR4.OSFXSR + fork 继承、PMM 位图 cli 原子性、页错误语义（P=1 拒绝、8MiB 栈下限、U/S 特权环、内核在用户地址空间拒绝）、XHCI 事件环 LINK 与轮询闸锁、EHCI CONFIGFLAG、OHCI 中断表/TD_R/NPS、MSC residue 与 sector_size、CDC-ACM 协议、SS EP0 mps9、FAT32 rmdir 点项 + UAF + 簇环越界 + unlink 保护、exFAT 位图生命周期、ext4 extent 偏移与恶意卷越界、e1000 strcat、ld_so 边界、网络 IP 帧校验 + RX 校验和 + 序号回绕 + RST 校验 + SYN_RCVD 回收 + SYN 选项 + udp_bind 去重 + 窗口缩放 + RTO 临界区；新增 sse_test/pf_test 用户测试，回归 tcptest/fork_test/ping/pmmrace 全绿。p1fix2 修复 P1 第 32~62 条（BUG-0073..0103）：SSH 主机密钥 TOFU 锚点（/etc/ssh_known_hosts 持久化，换钥硬失败拒连）与按机生成 RSA-2048 主机密钥（移除镜像内嵌万能私钥，客户端身份改 /etc/ssh_client_key，全协议调用点核验消费 rdrand CSPRNG）；TLS 按套件密钥长度（ChaCha20/AES-256-GCM 32B）、CertificateEntry 扩展跳过、32KiB transcript 懒分配+溢出硬失败、ServerHello 全边界、零长记录卡死上限、KeyUpdate/NST 递归改循环、X509 负长度四入口测试（ASAN 全拒）；shell capture 栈化、相对通配符、edit/cp 完整性、ls/tree/du 有界拼接；ush unalias 空参、nano/cp/mv 补 O_TRUNC、重定向 fd 生命周期（内核 console fd dup/dup2 + sys_read(0) 键盘行模式 ^D EOF）；L1 console hook 抑制契约、WP-08cd 注册真实接入 oc> 与 sys_execve 并补注销；文档 update_url/update_server.py 修正；新增 fdref_test/select_zero_test。WP-10-AUDIT_P2-fix1 修复 P2 第一批 53 条（BUG-0136..0188：内存/arch/USB 33 条/VFS-FAT 11 条），并发现修复堆块头 40 字节潜伏缺陷（负载对齐漂移致 ATA-DMA 偏移 8 字节、/etc 无法挂载）；fstest 新增 P2 双向断言块；新增 irqabitest/heapbounds/vmkernelpt/int3_user 测试。",
    },
    {
      name: "Shell 完全体（行编辑 + 工具 + 编辑器）",
      tag: "WP-10-wp08fix1",
      desc: "oc> 与 ush 双端全键位行编辑：上下键历史翻页（32 条，草稿保留）、左右/Home/End 光标、Ctrl+A/E/U/K/W、Delete、Ctrl+C、Tab 补全（命令表 + VFS 路径）。ush 补齐 15 个工具：ln（真实硬链接共享 inode + nlink）、ln -s（路径解析跟随，8 跳防环）、chmod/chown/sed/awk/ping/wget/netstat/ifconfig/ps/kill/top/du + 补实 help 承诺过的 stat/env。双端 nano 风格编辑器（nano/vi），^O 保存 ^X 退出；9 个新扩展接口（130-138）。",
    },
    {
      name: "用户角度可用性（第 ⑨ 条审计）",
      tag: "rule-9 audit",
      desc: "按“测试通过 ≠ 用户能用”对全部功能逐项审计：每功能五要素（有命令/有工具/有文档/有示例/有反馈）与五能力（能创建/能使用/能回滚/能查看/能理解）。落地：TRY-IT.md 用户指南（开机即试）、UPDATE-HOWTO.md OTA 分步指南、make_ab_disk.sh 一键 A/B 磁盘、mkfs/fsck 修复、play/volume 未知设备报可用名单；修复后 18/18 回归 + 存储/网卡/声卡/OTA 复测全 PASS。",
    },
    {
      name: "USB 主机栈（四控制器）",
      tag: "WP-10d",
      desc: "UHCI / OHCI / EHCI / XHCI 四个主机控制器后端全部设备级枚举跑通：HID 键盘/鼠标（中断 IN）、MSC 存储（BOT+SCSI，对接 blk + FAT32 读写往返）、CDC-ACM/FTDI 串口、UAC 1.0/2.0 音频；外部 Hub 级联枚举与真热插拔（等待窗口内 device_add 实测）；usb/usbdev 状态命令 + 7 个测试命令全部系统内可用。",
    },
    {
      name: "声卡驱动（六族）",
      tag: "WP-10c",
      desc: "Intel HDA（CORB/RIRB、codec/widget 枚举、BDL DMA、IOC 中断）、AC'97、Sound Blaster 16（ISA DMA）、ES1370、virtio-snd、USB Audio Class 1.0（新 UHCI 主机栈）：snd_register/play/stop/set_rate/set_volume/get_caps 扩展接口，44.1/48kHz，真 DMA 真中断，QEMU 五卡实测播放。",
    },
    {
      name: "系统内自动更新（A/B 分区）",
      tag: "WP-10u",
      desc: "Windows-Update 式更新：内核自带 gzip/DEFLATE + ustar 解包与流式 SHA256 校验，A/B 双分区安装、boot 标志自动回滚、update --local 离线更新；real_update_test 实测下载-安装-重启进入 slot B-回滚全链路。",
    },
    {
      name: "网卡驱动（九族）",
      tag: "WP-10b",
      desc: "e1000e / igb / ixgbe / RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx 及 3c59x、nForce、AR81xx、Yukon 识别与驱动：nic_register/send/recv/link_status/get_mac 扩展接口；e1000e、igb、RTL8139 在 QEMU 实测 DHCP/ping/HTTPS 全链路。",
    },
    {
      name: "AHCI SATA",
      tag: "WP-10a",
      desc: "SATA 硬盘/SSD 驱动：精确类别 0x010601 枚举、命令列表/H2D FIS、48 位 LBA DMA 读写、FLUSH CACHE，多控制器多端口。",
    },
    {
      name: "NVMe SSD",
      tag: "WP-10a",
      desc: "NVMe 驱动：管理队列对 + 双 I/O 队列对轮询、Identify、Read/Write/Flush，doorbell 提交完成。",
    },
    {
      name: "ATA Bus-Master DMA",
      tag: "WP-10a",
      desc: "经典 IDE 走 PCI BMDMA（BAR4 + PRDT）真 DMA 传输，PIO 路径保留为回退；自测含 DMA vs PIO 计时对比。",
    },
    {
      name: "virtio-blk",
      tag: "WP-10a",
      desc: "QEMU virtio 磁盘：容量读取修复（此前为 0）、virtqueue 请求/应答、读写刷全链路。",
    },
    {
      name: "FAT32 全设备挂载",
      tag: "WP-10a",
      desc: "FAT32 从直连 ATA 迁移到 blk 层：hda/sda/vda/nvme0 四类驱动均可 mkfs + 挂载 + 文件往返；MBR + GPT 分区解析。",
    },
    {
      name: "TLS 1.3 / TLS 1.2",
      tag: "kernel/tls.c",
      desc: "TLS 1.3（X25519 + AES-128-GCM，ChaCha20-Poly1305 套件）+ TLS 1.2 ECDHE-GCM 回退；X.509 CA 链验证（内嵌公共根），验证失败即握手失败。",
    },
    {
      name: "HTTPS",
      tag: "wget https://",
      desc: "wget https://host:port/path 经 TLS 下载，直接落入 VFS 文件；真实站点实测：GitHub Pages、google、cloudflare。",
    },
    {
      name: "SSH",
      tag: "ssh.c / sshd.c",
      desc: "客户端 + 服务端：curve25519-sha256 优先 + group14-sha256 回退、aes128-ctr 优先、hmac-sha2-256、主机密钥签名验证（TOFU + 指纹）、密码 + 公钥认证；与 paramiko 双向互操作。",
    },
  ],
  en: [
    {
      name: "Crash recovery: unified panic capture + core dump + crash log + policy + stack guards + SIGSEGV + TUI containment",
      tag: "WP-11-crash_recovery",
      desc: "When the kernel crashes, the system no longer just dies: a unified panic path (banner + 16-GPR register snapshot + call traceback on serial AND the red VGA console; an L1 observer chain where a handler claiming recovery resumes the faulting frame; OC_ASSERT), core dumps persisted to /var/crash/core.dump (hex-dumped on serial with no filesystem; dumpinfo decodes the header), a 32-entry crash log ring flushed to /var/log/crash.log, the HALT/REBOOT policy (QEMU-verified panic -> 8042 reset -> second boot), a soft watchdog, kernel-stack guard pages + scheduler bounds checks + KERNEL STACK OVERFLOW attribution with the task name, ring-3 SIGSEGV delivery with the exact syscall-path semantics (3-delivery ping-pong guard then force-kill; run sigsegv_test / stackovf / badapp prove catch, kill and parent-wake), and TUI crash containment (a ring-0 fault inside the linker TUI code window with an active guard longjmps back to the surviving desktop - the Control Panel Crash test page triggers one on demand). New commands crashtest + dumpinfo; crashlog completed (224 -> 226); L1 interfaces 138 -> 146. Gates, measured on this exact release ISO: WP-11 E2E 14/14 (14-scenario gate, every panic variant in a fresh QEMU boot), regression 28/28, dhtest, cryptotest, SSH E2E 5/5, HTTPS E2E 7/7, BIOS + UEFI boots, 0 errors 0 warnings. The one #DF boundary is stated honestly in KNOWN_ISSUES 15.13. 137,495 lines of source.",
    },
    {
      name: "Native TUI application completion + full Chinese localization: native editor + file manager final form + interaction fix",
      tag: "WP-10-tui-native",
      desc: "A self-audit completion round over the unified desktop's five applications, groups A/B/C all delivered. Group A: the native TUI editor (1089 lines: multi-line buffer + cursor/clipboard + F4 pinyin IME for Chinese input + ^O save / F12 save-as / reload round-trip, cat reads the file back byte-exact UTF-8) and the file manager final form (create/rename/copy/move/delete full paths + confirm dialog + attributes page + built-in viewer + mounts page). Group B: digit 1-9 quick-open alignment + focus/lifetime fixes. Group C: the global Chinese-completion sweep (terminal banners x3, desktop init errors x3, extensions lab 20 strings made bilingual) plus an interaction fix (canceling the Files delete confirmation no longer exits the whole application). New E2E assets: a 36-check native E2E x both firmwares + an isolated-scenario verifier (editor ASCII+CJK full round-trip / zh terminal banner byte-level / delete-cancel / tz.persist). Gates: 0E0W; regression 28/28; tui-test 93/93; native E2E 31/36 x2 (the 5 long-session flaps all pass in isolated fresh-QEMU re-runs 6/6 - QEMU 16550 key loss under the serial TUI TX flood, kernel function intact, KNOWN_ISSUES 15.12); 73 screenshots. 140,897 lines of source.",
    },
    {
      name: "Full QA sweep over the unified desktop: 22 confirmed fixes + deep-drive E2E + pixel-verified Chinese rendering",
      tag: "WP-10-tui-qa",
      desc: "A dedicated QA round over WP-10-tui-unified: the previous session's 22 pending findings re-verified through three channels (source read / in-process python / compiler + live QEMU) - 20 confirmed and fixed, 1 false positive withdrawn, 1 documented as an architecture gap (wireless RX has no QEMU model). Nine groups landed: LSI SCRIPTS buffer 16->32 dwords (P1: a 56-byte overflow landed in g_exfat_dir_ops); X509 root-search dangling pointer (P1: stack-use-after-scope - modern real chains like ISRG Root YR->X1 crashed the TLS trust path, ASAN-reproduced, re-verified against live github.io/cloudflare chains after the fix); inbound TCP SACK finally mapped into the rtx_sacked bitmap; DHCP server-id captured; ACPI _PTS passes the real state + S3 resume replays _WAK; exFAT mkfs sizes the FAT by fixed-point iteration + bounds; fsck BPB gate + honest FAIL/SCAN-INCOMPLETE/PASS verdicts; cn_add width specifiers; the ~1400 lines of Chinese twin pages revived through language-aware dispatchers; crash guard as a 4-deep stack; bt event-mask order; wifi test iterates the device table; honest power-test verdicts; ush/ld_so/l1_wp8b robustness; version_gen.h recompile trigger (incremental builds kept a stale banner); serial TUI input P2 (the fix2 ESC-stash now flushes on a dry queue - after any Esc every subsequent plain key lagged one behind; reproduced live under QEMU, tui-test 90->93). New test assets: a 34-check deep-drive E2E of all five desktop applications x BIOS/UEFI (Files operations through the real VFS verified by ls; a real nano round-trip verified by cat; ring-3 ush echo/ps/exit contained; every control-panel branch page; F2 live en->zh->en) and a 4-check framebuffer screenshot gate x2 with 8x/12x zoomed crops read back by a vision model (the glyph row reads zhong-wen-ce-shi OpenCube ce-shi, bottom pixel rows complete). Gates: 0E0W; regression 28/28; tui-test 93/93 x2; TUI E2E 37/37 x2; chinese E2E 21/21 x2; deep-drive 34/34 x2; screenshot gate 4/4 x2; host suite 18/0/1 (x509 google-chain fixture unreachable in the build sandbox, honest skip); dhtest 5/5; priv_test #GP contained; SSH E2E 5/5; HTTPS E2E 7/7.",
    },
    {
      name: "Unified TUI character desktop + five desktop applications + ring-3 I/O containment",
      tag: "WP-10-tui-unified",
      desc: "Delivered in four parts: (1) single entry - `tui` is the only TUI entry (the tui-cn launcher removed, 225 -> 224 commands; the tui-* app commands stay); en/zh labels driven by shell.lang with runtime F2 switching and persistence. (2) a fully dynamic 6-level menu registry (tui_dt_node_add/remove/find/depth; duplicate/orphan/depth-7 all refused) drives every entry. (3) five desktop applications over the real subsystems: Files (VFS file manager: readdir/stat/rename/delete/mount table/clipboard), Processes (live g_procs table + real PMM stats + kill-by-tid as the generic L1 API), Editor (the real nano over its live shell command path, ^O save / ^X exit), Terminal (ring-3 ush, syscall-only), System Info - plus Control Panel, the Chinese Input pad, the Command Bridge (shell_cmd_enum + shell_execute_captured) and the Extensions lab; digit 1-9 quick-open advertised in the status bar. (4) exception isolation closed at both boundaries: a page crash is contained by the tui_dt_guard setjmp barrier (the menu survives), and ring-3 port I/O is now fully denied - the TSS I/O-permission-bitmap offset field sat at 0x64 while SDM table 8-2 puts it at 0x66, so the CPU read the pad (0) as the bitmap offset and the bitmap pointed at the all-zero TSS header: every port was allowed from ring 3 since WP-02 (priv_test first ran open). After the fix, run priv_test shows #GP(0) at cs=0x33, the process is killed, the kernel continues (BIOS + UEFI). Gates: 0E0W; tui-test 90/90 x2; regression 28/28; chinese E2E 21/21 x2 (former tui-cn checks now drive the desktop pad via digit quick-open); TUI E2E 37/37 x2 (the re-enter round now exercises the desktop root). 138,458 lines of source.",
    },
    {
      name: "Line-editor fixes + TUI second frontend + L1 extension registry (tui-time/tui-sys/tui-drv/tui-ext)",
      tag: "WP-10-chinese_support-fix4",
      desc: "Two user-reproduced defects fixed + the TUI finished as the system's second frontend + the L1 extension registry. (1) OC prompt line editing (a WP-08 completion-era defect, broken the moment arrows/history were pressed): interactive_loop printed oc> itself while the editor session held an EMPTY prompt, so every redraw restarted at column 0 and overwrote the prompt (screenshots: help_ / _lp) - shell_lineedit_readline now syncs $PS1 into the session and prints it; second defect: the erase run covered only the current-line tail so long-history -> short-line left stale characters (hello world t.txt_) - the session tracks last_drawn_w. CJK line editing verified end to end: type 你好世界, Left x2, Delete removes the whole char 世, buffer = 你好界 (byte-exact on the serial mirror). (2) Rendering: echo/ls emit user content through the cn path (ASCII byte-identical, UTF-8 as real glyphs); systematic verification echoed 120 bottom-ink-dense characters and compared the screendump against the font bitmaps pixel by pixel - 30720 px, 0 mismatches; the fix3-era bottom-clip report is a screenshot-crop artifact. (3) TUI: tui-time (clock/sources/real CMOS RTC write/timezone), tui-sys (8 settings pages through the same lib_config keys the shell exposes), tui-drv (12 hardware pages incl. the new Display), tui-ext (the L1 extension lab); seven documented interface sets (menu/page/widget/command/theme/hotkey/event), selftest ALL PASS on screen. Dvorak layout (kbd.layout runtime switch; PS/2 q echoes an apostrophe), quiet log level, shell command table capacity fix (224 -> 232). Commands 221 -> 225, help three modes unchanged. Gates: 0E0W; regression 28/28 (baseline 225); chinese E2E 21/21 x2; TUI E2E 37/37 x2; chinesetest 101/101; dhtest 5/5; SSH 5/5; HTTPS 7/7; host 17/0/2. 135,538 lines of source.",
    },
    {
      name: "Screenshot-driven batch: zh message rendering / PCI page ids / real config persistence",
      tag: "WP-10-chinese_support-fix3",
      desc: "Screenshot review replaces ANSI assertions: every Chinese TUI page captured from QEMU (screendump -> PNG) and reviewed as an image, surfacing three real defects the serial assertions could not see. (1) zh system messages (banner/help headers/unknown-command) rendered as hollow boxes on the framebuffer: shell_lang.c/main.c/shell.c printed through the 8x16 ASCII font while the header comment claimed console_cn - every shell_msg() site now goes through console_cn_puts/putcw (ASCII routes unchanged, CJK renders 16x16) and help -w gained a localized tail (en column byte-identical). (2) The TUI PCI page (EN tui-hw + CN tui-cn twins) printed literal 2x:2x,0 4x:4x class 2x2x: the page-local formatters lacked zero-padded %02x/%04x width support (a WP-10-tui era defect the walkthrough surfaced) - real ids now (00:00.0 8086:1237 ... 00:03.0 8086:100e). (3) The cross-reboot persistence claim was false: the ATA DMA driver registers drives as hda..hdd while lib_config_init mounted /etc from the hard-coded ata0 alias, silently falling back to ramfs with lib_config_write rc=0 on the in-memory copy - lib_config now tries the alias then every registered block device and lang reports honestly per backend (FAT32 persisted / ramfs in-memory only). Verified end to end on a real disk (snapshot off): boot1 writes zh -> reboot -> boot2 shows the Chinese banner + Shell 输出语言：zh and host-side mtype reads shell.lang=zh from etc.img. User decision honored: suspend/suspend_test hidden (222 -> 220), lang added (-> 221); shell.lang=en|zh is a validated config key and tui opens the Chinese launcher when zh. Gates: 0E0W; Chinese E2E 21/21 BIOS+UEFI (config disk attached); TUI E2E 37/37 x2; chinesetest 101/101; regression 28/28 (baseline 221); full-page screenshot walkthrough incl. the update page. 128,053 lines of source.",
    },
    {
      name: "User-reported batch: full-glyph rendering + TUI session hygiene + CN parity + real-mode S3",
      tag: "WP-10-chinese_support-fix2",
      desc: "User-reported fix batch on the chinese_support release: every CJK glyph renders fully (the WIDE_CONT cell is no longer repainted with the background color, killing the right half of each hanzi); three TUI session-leak paths closed (tui/tui-cn/chinesetest leave the screen alive); the CSI decoder delivers a lone ESC instead of eating it; the hardware page really scrolls; volume/clock preferences are consumed; mouse events reach menus; the Chinese update page runs the real OTA flow (check/download/SHA256/install/rollback result lines stay visible after each operation); the Chinese pages match the English parity (process priority + scroll, monitor auto-sampling, window resize); the S3 resume trampoline is real 16-bit big-real-mode code (SeaBIOS farcall16big enters it) and the full resume chain re-arms (timer, PCI command registers, legacy e1000, UART RX); fsck goes through the blk registry (mkfs'd sda/vda are fsck-able); ping/dhcp return honest exit codes and the shell supports $?. Gates: 0E0W; TUI E2E 37/37 x2; CN E2E 17/17 x2; render-pixel E2E ALL PASS; suspend E2E ALL PASS on two QEMU generations (10.0.13 + 8.1.5); system E2E 15/15; regression 28/28; chinesetest 101/101. 127,616 lines of source.",
    },
    {
      name: "Post-release measured fixes: session-death root causes + persistence/E2E hardening",
      tag: "WP-10-chinese_support-fix1",
      desc: "The released ISO measured 1c-B session death 8/12 before this batch; three root causes located and fixed (TUI session counter/pool reuse, font-slot recycle timing, serial-mirror suppression residue); the released ISO then passes the session loop green. Also completed: the missing website timeline batches, update manifest + download assets 3-way re-verification. Gates: 0E0W; Chinese/TUI E2E green on both modes; chinesetest 91/91 (then-baseline, extended to 101/101 at fix2); regression 28/28.",
    },
    {
      name: "Chinese support: GB2312 font + encoding + CJK rendering + Chinese TUI + pinyin IME",
      tag: "WP-10-chinese_support",
      desc: "Chinese support enters L0: the chinese/ module carries the embedded GB2312 16x16 bitmap font (6763 hanzi zones 16-87 = 216,416 B compact + 682 symbols zones 1-9 = 21,824 B, generated exactly from the GNU Unifont rectangle outlines by tools/gen_chinese_font.py; zone/Unicode index tables both directions); the GB2312 + UTF-8 codec (strict RFC 3629: overlong/surrogate/>U+10FFFF/truncated rejected; bulk conversion any direction through the Unicode pivot; heuristic detection ASCII/UTF-8/GB2312/mixed); a pluggable font registry (font_register/font_select/font_get_bitmap/font_get_metrics) with the mixed-width renderer (the console cell stays 8x16 - a CJK glyph spans two cells - so cols/rows/scroll/ASCII output are unchanged); the console cn write path (console_cn_putcw/puts_utf8 with the stock putc byte path untouched); UTF-8 aware line editing (character-boundary cursor movement/backspace/delete/Ctrl+W + display-width redraw; the serial bridge now passes 8-bit bytes biased +0x100 so pasted CJK reaches the editor); UTF-8 aware cat; the TUI Chinese layer (tui_cell_t already stores u32 codepoints; tui_print decodes UTF-8, wide cells carry TUI_WIDE_CONT) with three Chinese applications (hardware configuration/wireless/system information - same live driver data sources as their English twins, Chinese labels and descriptions) and a pinyin IME over the GB2312 level-1 set (3755 chars, 396 tone-free syllables, digit-key candidates, generated by tools/gen_pinyin_table.py). 9 new commands (chinese/chinesetest/font/font-test/encoding/encoding-test/ime/ime-test/tui-cn, 213->222 additions only); the numbered L1 set stays closed at 1-138 - the 34 new interfaces are driver-API style, single-listed in EXTENSIONS_WP10-chinese_support.md. Gates: 0 errors 0 warnings; chinesetest 91/91 in-kernel; Chinese E2E 14/14 BIOS + 14/14 UEFI; TUI E2E 32/32 BIOS + 32/32 UEFI; full regression 28/28 (baseline 222); dhtest 5/5; SSH E2E 5/5; HTTPS E2E 7/7; host suite 17/0/2; pixel-verified CJK rendering (4/4 glyph blocks + 572 title pixels on the screendump). 125,776 lines of source.",
    },
    {
      name: "TUI framework + hardware configuration / wireless / system info apps",
      tag: "WP-10-tui",
      desc: "A terminal user interface (TUI) enters L0: tui/ (12 files), two real render backends (framebuffer character console + ANSI COM1 serial terminal, 16-color CGA/VGA palette + 256-color nearest-entry map), window system, text/attributes, input (OC_KEY mapping + a real ANSI CSI decoder + SGR mouse + touchpad path), panels, menus, forms; the hardware configuration TUI (11 domains through the live driver query APIs: CPU/memory/storage/network/sound/USB/PCI/input/camera/wireless/timers, 9 config keys persisted via lib_config to /etc/opencube.conf: tui.backend/tui.clock_24h/tui.tz_offset/snd.volume); the wireless network TUI (WiFi scan/list/connect through the real STA state machine + Bluetooth manager through the real HCI stack); the system information TUI (overview/processes through the live task table/log ring viewer/live monitor/power/update/time/help). 5 new shell commands (tui/tui-test/tui-hw/tui-net/tui-sysinfo), total 208->213 (additions only); the ring3 socket API (SYS 103-109, syscalls 46->53); EXTENSIONS_WP10-tui.md with a 59-interface contract (header/comment/default impl/doc). The handover pre-fix batch (12 suspicions) is fully dispositioned in the same tree: the WiFi->driver_nic bridge (wl0) + net_poll draining every present NIC, the resident USB poll thread actually starting, OTA signature verification tracked in KNOWN_ISSUES (key custody decision), the fs/_raw cache bypass documented, access_ok_str bounded to 16 pages, fork/MAX_TASKS=32 documented, real SB16 mixer/UAC1 volume, the make test host-suite target (17/0/2), 174 stale File: header paths fixed, and the 258 unremediated P3 findings ledgered in KNOWN_ISSUES 15.1. Gates: 0 errors 0 warnings; TUI E2E 32/32 BIOS + 32/32 UEFI; full regression 28/28; SSH 5/5; HTTPS 7/7. 121,890 lines of source.",
    },
    {
      name: "Cameras / parallel port / game port / infrared / SCSI: five domains landed",
      tag: "WP-10-camera_misc",
      desc: "Five hardware domains as bare-metal drivers: UVC cameras (USB Video Class 1.1 descriptor parser over the WP-10d USB stack, Probe/Commit control, alternate-setting switching, a baseline JPEG decoder with YCbCr->RGB, a 20-interface camera core); the IEEE 1284 parallel port (SPP forward/reverse, EPP 1.7/1.9 address/data cycles, ECP mode+FIFO, LPT1-LPT3); the legacy game port (0x200/0x201 RC-timing axis measurement with calibration, 4 buttons); infrared (CIR over ACPI + SIR over the secondary 16550 loopback-verified, NEC/RC-5/RC-6/SIRC codecs); and SCSI (a SAM-style command core with CDB builders, response parsers, per-device queues and UNIT-ATTENTION retry, plus an LSI 53C895A HBA backend programmed through its PCI I/O BAR and mini-SCRIPTS programs including the asynchronous disconnect/reselect protocol; direct-access targets land in the blk layer as sda.. and go through MBR/GPT like every other backend). 10 new commands (uvc/uvctest/parallel/paralleltest/gameport/gameporttest/ir/irtest/scsi/scsitest), total 198->208 (additions only). Gates: 0 errors 0 warnings; batch E2E 12/12 BIOS + 12/12 UEFI (LSI + scsi-hd + parallel chardev verified live); full regression 28/28; SSH 5/5; HTTPS 7/7; host_camera_test 30/30 (-Os); host_ir_test 21/21; host_scsi_test 49/49.",
    },
    {
      name: "Wireless + bluetooth (802.11 x7 / SDIO / HCI-L2CAP-RFCOMM-SDP-SPP-HID)",
      tag: "WP-10-wireless_bluetooth",
      desc: "The 802.11 core and seven wireless families enter L0, plus a full Bluetooth stack, verified in QEMU: 802.11 core (frame-control/IE/beacon parsing, open-system auth+assoc state machine, LLC/SNAP data bridge); Intel iwlwifi (7000/8000/9000/AX200/AX201/AX210: CSR register block, ucode container parsers (legacy 'UCOD' five-section + TLV 0x0a4c4657), FH firmware load); Realtek rtl8188/8192/8821 (PCIe + USB vendor-request register access, 8188E register map, efuse, 8051 firmware download, TXDESC); Atheros ath9k/ath10k/ath11k (AR9002/9003 MAC map + TX/RX descriptors; CE descriptors/HTC/BMI/WMI-TLV; MHI ring elements/QMI TLV); Broadcom b43 (SSB BAR0 core windowing) and brcmfmac (PCIe + an SDIO client over a real SDHCI/SDIO bus); Bluetooth: HCI core, H4 UART transport (COM2 0x2f8/IRQ3, 16550 loopback probe + IRQ RX ring with a single FIFO consumer against the classic race), HCI-USB btusb class driver, L2CAP signaling (response-box dispatcher + idempotent connect), RFCOMM TS 07.10 mux (real CRC-8 FCS, PN/MSC), SDP client (a real (attr-id,value) pair walker), SPP and HID profiles. 2 new shell commands (wifi / bt), total 196->198 (additions only), L1 numbering stays closed at 1-138, shell wp tag field widened 24->32 bytes. Per the work-package test matrix the wireless families are datasheet-verified (QEMU has no 802.11 model: byte-exact vectors through the production code + live enumeration; vendor firmware not shipped, honest DOWN without it); Bluetooth is verified end-to-end on real emulated 16550 hardware (tools/bt_test_peer.py host-side H4 controller peer), and the batch's own E2E loop found and fixed 8+ real defects (HCI async-event completion + parameter offsets, double-framed ACL/L2CAP sends, the 16550 FIFO race, an L2CAP CFG_REQ overflow, the RFCOMM body offset + C/R gating, the SDP walker layout, non-zeroed fixtures, the wp field one byte short). Gates: 0 errors 0 warnings; wireless/bluetooth E2E 32/32 BIOS + 32/32 UEFI; full regression 28/28; dhtest 5/5; SSH 5/5; HTTPS 7/7; host_wifi_test 38/38; host_bt_test 22/22. 121,697 lines of source.",
    },
    {
      name: "Time sources: four domains landed (CMOS RTC / HPET / ACPI PM timer / TSC)",
      tag: "WP-10-timer_rtc",
      desc: "Four hardware time sources live-verified in QEMU on BIOS and UEFI boots: CMOS RTC (status registers A/B/C/D decoded: UIP, periodic rate, PIE/AIE/UIE, BCD/binary, 12/24-hour, VRT, FADT century; three REAL IRQ8 interrupt tests - update-ended, periodic, alarm - with unconditional Reg A/B restore); HPET (consumes the ACPI HPET table + MMIO driver, 64-bit main counter with re-read-high, GCAP capability decoding (QEMU 0xfed00000, 100 MHz), one-shot and periodic comparators on timer 1 via the LEG_RT legacy route to IRQ8 with HPET-counter-bounded deadlines); ACPI PM timer (FADT PM_TMR_BLK resolution - SeaBIOS 0x608 / OVMF 0xb008, 24-bit-masked wrap arithmetic + 64-bit wrap extension + PIT comparison calibration, pmtimer.width=32 config override); TSC (cpuid-serialized rdtsc, invariant-TSC probe, PIT/HPET/PMTimer three-reference calibration + best-reference autocalibration + TSC busy-wait delay). 4 new shell commands (rtc hpet pmtimer tsc), total 192 to 196 (additions only), L1 numbering stays closed at 1-138. Four live-boot defects root-caused and fixed (acpi_hpet_t lacked the SDT header and split the block ID into 6 bytes - its first real consumer ever; the PM-timer 24-bit wrap misread as a 3.6 s backward step; TSC-vs-HPET calibration spinning forever on a frozen counter; the HPET periodic deadline not scaling with the fire count). Gates: 0 errors 0 warnings; time-source E2E 29/29 BIOS + 29/29 UEFI; full regression 28/28; dhtest 5/5; SSH E2E 5/5; HTTPS E2E 7/7. RTC UIE 3/3, PIE 32/32, alarm fired; HPET counter measured 100,113,820 Hz (nominal 100,000,000), oneshot 1/1, periodic 3/3; PMT calibrated 3,576,824 Hz (delta 7 bp); TSC 2-run drift 86 bp, HPET cross-check 0 bp, delay 100/100 ms. 114,593 lines of source.",
    },
    {
      name: "Input devices + chipset: five domains landed (touchpad/PCIe/ACPI/SMBus/I2C)",
      tag: "WP-10-input_chipset",
      desc: "Five L0 domains live-verified in QEMU on BIOS and UEFI boots: I2C-HID touchpad full stack (descriptor parser + type-B multitouch decoder + TAP/two-finger scroll/pinch/three-finger swipe gestures); PCI/PCIe (CF8/CFC + MMCONFIG extended config space, device cache scan, capability walk, BAR sizing/assignment, MSI/MSI-X via the xAPIC delivery path); ACPI (RSDP from Multiboot2 tags 14/15, RSDT/XSDT/FADT/MADT/MCFG/HPET, minimal AML interpreter, S5 shutdown + S3 suspend-to-RAM through PM1a_CNT + the \\_S5/\\_S3 packages + the FACS waking vector, 32-bit resume trampoline, SCI(IRQ9)+GPE); SMBus (quick/byte/word/block with the [count,payload] layout); I2C (PIIX4 cfg0x90 + i801 class-0C05 cfg0x20 + DesignWare backend, block transfers auto-incrementing under AUX_CTL 0x0D bit1). 16 new shell commands (acpi acpidev acpitest acpipow msitest pcidump pcibars pcie i2cdetect i2cget i2cset i2ctest smbus smbustest touchpad touchtest), total 176 to 192 (additions only). Eleven live-boot defects root-caused and fixed (I2C bus-registry wipe, QUICK probes rejected by the core, word-write misroute, AUX_CTL offset/bit/timing, block-read count drop, LAPIC base mask keeping the BSP bit, LINT0 ExtINT masking killing the serial console, self-IPI shorthand 11 = all-excluding-self, HID Input flags read from the item header, fixture page switch, device paths missing the \\ root). Gates: 0 errors 0 warnings; chipset E2E 28/28 BIOS + 28/28 UEFI; full regression 28/28; dhtest 5/5; cryptotest 4/4; SSH E2E 5/5; HTTPS E2E 7/7. License: LICENSE restored to the verbatim official Apache-2.0 text (appendix copyright line only), NOTICE lists GRUB's unicode.pf2 font (GPL-3.0 + font exception), SPDX coverage complete, 112,589 lines of source.",
    },
    {
      name: "All 53 P2 batch-2 fixes verified in tree + three embed-chain defects fixed",
      tag: "WP-10-AUDIT_P2-fix2b",
      desc: "The full fix2 payload was re-verified against the actual tree: 53/53 P2 items present (8 deep spot-checks confirm real logic) + 4/4 findings (git-describe version live in the banner, HTTPS two-sided close_notify assertion PASS, INTERFACES.md 176, docs/verification/ made public); the embed chain - which fix2 claimed fixed but still failed with the userprog_libfoo block-not-found error - was repaired: (1) libfoo.c now builds through a dedicated solib leg (new tools/build_solib.py with -Wl,-soname,libfoo.so + DT_SONAME verification, regenerating l1/solib_data.h); (2) the six dynamic-link programs build as ET_DYN/PIE via the new pie/pie-foo modes in build_c_userprog.py with hard e_type/PT_INTERP gates against silent downgrade; (3) libs/ld_so.c joined USERPROG_SRCS (the Makefile ld_so case was dead code); main_dyn gained its blob and dual exec-table registration. Verified: make userprogs end-to-end EXIT=0; QEMU six dyn progs PASS; the 21-command regression green (dhtest 10/10 + cryptotest + fstest p2 ALL PASS); SSH E2E 5/5; HTTPS E2E + CLOSE_NOTIFY_ASSERT PASS + eof no-hang; mkfs confirm gate dual-sided; help self-report 176 commands; 101,150 lines.",
    },
    {
      name: "Full security audit + all 41 P0 + all 94 P1 + first 53 P2 fixes done",
      tag: "WP-10-AUDIT_P2-fix1",
      desc: "An 18-route line-by-line audit produced 607 findings (P0 41 / P1 94 / P2 214 / P3 258); all 41 P0s are fixed (p0fix1 first 20 + p0fix2 remaining 21, each with QEMU FAIL-before/PASS-after, host ASAN, or construction-level path evidence); p1fix1 fixed the first 31 P1s (BUG-0042..0072): per-task FPU/SSE fxsave context + CR4.OSFXSR + fork inheritance, PMM bitmap cli atomicity, page-fault semantics (P=1 refusal, 8MiB stack floor, U/S ring, kernel-on-user-address-space refusal), the XHCI event-ring LINK wrap and poll latch, EHCI CONFIGFLAG, the OHCI interrupt table / TD_R / NPS, MSC residue and sector_size, the CDC-ACM protocol, SS EP0 mps9, FAT32 rmdir dot entries + UAF + cluster-cycle bounds + the unlink guard, the exFAT bitmap lifecycle, ext4 extent offsets and crafted-volume bounds, the e1000 strcat, ld_so bounds, and the network stack (IP frame checks + RX checksums + sequence wrap + RST validation + SYN_RCVD reaping + SYN options + udp_bind dedup + window scaling + RTO critical sections); new sse_test/pf_test user tests added, with the tcptest/fork_test/ping/pmmrace regressions all green. p1fix2 fixed P1 items 32..62 (BUG-0073..0103): SSH TOFU known_hosts anchor plus per-installation host key (embedded universal RSA key removed, client identity via /etc/ssh_client_key, CSPRNG consumption verified at all call sites); TLS per-cipher key lengths, CertificateEntry ext skip, 32KiB transcript with hard overflow failure, ServerHello bounds, zero-length stall cap, KeyUpdate loop, X509 negative-length entry tests (ASAN-clean); shell capture stack, relative wildcards, edit/cp integrity, bounded ls/tree/du joins; ush unalias guard, O_TRUNC for nano/cp/mv, redirect fd lifecycle (kernel console fd dup/dup2 + sys_read(0) line mode with ^D EOF); L1 hook suppression + real registration + unregister; docs update_url / update_server.py; new fdref_test/select_zero_test. WP-10-AUDIT_P2-fix1 fixed the first 53 P2 items (BUG-0136..0188: memory / arch / 33 USB / 11 VFS-FAT fixes) and found-and-fixed a latent 40-byte heap block header defect (payload alignment drift shifted ATA-DMA transfers 8 bytes, breaking the /etc mount); fstest gained a P2 dual-sided assertion block; new irqabitest/heapbounds/vmkernelpt/int3_user tests.",
    },
    {
      name: "The complete shell (line editing + tools + editor)",
      tag: "WP-10-wp08fix1",
      desc: "Full-featured line editing on both oc> and ush: Up/Down history paging (32 entries, draft line preserved), Left/Right/Home/End cursor, Ctrl+A/E/U/K/W, Delete, Ctrl+C, Tab completion (command table + VFS paths). 15 missing ush tools: ln (real hard links sharing the inode + nlink), ln -s (path resolution follows symlinks, 8-hop loop guard), chmod/chown/sed/awk/ping/wget/netstat/ifconfig/ps/kill/top/du plus the stat/env commands that help had always promised. A nano-style editor on both shells (nano/vi), ^O save ^X exit; 9 new extension interfaces (items 130-138).",
    },
    {
      name: "User-angle usability (rule-9 audit)",
      tag: "rule-9 audit",
      desc: "Every feature audited item by item against \"passing tests ≠ usable by users\": five elements per feature (a command, a tool, docs, examples, feedback) and five abilities (create, use, roll back, inspect, understand). Delivered: the TRY-IT.md user guide, the UPDATE-HOWTO.md step-by-step OTA guide, one-command A/B disks via make_ab_disk.sh, mkfs/fsck fixes and play/volume unknown-device feedback that lists the available names; after the fixes, 18/18 regression plus the storage/NIC/sound/OTA re-runs all PASS.",
    },
    {
      name: "USB host stack (four controllers)",
      tag: "WP-10d",
      desc: "All four host controller backends (UHCI / OHCI / EHCI / XHCI) verified at device level: HID keyboard/mouse (interrupt IN), MSC storage (BOT+SCSI wired into blk with FAT32 round-trips), CDC-ACM/FTDI serial, UAC 1.0/2.0 audio; external hub cascade enumeration and real hot-plug (device_add inside a wait window); usb/usbdev status commands + 7 test commands, all usable from the oc> shell.",
    },
    {
      name: "Sound card drivers (six families)",
      tag: "WP-10c",
      desc: "Intel HDA (CORB/RIRB, codec/widget enumeration, BDL DMA, IOC interrupts), AC'97, Sound Blaster 16 (ISA DMA), ES1370, virtio-snd and USB Audio Class 1.0 over a new UHCI host stack: the snd_register/play/stop/set_rate/set_volume/get_caps extension interfaces, 44.1/48 kHz, real DMA and real interrupts; five cards verified live in QEMU.",
    },
    {
      name: "In-system update (A/B partitions)",
      tag: "WP-10u",
      desc: "Windows-Update-style updates: the kernel ships its own gzip/DEFLATE + ustar extraction with streaming SHA256 verification, A/B slot installation, boot-flag based automatic rollback and offline update --local; real_update_test verifies the full chain: download, install, reboot into slot B, rollback to A.",
    },
    {
      name: "NIC drivers (nine families)",
      tag: "WP-10b",
      desc: "e1000e / igb / ixgbe / RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx plus 3c59x, nForce, AR81xx and Yukon detection & drivers: the nic_register/send/recv/link_status/get_mac extension interfaces; e1000e, igb and RTL8139 verified live in QEMU with the DHCP/ping/HTTPS full chain.",
    },
    {
      name: "AHCI SATA",
      tag: "WP-10a",
      desc: "SATA disk/SSD driver: exact class 0x010601 enumeration, command lists/H2D FIS, 48-bit LBA DMA, FLUSH CACHE, multi-controller multi-port.",
    },
    {
      name: "NVMe SSD",
      tag: "WP-10a",
      desc: "NVMe driver: admin queue pair + two round-robin I/O queue pairs, Identify, Read/Write/Flush with doorbells.",
    },
    {
      name: "ATA Bus-Master DMA",
      tag: "WP-10a",
      desc: "Classic IDE over PCI BMDMA (BAR4 + PRDT) with true DMA transfers; the PIO path stays as fallback; the self-test prints a DMA-vs-PIO timing comparison.",
    },
    {
      name: "virtio-blk",
      tag: "WP-10a",
      desc: "QEMU virtio disk: capacity read fixed (was 0), virtqueue request/reply, full read/write/flush path.",
    },
    {
      name: "FAT32 on every device",
      tag: "WP-10a",
      desc: "FAT32 moved from direct ATA I/O to the blk layer: mkfs + mount + file round-trips on hda/sda/vda/nvme0; MBR + GPT partition parsing.",
    },
    {
      name: "TLS 1.3 / TLS 1.2",
      tag: "kernel/tls.c",
      desc: "TLS 1.3 (X25519 + AES-128-GCM, ChaCha20-Poly1305 suites) + TLS 1.2 ECDHE-GCM fallback; X.509 CA chain verification against embedded public roots — the handshake fails closed on verification errors.",
    },
    {
      name: "HTTPS",
      tag: "wget https://",
      desc: "wget https://host:port/path downloads over TLS, landing directly in a VFS file; verified against three real sites: GitHub Pages, google, cloudflare.",
    },
    {
      name: "SSH",
      tag: "ssh.c / sshd.c",
      desc: "Client + server: curve25519-sha256 preferred with group14-sha256 fallback, aes128-ctr preferred, hmac-sha2-256, host-key signature verification (TOFU + fingerprint), password + publickey auth; bidirectional interop with paramiko.",
    },
  ],
};

// Base kernel (WP-01..WP-08) one-liners, from README.md per-WP sections
export const BASE_KERNEL: Bi<{ k: string; v: string }[]> = {
  zh: [
    {
      k: "WP-01",
      v: "BIOS + UEFI 双引导（GRUB multiboot2）、64-bit long mode、800x600x32 帧缓冲、8x16 位图字体控制台",
    },
    {
      k: "WP-02",
      v: "256 项 IDT + GDT/TSS、PIC remap、PIT @ 100Hz、PS/2 键盘 + COM1、oc> 交互行编辑器",
    },
    {
      k: "WP-03",
      v: "bitmap PMM、4 级页表 VMM、真实页错误处理、free-list 内核堆（kmalloc/kfree/krealloc）",
    },
    {
      k: "WP-04",
      v: "抢占式调度（32 优先级 + 时间片）、spinlock/semaphore/mutex/condvar、Ring 3 + ELF 装载",
    },
    {
      k: "WP-05",
      v: "VFS + RamFS、env/alias/cwd、ls/cd/cat/mkdir/rm/cp/mv 等文件命令",
    },
    {
      k: "WP-06",
      v: "e1000 驱动、ARP/IPv4/ICMP/TCP/UDP、socket API、dhcp/ping/wget/netstat",
    },
    {
      k: "WP-07",
      v: "ATA/virtio-blk/NVMe、64 槽 LRU 回写块缓存、MBR 分区、FAT32(R/W) exFAT(R/W) ext4(RO)",
    },
    {
      k: "WP-08",
      v: "37 syscalls、ld.so 动态链接（5 种重定位 + dlopen/dlsym）、ush 用户 shell（20 内建工具）",
    },
  ],
  en: [
    {
      k: "WP-01",
      v: "BIOS + UEFI dual boot (GRUB multiboot2), 64-bit long mode, 800x600x32 framebuffer, 8x16 bitmap-font console",
    },
    {
      k: "WP-02",
      v: "256-entry IDT + GDT/TSS, PIC remap, PIT @ 100Hz, PS/2 keyboard + COM1, oc> interactive line editor",
    },
    {
      k: "WP-03",
      v: "bitmap PMM, 4-level page-table VMM, real page-fault handling, free-list kernel heap (kmalloc/kfree/krealloc)",
    },
    {
      k: "WP-04",
      v: "Preemptive scheduler (32 priorities + time slices), spinlock/semaphore/mutex/condvar, Ring 3 + ELF loading",
    },
    {
      k: "WP-05",
      v: "VFS + RamFS, env/alias/cwd, ls/cd/cat/mkdir/rm/cp/mv file commands",
    },
    {
      k: "WP-06",
      v: "e1000 driver, ARP/IPv4/ICMP/TCP/UDP, socket API, dhcp/ping/wget/netstat",
    },
    {
      k: "WP-07",
      v: "ATA/virtio-blk/NVMe, 64-slot LRU write-back block cache, MBR partitions, FAT32(R/W) exFAT(R/W) ext4(RO)",
    },
    {
      k: "WP-08",
      v: "37 syscalls, ld.so dynamic linking (5 relocation types + dlopen/dlsym), ush user shell (20 built-in tools)",
    },
  ],
};

// Work packages (README.md section titles)
export const WORK_PACKAGES: Bi<{ no: string; title: string }[]> = {
  zh: [
    { no: "WP-01", title: "引导 + framebuffer + 文本渲染" },
    { no: "WP-02", title: "中断 + 定时器 + 键盘" },
    { no: "WP-03", title: "物理内存 + 虚拟内存 + 内核堆" },
    { no: "WP-04", title: "调度器 + 同步原语 + 用户态" },
    { no: "WP-05", title: "Shell 增强 + 文件系统" },
    { no: "WP-06", title: "网络协议栈" },
    { no: "WP-07", title: "磁盘子系统" },
    {
      no: "WP-08",
      title: "完整 syscall + 动态链接 + 用户 shell + 工具 + 审计修复",
    },
    {
      no: "WP-09",
      title: "安全传输：SSH（客户端 + 服务端）、TLS 1.3 / TLS 1.2 / HTTPS、crypto 核心",
    },
    {
      no: "WP-10a",
      title: "存储驱动：AHCI / NVMe / ATA DMA / virtio-blk + blk_* 扩展接口",
    },
    {
      no: "WP-10b",
      title: "网卡驱动：九族主流有线网卡 + nic_* 扩展接口",
    },
    {
      no: "WP-10u",
      title: "系统内自动更新：A/B 分区 + tar.gz 更新包 + 回滚 + 离线更新",
    },
    {
      no: "WP-10c",
      title: "声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频 + snd_* 扩展接口",
    },
    {
      no: "WP-10d",
      title: "USB 主机栈：UHCI/OHCI/EHCI/XHCI 四控制器设备级枚举 + HID/MSC/串口/音频类驱动 + Hub/热插拔",
    },
    {
      no: "WP-10-wp08fix1",
      title: "Shell 补全：oc>/ush 全键位行编辑 + ush 15 个工具 + nano 风格编辑器 + 9 个 L1 接口",
    },
  ],
  en: [
    { no: "WP-01", title: "Boot + framebuffer + text rendering" },
    { no: "WP-02", title: "Interrupts + timer + keyboard" },
    { no: "WP-03", title: "Physical memory + virtual memory + kernel heap" },
    { no: "WP-04", title: "Scheduler + sync primitives + user mode" },
    { no: "WP-05", title: "Shell enhancements + file system" },
    { no: "WP-06", title: "Network protocol stack" },
    { no: "WP-07", title: "Disk subsystem" },
    {
      no: "WP-08",
      title: "Complete syscall + dynamic linking + user shell + tools + audit fixes",
    },
    {
      no: "WP-09",
      title: "Secure transport: SSH (client + server), TLS 1.3 / TLS 1.2 / HTTPS, crypto core",
    },
    {
      no: "WP-10a",
      title: "Storage drivers: AHCI / NVMe / ATA DMA / virtio-blk + blk_* extension API",
    },
    {
      no: "WP-10b",
      title: "NIC drivers: nine mainstream wired Ethernet families + the nic_* extension API",
    },
    {
      no: "WP-10u",
      title: "In-system update: A/B partitions + tar.gz packages + rollback + offline update",
    },
    {
      no: "WP-10c",
      title: "Sound card drivers: Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio + the snd_* extension API",
    },
    {
      no: "WP-10d",
      title: "USB host stack: device-level bring-up on UHCI/OHCI/EHCI/XHCI + HID/MSC/serial/audio class drivers + hub and hot-plug",
    },
    {
      no: "WP-10-wp08fix1",
      title: "Shell completion: full line editing on oc>/ush + 15 ush tools + the nano-style editor + 9 L1 interfaces",
    },
  ],
};

// Verification (README.md "Tests" + docs/EXTENSIONS_WP10a/b/u/c.md)
export const VERIFY: Bi<{ name: string; desc: string }[]> = {
  zh: [
    {
      name: "WP-08 补全端到端 24/24",
      desc: "tools/wp10_wp08fix1_test.py 用真实按键（串口直注 + sendkey 方向键）驱动：oc> 历史/光标/Tab/Ctrl+C/导航键、ush 同套键位、ln 硬链接+软链接、chmod/chown+stat、sed、awk、ps/top/du、ifconfig/netstat/ping、双端 nano 编辑器写文件回读，24/24 全 PASS。",
    },
    {
      name: "18/18 QEMU 全量回归",
      desc: "boot 横幅 + uname + 12 个用户程序 + p3_test + heaptest + l1test + crashlog，单次 QEMU 会话完成（挂全部四类盘）。",
    },
    {
      name: "WP-10d USB 测试 9 项",
      desc: "usb_core_test（主机注册/设备表一致/每设备 GET_STATUS/类驱动注册/重枚举幂等 5/5）、usb_kbd_test / usb_mouse_test（HID attach + 中断轮询）、usb_storage_test（容量/MBR/写读回校验/恢复）、usb_serial_test（CDC-ACM 环回）、usb_hotplug_test（等待窗口真插拔）、usb_hub_test 在 QEMU 四控制器实测；UHCI/OHCI/EHCI 全 PASS，XHCI 枚举/HID/核心测试 PASS（MSC CSW 缺口如实标注）。",
    },
    {
      name: "WP-10c 声卡测试 9 项",
      desc: "hda_test / ac97_test / sb16_test / es1370_test / usb_audio_test 在 QEMU 实测初始化、能力、DMA 播放与中断计数全 PASS；audio_rw_test 五卡逐个播放回读 PASS；sample_rate_test 44.1/48kHz 配置 PASS；virtio_snd_test 无 QEMU 设备模型如实 SKIPPED；real_hw_test 如实 NOT RUN。",
    },
    {
      name: "WP-10a 存储测试 8 项",
      desc: "ahci_test / nvme_test / ata_dma_test（含 DMA vs PIO 计时）/ virtio_blk_test / disk_rw_test / partition_test（MBR+GPT）/ fs_mount_test（四设备 FAT32 往返）/ real_hw_test 全 PASS。",
    },
    {
      name: "WP-10b 网卡测试 10 项",
      desc: "e1000e_test / igb_test / rtl8139_test 在 QEMU 实测注册、MAC、链路、TX/RX 与 dhcp/ping/wget HTTPS 全链路全 PASS；ixgbe_test / rtl8168_test / rtl8125_test / rtl810x_test / bcm57xx_test / other_nic_test / nic_rw_test 无 QEMU 设备模型时如实 SKIPPED（数据手册实现），绝不伪造输出。",
    },
    {
      name: "WP-10u 更新测试 11 项",
      desc: "update_pkg_test 12/12（gzip 三种块型/头选项/坏 CRC/截断/单字节流式恢复/ustar 解析与坏校验和）、ab_partition_test 7/7、update_check/download/verify/install/rollback/local/status 全 PASS；real_update_test 7/7 端到端：下载-校验-安装-slot B-真实重启进入 test1 内核-回滚回 A 全链路。",
    },
    {
      name: "dhtest 5/5",
      desc: "DH modexp 正确性：Oakley Group 1 + group14 真值向量、8..256 字节尺度扫描、确定性验证。",
    },
    {
      name: "HTTPS E2E",
      desc: "内核 TLS 客户端访问三个真实站点：cubestudio-dev.github.io（update.json）、google.com、cloudflare.com——TLS 1.3 握手 + CA 链验证 + 加密传输全链路。",
    },
  ],
  en: [
    {
      name: "WP-08 completion end-to-end 24/24",
      desc: "tools/wp10_wp08fix1_test.py drives real keystrokes (direct serial injection + sendkey for arrow keys): oc> history/cursor/Tab/Ctrl+C/navigation keys, the same key set on ush, ln hard+symlinks, chmod/chown+stat, sed, awk, ps/top/du, ifconfig/netstat/ping, and the nano editor writing files on both shells with read-back — 24/24 PASS.",
    },
    {
      name: "18/18 full QEMU regression",
      desc: "boot banner + uname + 12 user programs + p3_test + heaptest + l1test + crashlog, executed in a single QEMU session with all four disk types attached.",
    },
    {
      name: "9 WP-10d USB tests",
      desc: "usb_core_test (host registration / device-table consistency / live GET_STATUS per device / class-driver registry / idempotent re-enumeration, 5/5), usb_kbd_test / usb_mouse_test (HID attach + interrupt polling), usb_storage_test (capacity/MBR/write-read-back/restore), usb_serial_test (CDC-ACM loopback), usb_hotplug_test (real plug during a wait window), usb_hub_test — all exercised on four controllers in QEMU; UHCI/OHCI/EHCI fully PASS, XHCI enum/HID/core PASS (MSC CSW gap documented honestly).",
    },
    {
      name: "9 WP-10c sound tests",
      desc: "hda_test / ac97_test / sb16_test / es1370_test / usb_audio_test verified live in QEMU: init, capabilities, DMA playback and IRQ counters all PASS; audio_rw_test plays every registered card in turn; sample_rate_test programs 44.1/48 kHz; virtio_snd_test reports SKIPPED honestly (no QEMU device model); real_hw_test reports NOT RUN.",
    },
    {
      name: "8 WP-10a storage tests",
      desc: "ahci_test / nvme_test / ata_dma_test (with DMA-vs-PIO timing) / virtio_blk_test / disk_rw_test / partition_test (MBR+GPT) / fs_mount_test (FAT32 round-trip on all four drivers) / real_hw_test — all PASS.",
    },
    {
      name: "10 WP-10b NIC tests",
      desc: "e1000e_test / igb_test / rtl8139_test verified live in QEMU: registration, MAC, link, TX/RX and the dhcp/ping/wget HTTPS chain all PASS; ixgbe_test / rtl8168_test / rtl8125_test / rtl810x_test / bcm57xx_test / other_nic_test / nic_rw_test report SKIPPED honestly without a QEMU device model (datasheet-derived), never fabricating results.",
    },
    {
      name: "11 WP-10u update tests",
      desc: "update_pkg_test 12/12 (all three DEFLATE block types, header options, bad CRC, truncation, 1-byte-chunk streaming resume, ustar parsing and bad checksums), ab_partition_test 7/7, update_check/download/verify/install/rollback/local/status all PASS; real_update_test 7/7 end-to-end: download, verify, install, reboot into slot B (WP-10u-test1 kernel), rollback to A.",
    },
    {
      name: "dhtest 5/5",
      desc: "DH modexp correctness: Oakley Group 1 + group14 truth vectors, 8..256-byte scale sweep, determinism check.",
    },
    {
      name: "HTTPS E2E",
      desc: "Kernel TLS client against three real sites: cubestudio-dev.github.io (update.json), google.com, cloudflare.com — TLS 1.3 handshake + CA chain verification + encrypted transport, end to end.",
    },
  ],
};
