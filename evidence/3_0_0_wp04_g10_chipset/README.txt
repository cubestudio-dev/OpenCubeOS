3.0.0-WP04 G10 - chipset family migrated to L1 (diag ops 27-39)
================================================================

13 commands (v2.2.0 recovery/shell_cmds_chipset.c bodies, verbatim)
in services/diag, one L1 app folder each:

  op  command      app folder
  --  -----------  -----------------
  27  acpi         src/L1/acpi/
  28  acpidev      src/L1/acpidev/
  29  acpipow      src/L1/acpipow/
  30  acpitest     src/L1/acpitest/
  31  pcidump      src/L1/pcidump/      (arg: <bus:dev.fn>)
  32  pcibars      src/L1/pcibars/
  33  pcie         src/L1/pcie/         (print_caps walk inlined so the
                                         text lands in the out buffer)
  34  i2cdetect     src/L1/i2cdetect/
  35  i2cget        src/L1/i2cget/       (arg: <bus> <addr> [reg])
  36  i2cset        src/L1/i2cset/       (arg: <bus> <addr> <reg> <val>)
  37  i2ctest       src/L1/i2ctest/
  38  smbus        src/L1/smbus/
  39  smbustest    src/L1/smbustest/

With ops 1-8 (G8) and 9-26 (G9) this completes the diag category:
all 39 frozen op numbers are live.

Tests (QEMU piix4 SMBus + 8 EEPROMs @0x50-0x57, per the 2.2.0 e2e rig):
bios_acpi_pci.log     acpi (RSDP/tables/FADT/DSDT/FACS/MCFG lines),
                      acpidev (54 devices, 32-truncation honored),
                      acpitest PASS, acpipow (S1-S5/SCI/FACS/resume),
                      pcibars (6 devices with sized BARs), pcie (legacy
                      caps + honest MCFG-absent line)
bios_pcidump_i2c.log  pcidump usage/bad-location/no-such-device branches
                      + 00:01.3 legacy dump; i2cget 0 0x50 -> 0x00 and
                      0 0x10 -> "i2cget: error (NACK/device error)";
                      i2ctest PASS (PIIX4 base 0x0700); smbus 8 EEPROMs;
                      smbustest PASS
bios_i2cdetect_rw.log i2cdetect full 0x03-0x77 grid (0x50-0x57 found);
                      i2cset 0 0x50 0x00 0x5a -> written; i2cget ... 0x00
                      -> 0x5a (write/read round trip)
uefi_smoke.log        OVMF pflash boot: acpi/acpidev/acpitest/pcibars/
                      i2ctest/smbustest 6/6 PASS

Build: 0 errors 0 warnings; 261 embedded programs (509,824 bytes).
