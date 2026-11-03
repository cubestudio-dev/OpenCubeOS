/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_wizard.h
 * Purpose: the 9-step GUI install wizard. Every mutating action is a
 * real OCSECL command line run through ocse_cl_exec (mkpart,
 * mkfs.fat32, mount, install, config set, write, sync, umount,
 * reboot) - the wizard implements NO install logic of its own; it is
 * a controller over the same command surface the shell offers. A
 * failed command shows its captured output on the error page with
 * Retry/Back. The wizard never overwrites an existing OpenCubeOS
 * system: it partitions the largest free gap, formats only the
 * partition it created, and refuses a target that already boots
 * opencube.elf.
 */
#ifndef OCSE_WIZARD_H
#define OCSE_WIZARD_H

/* Button action codes shared by the pages (wz_add_button). */
enum {
    WZA_NEXT   = 1,
    WZA_BACK   = 2,
    WZA_CANCEL = 3,
    WZA_LANG   = 4,
    WZA_RESCAN = 5,
    WZA_CREATE = 6,
    WZA_FORMAT = 7,
    WZA_START  = 8,
    WZA_RETRY  = 9,
    WZA_YES    = 10,
    WZA_NO     = 11,
    WZA_REBOOT = 12,
    WZA_FINISH = 13,
};

void ocse_wizard_init(void);    /* work-zone state reset + first page */
void ocse_wizard_run(void);     /* modal: until Finish / Reboot / cancel */

#endif /* OCSE_WIZARD_H */
