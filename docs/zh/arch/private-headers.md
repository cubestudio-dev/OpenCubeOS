<!-- SPDX-License-Identifier: Apache-2.0 -->
# 私有头文件：`ocos/interface/` 契约

`ocos/interface/` 是 **ring 3 应用程序唯一可以 include 的头文件集**
——由构建期强制：`tools/gen_userprogs.py` 的 `CFLAGS_BASE` 在 L1 编译
的 `-I` 路径上恰好只导出该目录，`ocos/kernel`、`ocos/drivers` 或
`ocos/services` 之下的任何东西都不会出现在 L1 include 路径上。
`oc_l1_api.h` 在其头注释中言明此规则，并自称“审计 L1 能看见什么的
唯一地点”。八个文件，一个 ABI：

| 文件 | 行数 | 角色 |
|---|---|---|
| `crt0.asm` | 36 | 共享的 ring 3 入口跳板 |
| `oc_l1_api.h` | 36 | 伞形总括 include |
| `l1_lib.h` | 337 | ring 3 运行时库（仅头文件） |
| `l1svc_ops.h` | 245 | L1SVC 协议常量（自动生成的镜像） |
| `fs_lib.h` | 87 | VFS 线上传输结构体 + 辅助函数 |
| `i18n.h` | 114 | 按应用的双语运行时 |
| `wizard_lib.h` | 218 | 交互式向导原语 |
| `editor_core.h` | 810 | 共享编辑器引擎 |

面向读者的配套文档是 [../l1/overview.md](../l1/overview.md)、
[../l1/writing-apps.md](../l1/writing-apps.md) 与
[../l1/app-anatomy.md](../l1/app-anatomy.md)；契约的系统调用一侧是
[syscall-ring3.md](syscall-ring3.md)。

## `crt0.asm`——ring 3 入口

内核 `execve` 以 `[rsp]=argc, [rsp+8]=argv[0]..` 进入新进程——这是
编译出来的 C `_start` 无法安全读取的布局（编译器序言可能在触碰
`rsp` 之前先压栈：WP04 G5 实测发现）。因此每个应用都一次性链入
这个 36 行跳板，并改为定义 `int app_main(int argc, char **argv)`：

```asm
_start:
    mov  rdi, [rsp]              ; argc
    lea  rsi, [rsp + 8]          ; argv
    and  rsp, -16                ; SysV: align before call
    call app_main
    mov  edi, eax                ; exit code
    mov  eax, 16                 ; SYS_EXIT2
    int  0x80
    hlt
section .note.GNU-stack noalloc noexec nowrite progbits
```

退出走 `SYS_EXIT2`（16）——运行统一进程收割器的现代退出。
`noexec` GNU-stack 注记使链接器不至于要求可执行栈。

## `l1svc_ops.h`——冻结的协议镜像

由 `tools/gen_l1svc_ops.py` 生成，是
`ocos/services/sysapi/interface/svc_sysapi_l1svc.h` 的**逐字镜像**
（只取单行 `#define L1SVC_*` 行；跳过 `L1SVC_CAT_NAME` 函数宏），
并在构建应用目录之前由 `gen_userprogs.py` 重新生成。为什么要镜像：
应用不得 include 内核侧头文件，但类别/op 编号是 ring 0 与 ring 3
必须逐字节一致的 ABI——因此生成器把单行常量拷进唯一合法的 include
目录。上游头文件言明契约：“只许追加式重编号”，并注明 cat 18 与
OCIRE 日志代理是逐字契约。20 个类别：

```c
#define L1SVC_CAT_TIME      1   /* services/time    */
#define L1SVC_CAT_BLOCK     2   /* services/block   */
#define L1SVC_CAT_NIC       3   /* services/nic     */
#define L1SVC_CAT_SND       4   /* services/snd     */
#define L1SVC_CAT_POWER     5   /* services/power   */
#define L1SVC_CAT_OTA       6   /* services/ota     */
#define L1SVC_CAT_FS        7   /* services/fs      */
#define L1SVC_CAT_NET       8   /* services/net     */
#define L1SVC_CAT_HEALTH    9   /* services/health  */
#define L1SVC_CAT_KLOG     10   /* services/klog    */
#define L1SVC_CAT_WATCHDOG 11   /* services/watchdog*/
#define L1SVC_CAT_CRYPTO   12   /* services/crypto  */
#define L1SVC_CAT_L1       13   /* services/l1      */
#define L1SVC_CAT_DISPLAY  14   /* services/display */
#define L1SVC_CAT_INPUT    15   /* services/input   */
#define L1SVC_CAT_CAMERA   16   /* services/camera  */
#define L1SVC_CAT_USB      17   /* services/usb      */
#define L1SVC_CAT_OCIRE    18   /* services/ocembed  */
#define L1SVC_CAT_DIAG     19   /* services/diag     */
#define L1SVC_CAT_TUI      20   /* services/tui      */
```

随后是 208 个 op 定义，每个服务操作一个（完整表格见
[syscall-ring3.md](syscall-ring3.md)）。使用者：经 `l1_lib.h` 的
`l1svc()` 包装使用它的每个 L1 应用，以及 OCIRE 代理（它把这些
编号当 ABI 编译，而非 include）。

## `oc_l1_api.h`——伞形总括

包含其余五个 C 头文件（`l1_lib.h`、`l1svc_ops.h`、`fs_lib.h`、
`editor_core.h`、`i18n.h`）。只 include 这一个头的应用即可看见全部
合法 L1 面——这正是让上文审计声明成立的原因。`crt0.asm` 是链接
而非 include；`wizard_lib.h` 由需要它的应用直接 include。

## `l1_lib.h`——ring 3 运行时库

仅头文件（全部 `static inline`）——每个应用编译自己的副本，不存在
共享的用户库。按依赖顺序导出：

- **系统调用号**——与
  `ocos/services/sysapi/interface/svc_sysapi_syscall.h` 的逐字契约：
  `SYS_WRITE 1` …… `SYS_L1SVC 110`（54 个；完整表见
  [syscall-ring3.md](syscall-ring3.md)）。
- **原始系统调用辅助**：`syscall0..syscall4`（`int $0x80`，`r10`
  作第 4 参）外加 `syscall5`——**`r8` 作第 5 参**的变体，仅
  `SYS_L1SVC` 使用。
- **带类型包装**：`sys_write/read/open/close/exit2/getpid/l1svc/ getcwd/getch/uptime/meminfo/kill/ps/readdir/mkdir/rmdir/unlink/stat/ symlink/readlink/link/chmod/chown/netcmd`。
- **`l1svc_main(argc, argv, cat, op)`**——标准包装应用体：把
  `argv[1..]` 以空格连接成 256 字节 arg，用 `static char out[32768]`
  （BSS，不是栈——缓冲有 32 KiB）调用
  `sys_l1svc(cat, op, arg, out, 4096)`，打印结果；失败时打印
  `"<name>: " tr_("svc_call_failed")`。284 个应用中的大多数就是这
  一包装再加一行。
- **微型字符串/IO**：`strlen_ streq_ starts_with_ strcpy_ strcat_ strcmp_ atoi_ puts_ putc_ putu_ putx_`。
- **定宽别名**（`u64/i64/i32/u32/i8/u8`）与内核 ABI 结构体
  `struct ush_ps_entry {i32 tid; i32 state; i32 priority; char name[32]; u64 cpu_time_ticks;}`（`SYS_PS` 的线上形态）。
- 在其尾部 include `i18n.h`，因此每个应用免费获得 `tr_()`。

## `fs_lib.h`——VFS 线上传输结构体

这些结构体必须与内核的 `vfs.h` 逐字节一致（`VFS_NAME_LEN=64`；
内核把 `sizeof(struct)` memcpy 进调用者缓冲——不一致将是无声的
内存破坏）：

```c
#define USH_TYPE_FILE 1
#define USH_TYPE_DIR  2
#define USH_TYPE_DEV  3
struct ush_dirent { char name[64]; int type; u64 inode; };
struct ush_stat  { int type; u64 size; u32 mode; u32 uid; u32 gid; u32 nlink; char name[64]; };
```

外加与 ush 逐字相同的辅助函数 `strncpy_ strstr_ strncmp_ memset_`。
使用者：每个触碰文件的应用（`ls`、`files`、编辑器的载入/保存
路径、`disk-tool`）。

## `i18n.h`——按应用的双语运行时

按应用的表生成进每个应用的 `i18n_gen.c`（`I18N_EN[]/I18N_ZH[]`，
按 key 排序的 `struct i18n_kv`），来源是应用的 `i18n/{en,zh}.json`
（保留键 `"_hint"` 是 `gen_userprogs.py` 消费的注册表提示）。两个
设计要点：

- **实时区域探测** `i18n_lang_zh()`：自足的内联 `int $0x80`——
  `SYS_L1SVC(110), cat=L1SVC_CAT_DISPLAY, op=L1SVC_DISP_LANG_GET, arg=0, out=&buf, cap=8`；当 `out` 以 `"zh"` 开头时为真。每次
  `tr_()` 调用都探测，**无静态缓存**（#136 修复，使 `lang zh` 在
  长时间运行的应用中立即生效）。
- **`tr_(key)` 未命中策略**：区域为 zh 时用 zh 表（未命中 -> en），
  否则用 en 表，仍未命中 -> key 本身。“未翻译的应用永远不会崩溃
  或打印乱码。"

使用者：每个应用，包括 8 个巨型应用（它们共享的向导键
`wiz_quit`/`wiz_select`/`wiz_pause`/`wiz_svc_failed` 流经此运行时）。
L0 侧的对应物（内核服务自身的 i18n）是 `core_lib_i18n.c` +
`etc/i18n/l0/*.json`——另一套机制，见 [chinese-layer.md](chinese-layer.md)。

## `wizard_lib.h`——向导原语 + 锚点契约

由 8 个合并巨型应用使用（`update`、`disk-tool`、`net-tool`、
`sound-tool`、`files`、`sysdiag`、`hwinfo`、`power`）。契约：调用者
持有一个 `WIZ_OUT_CAP 32768` 的静态 BSS 输出缓冲；行输入经
`SYS_READLINE`；单键选择经非阻塞 `SYS_GETCH`（数字 1..9，q/Q/ESC
中止）；一切人类可读字符串经 `tr_()`。

承重的思想是**锚点机制**：向导把服务自身输出的英文行当作稳定契约
来解析——`wiz_find(hay, needle)`（子串探测）与
`wiz_token_after(hay, key, dst, cap)`（跟在 `key` 之后的首个空白
分隔记号——`"IP: 10.0.2.15"` + `"IP:"` -> `"10.0.2.15"`）。没有
发明任何结构化 API；渲染出的文本本身就是接口。

完整原语清单：`wiz_svc` / `wiz_svc_quiet`（运行一个 L1SVC op，
打印或保持沉默）、`wiz_ask`（阻塞行）、`wiz_confirm`（只有裸 y/Y
算作是）、`wiz_pause`、`wiz_menu`（数字单键选择器，条目是 i18n
key）、`wiz_banner`、`wiz_step(step,total,what)`、`wiz_ok` /
`wiz_fail`（`"[OK] <s>"` / `"[!!] <s>"`）。OCSE 侧的对应物（其
命令面之上的自有向导）是 `ocse/ocse_wzctl.c`——同一模式，不同
世界。教程见 [../l1/wizard-library.md](../l1/wizard-library.md)。

## `editor_core.h`——共享编辑器引擎

810 行的 nano 风格引擎，被 **nano**、**vi**（在其模态层之下）与
**edit** include（而非链接）——三条命令，一个引擎。关键事实：

- 固定缓冲：`UNANO_MAX_LINES 256` x `UNANO_LINE_CAP 128`、单行
  剪贴板、IME strip[160]、差分状态栏 `unano_bar_prev[192]`。
- **BUG #34 守卫**：载入时若文件超出限制则置位
  `unano_file_truncated`；`unano_save()` 随即拒绝覆写
  （“save REFUSED, original file untouched”）。
- UTF-8 纪律：`u8_is_lead/u8_prev/u8_next/u8_cols` 外加宽区表
  `k_wide_extra[]`（2E80-2EFF、3000-303f、3041-30FF、3400-4DBF、
  4E00-9FFF、F900-FAFF、FF00-FF60、FFE0-FFE6 -> 2 单元格）——与
  内核控制台应用的宽度规则相同（[chinese-layer.md](chinese-layer.md)）。
- 视口：`ED_VIEW_ROWS 20`，位于 `ED_ROW_BASE 2`；为固定状态栏
  使用 ANSI 保存/恢复寻址；退出时清屏。
- **系统 IME 桥**：F2（键码 0x91）切换；合成期间的每次按键都喂给
  `unano_ime_feed(key)` -> L1SVC `DISPLAY/IME_KEY`，以键码十进制串
  作为 arg。返回的首行是动作数字：`0` 与 IME 无关、`1` 预编辑条
  已更新、`2` 提交（插入文本）、`3` 提交 + 调用者处理按键；
  **动作 4** 是对服务失败/协议畸形的本地归类——按键被吞掉，绝不
  泄漏进文件（验尸灾难 6）。
- 键位表（nano 面孔）：0x80..0x83 方向键、0x84/0x01 Home、
  0x85/0x05 End、0x89 Delete、ENTER 拆行、^K 剪切、^R 复制、^U
  粘贴、^C 位置、^G 帮助、^O 保存、^X 退出（脏提示）、0x20..0x7E
  插入。
- `unano_save` 以标志 22（WRONLY|CREAT|TRUNC）打开；`unano_load`
  经 4096 字节静态缓冲读取，并为上述截断守卫跟踪 `dropped_lines` /
  `long_line`。

## 谁消费什么（摘要）

| 头文件 | 消费者 |
|---|---|
| `crt0.asm` | 每个 L1 应用 + OCIRE 日志代理（经同一 `user.ld` 模式链接） |
| `oc_l1_api.h` | 想一次 include 拿到全部面的应用 |
| `l1_lib.h` | 每个 L1 应用（直接或经伞形头） |
| `l1svc_ops.h` | `l1_lib.h` 的使用者；OCIRE 代理（ABI 编号） |
| `fs_lib.h` | 操作文件的应用 |
| `i18n.h` | 每个带 `i18n/` 目录的应用（由 `l1_lib.h` 尾部自动 include） |
| `wizard_lib.h` | 8 个巨型应用 |
| `editor_core.h` | `nano`、`vi`、`edit` |
