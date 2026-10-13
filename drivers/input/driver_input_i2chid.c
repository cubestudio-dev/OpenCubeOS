/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/input/driver_input_i2chid.c
 * Purpose: HID-over-I2C touchpad implementation. See the header for
 *          the pipeline and the QEMU honesty note.
 *
 * Parts:
 *   A. i2c-hid transport (descriptor / reset / get-report framing)
 *   B. ACPI discovery (PNP0C50 + I2CSerialBusV2 in _CRS)
 *   C. HID report descriptor parser (digitizer multitouch subset)
 *   D. multitouch report decoder
 *   E. gesture engine (tap / scroll / pinch / swipe)
 */
#include "driver_input_i2chid.h"
#include "driver_i2c.h"
#include "driver_acpi.h"
#include "driver_acpi_aml.h"
#include "core_timer.h"
#include "lib_string.h"
#include "lib_log.h"

#include <stdint.h>

/* ================================================================= */
/* C. HID report descriptor parser (multitouch digitizer subset)      */
/* ================================================================= */

#define HID_UP_GENDESK  0x01
#define HID_UP_BUTTON   0x09
#define HID_UP_DIGITIZER 0x0D

#define HIDU_X          0x30
#define HIDU_Y          0x31
#define HIDU_WHEEL      0x38
#define HIDU_TIP        0x42
#define HIDU_INRANGE    0x44
#define HIDU_WIDTH      0x48
#define HIDU_HEIGHT     0x49
#define HIDU_PRESSURE   0x30    /* digitizer page */
#define HIDU_CONTACT_ID 0x51
#define HIDU_CONTACT_COUNT 0x54
#define HIDU_CONTACT_MAX   0x55
#define HIDU_SCAN_TIME     0x56

typedef enum { HFMT_TIP, HFMT_INRANGE, HFMT_CID, HFMT_X, HFMT_Y,
               HFMT_PRESSURE, HFMT_CONTACTCOUNT, HFMT_PAD, HFMT_BUTTON
} hid_field_kind_t;

typedef struct {
    hid_field_kind_t kind;
    u32 bit_off;
    u32 size;         /* bits */
    i32 lmin, lmax;
} hid_field_t;

#define HID_MAX_FIELDS 64
#define HID_MAX_REPORTS 4

typedef struct {
    u8  report_id;
    int field_count;
    hid_field_t fields[HID_MAX_FIELDS];
    u32 total_bits;
} hid_report_layout_t;

static hid_report_layout_t g_layouts[HID_MAX_REPORTS];
static int g_layout_count;
static int g_pad_ok;

/* item encoder: size field bits 1:0 (0=1,1=2,2=4), type bits 3:2 */
static int hid_parse_descriptor(const u8 *d, int len) {
    memset(g_layouts, 0, sizeof(g_layouts));
    g_layout_count = 0;
    g_pad_ok = 1;
    u32 usage_page = 0, usage = 0;
    u32 lmin = 0, lmax = 0, rsize = 0, rcount = 0;
    int cur = -1;
    int parse_err = 0;
    int i = 0;
    while (i < len && !parse_err) {
        u8 item = d[i++];
        u8 key = item & 0xFC;
        u32 size_c = item & 3;
        int dsz = (size_c == 3) ? 4 : (int)size_c;
        u32 sval = 0;
        i32 sval_s = 0;
        if (i + dsz > len) break;
        for (int k = 0; k < dsz; k++) sval |= (u32)d[i + k] << (8 * k);
        sval_s = (i32)sval;
        i += dsz;
        (void)sval_s;

        switch (key) {
        case 0x04: usage_page = sval; break;                 /* Usage Page */
        case 0x08: usage = sval; break;                      /* Usage */
        case 0x14: lmin = (u32)sval_s; break;                /* Logical Min */
        case 0x24: lmax = sval; break;                       /* Logical Max */
        case 0x34: /* Physical Min */ break;
        case 0x44: /* Physical Max */ break;
        case 0x54: /* Unit */ break;
        case 0x64: /* Unit Exp */ break;
        case 0x74: rsize = sval; break;                      /* Report Size */
        case 0x94: rcount = sval; break;                     /* Report Count */
        case 0x84: /* Report ID */
            if (g_layout_count < HID_MAX_REPORTS) {
                cur = g_layout_count;
                g_layouts[cur].report_id = (u8)sval;
                g_layout_count++;
            }
            break;
        case 0xA0:                                           /* Collection */
            /* digitizer/application nesting tracked implicitly by usages */
            break;
        case 0xC0: break;                                    /* End Collection */
        case 0x80: {                                         /* Input */
            if (cur < 0 || cur >= HID_MAX_REPORTS) { cur = 0; if (!g_layout_count) g_layout_count = 1; }
            if (rcount && rsize && g_layouts[cur].field_count + (int)rcount <= HID_MAX_FIELDS) {
                /* BUG-FIX (WP-10-input_chipset): the Data/Constant and
                 * Array/Variable flags live in the ITEM'S DATA BYTE
                 * (sval), not in the item header's low bits - those
                 * encode the data size (0x81 = key 0x80 + 1-byte data).
                 * Reading the header bits made every Data,Var,Abs field
                 * classify as an Array -> HFMT_BUTTON and every Const
                 * pad classify as a usage field, so the decoder never
                 * saw a tip / contact-id / x / y at all. */
                u32 const_bit = sval & 0x01;                 /* 1 = Constant */
                u32 var_bit   = sval & 0x02;                 /* 1 = Variable */
                for (u32 n = 0; n < rcount; n++) {
                    hid_field_t *f =
                        &g_layouts[cur].fields[g_layouts[cur].field_count];
                    f->bit_off = g_layouts[cur].total_bits + n * rsize;
                    f->size    = rsize;
                    f->lmin    = (i32)lmin;
                    f->lmax    = (i32)lmax;
                    if (const_bit) { f->kind = HFMT_PAD; }
                    else if (!var_bit) { f->kind = HFMT_BUTTON; }
                    else {
                        u32 u = usage;
                        if (usage_page == HID_UP_DIGITIZER) {
                            switch (u) {
                            case HIDU_TIP:        f->kind = HFMT_TIP; break;
                            case HIDU_INRANGE:    f->kind = HFMT_INRANGE; break;
                            case HIDU_CONTACT_ID: f->kind = HFMT_CID; break;
                            case HIDU_PRESSURE:   f->kind = HFMT_PRESSURE; break;
                            case HIDU_CONTACT_COUNT: f->kind = HFMT_CONTACTCOUNT; break;
                            default:              f->kind = HFMT_PAD; break;
                            }
                        } else if (usage_page == HID_UP_GENDESK) {
                            switch (u) {
                            case HIDU_X: f->kind = HFMT_X; break;
                            case HIDU_Y: f->kind = HFMT_Y; break;
                            default:     f->kind = HFMT_PAD; break;
                            }
                        } else if (usage_page == HID_UP_BUTTON) {
                            f->kind = HFMT_BUTTON;
                        } else {
                            f->kind = HFMT_PAD;
                        }
                    }
                    g_layouts[cur].field_count++;
                }
                g_layouts[cur].total_bits += rcount * rsize;
                if (g_layouts[cur].total_bits > 256) parse_err = 1;
            }
            break;
        }
        case 0x90: case 0xB0: case 0xB4: case 0xB8:          /* Output/Feature */
            /* skip: descriptors may declare outputs; not consumed */
            break;
        default:
            break;
        }
    }
    if (parse_err) return -1;
    return g_layout_count > 0 ? 0 : -1;
}

/* ================================================================= */
/* D. report decoder + E. gesture engine                               */
/* ================================================================= */

typedef struct {
    int  active;
    u32  down_ms;
    i32  down_x, down_y;
    i32  x, y;
    i32  pressure;
    i32  tip;
} tp_finger_t;

static tp_finger_t g_fingers[TOUCHPAD_MAX_FINGERS];
static i32 g_contact_count;
static i32 g_last_count;
static u32 g_report_count;
static u32 g_event_count;

static touchpad_event_t g_events[TOUCHPAD_MAX_EVENTS];
static int g_ev_head, g_ev_tail;

static i32 g_scroll_accum_x, g_scroll_accum_y;
static i32 g_pinch_last_dist;
static i32 g_pinch_base_dist;
static u32 g_gest_start_ms;
static int g_two_active, g_three_active;
static i32 g_swipe_dx, g_swipe_dy;

static void push_event(const touchpad_event_t *e) {
    int next = (g_ev_head + 1) % TOUCHPAD_MAX_EVENTS;
    if (next == g_ev_tail) {   /* full: drop oldest */
        g_ev_tail = (g_ev_tail + 1) % TOUCHPAD_MAX_EVENTS;
    }
    g_events[g_ev_head] = *e;
    g_ev_head = next;
    g_event_count++;
}

int driver_input_touchpad_get_event(touchpad_event_t *out) {
    if (!out || g_ev_head == g_ev_tail) return -1;
    *out = g_events[g_ev_tail];
    g_ev_tail = (g_ev_tail + 1) % TOUCHPAD_MAX_EVENTS;
    return 0;
}

static u64 tp_now_ms(void) { return core_timer_now_ms(); }

void driver_input_touchpad_gesture_reset(void) {
    memset(g_fingers, 0, sizeof(g_fingers));
    g_contact_count = 0;
    g_last_count = 0;
    g_scroll_accum_x = g_scroll_accum_y = 0;
    g_pinch_last_dist = 0;
    g_pinch_base_dist = 0;
    g_two_active = 0;
    g_three_active = 0;
    g_swipe_dx = g_swipe_dy = 0;
}

static i32 abs32(i32 v) { return v < 0 ? -v : v; }

/* called when a frame is complete (contact count consumed) */
static void tp_frame_done(u32 now) {
    int count = 0;
    for (int i = 0; i < TOUCHPAD_MAX_FINGERS; i++)
        if (g_fingers[i].tip) count++;
    g_contact_count = count;

    if (count == 0 && g_last_count > 0) {
        /* everything lifted: classify the gesture */
        u32 dt = now - g_gest_start_ms;
        if (g_three_active) {
            touchpad_event_t e;
            memset(&e, 0, sizeof(e));
            e.type = TPE_SWIPE;
            e.dx = g_swipe_dx; e.dy = g_swipe_dy;
            e.fingers = 3;
            e.time_ms = now;
            if (abs32(g_swipe_dx) > 8 || abs32(g_swipe_dy) > 8) push_event(&e);
        } else if (g_two_active) {
            if (g_scroll_accum_x || g_scroll_accum_y) {
                touchpad_event_t e;
                memset(&e, 0, sizeof(e));
                e.type = TPE_SCROLL;
                e.dx = g_scroll_accum_x;
                e.dy = g_scroll_accum_y;
                e.fingers = 2;
                e.time_ms = now;
                push_event(&e);
            }
            if (g_pinch_base_dist) {
                i32 delta = (i32)(((i64)g_pinch_last_dist - g_pinch_base_dist) * 100 / g_pinch_base_dist);
                if (abs32(delta) >= 5) {
                    touchpad_event_t e;
                    memset(&e, 0, sizeof(e));
                    e.type = TPE_PINCH;
                    e.scale_pct = delta;
                    e.fingers = 2;
                    e.time_ms = now;
                    push_event(&e);
                }
            }
        } else {
            /* single contact: tap or move */
            for (int i = 0; i < TOUCHPAD_MAX_FINGERS; i++) {
                if (g_fingers[i].active && !g_fingers[i].tip) {
                    tp_finger_t *f = &g_fingers[i];
                    if (f->down_ms && dt <= 300 &&
                        abs32(f->x - f->down_x) < 10 &&
                        abs32(f->y - f->down_y) < 10) {
                        touchpad_event_t e;
                        memset(&e, 0, sizeof(e));
                        e.type = TPE_TAP;
                        e.x = f->x; e.y = f->y;
                        e.fingers = 1;
                        e.time_ms = now;
                        push_event(&e);
                    }
                    f->down_ms = 0;
                }
                g_fingers[i].active = 0;
                g_fingers[i].tip = 0;
            }
        }
        g_scroll_accum_x = g_scroll_accum_y = 0;
        g_pinch_base_dist = 0;
        g_pinch_last_dist = 0;
        g_two_active = 0;
        g_three_active = 0;
        g_swipe_dx = g_swipe_dy = 0;
        touchpad_event_t e;
        memset(&e, 0, sizeof(e));
        e.type = TPE_UP;
        e.fingers = g_last_count;
        e.time_ms = now;
        push_event(&e);
    }
    g_last_count = count;
}

int driver_input_touchpad_decode_report(const u8 *report, int len) {
    if (g_layout_count <= 0) return -1;
    hid_report_layout_t *L = &g_layouts[0];
    if (L->report_id && (!len || report[0] != L->report_id)) {
        /* report-id prefixed layouts not used by our fixtures */
        return -1;
    }
    u32 now = (u32)tp_now_ms();
    int produced = 0;

    i32 f_x = 0, f_y = 0, f_cid = 0, f_tip = 0, f_pr = 0;
    int seen_count_field = 0;

    for (int i = 0; i < L->field_count; i++) {
        hid_field_t *f = &L->fields[i];
        if ((f->bit_off + f->size) > (u32)(len * 8)) return -1;
        u64 raw = 0;
        for (u32 b = 0; b < f->size; b++) {
            u32 bit = f->bit_off + b;
            raw |= (u64)((report[bit >> 3] >> (bit & 7)) & 1) << b;
        }
        i64 signed_v = (i64)raw;
        if (f->lmin < 0 && f->size > 1 && f->size < 32) {
            /* sign-extended field (two's complement over `size` bits) */
            u64 sign = 1ull << (f->size - 1);
            if (raw & sign) signed_v -= (i64)(1ull << f->size);
        }
        i32 v = (i32)signed_v;
        switch (f->kind) {
        case HFMT_TIP:         f_tip = v; break;
        case HFMT_CID:         f_cid = v; break;
        case HFMT_X:           f_x = v; break;
        case HFMT_Y:           f_y = v; break;
        case HFMT_PRESSURE:    f_pr = v; break;
        case HFMT_CONTACTCOUNT: seen_count_field = 1; break;
        default: break;
        }
        if (f->kind == HFMT_CONTACTCOUNT || i == L->field_count - 1) {
            /* end of one touch packet: commit the finger */
            if (f->kind != HFMT_CONTACTCOUNT || seen_count_field) {
                if (f_cid >= 0 && f_cid < TOUCHPAD_MAX_FINGERS) {
                    tp_finger_t *fg = &g_fingers[f_cid];
                    if (f_tip && !fg->tip && !fg->active) {
                        memset(fg, 0, sizeof(*fg));
                        fg->active = 1;
                        fg->down_ms = now;
                        fg->down_x = f_x;
                        fg->down_y = f_y;
                        if (!g_two_active && !g_three_active) g_gest_start_ms = now;
                    }
                    if (fg->active) {
                        i32 dx = f_x - fg->x;
                        i32 dy = f_y - fg->y;
                        fg->x = f_x;
                        fg->y = f_y;
                        fg->tip = f_tip;
                        fg->pressure = f_pr;
                        /* classify the current contact count */
                        int tips = 0;
                        for (int q = 0; q < TOUCHPAD_MAX_FINGERS; q++)
                            if (g_fingers[q].active && g_fingers[q].tip) tips++;
                        if (tips >= 3) {
                            g_three_active = 1;
                            g_two_active = 0;
                            g_swipe_dx += dx;
                            g_swipe_dy += dy;
                        } else if (tips == 2) {
                            g_two_active = 1;
                            g_scroll_accum_x += dx;
                            g_scroll_accum_y += dy;
                        }
                    }
                }
                f_x = f_y = f_cid = f_tip = f_pr = 0;
            }
        }
    }

    /* two-finger pinch: track distance when two tips down */
    int ids[TOUCHPAD_MAX_FINGERS], nid = 0;
    for (int i = 0; i < TOUCHPAD_MAX_FINGERS; i++)
        if (g_fingers[i].active && g_fingers[i].tip) ids[nid++] = i;
    if (nid == 2) {
        tp_finger_t *a = &g_fingers[ids[0]];
        tp_finger_t *b = &g_fingers[ids[1]];
        i32 dx = a->x - b->x, dy = a->y - b->y;
        i32 dist = (i32)((dx * dx + dy * dy) >> 4);   /* scaled distance */
        if (!g_pinch_base_dist) {
            g_pinch_base_dist = dist;
            g_pinch_last_dist = dist;
        } else {
            g_pinch_last_dist = dist;
        }
        g_two_active = 1;
    } else if (nid < 2 && !g_three_active) {
        /* keep accumulators for scroll even with 1 finger drifting */
    }

    g_report_count++;
    (void)produced;
    tp_frame_done(now);
    return 0;
}

/* ================================================================= */
/* A. i2c-hid transport + B. ACPI discovery                            */
/* ================================================================= */

/* HID descriptor (i2c-hid spec, 30 bytes). */
typedef struct {
    u16 len;
    u16 bcd_version;
    u8  report_desc_len;
    u16 vendor_id, product_id, version_id;
    u16 report_desc_register;
    u16 input_register;
    u16 max_input_len;
    u16 output_register;
    u16 max_output_len;
    u16 command_register;
    u16 data_register;
    u16 vendor_page;
} i2chid_desc_t;

static int g_present_tp;
static i2chid_desc_t g_desc;
static int g_tp_bus = -1;
static u16 g_tp_addr;
static u8 g_rdesc[256];
static int g_rdesc_len;

static int tp_read_reg(u16 reg, u8 *out, int len) {
    u8 w[2] = { (u8)(reg & 0xFF), (u8)(reg >> 8) };
    return driver_i2c_transfer(g_tp_bus, g_tp_addr, 0, w, 2, out, len);
}

static int tp_command(u16 opcode, u8 data) {
    u8 w[4];
    w[0] = (u8)(g_desc.command_register & 0xFF);
    w[1] = (u8)(g_desc.command_register >> 8);
    w[2] = opcode;
    w[3] = data;
    return driver_i2c_transfer(g_tp_bus, g_tp_addr, 0, w, 4, NULL, 0);
}

/* Parse the I2CSerialBusV2 resource from a _CRS buffer.
 * Returns 0 and fills bus type / addressing / slave address / speed. */
static int parse_i2c_serialbus(const u8 *crs, int len, u16 *addr_out,
                               int *addr10_out) {
    int i = 0;
    while (i < len) {
        u8 type = crs[i];
        if (type == 0x79) break;   /* END_TAG */
        int small = (type & 0x80) == 0;
        if (small) {
            i += 1 + ((type >> 3) & 0x0F);   /* header byte + payload */
            continue;
        } else {
            if (i + 3 > len) return -1;
            u16 big = (u16)crs[i + 1] | ((u16)crs[i + 2] << 8);
            u8 name = type & 0x7F;
            if (name == 0x0E) {   /* SerialBusConnection (large item 0x8E) */
                /* body: rev, srcidx, srclen, bustype... */
                int body = i + 3;
                if (body + big > len) return -1;
                const u8 *r = crs + body;
                /* SerialBusCommonDesc: RevId(1) SrcIndex(1) SrcLen(1)
                 * BusType(1) Flags(2) TypeSpecificFlags(2)
                 * then I2C: AccessMode(1) SlaveAddress(2)... */
                if (big < 13) { i = body + big; continue; }
                u8 bus_type = r[3];
                if (bus_type != 0x01) { i = body + big; continue; }  /* 1 = I2C */
                u16 tsf = (u16)r[6] | ((u16)r[7] << 8);
                int a10 = tsf & 1;
                u16 slave = (u16)r[11] | ((u16)r[12] << 8);
                if (addr_out) *addr_out = slave;
                if (addr10_out) *addr10_out = a10;
                return 0;
            }
            i = i + 3 + big;
        }
    }
    return -1;
}

int driver_input_i2chid_init(void) {
    g_present_tp = 0;
    g_tp_bus = -1;
    g_tp_addr = 0;

    if (driver_acpi_aml_loaded() <= 0 && !driver_acpi_aml_loaded()) {
        /* namespace not loaded: nothing to discover */
        return 0;
    }

    int ndev = driver_acpi_aml_device_count();
    for (int i = 0; i < ndev && !g_present_tp; i++) {
        acpi_aml_dev_info_t info;
        if (driver_acpi_aml_device_info(i, &info) != 0) continue;
        if (!info.is_device) continue;
        int hid_ok = strcmp(info.hid, "PNP0C50") == 0;
        int cid_ok = strcmp(info.cid, "PNP0C50") == 0;
        if (!hid_ok && !cid_ok) continue;

        /* _CRS gives the I2C bus resources (serial bus descriptor) */
        u8 crs[256]; int crs_len = 0;
        char path[100];
        strcpy(path, "\\");
        strcat(path, info.path);
        if (driver_acpi_aml_eval_buffer(path, crs, sizeof(crs), &crs_len) != 0)
            continue;
        u16 slave = 0; int a10 = 0;
        if (parse_i2c_serialbus(crs, crs_len, &slave, &a10) != 0) continue;

        /* bind to the first registered I2C bus (single-controller rigs) */
        if (driver_i2c_count() < 1) continue;
        g_tp_bus = 0;
        g_tp_addr = slave;

        /* HID descriptor read: write 0x0000, read 30 bytes */
        u8 raw[34];
        if (tp_read_reg(0x0000, raw, 30) != 0) {
            g_tp_bus = -1;
            continue;
        }
        g_desc.len = (u16)(raw[0] | (raw[1] << 8));
        g_desc.bcd_version = (u16)(raw[2] | (raw[3] << 8));
        g_desc.report_desc_len = raw[4];
        g_desc.vendor_id = (u16)(raw[6] | (raw[7] << 8));
        g_desc.product_id = (u16)(raw[8] | (raw[9] << 8));
        g_desc.version_id = (u16)(raw[10] | (raw[11] << 8));
        g_desc.report_desc_register = (u16)(raw[12] | (raw[13] << 8));
        g_desc.input_register = (u16)(raw[14] | (raw[15] << 8));
        g_desc.max_input_len = (u16)(raw[16] | (raw[17] << 8));
        g_desc.command_register = (u16)(raw[22] | (raw[23] << 8));
        g_desc.data_register = (u16)(raw[24] | (raw[25] << 8));
        if (g_desc.len < 30 || g_desc.len > 1024) {
            g_tp_bus = -1;
            continue;
        }

        /* report descriptor */
        int rd_len = g_desc.report_desc_len;
        if (rd_len > (int)sizeof(g_rdesc)) rd_len = (int)sizeof(g_rdesc);
        if (tp_read_reg(g_desc.report_desc_register, g_rdesc, rd_len) != 0) {
            g_tp_bus = -1;
            continue;
        }
        g_rdesc_len = rd_len;
        if (hid_parse_descriptor(g_rdesc, g_rdesc_len) != 0) {
            g_tp_bus = -1;
            continue;
        }

        /* RESET (opcode 0x01, data 0) then SET_POWER on (0x08, 0x00) */
        if (tp_command(0x01, 0x00) != 0) {
            g_tp_bus = -1;
            continue;
        }
        (void)tp_command(0x08, 0x00);

        g_present_tp = 1;
        {
            char line[80]; char n[8];
            strcpy(line, "touchpad: i2c-hid bound at 0x");
            u64_to_hex(g_tp_addr, n, 2); strcat(line, n);
            OC_LOG_OK(line);
        }
    }

    if (!g_present_tp) return 0;
    return 1;
}

void driver_input_i2chid_poll(void) {
    if (!g_present_tp) return;
    u8 rep[64];
    int maxlen = g_desc.max_input_len;
    if (maxlen > (int)sizeof(rep)) maxlen = (int)sizeof(rep);
    if (maxlen < 2) return;
    if (tp_read_reg(g_desc.input_register, rep, maxlen) != 0) return;
    (void)driver_input_touchpad_decode_report(rep, maxlen);
}

/* ---- status getters ---- */

int driver_input_i2chid_present(void)     { return g_present_tp; }
u16 driver_input_i2chid_vendor_id(void)   { return g_desc.vendor_id; }
u16 driver_input_i2chid_product_id(void)  { return g_desc.product_id; }
int driver_input_i2chid_input_bus(void)   { return g_tp_bus; }
u16 driver_input_i2chid_input_addr(void)  { return g_tp_addr; }
u32 driver_input_i2chid_event_count(void) { return g_event_count; }
u32 driver_input_i2chid_report_count(void){ return g_report_count; }

/* ================================================================= */
/* fixture-driven self test (`touchtest`)                              */
/* ================================================================= */

/*
 * Fixture report descriptor (multitouch type-B touchpad, 1 finger per
 * report + contact count):
 *   bit0     Tip Switch        bit1     In Range
 *   bits2-7  pad               bits8-15 Contact ID
 *   bits16-31 X (16 bit)       bits32-47 Y (16 bit)
 *   bits48-55 Contact Count
 * Report length: 7 bytes.
 */
static const u8 tp_fixture_rdesc[] = {
    0x05, 0x0D,                    /* Usage Page (Digitizers)   */
    0x09, 0x05,                    /* Usage (Touch Pad)         */
    0xA1, 0x01,                    /* Collection (Application)  */
    0x09, 0x22,                    /*   Usage (Finger)          */
    0xA1, 0x00,                    /*   Collection (Physical)   */
    0x09, 0x42,                    /*     Usage (Tip Switch)    */
    0x15, 0x00, 0x25, 0x01,        /*     Logical 0..1          */
    0x75, 0x01, 0x95, 0x01,        /*     Size 1, Count 1       */
    0x81, 0x02,                    /*     Input (Data,Var,Abs)  */
    0x09, 0x44,                    /*     Usage (In Range)      */
    0x81, 0x02,                    /*     Input                 */
    0x95, 0x06,                    /*     Count 6 (pad)         */
    0x81, 0x03,                    /*     Input (Const,Var,Abs) */
    0x09, 0x51,                    /*     Usage (Contact ID)    */
    0x25, 0x09, 0x75, 0x08, 0x95, 0x01,
    0x81, 0x02,                    /*     Input                 */
    0x05, 0x01,                    /*     Usage Page (Generic)  */
    0x09, 0x30,                    /*     Usage (X)             */
    0x26, 0xFF, 0x0F,              /*     Logical Max 4095      */
    0x75, 0x10, 0x95, 0x01,
    0x81, 0x02,                    /*     Input                 */
    0x09, 0x31,                    /*     Usage (Y)             */
    0x81, 0x02,                    /*     Input                 */
    0xC0,                          /*   End Collection          */
    0x05, 0x0D,                    /*   Usage Page (Digitizers) - re-declared
                                    *   before Contact Count, exactly like
                                    *   real Windows-Precision-touchpad
                                    *   descriptors do after the GenDesk
                                    *   X/Y section */
    0x09, 0x54,                    /*   Usage (Contact Count)   */
    0x25, 0x09, 0x75, 0x08, 0x95, 0x01,
    0x81, 0x02,                    /*   Input                   */
    0xC0,                          /* End Collection            */
};

static void fixture_report(u8 *r, u8 tip, u8 inrange, u8 cid,
                           u16 x, u16 y, u8 count) {
    r[0] = (u8)((tip ? 1 : 0) | (inrange ? 2 : 0));
    r[1] = cid;
    r[2] = (u8)(x & 0xFF);
    r[3] = (u8)(x >> 8);
    r[4] = (u8)(y & 0xFF);
    r[5] = (u8)(y >> 8);
    r[6] = count;
}

/* full pipeline self-test: descriptor parse + synthetic multitouch
 * sequences through the decoder + gesture engine.
 * Returns 0 on success, otherwise a bitmask of failed items:
 * bit0 = descriptor parse, bit1 = tap, bit2 = scroll, bit3 = pinch,
 * bit4 = swipe (so the shell can name each failing check). */
int driver_input_touchpad_selftest(void);

/* exposed for the shell test command */
int driver_input_touchpad_selftest(void) {
    u32 mask = 0;
    touchpad_event_t e;

    if (hid_parse_descriptor(tp_fixture_rdesc, sizeof(tp_fixture_rdesc)) != 0)
        return 1;   /* bit0: nothing after a failed parse is meaningful */
    driver_input_touchpad_gesture_reset();

    /* --- 1. tap: down, no move, up --- */
    u8 r[8];
    fixture_report(r, 1, 1, 0, 100, 100, 1);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 0, 0, 0, 100, 100, 0);
    driver_input_touchpad_decode_report(r, 7);
    int got_tap = 0;
    while (driver_input_touchpad_get_event(&e) == 0)
        if (e.type == TPE_TAP) got_tap = 1;
    if (!got_tap) mask |= 1u << 1;

    /* --- 2. two-finger scroll: y grows 30 units --- */
    driver_input_touchpad_gesture_reset();
    fixture_report(r, 1, 1, 0, 200, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 1, 260, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    for (int k = 0; k < 3; k++) {
        fixture_report(r, 1, 1, 0, 200, (u16)(210 + k * 10), 2);
        driver_input_touchpad_decode_report(r, 7);
        fixture_report(r, 1, 1, 1, 260, (u16)(210 + k * 10), 2);
        driver_input_touchpad_decode_report(r, 7);
    }
    fixture_report(r, 0, 0, 0, 200, 240, 1);
    driver_input_touchpad_decode_report(r, 7);   /* first finger up */
    fixture_report(r, 0, 0, 1, 260, 240, 0);
    driver_input_touchpad_decode_report(r, 7);   /* second finger up: all lifted */
    int got_scroll = 0;
    while (driver_input_touchpad_get_event(&e) == 0)
        if (e.type == TPE_SCROLL && e.dy > 0) got_scroll = 1;
    if (!got_scroll) mask |= 1u << 2;

    /* --- 3. pinch: fingers spread then close --- */
    driver_input_touchpad_gesture_reset();
    fixture_report(r, 1, 1, 0, 100, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 1, 140, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 0, 60, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 1, 180, 200, 2);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 0, 0, 0, 0, 0, 1);
    driver_input_touchpad_decode_report(r, 7);   /* first finger up */
    fixture_report(r, 0, 0, 1, 0, 0, 0);
    driver_input_touchpad_decode_report(r, 7);   /* second finger up: all lifted */
    int got_pinch = 0;
    while (driver_input_touchpad_get_event(&e) == 0)
        if (e.type == TPE_PINCH && e.scale_pct > 0) got_pinch = 1;
    if (!got_pinch) mask |= 1u << 3;

    /* --- 4. three-finger swipe: dy dominant --- */
    driver_input_touchpad_gesture_reset();
    fixture_report(r, 1, 1, 0, 100, 300, 3);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 1, 200, 300, 3);
    driver_input_touchpad_decode_report(r, 7);
    fixture_report(r, 1, 1, 2, 300, 300, 3);
    driver_input_touchpad_decode_report(r, 7);
    for (int k = 0; k < 4; k++) {
        fixture_report(r, 1, 1, 0, 100, (u16)(310 + k * 10), 3);
        driver_input_touchpad_decode_report(r, 7);
        fixture_report(r, 1, 1, 1, 200, (u16)(310 + k * 10), 3);
        driver_input_touchpad_decode_report(r, 7);
        fixture_report(r, 1, 1, 2, 300, (u16)(310 + k * 10), 3);
        driver_input_touchpad_decode_report(r, 7);
    }
    fixture_report(r, 0, 0, 0, 0, 0, 2);
    driver_input_touchpad_decode_report(r, 7);   /* finger 1 up */
    fixture_report(r, 0, 0, 1, 0, 0, 1);
    driver_input_touchpad_decode_report(r, 7);   /* finger 2 up */
    fixture_report(r, 0, 0, 2, 0, 0, 0);
    driver_input_touchpad_decode_report(r, 7);   /* finger 3 up: all lifted */
    int got_swipe = 0;
    while (driver_input_touchpad_get_event(&e) == 0)
        if (e.type == TPE_SWIPE && e.dy > 0) got_swipe = 1;   /* pads move downward (+y) */
    if (!got_swipe) mask |= 1u << 4;

    driver_input_touchpad_gesture_reset();
    return (int)mask;
}
