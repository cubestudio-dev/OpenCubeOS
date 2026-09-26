/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: keyboard.c
 * Purpose: PS/2 keyboard driver.
 *
 * IRQ1 (vector 33) handler reads port 0x60, decodes scancode set 1,
 * updates modifier state, produces ASCII / special keycodes, pushes
 * them into a ring buffer. L1 handlers run first.
 */
#include "keyboard.h"
#include "idt.h"
#include "pic.h"
#include "irq.h"
#include "string.h"

static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* Modifier state. */
static u8 g_mods = 0;
static int g_pause_bytes = 0;  /* P1-23: Pause/Break multi-byte sequence */

/* Ring buffer for produced keycodes. */
#define OC_KBD_BUF_LEN 128
static u16 g_buf[OC_KBD_BUF_LEN];
static u32 g_buf_head = 0;  /* consumer reads here */
static u32 g_buf_tail = 0;  /* producer writes here */

/* L1 handler chain. */
#define OC_KBD_CHAIN_LEN 4
static oc_kbd_handler_fn g_handlers[OC_KBD_CHAIN_LEN];

/* Scancode set 1 ? ASCII (unshifted). 0 = no mapping / special. */
static const u8 scancode_map[128] = {
    /* 0x00 */ 0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',
    /* 0x08 */ '7',  '8',  '9',  '0',  '-',  '=',  0x08, '\t',
    /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    /* 0x18 */ 'o',  'p',  '[',  ']',  '\r', 0,    'a',  's',
    /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    /* 0x28 */ '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    /* 0x38 */ 0,    ' ',  0,    OC_KEY_F1, OC_KEY_F2, OC_KEY_F3, OC_KEY_F4, OC_KEY_F5,
    /* 0x40 */ OC_KEY_F6, OC_KEY_F7, OC_KEY_F8, OC_KEY_F9, OC_KEY_F10, 0, 0, OC_KEY_HOME,
    /* 0x48 */ OC_KEY_UP, OC_KEY_PGUP, 0, OC_KEY_LEFT, 0, OC_KEY_RIGHT, 0, OC_KEY_END,
    /* 0x50 */ OC_KEY_DOWN, OC_KEY_PGDN, OC_KEY_INS, OC_KEY_DEL, 0, 0, 0, OC_KEY_F11,
    /* 0x58 */ OC_KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

/* Shifted variants. */
static const u8 scancode_map_shift[128] = {
    /* 0x00 */ 0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    /* 0x08 */ '&',  '*',  '(',  ')',  '_',  '+',  0x08, '\t',
    /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    /* 0x18 */ 'O',  'P',  '{',  '}',  '\r', 0,    'A',  'S',
    /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    /* 0x28 */ '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    /* 0x30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* rest 0 */
};

void oc_keyboard_init(void) {
    g_buf_head = g_buf_tail = 0;
    g_mods = 0;
    oc_memset(g_handlers, 0, sizeof(g_handlers));
    /* Register the IRQ1 handler. */
    oc_irq_register_handler(1, oc_keyboard_irq_handler, NULL);
}

u8 oc_keyboard_get_mods(void) {
    return g_mods;
}

int oc_keyboard_has_key(void) {
    return g_buf_head != g_buf_tail;
}

int oc_keyboard_getch(void) {
    if (g_buf_head == g_buf_tail) return -1;
    u16 k = g_buf[g_buf_head];
    g_buf_head = (g_buf_head + 1) % OC_KBD_BUF_LEN;
    return (int)k;
}

static void push_key(u16 key) {
    u32 next = (g_buf_tail + 1) % OC_KBD_BUF_LEN;
    if (next == g_buf_head) return;  /* buffer full, drop */
    g_buf[g_buf_tail] = key;
    g_buf_tail = next;
}

void oc_keyboard_inject(u16 keycode) {
    push_key(keycode);
}

int oc_keyboard_register_handler(oc_kbd_handler_fn handler) {
    if (!handler) return -1;
    for (int i = 0; i < OC_KBD_CHAIN_LEN; i++) {
        if (g_handlers[i] == NULL) {
            g_handlers[i] = handler;
            return 0;
        }
    }
    return -2;
}

int oc_keyboard_unregister_handler(oc_kbd_handler_fn handler) {
    if (!handler) return -1;
    for (int i = 0; i < OC_KBD_CHAIN_LEN; i++) {
        if (g_handlers[i] == handler) {
            g_handlers[i] = NULL;
            for (int j = i; j + 1 < OC_KBD_CHAIN_LEN; j++) {
                g_handlers[j] = g_handlers[j+1];
                g_handlers[j+1] = NULL;
            }
            return 0;
        }
    }
    return -2;
}

/* Process one scancode byte. Returns the produced keycode (or 0 = none). */
static u16 process_scancode(u8 sc, int *extended) {
    /* Extended prefix. */
    if (sc == 0xE0) { *extended = 1; return 0; }
    /* P1-23 FIX: Pause/Break key sends 0xE1 prefix followed by 7 bytes.
     * Consume the entire sequence by tracking a counter. */
    if (sc == 0xE1) { g_pause_bytes = 7; return 0; }
    if (g_pause_bytes > 0) { g_pause_bytes--; return 0; }

    int release = (sc & 0x80) != 0;
    u8  code    = sc & 0x7F;

    /* Extended-key handling: arrows + nav keys + numpad enter + etc. */
    if (*extended) {
        *extended = 0;
        u16 ext = 0;
        switch (code) {
            case 0x48: ext = OC_KEY_UP;        break;
            case 0x50: ext = OC_KEY_DOWN;      break;
            case 0x4B: ext = OC_KEY_LEFT;      break;
            case 0x4D: ext = OC_KEY_RIGHT;     break;
            case 0x47: ext = OC_KEY_HOME;      break;
            case 0x4F: ext = OC_KEY_END;       break;
            case 0x49: ext = OC_KEY_PGUP;      break;
            case 0x51: ext = OC_KEY_PGDN;      break;
            case 0x52: ext = OC_KEY_INS;       break;
            case 0x53: ext = OC_KEY_DEL;       break;
            case 0x1C: ext = OC_KEY_ENTER;     break;  /* numpad enter */
            default:   return 0;
        }
        if (!release) return ext;
        return 0;
    }

    /* Modifier keys. */
    switch (code) {
        case 0x2A: /* LShift */
        case 0x36: /* RShift */
            if (release) g_mods &= ~OC_MOD_SHIFT;
            else         g_mods |= OC_MOD_SHIFT;
            return 0;
        case 0x1D: /* Ctrl */
            if (release) g_mods &= ~OC_MOD_CTRL;
            else         g_mods |= OC_MOD_CTRL;
            return 0;
        case 0x38: /* Alt */
            if (release) g_mods &= ~OC_MOD_ALT;
            else         g_mods |= OC_MOD_ALT;
            return 0;
        case 0x3A: /* CapsLock */
            if (!release) g_mods ^= OC_MOD_CAPS;
            return 0;
        case 0x45: /* NumLock */
            if (!release) g_mods ^= OC_MOD_NUM;
            return 0;
        case 0x46: /* ScrollLock */
            if (!release) g_mods ^= OC_MOD_SCROLL;
            return 0;
    }

    if (release) return 0;  /* ignore break codes for regular keys */

    u16 key = scancode_map[code];
    if (key == 0) return 0;

    /* Ctrl + letter ? control code. */
    if ((g_mods & OC_MOD_CTRL) && key >= 'a' && key <= 'z') {
        return (u16)(key - 'a' + 1);
    }
    if ((g_mods & OC_MOD_CTRL) && key >= 'A' && key <= 'Z') {
        return (u16)(key - 'A' + 1);
    }

    /* Shift selects shifted map (for letters, CapsLock also flips case). */
    if (g_mods & OC_MOD_SHIFT) {
        u8 shifted = scancode_map_shift[code];
        if (shifted) key = shifted;
        /* BUG-043 FIX: CapsLock+Shift should invert case for letters.
         * When both CapsLock and Shift are active, letters should be
         * lowercase (CapsLock inverts the Shift effect for letters).
         * Old code only checked Shift OR CapsLock (if-else), so
         * CapsLock+Shift produced uppercase instead of lowercase. */
        if ((g_mods & OC_MOD_CAPS) && key >= 'A' && key <= 'Z') {
            key += 32;  /* invert to lowercase */
        }
    } else if (g_mods & OC_MOD_CAPS) {
        /* CapsLock flips case of letters only. */
        if (key >= 'a' && key <= 'z') key -= 32;
    }
    return key;
}

void oc_keyboard_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    static int extended = 0;
    u8 sc = inb(0x60);
    u16 key = process_scancode(sc, &extended);
    if (key == 0) return;

    /* Run L1 handlers first. */
    for (int i = 0; i < OC_KBD_CHAIN_LEN; i++) {
        if (g_handlers[i]) {
            if (g_handlers[i](key, g_mods)) {
                return;  /* L1 consumed it */
            }
        }
    }
    /* Default: enqueue. */
    push_key(key);
}
