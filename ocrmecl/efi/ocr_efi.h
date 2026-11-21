/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocrmecl/efi/ocr_efi.h
 * Purpose: self-contained minimal UEFI + base type definitions for
 *          the OCRMECL EFI track.  Deliberately NOT shared with
 *          ocbl/ - OCRMECL is a PHYSICALLY SEPARATE boot stage
 *          (spec rule: no OCBL header is ever included here, no
 *          OCRMECL object is ever linked into an OCBL image; the
 *          only shared contract is interface/boot_handoff.h).
 *
 *          Struct layouts mirror the UEFI spec one-to-one (the same
 *          discipline ocbl/efi/efi.h documented; offsets verified
 *          live against OVMF by the WP-05 B-batches).
 */
#ifndef OCR_EFI_H
#define OCR_EFI_H

/* ---- basic types ---- */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef int            bool;
#define true  1
#define false 0

typedef u64 efi_status;
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

/* ---- system table (spec 4.3) ---- */
typedef struct {
    efi_table_hdr hdr;
    efi_char16*     fw_vendor;
    u32           fw_revision;
    u8            _pad0[4];
    efi_handle    con_in_handle;
    void*         con_in;
    efi_handle    con_out_handle;
    void*         con_out;
    efi_handle    stderr_handle;
    void*         std_err;
    void*         runtime_services;
    struct efi_boot_services* boot_services;
    u64           nr_table_entries;
    efi_guid*     config_table;
} efi_system_table;

typedef struct {
    efi_guid vendor_guid;
    void*    vendor_table;
} efi_config_table;

/* ---- memory (spec 7.2) ---- */
#define EFI_BOOT_SERVICES_CODE     3
#define EFI_BOOT_SERVICES_DATA     4
#define EFI_CONVENTIONAL_MEMORY    7
#define EFI_UNUSABLE_MEMORY        8
#define EFI_ACPI_RECLAIM_MEMORY    9
#define EFI_ACPI_MEMORY_NVS       10

typedef struct {
    u32 type;
    u32 _pad;
    efi_phys physical_start;
    efi_phys virtual_start;
    u64      number_of_pages;
    u64      attribute;
} efi_memory_descriptor;

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
    u32 pixel_format;        /* 0 RGBx | 1 BGRx | 2 bitmask | 3 blt-only */
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
    void* blt;
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
    EFI_CALL efi_status (*connect_controller)(efi_handle ctrl,
                                              efi_handle driver_image,
                                              void* remaining_path,
                                              u8 recursive);
    EFI_CALL efi_status (*disconnect_controller)(efi_handle ctrl,
                                                 efi_handle driver_image,
                                                 efi_handle child);
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

/* ---- the OCRMECL serial console (ocr_serial.c) ---- */
void ocr_serial_init(void);
void ocr_serial_puts(const char* s);
void ocr_serial_hex(u64 v, int digits);
void ocr_serial_dec(u64 v);

/* ---- the in-RAM ISO9660 probe (ocr_iso9660.c) ---- */
int ocr_iso_pvd(const u8* iso, u64 iso_size);
int ocr_iso_pvd_sector(const u8* sec);
int ocr_iso_find(const u8* iso, u64 iso_size, const char* want,
                 u64* lba, u64* size);

#endif /* OCR_EFI_H */
