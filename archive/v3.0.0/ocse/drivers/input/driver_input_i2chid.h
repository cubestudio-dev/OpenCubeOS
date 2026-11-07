/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/input/driver_input_i2chid.h
 * Purpose: Touchpad support - HID over I2C (i2c-hid, HID PNP0C50):
 *          device discovery through the ACPI namespace (_HID/_CRS with
 *          an I2CSerialBusV2 resource), the i2c-hid transport (HID
 *          descriptor, RESET, GET_REPORT), a HID report-descriptor
 *          parser (multitouch digitizer subset), a multitouch report
 *          decoder and a gesture engine (tap / scroll / pinch /
 *          swipe) feeding a kernel event queue.
 *
 * QEMU note (honest): QEMU ships no I2C-HID touchpad model, so on the
 * QEMU rigs the hardware enumeration path finds zero devices while
 * the full descriptor -> parse -> decode -> gesture pipeline is
 * exercised end-to-end through the compiled-in fixture by the
 * `touchtest` command and covered in docs.
 */
#ifndef OC_DRIVER_INPUT_I2CHID_H
#define OC_DRIVER_INPUT_I2CHID_H

#include "types.h"

/* ---- events produced by the gesture engine ---- */
typedef enum touchpad_evt_type {
    TPE_NONE = 0,
    TPE_DOWN,        /* contact placed (fingers = #contacts now) */
    TPE_UP,          /* all contacts lifted */
    TPE_TAP,         /* quick tap (x, y = tap position) */
    TPE_MOVE,        /* single-finger move (dx, dy) */
    TPE_SCROLL,      /* two-finger scroll (dy, also dx) */
    TPE_PINCH,       /* two-finger pinch (scale: >1 grow, <1 shrink) */
    TPE_SWIPE,       /* >= 3-finger swipe (dx, dy, fingers) */
} touchpad_evt_type_t;

typedef struct touchpad_event {
    touchpad_evt_type_t type;
    i32 x, y;        /* absolute position (report units) */
    i32 dx, dy;      /* delta (move/scroll/swipe) */
    i32 fingers;     /* contact count involved */
    i32 scale_pct;   /* pinch: distance change in percent */
    u32 time_ms;
} touchpad_event_t;

#define TOUCHPAD_MAX_FINGERS 5
#define TOUCHPAD_MAX_EVENTS  16

/* Initialize: scan the ACPI namespace for i2c-hid devices (HID
 * "PNP0C50"), bind each to its I2C bus, read + parse the HID
 * descriptor, fetch the report descriptor, reset the device. Returns
 * the number of touchpads found (0 = none present; not an error). */
int  driver_input_i2chid_init(void);

/* 1 when a live touchpad was bound. */
int  driver_input_i2chid_present(void);

/* Status getters (touchpad command). */
u16  driver_input_i2chid_vendor_id(void);
u16  driver_input_i2chid_product_id(void);
int  driver_input_i2chid_input_bus(void);
u16  driver_input_i2chid_input_addr(void);
u32  driver_input_i2chid_event_count(void);
u32  driver_input_i2chid_report_count(void);

/* Poll the device: drains pending input reports through the decoder
 * and gesture engine into the event queue. Called from shell / timer
 * context; harmless when no device is bound. */
void driver_input_i2chid_poll(void);

/* Take one queued gesture event (0 on success, -1 empty). */
int  driver_input_touchpad_get_event(touchpad_event_t *out);

/* ---- pipeline entry points (also driven by `touchtest`) ---- */

/* Parse a HID report descriptor (multitouch digitizer subset).
 * Returns 0 on success, -1 on malformed input. */
int  driver_input_touchpad_parse_report_descriptor(const u8 *desc, int len);

/* Decode one input report against the parsed descriptor; feeds the
 * gesture engine. Returns the number of events produced. */
int  driver_input_touchpad_decode_report(const u8 *report, int len);

/* Reset the gesture engine state (no event output). */
void driver_input_touchpad_gesture_reset(void);

/* Full pipeline self-test (compiled-in multitouch fixture): descriptor
 * parse + tap / scroll / pinch / swipe sequences through the decoder
 * and gesture engine. Returns the number of failed checks. */
int  driver_input_touchpad_selftest(void);

#endif /* OC_DRIVER_INPUT_I2CHID_H */
