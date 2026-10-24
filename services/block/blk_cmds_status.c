/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/block status commands (migrated
 * verbatim from core/main.c inline commands). */
#include "blk_cmds.h"
#include "block_svc.h"
#include "driver_block_ahci.h"
#include "driver_block_nvme.h"
#include "driver_block_ata_dma.h"
#include "driver_block_ata.h"
#include "driver_block_cache.h"
#include "screen_console.h"
#include "lib_string.h"

int blk_cmd_ahci(const char *args) {
    (void)args;
    driver_block_ahci_print_state();
    return 0;
}

int blk_cmd_nvme(const char *args) {
    (void)args;
    driver_block_nvme_print_state();
    return 0;
}

int blk_cmd_ata(const char *args) {
    (void)args;
    driver_block_ata_dma_print_state();
    return 0;
}

int blk_cmd_dskstat(const char *args) {
    (void)args;
    /* Show ATA drive detection (original WP-05 interface). */
    screen_console_puts("ATA drive detection:\n");
    for (int d = 0; d < 4; d++) {
        const char *chan = (d < 2) ? "primary" : "secondary";
        const char *role = (d & 1) ? "slave" : "master";
        char line[60];
        strcpy(line, "  ");
        int p = strlen(line);
        int cl = strlen(chan); memcpy(line+p, chan, cl); p += cl;
        line[p++] = ' ';
        cl = strlen(role); memcpy(line+p, role, cl); p += cl;
        strcpy(line+p, ": "); p += 2;
        const char *s = driver_block_ata_detect(d) ? "present" : "absent";
        cl = strlen(s); memcpy(line+p, s, cl); p += cl;
        line[p++] = '\n'; line[p] = 0;
        screen_console_puts(line);
    }
    /* Also show blk-layer devices. */
    driver_block_list_devices();
    /* WP-10a: one-line DMA/AHCI/NVMe presence summary. */
    {
        int dma = 0;
        for (int i = 0; i < 4; i++) if (driver_block_ata_dma_available(i)) dma++;
        char line[96]; char n[24];
        strcpy(line, "drivers: ATA-DMA drives=");
        u64_to_str((u64)dma, n); strcat(line, n);
        strcat(line, "  AHCI drives=");
        u64_to_str((u64)driver_block_ahci_num_drives(), n); strcat(line, n);
        strcat(line, "  NVMe queues=");
        u64_to_str((u64)driver_block_nvme_num_io_queues(), n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
    return 0;
}
