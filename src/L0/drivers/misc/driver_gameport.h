/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_gameport.h
 *
 * PC game port (analog joystick port at I/O 0x201), implemented
 * against the classic IBM PC game adapter spec:
 *
 *   - one 8-bit register: bits 7..4 = push buttons 4..3..2..1
 *     (active low), bits 3..0 = axis one-shot outputs (X2 Y2 X1 Y1,
 *     high while the RC timer discharges)
 *   - any write to 0x201 fires all four one-shots; the time each bit
 *     stays high is proportional to the axis potentiometer
 *   - axis position = tick loop count until the bit drops, scaled
 *     0..255 against the calibration window (min / max / center)
 *   - an absent adapter / stick reads back stuck bits -> the driver
 *     reports a timeout honestly instead of inventing a position
 *
 * L1 extension surface (documented in docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   int gameport_register(gameport_dev_t *g);
 *   int gameport_unregister(gameport_dev_t *g);
 *   int gameport_count(void);
 *   gameport_dev_t *gameport_get(int idx);
 *   int gameport_read_axis(gameport_dev_t *g, u8 axis, u32 *out);
 *       axis 0..3 (X1 Y1 X2 Y2); *out is 0..255 or GAMEPORT_NO_AXIS
 *       on timeout (device absent or the stick does not settle).
 *   int gameport_read_buttons(gameport_dev_t *g, u8 *out);
 *       bit0..3 = buttons 1..4, set = pressed.
 *   int gameport_calibrate(gameport_dev_t *g);
 *       samples the min / max window per axis (user moves the stick
 *       during the call); center is the midpoint.
 */
#ifndef OC_DRIVER_GAMEPORT_H
#define OC_DRIVER_GAMEPORT_H

#include "types.h"

#define GAMEPORT_IO         0x201
#define GAMEPORT_MAX        4
#define GAMEPORT_NAME_MAX   8
#define GAMEPORT_NO_AXIS    0xFFFFFFFFu

typedef struct gameport_dev {
    char name[GAMEPORT_NAME_MAX];
    u16  io;
    u8   present;
    /* calibration per axis: raw min/max seen, center = midpoint */
    u32  cal_min[4];
    u32  cal_max[4];
    u32  cal_center[4];
    u8   calibrated;
    /* statistics */
    u32  reads;
    u32  timeouts;
} gameport_dev_t;

/* ---- subsystem ---- */
void gameport_init(void);
void gameport_probe_all(void);

/* ---- L1 extension interfaces ---- */
int  gameport_register(gameport_dev_t *g);
int  gameport_unregister(gameport_dev_t *g);
int  gameport_count(void);
gameport_dev_t *gameport_get(int idx);
int  gameport_read_axis(gameport_dev_t *g, u8 axis, u32 *out);
int  gameport_read_buttons(gameport_dev_t *g, u8 *out);
int  gameport_calibrate(gameport_dev_t *g);

#endif /* OC_DRIVER_GAMEPORT_H */
