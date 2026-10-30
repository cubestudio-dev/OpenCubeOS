/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/efi.h
 * Purpose: self-contained minimal UEFI definitions for the OCBL EFI
 *          flavor.  Deliberately NOT gnu-efi/edk2: the WP-05 toolchain
 *          set is fixed (117 debs), the WP-05 rule is "no GRUB, no
 *          foreign boot logic", and every struct here maps one-to-one
 *          to the UEFI Specification tables - offsets verified against
 *          the spec, offsets verified live against OVMF.
 *
 * ABI: every firmware callback is __attribute__((ms_abi)) (Microsoft
 * x64: rcx, rdx, r8, r9).  gcc honors it for function pointers, which
 * is the whole reason a plain freestanding gcc build can talk to UEFI
 * without the gnu-efi thunk assembly.
 */
#ifndef OCBL_EFI_H
#define OCBL_EFI_H

#include "../common/ocbl_types.h"

/* ---- basic types (UEFI spec 2.3.1) ---- */
typedef u64 efi_status;              /* EFI_STATUS, 0 = EFI_SUCCESS   */
typedef void* efi_handle;
typedef void* efi_event;
typedef u64 efi_tpl;
typedef u64 efi_phys;
typedef u16 efi_char16;

typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
} efi_guid;

#define EFI_CALL __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_ERROR(x) ((efi_status)(x))
#define EFI_NOT_FOUND        EFI_ERROR(14)
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_UNSUPPORTED      EFI_ERROR(3)
#define EFI_OUT_OF_RESOURCES EFI_ERROR(15)
#define EFI_LOAD_ERROR       EFI_ERROR(9)

/* ---- table header (spec 4.1) ---- */
typedef struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc32;
    u32 reserved;
} efi_table_hdr;

/* ---- simple text (spec 12.4 / 12.5) ---- */
typedef struct {
    u16 scan_code;
    efi_char16 unicode;
} efi_input_key;

typedef struct {
    EFI_CALL efi_status (*reset)(void* self, u8 extended);
    EFI_CALL efi_status (*read_keystroke)(void* self, efi_input_key* key);
    efi_event wait_for_key;
} efi_simple_text_in;

typedef struct {
    EFI_CALL efi_status (*reset)(void* self, u8 extended);
    EFI_CALL efi_status (*output_string)(void* self, const efi_char16* s);
    EFI_CALL efi_status (*test_string)(void* self, const efi_char16* s);
    EFI_CALL efi_status (*query_mode)(void* self, u64 mode, u64* cols,
                                      u64* rows);
    EFI_CALL efi_status (*set_mode)(void* self, u64 mode);
    EFI_CALL efi_status (*set_attribute)(void* self, u64 attr);
    EFI_CALL efi_status (*clear_screen)(void* self);
    EFI_CALL efi_status (*set_cursor_pos)(void* self, u64 col, u64 row);
    EFI_CALL efi_status (*enable_cursor)(void* self, u8 visible);
    void* mode;
} efi_simple_text_out;

/* ---- system table (spec 4.3) ---- */
typedef struct {
    efi_table_hdr hdr;
    efi_char16*     fw_vendor;
    u32           fw_revision;
    u8            _pad0[4];
    efi_handle    con_in_handle;
    efi_simple_text_in*  con_in;
    efi_handle    con_out_handle;
    efi_simple_text_out* con_out;
    efi_handle    stderr_handle;
    efi_simple_text_out* std_err;
    void*         runtime_services;     /* EFI_RUNTIME_SERVICES* */
    struct efi_boot_services* boot_services;
    u64           nr_table_entries;
    efi_guid*     config_table;         /* EFI_CONFIGURATION_TABLE[] */
} efi_system_table;

typedef struct {
    efi_guid vendor_guid;
    void*    vendor_table;
} efi_config_table;

/* ---- memory (spec 7.2) ---- */
#define EFI_ACPI_RECLAIM_MEMORY  9
#define EFI_LOADER_CODE           2
#define EFI_BOOT_SERVICES_CODE    4
#define EFI_BOOT_SERVICES_DATA    5
#define EFI_RUNTIME_SERVICES_CODE 6
#define EFI_RUNTIME_SERVICES_DATA 7
#define EFI_CONVENTIONAL_MEMORY   7
#define EFI_RESERVED_MEMORY_TYPE  0
#define EFI_UNUSABLE_MEMORY       5
#define EFI_ACPI_MEMORY_NVS      10
#define EFI_MEMORY_MAPPED_IO     11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE             13
#define EFI_PERSISTENT_MEMORY    16

typedef struct {
    u32 type;
    u32 _pad;
    efi_phys physical_start;
    efi_phys virtual_start;
    u64      number_of_pages;
    u64      attribute;
} efi_memory_descriptor;

typedef enum {
    EFI_ALLOCATE_ANY_PAGES = 0,
    EFI_ALLOCATE_MAX_ADDRESS = 1,
    EFI_ALLOCATE_ADDRESS = 2,
} efi_allocate_type;

/* ---- block I/O (spec 12.8) ---- */
typedef struct {
    u32 media_id;
    u8  removable;
    u8  media_present;
    u8  logical_partition;
    u8  read_only;
    u8  write_caching;
    u8  _pad[3];
    u32 block_size;
    u32 io_align;
    u64 last_block;
} efi_block_io_media;

typedef struct {
    u64 revision;
    efi_block_io_media* media;
    EFI_CALL efi_status (*reset)(void* self, u8 extended);
    EFI_CALL efi_status (*read_blocks)(void* self, u32 media_id, u64 lba,
                                       u64 buf_size, void* buf);
    EFI_CALL efi_status (*write_blocks)(void* self, u32 media_id, u64 lba,
                                        u64 buf_size, const void* buf);
    EFI_CALL efi_status (*flush_blocks)(void* self);
} efi_block_io;

/* ---- graphics output (spec 12.9) ---- */
typedef struct {
    u32 h_res;
    u32 v_res;
    u32 pixel_format;        /* 0 RGBx 8.8.8.8 | 1 BGRx | 2 bitmask | 3 blt-only */
    struct { u32 r, g, b, rsv; } pixel_info;
    u32 pixels_per_scan_line;
} efi_gop_mode_info;

typedef struct {
    u32 max_mode;
    u32 mode;
    efi_gop_mode_info* info;
    u64 size_of_info;
    efi_phys frame_buffer_base;
    u64 frame_buffer_size;
} efi_gop_mode;

typedef struct {
    EFI_CALL efi_status (*query_mode)(void* self, u32 mode, u64* sz,
                                      efi_gop_mode_info** info);
    EFI_CALL efi_status (*set_mode)(void* self, u32 mode);
    void* blt;               /* unused: linear fb only */
    efi_gop_mode* mode;
} efi_graphics_output;

/* ---- loaded image (spec 9.1) ---- */
typedef struct {
    u32 revision;
    void* parent_handle;
    efi_system_table* system_table;
    efi_handle device_handle;
    void* file_path;
    void* reserved;
    u32 load_options_size;
    void* load_options;
    void* image_base;
    u64 image_size;
    int image_code_type;
    int image_data_type;
    EFI_CALL efi_status (*unload)(void* self);
} efi_loaded_image;

/* ---- boot services (spec 4.3, function order is ABI) ---- */
typedef struct efi_boot_services {
    efi_table_hdr hdr;
    EFI_CALL efi_tpl  (*raise_tpl)(efi_tpl new_tpl);
    EFI_CALL void     (*restore_tpl)(efi_tpl old_tpl);
    EFI_CALL efi_status (*allocate_pages)(int type, int mem_type,
                                          u64 pages, efi_phys* addr);
    EFI_CALL efi_status (*free_pages)(efi_phys addr, u64 pages);
    EFI_CALL efi_status (*get_memory_map)(u64* sz, efi_memory_descriptor* map,
                                          u64* key, u64* desc_sz, u32* ver);
    EFI_CALL efi_status (*allocate_pool)(int mem_type, u64 sz, void** buf);
    EFI_CALL efi_status (*free_pool)(void* buf);
    EFI_CALL efi_status (*create_event)(u32 type, efi_tpl tpl,
                                        void* notify, void* ctx, efi_event* ev);
    EFI_CALL efi_status (*set_timer)(efi_event ev, int type, u64 trigger);
    EFI_CALL efi_status (*wait_for_event)(u64 nr, efi_event* events, u64* idx);
    EFI_CALL efi_status (*signal_event)(efi_event ev);
    EFI_CALL efi_status (*close_event)(efi_event ev);
    EFI_CALL efi_status (*check_event)(efi_event ev);
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    EFI_CALL efi_status (*handle_protocol)(efi_handle h, const efi_guid* g,
                                           void** iface);
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    EFI_CALL efi_status (*load_image)(u8 boot_policy, efi_handle parent,
                                      void* file_path, void* src, u64 src_sz,
                                      efi_handle* image);
    EFI_CALL efi_status (*start_image)(efi_handle image, u64* exit_sz,
                                       efi_char16** exit_data);
    EFI_CALL efi_status (*exit)(efi_handle image, efi_status status,
                                u64 exit_sz, efi_char16* exit_data);
    EFI_CALL efi_status (*unload_image)(efi_handle image);
    EFI_CALL efi_status (*exit_boot_services)(efi_handle image, u64 map_key);
    void* get_next_monotonic_count;
    EFI_CALL efi_status (*stall)(u64 usec);
    EFI_CALL efi_status (*set_watchdog_timer)(u32 timeout, u64 code,
                                              u64 data_sz, efi_char16* data);
    void* connect_controller;
    void* disconnect_controller;
    EFI_CALL efi_status (*open_protocol)(efi_handle h, const efi_guid* proto,
                                         void** iface, efi_handle agent,
                                         efi_handle ctrl, u32 attrs);
    EFI_CALL efi_status (*close_protocol)(efi_handle h, const efi_guid* proto,
                                          efi_handle agent, efi_handle ctrl);
    void* open_protocol_information;
    EFI_CALL efi_status (*protocols_per_handle)(efi_handle h,
                                                efi_guid*** proto_buf,
                                                u64* count);
    EFI_CALL efi_status (*locate_handle_buffer)(int search_type,
                                                const efi_guid* proto,
                                                void* search_key,
                                                u64* nr_handles,
                                                efi_handle** buf);
    EFI_CALL efi_status (*locate_protocol)(const efi_guid* proto,
                                           void* registration, void** iface);
    void* install_multiple;
    void* uninstall_multiple;
    void* calculate_crc32;
    void (*copy_mem)(void* dst, const void* src, u64 len);
    void (*set_mem)(void* dst, u8 val, u64 len);
    void* create_event_ex;
} efi_boot_services;

/* ---- GUIDs we need ---- */
extern const efi_guid ocbl_efi_block_io_guid;
extern const efi_guid ocbl_efi_gop_guid;
extern const efi_guid ocbl_efi_loaded_image_guid;
extern const efi_guid ocbl_efi_acpi20_guid;     /* ACPI 2.0 RSDP config entry */
extern const efi_guid ocbl_efi_acpi10_guid;     /* ACPI 1.0 fallback */

#endif /* OCBL_EFI_H */
