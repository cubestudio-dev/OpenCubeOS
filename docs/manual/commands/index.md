<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- GENERATED index (tools/gen_api_docs.py --pages). -->

# Shell command reference (251 commands)

Grouped by the v2 home that registers each command - the tree
structure is the domain structure. Inside a running system use
`help -a` (alphabetical) or `help -w` (by work package).

## Kernel core commands (36)
<a id="kernel-core"></a>

- [cr3test](cr3test.md) - test CR3 switching
- [echo](echo.md) - echo the text back
- [exc](exc.md) - run exception self-test (#DE/#UD/#PF)
- [frag](frag.md) - show memory fragmentation
- [fstest](fstest.md) - run VFS self-test (mkdir/write/read/ls)
- [halt](halt.md) - halt the kernel
- [heap](heap.md) - show kernel heap stats
- [heapbounds](heapbounds.md) - heap bounds regression (BUG-0140 wrap / BUG-0141 ceiling)
- [heaptest](heaptest.md) - test heap overhead with 100 allocs
- [help](help.md) - show this message
- [irqabitest](irqabitest.md) - IRQ-entry ABI regression (BUG-0137 align / BUG-0138 DF)
- [kill](kill.md) - kill a task (kill &lt;tid&gt;)
- [l1test](l1test.md) - test L1 job_create/job_list/job_control with running process
- [ldd](ldd.md) - list dynamic dependencies (ldd &lt;program&gt;)
- [mem](mem.md) - show physical memory stats
- [memtest](memtest.md) - run memory test (PMM + heap)
- [multi](multi.md) - spawn 3 tasks with interleaved output
- [nice](nice.md) - change priority (nice &lt;tid&gt; &lt;prio&gt;)
- [pftest](pftest.md) - test page fault handling
- [pmmrace](pmmrace.md) - BUG-0043 repro: PMM bitmap race thread-vs-IRQ
- [poweroff](poweroff.md) - alias of shutdown
- [ps](ps.md) - list all tasks
- [reboot](reboot.md) - reboot the machine (reboot)
- [run](run.md) - run a user program (hello/badapp/loop/fork_test/.../dyn_test/so_test/dyn_hello/dlsym_test/pie_test/reloc_test)
- [sched](sched.md) - show scheduler stats
- [shutdown](shutdown.md) - power off (flush + ACPI/Bochs/APM ports)
- [sleep](sleep.md) - delay for N seconds (sleep &lt;sec&gt;)
- [spawn](spawn.md) - spawn a test kernel thread
- [ssh](ssh.md) - SSH client connect (ssh &lt;ip&gt; [port] [user] [password])
- [sshd](sshd.md) - SSH server (sshd [port=22] [user=oc] [password=oc])
- [syncstat](syncstat.md) - show sync primitive stats
- [synctest](synctest.md) - test sync primitives (spinlock/mutex/sem)
- [timer](timer.md) - register a 500ms one-shot timer
- [vmkernelpt](vmkernelpt.md) - VMM PT-ownership regression (BUG-0136)
- [vmmap](vmmap.md) - show address space mappings
- [vmtest](vmtest.md) - run virtual memory test

## Chipset (ACPI/PCI/i2c) (14)
<a id="shell-chipset"></a>

- [acpi](acpi.md) - dump ACPI tables (RSDP/FADT/DSDT/MCFG)
- [acpidev](acpidev.md) - list ACPI namespace devices (AML)
- [acpipow](acpipow.md) - ACPI power status (S1-S5/SCI/GPE/FACS)
- [acpitest](acpitest.md) - AML interpreter self-test + DSDT checks
- [i2cdetect](i2cdetect.md) - scan I2C/SMBus buses for devices
- [i2cget](i2cget.md) - read I2C byte (i2cget &lt;bus&gt; &lt;addr&gt; [reg])
- [i2cset](i2cset.md) - write I2C byte (i2cset &lt;bus&gt; &lt;addr&gt; &lt;reg&gt; &lt;val&gt;)
- [i2ctest](i2ctest.md) - I2C core/controller self-test
- [msitest](msitest.md) - LAPIC self-IPI + MSI/MSI-X device fire test
- [pcibars](pcibars.md) - list PCI devices with sized BARs
- [pcidump](pcidump.md) - dump PCI config space (pcidump &lt;b:d.f&gt;)
- [pcie](pcie.md) - PCIe MMCONFIG state + capability walk
- [smbus](smbus.md) - list SMBus devices from the scan
- [smbustest](smbustest.md) - SMBus protocol test (byte/word/block)

## Shell core (built-ins) (4)
<a id="shell-core"></a>

- [alias](alias.md) - set/show aliases
- [export](export.md) - set env var (export NAME=value)
- [set](set.md) - list env vars
- [unalias](unalias.md) - remove an alias

## Crash recovery and dumps (3)
<a id="shell-crash"></a>

- [crashlog](crashlog.md) - show last exception crashes
- [crashtest](crashtest.md) - trigger kernel crash tests (panic/exc/pf/assert/wd/dump/policy)
- [dumpinfo](dumpinfo.md) - show core dump status (size/persisted/header)

## Files and the editor (15)
<a id="shell-file"></a>

- [cat](cat.md) - print file contents (cat &lt;file&gt;)
- [cd](cd.md) - change directory (cd [path])
- [cp](cp.md) - copy file (cp &lt;src&gt; &lt;dst&gt;)
- [df](df.md) - show disk/mount usage
- [du](du.md) - directory usage (du [path])
- [edit](edit.md) - line editor (edit &lt;file&gt;; :i :d :p :w :q :wq)
- [grep](grep.md) - filter lines matching pattern (grep &lt;pattern&gt; [file])
- [ls](ls.md) - list directory (ls [path])
- [mkdir](mkdir.md) - make directory (mkdir &lt;path&gt;)
- [mv](mv.md) - move/rename file (mv &lt;src&gt; &lt;dst&gt;)
- [pwd](pwd.md) - print working directory
- [rm](rm.md) - delete file (rm &lt;file&gt;)
- [rmdir](rmdir.md) - remove directory (rmdir &lt;path&gt;)
- [touch](touch.md) - create empty file (touch &lt;path&gt;)
- [write](write.md) - write text to file (write &lt;path&gt; &lt;text&gt;)

## Health monitoring (3)
<a id="shell-health"></a>

- [health](health.md) - system health monitor (health [watch|export])
- [health export](health_export.md) - write health JSON to /var/log/health.json
- [health watch](health_watch.md) - live health monitor, 1 s refresh (ENTER exits)

## Kernel log (klog) (3)
<a id="shell-log"></a>

- [dmesg](dmesg.md) - show the kernel log ring (dmesg [N])
- [logcat](logcat.md) - filter the kernel log (logcat -l lvl -m mod -k key -t sec -n N [-f])
- [logctl](logctl.md) - control the kernel log (logctl status|level|enable|disable|clear|flush|rotate|export|filter)

## Power management tests (3)
<a id="shell-power-test"></a>

- [help_a_test](help_a_test.md) - verify help -a A-Z ordering
- [help_default_test](help_default_test.md) - verify default help A-Z ordering
- [help_wp_test](help_wp_test.md) - verify help -w group ordering

## Time sources (4)
<a id="shell-timer"></a>

- [hpet](hpet.md) - HPET: caps + counter + one-shot/periodic IRQ8 tests
- [pmtimer](pmtimer.md) - ACPI PM timer: read + PIT calibration + wrap tests
- [rtc](rtc.md) - CMOS RTC: time + status + UIE/PIE/alarm interrupt tests
- [tsc](tsc.md) - TSC: reads + PIT/HPET/PMTimer calibration + delay test

## Watchdog (1)
<a id="shell-wd"></a>

- [watchdog](watchdog.md) - watchdog status/control (hw TCO/WDAT + monitored slots + policy tests)

## Networking service (L0 half) (4)
<a id="svc-net"></a>

- [ifconfig](ifconfig.md) - show network interface info
- [netstat](netstat.md) - show network statistics and sockets
- [ping](ping.md) - send ICMP echo (ping &lt;host&gt;)
- [wget](wget.md) - download file via HTTP (wget &lt;host&gt; [port] [path])

## TUI (terminal UI) (9)
<a id="tui"></a>

- [tui](tui.md) - full-screen TUI main menu (arrows/Tab/Enter/Esc)
- [tui-drv](tui-drv.md) - driver setup TUI (alias of tui-hw; page name optional)
- [tui-ext](tui-ext.md) - TUI L1 extension lab: selftest + registries
- [tui-hw](tui-hw.md) - hardware configuration TUI (optional: page name)
- [tui-net](tui-net.md) - wireless network TUI (WiFi + Bluetooth)
- [tui-sys](tui-sys.md) - system settings TUI (host/net/storage/user/security/update/log/tui)
- [tui-sysinfo](tui-sysinfo.md) - system information TUI (optional: page name)
- [tui-test](tui-test.md) - run the TUI self-test suite (input/expected/actual/verdict)
- [tui-time](tui-time.md) - time TUI: clock/sources/set RTC/timezone

## ush (ring-3 shell: SVC rows + built-ins) (152)
<a id="ush"></a>

- [ab_partition_test](ab_partition_test.md) - ab_partition_test
- [abcfg](abcfg.md) - abcfg
- [abdisk](abdisk.md) - abdisk &lt;dev&gt;
- [ac97](ac97.md) - ac97
- [ac97_test](ac97_test.md) - ac97_test
- [ahci](ahci.md) - ahci / nvme / ata
- [ahci_test](ahci_test.md) - ahci_test
- [arp](arp.md) - arp / route / ip
- [ata](ata.md) - ahci / nvme / ata
- [ata_dma_test](ata_dma_test.md) - ata_dma_test
- [audio_rw_test](audio_rw_test.md) - audio_rw_test
- [awk](awk.md) - 
- [bcm57xx](bcm57xx.md) - bcm57xx
- [bcm57xx_test](bcm57xx_test.md) - bcm57xx_test
- [bt](bt.md) - wifi / bt
- [checkupdate](checkupdate.md) - update / checkupdate / rollback
- [checkupdate_test](checkupdate_test.md) - checkupdate_test
- [chinese](chinese.md) - chinese
- [chinesetest](chinesetest.md) - chinesetest
- [chmod](chmod.md) - 
- [chown](chown.md) - 
- [clear](clear.md) - clear
- [config](config.md) - config list|get|set
- [config_test](config_test.md) - config_test
- [cryptotest](cryptotest.md) - cryptotest / dhtest
- [date](date.md) - date
- [dhcp](dhcp.md) - dhcp / dns / dnstest
- [dhtest](dhtest.md) - cryptotest / dhtest
- [disk_rw_test](disk_rw_test.md) - disk_rw_test &lt;dev&gt;
- [dns](dns.md) - dhcp / dns / dnstest
- [dnstest](dnstest.md) - dhcp / dns / dnstest
- [dskstat](dskstat.md) - dskstat / lsblk
- [e1000e](e1000e.md) - e1000e
- [e1000e_test](e1000e_test.md) - e1000e_test
- [encoding](encoding.md) - encoding
- [encoding-test](encoding-test.md) - encoding-test
- [env](env.md) - env
- [es1370](es1370.md) - es1370
- [es1370_test](es1370_test.md) - es1370_test
- [exit](exit.md) - 
- [fatmount](fatmount.md) - fatmount [dev p]   : mount FAT32
- [fatstat](fatstat.md) - fatstat
- [firewall](firewall.md) - firewall / nf_test
- [font](font.md) - font
- [font-test](font-test.md) - font-test
- [free](free.md) - free
- [fs_mount_test](fs_mount_test.md) - fs_mount_test &lt;dev&gt;
- [fsck](fsck.md) - fsck / sync
- [gameport](gameport.md) - gameport
- [gameporttest](gameporttest.md) - gameporttest
- [grub-install](grub-install.md) - grub-install &lt;dev&gt;
- [halt_test](halt_test.md) - halt_test
- [hda](hda.md) - hda
- [hda_test](hda_test.md) - hda_test
- [head](head.md) - 
- [history](history.md) - history
- [igb](igb.md) - igb
- [igb_test](igb_test.md) - igb_test
- [ime](ime.md) - ime
- [ime-test](ime-test.md) - ime-test
- [install](install.md) - install &lt;dev&gt;
- [ip](ip.md) - arp / route / ip
- [ir](ir.md) - ir
- [irtest](irtest.md) - irtest
- [ixgbe](ixgbe.md) - ixgbe
- [ixgbe_test](ixgbe_test.md) - ixgbe_test
- [lang](lang.md) - lang [en|zh]
- [ln](ln.md) - 
- [lsblk](lsblk.md) - dskstat / lsblk
- [lspci](lspci.md) - lspci
- [mkfs.exfat](mkfs-exfat.md) - mkfs.exfat &lt;dev&gt;
- [mkfs.ext4](mkfs-ext4.md) - mkfs.ext4 &lt;dev&gt;
- [mkfs.fat32](mkfs-fat32.md) - mkfs.fat32 &lt;dev&gt;
- [mount](mount.md) - mount [type dev p] : list or mount
- [mounts](mounts.md) - mounts
- [nano](nano.md) - 
- [nf_test](nf_test.md) - firewall / nf_test
- [nic_rw_test](nic_rw_test.md) - nic_rw_test
- [nvme](nvme.md) - ahci / nvme / ata
- [nvme_test](nvme_test.md) - nvme_test
- [other_nic_test](other_nic_test.md) - other_nic_test
- [parallel](parallel.md) - parallel
- [paralleltest](paralleltest.md) - paralleltest
- [parted](parted.md) - parted [dev]
- [partition_test](partition_test.md) - partition_test [dev]
- [play](play.md) - play &lt;file&gt;
- [poweroff_test](poweroff_test.md) - poweroff_test
- [real_hw_test](real_hw_test.md) - real_hw_test
- [real_update_test](real_update_test.md) - real_update_test
- [reboot_test](reboot_test.md) - reboot_test
- [rollback](rollback.md) - update / checkupdate / rollback
- [route](route.md) - arp / route / ip
- [rtl810x](rtl810x.md) - rtl810x
- [rtl810x_test](rtl810x_test.md) - rtl810x_test
- [rtl8125](rtl8125.md) - rtl8125
- [rtl8125_test](rtl8125_test.md) - rtl8125_test
- [rtl8139](rtl8139.md) - rtl8139
- [rtl8139_test](rtl8139_test.md) - rtl8139_test
- [rtl8168](rtl8168.md) - rtl8168
- [rtl8168_test](rtl8168_test.md) - rtl8168_test
- [sample_rate_test](sample_rate_test.md) - sample_rate_test
- [sb16](sb16.md) - sb16
- [sb16_test](sb16_test.md) - sb16_test
- [scsi](scsi.md) - scsi / scsitest
- [scsitest](scsitest.md) - scsi / scsitest
- [sed](sed.md) - 
- [sort](sort.md) - 
- [sound](sound.md) - sound
- [stat](stat.md) - 
- [stats](stats.md) - stats
- [suspend_test](suspend_test.md) - suspend_test
- [sync](sync.md) - fsck / sync
- [tail](tail.md) - 
- [tcpcc_test](tcpcc_test.md) - tcpcc_test
- [tcpstats](tcpstats.md) - tcpstats
- [tcptest](tcptest.md) - tcptest
- [top](top.md) - 
- [touchpad](touchpad.md) - touchpad / touchtest
- [touchtest](touchtest.md) - touchpad / touchtest
- [tree](tree.md) - tree [path]        : directory tree
- [umount](umount.md) - umount &lt;path&gt;      : unmount
- [uname](uname.md) - uname
- [uniq](uniq.md) - 
- [update](update.md) - update / checkupdate / rollback
- [update_check_test](update_check_test.md) - update_check_test
- [update_download_test](update_download_test.md) - update_download_test
- [update_install_test](update_install_test.md) - update_install_test
- [update_local_test](update_local_test.md) - update_local_test
- [update_pkg_test](update_pkg_test.md) - update_pkg_test
- [update_rollback_test](update_rollback_test.md) - update_rollback_test
- [update_status_test](update_status_test.md) - update_status_test
- [update_verify_test](update_verify_test.md) - update_verify_test
- [usb](usb.md) - usb
- [usb_audio_test](usb_audio_test.md) - usb_audio_test
- [usb_core_test](usb_core_test.md) - usb_core_test
- [usb_hotplug_test](usb_hotplug_test.md) - usb_hotplug_test [...]
- [usb_hub_test](usb_hub_test.md) - usb_hub_test
- [usb_kbd_test](usb_kbd_test.md) - usb_kbd_test
- [usb_mouse_test](usb_mouse_test.md) - usb_mouse_test
- [usb_serial_test](usb_serial_test.md) - usb_serial_test
- [usb_storage_test](usb_storage_test.md) - usb_storage_test
- [usbaudio](usbaudio.md) - usbaudio
- [usbdev](usbdev.md) - usbdev [slot]
- [uvc](uvc.md) - uvc / uvctest
- [uvctest](uvctest.md) - uvc / uvctest
- [vi](vi.md) - 
- [virtio_blk_test](virtio_blk_test.md) - virtio_blk_test
- [virtio_snd_test](virtio_snd_test.md) - virtio_snd_test
- [virtiosnd](virtiosnd.md) - virtiosnd
- [volume](volume.md) - volume [0-100]
- [wc](wc.md) - 
- [wifi](wifi.md) - wifi / bt
