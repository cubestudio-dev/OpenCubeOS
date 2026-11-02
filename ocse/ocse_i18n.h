/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_i18n.h
 * Purpose: OCSE message catalog (en / zh). Messages are stored as
 * UTF-8; ocse_out() converts zh output to GB2312 on the fly so the
 * console_cn renderer picks the glyphs up; the GUI renders UTF-8
 * directly through the unicode font tables.
 */
#ifndef OCSE_I18N_H
#define OCSE_I18N_H

#define OCSE_LANG_EN 0
#define OCSE_LANG_ZH 1

/* Message keys. */
enum {
    OCSE_T_TITLE = 0,
    OCSE_T_SUBTITLE,
    OCSE_T_HW_HEAD,
    OCSE_T_CHECK_CPU,
    OCSE_T_CHECK_MEM,
    OCSE_T_CHECK_DISP,
    OCSE_T_CHECK_DISK,
    OCSE_T_PASS,
    OCSE_T_FAIL,
    OCSE_T_DISK_HEAD,
    OCSE_T_NO_DISK,
    OCSE_T_SPACE_LOW,
    OCSE_T_BTN_CLI,
    OCSE_T_BTN_LANG,
    OCSE_T_BTN_CHECK,
    OCSE_T_GUI_HINT,
    OCSE_T_CLI_BANNER,
    OCSE_T_CLI_HELP,
    OCSE_T_LANG_NOW,
    OCSE_T_DONE,
    OCSE_T_HALT,
    /* WP-06b: OCSECL installer strings */
    OCSE_T_REBOOTING,
    OCSE_T_POWEROFF,
    OCSE_T_POWEROFF_NOACPI,
    OCSE_T_P_NODISK,
    OCSE_T_P_NOPART,
    OCSE_T_P_BADPART,
    OCSE_T_P_NOTABLE,
    OCSE_T_P_NOFREE,
    OCSE_T_P_OVERLAP,
    OCSE_T_P_TABLEFULL,
    OCSE_T_P_CREATED,
    OCSE_T_P_REMOVED,
    OCSE_T_P_FLAGSET,
    OCSE_T_P_BADFLAG,
    OCSE_T_P_MNT_WARN,
    OCSE_T_P_BADRANGE,
    /* WP-06b b6: installer-journey strings (mkfs/mount gates, the
     * install state machine, config, sync). The generic file-tool
     * output (ls/cat/cp/...) stays English: the batteries pin those
     * exact byte strings and the operator-critical safety/progress
     * messages are all covered below. */
    OCSE_T_M_WARN,        /* WARNING: formatting DESTROYS ... */
    OCSE_T_M_CONFIRMED,   /* '! Nothing was written.' tail */
    OCSE_T_M_NOWRITE,     /* 'Nothing was written.' */
    OCSE_T_M_MOUNTED,     /* ' is mounted - umount it first' tail */
    OCSE_T_M_FORMATTED,   /* 'formatted' word */
    OCSE_T_M_FMTFAIL,     /* 'format failed' word */
    OCSE_T_M_NOFS,        /* ' does not carry a mountable filesystem' */
    OCSE_T_M_MNTAT,       /* ') at ' inside the mount success line */
    OCSE_T_M_MOUNTEDW,    /* 'mounted ' word */
    OCSE_T_M_UNMOUNTEDW,  /* 'unmounted ' word */
    OCSE_T_M_UMNTFAIL,    /* ' failed (busy or not a mount point)' */
    OCSE_T_I_MEDIUMCHK,   /* install: checking the install medium ... */
    OCSE_T_I_MFATAL,      /* install: FATAL - medium incomplete */
    OCSE_T_I_MEDIUMOK,    /* install: medium ok (ocos.tar  */
    OCSE_T_I_TARGETW,     /* ' (fat32) mounted at ' inside target line */
    OCSE_T_I_NOTFAT,      /* not FAT32 refusal */
    OCSE_T_I_NOTFAT2,     /* ... format it with: mkfs.fat32 */
    OCSE_T_I_NOTMOUNTED,  /* not mounted refusal */
    OCSE_T_I_GATE,        /* OVERWRITES ... append 'confirm' */
    OCSE_T_I_EXTRACT,     /* install: extracting OCOS.TAR ... */
    OCSE_T_I_KERNELIN,    /* install: kernel in place ( */
    OCSE_T_I_BOOTCODE,    /* install: writing the boot code ... */
    OCSE_T_I_BOOTABLE,    /* ' marked bootable' tail */
    OCSE_T_I_COMPLETE,    /* Installation complete. */
    OCSE_T_I_REMOVE,      /* Remove the installation medium ... */
    OCSE_T_I_REBOOT,      /* or run: reboot */
    OCSE_T_O_BOOTCODE,    /* ocbl-install: boot code written ( ... */
    OCSE_T_O_VERIFIED,    /* ', partition table preserved, verified)' */
    OCSE_T_O_NOPART,      /* ocbl-install: no partition table */
    OCSE_T_C_INVALID,     /* config: invalid key or value */
    OCSE_T_C_KEYS,        /* config: keys: ... */
    OCSE_T_C_NOMOUNT,     /* config: no installed system mounted ... */
    OCSE_T_C_WRITTEN,     /* ' written' tail */
    OCSE_T_S_FLUSHED,     /* sync: block cache flushed */
    OCSE_T_COUNT_
};

void         ocse_i18n_init(void);
int          ocse_i18n_lang(void);
void         ocse_i18n_set(int lang);
const char*  ocse_tr(int key);       /* UTF-8 (zh) / ASCII (en) */
const char*  ocse_lang_name(int lang);/* "English" / "中文" */

/* Console+serial output line: converts zh to GB2312 for the console
 * renderer (the serial line gets the same bytes; QEMU log greps use
 * the en mode where everything is plain ASCII). */
void ocse_out(const char* s);
void ocse_outln(const char* s);

#endif /* OCSE_I18N_H */
