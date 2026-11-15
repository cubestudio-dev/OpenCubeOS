<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1SVC interface dictionary

The complete, authoritative listing of the L1SVC ring-3 service-call
surface: 20 categories, 208 numbered operations, reached through the
single syscall `SYS_L1SVC` (110). This page merges the kernel-side
numbering header (`ocos/services/sysapi/interface/svc_sysapi_l1svc.h`,
461 lines), the auto-generated ring-3 mirror
(`ocos/interface/l1svc_ops.h`, produced by
`tools/gen_l1svc_ops.py`), and the per-family op tables verified in
the service sources. Structs and defines below are copied verbatim.

Companion pages: the syscall ABI itself is documented in
[syscall-reference.md](syscall-reference.md); how to add a *new*
service or op is [writing-services.md](writing-services.md); the
call path through `core_kcap` is dissected in
[call-chains.md](call-chains.md).

## 1. Protocol ground rules

Every rule below is stated as trigger -> processing -> impact; each
one is load-bearing and verified in source.

### 1.1 The call shape

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

(the ring-3 helper from `ocos/interface/l1_lib.h`; the typed
wrapper `sys_l1svc(cat, op, arg, out, cap)` calls
`syscall5(SYS_L1SVC, cat, op, arg, out, cap)`):

- **Trigger**: a ring-3 program wants one service operation. It
  never links against kernel code; the only door is `int 0x80` with
  `rax=110` and the five arguments in `rdi, rsi, rdx, r10, r8`
  (the 5th argument rides in `r8`, stashed by the entry path into
  `core_syscall_arg5()` before the handler runs).
- **Processing**: `sys_l1svc()` in
  `ocos/services/sysapi/src/svc_sysapi_syscall.c` copies `arg` into a
  kernel buffer, routes `cat` through `L1SVC_CAT_NAME(cat)` to the
  owning service's `l1_dispatch(op, arg, out, cap)`, captures the
  rendered text, and copies it back.
- **Impact**: the caller receives a NUL-terminated plain-ASCII text
  block in `out`; the kernel's return value says only whether the
  call *routed*, never whether the operation "succeeded"
  (see 1.5).

### 1.2 Buffer limits

| Limit | Value | Why |
|---|---|---|
| `cap` (out capacity) | 1..32768 | raised from 8192 in WP06g2 so `help -a` fits the full 284-program bilingual registry listing |
| `arg` length | 256 bytes | NUL-terminated user string copied into a fixed kernel buffer |
| OCIRE `arg` length | 4096 bytes | cat 18 only: the log agent appends up to 4 KiB in one `LOG_APPEND` |
| `out == 0 && cap == 0` | allowed | the OCIRE fire-and-forget spelling: renders into a 256-byte kernel scratch buffer, copy-back skipped (3.0.0-WP03 G4) |

### 1.3 Interrupt state around dispatch

- **Trigger**: an op body may contain device-wait loops (disk
  settle, DHCP timeouts, audio polling) that are tick-driven.
- **Processing**: `sys_l1svc()` executes
  `__asm__ volatile("sti")` before `svc_framework_dispatch(...)` and
  `__asm__ volatile("cli")` after it (the v2.0.0 "group N" fix).
- **Impact**: op bodies can sleep/yield on the scheduler tick and
  still return through the syscall path; without this the first
  long op would freeze the machine instead of the caller.

### 1.4 The text-anchor contract

- **Trigger**: any consumer - a shell script, the mega-app wizards,
  the E2E regression gates, `tools/gen_api_docs.py` - needs to read
  an op's output programmatically.
- **Processing**: every op renders *plain ASCII English text*, line
  oriented, with stable key substrings ("A/B disk: present",
  "New version available.", "DHCP success", "DNS resolution
  failed", "reboot required", "Current version is up to date.",
  "IP:"). `wiz_find()`/`wiz_token_after()` in
  `ocos/interface/wizard_lib.h` parse exactly these anchors; the
  ring-3 side formats nothing.
- **Impact**: service output strings are ABI. Changing a rendered
  sentence breaks wizards and release gates the same way a
  renumbered op would - English output lines are the frozen
  contract (see also the interface-freeze rules in
  [../root/CONTRIBUTING.md](../root/CONTRIBUTING.md)).

### 1.5 Return-code semantics

- **Trigger**: the op completed (or failed) and `sys_l1svc` returns.
- **Processing**: the service's own non-zero exit status is *not*
  propagated as a syscall error. Only an unrouteable category/op
  (or an empty `out` buffer with `rc < 0`) returns `-1`; everything
  else `copy_to_user`s the NUL-terminated text and returns `0`.
- **Impact**: failure reporting lives *inside the text* ("...:
  FAILED (...)", "usage: ..."), which is why the anchors above are
  the contract, and why the ring-3 wrapper
  (`l1svc_main()`) prints `"<name>: " tr_("svc_call_failed")` only
  on `rc != 0` (i.e. only on routing failures).

### 1.6 Numbering discipline

- **Trigger**: a new operation is needed.
- **Processing**: a new `#define L1SVC_<CAT>_<NAME> <next>` is
  appended to `svc_sysapi_l1svc.h`; `tools/gen_l1svc_ops.py`
  regenerates the ring-3 mirror; the ush-side constant block is a
  verbatim copy.
- **Impact**: **renumber only by appending**. Existing numbers are
  frozen ABI: cat 18 ("ocire") is additionally frozen against
  `ocire/agent/ocire_agent.c`, which compiles against the numbers
  as an ABI, not a source dependency.

### 1.7 The three routing styles

How an op's text gets into `out` (decided per op in the family
dispatcher):

1. **captured** - `core_kcap_run_fn(fn, arg, out, cap)`: the op body
   is an old shell-command body that prints via
   `screen_console_puts`; while a capture is active the capture hook
   IS the console hook (one slot), characters are collected into
   `out`, framebuffer draw and the serial tee are suppressed,
   captures stack to 4 levels, overflow is marked never silently
   truncated. One-shot text ops.
2. **direct** - `out[0]0; return fn(arg);`: live console sessions
   (`sshd`, all 9 TUI ops, `ime -i`, `power sleep`); the op's own
   console output is the session, the serial tee mirrors it
   verbatim, `out` stays empty.
3. **buffer-render** - the dispatcher writes into `out` with its own
   `put()` helpers (`l1`, `klog`, `health`, `diag`, `fs`, `time`,
   `watchdog` dispatchers).

A documented field note: `tui-test` was first routed captured, but
its ~4.4 KiB verdict block overflowed the 4096-byte SVC result
buffer used at the time and truncated the final summary, so it went
direct. `power sleep` and `sshd` route direct because running them
captured swallowed every live console line during the park (found
live during an OCIRE-supervised boot).

## 2. The 20 categories

| # | Category string | Owning folder | Ops | Family role |
|---|---|---|---|---|
| 1 | `time` | `services/time` | 4 | clock sources: RTC / HPET / PM timer / TSC |
| 2 | `block` | `services/block` | 23 | block devices, mkfs, partitioning, install |
| 3 | `nic` | `services/net` (nic face) | 20 | per-family NIC status + tests + wifi/bt |
| 4 | `snd` | `services/audio` | 17 | sound cards, playback, volume, tests |
| 5 | `power` | `services/power` | 8 | shutdown/reboot/halt/sleep + path tests |
| 6 | `ota` | `services/selfhost` | 16 | A/B slots, update flow, config store |
| 7 | `fs` | `services/fs` | 6 | mount/umount/FAT32/tree |
| 8 | `net` | `services/net` | 14 | stack diagnostics: arp/dhcp/dns/firewall/ssh |
| 9 | `health` | `services/health` | 3 | snapshot / export / watch |
| 10 | `klog` | `services/klog` | 3 | dmesg / logcat / logctl |
| 11 | `watchdog` | `services/watchdog` | 1 | the whole `watchdog` subcommand grammar |
| 12 | `crypto` | `services/crypto` | 2 | cryptotest / dhtest self-tests |
| 13 | `l1` | `services/l1` | 3 | registry listing, l1test, ldd |
| 14 | `display` | `services/display` | 17 | console/locale/font/IME/serial encoding |
| 15 | `input` | `services/display` (input face) | 2 | touchpad status + test |
| 16 | `camera` | `services/camera` | 10 | UVC + SCSI/gameport/parallel/IR (historical grouping) |
| 17 | `usb` | `services/usb` | 9 | host stack status + class tests |
| 18 | `ocire` | `services/ocembed` | 2 | guard-only log channel (ops 4-5, frozen ABI) |
| 19 | `diag` | `services/diag` | 39 | vitals + ring-0 self-tests + chipset |
| 20 | `tui` | `services/tui` | 9 | full-screen desktop + pages |

Kernel-only families with no category and no ops: `crash`,
`encoding`, `font`, `ipc`, `loader`, `sysapi` (mechanism owners).

## 3. The complete numbering block (verbatim)

The following is the verbatim `#define` set from
`ocos/services/sysapi/interface/svc_sysapi_l1svc.h` (mirrored into
`ocos/interface/l1svc_ops.h`): 20 category defines + 208 op defines
= 228 `L1SVC_*` defines.

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

Op count per category: TIME 4, FS 6, BLOCK 23, NIC 20, SND 17,
OTA 16, POWER 8, DISPLAY 17, INPUT 2, CAMERA 10, USB 9, NET 14,
CRYPTO 2, OCIRE 2 (ops 4-5, guard-only), HEALTH 3, KLOG 3,
WATCHDOG 1, L1 3, DIAG 39, TUI 9 = **208**.

## 4. Per-category operation tables

The "Run as" column is the registered ring-3 program name (the
standard wrapper; see [debug-interfaces.md](debug-interfaces.md) for
the test-stub inventory). "Routing" is the style from 1.7.

### 4.1 time (cat 1) - services/time

Bring-up stays in kmain (order-sensitive: RTC before HPET before the
calibrated TSC read used by boot-log timestamps). All ops
buffer-render status text.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_TIME_RTC` | `rtc` | CMOS RTC status; `rtc uie N` arms the update-in-progress interrupt for N seconds |
| 2 | `L1SVC_TIME_HPET` | `hpet` | HPET presence, counter period, one-shot/periodic capability |
| 3 | `L1SVC_TIME_PMTIMER` | `pmtimer` | ACPI PM timer status (width from config key `pmtimer.width`) |
| 4 | `L1SVC_TIME_TSC` | `tsc` | TSC status + `tsc cal`/`tsc test` (monotonicity over 1000 reads, 2x PIT calibration drift <= 1000 bp, 100 ms delay measured 75-125 ms, invariant flag via CPUID 0x80000007 EDX[8]) |

Honest-failure anchors: `"oneshot: arm failed (honest FAIL)"`,
`"calibration: FAILED (no samples)"`, `"calibrated: not yet (run 'tsc cal')"`.

### 4.2 block (cat 2) - services/block

23 migrated v2.2.0 command bodies; `block_svc_init` +
`block_svc_probe_all` are the family's init/late slots. Status ops
are captured command bodies; setup ops write real disks.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_BLK_AHCI` | `ahci` | AHCI controller/port status |
| 2 | `L1SVC_BLK_NVME` | `nvme` | NVMe controller + namespace status |
| 3 | `L1SVC_BLK_ATA` | `ata` | ATA/IDE drive status (PIO + DMA-backed marker) |
| 4 | `L1SVC_BLK_DSKSTAT` | `dskstat` | per-device disk I/O statistics |
| 5 | `L1SVC_BLK_LSBLK` | `lsblk` | list block devices (+ cache stats) |
| 6 | `L1SVC_BLK_PARTED` | `parted` | partition table view (`parted [dev]`) |
| 7 | `L1SVC_BLK_MKFS_FAT32` | `mkfs.fat32` | create a FAT32 volume (destructive) |
| 8 | `L1SVC_BLK_MKFS_EXFAT` | `mkfs.exfat` | create an exFAT volume (destructive) |
| 9 | `L1SVC_BLK_MKFS_EXT4` | `mkfs.ext4` | create an ext4 volume (destructive) |
| 10 | `L1SVC_BLK_FSCK` | `fsck` | FAT32 consistency check (`fsck <dev>`) |
| 11 | `L1SVC_BLK_SYNC` | `sync` | flush write-back cache + driver FLUSH to media |
| 12 | `L1SVC_BLK_AHCI_TEST` | `ahci_test` | AHCI port read test |
| 13 | `L1SVC_BLK_NVME_TEST` | `nvme_test` | NVMe namespace read test |
| 14 | `L1SVC_BLK_ATA_DMA_TEST` | `ata_dma_test` | ATA bus-master DMA vs PIO timing |
| 15 | `L1SVC_BLK_VIRTIO_TEST` | `virtio_blk_test` | virtio-blk request/reply test |
| 16 | `L1SVC_BLK_RW_TEST` | `disk_rw_test` | raw sector write/read/compare (`disk_rw_test <dev>`) |
| 17 | `L1SVC_BLK_PART_TEST` | `partition_test` | partition scan test |
| 18 | `L1SVC_BLK_FS_MOUNT_TEST` | `fs_mount_test` | format + mount round-trip (formats the device!) |
| 19 | `L1SVC_BLK_REAL_HW_TEST` | `real_hw_test` | real-hardware disk + NIC survey |
| 20 | `L1SVC_BLK_ABDISK` | `abdisk` | create the A/B update disk layout (destructive; re-runs `ota_ab_rescan()`) |
| 21 | `L1SVC_BLK_INSTALL` | `install` | install Open Cube OS onto a disk |
| 22 | `L1SVC_BLK_GRUB_INSTALL` | `grub-install` | write the embedded GRUB MBR + core.img chain |
| 23 | `L1SVC_BLK_ABCFG` | `abcfg` | A/B boot configuration view |

Whole-disk hard protection (OCIRE embed active): every write
through a device covering the recovery mailbox is REFUSED; reads
stay open; `disk_rw_test hda` then fails by design. The
kernel-internal writer is `driver_block_read/write_sectors_trusted`
(never callable from ring 3).

### 4.3 nic (cat 3) - services/net (nic face)

Per-family status + selftest pairs for the 8 explicit WP-10b
families (e1000e 8086:10D3/100E/100F/10EA/15B8, igb 10C9/1521,
ixgbe 10FB/1563, rtl8139 10EC:8139, rtl8168 8168/8161, rtl8125
8125, rtl810x 8136/8137, bcm57xx 14E4:16xx/17xx/440x) + "other"
(3c59x/nForce/Atheros AR81xx/Marvell) + wireless families
(iwlwifi/rtlwifi/ath/brcm) + Bluetooth.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1-8 | `L1SVC_NIC_E1000E .. BCM57XX` | `e1000e igb ixgbe rtl8139 rtl8168 rtl8125 rtl810x bcm57xx` | per-driver card status (registers, link, MAC) |
| 9-16 | `L1SVC_NIC_E1000E_TEST .. BCM57XX_TEST` | matching `*_test` apps | per-driver selftests (loopback + register probes) |
| 17 | `L1SVC_NIC_OTHER_TEST` | `other_nic_test` | best-effort family detect test |
| 18 | `L1SVC_NIC_RW_TEST` | `nic_rw_test` | NIC loopback frame test |
| 19 | `L1SVC_NIC_WIFI` | `wifi` | wireless status; `wifi scan` (passive beacon scan, honest "no radio" under QEMU), `wifi test` (core + per-family selftest), `wifi sdio` (SDHCI bus state + selftest) |
| 20 | `L1SVC_NIC_BT` | `bt` | Bluetooth status; `bt usb` (HCI-USB), `bt uart` (H4), `bt test` (flow selftest) |

### 4.4 snd (cat 4) - services/audio

PCM contract: little-endian signed 16-bit interleaved stereo at the
device's current rate; `play` takes BYTES, not samples. No central
mixer - volume is per-device, applied by each driver's mixer path.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_SND_SOUND` | `sound` | audio subsystem status (card table) |
| 2-7 | `L1SVC_SND_HDA..USBAUDIO` | `hda ac97 sb16 es1370 virtiosnd usbaudio` | per-driver card status |
| 8 | `L1SVC_SND_PLAY` | `play` | play a WAV file or a 440 Hz tone (`play [device] [rate 4000-96000]`) |
| 9 | `L1SVC_SND_VOLUME` | `volume` | get/set volume (`volume [device] [0-100]`) |
| 10-15 | `L1SVC_SND_HDA_TEST..USB_TEST` | matching `*_test` | per-driver 500 ms tone selftests (hda/ac97/virtio at 48000; sb16/es1370 at 44100) |
| 16 | `L1SVC_SND_RW_TEST` | `audio_rw_test` | PCM write/read loopback (48 KB buffer) |
| 17 | `L1SVC_SND_RATE_TEST` | `sample_rate_test` | caps-bitmap vs set_rate negotiation; honest skip when no cards; HDA+44100 reports the caps bit only (QEMU freeze caveat, real hardware fine) |

### 4.5 power (cat 5) - services/power

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_PWR_SHUTDOWN` | `shutdown` / `poweroff` | flush block cache, ACPI S5 via FADT `\_S5`; fallbacks QEMU PIIX4 port 0x604, Bochs 0xB004, APM 0xF000; else "Power off not supported. System halted." |
| 2 | `L1SVC_PWR_REBOOT` | `reboot` | 8042 controller pulse + port 0xCF9 reset |
| 3 | `L1SVC_PWR_HALT` | `halt` | interrupts off, hlt loop |
| 4 | `L1SVC_PWR_SLEEP` | `sleep` | DELAY N seconds (fractional to 3 digits, ms); routes DIRECT (captured would swallow live output - BUG-0346 fixed the old suspend alias) |
| 5 | `L1SVC_PWR_POWEROFF_TEST` | `poweroff_test` | power-off path probe |
| 6 | `L1SVC_PWR_SUSPEND_TEST` | `suspend_test` | real ACPI S3 suspend/resume round trip |
| 7 | `L1SVC_PWR_HALT_TEST` | `halt_test` | halt path probe (honest SKIP: live halt not observed, destructive) |
| 8 | `L1SVC_PWR_REBOOT_TEST` | `reboot_test` | reboot path probe (honest SKIP) |

### 4.6 ota (cat 6) - services/selfhost

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_OTA_CHECKUPDATE` | `checkupdate` | fetch update.json, compare version/build |
| 2 | `L1SVC_OTA_CONFIG` | `config` | `config list\|get <k>\|set <k> <v>\|restore\|path` over /etc/opencube.conf |
| 3 | `L1SVC_OTA_CONFIG_TEST` | `config_test` | config backend unit test |
| 4 | `L1SVC_OTA_CHECKUPDATE_TEST` | `checkupdate_test` | check-path unit test |
| 5 | `L1SVC_OTA_UPDATE` | `update` | bare = online flow; `status`/`--status`; `--local <path>`/`local <path>` offline package |
| 6 | `L1SVC_OTA_ROLLBACK` | `rollback` | clear boot flags, return to the other slot |
| 7 | `L1SVC_OTA_PKG_TEST` | `update_pkg_test` | package manifest/checksum test |
| 8 | `L1SVC_OTA_AB_TEST` | `ab_partition_test` | A/B slot + flag semantics test |
| 9-16 | `L1SVC_OTA_CHECK_TEST..REAL_TEST` | `update_{check,download,verify,install,rollback,local,status}_test`, `real_update_test` | per-stage unit tests + full real flow |

### 4.7 fs (cat 7) - services/fs

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_FS_MOUNT` | `mount` | no args: list mounts; `mount <type> <dev> <path>` mounts (pre-creates the mountpoint) |
| 2 | `L1SVC_FS_MOUNTS` | `mounts` | list (alias) |
| 3 | `L1SVC_FS_UMOUNT` | `umount` | unmount (`umount <path>`); errors: root refusal / busy / nested-first |
| 4 | `L1SVC_FS_FATMOUNT` | `fatmount` | mount FAT32 (`fatmount [dev path]`); does NOT pre-create the mountpoint - failure prints the honest two-line message |
| 5 | `L1SVC_FS_FATSTAT` | `fatstat` | FAT32 volume statistics |
| 6 | `L1SVC_FS_TREE` | `tree` | directory tree (depth cap 16, "..." at depth >= 12) |

Relative paths resolve against the *calling process's* cwd
(`fspath_resolve_base(proc->cwd, ...)`), kernel fallback
`fspath_resolve`.

### 4.8 net (cat 8) - services/net

`ifconfig`/`ping`/`netstat`/`wget` stay L0-declared and reach the
same bodies through ush built-ins + the `SYS_NETCMD` bridge
(`net_netcmd_captured`, codes 1-4) - they are *not* L1SVC ops.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_NET_ARP` | `arp` | ARP cache dump (16 entries) |
| 2 | `L1SVC_NET_DHCP` | `dhcp` | DISCOVER->OFFER->REQUEST->ACK; prints the `DHCP success: IP=` anchor |
| 3 | `L1SVC_NET_DNS` | `dns` | resolve `<name>` with optional record selector `aaaa\|cname\|mx\|txt\|ns\|srv` |
| 4 | `L1SVC_NET_DNSTEST` | `dnstest` | resolver battery incl. built-in vectors (10.0.2.2/update.json fixture) |
| 5 | `L1SVC_NET_FIREWALL` | `firewall` | rule add/del/policy/ct/reset grammar over the netfilter engine |
| 6 | `L1SVC_NET_IP` | `ip` | show, or set `ip <ip> <mask> <gw>` |
| 7 | `L1SVC_NET_LSPCI` | `lspci` | PCI device list (historically net-tagged) |
| 8 | `L1SVC_NET_NF_TEST` | `nf_test` | netfilter rules/conntrack test |
| 9 | `L1SVC_NET_ROUTE` | `route` | show / `add <dst> <mask> <gw>` / `del <dst> <mask>` (16-entry table) |
| 10 | `L1SVC_NET_TCPCC_TEST` | `tcpcc_test` | CUBIC congestion-control unit test |
| 11 | `L1SVC_NET_TCPSTATS` | `tcpstats` | per-connection state/cwnd/ssthresh/mss/rto/rtx_len/dup_acks/ts/cc/sack/fr/srtt/rttvar dump |
| 12 | `L1SVC_NET_TCPTEST` | `tcptest` | TCP loopback test |
| 13 | `L1SVC_NET_SSH` | `ssh` | SSH-2.0 client: `ssh <ip> [port=2222] [user=oc] [password=oc\|"-"]`; `-` = publickey via /etc/ssh_client_key (password fallback if absent) |
| 14 | `L1SVC_NET_SSHD` | `sshd` | SSH server (`sshd [port] [user] [pass]`, default 22, one session); routes DIRECT so `net_sshd_log` reaches the console live |

### 4.9 health (cat 9) - services/health

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_HEALTH_SNAPSHOT` | `health` | v2.2.0-style report: cpu/memory/tasks/disk/network sections + `alarms:` line |
| 2 | `L1SVC_HEALTH_EXPORT` | `health export` | JSON sample written via fs_vfs to arg (default `/var/log/health.json`), then printed |
| 3 | `L1SVC_HEALTH_WATCH` | `health watch` | interactive 1 s refresh loop, ENTER exits; routes DIRECT |

### 4.10 klog (cat 10) - services/klog

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_KLOG_DMESG` | `dmesg` | replay ring tail (`dmesg [N]`, default 30, 1..1024), footer `-- <shown> lines (ring <count>/1024, dropped <n>) --` |
| 2 | `L1SVC_KLOG_LOGCAT` | `logcat` | filtered replay + follow: `-l lvl -m mod -k key -t sec -n N [-f]` (follow = in-op 10 Hz poll loop, ENTER exits) |
| 3 | `L1SVC_KLOG_LOGCTL` | `logctl` | 11 subcommands: `status level enable disable clear flush rotate threshold export filter test` |

Full klog internals: [../internals/klog.md](../internals/klog.md).

### 4.11 watchdog (cat 11) - services/watchdog

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_WDT_CMD` | `watchdog` | the whole grammar: `status slots hw status\|start [sec=30]\|stop\|feed\|timeout <2..63> start\|stop\|feed <id> timeout <id> <ms 100..3600000> register <name> <ms> <0=panic 1=reboot 2=kill 3=log> test kill\|panic\|log` |

Full watchdog internals:
[../internals/watchdog.md](../internals/watchdog.md).

### 4.12 crypto (cat 12) - services/crypto

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_CRYPTO_CRYPTOTEST` | `cryptotest` | NIST-vector self-test: SHA-256, AES-128, HMAC-SHA256, HMAC-SHA384 (PASS lines) |
| 2 | `L1SVC_CRYPTO_DHTEST` | `dhtest` | DH modexp goldens + ECDH/X25519 end-to-end + python3-pow() scale-sweep truth vectors |

Both run captured. Internals:
[../internals/drbg-crypto.md](../internals/drbg-crypto.md).

### 4.13 l1 (cat 13) - services/l1

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_L1_APPS` | `help` | registry listing rendered from the generated `OC_USERPROGS[]` table; arg `""` plain A-Z, `-a` full with wp tags, `-w` grouped by work package (19 canonical tags + bounded "other" bucket) |
| 2 | `L1SVC_L1_L1TEST` | `l1test` | job create/list/control/kill steps + `ext_wp8b_selftest()` 14/14 dynamic-link gate |
| 3 | `L1SVC_L1_LDD` | `ldd` | PT_INTERP + DT_NEEDED introspection of a registered program (PIE-safe vaddr translation) |

### 4.14 display (cat 14) - services/display

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_DISP_STATS` | `stats` | display pipeline statistics |
| 2 | `L1SVC_DISP_UNAME` | `uname` | system identification (`-a/-s/-r/-m`) |
| 3 | `L1SVC_DISP_CLEAR` | `clear` | clear the screen |
| 4 | `L1SVC_DISP_LANG` | `lang` | show/set console language (`lang [en\|zh]`); persists as `shell.lang` |
| 5 | `L1SVC_DISP_CHINESE` | `chinese` | CJK console demo |
| 6 | `L1SVC_DISP_CHINESETEST` | `chinesetest` | CJK font/encoding test battery |
| 7 | `L1SVC_DISP_FONT` | `font` | font engine status |
| 8 | `L1SVC_DISP_FONT_TEST` | `font-test` | font renderer test |
| 9 | `L1SVC_DISP_ENCODING` | `encoding` | console encoding status/set |
| 10 | `L1SVC_DISP_ENC_TEST` | `encoding-test` | GB2312/UTF-8 codec test |
| 11 | `L1SVC_DISP_IME` | `ime` | IME status; `ime -i` = interactive compose line (routes DIRECT) |
| 12 | `L1SVC_DISP_IME_TEST` | `ime-test` | engine + session battery (phrase/paging/digit-pick protocol assertions) |
| 13 | `L1SVC_DISP_IME_BEGIN` | (editors) | open an IME session (reset; out "") |
| 14 | `L1SVC_DISP_IME_KEY` | (editors) | feed one decimal key code; out = `"0\n"` passthrough / `"1\n<line>"` preedit / `"2\n<text>"` commit / `"3\n<text>"` commit+passthrough |
| 15 | `L1SVC_DISP_IME_END` | (editors) | close the session |
| 16 | `L1SVC_DISP_LANG_GET` | (i18n runtime) | emits exactly `en` or `zh` (no newline) - the live-locale probe every `tr_()` call performs |
| 17 | `L1SVC_DISP_SERIAL_ENC` | `serial_encoding` | show/set the COM1 tee encoding (`utf8\|gbk`) |

Action digit 4 is the caller-side classification of service failure
or malformed protocol (key swallowed, never leaked into the file).

### 4.15 input (cat 15) - services/display (input face)

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_INP_TOUCHPAD` | `touchpad` | i2c-hid touchpad status |
| 2 | `L1SVC_INP_TOUCHTEST` | `touchtest` | touchpad gesture-engine test |

### 4.16 camera (cat 16) - services/camera

Historical v1 "camera" command class: UVC camera plus the legacy
port diagnostics (SCSI over the block layer, gameport, parallel,
IR).

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_CAM_UVC` | `uvc` | UVC camera status |
| 2 | `L1SVC_CAM_UVCTEST` | `uvctest` | UVC capture test (30-frame E2E) |
| 3 | `L1SVC_CAM_SCSI` | `scsi` | SCSI device scan (LSI 53C895A) |
| 4 | `L1SVC_CAM_SCSITEST` | `scsitest` | SCSI read test |
| 5 | `L1SVC_CAM_GAMEPORT` | `gameport` | game port status |
| 6 | `L1SVC_CAM_GAMEPORTTEST` | `gameporttest` | game port axis/button test |
| 7 | `L1SVC_CAM_PARALLEL` | `parallel` | parallel port status (SPP byte verification) |
| 8 | `L1SVC_CAM_PARALLELTEST` | `paralleltest` | parallel port loop test |
| 9 | `L1SVC_CAM_IR` | `ir` | IR receiver status |
| 10 | `L1SVC_CAM_IRTEST` | `irtest` | IR codec test (NEC/RC5/RC6/SIRC) |

### 4.17 usb (cat 17) - services/usb

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_USB_STATUS` | `usb` | host-controller + device tree status |
| 2 | `L1SVC_USB_DEV` | `usbdev` | per-slot device detail (`usbdev [slot]`) |
| 3 | `L1SVC_USB_CORE_TEST` | `usb_core_test` | core enumeration/control-transfer test |
| 4 | `L1SVC_USB_KBD_TEST` | `usb_kbd_test` | HID keyboard class test |
| 5 | `L1SVC_USB_MOUSE_TEST` | `usb_mouse_test` | HID pointer class test |
| 6 | `L1SVC_USB_STORAGE_TEST` | `usb_storage_test` | MSC BOT test (registers block devices) |
| 7 | `L1SVC_USB_SERIAL_TEST` | `usb_serial_test` | CDC-ACM serial test |
| 8 | `L1SVC_USB_HOTPLUG_TEST` | `usb_hotplug_test` | connect/disconnect handling test |
| 9 | `L1SVC_USB_HUB_TEST` | `usb_hub_test` | external hub support test |

The kernel-side poll thread (`driver_usb_poll_start`) stays with the
module init started in kmain.

### 4.18 ocire (cat 18) - services/ocembed (guard-only)

Numbering frozen by the OCIRE agent ABI
(`ocire/agent/ocire_agent.c` compiles against ops 4/5). Any caller
without the `ocire_guard` process flag is refused:
`"ocire: refused (guard-only channel)"` + a WARN klog.

| Op | Define | Caller | Function |
|---|---|---|---|
| 4 | `L1SVC_OCIRE_LOG_APPEND` | the injected log agent | append `arg` text (up to 4096 B) to the boot-log ring on the OCIRE volume; header updated LAST for crash-safe ordering |
| 5 | `L1SVC_OCIRE_KLOG_SNAPSHOT` | the injected log agent | render the kernel log ring (oldest to newest) into `out` |

Full embed story: [../internals/ocembed-guard.md](../internals/ocembed-guard.md).

### 4.19 diag (cat 19) - services/diag

39 ops in three groups: G8 vitals + crash family (1-8), G9 ring-0
self-tests (9-26), G10 chipset (27-39). Five ops whose v2.2.0
semantics required the kernel address space current
(vmmap/pftest/cr3test/vmkernelpt) run inside a
switch-to-kernel-AS wrap, restoring the spawner AS before return.
`crashtest`'s `panic|exc|pf|assert|wd|ovf` variants REALLY crash
the kernel; `policy`/`dump`/usage are safe.

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_DIAG_MEM` | `mem` | PMM stats (total/used/free/fragments) |
| 2 | `L1SVC_DIAG_FRAG` | `frag` | PMM free-fragment report |
| 3 | `L1SVC_DIAG_HEAP` | `heap` | kernel heap stats (pools, blocks) |
| 4 | `L1SVC_DIAG_SCHED` | `sched` | scheduler stats (per-state census) |
| 5 | `L1SVC_DIAG_SYNCSTAT` | `syncstat` | sync primitive op counters |
| 6 | `L1SVC_DIAG_CRASHLOG` | `crashlog` | list the 32-slot crash ring; `crashlog flush` appends /var/log/crash.log |
| 7 | `L1SVC_DIAG_CRASHTEST` | `crashtest` | deliberate fault injection: `panic exc pf assert wd ovf dump policy [halt\|reboot]` |
| 8 | `L1SVC_DIAG_DUMPINFO` | `dumpinfo` | core-dump buffer status/usage |
| 9 | `L1SVC_DIAG_CR3TEST` | `cr3test` | CR3 audit (kernel-AS wrap) |
| 10 | `L1SVC_DIAG_EXC` | `exc` | exception self-test report (shares the boot counter) |
| 11 | `L1SVC_DIAG_PFTEST` | `pftest` | page-fault handler test (kernel-AS wrap) |
| 12 | `L1SVC_DIAG_PMMRACE` | `pmmrace` | PMM alloc/free race detector |
| 13 | `L1SVC_DIAG_VMTEST` | `vmtest` | VMM unit test |
| 14 | `L1SVC_DIAG_VMKERNELPT` | `vmkernelpt` | kernel page-table dump (kernel-AS wrap) |
| 15 | `L1SVC_DIAG_VMMAP` | `vmmap` | address-space map dump (kernel-AS wrap) |
| 16 | `L1SVC_DIAG_MEMTEST` | `memtest` | memory service battery |
| 17 | `L1SVC_DIAG_HEAPTEST` | `heaptest` | heap stress test (alloc/free/coalesce) |
| 18 | `L1SVC_DIAG_HEAPBOUNDS` | `heapbounds` | heap boundary-condition checks |
| 19 | `L1SVC_DIAG_IRQABITEST` | `irqabitest` | IRQ-entry ABI probe (rsp mod 16, DF=0) |
| 20 | `L1SVC_DIAG_TIMER` | `timer` | soft-timer test |
| 21 | `L1SVC_DIAG_MSITEST` | `msitest` | MSI/MSI-X vector test |
| 22 | `L1SVC_DIAG_SPAWN` | `spawn` | background spawn demo |
| 23 | `L1SVC_DIAG_MULTI` | `multi` | 3 interleaved task wake test |
| 24 | `L1SVC_DIAG_NICE` | `nice` | priority demo (`nice <tid> <prio 0..31>`) |
| 25 | `L1SVC_DIAG_SYNCTEST` | `synctest` | spinlock/sem/mutex/cond test |
| 26 | `L1SVC_DIAG_FSTEST` | `fstest` | VFS stress test |
| 27 | `L1SVC_DIAG_ACPI` | `acpi` | ACPI table summary |
| 28 | `L1SVC_DIAG_ACPIDEV` | `acpidev` | ACPI device tree |
| 29 | `L1SVC_DIAG_ACPIPOW` | `acpipow` | ACPI power states (S3/S5 objects) |
| 30 | `L1SVC_DIAG_ACPITEST` | `acpitest` | AML parser test |
| 31 | `L1SVC_DIAG_PCIDUMP` | `pcidump` | config-space dump (`pcidump <bus:dev.fn>`) |
| 32 | `L1SVC_DIAG_PCIBARS` | `pcibars` | PCI BAR assignment check |
| 33 | `L1SVC_DIAG_PCIE` | `pcie` | PCIe link status |
| 34 | `L1SVC_DIAG_I2CDETECT` | `i2cdetect` | I2C bus scan |
| 35 | `L1SVC_DIAG_I2CGET` | `i2cget` | read an I2C register (`<bus> <addr> [reg]`, hex) |
| 36 | `L1SVC_DIAG_I2CSET` | `i2cset` | write an I2C register (`<bus> <addr> <reg> <val>`) |
| 37 | `L1SVC_DIAG_I2CTEST` | `i2ctest` | I2C bus test |
| 38 | `L1SVC_DIAG_SMBUS` | `smbus` | SMBus status |
| 39 | `L1SVC_DIAG_SMBUSTEST` | `smbustest` | SMBus byte/word/block round-trips |

### 4.20 tui (cat 20) - services/tui

All nine ops route DIRECT (live rendering, live keyboard; the op's
own console output is the session, serial tee mirrors it; `out`
stays empty). Family init slot = `tui_ext_init()` (builtin themes +
hotkeys warm before the first op).

| Op | Define | Run as | Function |
|---|---|---|---|
| 1 | `L1SVC_TUI_TUI` | `tui` | the desktop (windows, menus, command bar, file manager, process list, editor, terminal) |
| 2 | `L1SVC_TUI_TEST` | `tui-test` | selftest battery (render, window, input, IME) |
| 3 | `L1SVC_TUI_HW` | `tui-hw` | hardware page set (`[page]`: system/CPU, PCI, sound, storage, network...) |
| 4 | `L1SVC_TUI_NET` | `tui-net` | network page |
| 5 | `L1SVC_TUI_SYSINFO` | `tui-sysinfo` | sysinfo pages + shared help screen |
| 6 | `L1SVC_TUI_TIME` | `tui-time` | clock page |
| 7 | `L1SVC_TUI_DRV` | `tui-drv` | driver-oriented page (v2.2.0 alias of tui-hw, own op number) |
| 8 | `L1SVC_TUI_SYS` | `tui-sys` | system page (memory/cpu/uptime) |
| 9 | `L1SVC_TUI_EXT` | `tui-ext` | extensions page |

Shared page-view hotkeys: Tab next page, Up/Down scroll, `+`/`=`
and `-` volume (persists `snd.volume`), F1 help, F2 save config,
F5 repaint, Esc/F10/q leave.

## 5. Ring-3 consumption patterns

The standard wrapper body (about 150 of the 284 programs are
exactly this):

```c
#include "l1_lib.h"
#include "l1svc_ops.h"
int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_X, L1SVC_X_OP); }
```

`l1svc_main()` (in `ocos/interface/l1_lib.h`):

- joins `argv[1..]` with single spaces into a 256-byte argument
  line (so multi-word grammar arrives exactly as typed),
- calls `sys_l1svc(cat, op, arg, out, 32768)` with a **static
  32 KiB BSS buffer** (a 4096-byte stack buffer once truncated
  `help -a`),
- prints `out` plus the missing trailing newline; on `rc != 0`
  prints `"<name>: " tr_("svc_call_failed")`.

Interactive apps (wizards, TUI, editors, `logcat -f`, `health watch`) layer SYS_READLINE/SYS_GETCH on top; the wizard anchor
helpers (`wiz_find`, `wiz_token_after`) parse the captured English
text per 1.4. See [../l1/writing-apps.md](../l1/writing-apps.md)
and [../l1/wizard-library.md](../l1/wizard-library.md).

## 6. Dispatch plumbing recap

- **Trigger**: `SYS_L1SVC` reaches `svc_framework_dispatch(cat, op, arg, out, cap)`.
- **Processing**: the collector table `s_svcs[64]` (filled at boot
  by `svc_framework_collect()` from the `.oc_svcs` linker section,
  boundaries `__oc_svcs_start/_end` from `linker.ld`) is scanned
  for an exact category-string match; a matched service with a NULL
  `l1_dispatch` is "kernel-only" and returns -1, otherwise the
  service's `l1_dispatch(op, arg, out, cap)` runs.
- **Impact**: exactly one service owns each category string (the
  declared exception: "ocire" is implemented by `services/ocembed/`);
  the boot ORDER stays the hand-curated kmain composition-root
  sequence - the collected table is only the identity/dispatch
  surface. See [writing-services.md](writing-services.md) for the
  full add-a-service walkthrough.
