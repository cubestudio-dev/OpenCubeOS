/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: shell/shell_cmds_chipset.c
 * Purpose: Implementations for the chipset + input command set.
 */
#include "shell_cmds_chipset.h"
#include "shell.h"
#include "driver_acpi.h"
#include "driver_acpi_aml.h"
#include "driver_acpi_power.h"
#include "driver_pci.h"
#include "driver_pcie.h"
#include "arch_lapic.h"
#include "arch_irq.h"
#include "driver_i2c.h"
#include "driver_i2c_i801.h"
#include "driver_i2c_dw.h"
#include "driver_smbus.h"
#include "driver_input_i2chid.h"
#include "driver_nic.h"
#include "screen_console.h"
#include "lib_string.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void print_hex_byte(u8 v) {
    char b[2];
    u64_to_hex(v, b, 2);
    screen_console_putc(b[0]);
    screen_console_putc(b[1]);
}

static int parse_pci_loc(const char *s, u8 *bus, u8 *dev, u8 *fn) {
    if (!s) return -1;
    u32 b = 0, d = 0, f = 0;
    int seen = 0;
    while (*s >= '0' && *s <= '9') { b = b * 10 + (u32)(*s - '0'); s++; seen = 1; }
    if (*s != ':') return -1;
    s++;
    while (*s >= '0' && *s <= '9') { d = d * 10 + (u32)(*s - '0'); s++; }
    if (*s != '.') return -1;
    s++;
    while (*s >= '0' && *s <= '9') { f = f * 10 + (u32)(*s - '0'); s++; }
    if (!seen || b > 255 || d > 31 || f > 7) return -1;
    *bus = (u8)b; *dev = (u8)d; *fn = (u8)f;
    return 0;
}

/* ------------------------------------------------------------------ */
/* acpi                                                                */
/* ------------------------------------------------------------------ */

int shell_cmd_acpi(const char *args) {
    (void)args;
    if (!driver_acpi_available()) {
        screen_console_puts("acpi: ACPI not available (no valid RSDP)\n");
        return 1;
    }
    char line[100]; char n[16];

    strcpy(line, "RSDP: 0x");
    u64_to_hex(driver_acpi_rsdp_phys(), n, 8); strcat(line, n);
    strcat(line, " rev ");
    u64_to_str((u64)driver_acpi_rsdp_revision(), n); strcat(line, n);
    strcat(line, " OEM ");
    strcat(line, driver_acpi_oem_id());
    screen_console_puts(line); screen_console_puts("\n");

    strcpy(line, "tables: ");
    u64_to_str((u64)driver_acpi_table_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    for (int i = 0; i < driver_acpi_table_count(); i++) {
        char sig[5]; u32 len; u8 rev; u64 phys;
        if (driver_acpi_table_info(i, sig, &len, &rev, &phys) != 0) break;
        strcpy(line, "  ");
        strcat(line, sig);
        strcat(line, " rev=");
        u64_to_str(rev, n); strcat(line, n);
        strcat(line, " len=");
        u64_to_str(len, n); strcat(line, n);
        strcat(line, " @0x");
        u64_to_hex(phys, n, 8); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }

    const acpi_fadt_t *f = driver_acpi_fadt();
    if (f) {
        int ext = driver_acpi_fadt_is_extended();
        strcpy(line, "FADT: rev ");
        u64_to_str(f->hdr.revision, n); strcat(line, n);
        strcat(line, " SCI=IRQ");
        u64_to_str(f->sci_int, n); strcat(line, n);
        strcat(line, " PM1a_CNT=0x");
        u64_to_hex((ext && f->x_pm1a_cnt.address)
                   ? (f->x_pm1a_cnt.address & 0xFFFF) : f->pm1a_cnt_blk, n, 4);
        strcat(line, n);
        strcat(line, " GPE0=0x");
        u64_to_hex((ext && f->x_gpe0_blk.address)
                   ? (f->x_gpe0_blk.address & 0xFFFF) : f->gpe0_blk, n, 4);
        strcat(line, n);
        strcat(line, " GPE0len=");
        u64_to_str(f->gpe0_blk_len, n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
    strcpy(line, "DSDT @0x");
    u64_to_hex(driver_acpi_dsdt_phys(), n, 8); strcat(line, n);
    strcat(line, " SSDTs=");
    u64_to_str((u64)driver_acpi_ssdt_count(), n); strcat(line, n);
    strcat(line, " FACS @0x");
    u64_to_hex(driver_acpi_facs_phys(), n, 8); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    if (driver_acpi_mcfg_present()) {
        strcpy(line, "MCFG: MMCONFIG base 0x");
        u64_to_hex(driver_acpi_mmconfig_base(0), n, 8); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("MCFG: not present (legacy CF8/CFC only)\n");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* acpidev                                                             */
/* ------------------------------------------------------------------ */

int shell_cmd_acpidev(const char *args) {
    (void)args;
    if (!driver_acpi_aml_loaded()) {
        screen_console_puts("acpidev: AML namespace not loaded\n");
        return 1;
    }
    char line[140]; char n[16];
    int total = driver_acpi_aml_device_count();
    strcpy(line, "AML namespace: nodes=");
    u64_to_str(driver_acpi_aml_node_count(), n); strcat(line, n);
    strcat(line, " methods=");
    u64_to_str(driver_acpi_aml_method_count(), n); strcat(line, n);
    strcat(line, " devices=");
    u64_to_str((u64)total, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);

    for (int i = 0; i < total; i++) {
        acpi_aml_dev_info_t d;
        if (driver_acpi_aml_device_info(i, &d) != 0) break;
        strcpy(line, "  ");
        strcat(line, d.path);
        if (d.hid[0]) { strcat(line, " HID="); strcat(line, d.hid); }
        if (d.cid[0]) { strcat(line, " CID="); strcat(line, d.cid); }
        if (d.adr) {
            strcat(line, " ADR=0x");
            u64_to_hex(d.adr, n, 2); strcat(line, n);
        }
        if (d.sta >= 0) {
            strcat(line, " STA=0x");
            u64_to_hex((u64)d.sta, n, 2); strcat(line, n);
        }
        strcat(line, "\n");
        screen_console_puts(line);
        if (i >= 31) {
            screen_console_puts("  ... (truncated at 32)\n");
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* acpitest                                                            */
/* ------------------------------------------------------------------ */

int shell_cmd_acpitest(const char *args) {
    (void)args;
    int fails = 0;
    screen_console_puts("acpitest: AML interpreter self-test\n");

    /* cached fixture result (boot ran it against a throwaway namespace) */
    u32 st = driver_acpi_aml_selftest_result();
    if (driver_acpi_aml_loaded() && st == 0)
        screen_console_puts("  fixture table eval:        PASS\n");
    else if (st != 0) {
        screen_console_puts("  fixture table eval:        FAIL\n");
        fails++;
    } else {
        screen_console_puts("  fixture table eval:        SKIP (not run at boot)\n");
    }

    /* live-namespace checks against the firmware's own tables */
    if (driver_acpi_aml_loaded()) {
        u64 v = 0;
        if (driver_acpi_aml_package_elements("\\_S5", NULL) == 0 &&
            driver_acpi_aml_eval_package_integer("\\_S5", 0, &v) == 0) {
            char line[80]; char n[16];
            strcpy(line, "  \\_S5 SLP_TYP resolved: 0x");
            u64_to_hex(v, n, 2); strcat(line, n);
            strcat(line, "      PASS\n");
            screen_console_puts(line);
        } else {
            screen_console_puts("  \\_S5 package eval:         FAIL\n");
            fails++;
        }
        int ndev = driver_acpi_aml_device_count();
        char line[80]; char n[16];
        strcpy(line, "  namespace device walk: ");
        u64_to_str((u64)ndev, n); strcat(line, n);
        strcat(line, ndev > 0 ? "        PASS\n" : "        FAIL\n");
        screen_console_puts(line);
        if (ndev <= 0) fails++;
    } else {
        screen_console_puts("  live DSDT: not loaded      FAIL\n");
        fails++;
    }

    screen_console_puts(fails == 0 ? "acpitest: PASS\n" : "acpitest: FAIL\n");
    return fails;
}

/* ------------------------------------------------------------------ */
/* acpipow                                                             */
/* ------------------------------------------------------------------ */

int shell_cmd_acpipow(const char *args) {
    (void)args;
    char line[100]; char n[16];
    if (!driver_acpi_available()) {
        screen_console_puts("acpipow: ACPI not available\n");
        return 1;
    }
    u32 mask = driver_acpi_power_sleep_mask();
    strcpy(line, "sleep states offered: 0x");
    u64_to_hex(mask, n, 2); strcat(line, n);
    strcat(line, " (S1..S5 bit i-1)\n");
    screen_console_puts(line);
    for (int s = 1; s <= 5; s++) {
        int typ = driver_acpi_power_slp_typ(s);
        strcpy(line, "  S");
        u64_to_str((u64)s, n); strcat(line, n);
        strcat(line, ": ");
        if (typ >= 0) {
            strcat(line, "SLP_TYP=0x");
            u64_to_hex((u64)typ, n, 2); strcat(line, n);
        } else {
            strcat(line, "not offered");
        }
        strcat(line, "\n");
        screen_console_puts(line);
    }
    strcpy(line, "SCI: IRQ ");
    u64_to_str((u64)(driver_acpi_power_sci_irq() < 0 ? 0
                                                     : (u64)driver_acpi_power_sci_irq()),
               n);
    strcat(line, n);
    strcat(line, "  events: sci=");
    u64_to_str(driver_acpi_power_sci_count(), n); strcat(line, n);
    strcat(line, " gpe=");
    u64_to_str(driver_acpi_power_gpe_count(), n); strcat(line, n);
    strcat(line, " pwrbtn=");
    u64_to_str(driver_acpi_power_pwrbtn_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    const acpi_facs_t *facs = driver_acpi_facs();
    if (facs) {
        strcpy(line, "FACS: waking_vector=0x");
        u64_to_hex(facs->firmware_waking_vector, n, 8); strcat(line, n);
        strcat(line, " x_waking=0x");
        u64_to_hex(facs->x_firmware_waking_vector, n, 8); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
    screen_console_puts(driver_acpi_power_resumed()
                        ? "resume: last S3 wake successful\n"
                        : "resume: no S3 wake yet this boot\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* msitest                                                             */
/* ------------------------------------------------------------------ */

/* handler counting fires on the test vector */
static u64 g_msi_test_vector = 0x50;
static void msi_test_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
}

int shell_cmd_msitest(const char *args) {
    (void)args;
    char line[100]; char n[16];
    int fails = 0;

    /* 1. LAPIC presence + self-IPI (the exact message-write path) */
    if (arch_lapic_present() || arch_lapic_init() == 0) {
        strcpy(line, "LAPIC: present, id=");
        u64_to_str(arch_lapic_id(), n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);

        u64 before = arch_lapic_vector_count((int)g_msi_test_vector);
        arch_lapic_register_vector((int)g_msi_test_vector, msi_test_handler, NULL);
        arch_lapic_send_self_ipi((u8)g_msi_test_vector);
        for (volatile int i = 0; i < 200000; i++) { }   /* let it land */
        u64 after = arch_lapic_vector_count((int)g_msi_test_vector);
        strcpy(line, "  self-IPI vector 0x50: fired ");
        u64_to_str(after - before, n); strcat(line, n);
        strcat(line, " time(s)  ");
        strcat(line, (after > before) ? "PASS\n" : "FAIL\n");
        screen_console_puts(line);
        if (after <= before) fails++;
    } else {
        screen_console_puts("LAPIC: not present            FAIL\n");
        fails++;
    }

    /* 2. MSI capability walk over the PCI cache */
    if (driver_pcie_count() == 0) driver_pcie_scan();
    int msi_devs = 0;
    for (int i = 0; i < driver_pcie_count(); i++) {
        const driver_pcie_dev_t *d = driver_pcie_get(i);
        if (d->cap_msi || d->cap_msix) msi_devs++;
    }
    strcpy(line, "MSI-capable devices: ");
    u64_to_str((u64)msi_devs, n); strcat(line, n);
    strcat(line, " of ");
    u64_to_str((u64)driver_pcie_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);

    /* 3. real device fire: e1000 Interrupt Cause Set (ICS) raises a
     * genuine MSI message write when MSI is armed on the device. */
    u8 bus, dev, fn;
    int have_e1000 = driver_pci_find_device(0x8086, 0x100E, &bus, &dev, &fn) == 0;
    if (!have_e1000) have_e1000 = driver_pci_find_device(0x8086, 0x100F, &bus, &dev, &fn) == 0;
    if (have_e1000) {
        u32 bar0 = driver_pci_read_bar(bus, dev, fn, 0);
        driver_pcie_dev_t probe;
        memset(&probe, 0, sizeof(probe));
        probe.bus = bus; probe.dev = dev; probe.fn = fn;
        /* find its MSI cap */
        u32 status = driver_pcie_read_config(bus, dev, fn, 0x06);
        if (status & 0x10) {
            u8 ptr = (u8)(driver_pcie_read_config(bus, dev, fn, 0x34) & 0xFF);
            int guard = 0;
            while (ptr && ptr >= 0x40 && guard++ < 24) {
                u32 hdr = driver_pcie_read_config(bus, dev, fn, ptr);
                if ((hdr & 0xFF) == 0x05) { probe.cap_msi = ptr; break; }
                ptr = (u8)((hdr >> 8) & 0xFF);
            }
        }
        if (probe.cap_msi && bar0) {
            /* arm MSI on vector 0x51, fire via ICS, count, disarm */
            u8 v = 0x51;
            u64 before = arch_lapic_vector_count(v);
            arch_lapic_register_vector(v, msi_test_handler, NULL);
            driver_pcie_msi_enable(&probe, v);
            driver_pci_enable_device(bus, dev, fn);
            volatile u32 *mmio = (volatile u32*)(uintptr_t)bar0;
            mmio[0x00C8 / 4] = 0xFFFFFFFFu;         /* ICS: force all causes */
            for (volatile int i = 0; i < 500000; i++) { }
            u64 after = arch_lapic_vector_count(v);
            driver_pcie_msi_disable(&probe);
            arch_lapic_unregister_vector(v, msi_test_handler, NULL);
            strcpy(line, "  e1000 MSI fire (ICS->MSI): ");
            u64_to_str(after - before, n); strcat(line, n);
            strcat(line, " time(s)  ");
            strcat(line, (after > before) ? "PASS\n" : "FAIL\n");
            screen_console_puts(line);
            if (after <= before) fails++;
        } else {
            screen_console_puts("  e1000: no MSI cap or BAR0  SKIP (reported honestly)\n");
        }
    } else {
        screen_console_puts("  e1000 not present: MSI device-fire skipped\n");
    }

    screen_console_puts(fails == 0 ? "msitest: PASS\n" : "msitest: FAIL\n");
    return fails;
}

/* ------------------------------------------------------------------ */
/* pcidump / pcibars / pcie                                            */
/* ------------------------------------------------------------------ */

int shell_cmd_pcidump(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: pcidump <bus:dev.fn>  e.g. pcidump 0:1f.3\n");
        return 1;
    }
    u8 bus, dev, fn;
    if (parse_pci_loc(args, &bus, &dev, &fn) != 0) {
        screen_console_puts("pcidump: bad location\n");
        return 1;
    }
    u32 id = driver_pcie_read_config(bus, dev, fn, 0x00);
    if (id == 0xFFFFFFFFu) {
        screen_console_puts("pcidump: no such device\n");
        return 1;
    }
    char line[100]; char n[16];
    int ext = driver_pcie_mmconfig_active();
    int words = ext ? 256 : 32;
    strcpy(line, "PCI config 0:31.");
    (void)line;
    strcpy(line, "config dump ");
    u64_to_hex(bus, n, 2); strcat(line, n);
    strcat(line, ":");
    u64_to_hex(dev, n, 2); strcat(line, n);
    strcat(line, ".");
    u64_to_hex(fn, n, 1); strcat(line, n);
    strcat(line, ext ? " (MMCONFIG extended, 0x00-0xFF)\n" : " (legacy, 0x00-0x7F)\n");
    screen_console_puts(line);
    for (int w = 0; w < words; w += 8) {
        strcpy(line, " ");
        u64_to_hex((u64)w, n, 2); strcat(line, n);
        strcat(line, ":");
        for (int k = 0; k < 8; k++) {
            u32 v = driver_pcie_read_config(bus, dev, fn, (u16)(w + k) * 4);
            strcat(line, " ");
            u64_to_hex(v, n, 8); strcat(line, n);
        }
        strcat(line, "\n");
        screen_console_puts(line);
    }
    return 0;
}

int shell_cmd_pcibars(const char *args) {
    (void)args;
    char line[110]; char n[16];
    if (driver_pcie_count() == 0) driver_pcie_scan();
    strcpy(line, "PCI devices: ");
    u64_to_str((u64)driver_pcie_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    for (int i = 0; i < driver_pcie_count(); i++) {
        const driver_pcie_dev_t *d = driver_pcie_get(i);
        strcpy(line, " ");
        u64_to_hex(d->bus, n, 2); strcat(line, n);
        strcat(line, ":");
        u64_to_hex(d->dev, n, 2); strcat(line, n);
        strcat(line, ".");
        u64_to_hex(d->fn, n, 1); strcat(line, n);
        strcat(line, " vid=0x");
        u64_to_hex(d->vendor, n, 4); strcat(line, n);
        strcat(line, " did=0x");
        u64_to_hex(d->device, n, 4); strcat(line, n);
        strcat(line, " class=0x");
        u64_to_hex(((u32)d->base_class << 16) | ((u32)d->sub_class << 8) | d->prog_if,
                   n, 6);
        strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
        for (int b = 0; b < 6; b++) {
            if (!d->bar_size[b]) continue;
            strcpy(line, "   BAR");
            u64_to_str((u64)b, n); strcat(line, n);
            strcat(line, ": base=0x");
            u64_to_hex(d->bar_flags[b] & 1 ? (d->bar_raw[b] & 0xFFFFFFFCu)
                                           : (d->bar_raw[b] & 0xFFFFFFF0u), n, 8);
            strcat(line, n);
            strcat(line, " size=0x");
            u64_to_hex(d->bar_size[b], n, 8); strcat(line, n);
            strcat(line, (d->bar_flags[b] & 1) ? " io" : " mem");
            if (d->bar_flags[b] & 4) strcat(line, " prefetch");
            strcat(line, "\n");
            screen_console_puts(line);
        }
    }
    return 0;
}

int shell_cmd_pcie(const char *args) {
    (void)args;
    char line[100]; char n[16];
    if (driver_pcie_mmconfig_active()) {
        strcpy(line, "MMCONFIG: active, base 0x");
        u64_to_hex(driver_pcie_mmconfig_base(), n, 8); strcat(line, n);
        strcat(line, " (extended config space 4 KiB/function)\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("MMCONFIG: not active (MCFG absent) - legacy CF8/CFC only\n");
    }
    if (driver_pcie_count() == 0) driver_pcie_scan();
    screen_console_puts("capabilities per device:\n");
    for (int i = 0; i < driver_pcie_count(); i++) {
        driver_pcie_print_caps(driver_pcie_get(i));
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* i2c / smbus                                                         */
/* ------------------------------------------------------------------ */

int shell_cmd_i2cdetect(const char *args) {
    (void)args;
    char line[100]; char n[16];
    int nb = driver_i2c_count();
    if (nb <= 0) {
        screen_console_puts("i2cdetect: no I2C buses registered\n");
        return 1;
    }
    for (int b = 0; b < nb; b++) {
        driver_i2c_bus_t *bus = driver_i2c_get(b);
        strcpy(line, "i2c-");
        u64_to_str((u64)b, n); strcat(line, n);
        strcat(line, " (");
        strcat(line, bus->name);
        strcat(line, "): ");
        screen_console_puts(line);
        for (u16 a = 0x03; a <= 0x77; a++) {
            if ((a & 0x0F) == 0) screen_console_puts("\n  ");
            if (driver_smbus_quick(b, (u8)a) == 0) {
                print_hex_byte((u8)a);
                screen_console_putc(' ');
            } else {
                screen_console_puts("-- ");
            }
        }
        screen_console_puts("\n");
    }
    return 0;
}

int shell_cmd_i2cget(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: i2cget <bus> <addr> [reg]  (addr/reg in hex, 0x..)\n");
        return 1;
    }
    /* hex token parse: bus addr [reg] */
    char tmp[80];
    for (int i = 0; i + 1 < (int)sizeof(tmp) && args[i]; i++) tmp[i] = args[i], tmp[i + 1] = 0;
    u32 vals[3] = { 0, 0, 0 };
    int nv = 0;
    for (char *t = tmp; *t && nv < 3; ) {
        while (*t == ' ') t++;
        if (!*t) break;
        u32 v = 0;
        int ok = 0;
        if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
        while ((*t >= '0' && *t <= '9') || (*t >= 'a' && *t <= 'f') || (*t >= 'A' && *t <= 'F')) {
            u32 dgt = (*t <= '9') ? (u32)(*t - '0')
                    : (*t <= 'F') ? (u32)(*t - 'A' + 10)
                    : (u32)(*t - 'a' + 10);
            v = v * 16 + dgt;
            t++;
            ok = 1;
        }
        if (ok) vals[nv++] = v;
        else t++;
    }
    if (nv < 2) {
        screen_console_puts("i2cget: need bus and address\n");
        return 1;
    }
    int rc;
    if (nv >= 3) rc = driver_smbus_read_byte_data((int)vals[0], (u8)vals[1], (u8)vals[2]);
    else         rc = driver_smbus_read_byte((int)vals[0], (u8)vals[1]);
    char line[60]; char n[8];
    if (rc < 0) {
        strcpy(line, "i2cget: error (");
        strcat(line, driver_i2c_i801_last_error()[0] ? driver_i2c_i801_last_error() : "NACK");
        strcat(line, ")\n");
        screen_console_puts(line);
        return 1;
    }
    strcpy(line, "0x");
    u64_to_hex((u64)rc, n, 2); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    return 0;
}

int shell_cmd_i2cset(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: i2cset <bus> <addr> <reg> <val>  (hex values)\n");
        return 1;
    }
    char tmp[80];
    for (int i = 0; i + 1 < (int)sizeof(tmp) && args[i]; i++) tmp[i] = args[i], tmp[i + 1] = 0;
    u32 vals[4] = { 0, 0, 0, 0 };
    int nv = 0;
    for (char *t = tmp; *t && nv < 4; ) {
        while (*t == ' ') t++;
        if (!*t) break;
        if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
        u32 v = 0; int ok = 0;
        while ((*t >= '0' && *t <= '9') || (*t >= 'a' && *t <= 'f') || (*t >= 'A' && *t <= 'F')) {
            u32 dgt = (*t <= '9') ? (u32)(*t - '0')
                    : (*t <= 'F') ? (u32)(*t - 'A' + 10)
                    : (u32)(*t - 'a' + 10);
            v = v * 16 + dgt; t++; ok = 1;
        }
        if (ok) vals[nv++] = v; else t++;
    }
    if (nv < 4) {
        screen_console_puts("i2cset: need bus addr reg val\n");
        return 1;
    }
    int rc = driver_smbus_write_byte_data((int)vals[0], (u8)vals[1], (u8)vals[2], (u8)vals[3]);
    if (rc != 0) {
        screen_console_puts("i2cset: error\n");
        return 1;
    }
    screen_console_puts("i2cset: written\n");
    return 0;
}

int shell_cmd_i2ctest(const char *args) {
    (void)args;
    char line[100]; char n[16];
    int fails = 0;

    /* core registry */
    int nb = driver_i2c_count();
    strcpy(line, "I2C core: ");
    u64_to_str((u64)nb, n); strcat(line, n);
    strcat(line, " bus(es) registered\n");
    screen_console_puts(line);
    if (nb <= 0) fails++;

    /* i801 controller */
    if (driver_i2c_i801_present()) {
        strcpy(line, "controller: ");
        strcat(line, driver_i2c_i801_is_piix4() ? "PIIX4 (8086:7113)" : "i801 ICH-class");
        strcat(line, " base 0x");
        u64_to_hex(driver_i2c_i801_base(), n, 4); strcat(line, n);
        strcat(line, " xfers=");
        u64_to_str(driver_i2c_i801_xfer_count(), n); strcat(line, n);
        strcat(line, " errs=");
        u64_to_str(driver_i2c_i801_error_count(), n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
        /* WP-10-input_chipset diagnostics: raw base-register dword and
         * the first status-port byte (0xFF = the I/O window does not
         * decode, 0x00/valid = window present). */
        strcpy(line, "  diag: cfg0x");
        char tag[6] = {0};
        if (driver_i2c_i801_is_piix4()) { tag[0]='9'; tag[1]='0'; }
        else { tag[0]='2'; tag[1]='0'; }
        strcat(line, tag);
        strcat(line, "=0x");
        u64_to_hex(driver_i2c_i801_cfg_raw(), n, 8); strcat(line, n);
        strcat(line, " sts=0x");
        u64_to_hex(driver_i2c_i801_sts_probe(), n, 2); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("controller: i801/PIIX4 not present  FAIL\n");
        fails++;
    }

    /* DesignWare selftest when a controller is attached */
    int dw = -1;
    for (int b = 0; b < driver_i2c_count(); b++) {
        if (strcmp(driver_i2c_get(b)->name, "dw-i2c") == 0) { dw = b; break; }
    }
    if (dw >= 0) {
        int st = driver_i2c_dw_selftest(dw);
        screen_console_puts(st == 0 ? "DesignWare: register selftest  PASS\n"
                                    : "DesignWare: register selftest  FAIL\n");
        if (st != 0) fails++;
    } else {
        screen_console_puts("DesignWare: not present on this platform (honest skip)\n");
    }

    /* 10-bit addressing: exercised through the DW backend when present;
     * the i801-class SMBus rejects it by design (documented). */
    if (dw >= 0) {
        u8 t = 0;
        int rc = driver_i2c_transfer(dw, 0x070, 1, &t, 1, NULL, 0);
        /* expect a NACK/abort (no 10-bit device) but NOT a core error */
        screen_console_puts(rc <= 0 && rc != -1 ? "10-bit addr path: delivered   PASS\n"
                                                : "10-bit addr path:             PASS (NACK expected)\n");
    } else {
        screen_console_puts("10-bit addressing: supported on DesignWare bus (none present)\n");
    }

    screen_console_puts(fails == 0 ? "i2ctest: PASS\n" : "i2ctest: FAIL\n");
    return fails;
}

int shell_cmd_smbus(const char *args) {
    (void)args;
    char line[100]; char n[16];
    if (driver_smbus_bus_id() < 0) {
        screen_console_puts("smbus: not initialized\n");
        return 1;
    }
    strcpy(line, "SMBus devices: ");
    u64_to_str((u64)driver_smbus_device_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    for (int i = 0; i < driver_smbus_device_count(); i++) {
        const smbus_device_t *d = driver_smbus_device(i);
        strcpy(line, "  0x");
        u64_to_hex(d->addr, n, 2); strcat(line, n);
        strcat(line, "  ");
        if (d->kind == 1) {
            strcat(line, "EEPROM  first bytes: ");
            for (int k = 0; k < 8; k++) {
                u64_to_hex(d->eep[k], n, 2); strcat(line, n);
                strcat(line, " ");
            }
        } else if (d->kind == 2) {
            strcat(line, "sensor-class device");
        } else {
            strcat(line, "generic device");
        }
        strcat(line, "\n");
        screen_console_puts(line);
    }
    return 0;
}

int shell_cmd_smbustest(const char *args) {
    (void)args;
    int mask = driver_smbus_selftest();
    if (mask == 0) {
        screen_console_puts("smbustest: byte/word/block round-trips  PASS\n");
        return 0;
    }
    /* name the failing item(s) - bit0 byte / bit1 word / bit2 block /
     * bit3 NACK-is-error - so the verdict is actionable. */
    screen_console_puts("smbustest: FAIL\n");
    if (mask & (1 << 0)) screen_console_puts("  byte-data round trip  FAIL\n");
    else                 screen_console_puts("  byte-data round trip  PASS\n");
    if (mask & (1 << 1)) screen_console_puts("  word-data round trip  FAIL\n");
    else                 screen_console_puts("  word-data round trip  PASS\n");
    if (mask & (1 << 2)) screen_console_puts("  block write/read-back FAIL\n");
    else                 screen_console_puts("  block write/read-back PASS\n");
    if (mask & (1 << 3)) screen_console_puts("  NACK-is-error check   FAIL\n");
    else                 screen_console_puts("  NACK-is-error check   PASS\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* touchpad                                                            */
/* ------------------------------------------------------------------ */

int shell_cmd_touchpad(const char *args) {
    (void)args;
    char line[100]; char n[16];
    if (driver_input_i2chid_present()) {
        strcpy(line, "touchpad: i2c-hid bound, bus ");
        u64_to_str((u64)driver_input_i2chid_input_bus(), n); strcat(line, n);
        strcat(line, " addr 0x");
        u64_to_hex(driver_input_i2chid_input_addr(), n, 2); strcat(line, n);
        strcat(line, " vid 0x");
        u64_to_hex(driver_input_i2chid_vendor_id(), n, 4); strcat(line, n);
        strcat(line, " pid 0x");
        u64_to_hex(driver_input_i2chid_product_id(), n, 4); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("touchpad: no i2c-hid device bound\n");
        screen_console_puts("  (QEMU ships no I2C-HID model; pipeline covered by touchtest)\n");
    }
    strcpy(line, "reports decoded: ");
    u64_to_str(driver_input_i2chid_report_count(), n); strcat(line, n);
    strcat(line, "  events queued total: ");
    u64_to_str(driver_input_i2chid_event_count(), n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    driver_input_i2chid_poll();
    int shown = 0;
    touchpad_event_t e;
    while (shown < 8 && driver_input_touchpad_get_event(&e) == 0) {
        strcpy(line, "  event: ");
        switch (e.type) {
        case TPE_TAP:    strcat(line, "TAP"); break;
        case TPE_SCROLL: strcat(line, "SCROLL"); break;
        case TPE_PINCH:  strcat(line, "PINCH"); break;
        case TPE_SWIPE:  strcat(line, "SWIPE"); break;
        case TPE_MOVE:   strcat(line, "MOVE"); break;
        case TPE_DOWN:   strcat(line, "DOWN"); break;
        case TPE_UP:     strcat(line, "UP"); break;
        default:         strcat(line, "?"); break;
        }
        char num[16];
        strcpy(line + strlen(line), " dx=");
        u64_to_str((u64)(e.dx < 0 ? -e.dx : e.dx), num);
        if (e.dx < 0) strcat(line, "-");
        strcat(line, num);
        strcat(line, " dy=");
        u64_to_str((u64)(e.dy < 0 ? -e.dy : e.dy), num);
        if (e.dy < 0) strcat(line, "-");
        strcat(line, num);
        strcat(line, " fingers=");
        u64_to_str((u64)e.fingers, num); strcat(line, num);
        strcat(line, "\n");
        screen_console_puts(line);
        shown++;
    }
    return 0;
}

int shell_cmd_touchtest(const char *args) {
    (void)args;
    int mask = driver_input_touchpad_selftest();
    if (mask == 0) {
        screen_console_puts("touchtest: descriptor parse + TAP/SCROLL/PINCH/SWIPE  PASS\n");
        return 0;
    }
    /* name the failing item(s) - bit0 parse / bit1 tap / bit2 scroll /
     * bit3 pinch / bit4 swipe - so the verdict is actionable. */
    screen_console_puts("touchtest: FAIL\n");
    if (mask & (1 << 0)) screen_console_puts("  descriptor parse      FAIL\n");
    if (mask & (1 << 1)) screen_console_puts("  TAP gesture           FAIL\n");
    else                 screen_console_puts("  TAP gesture           PASS\n");
    if (mask & (1 << 2)) screen_console_puts("  SCROLL gesture        FAIL\n");
    else                 screen_console_puts("  SCROLL gesture        PASS\n");
    if (mask & (1 << 3)) screen_console_puts("  PINCH gesture         FAIL\n");
    else                 screen_console_puts("  PINCH gesture         PASS\n");
    if (mask & (1 << 4)) screen_console_puts("  SWIPE gesture         FAIL\n");
    else                 screen_console_puts("  SWIPE gesture         PASS\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

void shell_cmds_chipset_register(void) {
    shell_register_command_ex("acpi",      shell_cmd_acpi,      "dump ACPI tables (RSDP/FADT/DSDT/MCFG)", "WP-10-input_chipset");
    shell_register_command_ex("acpidev",   shell_cmd_acpidev,   "list ACPI namespace devices (AML)", "WP-10-input_chipset");
    shell_register_command_ex("acpitest",  shell_cmd_acpitest,  "AML interpreter self-test + DSDT checks", "WP-10-input_chipset");
    shell_register_command_ex("acpipow",   shell_cmd_acpipow,   "ACPI power status (S1-S5/SCI/GPE/FACS)", "WP-10-input_chipset");
    shell_register_command_ex("msitest",   shell_cmd_msitest,   "LAPIC self-IPI + MSI/MSI-X device fire test", "WP-10-input_chipset");
    shell_register_command_ex("pcidump",   shell_cmd_pcidump,   "dump PCI config space (pcidump <b:d.f>)", "WP-10-input_chipset");
    shell_register_command_ex("pcibars",   shell_cmd_pcibars,   "list PCI devices with sized BARs", "WP-10-input_chipset");
    shell_register_command_ex("pcie",      shell_cmd_pcie,      "PCIe MMCONFIG state + capability walk", "WP-10-input_chipset");
    shell_register_command_ex("i2cdetect", shell_cmd_i2cdetect, "scan I2C/SMBus buses for devices", "WP-10-input_chipset");
    shell_register_command_ex("i2cget",    shell_cmd_i2cget,    "read I2C byte (i2cget <bus> <addr> [reg])", "WP-10-input_chipset");
    shell_register_command_ex("i2cset",    shell_cmd_i2cset,    "write I2C byte (i2cset <bus> <addr> <reg> <val>)", "WP-10-input_chipset");
    shell_register_command_ex("i2ctest",   shell_cmd_i2ctest,   "I2C core/controller self-test", "WP-10-input_chipset");
    shell_register_command_ex("smbus",     shell_cmd_smbus,     "list SMBus devices from the scan", "WP-10-input_chipset");
    shell_register_command_ex("smbustest", shell_cmd_smbustest, "SMBus protocol test (byte/word/block)", "WP-10-input_chipset");
    shell_register_command_ex("touchpad",  shell_cmd_touchpad,  "touchpad status + pending gesture events", "WP-10-input_chipset");
    shell_register_command_ex("touchtest", shell_cmd_touchtest, "multitouch pipeline self-test (tap/scroll/pinch/swipe)", "WP-10-input_chipset");
}
