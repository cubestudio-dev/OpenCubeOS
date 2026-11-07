# EXTENSIONS_WP10-tui.md

**Work Package**: WP-10-tui — TUI framework + hardware configuration +
wireless network + system information
**Total L1 Interfaces**: 0 new numbered items (the numbered L1 set stays
**closed at 1-138**); the 59 interfaces below are **driver-API style,
single-listed** per the WP-10-camera_misc precedent
**Status**: Complete
**License**: Apache 2.0

## 1. Contract

Every interface in this work package follows the four-part contract:

1. **Header declaration** — `tui/tui.h` (the single public header).
2. **Comment** — every declaration carries a one-line purpose comment
   in the header; the .c files document behavior and edge cases.
3. **Default implementation** — every interface has a real
   implementation in `tui/` (no stubs): the framework layers in
   tui_core/tui_win/tui_text/tui_input/tui_panel/tui_menu/tui_form.c,
   the applications in tui_hw/tui_net/tui_sysinfo.c.
4. **Documentation** — this file plus docs/INTERFACES.md (command
   table), docs/CONFIG.md (new keys), docs/TRY-IT.md (usage).

## 2. TUI framework interfaces (set 7.1)

| Interface | Purpose | Default impl |
|---|---|---|
| `tui_init()` | initialize backend + palette | tui_core.c |
| `tui_shutdown()` | restore terminal | tui_core.c |
| `tui_term_detect()` | terminal type/size/colors | tui_core.c |
| `tui_term_size()` | grid size in cells | tui_core.c |
| `tui_window_new()` | allocate window + cell buffer | tui_win.c |
| `tui_window_free()` | release window cells | tui_win.c |
| `tui_window_move()` | reposition (bounds-checked) | tui_win.c |
| `tui_window_resize()` | grow/shrink, buffer preserved | tui_win.c |
| `tui_window_refresh()` | paint window through backend | tui_win.c |
| `tui_window_clear()` | fill with background | tui_win.c |
| `tui_print()` | write at cursor | tui_text.c |
| `tui_print_at()` | move + write | tui_text.c |
| `tui_set_attr()` | set write attribute | tui_text.c |
| `tui_set_color()` | fg/bg pair | tui_text.c |
| `tui_getch()` | blocking key read | tui_input.c |
| `tui_getch_timeout()` | bounded key read | tui_input.c |
| `tui_mouse_enable()` | toggle mouse event path | tui_input.c |
| `tui_mouse_get()` | fetch queued mouse event | tui_input.c |
| `tui_panel_new()` | add window to Z-stack | tui_panel.c |
| `tui_panel_free()` | drop panel | tui_panel.c |
| `tui_panel_top()` | raise to top | tui_panel.c |
| `tui_menu_new()` | menu from item array | tui_menu.c |
| `tui_menu_free()` | release menu | tui_menu.c |
| `tui_form_new()` | form from field array | tui_form.c |
| `tui_form_free()` | release form | tui_form.c |

Extended framework surface (same contract, same header):

| Interface | Purpose |
|---|---|
| `tui_window_border/title/scrollok/scroll/subwin/touch` | window dressing |
| `tui_addch/printw/print_at/move/getyx/getbegyx/getmaxyx` | text output |
| `tui_attron/attroff/curs_set` | attributes/cursor |
| `tui_nodelay/color_supported/color_pairs_max/init_pair/pair_attr/color_set_256/color_set_rgb` | colors |
| `tui_panel_bottom/show/hide/move/hidden/window` + `tui_update_panels` | panels |
| `tui_menu_post/unpost/driver/current/set_current/set_colors` | menus |
| `tui_form_post/unpost/driver/field_buffer/set_field` | forms |
| `tui_status_line` | shared one-line status bar |

## 3. Hardware configuration interfaces (set 7.2)

| Interface | Purpose | Data source |
|---|---|---|
| `tui_hw_init()` | prerequisites check | tui_started |
| `tui_hw_cpu()` | CPU page | CPUID leaves 0/1/0x80000002-4 |
| `tui_hw_mem()` | memory page | mem_pmm_get_stats |
| `tui_hw_storage()` | storage page | driver_block_get_device |
| `tui_hw_network()` | network page | driver_nic_get_device + link |
| `tui_hw_sound()` | sound page | driver_snd_get_device/caps |
| `tui_hw_usb()` | USB page | driver_usb_get_device/print_state |
| `tui_hw_pci()` | PCI page | driver_pcie_get/count |
| `tui_hw_input()` | input page | keyboard/i2chid probe state |
| `tui_hw_camera()` | camera page | camera_count/camera_get |
| `tui_hw_wireless()` | wireless page | wl_device_get/count |
| `tui_hw_timer()` | timers page | core_timer/HPET/PMT/TSC reads |
| `tui_hw_save()` | persist TUI keys | lib_config_write |
| `tui_hw_load()` | read TUI keys back | lib_config_read |

`tui_hw_main()` (menu-driven app) and `tui_hw_run_page(name)` (direct
page) are the two entry shapes behind the `tui-hw` command.

## 4. Wireless network interfaces (set 7.3)

| Interface | Purpose | Data source |
|---|---|---|
| `tui_wifi_init()` | prerequisites | wl_device_count |
| `tui_wifi_scan()` | active scan | wl_scan_start + result table |
| `tui_wifi_list()` | results TUI | wl_scan_result_get |
| `tui_wifi_connect()` | associate | wl_connect (open-system only) |
| `tui_wifi_disconnect()` | leave network | wl_disconnect |
| `tui_wifi_status()` | state panel | wl_device_t state |
| `tui_bt_init()` | transport check | bt_hci_present |
| `tui_bt_scan()` | HCI inquiry | bt_hci_inquiry_start/results |
| `tui_bt_pair()` | ACL connection | bt_hci_create_connection |
| `tui_bt_unpair()` | disconnect | bt_hci_disconnect |
| `tui_bt_services()` | remote name + SDP pointer | bt_hci_remote_name |

Honesty notes: the 802.11 core authenticates open-system only —
WEP/WPA1/WPA2-advertising networks are refused with -1 (capability and
RSN/WPA IE flags are honored).  QEMU has no 802.11 device model, so a
live scan reports zero results and the link stays DOWN; on real
hardware the same code path runs against the actual radio.

## 5. System information interfaces (set 7.4)

| Interface | Purpose | Data source |
|---|---|---|
| `tui_sysinfo_init()` | prerequisites | tui_started |
| `tui_sysinfo_overview()` | version/build/uptime/resources | OC_RELEASE_VERSION macros, core_timer, pmm, block |
| `tui_sysinfo_processes()` | task table + kill | core_sched_task_info_get, core_kthread_destroy |
| `tui_sysinfo_logs()` | boot log pages | lib_log_ring_count/get |
| `tui_sysinfo_monitor()` | live sampling | pmm + core_timer_ticks deltas |
| `tui_sysinfo_power()` | reboot/poweroff/S3 | core_power_* |
| `tui_sysinfo_update()` | OTA check/rollback | ota_update_check_pkg, ota_update_rollback |
| `tui_sysinfo_time()` | clock prefs form | lib_config_write |
| `tui_sysinfo_help()` | shortcut reference | static text |

## 6. Shell commands

| Command | Purpose | Behavior |
|---|---|---|
| `tui` | full-screen main menu | menu -> HW/Net/SysInfo/Help |
| `tui-test` | self-test suite | prints input/expected/actual/verdict per check; exit code = failures |
| `tui-hw [page]` | hardware config | page = CPU/Memory/Storage/Network/Sound/USB/PCI/Input/Camera/Wireless/Timers; no arg = menu |
| `tui-net` | wireless app | WiFi scan/list/status + Bluetooth manager |
| `tui-sysinfo [page]` | system info | page = overview/processes/logs/monitor/power/update/time/help; no arg = menu |

## 7. Related surface changes in this batch

- **WiFi-NIC bridge** (`wl_nic_bridge_register`): the wireless STA now
  registers with the driver_nic framework as `wl0`; net_poll drains
  every present framework NIC.  No carrier -> honest DOWN link.
- **Resident USB poll thread**: `driver_usb_poll_start()` now runs from
  kmain (the thread body previously lived only inside the USB test
  command file and was never started).
- **Socket syscalls SYS 103-109** + `l1_socket/l1_net_*` wrappers:
  see EXTENSIONS_WP08a.md and INTERFACES.md; socket ids are net-stack
  table ids, not process file descriptors.
- **access_ok_str bounded** to 16 pages (was unbounded user-window
  scan); the always-false "sanity" line is gone.
- **Real audio volume paths**: SB16 mixer (read-back verified), UAC1
  Feature Unit SET_CUR, virtio-snd honest failure.
- **lib_log boot-log ring** (64 lines) + `lib_log_ring_count/get`.
- **make test**: host test suite runner (`tools/run_host_tests.py`),
  17 PASS / 2 SKIP-FIXTURE / 0 failed at integration time.

## 8. New configuration keys (docs/CONFIG.md section)

- `tui.backend` — `fb` (default) or `serial` (ANSI COM1 terminal)
- `tui.clock_24h` — sysinfo time page, 1/0
- `tui.tz_offset` — sysinfo time page, hours
- `snd.volume` — sound app applies it through the driver volume path
