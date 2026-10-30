/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/reloc.S
 * Purpose: the PE base-relocation directory content.  ld -m i386pep
 *          turns our absolute-address relocations into .reloc entries
 *          itself; this seed block guarantees the directory exists
 *          even when the linker generates none, because EDK2's
 *          PeCoffLoader REFUSES an image whose load address differs
 *          from ImageBase while the relocation directory is empty
 *          (found live: OVMF BdsDxe "Load Error" with the bare ld
 *          output; adding this block alone fixed the load).
 */
.section .reloc,"a"
.long 0    /* PageRVA  = 0       */
.long 8    /* BlockSize = 8 (header only, zero entries) */
