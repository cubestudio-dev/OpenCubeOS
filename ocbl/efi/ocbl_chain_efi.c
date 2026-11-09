/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_chain_efi.c
 * Purpose: the UEFI flavor of the foreign-OS chainload (B5-6): read
 *          the chosen ESP loader (Windows bootmgfw / a foreign Linux
 *          shim / a generic BOOTX64.EFI) into a firmware-allocated
 *          buffer and hand it to LoadImage + StartImage - the UEFI
 *          mirror of the BIOS flavor's VBR chainload (ocbl_chain16).
 *
 *          Boot services stay ALIVE here (the target loader owns the
 *          rest of the boot and its own ExitBootServices); our i6300esb
 *          watchdog is disarmed first - the same honesty rule as the
 *          BIOS chainload: a payload that never installed the
 *          heartbeat must not be reset by ours.
 *
 *          DevicePath is NULL (LoadImage-from-buffer): OVMF accepts a
 *          NULL device path and builds a minimal one internally - the
 *          image handle the target sees carries our OCBL as parent.
 */
#include "efi.h"
#include "../common/ocbl_fs.h"
#include "../common/ocbl_serial.h"

/* firmware allocation for the image buffer (grub/shim are ~1 MiB,
 * bootmgfw ~1.5 MiB; 4 MiB is a hard honest ceiling) */
#define CHAIN_MAX_BYTES (4u * 1024u * 1024u)

/* Chainload `efi_path` from volume `v`.  Returns an EFI_STATUS: 0 when
 * the target image returned control on its own (its exit code is NOT
 * propagated - a chainloaded OS that fails is its own failure), an
 * EFI error when the chain could not even be started. */
efi_status ocbl_efi_chainload(const ocbl_volume_t* v, const char* efi_path)
{
    u8* buf = 0;
    if (ocbl_efi_bs->allocate_pool(4 /* BootServicesData */,
                                   CHAIN_MAX_BYTES,
                                   (void**)&buf) != EFI_SUCCESS || !buf) {
        ocbl_serial_puts("[boot] FATAL: chainload buffer alloc failed\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    u32 got = ocbl_fs_read_file(v, efi_path, buf, CHAIN_MAX_BYTES);
    if (!got) {
        ocbl_serial_puts("[boot] FATAL: chainload target not readable: ");
        ocbl_serial_puts(efi_path);
        ocbl_serial_puts("\r\n");
        ocbl_efi_bs->free_pool(buf);
        return EFI_LOAD_ERROR;
    }
    ocbl_serial_puts("[boot] chainload image: ");
    ocbl_serial_puts(efi_path);
    ocbl_serial_puts(" (");
    ocbl_serial_dec(got);
    ocbl_serial_puts(" bytes)\r\n");

    efi_handle himage = 0;
    efi_status st = ocbl_efi_bs->load_image(
        0 /* BootPolicy: load from the exact source */,
        ocbl_efi_image, 0 /* DevicePath: from-buffer */, buf, got,
        &himage);
    if (st != EFI_SUCCESS) {
        ocbl_serial_puts("[boot] LoadImage failed: 0x");
        ocbl_serial_hex((u64)st, 8);
        ocbl_serial_puts("\r\n");
        ocbl_efi_bs->free_pool(buf);
        return st;
    }
    st = ocbl_efi_bs->start_image(himage, 0, 0);
    ocbl_serial_puts("[boot] chainloaded image returned: 0x");
    ocbl_serial_hex((u64)st, 8);
    ocbl_serial_puts("\r\n");
    return EFI_SUCCESS;
}
