# B-k10: the OCIRE UEFI track's single-'!' death - the raw entry was
# staged as the PE entry, and StartImage speaks a different ABI

Work package: WP-severe-fix-and-closure, item K-10.

## The symptom (as shipped)

tools/run_ocire_tests.py 7/10 - all three UEFI-track scenes (shell /
shutdown / reboot) stopped right after the OVMF boot dispatch with
the single byte '!' on the serial line and nothing else.  The BIOS
tracks passed.  The shipped KNOWN_ISSUES entry recorded "the UEFI
boot path needs its own work package" and dated the hang before the
current boot-stack release.

## The hunt

The '!' is not OVMF's and not OCIRE's - it is the ABI-violation
marker in OCBL's raw entry (ocbl/efi/ocbl_raw.c):

    if (magic != OCR_UEFI_HANDOFF_MAGIC || !h) {
        /* ABI violation - the serial marker is all we can honestly do */
        ... '!' on COM1, halt ...
    }

The chain as STAGED on the OCIRE ISO (tools/build_ocire_iso.sh):

    BOOTX64.EFI = build/ocbl/ocbl_x64.efi   <- the OCBL PE

The OCBL PE's entry RVA is ocbl_raw_entry - the POST-EBS entry whose
calling convention is the OCRMECL handoff ABI (RCX = handoff*,
RDX = OCR_UEFI_HANDOFF_MAGIC).  But OVMF's StartImage calls the PE
entry with the EFI ABI (RCX = image handle, RDX = system table), so
the magic check failed on entry and the whole UEFI track died on
the '!' - before a single OCBL banner byte.

The main ISO never had this: tools/build_iso.sh stages the chain
correctly -

    BOOTX64.EFI = ocrmecl_x64.efi  (OCRMECL performs every firmware
                                     call, finds /OCBL.EFI, loads it
                                     from buffer, does EBS, and
                                     jumps to ocbl_raw_entry with
                                     the handoff ABI)
    /OCBL.EFI    = ocbl_x64.efi
    El Torito    = OCRMECL.BIN (the composite embedding ocbl_bios.bin)

The OCIRE ISO was still staging the pre-OCRMECL layout (OCBL.BIN as
the El Torito image, the OCBL raw PE as BOOTX64.EFI) - the UEFI track
of the recovery ISO was never re-pointed at the OCRMECL chain.

## The fix

tools/build_ocire_iso.sh now stages the main ISO's chain:
BOOTX64.EFI = the OCRMECL PE, /OCBL.EFI = the OCBL PE for
LoadImage-from-buffer, and the El Torito BIOS image = the OCRMECL
composite (which embeds ocbl_bios.bin).  The packer call follows
(OCRMECL.BIN BOOTX64.EFI), G13 determinism seals unchanged.

## The acceptance

    make ocire-iso (repacked, 1,988,608 bytes) then
    tools/run_ocire_tests.py:

    [PASS] shell:help        (BIOS)      [PASS] shell:help        (UEFI)
    [PASS] shell:unknown      (BIOS)      [PASS] shell:unknown    (UEFI)
    [PASS] shell:backspace   (BIOS)      [PASS] shell:backspace   (UEFI)
    [PASS] shutdown:notice    (BIOS)      [PASS] shutdown:notice   (UEFI)
    [PASS] shutdown:qemu-exit(BIOS)      [PASS] shutdown:qemu-exit(UEFI)
    [PASS] reboot:notice      (BIOS)      [PASS] reboot:notice     (UEFI)
    [PASS] reboot:qemu-exit  (BIOS)      [PASS] reboot:qemu-exit  (UEFI)

    ===== OCIRE test battery summary =====
    total cases: 14, passed: 14, failed: 0

Transcripts: build/ocire/test-*.log (the three UEFI scenes now carry
the full OCRMECL -> OCBL -> OCIRE banner chain).

## KNOWN_ISSUES: K-10 closes - not "a UEFI boot path that needs its
own work package" but a mis-staged PE entry on the recovery ISO.
