/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
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
    OCSE_T_BTN_INSTALL,   /* WP-06c: "Install now" (the 4th button) */
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
    /* WP-06c: config gains the two update keys (the wizard's system
     * step sets them; the CLI help line grows with them). */
    OCSE_T_C_KEYS2,       /* config: keys (extended, mentions update_url) */
    /* WP-06c: GUI wizard strings. The wizard renders UTF-8 through the
     * unicode font tables directly (no GB2312 hop). */
    OCSE_W_TITLE,         /* Install OpenCubeOS */
    OCSE_W_WELCOME1,      /* wizard intro line 1 */
    OCSE_W_WELCOME2,      /* wizard intro line 2 (OCSECL-driven) */
    OCSE_W_BTN_NEXT,      /* Next */
    OCSE_W_BTN_BACK,      /* Back */
    OCSE_W_BTN_CANCEL,    /* Cancel */
    OCSE_W_BTN_RETRY,     /* Retry */
    OCSE_W_BTN_LANG,      /* Language */
    OCSE_W_BTN_FINISH,    /* Finish */
    OCSE_W_BTN_REBOOT,    /* Reboot now */
    OCSE_W_BTN_CREATE,    /* Create partition */
    OCSE_W_BTN_FORMAT,    /* Format as FAT32 */
    OCSE_W_BTN_RESCAN,    /* Rescan */
    OCSE_W_BTN_START,     /* Start installation */
    OCSE_W_STEP_LANG,     /* step 1 name: welcome / language */
    OCSE_W_STEP_DISK,     /* step 2 name */
    OCSE_W_STEP_PART,     /* step 3 name */
    OCSE_W_STEP_FMT,      /* step 4 name */
    OCSE_W_STEP_USER,     /* step 5 name */
    OCSE_W_STEP_SYS,      /* step 6 name */
    OCSE_W_STEP_CONFIRM,  /* step 7 name */
    OCSE_W_STEP_INSTALL,  /* step 8 name */
    OCSE_W_STEP_DONE,     /* step 9 name */
    OCSE_W_LANG_HEAD,     /* choose the language */
    OCSE_W_DISK_HEAD,     /* choose the disk */
    OCSE_W_DISK_NONE,     /* no disk found */
    OCSE_W_DISK_NOOVER,   /* never overwrites an existing system */
    OCSE_W_PART_HEAD,     /* partitioning */
    OCSE_W_PART_AUTO,     /* use the largest free gap (recommended) */
    OCSE_W_PART_GAP,      /* largest free gap: */
    OCSE_W_PART_NOGAP,    /* no free space */
    OCSE_W_PART_MADE,     /* partition created: */
    OCSE_W_PART_REUSE,    /* existing partition will be used */
    OCSE_W_FMT_HEAD,      /* format */
    OCSE_W_FMT_ONLY,      /* why FAT32 (OCBL contract) */
    OCSE_W_FMT_WARN,      /* destroys data warning */
    OCSE_W_FMT_DONE,      /* formatted. */
    OCSE_W_USER_HEAD,     /* create the primary user */
    OCSE_W_USER_NAME,     /* login name */
    OCSE_W_USER_FULL,     /* full name */
    OCSE_W_USER_PASS,     /* password */
    OCSE_W_USER_NOTE,     /* SHA-256 note */
    OCSE_W_USER_SHORT,    /* login name too short / bad chars */
    OCSE_W_SYS_HEAD,      /* system configuration */
    OCSE_W_SYS_HOST,      /* hostname */
    OCSE_W_SYS_UPD,       /* update check URL (empty = default) */
    OCSE_W_SYS_AUTO,      /* automatic update check (yes/no) */
    OCSE_W_CONF_HEAD,     /* review */
    OCSE_W_CONF_DISK,     /* disk */
    OCSE_W_CONF_PART,     /* partition */
    OCSE_W_CONF_USER,     /* user */
    OCSE_W_CONF_HOST,     /* hostname */
    OCSE_W_CONF_GO,       /* runs the real OCSECL commands note */
    OCSE_W_INST_PROG,     /* installing - stage */
    OCSE_W_INST_DONE,     /* installation complete. */
    OCSE_W_INST_REMOVE,   /* remove the medium and reboot */
    OCSE_W_ERR_HEAD,      /* a command failed */
    OCSE_W_ERR_CMD,       /* command: */
    OCSE_W_ERR_OUT,       /* output: */
    OCSE_W_CANCEL_Q,      /* cancel the installation? */
    OCSE_W_CANCEL_CLEAN,  /* nothing written yet - safe */
    OCSE_W_CANCEL_WROT,   /* data already written - sure? */
    OCSE_W_YES,           /* Yes */
    OCSE_W_NO,            /* No */
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

/* WP-06c: output capture for the wizard's command channel. When a
 * capture buffer is set, ocse_out/outln append into it INSTEAD of the
 * console (the serial line still gets a copy - the QEMU battery greps
 * it) and the captured bytes let the wizard show a failed command's
 * output on its error page. Nested capture is not needed. */
void ocse_out_capture(char* buf, int cap);
void ocse_out_capture_off(void);

#endif /* OCSE_I18N_H */
