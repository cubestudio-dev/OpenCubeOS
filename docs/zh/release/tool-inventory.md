<!-- SPDX-License-Identifier: Apache-2.0 -->
# tools/ 清单——全部 117 个文件（维护者手册）

`tools/`（117 个文件：95 个 `.py` + 16 个 `.sh` + 6 个数据/C
文件）的完整分类。每个文件一行职责，取自文件头。各节合计恰为
117。带守护机制者的深入剖析：[guard-scripts](guard-scripts.md)；
调用它们的构建目标：[build-reference](build-reference.md)。

| 板块 | 数量 |
|---------|-------|
| 1. 构建流水线（shell + tar 打包器） | 17 |
| 2. 源与数据生成器 | 12 |
| 3. 字体与 i18n 生成器 | 7 |
| 4. 文档与证据生成器 | 3 |
| 5. 架构与确定性守护 | 6 |
| 6. E2E / 回归套件 | 46 |
| 7. 敌意安全对端（p0/p0fix3） | 9 |
| 8. 测试对端服务器 | 4 |
| 9. 历史一次性重构 | 7 |
| 10. 数据与 C 文件 | 6 |

## 1. 构建流水线（17）

| 文件 | 职责 |
|------|----------------|
| `build.sh` | POSIX 一条命令入口：依赖检测（gcc/ld/nasm/grub-mkimage/xorriso/mformat/mcopy/python3/zip，各自带其 Debian 包）、`--install` apt 回退、`OC_TOOLS` 自动发现、目标分派，以及 WP-13 规范 8 可脚本化 API（`build_all/build_iso/build_src/build_ota/config_load/config_save/config_get/cache_lookup/cross_compile/cross_toolchain_setup`） |
| `build_iso.sh` | OCOS 发布 ISO：把双固件 OCBL 对（BIOS stage1+core + UEFI PE）+ 内核 + ocire.elf 经 OCBL ISO 打包器上架；G13 确定性封印委托给打包器 → `build/opencube.iso` |
| `build_ocire_iso.sh` | 独立 OCIRE 恢复 ISO（OCBL 启动它；扫描器类别 = 携带 `/boot/ocire.elf` 的卷）→ `build/ocire/ocire.iso` |
| `build_ocse_iso.sh` | OCSE 安装介质 ISO：先对 ELF 运行 `check_bss.py`，上架 `/ocse.mrk` 标记 + `/boot/ocse.elf` → `build/ocse.iso` |
| `build_release.sh` | 一条命令的发布 SOP 第 1–5 步，带门禁：干净 `make all`（0 警告）→ `reproducible-check` → BIOS+UEFI 启动横幅门禁（每次运行全新 mktemp 日志，BUG #63 修复）→ 可选 `--deep` 门禁 → 文档/网站数据；打印三个 SHA256；拒绝已存在的 tag 或错误的 git 身份；不推送任何东西 |
| `make_update_pkg.sh` | OTA 包：`manifest.json`（版本 + 文件 + 按归档顺序拼接的载荷体 sha256）+ 内核 + etc + 文档；自 WP05 起无 boot-cfg 成员（OCBL 动态扫描） |
| `make_ocos_tar.py` | 确定性 OCOS.TAR 安装载荷（boot/opencube.elf + boot/ocire.elf + etc/opencube.conf；ustar、零 mtime、已排序） |
| `make_ab_disk.sh` | A/B 更新盘 `build/abdisk.img`：p1 boot/flags 64M FAT32 + slot A 128M + slot B 128M + data 128M；仅 mtools（无 loop、无 root）；预置 slot A |
| `make_ocire_disk.sh` | WP-02 双启动测试盘：hd0 GRUB 双菜单（OCOS 卷 + 恢复条目），hd1 OCIRE 盘（0xEE 影子、无 GPT 头、FAT12：黄金文件 + MANIFEST.TXT SHA-256 + agent + 64 KiB 日志环） |
| `install-toolchain.sh` | 免 root 工具链：分 3 批下载 Debian .deb（构建工具 / qemu / grub+固件；含依赖约 117 个包）并 `dpkg -x` 解到 `$OC_OPT`（默认 `$HOME/opt/extract`）：nasm 2.16.03、qemu-system-x86 10.0.13（+数据+库）、xorriso 1.5.6、grub-pc-bin/efi-amd64-bin/common、mtools、ovmf、seabios |
| `env-sandbox.sh` | 面向 extract 布局的 3 行环境：导出 `OC_TOOLS=$HOME/opt/extract`、PATH、LD_LIBRARY_PATH |
| `oc_cc.sh` | 内容寻址编译缓存封装：键 = SHA-256（编译器身份 + 参数 + 源哈希 + 来自 `-MM` 的头文件内容哈希）；命中按字节一致还原 `.o`+`.d`（reproducible-check 保持有效）；`OC_CACHE_OFF` 绕过、`OC_CACHE_DIR` 迁移、计数器追加到 `STATS` |
| `oc_menuconfig.sh` | 无依赖的交互式 `.config` 编辑器（数字=切换，s=保存，q=退出，r=重置） |
| `vendor_ocse.sh` | 从 tag `v1.0.0` 重新 vendor OCSE（143 个文件：kernel core/lib/mem/arch、drivers acpi/block/display/i2c/input/pci/time/usb、fs、boot、l1、chinese） |
| `github_release_wp08.sh` | WP-08 时代 GitHub Release 助手：推送 commit+tag、经 API（`GITHUB_TOKEN`）创建带 ISO 资产的 Release |
| `update_website.sh` | 读 dist 资产 → 写 `website/assets/site-data.js` + `website/update.json` + `update-v2.json`（OTA 清单）；每次发布幂等，无任何手工编辑 |
| `sync_wiki.sh` | docs/（EN）+ docs/zh/ → GitHub Wiki：zh 规范页名、EN 作为 `<Page>-EN.md`、语言横幅、按语言改写 `[[Wiki-Link]]`、剥离资源；约 83 项的 `map_page()` 表就是契约 |

## 2. 源与数据生成器（12）

| 文件 | 职责 |
|------|----------------|
| `gen_userprogs.py` | 全量再生 `userprogs_data.h`：目录应用 + 遗留平铺程序 + 跳过 `_shared/` + `libfoo.c`→solib；产出字节数组 + `OC_USERPROGS[]` 注册表（execve 面）；清除孤儿 blob；重名致命（见 [guard-scripts](guard-scripts.md) §4） |
| `build_c_userprog.py` | 编译一个 C 用户程序并刷新其 blob；模式：`exec`（user.ld @0x400000）、`pie`（ET_DYN + PT_INTERP=/lib/ld.so）、`pie-foo`（+DT_NEEDED libfoo.so）、`ld_so`（ld_so.ld @0x10000000）；按模式校验 e_type/PT_INTERP |
| `build_solib.py` | solib 一条腿：ET_DYN `-Wl,-soname,<name>.so`，刷新 `l1/solib_data.h`（`solib_libfoo`） |
| `embed_userprog.py` | 编译一个 `.asm` 用户程序（NASM）并把其字节追加到生成的头文件 |
| `embed_grub.py` | stage-2 生成器：GRUB boot.img（512 B 副本）+ core.img（grub-mkimage，带盘上 cfg 所需模块 + 内嵌早期配置）→ `grub_boot_data.c`；内核 ELF 不被内嵌（经 grub.cfg 作 multiboot2 模块）——内核内安装器的载荷 |
| `embed_roots.py` | 把公共 CA 根（ISRG X1/X2、GlobalSign、GTS R1/R4、DigiCert、Baltimore、Amazon、Microsoft、Sectigo E46/R46）作为 DER 数组内嵌进 `crypto_roots.h` |
| `gen_l1svc_ops.py` | 把 `svc_sysapi_l1svc.h` 中的内核 `L1SVC_*` 编号逐字镜像到 `ocos/interface/l1svc_ops.h`（只追加重编号契约） |
| `gen_svc_api_headers.py` | 每个服务发一顶 `svc_<name>_api.h` 伞头文件；幂等，手工迁移的伞头逐字保留 |
| `gen_gzip_vectors.py` | 供内核更新器自测的 gzip/deflate 测试向量（`update_test_vectors.h`） |
| `make_wdat_table.py` | 合成但有效的 ACPI WDAT 表，供 QEMU `-acpitable` 注入，驱动真实的 q35 TCO 寄存器（WDAT 发现的 WP-12 E2E 门禁） |
| `host_keygen_etc.py` | 在宿主机上生成 RSA-2048 主机/客户端密钥写入 `etc.img`（n‖d、各 256 B、e=65537），让客户机跳过数分钟的 TCG 密钥生成 |
| `ocse_edits.py` | 每个 OCSE vendored 源编辑的唯一事实来源（E1 OTA API 剥离、E2 GRUB 封装移出、E3 网络栈不 vendor、E4 崩溃管道打桩、E5 ring-3 kill 移除）；绝不手工编辑 vendored 文件 |

## 3. 字体与 i18n 生成器（7）

| 文件 | 职责 |
|------|----------------|
| `gen_font.py` | 8×16 ASCII 0x00–0xFF 位图字体（16 行 × 8 位，位 7 = 最左）→ `screen_font_data.c`（`screen_font8x16[256][16]`） |
| `gen_chinese_font.py` | 来自 GNU Unifont 轮廓的 16×16 GB2312 字体（矩形并集栅格化、upem=64、无抗锯齿）：6763 个汉字（区 16..87）+ 682 个符号 + 区索引 + unicode→GB 映射 → `ocse/chinese/chinese_font_data.c` |
| `gen_font_gz.py` | 把六张中文字体表包成一个 gzip 块——恒等窗口减压：内核必须结束于 0x400000 之下（用户地址空间让 0x400000–0x600000 留空）；约 298 KiB 的裸 `.rodata` 变成惰性解压流 |
| `gen_l0_i18n.py` | `etc/i18n/l0/{en,zh}.json`（各 49 键）→ `core_lib_i18n_data.c` 中按键排序的查找表，供 `l0_tr_()` 二分查找 |
| `gen_pinyin_phrase.py` | `tools/ime_phrases.txt` → 拼音排序的词组表（最长匹配优先）；无声调拼音由内核单字表合成——不用 pypinyin（CI 干净环境修复） |
| `apply_i18n_hints.py` | 把 `_hint`（en, zh）对写进每个程序的 `i18n/{en,zh}.json`——覆盖全部 274 个程序的手写词典 |
| `apply_tool_i18n.py` | 让文件工具应用的用户可见句子过 `tr_()`（gettext 模型：键 = 英文句子；ZH 键带真实换行——v1 的字面 `\n` 键从未匹配上） |

## 4. 文档与证据生成器（3）

| 文件 | 职责 |
|------|----------------|
| `gen_api_docs.py` | 文档数据生成器：解析运行时 `help -a` 抓取（注册表头 + `name [tag] hint` 行；容忍串口回显重叠）或 `--qemu` 实时抓取 → `docs/data/{commands,interfaces,stats}.json`；`--pages`/`--pages-zh`/`--md` 再生 292 个命令参考页（EN+ZH）；v1 基线回退 `evidence/v1_command_list.txt` |
| `qemu_shot.py` | 运行 QEMU → 等待 → screendump → 杀掉（PPM 输出） |
| `qemu_shot_vnc.py` | 经 PIL 写 PNG 的 VNC 变体——`make shot-bios` / `make shot-uefi` 背后的引擎 |

## 5. 架构与确定性守护（6）

每个的深入剖析见 [guard-scripts](guard-scripts.md)。

| 文件 | 职责 |
|------|----------------|
| `check_deps.py` | `ocos/{kernel,drivers,services,l1apps}` 上的依赖方向门禁：内核不得 include 驱动/服务（组合根除外）、驱动不得 include 服务、l1apps 只用 `ocos/interface/` 头；按真实文件清单做纯基名解析；任一违规即 exit 1 带 file:line |
| `check_bss.py` | 任何常规 `.bss` 变量不得落入 `[__boot_pml4, __ocse_work_start)`（页表 + 引导栈）；一个落在活动 PML4 上的 64 KiB 缓冲曾让启动三重故障；接入 `build_ocse_iso.sh` |
| `v2_registry_gate.py` | v2 B 组冻结：启动按声明收集的内核，验证命令面（231 行 `help -a` / 总计 233 / 收集日志 / 服务组视图） |
| `fat_zero_dates.py` | 把 mtools 镜像中每个 FAT12/16 日期/时间字段清零（mformat/mcopy 盖墙上时钟），使相隔几分钟的两次构建保持字节一致 |
| `iso_zero_ctime.py` | 把每个 ISO9660 目录记录中的 Rock Ridge TF ctime/atime 戳改写为 1970 纪元（xorriso 把 ctime 硬编码为组装时间；atime 泄漏在 WP04-fix1 修复） |
| `fix_file_headers.py` | 改写指向重构前路径的过期 `* File:` 头注释（仅注释） |

## 6. E2E / 回归套件（46）

| 文件 | 职责 |
|------|----------------|
| `qemu_runner.py` | 共享 QEMU+pexpect 驱动：启动 ISO、运行 shell 命令（`@` 前缀 = 经 `mon:stdio` 的 HMP monitor）、`strip_ansi()`、`boot_to_recovery()`、`screen_assert_text()`（BUG #66：PPM screendump 必须显示 ≥2 色和可测的前景比例） |
| `run_host_tests.py` | `make test` 后端：从每个 `tests/host_*.c` 头部提取 gcc 行、重映射重构前路径、编译 + 运行；openssl 夹具缺失时 SKIP-FIXTURE |
| `run_fix4_regression.py` | 31 项检查全量回归门禁（下限机制见 [ci-gates](ci-gates.md) §6） |
| `run_fix4_https_e2e.py` | 8 项检查 HTTPS E2E 门禁（TLS 1.2 + 0x0067 密码套件、close_notify 记录器、截断防护） |
| `ssh_g11a_e2e.py` | 5 项检查 SSH E2E 门禁（客户机→宿主 paramiko + 客户机 sshd ← 宿主客户端） |
| `run_l1_per_app.py` | P-08 逐应用记录：每个程序启动一次 ISO（每行全新进程表），经 `run <name>` 运行 |
| `run_ocire_tests.py` | OCIRE 独立全套：boot/help/unknown/backspace/shutdown × BIOS+UEFI |
| `run_ocire_dual_tests.py` | WP-01 双启动 + 分区隔离（GRUB 菜单启动 OCOS 或 OCIRE；0xEE 影子盘不动） |
| `run_ocire_shell_tests.py` | WP-02 第 4 组恢复 shell 全套，跑在一次性磁盘副本上（摘要损坏、BPB 被清零的完整修复） |
| `run_ocire_supervised_tests.py` | WP-03 G4 OCIRE 监督启动（60 秒看门狗 + 邮箱 + agent） |
| `wp07a_state_e2e.py` | 启动状态监视门禁：S1 正常阶段 1..6 + 45 秒不复位 + 隔离遍历，S2 user_request → OCIRE，S3 hang_boot 由 30 秒预算诊断，…，S8 OVMF 下 UEFI 冒烟 |
| `wp11_crash_e2e.py` | 崩溃恢复：#DE/#UD/#PF 自测、sigsegv/stackovf/badapp、crashlog 刷到 /var/log、dump + dumpinfo |
| `wp12_watchdog_logging_e2e.py` | 14 个场景 S1–S14：klog 级别/开关、logcat 过滤器、dmesg 回放、logctl、持久化+轮转 `.1` 链、全局过滤器、Intel TCO 真实复位 + 第二次启动、ACPI WDAT、策略击杀、健康 JSON |
| `wp10_native_e2e.py` | 原生桌面流程 |
| `wp10_native_manual.py` | 把抖动检查作为隔离的全新 QEMU 场景重跑（HMP screendump 之后 QEMU 丢串口 RX 字节——经内核内 RX 跟踪验证） |
| `wp10_fix2_render_e2e.py` | 像素级验证 TUI 汉字渲染——对照内嵌字体位图的完整 16×16 字形 |
| `wp10_chinese_shot.py` | 驱动 font-test/tui-cn、screendump、对照内嵌位图比较 CJK 字形块 |
| `wp10_chipset_e2e.py` | 芯片组/输入命令面：input→expected→actual→verdict 记录（BIOS 或 UEFI） |
| `wp10_timer_rtc_e2e.py` | 时间源面：CMOS RTC / HPET / ACPI PM 定时器 / TSC |
| `wp10_wireless_bluetooth_e2e.py` | 无线 + BT 面；BT 是经 COM2 H4 字节进 `bt_test_peer.py` 的真实 E2E |
| `wp10_camera_misc_e2e.py` | UVC 30/30、parallel 10/10（SPP 字节在 chardev 文件中验证）、gameport 6/6、IR 14/14（NEC/RC5/RC6/SIRC）、SCSI 对 LSI 53C895A |
| `wp10_wp08fix1_test.py` | 行编辑（oc> 和 ush）+ ush 工具 + nano 编辑器；文本走串口、方向键走 HMP sendkey |
| `wp_p2_batch_a_test.py` | 经 `p2test` 应用的 A 批次现场探针（read0 #14、管道槽泄漏 #9、…） |
| `test_p1fix3_runtime.py` | BUG-0133 `umount /` 被拒绝、BUG-0134 run/kill tid + 延迟上限、BUG-0135 vi 模态编辑器 |
| `tui_g11c_e2e.py` | 八个全屏 TUI 页：启动、HMP screendump PNG 证据、Esc 离开、提示符回来 |
| `ime_g11b_e2e.py` | 经 PS/2 队列用 HMP sendkey 做 `ime -i` 组合（"ni" + 数字 1 → 你） |
| `fix1_before_ush.py` | WP04-fix1 BEFORE 证据抓取（原始字节 unix-socket 串口通道，控制字节逐字到达） |
| `fix1_before_tui.py` | 同上，TUI 侧 |
| `fix1_after_ush.py` | AFTER 证据抓取（ush） |
| `fix1_after_ush2.py` | AFTER 证据抓取，第二轮 |
| `fix1_checkupdate_e2e.py` | 出厂 ISO 的 checkupdate 看到真实线上源、诚实的 up-to-date（版本反转回归） |
| `fix1_ocire_kbd_scroll.py` | OCIRE PS/2 键盘滚动证明 |
| `fix1_ocire_vga_scroll.py` | OCIRE VGA 文本滚动证明（0xB8000 单元数组的 pmemsave、冻结带分析、无 OCR） |
| `fix1_ocire_vga_scroll2.py` | VGA 滚动证明，第二轮（基于 screendump） |
| `wp01_gui_click_test.py` | BUG #113：经 QMP `input-send-event` 的真实指针点击（按压沿锁存） |
| `wp01_killrace_test.py` | BUG #5：kill/fork 竞态压力，每轮之后提示符保持响应 |
| `wp01_p0leak_test.py` | P0 用户指针栅栏探针（内核地址 sys_write/read 必须被栅栏） |
| `wp01_section_dump.py` | 差异分诊驱动器：运行一组 ush 命令，逐字转储每一节 |
| `wp06g3_baseline.py` | 编码修复全套：基线抓取 |
| `wp06g3_repro_nano_scroll.py` | nano 滚动/串口编码复现 |
| `wp06g3_repro_serial_enc.py` | 串口编码复现 |
| `wp06g3_test_editor_rework.py` | 编辑器重做（zh 头图例、IME 预编辑纪律、无滚动渲染） |
| `wp06g3_test_install_ocire.py` | 带 OCIRE 分区的安装 |
| `wp06g3_test_ocire_menu.py` | OCBL 菜单 OCIRE 条目 |
| `wp06g3_test_ocse_enc.py` | OCSE 编码（utf8/gbk） |
| `wp06g3_test_serial_enc.py` | `serial_encoding` 命令面 |

## 7. 敌意安全对端（9）

p0/p0fix3 敌意服务器驱动器（P0 安全批次；案例矩阵客户端侧
a04/a05、sshd 侧 a09/a13/a38、TLS a18_chain_*）：

| 文件 | 职责 |
|------|----------------|
| `p0_paramiko_evil.py` | BUG-0013 SSH 溢出驱动器（敌意 paramiko 服务器） |
| `p0_evil_tls_server.py` | BUG-0022/0023/0024 TLS 记录过读服务器 |
| `p0_redir_server.py` | BUG-0021 超大重定向路径服务器 |
| `p0_run_p0_case.py` | p0 案例运行器 |
| `p0_run_tls_case.py` | p0 TLS 案例运行器 |
| `p0fix3_evil_tls.py` | chain_buf 回绕（p0fix3 批次） |
| `p0fix3_run_case.py` | p0fix3 案例运行器（矩阵含 `OC_ETC_IMG` 预置主机密钥快速路径） |
| `p0fix3_new01_test.py` | 首次运行主机密钥生成：非阻塞通知、^C 取消、后台完成、持久化 |
| `p0fix3_new01b_test.py` | 第二次启动快速路径 |

## 8. 测试对端服务器（4）

| 文件 | 职责 |
|------|----------------|
| `paramiko_sshd.py` | 宿主 SSH 服务器：记录协商出的 DH 共享密钥 K（与内核 dhtest 交叉核对）、密码 oc/oc、exec 通道 → /bin/sh |
| `https_test_server.py` | 与内核客户端匹配的单请求 OpenSSL HTTPS 服务器（仅 TLS 1.2、0x0067 DHE-RSA-AES128-SHA256、1024 位 RFC 3526 DH、自签名；`observe` 模式 = 原始字节 close_notify 记录器、`eof` 模式 = 硬关闭截断服务器） |
| `update_server.py` | 供 checkupdate 测试的更新清单服务器（HTTP 或 TLS）；整个 QEMU 会话期间循环 |
| `bt_test_peer.py` | COM2 上的 H4 BT 控制器 + 虚拟遥控（L2CAP/SDP/RFCOMM-SPP/HID） |

## 9. 历史一次性重构（7）

为出处保留；它们硬编码了历史绝对根
（历史上位于本树之外的一份检出），不属于任何当前流水线。

| 文件 | 职责 |
|------|----------------|
| `restructure_move.py` | WP-10 project_restructure 阶段 1：按新布局逐项 git-mv |
| `restructure_includes.py` | 阶段 1b：include 改写 |
| `restructure_build_map.py` | 阶段 2a：符号重命名映射构建 |
| `restructure_apply_map.py` | 阶段 2a-final：映射应用 |
| `restructure_rename_iface.py` | 阶段 2：接口重命名 |
| `nak_migrate.py` | NAK 重构引擎：旧→新映射、唯一性不变量、`git mv` + include 改写（映射歧义 / 重复目标 = 失败） |
| `nak_fix_refs.py` | NAK 线的移动后引用修复器 |

## 10. 数据与 C 文件（6）

| 文件 | 是什么 |
|------|-----------|
| `host_gzip_test.c` | 宿主侧 gzip 解压测试程序 |
| `ocire_fat_host_test.c` | 宿主侧 OCIRE FAT 驱动测试 |
| `ocse_kmain_c` | OCSE kmain.c vendoring 载荷（由 `vendor_ocse.sh` 复制） |
| `ime_phrases.txt` | IME 词组表（词 + 多音字条目的显式拼音） |
| `nak_map.tsv` | NAK 重构旧→新映射（77 KB） |
| `update_test_vectors.h` | 已提交的更新器测试向量（由 `gen_gzip_vectors.py` 生成） |

## 11. 新维护者的阅读顺序

1. `build.sh` + `Makefile` 头——任何东西是怎么构建的。
2. `build_release.sh`——发布是怎么被门禁的（然后是
   [process](process.md)、[ci-gates](ci-gates.md)）。
3. `qemu_runner.py`——每个套件都假定的共享 E2E 基底。
4. `gen_userprogs.py` + `gen_api_docs.py`——让系统自我描述的
   两个注册表（内嵌程序；命令文档）。
5. 动分层、`.bss` 布局或确定性之前，先看
   [guard-scripts](guard-scripts.md) 里的守护。

已知清单缺口（已记录，不隐藏）：没有已提交的大应用冒烟脚本
——WP-l1apps-merge 发布说明引用的 21/21 门禁是一个被引用的
台架；重建它的 Phase A/B 清单在 `docs/dev/e2e-suites.md`。
`oc_menuconfig.sh` 的菜单文本仍在宣传已移除的
`CONFIG_TUI`/`CONFIG_CHINESE` 旋钮（Makefile 只消费
NVME/AHCI/VIRTIO_BLK）。
