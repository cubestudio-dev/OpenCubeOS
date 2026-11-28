<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1SVC 接口字典

L1SVC ring-3 服务调用面的完整权威清单：20 个类别、208 个
编号操作，经唯一系统调用 `SYS_L1SVC`（110）到达。本页合并
内核侧编号头文件
（`ocos/services/sysapi/interface/svc_sysapi_l1svc.h`，
461 行）、自动生成的 ring-3 镜像
（`ocos/interface/l1_svc_ops.h`，由
`tools/gen_l1svc_ops.py` 产出），以及在服务源码中核验过的
各家族 op 表。下述结构体与宏为原文照录。

配套页面：系统调用 ABI 本身见
[syscall-reference.md](syscall-reference.md)；如何添加*新*
服务或 op 见 [writing-services.md](writing-services.md)；经
`core_kcap` 的调用路径在
[call-chains.md](call-chains.md) 中解剖。

## 1. 协议基本规则

下列每条规则都以 触发 -> 处理 -> 影响 的方式陈述；每一条
都是承重的且在源码中核验过。

### 1.1 调用形状

```c
static inline long syscall5(long n, long a, long b, long c, long e, long f) {
    long r;
    register long r10 __asm__("r10") = e;
    register long r8  __asm__("r8")  = f;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                     : "memory");
    return r;
}
```

（来自 `ocos/interface/l1_api_syscall.h` 的 ring-3 辅助函数；类型化
包装 `sys_l1svc(cat, op, arg, out, cap)` 调用
`syscall5(SYS_L1SVC, cat, op, arg, out, cap)`）：

- **触发**：一个 ring-3 程序想要一次服务操作。它从不链接
  内核代码；唯一的门是以 `rax=110`、五个参数位于
  `rdi, rsi, rdx, r10, r8` 的 `int 0x80`（第 5 参走 `r8`，
  入口路径在处理函数运行前把它暂存进
  `core_syscall_arg5()`）。
- **处理**：`ocos/services/sysapi/src/svc_sysapi_syscall.c`
  中的 `sys_l1svc()` 把 `arg` 拷入内核缓冲区，经
  `L1SVC_CAT_NAME(cat)` 把 `cat` 路由到所属服务的
  `l1_dispatch(op, arg, out, cap)`，捕获渲染出的文本并
  回拷。
- **影响**：调用方在 `out` 中收到一块 NUL 结尾的纯 ASCII
  文本；内核返回值只说明调用是否*路由成功*，从不说明操作
  是否"成功"（见 1.5）。

### 1.2 缓冲区限制

| 限制 | 值 | 原因 |
|---|---|---|
| `cap`（out 容量） | 1..32768 | WP06g2 从 8192 提升，使 `help -a` 能装下完整 284 程序双语注册清单 |
| `arg` 长度 | 256 字节 | NUL 结尾的用户字符串，拷入固定内核缓冲区 |
| OCIRE `arg` 长度 | 4096 字节 | 仅 cat 18：日志代理一次 `LOG_APPEND` 至多追加 4 KiB |
| `out == 0 && cap == 0` | 允许 | OCIRE 发后即忘拼法：渲染进 256 字节内核暂存缓冲区，跳过回拷（3.0.0-WP03 G4） |

### 1.3 分发前后的中断状态

- **触发**：op 体可能包含 tick 驱动的设备等待循环（磁盘
  稳定、DHCP 超时、音频轮询）。
- **处理**：`sys_l1svc()` 在 `svc_framework_dispatch(...)`
  之前执行 `__asm__ volatile("sti")`、之后执行
  `__asm__ volatile("cli")`（v2.0.0 的 "group N" 修复）。
- **影响**：op 体可以在调度器 tick 上睡眠/让出并仍经系统
  调用路径返回；没有这一点，第一个长 op 会冻住整台机器
  而不是只冻住调用方。

### 1.4 文本锚点契约

- **触发**：任何消费者 —— shell 脚本、mega 应用向导、E2E
  回归门禁、`tools/gen_api_docs.py` —— 需要以编程方式
  读取一个 op 的输出。
- **处理**：每个 op 渲染*纯 ASCII 英文文本*，面向行，带
  稳定的关键子串（"A/B disk: present"、"New version
  available."、"DHCP success"、"DNS resolution failed"、
  "reboot required"、"Current version is up to date."、
  "IP:"）。`ocos/interface/l1_wizard_api.h` 中的
  `wiz_find()`/`wiz_token_after()` 解析的正是这些锚点；
  ring-3 侧不做任何格式化。
- **影响**：服务输出字符串就是 ABI。改动一句渲染出的句子
  对向导与发布门禁的破坏与重排 op 编号完全等同 —— 英文
  输出行是冻结契约（另见
  [../root/CONTRIBUTING.md](../root/CONTRIBUTING.md) 的
  接口冻结规则）。

### 1.5 返回码语义

- **触发**：op 完成（或失败）且 `sys_l1svc` 返回。
- **处理**：服务自身的非零退出状态*不会*作为系统调用错误
  传播。只有不可路由的类别/op（或 `rc < 0` 且 `out` 缓冲
  区为空）返回 `-1`；其余一切把 NUL 结尾的文本
  `copy_to_user` 并返回 `0`。
- **影响**：失败报告*活在这些文本里*（"...: FAILED (...)"、
  "usage: ..."），这正是上述锚点是契约的原因，也是 ring-3
  包装（`l1svc_main()`）只在 `rc != 0`（即只在路由失败）
  时打印 `"<name>: " tr_("svc_call_failed")` 的原因。

### 1.6 编号纪律

- **触发**：需要一个新操作。
- **处理**：向 `svc_sysapi_l1svc.h` 追加一条新的
  `#define L1SVC_<CAT>_<NAME> <next>`；
  `tools/gen_l1svc_ops.py` 重新生成 ring-3 镜像；ush 侧的
  常量块是逐字副本。
- **影响**：**只允许以追加方式编号**。既有编号是冻结 ABI：
  cat 18（"ocire"）还额外相对 `ocire/agent/ocire_agent.c`
  冻结 —— 后者把这些编号当作 ABI 编译，而非源码依赖。

### 1.7 三种路由方式

一个 op 的文本如何进入 `out`（按 op 在家族分发器中逐个
决定）：

1. **捕获** —— `core_kcap_run_fn(fn, arg, out, cap)`：
   op 体是一个经 `screen_console_puts` 打印的旧 shell 命令
   体；捕获激活期间捕获钩子*就是*控制台钩子（唯一槽位），
   字符被收集进 `out`，帧缓冲绘制与串口 tee 被压制，捕获
   可叠至 4 层，溢出被标记、绝不静默截断。用于一次性文本
   op。
2. **直连** —— `out[0]0; return fn(arg);`：实时控制台
   会话（`sshd`、全部 9 个 TUI op、`ime -i`、
   `power sleep`）；op 自身的控制台输出就是会话，串口 tee
   原样镜像，`out` 保持为空。
3. **缓冲区渲染** —— 分发器用自己的 `put()` 辅助函数写
   `out`（`l1`、`klog`、`health`、`diag`、`fs`、`time`、
   `watchdog` 分发器）。

一条文档化的现场注记：`tui-test` 最初以捕获方式路由，但
其约 4.4 KiB 的结论块溢出了当时使用的 4096 字节 SVC 结果
缓冲区并截断了最终摘要，因此改为直连。`power sleep` 与
`sshd` 直连路由，因为以捕获方式运行它们会在停驻期间吞掉
每一条实时控制台行（在 OCIRE 监督启动期间现场发现）。

## 2. 20 个类别

| # | 类别字符串 | 所属目录 | op 数 | 家族角色 |
|---|---|---|---|---|
| 1 | `time` | `services/time` | 4 | 时钟源：RTC / HPET / PM timer / TSC |
| 2 | `block` | `services/block` | 23 | 块设备、mkfs、分区、安装 |
| 3 | `nic` | `services/net`（nic 面） | 20 | 各家族 NIC 状态 + 测试 + wifi/bt |
| 4 | `snd` | `services/audio` | 17 | 声卡、播放、音量、测试 |
| 5 | `power` | `services/power` | 8 | 关机/重启/停机/睡眠 + 路径测试 |
| 6 | `ota` | `services/selfhost` | 16 | A/B 槽位、更新流程、配置存储 |
| 7 | `fs` | `services/fs` | 6 | mount/umount/FAT32/tree |
| 8 | `net` | `services/net` | 14 | 协议栈诊断：arp/dhcp/dns/firewall/ssh |
| 9 | `health` | `services/health` | 3 | 快照 / 导出 / 监视 |
| 10 | `klog` | `services/klog` | 3 | dmesg / logcat / logctl |
| 11 | `watchdog` | `services/watchdog` | 1 | 整个 `watchdog` 子命令语法 |
| 12 | `crypto` | `services/crypto` | 2 | cryptotest / dhtest 自检 |
| 13 | `l1` | `services/l1` | 3 | 注册清单、l1test、ldd |
| 14 | `display` | `services/display` | 17 | 控制台/区域设置/字体/IME/串口编码 |
| 15 | `input` | `services/display`（input 面） | 2 | 触摸板状态 + 测试 |
| 16 | `camera` | `services/camera` | 10 | UVC + SCSI/gameport/parallel/IR（历史分组） |
| 17 | `usb` | `services/usb` | 9 | 主机栈状态 + 类测试 |
| 18 | `ocire` | `services/ocembed` | 2 | 仅限守卫的日志通道（op 4-5，冻结 ABI） |
| 19 | `diag` | `services/diag` | 39 | 生命体征 + ring-0 自检 + 芯片组 |
| 20 | `tui` | `services/tui` | 9 | 全屏桌面 + 页面 |

无类别、无 op 的纯内核家族：`crash`、`encoding`、`font`、
`ipc`、`loader`、`sysapi`（机制所有者）。

## 3. 完整编号块（原文照录）

以下是 `ocos/services/sysapi/interface/svc_sysapi_l1svc.h`
的逐字 `#define` 集（镜像到
`ocos/interface/l1_svc_ops.h`）：20 个类别 define + 208 个
op define = 228 条 `L1SVC_*` define。

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
#define L1SVC_TIME_RTC      1   /* rtc [uie N]        : CMOS RTC status  */
#define L1SVC_TIME_HPET     2   /* hpet               : HPET status      */
#define L1SVC_TIME_PMTIMER  3   /* pmtimer            : PM timer status  */
#define L1SVC_TIME_TSC      4   /* tsc                : TSC status       */
#define L1SVC_FS_MOUNT      1   /* mount [type dev p] : list or mount    */
#define L1SVC_FS_MOUNTS     2   /* mounts             : list (alias)     */
#define L1SVC_FS_UMOUNT     3   /* umount <path>      : unmount          */
#define L1SVC_FS_FATMOUNT   4   /* fatmount [dev p]   : mount FAT32      */
#define L1SVC_FS_FATSTAT    5   /* fatstat            : FAT32 stats      */
#define L1SVC_FS_TREE       6   /* tree [path]        : directory tree   */
#define L1SVC_BLK_AHCI          1   /* ahci                    */
#define L1SVC_BLK_NVME          2   /* nvme                    */
#define L1SVC_BLK_ATA           3   /* ata                     */
#define L1SVC_BLK_DSKSTAT       4   /* dskstat                 */
#define L1SVC_BLK_LSBLK         5   /* lsblk                   */
#define L1SVC_BLK_PARTED        6   /* parted [dev]            */
#define L1SVC_BLK_MKFS_FAT32    7   /* mkfs.fat32 <dev>        */
#define L1SVC_BLK_MKFS_EXFAT    8   /* mkfs.exfat <dev>        */
#define L1SVC_BLK_MKFS_EXT4     9   /* mkfs.ext4 <dev>         */
#define L1SVC_BLK_FSCK         10   /* fsck                    */
#define L1SVC_BLK_SYNC         11   /* sync                    */
#define L1SVC_BLK_AHCI_TEST    12   /* ahci_test               */
#define L1SVC_BLK_NVME_TEST    13   /* nvme_test               */
#define L1SVC_BLK_ATA_DMA_TEST 14   /* ata_dma_test            */
#define L1SVC_BLK_VIRTIO_TEST  15   /* virtio_blk_test         */
#define L1SVC_BLK_RW_TEST      16   /* disk_rw_test <dev>      */
#define L1SVC_BLK_PART_TEST    17   /* partition_test [dev]    */
#define L1SVC_BLK_FS_MOUNT_TEST 18  /* fs_mount_test <dev>     */
#define L1SVC_BLK_REAL_HW_TEST 19   /* real_hw_test            */
#define L1SVC_BLK_ABDISK       20   /* abdisk <dev>            */
#define L1SVC_BLK_INSTALL      21   /* install <dev>           */
#define L1SVC_BLK_GRUB_INSTALL 22   /* grub-install <dev>      */
#define L1SVC_BLK_ABCFG        23   /* abcfg                   */
#define L1SVC_NIC_E1000E        1   /* e1000e                  */
#define L1SVC_NIC_IGB           2   /* igb                     */
#define L1SVC_NIC_IXGBE         3   /* ixgbe                   */
#define L1SVC_NIC_RTL8139       4   /* rtl8139                 */
#define L1SVC_NIC_RTL8168       5   /* rtl8168                 */
#define L1SVC_NIC_RTL8125       6   /* rtl8125                 */
#define L1SVC_NIC_RTL810X       7   /* rtl810x                 */
#define L1SVC_NIC_BCM57XX       8   /* bcm57xx                 */
#define L1SVC_NIC_E1000E_TEST   9   /* e1000e_test             */
#define L1SVC_NIC_IGB_TEST     10   /* igb_test                */
#define L1SVC_NIC_IXGBE_TEST   11   /* ixgbe_test              */
#define L1SVC_NIC_RTL8139_TEST 12   /* rtl8139_test            */
#define L1SVC_NIC_RTL8168_TEST 13   /* rtl8168_test            */
#define L1SVC_NIC_RTL8125_TEST 14   /* rtl8125_test            */
#define L1SVC_NIC_RTL810X_TEST 15   /* rtl810x_test            */
#define L1SVC_NIC_BCM57XX_TEST 16   /* bcm57xx_test            */
#define L1SVC_NIC_OTHER_TEST   17   /* other_nic_test          */
#define L1SVC_NIC_RW_TEST      18   /* nic_rw_test             */
#define L1SVC_NIC_WIFI         19   /* wifi [status|scan|...]  */
#define L1SVC_NIC_BT           20   /* bt [status|usb|test]    */
#define L1SVC_SND_SOUND        1   /* sound                   */
#define L1SVC_SND_HDA          2   /* hda                     */
#define L1SVC_SND_AC97         3   /* ac97                    */
#define L1SVC_SND_SB16         4   /* sb16                    */
#define L1SVC_SND_ES1370       5   /* es1370                  */
#define L1SVC_SND_VIRTIOSND    6   /* virtiosnd               */
#define L1SVC_SND_USBAUDIO     7   /* usbaudio                */
#define L1SVC_SND_PLAY         8   /* play <file>             */
#define L1SVC_SND_VOLUME       9   /* volume [0-100]          */
#define L1SVC_SND_HDA_TEST    10   /* hda_test                */
#define L1SVC_SND_AC97_TEST   11   /* ac97_test               */
#define L1SVC_SND_SB16_TEST   12   /* sb16_test               */
#define L1SVC_SND_ES1370_TEST 13   /* es1370_test             */
#define L1SVC_SND_VIRTIO_TEST 14   /* virtio_snd_test         */
#define L1SVC_SND_USB_TEST    15   /* usb_audio_test          */
#define L1SVC_SND_RW_TEST     16   /* audio_rw_test           */
#define L1SVC_SND_RATE_TEST   17   /* sample_rate_test        */
#define L1SVC_OTA_CHECKUPDATE      1  /* checkupdate           */
#define L1SVC_OTA_CONFIG           2  /* config list|get|set   */
#define L1SVC_OTA_CONFIG_TEST      3  /* config_test           */
#define L1SVC_OTA_CHECKUPDATE_TEST 4  /* checkupdate_test      */
#define L1SVC_OTA_UPDATE           5  /* update [...]          */
#define L1SVC_OTA_ROLLBACK         6  /* rollback              */
#define L1SVC_OTA_PKG_TEST         7  /* update_pkg_test       */
#define L1SVC_OTA_AB_TEST          8  /* ab_partition_test     */
#define L1SVC_OTA_CHECK_TEST       9  /* update_check_test     */
#define L1SVC_OTA_DOWNLOAD_TEST   10  /* update_download_test  */
#define L1SVC_OTA_VERIFY_TEST     11  /* update_verify_test    */
#define L1SVC_OTA_INSTALL_TEST    12  /* update_install_test   */
#define L1SVC_OTA_ROLLBACK_TEST   13  /* update_rollback_test  */
#define L1SVC_OTA_LOCAL_TEST      14  /* update_local_test     */
#define L1SVC_OTA_STATUS_TEST     15  /* update_status_test    */
#define L1SVC_OTA_REAL_TEST       16  /* real_update_test      */
#define L1SVC_PWR_SHUTDOWN        1  /* shutdown / poweroff   */
#define L1SVC_PWR_REBOOT          2  /* reboot                */
#define L1SVC_PWR_HALT            3  /* halt                  */
#define L1SVC_PWR_SLEEP           4  /* sleep <sec>           */
#define L1SVC_PWR_POWEROFF_TEST   5  /* poweroff_test         */
#define L1SVC_PWR_SUSPEND_TEST    6  /* suspend_test          */
#define L1SVC_PWR_HALT_TEST       7  /* halt_test             */
#define L1SVC_PWR_REBOOT_TEST     8  /* reboot_test           */
#define L1SVC_DISP_STATS       1   /* stats                   */
#define L1SVC_DISP_UNAME       2   /* uname [-a|-s|-r|-m]     */
#define L1SVC_DISP_CLEAR       3   /* clear                   */
#define L1SVC_DISP_LANG        4   /* lang [en|zh]            */
#define L1SVC_DISP_CHINESE     5   /* chinese                 */
#define L1SVC_DISP_CHINESETEST 6   /* chinesetest             */
#define L1SVC_DISP_FONT        7   /* font                    */
#define L1SVC_DISP_FONT_TEST   8   /* font-test               */
#define L1SVC_DISP_ENCODING    9   /* encoding                */
#define L1SVC_DISP_ENC_TEST   10   /* encoding-test           */
#define L1SVC_DISP_IME        11   /* ime [-i]                */
#define L1SVC_DISP_IME_TEST   12   /* ime-test                */
#define L1SVC_DISP_IME_BEGIN  13   /* ime session open        */
#define L1SVC_DISP_IME_KEY    14   /* ime session key feed    */
#define L1SVC_DISP_IME_END    15   /* ime session close       */
#define L1SVC_DISP_LANG_GET   16   /* locale query (en|zh)    */
#define L1SVC_DISP_SERIAL_ENC 17   /* serial_encoding [utf8|gbk] (WP06g3) */
#define L1SVC_INP_TOUCHPAD     1   /* touchpad                */
#define L1SVC_INP_TOUCHTEST    2   /* touchtest               */
#define L1SVC_CAM_UVC           1   /* uvc                     */
#define L1SVC_CAM_UVCTEST       2   /* uvctest                 */
#define L1SVC_CAM_SCSI          3   /* scsi                    */
#define L1SVC_CAM_SCSITEST      4   /* scsitest                */
#define L1SVC_CAM_GAMEPORT      5   /* gameport                */
#define L1SVC_CAM_GAMEPORTTEST  6   /* gameporttest            */
#define L1SVC_CAM_PARALLEL      7   /* parallel                */
#define L1SVC_CAM_PARALLELTEST  8   /* paralleltest            */
#define L1SVC_CAM_IR            9   /* ir                      */
#define L1SVC_CAM_IRTEST       10   /* irtest                  */
#define L1SVC_USB_STATUS        1   /* usb                     */
#define L1SVC_USB_DEV           2   /* usbdev [slot]           */
#define L1SVC_USB_CORE_TEST     3   /* usb_core_test           */
#define L1SVC_USB_KBD_TEST      4   /* usb_kbd_test            */
#define L1SVC_USB_MOUSE_TEST    5   /* usb_mouse_test          */
#define L1SVC_USB_STORAGE_TEST  6   /* usb_storage_test        */
#define L1SVC_USB_SERIAL_TEST   7   /* usb_serial_test         */
#define L1SVC_USB_HOTPLUG_TEST  8   /* usb_hotplug_test [...]  */
#define L1SVC_USB_HUB_TEST      9   /* usb_hub_test            */
#define L1SVC_NET_ARP           1   /* arp                     */
#define L1SVC_NET_DHCP          2   /* dhcp                    */
#define L1SVC_NET_DNS           3   /* dns <host>              */
#define L1SVC_NET_DNSTEST       4   /* dnstest                 */
#define L1SVC_NET_FIREWALL      5   /* firewall [...]          */
#define L1SVC_NET_IP            6   /* ip                      */
#define L1SVC_NET_LSPCI         7   /* lspci                   */
#define L1SVC_NET_NF_TEST       8   /* nf_test                 */
#define L1SVC_NET_ROUTE         9   /* route                   */
#define L1SVC_NET_TCPCC_TEST   10   /* tcpcc_test              */
#define L1SVC_NET_TCPSTATS     11   /* tcpstats                */
#define L1SVC_NET_TCPTEST      12   /* tcptest                 */
#define L1SVC_NET_SSH          13   /* ssh <ip> [port] [user] [pass] */
#define L1SVC_NET_SSHD         14   /* sshd [port] [user] [pass]    */
#define L1SVC_CRYPTO_CRYPTOTEST 1   /* cryptotest              */
#define L1SVC_CRYPTO_DHTEST     2   /* dhtest                  */
#define L1SVC_OCIRE_LOG_APPEND    4   /* guard-only: append text to the
                                      * boot-log ring on the OCIRE
                                      * volume (arg = payload)      */
#define L1SVC_OCIRE_KLOG_SNAPSHOT 5   /* guard-only: kernel log ring
                                      * text into out (cap)         */
#define L1SVC_HEALTH_SNAPSHOT 1   /* health                  */
#define L1SVC_HEALTH_EXPORT   2   /* health export [path]    */
#define L1SVC_HEALTH_WATCH    3   /* health watch (loop, ENTER exits) */
#define L1SVC_KLOG_DMESG  1   /* dmesg [N]                    */
#define L1SVC_KLOG_LOGCAT 2   /* logcat -l lvl -m mod -k key -t sec -n N [-f] */
#define L1SVC_KLOG_LOGCTL 3   /* logctl status|level|...      */
#define L1SVC_WDT_CMD 1     /* watchdog [subcommand ...]     */
#define L1SVC_L1_APPS   1   /* registry listing; fix1: arg selects
                                 * the mode - "" plain A-Z listing,
                                 * "-a" full A-Z with wp tags, "-w"
                                 * grouped by work package */
#define L1SVC_L1_L1TEST 2   /* l1test (job + wp8b + CPL0 smoke)  */
#define L1SVC_L1_LDD    3   /* ldd <program>                     */
#define L1SVC_DIAG_MEM       1   /* mem        : PMM stats             */
#define L1SVC_DIAG_FRAG      2   /* frag       : PMM fragments        */
#define L1SVC_DIAG_HEAP      3   /* heap       : kernel heap stats     */
#define L1SVC_DIAG_SCHED     4   /* sched      : scheduler stats      */
#define L1SVC_DIAG_SYNCSTAT  5   /* syncstat   : sync primitive stats  */
#define L1SVC_DIAG_CRASHLOG  6   /* crashlog [flush]                   */
#define L1SVC_DIAG_CRASHTEST 7   /* crashtest panic|exc|...|policy ... */
#define L1SVC_DIAG_DUMPINFO  8   /* dumpinfo   : core dump status      */
#define L1SVC_DIAG_CR3TEST   9
#define L1SVC_DIAG_EXC      10
#define L1SVC_DIAG_PFTEST   11
#define L1SVC_DIAG_PMMRACE  12
#define L1SVC_DIAG_VMTEST   13
#define L1SVC_DIAG_VMKERNELPT 14
#define L1SVC_DIAG_VMMAP    15
#define L1SVC_DIAG_MEMTEST  16
#define L1SVC_DIAG_HEAPTEST 17
#define L1SVC_DIAG_HEAPBOUNDS 18
#define L1SVC_DIAG_IRQABITEST 19
#define L1SVC_DIAG_TIMER    20
#define L1SVC_DIAG_MSITEST  21
#define L1SVC_DIAG_SPAWN    22
#define L1SVC_DIAG_MULTI    23
#define L1SVC_DIAG_NICE     24
#define L1SVC_DIAG_SYNCTEST 25
#define L1SVC_DIAG_FSTEST   26
#define L1SVC_DIAG_ACPI      27
#define L1SVC_DIAG_ACPIDEV   28
#define L1SVC_DIAG_ACPIPOW   29
#define L1SVC_DIAG_ACPITEST  30
#define L1SVC_DIAG_PCIDUMP   31
#define L1SVC_DIAG_PCIBARS   32
#define L1SVC_DIAG_PCIE      33
#define L1SVC_DIAG_I2CDETECT 34
#define L1SVC_DIAG_I2CGET    35
#define L1SVC_DIAG_I2CSET    36
#define L1SVC_DIAG_I2CTEST   37
#define L1SVC_DIAG_SMBUS     38
#define L1SVC_DIAG_SMBUSTEST 39
#define L1SVC_TUI_TUI        1   /* tui      (desktop)       */
#define L1SVC_TUI_TEST       2   /* tui-test (selftests)     */
#define L1SVC_TUI_HW         3   /* tui-hw   [page]          */
#define L1SVC_TUI_NET        4   /* tui-net                  */
#define L1SVC_TUI_SYSINFO    5   /* tui-sysinfo [page]       */
#define L1SVC_TUI_TIME       6   /* tui-time                 */
#define L1SVC_TUI_DRV        7   /* tui-drv   [page]         */
#define L1SVC_TUI_SYS        8   /* tui-sys   [page]         */
#define L1SVC_TUI_EXT        9   /* tui-ext                  */
```

各类别 op 计数：TIME 4、FS 6、BLOCK 23、NIC 20、SND 17、
OTA 16、POWER 8、DISPLAY 17、INPUT 2、CAMERA 10、USB 9、
NET 14、CRYPTO 2、OCIRE 2（op 4-5，仅限守卫）、HEALTH 3、
KLOG 3、WATCHDOG 1、L1 3、DIAG 39、TUI 9 = **208**。

## 4. 逐类别操作表

"运行名"列是已注册的 ring-3 程序名（标准包装；测试桩清单
见 [debug-interfaces.md](debug-interfaces.md)）。"路由"是
1.7 中的方式。

### 4.1 time（cat 1）—— services/time

启动保持在 kmain（顺序敏感：RTC 先于 HPET，先于启动日志
时间戳所用的已校准 TSC 读取）。所有 op 缓冲区渲染状态
文本。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_TIME_RTC` | `rtc` | CMOS RTC 状态；`rtc uie N` 为 N 秒布防更新进行中中断 |
| 2 | `L1SVC_TIME_HPET` | `hpet` | HPET 存在性、计数器周期、单次/周期能力 |
| 3 | `L1SVC_TIME_PMTIMER` | `pmtimer` | ACPI PM 定时器状态（位宽来自配置键 `pmtimer.width`） |
| 4 | `L1SVC_TIME_TSC` | `tsc` | TSC 状态 + `tsc cal`/`tsc test`（1000 次读取的单调性、2 次 PIT 校准漂移 <= 1000 bp、100 ms 延迟实测 75-125 ms、经 CPUID 0x80000007 EDX[8] 的不变性标志） |

诚实失败锚点：`"oneshot: arm failed (honest FAIL)"`、
`"calibration: FAILED (no samples)"`、`"calibrated: not yet (run 'tsc cal')"`。

### 4.2 block（cat 2）—— services/block

23 个迁移自 v2.2.0 的命令体；`block_svc_init` +
`block_svc_probe_all` 是该家族的 init/late 槽位。状态 op
是被捕获的命令体；安装类 op 写真实磁盘。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_BLK_AHCI` | `ahci` | AHCI 控制器/端口状态 |
| 2 | `L1SVC_BLK_NVME` | `nvme` | NVMe 控制器 + 命名空间状态 |
| 3 | `L1SVC_BLK_ATA` | `ata` | ATA/IDE 驱动器状态（PIO + DMA 支持标记） |
| 4 | `L1SVC_BLK_DSKSTAT` | `dskstat` | 每设备磁盘 I/O 统计 |
| 5 | `L1SVC_BLK_LSBLK` | `lsblk` | 列出块设备（+ 缓存统计） |
| 6 | `L1SVC_BLK_PARTED` | `parted` | 分区表视图（`parted [dev]`） |
| 7 | `L1SVC_BLK_MKFS_FAT32` | `mkfs.fat32` | 创建 FAT32 卷（破坏性） |
| 8 | `L1SVC_BLK_MKFS_EXFAT` | `mkfs.exfat` | 创建 exFAT 卷（破坏性） |
| 9 | `L1SVC_BLK_MKFS_EXT4` | `mkfs.ext4` | 创建 ext4 卷（破坏性） |
| 10 | `L1SVC_BLK_FSCK` | `fsck` | FAT32 一致性检查（`fsck <dev>`） |
| 11 | `L1SVC_BLK_SYNC` | `sync` | 刷写回缓存 + 驱动 FLUSH 到介质 |
| 12 | `L1SVC_BLK_AHCI_TEST` | `ahci_test` | AHCI 端口读测试 |
| 13 | `L1SVC_BLK_NVME_TEST` | `nvme_test` | NVMe 命名空间读测试 |
| 14 | `L1SVC_BLK_ATA_DMA_TEST` | `ata_dma_test` | ATA bus-master DMA 与 PIO 时序对比 |
| 15 | `L1SVC_BLK_VIRTIO_TEST` | `virtio_blk_test` | virtio-blk 请求/应答测试 |
| 16 | `L1SVC_BLK_RW_TEST` | `disk_rw_test` | 原始扇区写/读/比较（`disk_rw_test <dev>`） |
| 17 | `L1SVC_BLK_PART_TEST` | `partition_test` | 分区扫描测试 |
| 18 | `L1SVC_BLK_FS_MOUNT_TEST` | `fs_mount_test` | 格式化 + 挂载往返（会格式化设备！） |
| 19 | `L1SVC_BLK_REAL_HW_TEST` | `real_hw_test` | 真实硬件磁盘 + NIC 普查 |
| 20 | `L1SVC_BLK_ABDISK` | `abdisk` | 创建 A/B 更新磁盘布局（破坏性；随后重跑 `ota_ab_rescan()`） |
| 21 | `L1SVC_BLK_INSTALL` | `install` | 把 Open Cube OS 安装到磁盘 |
| 22 | `L1SVC_BLK_GRUB_INSTALL` | `grub-install` | 写入内嵌 GRUB MBR + core.img 链 |
| 23 | `L1SVC_BLK_ABCFG` | `abcfg` | A/B 启动配置视图 |

全盘硬保护（OCIRE 嵌入激活）：经覆盖恢复邮箱的设备进行
的每一次写都被拒绝；读保持开放；`disk_rw_test hda` 于是
按设计失败。内核内部写方是
`driver_block_read/write_sectors_trusted`（绝不可从 ring 3
调用）。

### 4.3 nic（cat 3）—— services/net（nic 面）

8 个显式 WP-10b 家族（e1000e 8086:10D3/100E/100F/10EA/
15B8、igb 10C9/1521、ixgbe 10FB/1563、rtl8139 10EC:8139、
rtl8168 8168/8161、rtl8125 8125、rtl810x 8136/8137、
bcm57xx 14E4:16xx/17xx/440x）+ "other"（3c59x/nForce/
Atheros AR81xx/Marvell）+ 无线家族
（iwlwifi/rtlwifi/ath/brcm）+ 蓝牙的逐家族状态 + 自检
对。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1-8 | `L1SVC_NIC_E1000E .. BCM57XX` | `e1000e igb ixgbe rtl8139 rtl8168 rtl8125 rtl810x bcm57xx` | 逐驱动网卡状态（寄存器、链路、MAC） |
| 9-16 | `L1SVC_NIC_E1000E_TEST .. BCM57XX_TEST` | 对应的 `*_test` 应用 | 逐驱动自检（环回 + 寄存器探针） |
| 17 | `L1SVC_NIC_OTHER_TEST` | `other_nic_test` | 尽力而为的家族探测测试 |
| 18 | `L1SVC_NIC_RW_TEST` | `nic_rw_test` | NIC 环回帧测试 |
| 19 | `L1SVC_NIC_WIFI` | `wifi` | 无线状态；`wifi scan`（被动信标扫描，QEMU 下诚实的 "no radio"）、`wifi test`（核心 + 各家族自检）、`wifi sdio`（SDHCI 总线状态 + 自检） |
| 20 | `L1SVC_NIC_BT` | `bt` | 蓝牙状态；`bt usb`（HCI-USB）、`bt uart`（H4）、`bt test`（流自检） |

### 4.4 snd（cat 4）—— services/audio

PCM 契约：小端有符号 16 位交错立体声，按设备当前速率；
`play` 接收的是字节数而非采样数。没有中央混音器 —— 音量
按设备设置，由各驱动的混音路径施加。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_SND_SOUND` | `sound` | 音频子系统状态（卡表） |
| 2-7 | `L1SVC_SND_HDA..USBAUDIO` | `hda ac97 sb16 es1370 virtiosnd usbaudio` | 逐驱动卡状态 |
| 8 | `L1SVC_SND_PLAY` | `play` | 播放 WAV 文件或 440 Hz 音调（`play [device] [rate 4000-96000]`） |
| 9 | `L1SVC_SND_VOLUME` | `volume` | 读取/设置音量（`volume [device] [0-100]`） |
| 10-15 | `L1SVC_SND_HDA_TEST..USB_TEST` | 对应的 `*_test` | 逐驱动 500 ms 音调自检（hda/ac97/virtio 在 48000；sb16/es1370 在 44100） |
| 16 | `L1SVC_SND_RW_TEST` | `audio_rw_test` | PCM 写/读环回（48 KB 缓冲区） |
| 17 | `L1SVC_SND_RATE_TEST` | `sample_rate_test` | caps 位图与 set_rate 协商；无卡时诚实跳过；HDA+44100 仅报告 caps 位（QEMU 冻结告警，真实硬件正常） |

### 4.5 power（cat 5）—— services/power

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_PWR_SHUTDOWN` | `shutdown` / `poweroff` | 刷块缓存，经 FADT `\_S5` 走 ACPI S5；回退顺序 QEMU PIIX4 端口 0x604、Bochs 0xB004、APM 0xF000；否则 "Power off not supported. System halted." |
| 2 | `L1SVC_PWR_REBOOT` | `reboot` | 8042 控制器脉冲 + 端口 0xCF9 复位 |
| 3 | `L1SVC_PWR_HALT` | `halt` | 关中断，hlt 循环 |
| 4 | `L1SVC_PWR_SLEEP` | `sleep` | 延迟 N 秒（小数至 3 位，ms）；直连路由（捕获会吞掉实时输出 —— BUG-0346 修复了旧的 suspend 别名） |
| 5 | `L1SVC_PWR_POWEROFF_TEST` | `poweroff_test` | 关机路径探针 |
| 6 | `L1SVC_PWR_SUSPEND_TEST` | `suspend_test` | 真实 ACPI S3 挂起/唤醒往返 |
| 7 | `L1SVC_PWR_HALT_TEST` | `halt_test` | 停机路径探针（诚实 SKIP：不观察活体停机，破坏性） |
| 8 | `L1SVC_PWR_REBOOT_TEST` | `reboot_test` | 重启路径探针（诚实 SKIP） |

### 4.6 ota（cat 6）—— services/selfhost

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_OTA_CHECKUPDATE` | `checkupdate` | 抓取 update.json，比较 version/build |
| 2 | `L1SVC_OTA_CONFIG` | `config` | 对 /etc/opencube.conf 的 `config list\|get <k>\|set <k> <v>\|restore\|path` |
| 3 | `L1SVC_OTA_CONFIG_TEST` | `config_test` | 配置后端单元测试 |
| 4 | `L1SVC_OTA_CHECKUPDATE_TEST` | `checkupdate_test` | 检查路径单元测试 |
| 5 | `L1SVC_OTA_UPDATE` | `update` | 裸 = 在线流程；`status`/`--status`；`--local <path>`/`local <path>` 离线包 |
| 6 | `L1SVC_OTA_ROLLBACK` | `rollback` | 清除启动标志，回到另一槽位 |
| 7 | `L1SVC_OTA_PKG_TEST` | `update_pkg_test` | 包清单/校验和测试 |
| 8 | `L1SVC_OTA_AB_TEST` | `ab_partition_test` | A/B 槽位 + 标志语义测试 |
| 9-16 | `L1SVC_OTA_CHECK_TEST..REAL_TEST` | `update_{check,download,verify,install,rollback,local,status}_test`、`real_update_test` | 逐阶段单元测试 + 完整真实流程 |

### 4.7 fs（cat 7）—— services/fs

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_FS_MOUNT` | `mount` | 无参数：列出挂载；`mount <type> <dev> <path>` 挂载（预创建挂载点） |
| 2 | `L1SVC_FS_MOUNTS` | `mounts` | 列出（别名） |
| 3 | `L1SVC_FS_UMOUNT` | `umount` | 卸载（`umount <path>`）；错误：根拒绝 / 忙 / 先卸嵌套 |
| 4 | `L1SVC_FS_FATMOUNT` | `fatmount` | 挂载 FAT32（`fatmount [dev path]`）；不预创建挂载点 —— 失败时打印诚实的两行消息 |
| 5 | `L1SVC_FS_FATSTAT` | `fatstat` | FAT32 卷统计 |
| 6 | `L1SVC_FS_TREE` | `tree` | 目录树（深度上限 16，深度 >= 12 处 "..."） |

相对路径相对*调用进程的* cwd 解析
（`fspath_resolve_base(proc->cwd, ...)`），内核回退
`fspath_resolve`。

### 4.8 net（cat 8）—— services/net

`ifconfig`/`ping`/`netstat`/`wget` 保持 L0 声明，经 ush
内建 + `SYS_NETCMD` 桥（`net_netcmd_captured`，码 1-4）
到达相同函数体 —— 它们*不是* L1SVC op。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_NET_ARP` | `arp` | ARP 缓存转储（16 项） |
| 2 | `L1SVC_NET_DHCP` | `dhcp` | DISCOVER->OFFER->REQUEST->ACK；打印 `DHCP success: IP=` 锚点 |
| 3 | `L1SVC_NET_DNS` | `dns` | 解析 `<name>`，可选记录选择器 `aaaa\|cname\|mx\|txt\|ns\|srv` |
| 4 | `L1SVC_NET_DNSTEST` | `dnstest` | 解析器电池，含内建向量（10.0.2.2/update.json 夹具） |
| 5 | `L1SVC_NET_FIREWALL` | `firewall` | netfilter 引擎之上的 rule add/del/policy/ct/reset 语法 |
| 6 | `L1SVC_NET_IP` | `ip` | 显示，或设置 `ip <ip> <mask> <gw>` |
| 7 | `L1SVC_NET_LSPCI` | `lspci` | PCI 设备列表（历史上归入 net 标签） |
| 8 | `L1SVC_NET_NF_TEST` | `nf_test` | netfilter 规则/conntrack 测试 |
| 9 | `L1SVC_NET_ROUTE` | `route` | 显示 / `add <dst> <mask> <gw>` / `del <dst> <mask>`（16 项表） |
| 10 | `L1SVC_NET_TCPCC_TEST` | `tcpcc_test` | CUBIC 拥塞控制单元测试 |
| 11 | `L1SVC_NET_TCPSTATS` | `tcpstats` | 逐连接 state/cwnd/ssthresh/mss/rto/rtx_len/dup_acks/ts/cc/sack/fr/srtt/rttvar 转储 |
| 12 | `L1SVC_NET_TCPTEST` | `tcptest` | TCP 环回测试 |
| 13 | `L1SVC_NET_SSH` | `ssh` | SSH-2.0 客户端：`ssh <ip> [port=2222] [user=oc] [password=oc\|"-"]`；`-` = 经 /etc/ssh_client_key 的公钥（缺失时回退密码） |
| 14 | `L1SVC_NET_SSHD` | `sshd` | SSH 服务器（`sshd [port] [user] [pass]`，默认 22，单会话）；直连路由，让 `net_sshd_log` 实时到达控制台 |

### 4.9 health（cat 9）—— services/health

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_HEALTH_SNAPSHOT` | `health` | v2.2.0 风格报告：cpu/内存/任务/磁盘/网络区段 + `alarms:` 行 |
| 2 | `L1SVC_HEALTH_EXPORT` | `health export` | 经 fs_vfs 把 JSON 样本写入 arg（默认 `/var/log/health.json`），随后打印 |
| 3 | `L1SVC_HEALTH_WATCH` | `health watch` | 交互式 1 秒刷新循环，ENTER 退出；直连路由 |

### 4.10 klog（cat 10）—— services/klog

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_KLOG_DMESG` | `dmesg` | 回放环尾（`dmesg [N]`，默认 30，1..1024），页脚 `-- <shown> lines (ring <count>/1024, dropped <n>) --` |
| 2 | `L1SVC_KLOG_LOGCAT` | `logcat` | 过滤回放 + 跟随：`-l lvl -m mod -k key -t sec -n N [-f]`（跟随 = op 内 10 Hz 轮询循环，ENTER 退出） |
| 3 | `L1SVC_KLOG_LOGCTL` | `logctl` | 11 个子命令：`status level enable disable clear flush rotate threshold export filter test` |

完整 klog 内部机制：[../internals/klog.md](../internals/klog.md)。

### 4.11 watchdog（cat 11）—— services/watchdog

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_WDT_CMD` | `watchdog` | 完整语法：`status slots hw status\|start [sec=30]\|stop\|feed\|timeout <2..63> start\|stop\|feed <id> timeout <id> <ms 100..3600000> register <name> <ms> <0=panic 1=reboot 2=kill 3=log> test kill\|panic\|log` |

完整看门狗内部机制：
[../internals/watchdog.md](../internals/watchdog.md)。

### 4.12 crypto（cat 12）—— services/crypto

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_CRYPTO_CRYPTOTEST` | `cryptotest` | NIST 向量自检：SHA-256、AES-128、HMAC-SHA256、HMAC-SHA384（PASS 行） |
| 2 | `L1SVC_CRYPTO_DHTEST` | `dhtest` | DH 模幂金样本 + ECDH/X25519 端到端 + python3-pow() 规模扫描真值向量 |

两者都以捕获方式运行。内部机制：
[../internals/drbg-crypto.md](../internals/drbg-crypto.md)。

### 4.13 l1（cat 13）—— services/l1

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_L1_APPS` | `help` | 从生成的 `OC_USERPROGS[]` 表渲染注册清单；arg `""` 纯 A-Z、`-a` 带 wp 标签的完整清单、`-w` 按工作包分组（19 个规范标签 + 有界的 "other" 桶） |
| 2 | `L1SVC_L1_L1TEST` | `l1test` | 作业 create/list/control/kill 步骤 + `ext_wp8b_selftest()` 14/14 动态链接门禁 |
| 3 | `L1SVC_L1_LDD` | `ldd` | 已注册程序的 PT_INTERP + DT_NEEDED 自省（PIE 安全的 vaddr 翻译） |

### 4.14 display（cat 14）—— services/display

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_DISP_STATS` | `stats` | 显示流水线统计 |
| 2 | `L1SVC_DISP_UNAME` | `uname` | 系统标识（`-a/-s/-r/-m`） |
| 3 | `L1SVC_DISP_CLEAR` | `clear` | 清屏 |
| 4 | `L1SVC_DISP_LANG` | `lang` | 显示/设置控制台语言（`lang [en\|zh]`）；持久化为 `shell.lang` |
| 5 | `L1SVC_DISP_CHINESE` | `chinese` | CJK 控制台演示 |
| 6 | `L1SVC_DISP_CHINESETEST` | `chinesetest` | CJK 字体/编码测试电池 |
| 7 | `L1SVC_DISP_FONT` | `font` | 字体引擎状态 |
| 8 | `L1SVC_DISP_FONT_TEST` | `font-test` | 字体渲染器测试 |
| 9 | `L1SVC_DISP_ENCODING` | `encoding` | 控制台编码状态/设置 |
| 10 | `L1SVC_DISP_ENC_TEST` | `encoding-test` | GB2312/UTF-8 编解码器测试 |
| 11 | `L1SVC_DISP_IME` | `ime` | IME 状态；`ime -i` = 交互式合成行（直连路由） |
| 12 | `L1SVC_DISP_IME_TEST` | `ime-test` | 引擎 + 会话电池（词组/翻页/数字选字协议断言） |
| 13 | `L1SVC_DISP_IME_BEGIN` | （编辑器） | 打开 IME 会话（重置；out 为 ""） |
| 14 | `L1SVC_DISP_IME_KEY` | （编辑器） | 喂入一个十进制键码；out = `"0\n"` 透传 / `"1\n<line>"` 预编辑 / `"2\n<text>"` 提交 / `"3\n<text>"` 提交+透传 |
| 15 | `L1SVC_DISP_IME_END` | （编辑器） | 关闭会话 |
| 16 | `L1SVC_DISP_LANG_GET` | （i18n 运行时） | 恰好输出 `en` 或 `zh`（无换行）—— 每次 `tr_()` 调用都执行的活体区域设置探针 |
| 17 | `L1SVC_DISP_SERIAL_ENC` | `serial_encoding` | 显示/设置 COM1 tee 编码（`utf8\|gbk`） |

动作数字 4 是调用方一侧对服务失败或协议畸形的分类（按键
被吞掉，绝不泄漏进文件）。

### 4.15 input（cat 15）—— services/display（input 面）

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_INP_TOUCHPAD` | `touchpad` | i2c-hid 触摸板状态 |
| 2 | `L1SVC_INP_TOUCHTEST` | `touchtest` | 触摸板手势引擎测试 |

### 4.16 camera（cat 16）—— services/camera

历史 v1 "camera" 命令类：UVC 相机加上遗留端口诊断
（块层之上的 SCSI、gameport、parallel、IR）。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_CAM_UVC` | `uvc` | UVC 相机状态 |
| 2 | `L1SVC_CAM_UVCTEST` | `uvctest` | UVC 捕获测试（30 帧 E2E） |
| 3 | `L1SVC_CAM_SCSI` | `scsi` | SCSI 设备扫描（LSI 53C895A） |
| 4 | `L1SVC_CAM_SCSITEST` | `scsitest` | SCSI 读测试 |
| 5 | `L1SVC_CAM_GAMEPORT` | `gameport` | 游戏端口状态 |
| 6 | `L1SVC_CAM_GAMEPORTTEST` | `gameporttest` | 游戏端口轴/按钮测试 |
| 7 | `L1SVC_CAM_PARALLEL` | `parallel` | 并口状态（SPP 字节校验） |
| 8 | `L1SVC_CAM_PARALLELTEST` | `paralleltest` | 并口环回测试 |
| 9 | `L1SVC_CAM_IR` | `ir` | 红外接收器状态 |
| 10 | `L1SVC_CAM_IRTEST` | `irtest` | 红外编解码测试（NEC/RC5/RC6/SIRC） |

### 4.17 usb（cat 17）—— services/usb

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_USB_STATUS` | `usb` | 主机控制器 + 设备树状态 |
| 2 | `L1SVC_USB_DEV` | `usbdev` | 逐槽位设备细节（`usbdev [slot]`） |
| 3 | `L1SVC_USB_CORE_TEST` | `usb_core_test` | 核心枚举/控制传输测试 |
| 4 | `L1SVC_USB_KBD_TEST` | `usb_kbd_test` | HID 键盘类测试 |
| 5 | `L1SVC_USB_MOUSE_TEST` | `usb_mouse_test` | HID 指针类测试 |
| 6 | `L1SVC_USB_STORAGE_TEST` | `usb_storage_test` | MSC BOT 测试（注册块设备） |
| 7 | `L1SVC_USB_SERIAL_TEST` | `usb_serial_test` | CDC-ACM 串口测试 |
| 8 | `L1SVC_USB_HOTPLUG_TEST` | `usb_hotplug_test` | 插拔处理测试 |
| 9 | `L1SVC_USB_HUB_TEST` | `usb_hub_test` | 外部集线器支持测试 |

内核侧轮询线程（`driver_usb_poll_start`）随 kmain 启动的
模块 init 常驻。

### 4.18 ocire（cat 18）—— services/ocembed（仅限守卫）

编号由 OCIRE 代理 ABI 冻结
（`ocire/agent/ocire_agent.c` 针对 op 4/5 编译）。任何不带
`ocire_guard` 进程标志的调用方都被拒绝：
`"ocire: refused (guard-only channel)"` + 一条 WARN
klog。

| Op | Define | 调用方 | 功能 |
|---|---|---|---|
| 4 | `L1SVC_OCIRE_LOG_APPEND` | 注入的日志代理 | 把 `arg` 文本（至多 4096 B）追加到 OCIRE 卷上的启动日志环；头最后更新以保证崩溃安全顺序 |
| 5 | `L1SVC_OCIRE_KLOG_SNAPSHOT` | 注入的日志代理 | 把内核日志环（从旧到新）渲染进 `out` |

完整嵌入故事：
[../internals/ocembed-guard.md](../internals/ocembed-guard.md)。

### 4.19 diag（cat 19）—— services/diag

39 个 op 分三组：G8 生命体征 + 崩溃家族（1-8）、G9
ring-0 自检（9-26）、G10 芯片组（27-39）。五个 v2.2.0
语义要求内核地址空间为当前空间的 op
（vmmap/pftest/cr3test/vmkernelpt）运行在切换到内核 AS
的包裹内，返回前恢复孵化者 AS。`crashtest` 的
`panic|exc|pf|assert|wd|ovf` 变体会真的让内核崩溃；
`policy`/`dump`/用法是安全的。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_DIAG_MEM` | `mem` | PMM 统计（总/已用/空闲/碎片） |
| 2 | `L1SVC_DIAG_FRAG` | `frag` | PMM 空闲碎片报告 |
| 3 | `L1SVC_DIAG_HEAP` | `heap` | 内核堆统计（池、块） |
| 4 | `L1SVC_DIAG_SCHED` | `sched` | 调度器统计（按状态普查） |
| 5 | `L1SVC_DIAG_SYNCSTAT` | `syncstat` | 同步原语操作计数器 |
| 6 | `L1SVC_DIAG_CRASHLOG` | `crashlog` | 列出 32 槽位崩溃环；`crashlog flush` 追加 /var/log/crash.log |
| 7 | `L1SVC_DIAG_CRASHTEST` | `crashtest` | 刻意故障注入：`panic exc pf assert wd ovf dump policy [halt\|reboot]` |
| 8 | `L1SVC_DIAG_DUMPINFO` | `dumpinfo` | 核心转储缓冲区状态/用量 |
| 9 | `L1SVC_DIAG_CR3TEST` | `cr3test` | CR3 审计（内核 AS 包裹） |
| 10 | `L1SVC_DIAG_EXC` | `exc` | 异常自检报告（与启动计数器共享） |
| 11 | `L1SVC_DIAG_PFTEST` | `pftest` | 缺页处理程序测试（内核 AS 包裹） |
| 12 | `L1SVC_DIAG_PMMRACE` | `pmmrace` | PMM 分配/释放竞态检测器 |
| 13 | `L1SVC_DIAG_VMTEST` | `vmtest` | VMM 单元测试 |
| 14 | `L1SVC_DIAG_VMKERNELPT` | `vmkernelpt` | 内核页表转储（内核 AS 包裹） |
| 15 | `L1SVC_DIAG_VMMAP` | `vmmap` | 地址空间映射转储（内核 AS 包裹） |
| 16 | `L1SVC_DIAG_MEMTEST` | `memtest` | 内存服务电池 |
| 17 | `L1SVC_DIAG_HEAPTEST` | `heaptest` | 堆压力测试（分配/释放/合并） |
| 18 | `L1SVC_DIAG_HEAPBOUNDS` | `heapbounds` | 堆边界条件检查 |
| 19 | `L1SVC_DIAG_IRQABITEST` | `irqabitest` | IRQ 入口 ABI 探针（rsp mod 16、DF=0） |
| 20 | `L1SVC_DIAG_TIMER` | `timer` | 软定时器测试 |
| 21 | `L1SVC_DIAG_MSITEST` | `msitest` | MSI/MSI-X 向量测试 |
| 22 | `L1SVC_DIAG_SPAWN` | `spawn` | 后台孵化演示 |
| 23 | `L1SVC_DIAG_MULTI` | `multi` | 3 个交错任务唤醒测试 |
| 24 | `L1SVC_DIAG_NICE` | `nice` | 优先级演示（`nice <tid> <prio 0..31>`） |
| 25 | `L1SVC_DIAG_SYNCTEST` | `synctest` | 自旋锁/信号量/互斥锁/条件变量测试 |
| 26 | `L1SVC_DIAG_FSTEST` | `fstest` | VFS 压力测试 |
| 27 | `L1SVC_DIAG_ACPI` | `acpi` | ACPI 表摘要 |
| 28 | `L1SVC_DIAG_ACPIDEV` | `acpidev` | ACPI 设备树 |
| 29 | `L1SVC_DIAG_ACPIPOW` | `acpipow` | ACPI 电源状态（S3/S5 对象） |
| 30 | `L1SVC_DIAG_ACPITEST` | `acpitest` | AML 解析器测试 |
| 31 | `L1SVC_DIAG_PCIDUMP` | `pcidump` | 配置空间转储（`pcidump <bus:dev.fn>`） |
| 32 | `L1SVC_DIAG_PCIBARS` | `pcibars` | PCI BAR 分配检查 |
| 33 | `L1SVC_DIAG_PCIE` | `pcie` | PCIe 链路状态 |
| 34 | `L1SVC_DIAG_I2CDETECT` | `i2cdetect` | I2C 总线扫描 |
| 35 | `L1SVC_DIAG_I2CGET` | `i2cget` | 读 I2C 寄存器（`<bus> <addr> [reg]`，十六进制） |
| 36 | `L1SVC_DIAG_I2CSET` | `i2cset` | 写 I2C 寄存器（`<bus> <addr> <reg> <val>`） |
| 37 | `L1SVC_DIAG_I2CTEST` | `i2ctest` | I2C 总线测试 |
| 38 | `L1SVC_DIAG_SMBUS` | `smbus` | SMBus 状态 |
| 39 | `L1SVC_DIAG_SMBUSTEST` | `smbustest` | SMBus 字节/字/块往返 |

### 4.20 tui（cat 20）—— services/tui

全部九个 op 直连路由（实时渲染、实时键盘；op 自身的控制台
输出就是会话，串口 tee 镜像它；`out` 保持为空）。家族
init 槽位 = `tui_ext_init()`（内建主题 + 热键在首个 op
之前预热）。

| Op | Define | 运行名 | 功能 |
|---|---|---|---|
| 1 | `L1SVC_TUI_TUI` | `tui` | 桌面（窗口、菜单、命令栏、文件管理器、进程列表、编辑器、终端） |
| 2 | `L1SVC_TUI_TEST` | `tui-test` | 自检电池（渲染、窗口、输入、IME） |
| 3 | `L1SVC_TUI_HW` | `tui-hw` | 硬件页面集（`[page]`：系统/CPU、PCI、声音、存储、网络...） |
| 4 | `L1SVC_TUI_NET` | `tui-net` | 网络页面 |
| 5 | `L1SVC_TUI_SYSINFO` | `tui-sysinfo` | 系统信息页 + 共享帮助屏 |
| 6 | `L1SVC_TUI_TIME` | `tui-time` | 时钟页面 |
| 7 | `L1SVC_TUI_DRV` | `tui-drv` | 面向驱动的页面（tui-hw 的 v2.2.0 别名，独立 op 编号） |
| 8 | `L1SVC_TUI_SYS` | `tui-sys` | 系统页面（内存/CPU/运行时间） |
| 9 | `L1SVC_TUI_EXT` | `tui-ext` | 扩展页面 |

共享的页面查看热键：Tab 下一页、Up/Down 滚动、`+`/`=`
与 `-` 音量（持久化 `snd.volume`）、F1 帮助、F2 保存配置、
F5 重绘、Esc/F10/q 离开。

## 5. ring-3 消费模式

标准包装体（284 个程序中约 150 个正是这个）：

```c
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_X, L1SVC_X_OP); }
```

`l1svc_main()`（位于 `ocos/interface/l1_api_syscall.h`）：

- 把 `argv[1..]` 以单空格拼成 256 字节参数行（因此多词
  语法与键入时完全一致地到达），
- 以**静态 32 KiB BSS 缓冲区**调用
  `sys_l1svc(cat, op, arg, out, 32768)`（曾经一个 4096
  字节栈缓冲区截断过 `help -a`），
- 打印 `out` 加上缺失的结尾换行；`rc != 0` 时打印
  `"<name>: " tr_("svc_call_failed")`。

交互式应用（向导、TUI、编辑器、`logcat -f`、
`health watch`）在其上叠加 SYS_READLINE/SYS_GETCH；向导
锚点辅助函数（`wiz_find`、`wiz_token_after`）按 1.4 解析
捕获的英文文本。见
[../l1/writing-apps.md](../l1/writing-apps.md) 与
[../l1/wizard-library.md](../l1/wizard-library.md)。

## 6. 分发管线回顾

- **触发**：`SYS_L1SVC` 到达
  `svc_framework_dispatch(cat, op, arg, out, cap)`。
- **处理**：扫描收集表 `s_svcs[64]`（启动时由
  `svc_framework_collect()` 从 `.oc_svcs` 链接节填充，边界
  `__oc_svcs_start/_end` 来自 `linker.ld`）寻找类别字符串
  的精确匹配；匹配到但 `l1_dispatch` 为 NULL 的服务是
  "纯内核"并返回 -1，否则运行该服务的
  `l1_dispatch(op, arg, out, cap)`。
- **影响**：每个类别字符串恰由一个服务拥有（已声明的
  例外："ocire" 由 `services/ocembed/` 实现）；启动顺序
  仍是手工策展的 kmain 组合根序列 —— 收集表只是身份/
  分发面。完整的添加服务流程见
  [writing-services.md](writing-services.md)。
