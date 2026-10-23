<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- GENERATED index (tools/gen_api_docs.py --pages). -->

# Shell command reference (233 commands)

Grouped by the source file that registers each command - the
tree structure is the domain structure. Inside a running system
use `help -a` (alphabetical) or `help -w` (by work package).

## Chinese console layer (8)
<a id="chinese"></a>

- [chinese](chinese.md) - chinese support status (font/encoding/render/ime)
- [chinesetest](chinesetest.md) - run the full Chinese support regression suite
- [encoding](encoding.md) - encoding codec status (gb2312/utf-8/ascii)
- [encoding-test](encoding-test.md) - quick codec round-trip check (full: chinesetest)
- [font](font.md) - list registered fonts and the active renderer
- [font-test](font-test.md) - render a mixed CJK/ASCII sample row on the console
- [ime](ime.md) - pinyin IME status; "ime -i" opens the interactive console line
- [ime-test](ime-test.md) - quick IME syllable resolution check

## Kernel core commands (55)
<a id="kernel-core"></a>

- [ahci](ahci.md) - AHCI controller/port status
- [ata](ata.md) - ATA (PIO + Bus-Master DMA) status
- [checkupdate](checkupdate.md) - check for updates via /etc/opencube.conf (http/https)
- [checkupdate_test](checkupdate_test.md) - update check self-test (URL/JSON units + live probe)
- [clear](clear.md) - clear screen
- [config](config.md) - read/write system config (config list|get|set|restore|path)
- [config_test](config_test.md) - config subsystem self-test (write/read/get_all/delete/restore)
- [cr3test](cr3test.md) - test CR3 switching
- [cryptotest](cryptotest.md) - test AES/SHA-256/HMAC with NIST vectors
- [dhtest](dhtest.md) - DH modexp 1024-bit (Oakley Group 1) timing + correctness
- [dskstat](dskstat.md) - show block devices
- [echo](echo.md) - echo the text back
- [exc](exc.md) - run exception self-test (#DE/#UD/#PF)
- [fatmount](fatmount.md) - mount FAT32 (fatmount &lt;dev&gt; &lt;path&gt;)
- [fatstat](fatstat.md) - show FAT32 stats
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
- [lang](lang.md) - show or set the shell output language (lang [en|zh])
- [ldd](ldd.md) - list dynamic dependencies (ldd &lt;program&gt;)
- [mem](mem.md) - show physical memory stats
- [memtest](memtest.md) - run memory test (PMM + heap)
- [mounts](mounts.md) - list VFS mount table (alias for mount)
- [multi](multi.md) - spawn 3 tasks with interleaved output
- [nice](nice.md) - change priority (nice &lt;tid&gt; &lt;prio&gt;)
- [nvme](nvme.md) - NVMe controller/queue status
- [pftest](pftest.md) - test page fault handling
- [pmmrace](pmmrace.md) - BUG-0043 repro: PMM bitmap race thread-vs-IRQ
- [poweroff](poweroff.md) - alias of shutdown
- [ps](ps.md) - list all tasks
- [reboot](reboot.md) - reboot the machine (reboot)
- [rollback](rollback.md) - roll back to slot A (rollback)
- [run](run.md) - run a user program (hello/badapp/loop/fork_test/.../dyn_test/so_test/dyn_hello/
- [sched](sched.md) - show scheduler stats
- [shutdown](shutdown.md) - power off (flush + ACPI/Bochs/APM ports)
- [sleep](sleep.md) - delay for N seconds (sleep &lt;sec&gt;)
- [spawn](spawn.md) - spawn a test kernel thread
- [ssh](ssh.md) - SSH client connect (ssh &lt;ip&gt; [port] [user] [password])
- [sshd](sshd.md) - SSH server (sshd [port=22] [user=oc] [password=oc])
- [stats](stats.md) - show interrupt/timer stats
- [syncstat](syncstat.md) - show sync primitive stats
- [synctest](synctest.md) - test sync primitives (spinlock/mutex/sem)
- [timer](timer.md) - register a 500ms one-shot timer
- [uname](uname.md) - print OS name (uname [-a|-s|-r|-m])
- [update](update.md) - check + install system update (update [local &lt;pkg&gt; | --local &lt;pkg&gt; | --status])
- [vmkernelpt](vmkernelpt.md) - VMM PT-ownership regression (BUG-0136)
- [vmmap](vmmap.md) - show address space mappings
- [vmtest](vmtest.md) - run virtual memory test

## Networking (16)
<a id="network"></a>

- [arp](arp.md) - show ARP cache
- [dhcp](dhcp.md) - get IP via DHCP
- [dns](dns.md) - resolve domain name (dns &lt;name&gt; [aaaa|cname|mx|txt|ns|srv])
- [dnstest](dnstest.md) - DNS record-type self-test (A/AAAA/MX/TXT/NS/SRV live)
- [firewall](firewall.md) - show/add/del firewall rules (chains, states, policies, conntrack)
- [ifconfig](ifconfig.md) - show network interface info
- [ip](ip.md) - show/set IP address
- [lspci](lspci.md) - list PCI devices
- [netstat](netstat.md) - show network statistics and sockets
- [nf_test](nf_test.md) - netfilter self-test (rules, conntrack, policies, real-path)
- [ping](ping.md) - send ICMP echo (ping &lt;host&gt;)
- [route](route.md) - show/add/del routing table (route add &lt;dst&gt; &lt;mask&gt; &lt;gw&gt;)
- [tcpcc_test](tcpcc_test.md) - CUBIC congestion control self-test vectors
- [tcpstats](tcpstats.md) - show TCP reliability stats (cwnd, rto, etc.)
- [tcptest](tcptest.md) - TCP option negotiation self-test (MSS/WScale/SACK/TS live)
- [wget](wget.md) - download file via HTTP (wget &lt;host&gt; [port] [path])

## Bluetooth (1)
<a id="shell-bt"></a>

- [bt](bt.md) - bluetooth: status/usb/uart/test (HCI/L2CAP/RFCOMM/SDP/SPP/HID)

## Cameras (UVC) (2)
<a id="shell-camera"></a>

- [uvc](uvc.md) - camera subsystem status (UVC devices, formats, frames)
- [uvctest](uvctest.md) - camera self test (JPEG decode, UVC descriptor parse, core)

## Chipset (ACPI/PCI/i2c) (16)
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
- [touchpad](touchpad.md) - touchpad status + pending gesture events
- [touchtest](touchtest.md) - multitouch pipeline self-test (tap/scroll/pinch/swipe)

## Shell core (built-ins) (6)
<a id="shell-core"></a>

- [alias](alias.md) - set/show aliases
- [export](export.md) - set env var (export NAME=value)
- [nano](nano.md) - nano-style editor (nano &lt;file&gt;; ^O save ^X exit)
- [set](set.md) - list env vars
- [unalias](unalias.md) - remove an alias
- [vi](vi.md) - vi editor, two-mode (hjkl move, i/a/o insert, ESC, :w :q :wq :q!)

## Crash recovery and dumps (3)
<a id="shell-crash"></a>

- [crashlog](crashlog.md) - show last exception crashes
- [crashtest](crashtest.md) - trigger kernel crash tests (panic/exc/pf/assert/wd/dump/policy)
- [dumpinfo](dumpinfo.md) - show core dump status (size/persisted/header)

## Disks, partitions, filesystems (7)
<a id="shell-disk"></a>

- [fsck](fsck.md) - filesystem check
- [lsblk](lsblk.md) - list block devices
- [mkfs.exfat](mkfs-exfat.md) - format exFAT (mkfs.exfat &lt;dev&gt; confirm)
- [mkfs.ext4](mkfs-ext4.md) - format ext4 (mkfs.ext4 &lt;dev&gt; confirm)
- [mkfs.fat32](mkfs-fat32.md) - format FAT32 (mkfs.fat32 &lt;dev&gt; confirm)
- [parted](parted.md) - show partition table (parted [dev])
- [sync](sync.md) - flush disk cache

## Disk/storage tests (8)
<a id="shell-disk-test"></a>

- [ahci_test](ahci_test.md) - WP-10a: AHCI init/ports/read-write test
- [ata_dma_test](ata_dma_test.md) - WP-10a: ATA Bus-Master DMA test + PIO timing
- [disk_rw_test](disk_rw_test.md) - WP-10a: rw round-trip on one device (disk_rw_test &lt;dev&gt;)
- [fs_mount_test](fs_mount_test.md) - WP-10a: mkfs+mount FAT32 file round-trip (fs_mount_test &lt;dev&gt;)
- [nvme_test](nvme_test.md) - WP-10a: NVMe init/queues/read-write test
- [partition_test](partition_test.md) - WP-10a: MBR+GPT parse test (partition_test [dev])
- [real_hw_test](real_hw_test.md) - WP-10a: real-hardware detection report
- [virtio_blk_test](virtio_blk_test.md) - WP-10a: virtio-blk capacity + read/write test

## Files and the editor (18)
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
- [mount](mount.md) - list mounts, or mount a fs: mount &lt;type&gt; &lt;device&gt; &lt;path&gt;
- [mv](mv.md) - move/rename file (mv &lt;src&gt; &lt;dst&gt;)
- [pwd](pwd.md) - print working directory
- [rm](rm.md) - delete file (rm &lt;file&gt;)
- [rmdir](rmdir.md) - remove directory (rmdir &lt;path&gt;)
- [touch](touch.md) - create empty file (touch &lt;path&gt;)
- [tree](tree.md) - tree view of directory (tree [path])
- [umount](umount.md) - unmount (umount &lt;path&gt;)
- [write](write.md) - write text to file (write &lt;path&gt; &lt;text&gt;)

## Health monitoring (3)
<a id="shell-health"></a>

- [health](health.md) - system health monitor (health [watch|export])
- [health export](health_export.md) - write health JSON to /var/log/health.json
- [health watch](health_watch.md) - live health monitor, 1 s refresh (ENTER exits)

## Infrared (2)
<a id="shell-ir"></a>

- [ir](ir.md) - infrared status (CIR/SIR devices, carrier, counters)
- [irtest](irtest.md) - infrared self test (NEC/RC5/RC6/SIRC round trips)

## Kernel log (klog) (3)
<a id="shell-log"></a>

- [dmesg](dmesg.md) - show the kernel log ring (dmesg [N])
- [logcat](logcat.md) - filter the kernel log (logcat -l lvl -m mod -k key -t sec -n N [-f])
- [logctl](logctl.md) - control the kernel log (logctl status|level|enable|disable|clear|flush|rotate|e

## Network interface tests (18)
<a id="shell-nic-test"></a>

- [bcm57xx](bcm57xx.md) - BCM57xx NIC status
- [bcm57xx_test](bcm57xx_test.md) - WP-10b: BCM57xx presence/link/send-recv test
- [e1000e](e1000e.md) - e1000e NIC status
- [e1000e_test](e1000e_test.md) - WP-10b: e1000e init/link/send-recv test
- [igb](igb.md) - igb NIC status
- [igb_test](igb_test.md) - WP-10b: igb init/link/send-recv test
- [ixgbe](ixgbe.md) - ixgbe NIC status
- [ixgbe_test](ixgbe_test.md) - WP-10b: ixgbe presence/link/send-recv test
- [nic_rw_test](nic_rw_test.md) - WP-10b: send/recv correctness on all NICs
- [other_nic_test](other_nic_test.md) - WP-10b: legacy NICs (3c59x/nForce/AR81xx/Yukon)
- [rtl810x](rtl810x.md) - RTL810x NIC status
- [rtl810x_test](rtl810x_test.md) - WP-10b: RTL810x presence/link/send-recv test
- [rtl8125](rtl8125.md) - RTL8125 NIC status
- [rtl8125_test](rtl8125_test.md) - WP-10b: RTL8125 presence/link/send-recv test
- [rtl8139](rtl8139.md) - RTL8139 NIC status
- [rtl8139_test](rtl8139_test.md) - WP-10b: RTL8139 init/link/send-recv test
- [rtl8168](rtl8168.md) - RTL8168 NIC status
- [rtl8168_test](rtl8168_test.md) - WP-10b: RTL8168 presence/link/send-recv test

## Parallel port (4)
<a id="shell-pargame"></a>

- [gameport](gameport.md) - game port status (axes, buttons, calibration)
- [gameporttest](gameporttest.md) - game port self test (axis reads, buttons, calibration)
- [parallel](parallel.md) - parallel port status (IEEE 1284 modes, counters)
- [paralleltest](paralleltest.md) - parallel self test (SPP write, EPP cycles, ECP probe)

## Power management tests (6)
<a id="shell-power-test"></a>

- [halt_test](halt_test.md) - verify halt path (live halt via 'halt')
- [help_a_test](help_a_test.md) - verify help -a A-Z ordering
- [help_default_test](help_default_test.md) - verify default help A-Z ordering
- [help_wp_test](help_wp_test.md) - verify help -w group ordering
- [poweroff_test](poweroff_test.md) - real power-off attempt (QEMU exits)
- [reboot_test](reboot_test.md) - verify reboot path (live reset via 'reboot')

## SCSI (2)
<a id="shell-scsi"></a>

- [scsi](scsi.md) - SCSI status (HBAs, targets, capacity, blk names)
- [scsitest](scsitest.md) - SCSI self test (CDB goldens, parsers, live target)

## Sound tests (17)
<a id="shell-snd-test"></a>

- [ac97](ac97.md) - AC'97 status
- [ac97_test](ac97_test.md) - AC'97 init/play test
- [audio_rw_test](audio_rw_test.md) - play/stop round-trip on every card
- [es1370](es1370.md) - ES1370/1371 status
- [es1370_test](es1370_test.md) - ES1370 init/play test
- [hda](hda.md) - Intel HDA status
- [hda_test](hda_test.md) - Intel HDA init/codec/play test
- [play](play.md) - play a test tone (play [rate])
- [sample_rate_test](sample_rate_test.md) - 44100/48000 rate configuration test
- [sb16](sb16.md) - Sound Blaster 16 status
- [sb16_test](sb16_test.md) - SB16 init/play test
- [sound](sound.md) - list registered sound cards
- [usb_audio_test](usb_audio_test.md) - USB audio init/play test
- [usbaudio](usbaudio.md) - USB audio status
- [virtio_snd_test](virtio_snd_test.md) - virtio-snd init/play test
- [virtiosnd](virtiosnd.md) - virtio-snd status
- [volume](volume.md) - get/set volume (volume [0-100])

## Time sources (4)
<a id="shell-timer"></a>

- [hpet](hpet.md) - HPET: caps + counter + one-shot/periodic IRQ8 tests
- [pmtimer](pmtimer.md) - ACPI PM timer: read + PIT calibration + wrap tests
- [rtc](rtc.md) - CMOS RTC: time + status + UIE/PIE/alarm interrupt tests
- [tsc](tsc.md) - TSC: reads + PIT/HPET/PMTimer calibration + delay test

## OTA update tests (10)
<a id="shell-update-test"></a>

- [ab_partition_test](ab_partition_test.md) - A/B disk discovery + slots + flags (WP-10u)
- [real_update_test](real_update_test.md) - end-to-end update phase 1 (WP-10u)
- [update_check_test](update_check_test.md) - update manifest check with package fields (WP-10u)
- [update_download_test](update_download_test.md) - download the update package (WP-10u)
- [update_install_test](update_install_test.md) - install the package into slot B (WP-10u)
- [update_local_test](update_local_test.md) - offline update flow via cmd_update --local (WP-10u)
- [update_pkg_test](update_pkg_test.md) - gzip/DEFLATE + tar unit tests (WP-10u)
- [update_rollback_test](update_rollback_test.md) - boot flags + rollback (WP-10u)
- [update_status_test](update_status_test.md) - ota_update_get_status coverage (WP-10u)
- [update_verify_test](update_verify_test.md) - SHA256 verification of the package (WP-10u)

## USB tests (9)
<a id="shell-usb-test"></a>

- [usb](usb.md) - USB hosts/devices + class driver status
- [usb_core_test](usb_core_test.md) - USB core invariants + live transfers
- [usb_hotplug_test](usb_hotplug_test.md) - USB hot-plug (usb_hotplug_test [wait-new N|wait-leave N])
- [usb_hub_test](usb_hub_test.md) - USB external hub + children
- [usb_kbd_test](usb_kbd_test.md) - USB HID keyboard presence + injection
- [usb_mouse_test](usb_mouse_test.md) - USB HID mouse presence + event drain
- [usb_serial_test](usb_serial_test.md) - USB serial TX/RX round
- [usb_storage_test](usb_storage_test.md) - USB MSC drive capacity/rw/partitions
- [usbdev](usbdev.md) - USB device detail (usbdev [slot])

## Watchdog (1)
<a id="shell-wd"></a>

- [watchdog](watchdog.md) - watchdog status/control (hw TCO/WDAT + monitored slots + policy tests)

## Wireless (1)
<a id="shell-wireless"></a>

- [wifi](wifi.md) - wireless: status/scan/test/sdio (802.11 families + SDIO bus)

## Storage and block devices (4)
<a id="storage"></a>

- [abcfg](abcfg.md) - show the grub.cfg files abdisk/install write
- [abdisk](abdisk.md) - create a bootable A/B update disk (abdisk &lt;dev&gt;; destroys data)
- [grub-install](grub-install.md) - write the GRUB BIOS boot loader (grub-install &lt;dev&gt;)
- [install](install.md) - install the running system to a disk (install &lt;dev&gt;; destroys data)

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
