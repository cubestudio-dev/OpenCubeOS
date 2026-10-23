<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- GENERATED index (tools/gen_api_docs.py --pages). -->

# Shell 命令参考（233 条命令）

按注册每个命令的源文件分组 -
树结构即领域结构。在运行中的系统内使用 `help -a`（按字母顺序）或 `help -w`（按工作包）。

## 中文控制台层（8）
<a id="chinese"></a>

- [chinese](chinese.md) - 中文支持状态（字体/编码/渲染/输入法）
- [chinesetest](chinesetest.md) - 运行完整的中文支持回归测试套件
- [encoding](encoding.md) - 编解码器状态（gb2312/utf-8/ascii）
- [encoding-test](encoding-test.md) - 快速编解码器往返检查（完整版：chinesetest）
- [font](font.md) - 列出已注册的字体和活动渲染器
- [font-test](font-test.md) - 在控制台上渲染混合 CJK/ASCII 示例行
- [ime](ime.md) - 拼音输入法状态；"ime -i" 打开交互式控制台行
- [ime-test](ime-test.md) - 快速输入法音节解析检查

## 内核核心命令（55）
<a id="kernel-core"></a>

- [ahci](ahci.md) - AHCI 控制器/端口状态
- [ata](ata.md) - ATA（PIO + 总线主控 DMA）状态
- [checkupdate](checkupdate.md) - 通过 /etc/opencube.conf 检查更新（http/https）
- [checkupdate_test](checkupdate_test.md) - 更新检查自测（URL/JSON 单元 + 实时探测）
- [clear](clear.md) - 清屏
- [config](config.md) - 读写系统配置（config list|get|set|restore|path）
- [config_test](config_test.md) - 配置子系统自测（write/read/get_all/delete/restore）
- [cr3test](cr3test.md) - 测试 CR3 切换
- [cryptotest](cryptotest.md) - 使用 NIST 向量测试 AES/SHA-256/HMAC
- [dhtest](dhtest.md) - DH 模幂运算 1024 位（Oakley Group 1）计时 + 正确性
- [dskstat](dskstat.md) - 显示块设备
- [echo](echo.md) - 回显文本
- [exc](exc.md) - 运行异常自测（#DE/#UD/#PF）
- [fatmount](fatmount.md) - 挂载 FAT32（fatmount &lt;dev&gt; &lt;path&gt;）
- [fatstat](fatstat.md) - 显示 FAT32 统计
- [frag](frag.md) - 显示内存碎片
- [fstest](fstest.md) - 运行 VFS 自测（mkdir/write/read/ls）
- [halt](halt.md) - 停止内核
- [heap](heap.md) - 显示内核堆统计
- [heapbounds](heapbounds.md) - 堆边界回归测试（BUG-0140 包装 / BUG-0141 上限）
- [heaptest](heaptest.md) - 通过 100 次分配测试堆开销
- [help](help.md) - 显示此消息
- [irqabitest](irqabitest.md) - IRQ 入口 ABI 回归测试（BUG-0137 对齐 / BUG-0138 DF）
- [kill](kill.md) - 终止任务（kill &lt;tid&gt;）
- [l1test](l1test.md) - 使用运行中的进程测试 L1 job_create/job_list/job_control
- [lang](lang.md) - 显示或设置 Shell 输出语言（lang [en|zh]）
- [ldd](ldd.md) - 列出动态依赖（ldd &lt;program&gt;）
- [mem](mem.md) - 显示物理内存统计
- [memtest](memtest.md) - 运行内存测试（PMM + 堆）
- [mounts](mounts.md) - 列出 VFS 挂载表（mount 的别名）
- [multi](multi.md) - 生成 3 个任务交错输出
- [nice](nice.md) - 更改优先级（nice &lt;tid&gt; &lt;prio&gt;）
- [nvme](nvme.md) - NVMe 控制器/队列状态
- [pftest](pftest.md) - 测试缺页异常处理
- [pmmrace](pmmrace.md) - BUG-0043 复现：PMM 位图竞争 线程-vs-IRQ
- [poweroff](poweroff.md) - shutdown 的别名
- [ps](ps.md) - 列出所有任务
- [reboot](reboot.md) - 重启机器（reboot）
- [rollback](rollback.md) - 回滚到槽位 A（rollback）
- [run](run.md) - 运行用户程序（hello/badapp/loop/fork_test/.../dyn_test/so_test/dyn_hello/
- [sched](sched.md) - 显示调度器统计
- [shutdown](shutdown.md) - 关机（刷新 + ACPI/Bochs/APM 端口）
- [sleep](sleep.md) - 延迟 N 秒（sleep &lt;sec&gt;）
- [spawn](spawn.md) - 生成测试内核线程
- [ssh](ssh.md) - SSH 客户端连接（ssh &lt;ip&gt; [port] [user] [password]）
- [sshd](sshd.md) - SSH 服务器（sshd [port=22] [user=oc] [password=oc]）
- [stats](stats.md) - 显示中断/定时器统计
- [syncstat](syncstat.md) - 显示同步原语统计
- [synctest](synctest.md) - 测试同步原语（自旋锁/互斥锁/信号量）
- [timer](timer.md) - 注册一个 500ms 单次定时器
- [uname](uname.md) - 打印操作系统名称（uname [-a|-s|-r|-m]）
- [update](update.md) - 检查 + 安装系统更新（update [local &lt;pkg&gt; | --local &lt;pkg&gt; | --status]）
- [vmkernelpt](vmkernelpt.md) - VMM PT-所有权回归测试（BUG-0136）
- [vmmap](vmmap.md) - 显示地址空间映射
- [vmtest](vmtest.md) - 运行虚拟内存测试

## 网络（16）
<a id="network"></a>

- [arp](arp.md) - 显示 ARP 缓存
- [dhcp](dhcp.md) - 通过 DHCP 获取 IP
- [dns](dns.md) - 解析域名（dns &lt;name&gt; [aaaa|cname|mx|txt|ns|srv]）
- [dnstest](dnstest.md) - DNS 记录类型自测（A/AAAA/MX/TXT/NS/SRV 实时）
- [firewall](firewall.md) - 显示/添加/删除防火墙规则（链、状态、策略、连接跟踪）
- [ifconfig](ifconfig.md) - 显示网络接口信息
- [ip](ip.md) - 显示/设置 IP 地址
- [lspci](lspci.md) - 列出 PCI 设备
- [netstat](netstat.md) - 显示网络统计和套接字
- [nf_test](nf_test.md) - netfilter 自测（规则、连接跟踪、策略、实际路径）
- [ping](ping.md) - 发送 ICMP 回显（ping &lt;host&gt;）
- [route](route.md) - 显示/添加/删除路由表（route add &lt;dst&gt; &lt;mask&gt; &lt;gw&gt;）
- [tcpcc_test](tcpcc_test.md) - CUBIC 拥塞控制自测向量
- [tcpstats](tcpstats.md) - 显示 TCP 可靠性统计（cwnd、rto 等）
- [tcptest](tcptest.md) - TCP 选项协商自测（MSS/WScale/SACK/TS 实时）
- [wget](wget.md) - 通过 HTTP 下载文件（wget &lt;host&gt; [port] [path]）

## 蓝牙（1）
<a id="shell-bt"></a>

- [bt](bt.md) - 蓝牙：状态/USB/UART/测试（HCI/L2CAP/RFCOMM/SDP/SPP/HID）

## 摄像头（UVC）（2）
<a id="shell-camera"></a>

- [uvc](uvc.md) - 摄像头子系统状态（UVC 设备、格式、帧）
- [uvctest](uvctest.md) - 摄像头自测（JPEG 解码、UVC 描述符解析、核心）

## 芯片组（ACPI/PCI/i2c）（16）
<a id="shell-chipset"></a>

- [acpi](acpi.md) - 转储 ACPI 表（RSDP/FADT/DSDT/MCFG）
- [acpidev](acpidev.md) - 列出 ACPI 命名空间设备（AML）
- [acpipow](acpipow.md) - ACPI 电源状态（S1-S5/SCI/GPE/FACS）
- [acpitest](acpitest.md) - AML 解释器自测 + DSDT 检查
- [i2cdetect](i2cdetect.md) - 扫描 I2C/SMBus 总线上的设备
- [i2cget](i2cget.md) - 读取 I2C 字节（i2cget &lt;bus&gt; &lt;addr&gt; [reg]）
- [i2cset](i2cset.md) - 写入 I2C 字节（i2cset &lt;bus&gt; &lt;addr&gt; &lt;reg&gt; &lt;val&gt;）
- [i2ctest](i2ctest.md) - I2C 核心/控制器自测
- [msitest](msitest.md) - LAPIC 自 IPI + MSI/MSI-X 设备触发测试
- [pcibars](pcibars.md) - 列出带大小 BAR 的 PCI 设备
- [pcidump](pcidump.md) - 转储 PCI 配置空间（pcidump &lt;b:d.f&gt;）
- [pcie](pcie.md) - PCIe MMCONFIG 状态 + 能力遍历
- [smbus](smbus.md) - 从扫描中列出 SMBus 设备
- [smbustest](smbustest.md) - SMBus 协议测试（字节/字/块）
- [touchpad](touchpad.md) - 触摸板状态 + 待处理的手势事件
- [touchtest](touchtest.md) - 多点触控流水线自测（点击/滚动/捏合/滑动）

## Shell 核心（内置）（6）
<a id="shell-core"></a>

- [alias](alias.md) - 设置/显示别名
- [export](export.md) - 设置环境变量（export NAME=value）
- [nano](nano.md) - nano 风格编辑器（nano &lt;file&gt;; ^O 保存 ^X 退出）
- [set](set.md) - 列出环境变量
- [unalias](unalias.md) - 删除别名
- [vi](vi.md) - vi 编辑器，双模式（hjkl 移动，i/a/o 插入，ESC，:w :q :wq :q!）

## 崩溃恢复和转储（3）
<a id="shell-crash"></a>

- [crashlog](crashlog.md) - 显示最近的异常崩溃
- [crashtest](crashtest.md) - 触发内核崩溃测试（panic/exc/pf/assert/wd/dump/policy）
- [dumpinfo](dumpinfo.md) - 显示核心转储状态（大小/持久化/头部）

## 磁盘、分区、文件系统（7）
<a id="shell-disk"></a>

- [fsck](fsck.md) - 文件系统检查
- [lsblk](lsblk.md) - 列出块设备
- [mkfs.exfat](mkfs-exfat.md) - 格式化 exFAT (mkfs.exfat &lt;dev&gt; confirm)
- [mkfs.ext4](mkfs-ext4.md) - 格式化 ext4 (mkfs.ext4 &lt;dev&gt; confirm)
- [mkfs.fat32](mkfs-fat32.md) - 格式化 FAT32 (mkfs.fat32 &lt;dev&gt; confirm)
- [parted](parted.md) - 显示分区表 (parted [dev])
- [sync](sync.md) - 刷新磁盘缓存

## 磁盘/存储测试 (8)
<a id="shell-disk-test"></a>

- [ahci_test](ahci_test.md) - WP-10a：AHCI 初始化/端口/读写测试
- [ata_dma_test](ata_dma_test.md) - WP-10a：ATA 总线主控 DMA 测试 + PIO 时序
- [disk_rw_test](disk_rw_test.md) - WP-10a：单个设备上的往返读写测试 (disk_rw_test &lt;dev&gt;)
- [fs_mount_test](fs_mount_test.md) - WP-10a：mkfs+mount FAT32 文件往返测试 (fs_mount_test &lt;dev&gt;)
- [nvme_test](nvme_test.md) - WP-10a：NVMe 初始化/队列/读写测试
- [partition_test](partition_test.md) - WP-10a：MBR+GPT 解析测试 (partition_test [dev])
- [real_hw_test](real_hw_test.md) - WP-10a：真实硬件检测报告
- [virtio_blk_test](virtio_blk_test.md) - WP-10a：virtio-blk 容量 + 读写测试

## 文件与编辑器 (18)
<a id="shell-file"></a>

- [cat](cat.md) - 打印文件内容 (cat &lt;file&gt;)
- [cd](cd.md) - 切换目录 (cd [path])
- [cp](cp.md) - 复制文件 (cp &lt;src&gt; &lt;dst&gt;)
- [df](df.md) - 显示磁盘/挂载使用情况
- [du](du.md) - 目录使用情况 (du [path])
- [edit](edit.md) - 行编辑器 (edit &lt;file&gt;; :i :d :p :w :q :wq)
- [grep](grep.md) - 过滤匹配模式的行 (grep &lt;pattern&gt; [file])
- [ls](ls.md) - 列出目录 (ls [path])
- [mkdir](mkdir.md) - 创建目录 (mkdir &lt;path&gt;)
- [mount](mount.md) - 列出挂载，或挂载文件系统：mount &lt;type&gt; &lt;device&gt; &lt;path&gt;
- [mv](mv.md) - 移动/重命名文件 (mv &lt;src&gt; &lt;dst&gt;)
- [pwd](pwd.md) - 打印当前工作目录
- [rm](rm.md) - 删除文件 (rm &lt;file&gt;)
- [rmdir](rmdir.md) - 删除目录 (rmdir &lt;path&gt;)
- [touch](touch.md) - 创建空文件 (touch &lt;path&gt;)
- [tree](tree.md) - 目录树视图 (tree [path])
- [umount](umount.md) - 卸载 (umount &lt;path&gt;)
- [write](write.md) - 向文件写入文本 (write &lt;path&gt; &lt;text&gt;)

## 健康监控 (3)
<a id="shell-health"></a>

- [health](health.md) - 系统健康监控器 (health [watch|export])
- [health export](health_export.md) - 将健康 JSON 写入 /var/log/health.json
- [health watch](health_watch.md) - 实时健康监控，1 秒刷新 (ENTER 退出)

## 红外 (2)
<a id="shell-ir"></a>

- [ir](ir.md) - 红外状态 (CIR/SIR 设备、载波、计数器)
- [irtest](irtest.md) - 红外自检 (NEC/RC5/RC6/SIRC 往返测试)

## 内核日志 (klog) (3)
<a id="shell-log"></a>

- [dmesg](dmesg.md) - 显示内核日志环 (dmesg [N])
- [logcat](logcat.md) - 过滤内核日志 (logcat -l lvl -m mod -k key -t sec -n N [-f])
- [logctl](logctl.md) - 控制内核日志 (logctl status|level|enable|disable|clear|flush|rotate|e

## 网络接口测试 (18)
<a id="shell-nic-test"></a>

- [bcm57xx](bcm57xx.md) - BCM57xx 网卡状态
- [bcm57xx_test](bcm57xx_test.md) - WP-10b：BCM57xx 存在性/链路/发送-接收测试
- [e1000e](e1000e.md) - e1000e 网卡状态
- [e1000e_test](e1000e_test.md) - WP-10b：e1000e 初始化/链路/发送-接收测试
- [igb](igb.md) - igb 网卡状态
- [igb_test](igb_test.md) - WP-10b：igb 初始化/链路/发送-接收测试
- [ixgbe](ixgbe.md) - ixgbe 网卡状态
- [ixgbe_test](ixgbe_test.md) - WP-10b：ixgbe 存在性/链路/发送-接收测试
- [nic_rw_test](nic_rw_test.md) - WP-10b：所有网卡上的发送/接收正确性测试
- [other_nic_test](other_nic_test.md) - WP-10b：传统网卡 (3c59x/nForce/AR81xx/Yukon)
- [rtl810x](rtl810x.md) - RTL810x 网卡状态
- [rtl810x_test](rtl810x_test.md) - WP-10b：RTL810x 存在性/链路/发送-接收测试
- [rtl8125](rtl8125.md) - RTL8125 网卡状态
- [rtl8125_test](rtl8125_test.md) - WP-10b：RTL8125 存在性/链路/发送-接收测试
- [rtl8139](rtl8139.md) - RTL8139 网卡状态
- [rtl8139_test](rtl8139_test.md) - WP-10b：RTL8139 初始化/链路/发送-接收测试
- [rtl8168](rtl8168.md) - RTL8168 网卡状态
- [rtl8168_test](rtl8168_test.md) - WP-10b：RTL8168 存在性/链路/发送-接收测试

## 并行端口 (4)
<a id="shell-pargame"></a>

- [gameport](gameport.md) - 游戏端口状态 (轴、按钮、校准)
- [gameporttest](gameporttest.md) - 游戏端口自检 (轴读取、按钮、校准)
- [parallel](parallel.md) - 并行端口状态 (IEEE 1284 模式、计数器)
- [paralleltest](paralleltest.md) - 并行端口自检 (SPP 写入、EPP 周期、ECP 探测)

## 电源管理测试 (6)
<a id="shell-power-test"></a>

- [halt_test](halt_test.md) - 验证停止路径 (通过 'halt' 进行实时停止)
- [help_a_test](help_a_test.md) - 验证 help -a A-Z 排序
- [help_default_test](help_default_test.md) - 验证默认 help A-Z 排序
- [help_wp_test](help_wp_test.md) - 验证 help -w 组排序
- [poweroff_test](poweroff_test.md) - 真实关机尝试 (QEMU 退出)
- [reboot_test](reboot_test.md) - 验证重启路径 (通过 'reboot' 进行实时重启)

## SCSI (2)
<a id="shell-scsi"></a>

- [scsi](scsi.md) - SCSI 状态 (HBAs、目标、容量、块名称)
- [scsitest](scsitest.md) - SCSI 自检 (CDB 金标准、解析器、实时目标)

## 声音测试 (17)
<a id="shell-snd-test"></a>

- [ac97](ac97.md) - AC'97 状态
- [ac97_test](ac97_test.md) - AC'97 初始化/播放测试
- [audio_rw_test](audio_rw_test.md) - 每个卡上的播放/停止往返测试
- [es1370](es1370.md) - ES1370/1371 状态
- [es1370_test](es1370_test.md) - ES1370 初始化/播放测试
- [hda](hda.md) - Intel HDA 状态
- [hda_test](hda_test.md) - Intel HDA 初始化/编解码器/播放测试
- [play](play.md) - 播放测试音 (play [rate])
- [sample_rate_test](sample_rate_test.md) - 44100/48000 速率配置测试
- [sb16](sb16.md) - Sound Blaster 16 状态
- [sb16_test](sb16_test.md) - SB16 初始化/播放测试
- [sound](sound.md) - 列出已注册的声卡
- [usb_audio_test](usb_audio_test.md) - USB 音频初始化/播放测试
- [usbaudio](usbaudio.md) - USB 音频状态
- [virtio_snd_test](virtio_snd_test.md) - virtio-snd 初始化/播放测试
- [virtiosnd](virtiosnd.md) - virtio-snd 状态
- [volume](volume.md) - 获取/设置音量 (volume [0-100])

## 时间源 (4)
<a id="shell-timer"></a>

- [hpet](hpet.md) - HPET：功能 + 计数器 + 单次/周期性 IRQ8 测试
- [pmtimer](pmtimer.md) - ACPI PM 计时器：读取 + PIT 校准 + 回绕测试
- [rtc](rtc.md) - CMOS RTC：时间 + 状态 + UIE/PIE/报警中断测试
- [tsc](tsc.md) - TSC：读取 + PIT/HPET/PMTimer 校准 + 延迟测试

## OTA 更新测试 (10)
<a id="shell-update-test"></a>

- [ab_partition_test](ab_partition_test.md) - A/B 磁盘发现 + 槽位 + 标志 (WP-10u)
- [real_update_test](real_update_test.md) - 端到端更新阶段 1 (WP-10u)
- [update_check_test](update_check_test.md) - 使用包字段更新清单检查 (WP-10u)
- [update_download_test](update_download_test.md) - 下载更新包 (WP-10u)
- [update_install_test](update_install_test.md) - 将包安装到槽位 B (WP-10u)
- [update_local_test](update_local_test.md) - 通过 cmd_update --local 的离线更新流程 (WP-10u)
- [update_pkg_test](update_pkg_test.md) - gzip/DEFLATE + tar 单元测试 (WP-10u)
- [update_rollback_test](update_rollback_test.md) - 启动标志 + 回滚 (WP-10u)
- [update_status_test](update_status_test.md) - ota_update_get_status 覆盖 (WP-10u)
- [update_verify_test](update_verify_test.md) - 包的 SHA256 验证 (WP-10u)

## USB 测试 (9)
<a id="shell-usb-test"></a>

- [usb](usb.md) - USB 主机/设备 + 类驱动状态
- [usb_core_test](usb_core_test.md) - USB 核心不变性 + 实时传输
- [usb_hotplug_test](usb_hotplug_test.md) - USB 热插拔 (usb_hotplug_test [wait-new N|wait-leave N])
- [usb_hub_test](usb_hub_test.md) - USB 外部集线器 + 子设备
- [usb_kbd_test](usb_kbd_test.md) - USB HID 键盘存在性 + 注入
- [usb_mouse_test](usb_mouse_test.md) - USB HID 鼠标存在性 + 事件消耗
- [usb_serial_test](usb_serial_test.md) - USB 串行 TX/RX 往返测试
- [usb_storage_test](usb_storage_test.md) - USB MSC 驱动器容量/读写/分区
- [usbdev](usbdev.md) - USB 设备详情 (usbdev [槽位])

## 看门狗 (1)
<a id="shell-wd"></a>

- [watchdog](watchdog.md) - 看门狗状态/控制（硬件 TCO/WDAT + 监控槽位 + 策略测试）

## 无线 (1)
<a id="shell-wireless"></a>

- [wifi](wifi.md) - 无线：状态/扫描/测试/SDIO（802.11 系列族 + SDIO 总线）

## 存储与块设备 (4)
<a id="storage"></a>

- [abcfg](abcfg.md) - 显示 grub.cfg 文件的 A/B 磁盘/安装写入
- [abdisk](abdisk.md) - 创建可启动的 A/B 更新磁盘（abdisk &lt;dev&gt;; 会销毁数据）
- [grub-install](grub-install.md) - 写入 GRUB BIOS 引导加载器（grub-install &lt;dev&gt;）
- [install](install.md) - 将运行中的系统安装到磁盘（install &lt;dev&gt;; 会销毁数据）

## TUI（终端用户界面）(9)
<a id="tui"></a>

- [tui](tui.md) - 全屏 TUI 主菜单（方向键/Tab/Enter/Esc）
- [tui-drv](tui-drv.md) - 驱动设置 TUI（tui-hw 的别名；可选页面名称）
- [tui-ext](tui-ext.md) - TUI L1 扩展实验室：自检 + 注册表
- [tui-hw](tui-hw.md) - 硬件配置 TUI（可选：页面名称）
- [tui-net](tui-net.md) - 无线网络 TUI（WiFi + 蓝牙）
- [tui-sys](tui-sys.md) - 系统设置 TUI（主机/网络/存储/用户/安全/更新/日志/TUI）
- [tui-sysinfo](tui-sysinfo.md) - 系统信息 TUI（可选：页面名称）
- [tui-test](tui-test.md) - 运行 TUI 自检套件（输入/预期/实际/判定）
- [tui-time](tui-time.md) - 时间 TUI：时钟/源/设置 RTC/时区
