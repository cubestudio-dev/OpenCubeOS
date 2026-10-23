<!-- SPDX-License-Identifier: Apache-2.0 -->
# The L1 surface, group by group

The numbered 162, mapped to where each group's declarations live.
(Regenerate the raw tables any time: `make api-docs` ->
INTERFACES.md - every declaration with file and line.)

## Group 1-57 - the WP-09 foundation

The original extension surface: framebuffer access
(`l1_ext_fb_get_info`), the font-engine registration protocol
(`l1_ext_register_font_engine` + the `screen_font_engine` ops
struct), the console hook (`l1_ext_set_console_hook` /
`l1_ext_console_hook`), configuration
(`l1_ext_config_read/write/get_all`), the update chain entry
(`l1_ext_check_update` + async), power (shutdown/suspend/halt/reboot
- suspend returns 0 after a live S3 resume), and the shell bridge
(`l1_ext_shell_register_command_ex`, list_commands_a_z /
by_wp). All in `l1/l1_ext.h`.

## +8 - block driver items (WP-10a)

`driver_block_register / read / write / flush` (+ set_ops),
`driver_block_ahci_init`, `driver_block_nvme_init`,
`driver_block_ata_dma_init` (each taking the discovered pci_dev).
`drivers/block/driver_block_blk.h`.

## +13 - NIC driver items (WP-10b)

`driver_nic_register / send / recv / link_status / get_mac` plus the
per-family init entry points (e1000e, igb, ixgbe, rtl8139, rtl8168,
rtl8125, rtl810x, bcm57xx). `drivers/nic/driver_nic.h`.

## +7 - A/B update items (WP-10u)

The ota_ab family: slots, flags, verify, install.
`kernel/ota/ota_ab.h`.

## +26 - sound driver items (WP-10c)

`driver_snd_register / play / stop / set_rate / set_volume /
get_caps` + probe_all/make_tone + the per-hw init entries (hda, ac97,
sb16, es1370, virtio) + the lookup/listing helpers.
`drivers/snd/driver_snd.h`.

## +11 - dynamic loading (WP-08b surface)

`elf_load_dynamic`, `ldso_run`, `so_load/so_unload`,
`symbol_resolve`, `dlsym_impl/dlopen_impl/dlclose_impl`,
`reloc_apply`, `elf_get_needed`. `l1/l1_wp8b.h`.

## +7 - shell built-ins and jobs (WP-08cd)

`shell_run`, `shell_register_builtin / unregister_builtin`,
`tool_register / tool_list`, `job_create / job_list / job_control`.
`l1/l1_wp8cd.h`.

## +9 - process/fd/memory API (WP-08a)

`sys_proc_exec / sys_proc_exit`, `pipe_create`, `fd_dup / fd_dup2`,
`signal_send / signal_return`, `sys_mem_mmap / munmap / mprotect /
brk`, `sys_chdir` - the Unix-shaped surface `userprogs/` builds on.
`l1/l1_wp8a.h`.

## +8 - TUI extension items

The first TUI declarations in `tui/tui_ext.h`.

## +16 - watchdog/logging/health (WP-12)

klog 6 (`klog_write / set_level / set_level_enabled / flush /
rotate / filter_set`, `kernel/lib/lib_klog.h` lines 72-106) +
watchdog 5 (`register / start / feed / stop / set_timeout`,
`kernel/core/core_watchdog.h` lines 74-83) + health 5 (`get_cpu /
get_mem / get_tasks / get_io / export`, `kernel/core/core_health.h`
lines 76-85).

## Outside the numbering (documented, real)

- WP-09-fix5: 5 config/check-update interfaces.
- rule-9 self-hosting: 5 interfaces (the in-kernel GRUB payload
  behind install/abdisk/grub-install).
- The full TUI widget surface beyond the numbered 8 (37 declarations
  total in `tui/tui_ext.h`).
