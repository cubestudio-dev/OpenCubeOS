#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> -*-
"""tools/apply_i18n_hints.py - write the _hint pair into every
program's i18n/en.json + i18n/zh.json (the registry hint help -a
renders by locale).  The dictionary below is hand-authored for all
274 programs; run from the repo root."""
import json
import os

H = {
 # ---- shell / help ----
 "ush": ("the Open Cube OS ring-3 shell (default entry)", "Open Cube OS 的 ring-3 shell（默认入口）"),
 "help": ("show shell built-ins and the program registry", "显示 shell 内建命令与程序注册表"),
 "help_a_test": ("registry A-Z listing invariant test", "注册表 A-Z 列表不变量测试"),
 "help_default_test": ("registry default listing test", "注册表默认列表测试"),
 "help_wp_test": ("registry work-package grouping test", "注册表工作包分组测试"),
 "ldd": ("print a program's dynamic dependencies", "显示程序的动态依赖"),
 "l1test": ("L1 job/IPC/dynamic-link self-test", "L1 作业/IPC/动态链接自检"),
 "run": ("run a program with arguments", "带参数运行程序"),
 "echo": ("print the arguments", "打印参数"),
 "clear": ("clear the screen", "清屏"),
 "lang": ("show/set the console language (en/zh)", "显示/设置控制台语言（en/zh）"),
 # ---- file tools ----
 "ls": ("list directory contents", "列出目录内容"),
 "cat": ("print file contents", "打印文件内容"),
 "cp": ("copy files", "复制文件"),
 "mv": ("move/rename files", "移动/重命名文件"),
 "rm": ("remove files", "删除文件"),
 "mkdir": ("create a directory", "创建目录"),
 "rmdir": ("remove an empty directory", "删除空目录"),
 "pwd": ("print the working directory", "显示当前目录"),
 "ln": ("create links", "创建链接"),
 "chmod": ("change file permissions", "修改文件权限"),
 "chown": ("change file owner", "修改文件属主"),
 "touch": ("create an empty file / update mtime", "创建空文件/更新时间戳"),
 "df": ("report filesystem free space", "报告文件系统剩余空间"),
 "du": ("report file space usage", "报告文件占用空间"),
 "stat": ("show file metadata", "显示文件元数据"),
 "grep": ("search text in files", "在文件中搜索文本"),
 "head": ("show the first lines of a file", "显示文件开头几行"),
 "tail": ("show the last lines of a file", "显示文件末尾几行"),
 "awk": ("pattern scanning and processing", "模式扫描与文本处理"),
 "sed": ("stream editor", "流编辑器"),
 "sort": ("sort lines", "排序行"),
 "uniq": ("drop repeated adjacent lines", "去除相邻重复行"),
 "wc": ("count lines/words/bytes", "统计行数/单词/字节"),
 "tree": ("show a directory tree", "显示目录树"),
 "mount": ("mount a filesystem / list mounts", "挂载文件系统/列出挂载"),
 "mounts": ("list mounted filesystems", "列出已挂载文件系统"),
 "umount": ("unmount a filesystem", "卸载文件系统"),
 "fatmount": ("mount a FAT32 volume", "挂载 FAT32 卷"),
 "fatstat": ("FAT32 volume statistics", "FAT32 卷统计"),
 "fsck": ("check a filesystem", "检查文件系统"),
 "write": ("write stdin to a file", "把标准输入写入文件"),
 # ---- editors ----
 "nano": ("the nano-style editor (F2 = Chinese IME)", "nano 风格编辑器（F2 = 中文输入法）"),
 "vi": ("the modal vi editor (i/a/o insert, F2 = IME)", "vi 模式编辑器（i/a/o 插入，F2 = 输入法）"),
 "edit": ("the classic line editor face", "经典 edit 编辑器"),
 # ---- process / system ----
 "ps": ("list processes", "列出进程"),
 "top": ("process overview (live)", "进程总览（动态）"),
 "kill": ("terminate a process", "终止进程"),
 "nice": ("run with a priority / renice", "以指定优先级运行/调整优先级"),
 "spawn": ("spawn a background process", "派生后台进程"),
 "free": ("memory usage summary", "内存使用摘要"),
 "date": ("show date/time from the RTC", "显示 RTC 日期时间"),
 "uname": ("system identification", "系统标识"),
 "stats": ("display pipeline statistics", "显示管线统计"),
 "sleep": ("suspend for N seconds", "休眠 N 秒"),
 "halt": ("halt the machine", "停机"),
 "reboot": ("reboot the machine", "重启"),
 "shutdown": ("power off the machine", "关机"),
 "poweroff": ("power off (ACPI)", "关机（ACPI）"),
 "sync": ("flush block caches to disk", "把块缓存刷入磁盘"),
 "dmesg": ("show the kernel log", "显示内核日志"),
 "logcat": ("filtered kernel log stream", "带过滤的内核日志流"),
 "logctl": ("kernel log level control", "内核日志级别控制"),
 "crashlog": ("crash dump records", "崩溃转储记录"),
 "crashtest": ("crash handling test triggers", "崩溃处理测试触发器"),
 "health": ("system health snapshot/watch", "系统健康快照/监视"),
 "exc": ("exception report status", "异常报告状态"),
 "timer": ("timer/tick diagnostics", "定时器/滴答诊断"),
 "hpet": ("HPET timer status", "HPET 定时器状态"),
 "rtc": ("CMOS RTC status", "CMOS RTC 状态"),
 "pmtimer": ("ACPI PM timer status", "ACPI PM 定时器状态"),
 "tsc": ("TSC counter status", "TSC 计数器状态"),
 "i2cdetect": ("scan the I2C bus", "扫描 I2C 总线"),
 "i2cget": ("read an I2C register", "读取 I2C 寄存器"),
 "i2cset": ("write an I2C register", "写入 I2C 寄存器"),
 "smbus": ("SMBus status", "SMBus 状态"),
 "pcidump": ("dump PCI configuration", "转储 PCI 配置"),
 "pcibars": ("PCI BAR assignment check", "PCI BAR 分配检查"),
 "pcie": ("PCIe link status", "PCIe 链路状态"),
 "lspci": ("list PCI devices", "列出 PCI 设备"),
 "acpi": ("ACPI table summary", "ACPI 表摘要"),
 "acpidev": ("ACPI device tree", "ACPI 设备树"),
 "acpipow": ("ACPI power states", "ACPI 电源状态"),
 "msitest": ("MSI interrupt test", "MSI 中断测试"),
 "irqabitest": ("IRQ affinity test", "IRQ 亲和性测试"),
 "frag": ("physical memory fragments", "物理内存碎片"),
 "heap": ("kernel heap statistics", "内核堆统计"),
 "mem": ("physical memory statistics", "物理内存统计"),
 "sched": ("scheduler statistics", "调度器统计"),
 "syncstat": ("sync primitive statistics", "同步原语统计"),
 "dumpinfo": ("core dump status", "核心转储状态"),
 "watchdog": ("watchdog control", "看门狗控制"),
 "dskstat": ("disk I/O statistics", "磁盘 I/O 统计"),
 "lsblk": ("list block devices", "列出块设备"),
 "volume": ("mixer volume control", "混音器音量控制"),
 # ---- diagnostics family (tests) ----
 "memtest": ("memory service test battery", "内存服务测试组"),
 "heaptest": ("heap stress test", "堆压力测试"),
 "heapbounds": ("heap boundary check", "堆边界检查"),
 "pftest": ("page-fault handler test", "缺页处理测试"),
 "vmtest": ("VMM unit test", "VMM 单元测试"),
 "vmmap": ("address-space map dump", "地址空间映射转储"),
 "vmkernelpt": ("kernel page-table dump", "内核页表转储"),
 "cr3test": ("CR3 register audit", "CR3 寄存器审计"),
 "fstest": ("VFS stress test", "VFS 压力测试"),
 "synctest": ("sync primitive test", "同步原语测试"),
 "multi": ("multi-core wake test", "多核唤醒测试"),
 "frag": ("PMM fragment report", "PMM 碎片报告"),
 "pmmrace": ("PMM race detector", "PMM 竞态检测"),
 "loop": ("idle loop (job test target)", "空转循环（作业测试目标）"),
 "hello": ("hello world", "你好世界"),
 "badapp": ("intentionally misbehaving app", "故意行为异常的程序"),
 "int3_user": ("ring-3 int3 trap test", "ring-3 int3 陷阱测试"),
 "sigsegv_test": ("SIGSEGV delivery test", "SIGSEGV 投递测试"),
 "signal_test": ("signal subsystem test", "信号子系统测试"),
 "stackovf": ("stack overflow guard test", "栈溢出保护测试"),
 "sse_test": ("SSE state preservation test", "SSE 状态保存测试"),
 "priv_test": ("privilege instruction trap test", "特权指令陷阱测试"),
 "pf_test": ("page-fault exit test", "缺页退出测试"),
 "p3_test": ("P3 security window test", "P3 安全窗口测试"),
 "fork_test": ("fork/exit bookkeeping test", "fork/exit 记账测试"),
 "exec_test": ("execve replacement test", "execve 替换测试"),
 "pipe_test": ("pipe read/write test", "管道读写测试"),
 "select_test": ("select multiplexing test", "select 多路复用测试"),
 "select_zero_test": ("select timeout test", "select 超时测试"),
 "mmap_test": ("mmap mapping test", "mmap 映射测试"),
 "mmap_multi": ("multi-mapping test", "多映射测试"),
 "mprotect_test": ("mprotect permission test", "mprotect 权限测试"),
 "g6_test": ("G6 process group test", "G6 进程组测试"),
 "g6_exec_a": ("G6 exec stage A", "G6 exec 阶段 A"),
 "g6_exec_b": ("G6 exec stage B", "G6 exec 阶段 B"),
 "g6_oom": ("G6 OOM path test", "G6 OOM 路径测试"),
 "g7mmap": ("G7 mmap test", "G7 mmap 测试"),
 "fdref_test": ("fd refcount test", "fd 引用计数测试"),
 "nice": ("priority demo", "优先级演示"),
 "spawn": ("background spawn demo", "后台派生演示"),
 "timer": ("timer test", "定时器测试"),
 # ---- network ----
 "ping": ("ICMP echo to a host", "对主机发 ICMP 回显"),
 "wget": ("download a file over HTTP(S)", "通过 HTTP(S) 下载文件"),
 "ifconfig": ("network interface status", "网络接口状态"),
 "netstat": ("socket/connection statistics", "套接字/连接统计"),
 "dns": ("resolve a hostname", "解析主机名"),
 "dnstest": ("DNS resolver test", "DNS 解析器测试"),
 "dhcp": ("DHCP lease status/renew", "DHCP 租约状态/续租"),
 "arp": ("ARP table", "ARP 表"),
 "route": ("routing table", "路由表"),
 "ip": ("IP address configuration", "IP 地址配置"),
 "firewall": ("packet filter rules", "包过滤规则"),
 "ssh": ("SSH client", "SSH 客户端"),
 "sshd": ("SSH server", "SSH 服务端"),
 "tcpstats": ("TCP statistics", "TCP 统计"),
 "tcptest": ("TCP loopback test", "TCP 回环测试"),
 "tcpcc_test": ("TCP congestion control test", "TCP 拥塞控制测试"),
 "nf_test": ("netfilter test", "netfilter 测试"),
 "wifi": ("wireless status/scan", "无线状态/扫描"),
 "bt": ("bluetooth status", "蓝牙状态"),
 # ---- block / storage ----
 "ahci": ("AHCI controller status", "AHCI 控制器状态"),
 "nvme": ("NVMe controller status", "NVMe 控制器状态"),
 "ata": ("ATA/IDE status", "ATA/IDE 状态"),
 "parted": ("partition table editor (view)", "分区表查看"),
 "mkfs.fat32": ("create a FAT32 filesystem", "创建 FAT32 文件系统"),
 "mkfs.exfat": ("create an exFAT filesystem", "创建 exFAT 文件系统"),
 "mkfs.ext4": ("create an ext4 filesystem", "创建 ext4 文件系统"),
 "install": ("install Open Cube OS to a disk", "把系统安装到磁盘"),
 "grub-install": ("install the OCBL boot chain", "安装 OCBL 引导链"),
 "abdisk": ("A/B slot disk tool", "A/B 分区磁盘工具"),
 "abcfg": ("A/B slot configuration", "A/B 分区配置"),
 "checkupdate": ("check for OTA updates", "检查 OTA 更新"),
 "update": ("apply an OTA update", "应用 OTA 更新"),
 "rollback": ("roll back to the other slot", "回滚到另一分区"),
 "config": ("read/write system config", "读写系统配置"),
 # ---- NIC drivers ----
 "e1000e": ("e1000e NIC status", "e1000e 网卡状态"),
 "igb": ("igb NIC status", "igb 网卡状态"),
 "ixgbe": ("ixgbe NIC status", "ixgbe 网卡状态"),
 "rtl8139": ("RTL8139 NIC status", "RTL8139 网卡状态"),
 "rtl8168": ("RTL8168 NIC status", "RTL8168 网卡状态"),
 "rtl8125": ("RTL8125 NIC status", "RTL8125 网卡状态"),
 "rtl810x": ("RTL810x NIC status", "RTL810x 网卡状态"),
 "bcm57xx": ("BCM57xx NIC status", "BCM57xx 网卡状态"),
 # ---- audio ----
 "sound": ("audio subsystem status", "音频子系统状态"),
 "play": ("play a WAV file", "播放 WAV 文件"),
 "ac97": ("AC97 codec status", "AC97 编解码器状态"),
 "hda": ("HD Audio codec status", "HD Audio 编解码器状态"),
 "sb16": ("SoundBlaster16 status", "声霸卡 16 状态"),
 "es1370": ("ES1370 codec status", "ES1370 编解码器状态"),
 "usbaudio": ("USB audio status", "USB 音频状态"),
 "virtiosnd": ("virtio-sound status", "virtio 声卡状态"),
 # ---- display / CJK ----
 "chinese": ("CJK console demo", "中文控制台演示"),
 "chinesetest": ("CJK font/encoding test battery", "中文字体/编码测试组"),
 "font": ("font engine status", "字体引擎状态"),
 "font-test": ("font renderer test", "字体渲染测试"),
 "encoding": ("console encoding status/set", "控制台编码状态/设置"),
 "encoding-test": ("encoding codec test", "编码编解码测试"),
 "ime": ("the pinyin IME (-i interactive)", "拼音输入法（-i 交互）"),
 "ime-test": ("IME engine + session battery", "输入法引擎与会话测试组"),
 # ---- input / camera / misc hardware ----
 "touchpad": ("touchpad status", "触摸板状态"),
 "uvc": ("UVC camera status", "UVC 摄像头状态"),
 "scsi": ("SCSI device scan", "SCSI 设备扫描"),
 "gameport": ("game port status", "游戏端口状态"),
 "parallel": ("parallel port status", "并口状态"),
 "ir": ("infrared receiver status", "红外接收状态"),
 "usb": ("USB host status", "USB 主机状态"),
 "usbdev": ("USB device detail", "USB 设备详情"),
 # ---- TUI family ----
 "tui": ("the TUI desktop", "TUI 桌面"),
 "tui-test": ("TUI selftests", "TUI 自检"),
 "tui-hw": ("TUI hardware page", "TUI 硬件页"),
 "tui-net": ("TUI network page", "TUI 网络页"),
 "tui-sysinfo": ("TUI system info page", "TUI 系统信息页"),
 "tui-time": ("TUI clock page", "TUI 时钟页"),
 "tui-drv": ("TUI driver page", "TUI 驱动页"),
 "tui-sys": ("TUI system page", "TUI 系统页"),
 "tui-ext": ("TUI extensions page", "TUI 扩展页"),
 # ---- dynamic link ----
 "ld_so": ("the dynamic loader itself", "动态加载器本体"),
 "ldd": ("dynamic dependency lister", "动态依赖列表"),
 "main_dyn": ("dynamic app demo", "动态应用演示"),
 "dyn_hello": ("PIE hello demo", "PIE 演示"),
 "pie_test": ("PIE load test", "PIE 加载测试"),
 "so_test": ("shared library test", "共享库测试"),
 "dlsym_test": ("dlsym resolution test", "dlsym 解析测试"),
 "reloc_test": ("relocation test", "重定位测试"),
 # ---- storage / driver tests ----
 "ahci_test": ("AHCI port test", "AHCI 端口测试"),
 "nvme_test": ("NVMe namespace test", "NVMe 命名空间测试"),
 "ata_dma_test": ("ATA DMA test", "ATA DMA 测试"),
 "disk_rw_test": ("disk read/write test", "磁盘读写测试"),
 "partition_test": ("partition scan test", "分区扫描测试"),
 "fs_mount_test": ("filesystem mount test", "文件系统挂载测试"),
 "virtio_blk_test": ("virtio-blk test", "virtio 块设备测试"),
 "nic_rw_test": ("NIC loopback test", "网卡回环测试"),
 "e1000e_test": ("e1000e driver test", "e1000e 驱动测试"),
 "igb_test": ("igb driver test", "igb 驱动测试"),
 "ixgbe_test": ("ixgbe driver test", "ixgbe 驱动测试"),
 "rtl8139_test": ("RTL8139 driver test", "RTL8139 驱动测试"),
 "rtl8168_test": ("RTL8168 driver test", "RTL8168 驱动测试"),
 "rtl8125_test": ("RTL8125 driver test", "RTL8125 驱动测试"),
 "rtl810x_test": ("RTL810x driver test", "RTL810x 驱动测试"),
 "bcm57xx_test": ("BCM57xx driver test", "BCM57xx 驱动测试"),
 "other_nic_test": ("other NIC driver test", "其他网卡驱动测试"),
 "ac97_test": ("AC97 playback test", "AC97 播放测试"),
 "hda_test": ("HD Audio playback test", "HD Audio 播放测试"),
 "sb16_test": ("SB16 playback test", "SB16 播放测试"),
 "es1370_test": ("ES1370 playback test", "ES1370 播放测试"),
 "virtio_snd_test": ("virtio-snd test", "virtio 声卡测试"),
 "audio_rw_test": ("audio read/write test", "音频读写测试"),
 "sample_rate_test": ("sample rate test", "采样率测试"),
 "usb_core_test": ("USB core test", "USB 核心测试"),
 "usb_kbd_test": ("USB keyboard test", "USB 键盘测试"),
 "usb_mouse_test": ("USB mouse test", "USB 鼠标测试"),
 "usb_storage_test": ("USB mass storage test", "USB 大容量存储测试"),
 "usb_serial_test": ("USB serial test", "USB 串口测试"),
 "usb_hub_test": ("USB hub test", "USB 集线器测试"),
 "usb_hotplug_test": ("USB hotplug test", "USB 热插拔测试"),
 "usb_audio_test": ("USB audio test", "USB 音频测试"),
 "uvctest": ("UVC camera test", "UVC 摄像头测试"),
 "scsitest": ("SCSI device test", "SCSI 设备测试"),
 "gameporttest": ("game port test", "游戏端口测试"),
 "paralleltest": ("parallel port test", "并口测试"),
 "irtest": ("IR receiver test", "红外接收测试"),
 "touchtest": ("touchpad test", "触摸板测试"),
 "checkupdate_test": ("OTA check test", "OTA 检查测试"),
 "config_test": ("config backend test", "配置后端测试"),
 "update_pkg_test": ("update package test", "升级包测试"),
 "ab_partition_test": ("A/B partition test", "A/B 分区测试"),
 "update_download_test": ("OTA download test", "OTA 下载测试"),
 "update_verify_test": ("OTA verify test", "OTA 校验测试"),
 "update_install_test": ("OTA install test", "OTA 安装测试"),
 "update_rollback_test": ("OTA rollback test", "OTA 回滚测试"),
 "update_local_test": ("OTA local-file test", "OTA 本地文件测试"),
 "update_status_test": ("OTA status test", "OTA 状态测试"),
 "real_update_test": ("OTA real-flow test", "OTA 真实流程测试"),
 "halt_test": ("halt path test", "停机路径测试"),
 "reboot_test": ("reboot path test", "重启路径测试"),
 "poweroff_test": ("poweroff path test", "关机路径测试"),
 "suspend_test": ("S3 suspend test", "S3 挂起测试"),
 "acpitest": ("ACPI parser test", "ACPI 解析测试"),
 "cryptotest": ("crypto primitives test", "密码学原语测试"),
 "dhtest": ("Diffie-Hellman test", "迪菲-赫尔曼测试"),
 "i2ctest": ("I2C bus test", "I2C 总线测试"),
 "smbustest": ("SMBus test", "SMBus 测试"),
 "update_check_test": ("update check unit test", "升级检查单元测试"),
 "real_hw_test": ("real hardware disk test", "真实硬件磁盘测试"),
}

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    L1 = os.path.join(root, "src", "L1")
    missing = []
    covered = 0
    # folder apps
    for entry in sorted(os.listdir(L1)):
        d = os.path.join(L1, entry)
        if not os.path.isdir(d) or entry in ("_shared", "_i18n"):
            continue
        if not (os.path.exists(os.path.join(d, "main.c")) or
                os.path.exists(os.path.join(d, "main.asm"))):
            continue
        base = os.path.join(d, "i18n")
        if entry in H:
            covered += 1
            _apply(base, entry)
        else:
            missing.append(entry)
    # flat programs
    for f in sorted(os.listdir(L1)):
        p = os.path.join(L1, f)
        if not os.path.isfile(p) or not (f.endswith(".c") or f.endswith(".asm")):
            continue
        if f == "libfoo.c":
            continue
        name = os.path.splitext(f)[0]
        base = os.path.join(L1, "_i18n", name)
        if name in H:
            covered += 1
            _apply(base, name)
        else:
            missing.append(name)
    print(f"covered: {covered}, missing hints: {len(missing)}")
    if missing:
        print("MISSING:", " ".join(missing))
        raise SystemExit(1)


def _apply(base, name):
    en, zh = H[name]
    for lang, hint in (("en", en), ("zh", zh)):
        p = os.path.join(base, f"{lang}.json")
        with open(p, encoding="utf-8") as f:
            d = json.load(f)
        if d.get("_hint") == hint:
            continue
        d["_hint"] = hint
        with open(p, "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
