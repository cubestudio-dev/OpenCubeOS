3.0.0-WP03 G3 - registry mechanism + extension live test evidence
=================================================================

What this proves (WP-03 spec: "add a driver/service = add one file in
its own folder + one identity declaration, no global table edit"):

1. REGISTER-CLASS BOOT CALLS DELETED
   - 4 wireless family register calls in boot/kmain.c
     (driver_iwlwifi/rtlwifi/ath/brcm_register)
   - 5 USB class-driver registration calls in boot/kmain.c
     (driver_usb_hid_init, driver_usb_msc_init, driver_usb_serial_init,
      driver_usb_audio_class_register, driver_camera_uvc_register)
   - 1 SDIO client registration call chain (driver_brcm_register ->
     driver_sdio_register_driver)
   - 1 BT USB transport registration (bt_usb_init from the HCI bring-up)
   - public prototypes removed from 8 headers; the fill helpers
     (driver_usb_register_driver, wl_register_family,
      driver_sdio_register_driver) are core-private now (static)

2. SELF-DECLARATION COVERAGE (all collected from .oc_drvs at boot)
   - "usb" family: 8 class drivers (hid-kbd hid-mouse usb-msc cdc-acm
     ftdi-serial usb-audio btusb uvc) - collected inside
     driver_usb_probe_all() BEFORE host enumeration
   - "wireless" family: 7 families (iwlwifi rtlwifi ath9k ath10k
     ath11k b43 brcmfmac) - collected inside driver_wireless_init()
   - "sdio" family: 1 client driver (brcmfmac-sdio) - collected inside
     driver_sdhci_init() before host probing
   - (block/nic/snd families were already self-declared since 2.0.0)

3. EXTENSION LIVE TEST (the two exttest files were added, then removed)
   - src/L0/drivers/wireless/driver_wl_exttest.c: ONE file with ONE
     OC_DRIVER_DECL("wireless", "exttest", ...) - nothing else changed
   - src/L0/drivers/usb/driver_usb_exttest.c: ONE file with ONE
     OC_DRIVER_DECL("usb", "exttest-cls", ...) - nothing else changed
   - build: 0 errors 0 warnings (incremental, KERNEL_DIRS wildcard
     picked both files up with zero Makefile edit)
   - runtime (BIOS boot, exttest_bios_wifi.log):
       "usb: class driver collected: exttest-cls"
       "wireless families: 8" (was 7) + "exttest" in the family list
       + "dev7 exttest bus=auto vid=0000 did=0000 state=down"
   - the exttest files are NOT in the tree anymore: they are proof
     artifacts, not drivers (honest hardware support rule)

4. HONEST ORDER-CHANGE NOTE
   The UVC class driver and the btusb transport are now registered
   BEFORE host enumeration (they used to register after the camera /
   bluetooth core inits).  Strictly more complete: devices present at
   power-on bind immediately; no UVC/BT hardware in the standard test
   battery, zero observable difference there.

5. REMAINING *_register* FUNCTIONS (declared, kept on purpose)
   All of them are runtime operation/mechanism APIs, NOT module
   identity registration: IRQ/timer/panic/fault handler hooks,
   keyboard/console input hooks, netfilter/fs hooks, runtime device
   instances (camera_register, scsi_register, wl_register_device,
   i2c/gameport/parallel/ir register), BT profile/recv callbacks,
   wl_nic_bridge_register (STA->NIC bridge bring-up operation),
   core_syscall_register (syscall table fill), font_register (CJK
   font instances), watchdog_register (watchdog clients),
   tool_register (wp8cd runtime tool set), arch exception test
   handlers.  Zero of these appear in the L1SVC interface surface
   (l1svc_defs.h has no register op).

6. BUILD-SYSTEM HAZARD CAUGHT DURING THE TEST (documented, not hit)
   Removing the exttest source files is NOT enough for an incremental
   rebuild: make only relinks when a listed prerequisite is newer, so
   the stale exttest .o files stayed linked into the ELF (verified:
   strings found "exttest" in the binary).  Fixed by deleting the
   stale .o files + forcing the relink; the shipped tree is verified
   clean ("strings opencube.elf | grep -c exttest" = 0, wifi reports
   7 families again).  Same class as the WP-02 F5 stale-object bug -
   clean builds (CI) are immune, incremental local builds are not.
   final_build.log = the verified-clean rebuild log (0 warnings).
