<!-- SPDX-License-Identifier: Apache-2.0 -->
# 引导流程，逐阶段详解

发布 ISO 经由 **OCBL**——树内引导加载器——引导：GRUB 已在
3.0.0-WP05 B5-7 离开发布链（它仅作为遗留的 `grub.cfg`/
`grub-ab.cfg` 磁盘契约，以及系统内 `install`/`abdisk`/`grub-install`
命令的内嵌引导数据载荷存活）。本页走读整条链：stage1 -> OCBL
core -> 扫描器 -> 菜单 -> multiboot2 交接 -> 内核入口 -> kmain。
组件全景见 [system-map.md](system-map.md)；状态监视器的角色见
[hidden-mechanisms.md](hidden-mechanisms.md)。

## 第 1 阶段——512 字节的扇区（`ocbl/bios/stage1.asm`）

一份源码，两种形态：`-D OCBL_ELTORITO`（ISO；整个镜像被预加载，
core 已在 0x7E00，打印 `OCBL stage1 ok`）与 `-D OCBL_MBR`（已安装
磁盘；有意保持静默——512 字节预算沿袭 GRUB boot.img / ISOLINUX
stage1 模式）。

- 入口位于 0x7C00；经 `call .ip / pop ax` 自地址检查同时接受
  `0000:0x7C00` 与 `0x07C0:0x0000` 两种入口形态；其余形态一律带
  串口信息停机。
- 为 64 位 core 把 `{boot drive, flavor, entry CS}` 写入线性地址
  0x520..0x523。
- **MBR 形态**：用 int 13h AH=42h 把 core 原始读入 0x7E00，依据
  OCBL-install 写在镜像偏移 `+0x1A0` 处的补丁块：`u32 magic 'OCBL'`、`u32 core_lba_lo`、`u32 core_lba_hi`、`u16 core_sectors`。
  重试循环（12 次尝试，ISA pause 端口 0x1ED）**从不复位驱动器**
  ——复位会清除 SeaBIOS 的 PIO 回退状态并导致死循环（实测发现的
  WP06b b5）。
- A20：先 int 15h AX=2401，再端口 0x92（bit 0 = 快速复位被屏蔽）；
  验证是对 0xFFFF:0x0510 别名做两次独立的写入模式（orig^0x55、
  orig^0xAA）——只有两次别名测试都失败才判定 OFF（对单次巧合的
  不信任，B1 修复）。键盘控制器方法有意缺席（已声明）。
- GDT 位于 0x7C00（0x08 code32、0x10 data32），`cr0 |= 1`，
  `jmp 0x08:0x7E00`。
- 镜像中第 446..509 字节为零——`ocbl-install` 把真实分区表拷贝到
  其上；510..511 = 0xAA55。

## 第 2 阶段——OCBL core，BIOS 形态（`ocbl/bios/core_head.S`）

32 位入口 `core_head` 位于 0x8200（镜像经 `ocbl/linker_bios.ld`
链接于 0x7E00，`.text.head` 在前）：平坦段、自有 GDT（32+64 位
代码）、清零 .bss、恒等页表（PML4@0x8F000 -> PDPT@0x90000 ->
PD@0x91000..0x94000，2 MiB 大页 = 4 GiB）、PAE+LME+PG、远跳转到
64 位、栈顶 0x8E000（WP06f VMware 修复——旧的 0x9F000 与 VMware
更大的 EBDA 相撞）。**EBDA 守卫**（#134）：读 BDA 0x40E；一旦
重叠就把 6 页 PT 块重定位到 0x40000。

一切固件服务都经实模式跳板运行（`tramp16.asm` blob 拷贝到
0x40000；`ocbl_bios_int(int_no, regs*)` 是 64 位包装——reg 块位于
0x40300，远跳转到选择子 0x08 -> 0x40000，经保存的 RIP/RSP 恢复）。
建于其上的后端：int 10h VGA 文本控制台、int 1Ah 时钟（54.9 毫秒
节拍）、int 16h PS/2 + int 14h 串口按键、int 13h 磁盘（AH=48h
几何参数、AH=42h 扩展读、驱动器 0x80..0x8F + 引导 CD 0xE0+）、
int 15h E820（以 multiboot2 形态存于 0x60000）、VBE（模式枚举 +
LFB 设置；ModeInfoBlock 偏移经 SeaBIOS 实测验证）、RSDP 扫描
（先 EBDA 再 0xE0000-0xFFFFF，20/36 字节校验和），以及 VBR 链式
加载。

## 第 2' 阶段——OCBL core，UEFI 形态（`ocbl/efi/`）

构建为中间 ELF（`ocbl/linker_efi.ld`，`.` = 0x11000 = PE
ImageBase 0x10000 + RVA 0x1000——“每个链接期绝对值都等于其最终
虚拟地址”），再由树内 `ocbl/elf_to_efi.py` 转换，后者从
`ld --emit-relocs` 导出**完整的** `.reloc` 目录（直接用
`ld -m i386pep` 只会导出部分目录 -> OVMF 错误重定位 -> 匹配到错误
的协议 GUID -> #UD；已记录的实测追查）。

`efi_main`（MS x64 ABI）：手工清零 .bss（EDK2 不保证 raw 与
virtual 之间的余量——实测捕获 `g_ndisks` = 29 亿）；用
`EFI_ALLOCATE_ADDRESS` 保留固定窗口（0x30000/0x10000、
0x50000/0x10000、0x60000/0x23000——碰撞即 FATAL 停机，因为该布局
会破坏固件数据）；statemon 窗口保留（失败只是禁用监视器 +
`oc.stmon=off`）；BlockIo 磁盘初始化（GUID 964e5b**21**——不是
SimpleFileSystem 的 ...22，一位数字之差的陷阱，实测捕获）；随后是
与 BIOS 相同的扫描/菜单/交接，只是以 GOP 取代 VBE、以
GetMemoryMap -> E820 取代 int 15h，并在 0x30000 的 64->32 交接
blob 之前有 ExitBootServices 重试循环（3 次尝试）。EBS 之前先
解除 OVMF 默认 5 分钟看门狗（`set_watchdog_timer(0,...)`）。

## 第 3 阶段——六类扫描器（`ocbl/common/ocbl_scan.c`）

`ocbl_scan_all()` 枚举驱动器（BIOS int-13 范围 / UEFI BlockIo
句柄），把类 CD 设备限定为 2048 字节扇区，并对每个驱动器先做
分区扫描、再做整盘“superfloppy”回退（当 0xEE 表项本身就是卷时，
OCIRE 的 16 MiB 0xEE 影子卷即按此方式解码）。`judge_volume()`
短路优先级：**OCSE -> OCOS -> OCIRE -> WINDOWS -> LINUX ->
GENERIC**：

| 类别 | 判定方式 |
|---|---|
| OCSE | 携带 `/ocse.mrk` 的 ISO9660 卷（8.3 安全的标记；最先判定——安装介质即使上面有内核也绝不作为 OCOS 引导）。路径 `/boot/ocse.elf` |
| OCOS | 任何带有 `/boot/opencube.elf` 且其前 8 KiB 含 mb2 魔数 `0xE85250D6` 的卷。无标记的 ISO 也会注册 |
| OCIRE | 任何带有 `/boot/ocire.elf` 的卷（含 0xEE 影子与 ISO） |
| WINDOWS | 存在 `/EFI/Microsoft/Boot/bootmgfw.efi` |
| LINUX | 带有 `/EFI/<vendor>/{grubx64,shimx64,systemd-bootx64}.efi` 的 ESP（vendor 目录名即标签名；跳过 `BOOT` -> 落入 GENERIC） |
| GENERIC | 存在 `/EFI/BOOT/BOOTX64.EFI` |
| SHUTDOWN | 合成条目，永远最后，绝不会被滚走 |

已声明的限制：没有 ext2/3/4 驱动——Linux/Windows 经其 ESP 探测，
绝不经 ext/NTFS 根。同时携带 `opencube.elf` 与 `ocire.elf` 的卷
也会暴露 OCIRE 恢复条目（WP06g3-4.4——短路逻辑过去会把它藏起来，
“验尸灾难 4”）。扫描器背后的只读卷层（`ocbl/common/ocbl_fs.c`）
是带 LFN 读取的 FAT12/16/32 + ISO9660，共享位于 0x62000 的 2 KiB
扇区窗口与位于 0x62800 的 128 KiB FAT 缓存。分区遍历
（`ocbl_part.c`）认识 MBR、GPT 与 **SHADOW**（带单个 type-0xEE
表项却无 GPT 头的 MBR——OCIRE 恢复卷的形态；0xEE 不盲目等于
“去读 GPT”）。

### A/B 判定链（原生，位于 `judge_volume()` 内）

当磁盘的第一个 OCOS 卷携带 `/boot/next_B`、`/boot/bootfail_B`、
`/boot/ok_B` 中的任何一个时（分区 1 = 标志卷）：

- `want = (next_b && !bootfail) || (!next_b && ok_b) ? 3 : 2`——
  **分区 3 = 槽位 B，分区 2 = 槽位 A**；
- `next_B && bootfail_B` -> 槽位 A，标签后缀 `" rollback"`
  （**自动回滚**：槽位 B 内核在引导时悲观地写入 `bootfail_B`，
  只有完全确认的引导才会移除它）；
- 所选槽位卷被重新挂载，条目的 drive/base_lba/fs 重新指向它
  （`e->slot` = 1=A / 2=B / 0=普通）；
- 该磁盘的分区 2/3 已被“槽位占用”——不能再注册自己的条目。

相同的标志语义存在于 `grub.cfg`（ISO 介质）、`grub-ab.cfg`
（已安装磁盘）与 OCBL 原生实现中——三份实现，一个契约。
[ota-ab.md](ota-ab.md) 在 OCOS 一侧续写这个故事。

## 第 4 阶段——3 键菜单（`ocbl/common/ocbl_menu.c`）

80x25 文本菜单：第 0 行标题 / 第 1 行提示 / 第 2 行倒计时 /
第 4..22 行是可滚动的 19 行窗口。**恰好三个键是活的：Up（0x48）、
Down（0x50）、Enter（0x1C）**——其余按键一律吞掉并忽略
（`ocbl_menu.h` 中的契约；没有隐藏键）。倒计时为
`OCBL_MENU_TIMEOUT_SEC = 10`；Up/Down 取消它（“(timer
cancelled)”）。关机行最后合成，绝不会被滚走。菜单可完全经
**串口**驱动（VGA 菜单的完整 ANSI 镜像——反显、光标寻址；CSI
方向键 `ESC [ A/B`、`\r`/`\n` 作 Enter），无头自动化正是这样选择
条目（`ocbl/menu_drive.py` 以串口 `MENU-READY` 轮询驱动 QEMU
monitor sendkey——PS/2 是唯一的注入路径；经 tcp socket 的 CSI
字节到得太晚）。菜单的每次心跳都踢一下 6300ESB 看门狗。

在菜单运行之前，`ocbl_run()` 已经：打印横幅（`OCBL_RELEASE` +
`OCBL_BUILD`、CPUID brand、栈），运行 B2a 探测（跳板 + E820 转储
+ 驱动器扫掠 + LBA 16 的 CD PVD / LBA 34 的自身引导镜像），运行
**statemon 引导检查**（可能判定上一次引导失败并复位——而不是
返回），扫描并转储全部卷，探测/布防看门狗 60 秒，并咨询
`ocbl_statemon_route_pending()`——一条待处理的 BOOT_FAILED 记录
会使 OCIRE 条目被一次性自动选中。

## 第 5 阶段——加载与 multiboot2 交接（`ocbl/common/ocbl_load.c`）

`ocbl_load_elf(v, path, want_base, &size)` 经 0x30000 弹跳窗口读取
ELF64 头 + 最多 32 个程序头；每个 PT_LOAD 都拷贝到其 `p_paddr`
并零填充至 `p_memsz`；`[want_base, 0x40000000)` 之外的段被拒绝；
BUG #46 修复使 `p_offset`/`p_filesz`/`mem_end` 边界免于溢出（旧的
未检查形式允许接近 2^64 的值装载攻击者字节或把整个窗口清零）。
OCOS 与 OCSE 装载于 0x100000；OCIRE 装载于 0x1000000。
`ocbl_load_raw()` 整文件拷贝 "self" 模块——放置在 0x800000 的
原始内核 ELF（安装器载荷模式：运行中的系统可以从模块内存把自己
安装到其他磁盘）。

`ocbl_build_mbi()` 在 **0x50000** 写下 GRUB 形态的 multiboot2
信息：(1) 命令行 `oc.slot=A|B|ISO` 外加 statemon 附加项（例如来自
`/HANGAT` 探测的 ` oc.hang=N`——见
[hidden-mechanisms.md](hidden-mechanisms.md)）；(2) 引导加载器名
"OCBL 3.0.0"；(3) 给 OCOS 的模块 "self" [start,end]；(4) meminfo；
(6) 24 字节表项的 mmap——**状态监视器的 [0x98000,0x9C000) 页被划
为 E820 保留**（按下方/重叠/上方分裂发出）；(8) 40 字节 RGB 帧缓冲
标签；(15/14) RSDP 副本（36 字节 rev>=2 / 20 字节 rev1）；(0) 结束。

交接契约（与 GRUB 完全一致，因此各内核的 `boot.S` 无从分辨）：
32 位保护模式、分页关闭、GDT 可以是我们自己的、`EAX=0x36D76289`、
`EBX=MBI 物理地址`、跳转到内核的 32 位入口。BIOS 形态：原地
64->32 降级。EFI 形态：ExitBootServices，然后是 0x30000 处的交接
blob。

## 第 6 阶段——OCOS 入口（`ocos/kernel/`）

`core_boot_entry.S`（`_start`）：加载自己的 GDT，构建引导页表
（PML4 + 4 个 PDPT 表项 + 4 张 2 MiB 大页的 PD 表 = 4 GiB 恒等
映射），启用 PAE+LME+NXE+PG+WP，远跳转到 `long_mode_start`
（`core_boot_longmode.S`），后者重载 64 位数据选择子，设置
`rsp = __boot_stack_top`（.bss 中 16 KiB 引导栈）并调用
`kmain(magic, mbi)`——mb2 魔数自交接起保存在 `edi`，mbi 指针保存
在 `esi`。multiboot2 头（`core_boot_mb2header.S`，
`.rodata.mbthdr` 位于镜像最前）请求 **800x600x32 RGB 帧缓冲**——
没有 fb 标签 OCOS 即硬失败（这正是 OCBL 在交接前设置 VBE/GOP
模式、OCIRE 的 kexec 逐字递交该标签的原因）。

`arch_multiboot2.c` 解析标签列表（cmdline、引导加载器名、模块
——含 "self" 模块查找、meminfo、mmap、帧缓冲、RSDP 标签 14/15），
对恶意输入加固：total_size 边界、每标签最小长度、字符串 NUL 终止、
截断标签拒绝（BUG-0142 链）。

## 第 7 阶段——kmain，组合根（`core_boot_main.c`）

按引导日志顺序，附打印标记：

1. COM1 串口 115200 8N1 + 串口编码器分岔。
2. mb2 魔数校验 + mbi 解析（失败 -> 串口 `FATAL:` + hlt）。
3. `boot_state_early_init(cmdline)`——状态监视器接管 6300ESB
   看门狗，写入阶段 1 `KM_AIN_ENTERED`。
4. `screen_fb_init`（仅 RGB 32bpp；来自标签的掩码同时处理 BGRX
   BIOS-VBE 与 RGBX UEFI-GOP）-> 控制台 -> CJK 渲染器 ->
   `lib_log_init` -> `klog_init`。"boot chain 1/4: kernel core
   mechanisms"。
5. IDT/GDT/TSS（256 个门）、8259 重映射、PIT 100 Hz、PS/2 键盘、
   COM1 RX -> 按键队列、行编辑器、`sti`、异常自测（#DE/#UD/#PF
   被捕获，内核存活）、PMM/VMM/堆、`klog_grow()`（24 条目的引导
   环变为 1024 条目的堆环——不丢失任何东西）。
6. 调度器 + 同步 + 用户态；**注册表收集**（`drv/svc/core_registry`
   ——记录 `registry: drv=N svc=N app=N`）；ACPI/AML/PCIe/看门狗
   探测/i2c/RTC/HPET。"boot chain 2/4: drivers + driver-free
   services"，阶段 2 `SCHED_READY`。
7. VFS + ramfs（+ klog 转储到 /var/log/kernel.log）、块缓存 + 探测、
   SCSI、USB 探测 + 常驻轮询线程；FAT32/exFAT/ext4 注册表、配置、
   PM 定时器、A/B 槽位、无线/SDIO/蓝牙、摄像头/遗留端口、LAPIC、
   网络栈 + 10 毫秒定时器、health 采样器、音频探测。"boot chain
   3/4: driver-dependent services"，阶段 3 `DRIVERS_READY`。
8. 崩溃基础设施布防（`core_dump_alloc` 一次性预先分配、panic
   处理器、crashlog、软看门狗——见
   [crash-recovery.md](crash-recovery.md)）；可选的异步更新检查；
   `ota_update_confirm_boot()`（清除槽位 B 的 bootfail 标记——A/B
   链的另一端）；监督引导时的 `ocembed_boot_confirm()`（邮箱
   SIGNAL_OK、解除 i6300esb、整盘写保护、拉起 ocire_guard）。
   "boot chain 4/4: interfaces ready"，阶段 4 `SERVICES_READY`。
9. "L0 ready, waiting for L1" -> `l1_launcher_boot_default()` 拉起
   ring 3 **ush** shell（`core_boot_l1launch.c` 中默认的
   `OC_L1_APP_DECL` 条目），标记阶段 5 `L1_LAUNCHED`，并把引导
   任务停在 yield 循环中；kmain 以 `for(;;) hlt` 结束。来自 ush
   的第一个系统调用把阶段翻到 6 `L1_READY`。

扫描器测试舰队（`ocbl/make_scan_disks.py`）锁死这条链：hd0（MBR
三分区 A/B FAT32 OCOS 标志卷 -> 槽位 B 条目）、hd1（0xEE 影子
FAT12 OCIRE）、hd2（Windows ESP LFN）、hd3（ubuntu ESP）、hd4
（通用 ESP）、fat0（superfloppy FAT16）、bare0（垃圾 -> 0 条目）、
cd0.iso（按内容）。链式加载舰队追加 chain0（BOOTX64.EFI 即 OCBL
本身的 ESP——自链式加载证明）与 chain1（必须以 EFI_LOAD_ERROR
拒绝的非 PE 桩）。

同一故事面向用户的一侧见
[../manual/boot-modes.md](../manual/boot-modes.md)；OCSE 自己的
24 级梯子与 OCIRE 的监督者决策循环见 [system-map.md](system-map.md)
与 [../dev/ocire.md](../dev/ocire.md)。
