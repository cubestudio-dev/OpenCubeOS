WP-tui-input-encoding-hardening 4.6: the extended sweep - input/encoding/display findings
===================================================================================
Scope: the full input / encoding / display chains of the boot loader,
the recovery OS and the main OS, re-audited on the final tree (every
probe re-run after the 4.5 boot-loader changes).

--- re-verification of the standing matrices (all PASS on this tree) ---

* 4.2 space matrix, six cells: ocire/serial, ocire/ps2, ocos/serial,
  ocos/ps2, ocos/uefi, ocire/uefi(standalone ISO) - all PASS
  (sweep/s2_*.log).  The uefi cells: the main ISO boots OCOS as the
  OVMF default; the OCIRE cell uses the standalone OCIRE ISO (the
  documented workaround for the OVMF menu-input limitation, below).
* 4.3 UTF-8 chain, 7/7 PASS (sweep/s3_utf8.rerun.log): multi-byte
  receive+echo, cat byte-exact, whole-character backspace with the
  2-cell wipe on the wire, 80-cell wrap without splitting a glyph,
  plus the VGA screendumps.

--- chain A: input ---

A-1 [FIXED in 4.6] BIOS OCBL menu, serial keying: the getkey polled
    int 14h AH=03/AH=02 for the serial line - under QEMU/SeaBIOS
    that route returned "no data" forever (SeaBIOS's int 14h
    implementation is minimal), so the menu was serial-dead while
    the PS/2 path worked.  Live-caught during the sweep: 'B'+CR over
    COM1 during the countdown selected nothing (the default booted),
    sendkey down/ret worked.  Fix: read the 16550 directly (LSR 0x3FD
    bit 0, RBR 0x3F8) - the same contract the EFI flavor's getkey
    and BOTH flavors' serial OUTPUT already use; the int 14h
    middleman was the only inconsistent hop.  Verified live: the
    serial 'B'+CR now selects OCIRE and boots it to the ocire>
    prompt (sweep/menu_serial_keying_fixed.log), the PS/2 path
    unchanged (sweep/menu_ps2_keying.log).

A-2 [FIXED in 4.6] UEFI OCBL, our own IDT under UEFI: the 4.5 fix
    installed the blanket-iretq IDT in BOTH flavors; under OVMF that
    KILLS the firmware's interrupt-driven driver ecosystem (its
    timer events stop firing, so the PS/2 and serial-terminal input
    drivers never poll and ReadKeyStroke never returns a key - the
    menu became unnavigable and even sendkey died).  Caught by the
    sweep BEFORE shipping: the diagnostic build showed zero
    ReadKeyStroke hits with the table installed; with it removed the
    firmware ecosystem runs again.  Fix: the IDT install is now
    BIOS-flavor only (under UEFI the inherited IDT is the firmware's
    own proper 64-bit table - exactly the guarantee H1 wants).
    Registered as the standing lesson: a boot loader must not
    blanket-mask interrupts it does not own while boot services live.

A-3 [REGISTERED - KNOWN_ISSUES] OVMF menu input starvation (B5-5,
    revised diagnosis): even with the firmware's IDT intact, no
    input source delivers to the OCBL-EFI menu while boot services
    are live: OVMF's terminal driver drains the 16550 RBR on its own
    timer and the consumed byte never resurfaces through ConIn (the
    4.5 'A'/'B'/CR acceptance stays in the code - harmless, and
    correct if a firmware ever does resurface the byte); the PS/2
    sendkey path shows the same starvation (ReadKeyStroke returned
    no key in a 10-second window in two independent diagnostic
    runs).  The root cause sits in the OVMF/QEMU firmware-event
    interaction (a polled UEFI application that never enters
    WaitForEvent gets no driver progress in this configuration), not
    in OCBL's poll code.  Impact: under OVMF the menu always
    auto-boots the default entry (OCOS) after the 10 s countdown -
    functional but not user-selectable.  Workarounds: the default IS
    the OS; the standalone OCIRE ISO defaults to OCIRE; both payload
    shells (after ExitBootServices, the kernel's own drivers) have
    fully working serial + PS/2 input (the 4.2 six-cell matrix).
    Candidate future fix: a WaitForEvent-based menu loop (pump the
    firmware event system while waiting) - out of this WP's scope,
    registered.

--- chain B: encoding ---

B-1 [re-verified] the 4.3 UTF-8 chain 7/7 (above).
B-2 [registered in 4.4, unchanged] the reported COM1 mojibake is
    host-terminal decoding (windows-936/GBK reading UTF-8 output);
    the built-in serial_encoding=gbk tee serves GBK terminals.

--- chain C: display ---

C-1 [verified] both firmware flavors' menu renders use standard
    ANSI cursor addressing (the same sequences GRUB's serial menu
    emits - 4.4's raw captures); the BIOS VGA and UEFI ConOut paths
    both render the three-entry menu with the inverse-video
    selection bar (captured in the sweep logs).
C-2 [verified] the 4.3 VGA screendumps (echo/cat/backspace/wrap)
    re-captured on this tree.

--- summary ---

Findings registered: 3 (A-1, A-2 fixed in 4.6; A-3 registered).
Re-verified standing matrices: 4.2 six cells + 4.3 seven checks.
No further defects found in the encoding or display chains on this
tree.
