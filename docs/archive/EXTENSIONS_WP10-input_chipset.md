<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# WP-10-input_chipset — input devices + chipset (L0 extension)

Batch: WP-10-input_chipset (first drop, no suffix).
Scope: five functional domains wired into the L0 kernel and exercised
live in QEMU on both BIOS (SeaBIOS) and UEFI (OVMF) boots.

## 1. Domains delivered

1. **Touchpad (I2C HID)** — `drivers/input/driver_input_i2chid.c/.h`
   - i2c-hid transport (HID descriptor read, RESET/SET_POWER commands)
   - HID report-descriptor parser (multitouch digitizer subset)
   - report decoder (type-B, 1 finger per report + contact count)
   - gesture engine: TAP / MOVE / DOWN / UP / two-finger SCROLL /
     two-finger PINCH (scale %) / three-finger SWIPE
   - event ring queue + `driver_input_touchpad_get_event()` API
   - QEMU ships no I2C-HID model: the pipeline is covered end to end by
     the synthetic `touchtest` fixtures (plus the new host regression
     `tests/host_i2chid_touch_test.c`); `touchpad` reports the binding
     honestly when no device is present.

2. **PCI / PCIe** — `drivers/pci/driver_pcie.c/.h` + `kernel/arch/x86_64/arch_lapic.c/.h`
   - config-space access via CF8/CFC and, when the MCFG table provides
     it, PCIe MMCONFIG extended config space
   - device cache scan + capability walk (MSI / MSI-X / PCIe caps)
   - BAR sizing + assignment (64-bit BAR handling)
   - MSI/MSI-X message programming through the xAPIC delivery path
   - LAPIC: enable (MSR 0x1B base correctly masked to bits 12..35),
     EOI, self-IPI (destination shorthand 01 = SELF), vector dispatch
     table 0x30..0x6F fed by `arch_msi_dispatch`

3. **ACPI** — `drivers/acpi/driver_acpi.c/.h`, `driver_acpi_aml.c/.h`,
   `driver_acpi_power.c/.h`, `kernel/arch/x86_64/arch_acpi_s3.S`
   - RSDP from the Multiboot2 tags 14/15 (with fallback scan), RSDT /
     XSDT / FADT / MADT / MCFG / HPET parsing
   - minimal AML interpreter: namespace (DefScope/DefDevice/DefMethod),
     integers/strings/buffers/packages, NameOp, method invocation,
     `_HID`/`_STA`/`_ADR`/`\_Sx` evaluation, EisaId decoding
   - power management: S5 shutdown and S3 suspend-to-RAM through the
     FADT PM1a_CNT + the `\_S5`/`\_S3` packages + the FACS waking
     vector; S3 resume through a 32-bit trampoline
     (`arch_acpi_s3.S`) that re-issues lgdt/lidt and jumps back into
     the kernel; SCI (IRQ9) registration + GPE dispatch
   - device enumeration (`acpidev`, canonical `\_SB_.PCI0...` paths)

4. **SMBus** — `drivers/i2c/driver_smbus.c/.h`
   - protocol layer over the I2C core: quick / byte / byte-data /
     word-data / block write + block read (self-describing
     `[count, payload]` layout) / process-call honestly unsupported
   - device scan 0x03..0x77 with EEPROM classification + first-bytes
     dump; NACK-on-absent must stay an error (self-checked)

5. **I2C** — `drivers/i2c/driver_i2c.c/.h`, `driver_i2c_i801.c/.h`,
   `driver_i2c_dw.c/.h`
   - bus registry + dispatch (zero-length transfer = the QUICK probe,
     valid by design)
   - Intel PIIX4 (8086:7113, config 0x90 base) and ICH-class i801
     (class 0x0C05, config 0x20 base) host controllers; block
     transfers run with AUX_CTL.AUX_BLK (bit 1, offset 0x0D) so the
     BLKDAT window auto-increments, matching ICH silicon and QEMU
   - DesignWare controller backend with a register self-test (present
     on real hardware; honestly skipped when absent)
   - 10-bit addressing supported on the DesignWare backend (documented
     as unsupported on the i801-class SMBus)

## 2. New shell commands (16; none removed — interface freeze holds)

```
acpi  acpidev  acpitest  acpipow
msitest  pcidump  pcibars  pcie
i2cdetect  i2cget  i2cset  i2ctest
smbus  smbustest
touchpad  touchtest
```

Total command count: 176 -> 192 (regression baseline updated in
`tools/run_fix4_regression.py`; all 176 previous commands verified
still present via `help -a`).

## 3. Extension-interface contract (frozen four-part rule)

Every new capability ships all four parts:

| Part | Where |
|------|-------|
| Header declaration | `drivers/*/driver_*.h`, `kernel/arch/x86_64/arch_lapic.h`, `shell/shell_cmds_chipset.h` |
| Comment contract | each declaration carries its semantics (units, error codes, ownership) |
| Default implementation | the driver `.c` files; ACPI-less platforms fall back to the legacy ports in `core_power.c` |
| Documentation | this file + `docs/INTERFACES.md` + `help` descriptions |

## 4. Live-boot defects found and fixed by the QEMU gate

1. **I2C core register wipe** (`driver_i2c.c`): `driver_i2c_register()`
   memset() the CALLER's bus struct (wiping `bus->xfer`) before copying
   it into the registry — every bus was registered with `xfer == NULL`
   and every transfer bailed at the gate before reaching the hardware.
   SMBus scan saw zero devices, every i2cget/i2cset "NACK"ed with an
   empty last-error.
2. **QUICK probes rejected in the core** (`driver_i2c.c`):
   `wbuf==NULL && rbuf==NULL` (the zero-length QUICK address probe,
   the only way a scan can discover devices) was rejected outright;
   only non-zero lengths now require a buffer.
3. **Word writes misrouted** (`driver_i2c_i801.c`): a 3-byte
   write (cmd + 2 data) — the WORD protocol — fell into the block
   path; word write/read round-trip now uses PROT_WORD_DATA.
4. **Block transfers without AUX_BLK** (`driver_i2c_i801.c`): AUX_CTL
   is offset 0x0D (not 0x08) and AUX_BLK is bit 1 (value 0x02, not
   0x01), set BEFORE any BLKDAT access; without it the BLKDAT window is
   a single latch (QEMU model + ICH silicon) and block reads repeat one
   byte while block writes never reach the bus.
5. **Block read dropped its count** (`driver_i2c_i801.c`): the byte
   count in DAT0 was never surfaced, leaving the payload shifted by
   one; block reads now deliver `rbuf[0] = count, payload at rbuf[1..]`
   (the layout the SMBus layer documents).
6. **LAPIC base mask** (`arch_lapic.c`): IA32_APIC_BASE bits 0..11
   (reserved + BSP flag + enable) were kept in the base, pointing
   `g_lapic` at 0xFEE00900 — reads silently returned 0 (fake "APIC id
   0"), writes hit illegal offsets (ESR bit 7, the exact 0x80 QEMU
   reported) and the self-IPI never went out. Base mask is now
   0xFFFFF000 (+ hi nibble).
7. **LINT0/LINT1 masking killed PIC interrupts** (`arch_lapic.c`): on
   the pc platform the 8259 PIC delivers every legacy line through
   LINT0 as ExtINT; masking it in `arch_lapic_init` silenced the serial
   console immediately after init. LINT0/LINT1 are now left as the
   firmware configured them.
8. **Self-IPI destination shorthand** (`arch_lapic.c`): shorthand 11 is
   "all EXCLUDING self" — on the single-BSP platform nobody received
   the message (QEMU `info lapic`: ICR=0xc4050, ESR=0). Shorthand 01
   (self) is used now; `msitest` fires exactly once.
9. **HID Input-item flags** (`driver_input_i2chid.c`): the
   Data/Constant and Array/Variable flags live in the item's DATA byte,
   not the item header's low bits (those encode the data size). Every
   Data,Var,Abs field classified as Array -> BUTTON and the decoder saw
   no tip/contact-id/x/y at all.
10. **Fixture descriptor page switch** (`driver_input_i2chid.c`): the
    Contact Count usage must be declared on the Digitizer page — the
    fixture now re-declares `Usage Page (Digitizers)` before it,
    exactly like real Windows-Precision-touchpad descriptors do after
    the GenDesk X/Y section.
11. **Device paths lacked the namespace root** (`driver_acpi_aml.c`):
    `acpidev` printed `_SB_.PCI0` without the canonical `\` prefix;
    paths now render `\_SB_.PCI0` per the ACPI canonical form.

Also updated honestly: the self-test harnesses (`smbustest`,
`touchtest`) now print per-item PASS/FAIL verdicts; the gesture fixture
sequences lift every finger they press (type-B is one finger per
report) so `count == 0` is actually reached and the gestures classify.

## 5. Verification evidence

- Build: 0 errors 0 warnings (`-Werror`), clean rebuild from scratch.
- WP-10-input_chipset E2E (`tools/wp10_chipset_e2e.py`): **28/28 on
  BIOS** and **28/28 on UEFI** (`/home/z/work/wp10e-evidence/e2e_bios.log`,
  `e2e_uefi.log`), covering every command above plus `lspci`,
  `help -w`, `help -a`, `uname`.
- Full regression (`tools/run_fix4_regression.py`): **28/28**
  (`regression.log`), including cryptotest 4/4 NIST vectors, fstest p2
  ALL PASS, g6_test full chain failed tests = 0, G8TEST 27/27.
- dhtest: **5/5 correctness tests passed** (TCG).
- SSH E2E (`tools/ssh_mainstream_test.py`): **5/5 ALL PASS**.
- HTTPS E2E (`tools/run_fix4_https_e2e.py`): **7/7 checks passed**.
- Host regression for the touchpad pipeline:
  `gcc -O2 -I. -Ikernel -Ikernel/core -Ikernel/lib -Ikernel/arch/x86_64
  -Idrivers/input -Idrivers/i2c -Idrivers/acpi
  tests/host_i2chid_touch_test.c kernel/lib/lib_string.c -o /tmp/i2chid_test`
  — ALL PASS.
- QEMU evidence archived under `/home/z/work/wp10e-evidence/`.

## 6. Honest notes

- `driver_smbus_process_call()` returns "unsupported" by design: the
  i801-class backend exposes no atomic process-call path, and a
  decomposed word-write + word-read would CORRUPT EEPROM contents
  (the swap leg is not a store on real devices).
- QEMU has no I2C-HID device model, so `touchpad` reports the binding
  honestly and the gesture pipeline is proven by `touchtest` + the
  host regression instead of a live device.
- The DesignWare controller has no QEMU model either; its register
  self-test runs when hardware is present and is skipped honestly
  otherwise.
- The regression command-count baseline moved 176 -> 192 (16 new
  commands); the two-way `help -a` check in the batch report covers the
  no-removal side of the freeze.
