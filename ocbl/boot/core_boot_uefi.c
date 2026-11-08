/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL UEFI boot entry (WP-02-ocbl).
 *
 * efi_main: the PE32+ entry point of ocbl-uefi.efi. Flow:
 *
 *   1. bring up the debug outputs: COM1 serial (the shared core
 *      channel) and the screen - firmware Simple Text Output plus
 *      the GOP framebuffer takeover (drv_video_uefi_gop.c);
 *   2. open the boot volume through Simple File System Protocol and
 *      read \boot\kernel.elf into a pool buffer (EFI_FILE_INFO for
 *      the size, then one full Read);
 *   3. reserve the kernel's physical load area with AllocatePages
 *      (AllocateAddress, EfiLoaderData) so the UEFI memory map owns
 *      the bytes the shared ELF loader is about to copy into;
 *   4. parse + load the kernel through the SHARED ELF64 loader
 *      (core_boot_elf_loader.c, the same code the BIOS path runs);
 *   5. stop the watchdog, get the memory map, call ExitBootServices
 *      (standard retry loop - the map key goes stale whenever the
 *      map changes, so re-fetch and retry);
 *   6. jump to the kernel entry. Boot arguments are zeroed: the NAK
 *      kernel's entry currently ignores its arguments (WP-01
 *      contract); a GRUB-less boot-parameter protocol is a later
 *      work package. Declared honestly in the delivery report.
 *
 * Compiled by x86_64-w64-mingw32-gcc into a native PE32+ image
 * (-Wl,--subsystem,10). Never includes any BIOS-side header.
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), uefi (the firmware interface this implements).
 */

#include <stdint.h>
#include <stddef.h>

#include "boot/core_boot_uefi.h"
#include "boot/core_boot_elf_loader.h"
#include "drivers/drv_serial_16550.h"
#include "drivers/drv_video_uefi_gop.h"

/* mingw freestanding quirk: the compiler emits a call to __main from
 * main-like functions; with -nostdlib there is no CRT to provide it.
 * Same approach the gnu-efi runtime takes. */
void __main(void)
{
}

/* Static buffer for the ExitBootServices memory map: OVMF maps are a
 * few KiB; 64 KiB leaves ample headroom. */
static uint8_t core_boot_uefi_memory_map[64 * 1024];

static core_boot_efi_system_table_t *core_boot_uefi_st;
static core_boot_efi_handle_t core_boot_uefi_image;

/* 16-bit strings for EFI (L"" is 16-bit under -fshort-wchar). */
static const uint16_t core_boot_uefi_msg_boot[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'U', 'E', 'F', 'I', ' ', 'b', 'o', 'o', 't', '\r', '\n', 0 };
static const uint16_t core_boot_uefi_msg_gop_fail[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'W', 'A', 'R', 'N', ':', ' ', 'G', 'O', 'P', ' ', 'n', 'o', 't', ' ', 'f', 'o', 'u', 'n', 'd', '\r', '\n', 0 };
static const uint16_t core_boot_uefi_msg_err_file[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'E', 'R', 'R', 'O', 'R', ':', ' ', 'c', 'a', 'n', 'n', 'o', 't', ' ', 'r', 'e', 'a', 'd', ' ', '\\', 'b', 'o', 'o', 't', '\\', 'k', 'e', 'r', 'n', 'e', 'l', '.', 'e', 'l', 'f', '\r', '\n', 0 };
static const uint16_t core_boot_uefi_msg_err_alloc[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'E', 'R', 'R', 'O', 'R', ':', ' ', 'A', 'l', 'l', 'o', 'c', 'a', 't', 'e', 'P', 'a', 'g', 'e', 's', '\r', '\n', 0 };
static const uint16_t core_boot_uefi_msg_err_elf[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'E', 'R', 'R', 'O', 'R', ':', ' ', 'E', 'L', 'F', ' ', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '\r', '\n', 0 };
static const uint16_t core_boot_uefi_msg_err_ebs[] =
    { 'O', 'C', 'B', 'L', ':', ' ', 'E', 'R', 'R', 'O', 'R', ':', ' ', 'E', 'x', 'i', 't', 'B', 'o', 'o', 't', 'S', 'e', 'r', 'v', 'i', 'c', 'e', 's', '\r', '\n', 0 };

static const uint16_t core_boot_uefi_kernel_path[] =
    { '\\', 'b', 'o', 'o', 't', '\\', 'k', 'e', 'r', 'n', 'e', 'l', '.', 'e', 'l', 'f', 0 };

static void uefi_text(const uint16_t *s)
{
    if (core_boot_uefi_st != NULL && core_boot_uefi_st->con_out != NULL) {
        core_boot_uefi_st->con_out->output_string(core_boot_uefi_st->con_out, s);
    }
}

static void uefi_fail(const uint16_t *msg)
{
    drv_serial_puts("OCBL: ERROR - ");
    drv_serial_puts("UEFI boot failed, halting\n");
    uefi_text(msg);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

/* Read the whole \boot\kernel.elf into a fresh pool buffer.
 * Returns 0 on success; *out holds the buffer, *out_size the size. */
static int uefi_read_kernel(void **out, core_boot_efi_uintn_t *out_size)
{
    core_boot_efi_boot_services_t *bs = core_boot_uefi_st->boot_services;
    const core_boot_efi_guid_t sfs_guid = CORE_BOOT_EFI_SFS_GUID;
    const core_boot_efi_guid_t file_info_guid = CORE_BOOT_EFI_FILE_INFO_GUID;
    core_boot_efi_sfs_protocol_t *sfs = NULL;
    core_boot_efi_file_protocol_t *root = NULL;
    core_boot_efi_file_protocol_t *file = NULL;
    uint8_t info_buf[256];
    core_boot_efi_uintn_t info_size = sizeof(info_buf);
    core_boot_efi_uintn_t file_size;
    void *buffer = NULL;
    core_boot_efi_status_t status;

    status = bs->locate_protocol(&sfs_guid, NULL, (void **)&sfs);
    if (CORE_BOOT_EFI_ERROR(status) || sfs == NULL) {
        return 1;
    }

    status = sfs->open_volume(sfs, &root);
    if (CORE_BOOT_EFI_ERROR(status) || root == NULL) {
        return 1;
    }

    status = root->open(root, &file, core_boot_uefi_kernel_path,
                        CORE_BOOT_EFI_FILE_MODE_READ, CORE_BOOT_EFI_FILE_ATTR_NONE);
    if (CORE_BOOT_EFI_ERROR(status) || file == NULL) {
        root->close(root);
        return 1;
    }

    /* EFI_FILE_INFO: first call fills info_buf, then FileSize is the
     * second member after the 8-byte Size field. */
    status = file->get_info(file, &file_info_guid, &info_size, info_buf);
    if (CORE_BOOT_EFI_ERROR(status)) {
        file->close(file);
        root->close(root);
        return 1;
    }
    file_size = *(uint64_t *)(void *)(info_buf + 8);
    if (file_size == 0U || file_size > CORE_BOOT_ELF_MAX_PADDR - CORE_BOOT_ELF_MIN_PADDR) {
        file->close(file);
        root->close(root);
        return 1;
    }

    status = bs->allocate_pool(CORE_BOOT_EFI_LOADER_DATA, file_size, &buffer);
    if (CORE_BOOT_EFI_ERROR(status) || buffer == NULL) {
        file->close(file);
        root->close(root);
        return 1;
    }

    {
        core_boot_efi_uintn_t read_size = file_size;
        status = file->read(file, &read_size, buffer);
        if (CORE_BOOT_EFI_ERROR(status) || read_size != file_size) {
            file->close(file);
            root->close(root);
            return 1;
        }
    }

    file->close(file);
    root->close(root);

    *out = buffer;
    *out_size = file_size;
    return 0;
}

/* Handoff GDT: null | 64-bit code (0x08) | flat writable data (0x10)
 * | 32-bit code (0x18). Selector 0x10 is what the kernel's 64-bit
 * entry expects - identical to the BIOS path's GDT. */
static const uint64_t core_boot_uefi_handoff_gdt[4] = {
    0x0000000000000000ULL,
    0x00AF9A000000FFFFULL,
    0x00CF92000000FFFFULL,
    0x00CF9A000000FFFFULL
};

static void uefi_install_handoff_gdt(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;

    gdtr.limit = (uint16_t)(sizeof(core_boot_uefi_handoff_gdt) - 1U);
    gdtr.base = (uint64_t)(uintptr_t)core_boot_uefi_handoff_gdt;

    __asm__ __volatile__(
        "lgdt %0\n\t"
        /* far-return into the new 64-bit code segment so CS is live */
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(gdtr)
        : "rax", "memory");
}

core_boot_efi_status_t efi_main(core_boot_efi_handle_t image_handle,
                                core_boot_efi_system_table_t *system_table)
{
    core_boot_efi_boot_services_t *bs;
    void *kernel_image = NULL;
    core_boot_efi_uintn_t kernel_size = 0U;
    uint64_t entry;

    core_boot_uefi_image = image_handle;
    core_boot_uefi_st = system_table;
    bs = system_table->boot_services;

    /* --- debug outputs ------------------------------------------------- */
    drv_serial_init();
    drv_serial_puts("OCBL: UEFI boot, COM1 serial up\n");
    uefi_text(core_boot_uefi_msg_boot);

    if (drv_video_gop_init(bs) != 0) {
        uefi_text(core_boot_uefi_msg_gop_fail);
        drv_serial_puts("OCBL: WARN - GOP unavailable, continuing on serial\n");
    }

    /* --- read \boot\kernel.elf ----------------------------------------- */
    if (bs == NULL || uefi_read_kernel(&kernel_image, &kernel_size) != 0) {
        drv_serial_puts("OCBL: ERROR - cannot read \\boot\\kernel.elf\n");
        uefi_fail(core_boot_uefi_msg_err_file);
    }
    {
        /* size is only used for the log; hex-print without libc */
        static const char hex_digits[] = "0123456789abcdef";
        char line[32];
        int i;
        for (i = 0; i < 16; ++i) {
            line[i] = hex_digits[(kernel_size >> (60 - 4 * i)) & 0xFU];
        }
        line[16] = '\0';
        drv_serial_puts("OCBL: read \\boot\\kernel.elf, ");
        drv_serial_puts(line);
        drv_serial_puts(" bytes\n");
    }

    /* --- reserve exactly the range the kernel needs --------------------- */
    {
        uint64_t first = 0U;
        uint64_t last = 0U;
        uint64_t pages;
        core_boot_efi_physical_address_t load_base;
        core_boot_efi_status_t status;

        if (core_boot_elf_probe(kernel_image, &first, &last) != 0) {
            drv_serial_puts("OCBL: ERROR - kernel.elf failed to validate\n");
            uefi_fail(core_boot_uefi_msg_err_elf);
        }
        pages = (last - first + 4095U) / 4096U;
        load_base = first;
        status = bs->allocate_pages(CORE_BOOT_EFI_ALLOCATE_ADDRESS,
                                    CORE_BOOT_EFI_LOADER_DATA,
                                    (core_boot_efi_uintn_t)pages,
                                    &load_base);
        if (CORE_BOOT_EFI_ERROR(status)) {
            drv_serial_puts("OCBL: ERROR - AllocatePages for kernel area\n");
            uefi_fail(core_boot_uefi_msg_err_alloc);
        }
        drv_serial_puts("OCBL: kernel area reserved\n");
    }

    /* --- shared ELF64 parse + load -------------------------------------- */
    entry = core_boot_elf_load(kernel_image);
    if (entry == 0U) {
        drv_serial_puts("OCBL: ERROR - kernel.elf failed to validate\n");
        uefi_fail(core_boot_uefi_msg_err_elf);
    }
    {
        static const char hex_digits[] = "0123456789abcdef";
        char line[32];
        int i;
        for (i = 0; i < 16; ++i) {
            line[i] = hex_digits[(entry >> (60 - 4 * i)) & 0xFU];
        }
        line[16] = '\0';
        drv_serial_puts("OCBL: kernel entry at ");
        drv_serial_puts(line);
        drv_serial_puts(", exiting boot services\n");
    }

    /* --- watchdog off, then ExitBootServices (retry loop) ---------------- */
    bs->set_watchdog_timer(0U, 0U, 0U, NULL);

    {
        int attempt;
        int exited = 0;
        for (attempt = 0; attempt < 3 && exited == 0; ++attempt) {
            core_boot_efi_uintn_t map_size = sizeof(core_boot_uefi_memory_map);
            core_boot_efi_uintn_t map_key = 0U;
            core_boot_efi_uintn_t descriptor_size = 0U;
            uint32_t descriptor_version = 0U;
            core_boot_efi_status_t status =
                bs->get_memory_map(&map_size,
                                   (void *)core_boot_uefi_memory_map,
                                   &map_key, &descriptor_size,
                                   &descriptor_version);
            if (CORE_BOOT_EFI_ERROR(status)) {
                continue;
            }
            status = bs->exit_boot_services(core_boot_uefi_image, map_key);
            if (!CORE_BOOT_EFI_ERROR(status)) {
                exited = 1;
            }
        }
        if (exited == 0) {
            drv_serial_puts("OCBL: ERROR - ExitBootServices retry exhausted\n");
            uefi_fail(core_boot_uefi_msg_err_ebs);
        }
    }

    drv_serial_puts("OCBL: boot services exited, jumping to kernel\n");

    /* --- hand off -------------------------------------------------------- */
    /* The firmware GDT does not necessarily keep a flat writable data
     * segment at selector 0x10 (OVMF's layout differs), but the NAK
     * kernel's 64-bit entry reloads its segment registers from 0x10
     * by contract. OCBL installs its OWN GDT - same layout the BIOS
     * path already guarantees - so both firmware paths hand the
     * kernel the exact environment it expects. */
    __asm__ __volatile__("cli");
    uefi_install_handoff_gdt();
    ((void (*)(uint32_t, uint32_t))(uintptr_t)entry)(0U, 0U);

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
    return CORE_BOOT_EFI_SUCCESS;
}
