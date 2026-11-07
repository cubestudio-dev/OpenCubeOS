; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/efi/handoff_data.asm
; Purpose: embed the assembled downgrade trampoline (handoff_blob.bin)
;          into the EFI image; efi_entry.c memcpy's it to 0x30000 right
;          before ExitBootServices hands the machine over (B5-5).
;          The same two-step shape as bios/tramp_data.asm.

                bits 64
                default rel

                global ocbl_efi_handoff_blob
                global ocbl_efi_handoff_blob_end

section .rodata
ocbl_efi_handoff_blob:
        incbin "handoff_blob.bin"
ocbl_efi_handoff_blob_end:
