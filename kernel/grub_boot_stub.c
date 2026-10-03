/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10d-pre (rule-9 system self-sufficiency fix)
 * File: kernel/grub_boot_stub.c
 * Purpose: stage-1 link stub for the embedded boot data.
 *
 * Stage 1 of the build (opencube_base.elf) is linked WITHOUT the
 * generated kernel/generated/grub_boot_data.c (the real data does not
 * exist yet - embed_grub.py needs the stage-1 ELF).  This stub provides
 * all six symbols as strong definitions with zero lengths so the stage-1
 * link succeeds; stage 2 drops this object and links the real data
 * instead (identical symbol names, same strong linkage - no weak
 * symbols involved).
 *
 * disk_setup.c refuses to run abdisk / install / grub-install when the
 * GRUB image lengths are zero, so a stage-1 kernel never writes garbage.
 */
#include "grub_boot_data.h"

const unsigned char oc_grub_boot_img[1]   = { 0 };
const unsigned char oc_grub_core_img[1]   = { 0 };

const unsigned int oc_grub_boot_img_len   = 0;
const unsigned int oc_grub_core_img_len   = 0;
