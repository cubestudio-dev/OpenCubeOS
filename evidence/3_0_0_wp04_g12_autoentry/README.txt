Open Cube OS 3.0.0-WP04 G12 - L0 -> L1 automatic entry verification
====================================================================

The G12 acceptance gate: after the L0 service kernel finishes its boot
chain, the machine hands itself to the default L1 entry with no
operator action.  Verified on BOTH firmwares, full boot captured:

  BIOS  (SeaBIOS El Torito):  g12_bios_boot.log
  UEFI  (OVMF pflash):        g12_uefi_boot.log

Sequence observed in both (identical mechanism, byte-identical
launcher lines):

  boot chain 1/4..4/4 OK
  [00:00:00.xxx] L0 ready, waiting for L1.......... OK   (kmain step 17)
    l1 launcher: booting 'ush' (ring 3, 29064 bytes)
  ... ush banner ...
  ush>                                                (auto-entry done)

Mechanism notes (unchanged since WP-03/G7 hardening, re-verified here):
  - The default entry is the RAW (uncompressed) ush array - the l1
    launcher's OC_L1_APP_DECL needs compile-time ELF pointers before
    any registry lookup / inflate can run (G11c gz design).
  - The launcher parks on core_sched_yield (NOT sti;hlt: the boot task
    must stay schedulable while ring-3 apps run - see l1_launcher.c).
  - g_l1_entry_running clears only when the DEFAULT entry's pid exits
    (G7 live-catch), so a background spawn cannot fake "shell back".
