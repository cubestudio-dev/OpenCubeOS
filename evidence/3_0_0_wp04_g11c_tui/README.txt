Open Cube OS 3.0.0-WP04 G11c - TUI revival (tui x9) evidence
============================================================

What this batch delivers
------------------------
The full-screen TUI family is back, revived verbatim from v2.2.0:
  - services/tui/ = v2.2.0 tui/ (25 files, ~11k lines: core/windows/
    menus/forms/input/panels, desktop + 5 desktop applets, ext lab,
    the 9 command bodies) + chinese/tui_chinese + the three Chinese
    twin pages (tui_cn_hw/net/sysinfo), 30 files total.
  - New service category 20 ("tui", folder rule), ops 1-9 = the nine
    v2.2.0 commands (tui/tui-test/tui-hw/tui-net/tui-sysinfo/tui-time/
    tui-drv/tui-sys/tui-ext; tui-drv keeps its v2.2.0 alias-of-tui-hw
    role, own op number).
  - L1 apps: src/L1/tui{,-test,-hw,-net,-sysinfo,-time,-drv,-sys,-ext}
  - Adaptations (declared, same class as G11a sshd):
      * tui_getch/tui_test: core_sched_yield() -> sti;hlt park
        (SVC-frame rule - switching tasks from the int 0x80 dispatch
        frame leaves interrupts off; v2.2.0 ran the TUI in L0 shell
        context where yield was safe).
      * The desktop shell bridge (shell_execute/_captured/_enum) ->
        tui_exec.c: spawn the L1 app named by the first word + captured
        retire wait (the G11a sshd exec pattern), enumeration over the
        OC_USERPROGS registry.  Kernel-side spawns are argv-less, so
        command-bar lines run their first word in default form.
      * tui_chinese.c consumes the G11b ime_engine (the IME bodies
        live in services/display now; syllable_str additionally
        exported as ime_syllable_str).
      * tui_ext_init() (builtin themes/hotkeys) moved from the v2.2.0
        kmain call into the tui service init slot.
  - ALL NINE ops route DIRECT (live rendering + live keyboard): the
    first cut had tui-test on the capture, but its verdict block is
    ~4.4 KiB and the 4096-byte SVC result truncated the summary -
    the direct route restores the v2.2.0 live stream exactly.

Identity-window relief #2 (found and fixed in this batch)
---------------------------------------------------------
The TUI engine (+152 KiB .text) pushed the link past the 0x400000
guard again.  The remaining elephant was the embedded program
registry itself: 274 apps as raw byte arrays = 527,504 B of .rodata.
  - gen_userprogs.py now gzips every app individually (273 streams;
    ush stays raw - the l1 launcher's OC_L1_APP_DECL needs
    compile-time ELF pointers before any lookup can run).
  - core_usermode.c inflates each entry LAZILY into a per-app heap
    cache on first exec (core_userprog_inflate; boot order guarantees
    post-heap use: the launcher spawns ush raw, everything else goes
    through lookup from ring 3).
  - Registry struct: {name, gz, raw, gz_len, raw_size}.
  - Direct-symbol consumers converted to lookups: interp table
    (ld.so -> "ld_so" name walk in-core), l1_wp8b ldd selftest,
    tui_test's priv_test payload check.  kmain's 70-line block of
    dead v2.x-era userprog_* extern declarations deleted (they had
    zero uses and the symbols no longer exist).
  - Result: elf 2216016 -> 2077880 B; the link clears the boundary
    with headroom for the remaining batches.

F7 kernel live bug (found and fixed in this batch)
--------------------------------------------------
The `acpi` status op #PF'd: cr2=0x414d7f, rip=driver_acpi_rsdp_revision.
Root cause: the multiboot2 ACPI tags carry an RSDP COPY in loader
memory; driver_acpi_init kept g_rsdp pointing straight at that copy.
The G11b/G11c inflate caches grew the heap into those loader pages,
the stale pointer read heap data -> #PF.  (v2.2.0 has the same
dormant pattern - its heap never reached the tags.)  Fix: copy the
RSDP struct into kernel storage at init (rsdt/xsdt still point at
the real BIOS tables in ROM).  Regression: acpi/acpitest/acpidev/
acpipow/pcidump/stats/tui-test all green after the fix.

Test battery (this folder)
--------------------------
tui_test_serial.txt      tui-test: 93/93 checks passed, all green
                         (full [PASS] verdict block, BIOS)
interactive_battery.txt   the 8 interactive pages: launch -> screendump
                         -> Esc -> prompt return; 7 distinct md5s,
                         tui_drv == tui_hw byte-identical (the alias
                         proof); VLM read the desktop: title bar
                         "Open Cube OS Desktop", F1 Files / F2 Processes
                         / F3 Control Panel / F4 Editor / F5 Terminal /
                         F6 System Info / F7 Chinese Input / F9 Command
                         Bridge / Fx Extensions, status bar key hints
uefi_smoke.txt            OVMF: tui-test 93/93 + run hello + echo +
                         ime-test PASS + chinesetest 77/77
f7_rsdp_fix_regression.txt  acpi (RSDP rev0 BOCHS/5 tables/FADT) +
                         acpitest PASS + family regression after the fix
tui_*.png (8)            the page screenshots (800x600 PPM->PNG)

Also re-verified post-gz: run hello, ldd so_test (interp through the
gz registry), l1test 1-7, stats, health, dmesg, uname, sleep, echo,
font-test, encoding-test, ime-test, chinesetest - all green on BIOS;
0 errors 0 warnings; 274 programs; ISO 12351488 B dual El Torito.

Migration status after G11c: 235/251 commands live in L1 (all
C-level done); remaining batches: G12 auto-entry verification,
G13 full matrix, G14 dossier, G15 release chain.
