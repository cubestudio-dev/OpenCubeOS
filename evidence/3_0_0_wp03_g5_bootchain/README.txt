3.0.0-WP03 G5 - boot dependency chain + architecture direction gate
=====================================================================

1. THE BOOT CHAIN (kmain's five phases, documented in the file header
   and printed as "boot chain N/4" markers - asserted in order by the
   boot logs in this directory, BIOS + UEFI):
     phase 1  kernel core mechanisms (console/log, IDT/PIC, PIT,
              PMM/VMM/heap, scheduler/sync, userspace, registry,
              ACPI/PCIe/watchdog-hw/chipset, RTC/HPET)
     phase 2  drivers + driver-free services (VFS/ramfs need no
              driver; block/USB/wireless/SD/BT/camera/misc families
              need no service - the halves are independent; encoding
              + the dynamic loader are demand-initialized)
     phase 3  driver-dependent services (FAT/exFAT/ext4 + config +
              A/B after the block family; wireless after fs-mounted
              firmware; net after NIC; snd after the snd family)
     phase 4  interfaces ready (crash dump + crashlog + soft wdt,
              health monitor, update check, slot confirm, the OCIRE
              embed boot-confirmation)
     phase 5  "L0 ready, waiting for L1" (the terminal state)

2. THE ARCHITECTURE DIRECTION GATE (tools/check_deps.py, new):
   core/ must not depend on drivers/ or services/; drivers/ must not
   depend on services/.  The first run found 23 violations; all are
   fixed (check_deps.txt = 0 violations, 181 files scanned):
   - the platform console stack moved drivers/display ->
     core/console (it is the mb2-framebuffer platform text layer, not
     a PCI device family; the input-routing pair screen_console_in/
     screen_serial_in moved on to drivers/input, their real family)
   - the klog RING stayed in core (lib_klog.c, mechanism); the
     on-disk channel moved to services/klog/klog_file.c and installs
     itself as the ring's new emit hook (klog_set_emit_hook)
   - crash capture moved core -> services/crash (its dump path needs
     fs + power + rtc = service deps); the PANIC entry points are
     now core-owned slots (arch_exceptions: kernel_panic /
     kernel_panic_frame / the crashlog recorder) that services/crash
     installs at boot (crash_install_panic_handlers) - until then an
     honest fallback prints and halts
   - lib_fspath moved core/lib -> services/fs (fs path glue)
   - lib_config: the key-value mechanism stayed in core; the fs
     storage backend (mount /etc, stat, read, write) moved to
     services/fs/fs_config_backend.c and is installed by kmain
     (lib_config_init_with); plain lib_config_init() = in-memory
   - the signal ABI (numbers, handler type, sigaction) moved from
     services/sysapi/core_syscall.h into core_usermode.h (the
     process mechanism owns its signal state)
   - stale includes removed (core_sched->core_power,
     driver_acpi_power->net_core); driver_cmos_rtc's alarm test
     reads its OWN registers instead of the time service's reader

3. REGRESSION STATE after the surgery (all green):
   build 0 errors 0 warnings | host tests 17/0/2 | supervised
   battery 3/3 | WP-01 dual 11/11 | WP-02 shell 4/4 | UEFI boot with
   all four phase markers in order (bootchain_uefi.log)
