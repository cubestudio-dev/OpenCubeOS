/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL UEFI firmware interface (WP-02-ocbl).
 *
 * Hand-written minimal EFI definitions (UEFI 2.x): only the tables,
 * protocols and functions OCBL actually calls, with the exact member
 * order the specification mandates (offsets are contractual). This
 * header belongs to the UEFI path ONLY - the BIOS path must never
 * include it (decoupling is enforced by grep in the build).
 *
 * No gnu-efi, no external headers: freestanding C on top of the raw
 * SystemTable, compiled by x86_64-w64-mingw32-gcc into a native
 * PE32+ image.
 *
 * Naming: [class]_[category]_[role] - types end in _t, macros are
 * uppercase (CORE_BOOT_EFI_*).
 */

#ifndef CORE_BOOT_UEFI_H
#define CORE_BOOT_UEFI_H

#include <stdint.h>

/* ---- base types (x86_64 UEFI: UINTN is 64-bit) ---------------------- */

typedef uint64_t core_boot_efi_status_t;
typedef void    *core_boot_efi_handle_t;
typedef uint64_t core_boot_efi_uintn_t;
typedef uint64_t core_boot_efi_physical_address_t;

#define CORE_BOOT_EFI_SUCCESS        ((core_boot_efi_status_t)0U)
#define CORE_BOOT_EFI_ERROR_BIT      ((core_boot_efi_status_t)0x8000000000000000ULL)
#define CORE_BOOT_EFI_ERROR(status)  (((status) & CORE_BOOT_EFI_ERROR_BIT) != 0U)

typedef struct {
    uint8_t b[16];
} core_boot_efi_guid_t;

/* ---- protocols OCBL consumes ---------------------------------------- */

/* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
 * {964E5B22-6459-11D2-8E39-00A0C969723B} */
#define CORE_BOOT_EFI_SFS_GUID                                        \
    { { 0x22, 0x5b, 0x4e, 0x96, 0x59, 0x64, 0xd2, 0x11,               \
        0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

/* EFI_FILE_INFO
 * {09576E92-6D3F-11D2-8E39-00A0C969723B} */
#define CORE_BOOT_EFI_FILE_INFO_GUID                                  \
    { { 0x92, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11,               \
        0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

/* EFI_GRAPHICS_OUTPUT_PROTOCOL
 * {9042A9DE-23DC-4A38-96FB-7ADED080516A} */
#define CORE_BOOT_EFI_GOP_GUID                                        \
    { { 0xde, 0xa9, 0x42, 0x90, 0xdc, 0x23, 0x38, 0x4a,               \
        0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef struct core_boot_efi_file_protocol_t core_boot_efi_file_protocol_t;

typedef
core_boot_efi_status_t (*core_boot_efi_file_open_t)(
    core_boot_efi_file_protocol_t *this_file,
    core_boot_efi_file_protocol_t **new_handle,
    const uint16_t *path,
    uint64_t open_mode,
    uint64_t attributes);

typedef
core_boot_efi_status_t (*core_boot_efi_file_read_t)(
    core_boot_efi_file_protocol_t *this_file,
    core_boot_efi_uintn_t *buffer_size,
    void *buffer);

typedef
core_boot_efi_status_t (*core_boot_efi_file_set_position_t)(
    core_boot_efi_file_protocol_t *this_file,
    uint64_t position);

typedef
core_boot_efi_status_t (*core_boot_efi_file_get_info_t)(
    core_boot_efi_file_protocol_t *this_file,
    const core_boot_efi_guid_t *information_type,
    core_boot_efi_uintn_t *buffer_size,
    void *buffer);

typedef
core_boot_efi_status_t (*core_boot_efi_file_close_t)(
    core_boot_efi_file_protocol_t *this_file);

/* EFI_FILE_PROTOCOL - exact UEFI 2.x member order. */
struct core_boot_efi_file_protocol_t {
    uint64_t revision;                    /* 0x00010000 or 0x00020000 */
    core_boot_efi_file_open_t         open;
    core_boot_efi_file_close_t        close;
    core_boot_efi_file_close_t        delete;          /* same signature */
    core_boot_efi_file_read_t         read;
    core_boot_efi_status_t            (*write)(core_boot_efi_file_protocol_t *, core_boot_efi_uintn_t *, void *);
    core_boot_efi_status_t            (*get_position)(core_boot_efi_file_protocol_t *, uint64_t *);
    core_boot_efi_file_set_position_t set_position;
    core_boot_efi_file_get_info_t     get_info;
    core_boot_efi_status_t            (*set_info)(core_boot_efi_file_protocol_t *, const core_boot_efi_guid_t *, core_boot_efi_uintn_t, void *);
    core_boot_efi_status_t            (*flush)(core_boot_efi_file_protocol_t *);
    /* UEFI 2.x extensions (unused, present to keep offsets exact) */
    core_boot_efi_status_t            (*open_ex)(core_boot_efi_file_protocol_t *, core_boot_efi_file_protocol_t **, const uint16_t *, uint64_t, uint64_t);
    core_boot_efi_status_t            (*read_ex)(core_boot_efi_file_protocol_t *, core_boot_efi_uintn_t *, void *);
    core_boot_efi_status_t            (*write_ex)(core_boot_efi_file_protocol_t *, core_boot_efi_uintn_t *, void *);
    core_boot_efi_status_t            (*flush_ex)(core_boot_efi_file_protocol_t *);
};

typedef
core_boot_efi_status_t (*core_boot_efi_sfs_open_volume_t)(
    void *this_fs,
    core_boot_efi_file_protocol_t **root);

/* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL - exact member order. */
typedef struct {
    uint64_t revision;
    core_boot_efi_sfs_open_volume_t open_volume;
} core_boot_efi_sfs_protocol_t;

typedef struct {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} core_boot_efi_pixel_bitmask_t;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    /* EFI_GRAPHICS_PIXEL_FORMAT: 0 = RGBR, 1 = BGRR, 2 = BitMask,
     * 3 = BltOnly */
    uint32_t pixel_format;
    core_boot_efi_pixel_bitmask_t pixel_information;
    uint32_t pixels_per_scan_line;
} core_boot_efi_gop_mode_info_t;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    core_boot_efi_gop_mode_info_t *info;
    core_boot_efi_uintn_t size_of_info;
    core_boot_efi_physical_address_t frame_buffer_base;
    core_boot_efi_uintn_t frame_buffer_size;
} core_boot_efi_gop_mode_t;

typedef
core_boot_efi_status_t (*core_boot_efi_gop_locate_t)(
    void *this_gop,
    void *registration);

typedef struct {
    core_boot_efi_gop_locate_t query_mode;
    core_boot_efi_gop_locate_t set_mode;
    core_boot_efi_status_t     (*blt)(void *, void *, uint32_t, core_boot_efi_uintn_t, core_boot_efi_uintn_t, core_boot_efi_uintn_t, core_boot_efi_uintn_t, core_boot_efi_uintn_t, core_boot_efi_uintn_t);
    core_boot_efi_gop_mode_t  *mode;
} core_boot_efi_gop_protocol_t;

/* ---- boot services (only the members OCBL uses, full order) ---------- */

typedef
core_boot_efi_status_t (*core_boot_efi_get_memory_map_t)(
    core_boot_efi_uintn_t *memory_map_size,
    void *memory_map,
    core_boot_efi_uintn_t *map_key,
    core_boot_efi_uintn_t *descriptor_size,
    uint32_t *descriptor_version);

typedef
core_boot_efi_status_t (*core_boot_efi_exit_boot_services_t)(
    core_boot_efi_handle_t image_handle,
    core_boot_efi_uintn_t map_key);

typedef
core_boot_efi_status_t (*core_boot_efi_allocate_pages_t)(
    uint32_t allocate_type,               /* 2 = AllocateAddress */
    uint32_t memory_type,                 /* 2 = EfiLoaderData */
    core_boot_efi_uintn_t pages,
    core_boot_efi_physical_address_t *memory);

typedef
core_boot_efi_status_t (*core_boot_efi_allocate_pool_t)(
    uint32_t memory_type,
    core_boot_efi_uintn_t size,
    void **buffer);

typedef
core_boot_efi_status_t (*core_boot_efi_locate_protocol_t)(
    const core_boot_efi_guid_t *protocol,
    void *registration,
    void **interface);

typedef
core_boot_efi_status_t (*core_boot_efi_no_args_t)(void);

typedef
core_boot_efi_status_t (*core_boot_efi_set_watchdog_t)(
    core_boot_efi_uintn_t timeout,
    uint64_t watchdog_code,
    core_boot_efi_uintn_t data_size,
    const uint16_t *watchdog_data);

typedef
core_boot_efi_status_t (*core_boot_efi_stall_t)(
    core_boot_efi_uintn_t microseconds);

typedef struct {
    /* EFI_TABLE_HEADER */
    uint64_t signature;                   /* 0x5653554320494249 "IBI SYSV" */
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved_hdr;
    /* task priority */
    core_boot_efi_no_args_t raise_tpl;    /* takes TPL argument - unused */
    core_boot_efi_no_args_t restore_tpl;  /* takes TPL argument - unused */
    /* memory services (contractual position: BEFORE the event services) */
    core_boot_efi_allocate_pages_t allocate_pages;
    core_boot_efi_status_t         (*free_pages)(core_boot_efi_physical_address_t, core_boot_efi_uintn_t);
    core_boot_efi_get_memory_map_t get_memory_map;
    core_boot_efi_allocate_pool_t  allocate_pool;
    core_boot_efi_status_t         (*free_pool)(void *);
    /* events (unused) */
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    /* protocol handling */
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    void *handle_protocol;                /* (handle, guid, void**) - unused */
    void *reserved_ptr;                   /* VOID *Reserved in the spec */
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    /* images (unused) */
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    /* the members OCBL actually calls, in contractual order: */
    core_boot_efi_exit_boot_services_t exit_boot_services;
    void *get_next_monotonic_count;
    core_boot_efi_stall_t stall;
    core_boot_efi_set_watchdog_t set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    core_boot_efi_locate_protocol_t locate_protocol;
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    void *copy_mem;
    void *set_mem;
    void *create_event_ex;
} core_boot_efi_boot_services_t;

/* ---- system table ---------------------------------------------------- */

typedef
core_boot_efi_status_t (*core_boot_efi_text_reset_t)(
    void *this_text,
    uint8_t extended_verification);

typedef
core_boot_efi_status_t (*core_boot_efi_text_output_string_t)(
    void *this_text,
    const uint16_t *string);

typedef struct {
    core_boot_efi_text_reset_t reset;
    core_boot_efi_text_output_string_t output_string;
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *enable_cursor;
    void *clear_screen;
    void *set_attribute;
} core_boot_efi_simple_text_output_protocol_t;

typedef struct {
    /* EFI_TABLE_HEADER */
    uint64_t signature;                   /* 0x5453595320494249 "IBI STSY" */
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    const uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    /* implicit 4 bytes padding for the following pointers */
    core_boot_efi_handle_t console_in_handle;
    void *con_in;
    core_boot_efi_handle_t console_out_handle;
    core_boot_efi_simple_text_output_protocol_t *con_out;
    core_boot_efi_handle_t standard_error_handle;
    core_boot_efi_simple_text_output_protocol_t *std_err;
    void *runtime_services;
    core_boot_efi_boot_services_t *boot_services;
    core_boot_efi_uintn_t number_of_table_entries;
    void *configuration_table;
} core_boot_efi_system_table_t;

/* EFI_MEMORY_DESCRIPTOR (layout, not enumerated types). */
typedef struct {
    uint32_t type;                        /* 7 = EfiConventionalMemory */
    core_boot_efi_physical_address_t physical_start;
    core_boot_efi_physical_address_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} core_boot_efi_memory_descriptor_t;

/* ---- constants OCBL uses --------------------------------------------- */

#define CORE_BOOT_EFI_ALLOCATE_ADDRESS 2U   /* EFI_ALLOCATE_TYPE */
#define CORE_BOOT_EFI_LOADER_DATA      2U   /* EFI_MEMORY_TYPE */

#define CORE_BOOT_EFI_FILE_MODE_READ   0x0000000000000001ULL
#define CORE_BOOT_EFI_FILE_ATTR_NONE   0x0000000000000000ULL

#endif /* CORE_BOOT_UEFI_H */
