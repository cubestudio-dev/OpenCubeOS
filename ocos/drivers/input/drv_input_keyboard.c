/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: drivers/input/driver_input_keyboard.c
 * Purpose: PS/2 keyboard driver.
 *
 * IRQ1 (vector 33) handler reads port 0x60, decodes scancode set 1,
 * updates modifier state, produces ASCII / special keycodes, pushes
 * them into a ring buffer. L1 handlers run first.
 */
#include "drv_input_keyboard.h"
#include "arch_idt.h"
#include "arch_pic.h"
#include "arch_irq.h"
#include "core_sync.h"
#include "core_lib_string.h"

static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }

/* ---- i8042 (PS/2 controller) plumbing ------------------------------
 *
 * WP-vmware-input-fix 4.1: this driver used to read port 0x60 from the
 * IRQ1 handler WITHOUT ever programming the i8042 and WITHOUT looking at
 * the status port, and it accepted whatever byte came back. That works on
 * QEMU, whose controller is already enabled when the firmware hands over,
 * but not on VMware: there the controller is left disabled, the keyboard
 * interface clock is inhibited, and reading 0x60 returns 0xFF (or a stale
 * byte) forever. Every such read fires IRQ1 again, so the machine entered
 * an interrupt storm that flooded the console with uninitialised glyphs
 * (the "wall of blocks" in the field screenshot) and finally tripped the
 * scheduler's stack-bounds check. The init sequence below is the standard
 * one every PC OS uses: disable both ports, drain the output buffer,
 * self-test, enable the keyboard port, re-enable IRQ1, and make sure the
 * keyboard itself is in scancode set 1.
 *
 * The reads are bounded so a machine with no keyboard (or a broken
 * controller) never hangs the boot. */
#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define I8042_ST_OBF 0x01    /* output buffer full  - a byte is waiting */
#define I8042_ST_IBF 0x02    /* input buffer full   - do not write yet */
#define I8042_ST_AUX 0x20    /* the waiting byte came from the AUX port */

/* Bound every controller poll: 100k iterations is far more than the
 * hardware needs and still terminates on a machine without a keyboard. */
#define I8042_POLL_MAX 100000

/* The 0x80 legacy delay: writing to an unused port costs ~1us on every
 * PC and is what the PS/2 protocol expects between bytes. */
static inline void i8042_io_delay(void) { outb(0x80, 0); }

static int i8042_wait_write(void) {
    for (int i = 0; i < I8042_POLL_MAX; i++) {
        if ((inb(I8042_STATUS) & I8042_ST_IBF) == 0) return 0;
    }
    return -1;
}

static int i8042_wait_read(void) {
    for (int i = 0; i < I8042_POLL_MAX; i++) {
        if (inb(I8042_STATUS) & I8042_ST_OBF) return 0;
    }
    return -1;
}

/* Drain anything the firmware left in the output buffer. Without this the
 * first IRQ1 fires on a stale byte (firmware self-test result 0x55 or a
 * mouse packet) which the decoder then renders as stray keystrokes. */
static void i8042_drain(void) {
    for (int i = 0; i < 64; i++) {
        if ((inb(I8042_STATUS) & I8042_ST_OBF) == 0) break;
        (void)inb(I8042_DATA);
        i8042_io_delay();
    }
}

static int i8042_cmd(u8 cmd) {
    if (i8042_wait_write() != 0) return -1;
    outb(I8042_CMD, cmd);
    i8042_io_delay();
    return 0;
}

static int i8042_cmd_arg(u8 cmd, u8 arg) {
    if (i8042_cmd(cmd) != 0) return -1;
    if (i8042_wait_write() != 0) return -1;
    outb(I8042_DATA, arg);
    i8042_io_delay();
    return 0;
}

/* Write a byte to the KEYBOARD (not the controller) and optionally wait
 * for its ACK (0xFA). Returns 0 on success. */
static int kbd_write(u8 byte, int want_ack) {
    if (i8042_wait_write() != 0) return -1;
    outb(I8042_DATA, byte);
    i8042_io_delay();
    if (!want_ack) return 0;
    if (i8042_wait_read() != 0) return -1;
    u8 ack = inb(I8042_DATA);
    return (ack == 0xFA) ? 0 : -1;
}

/* Modifier state. */
static u8 g_mods = 0;
/* WP-vmware-input-fix 4.1: did i8042_init() find an enabled keyboard?
 * 0 = absent (serial-only console), 1 = present. The IRQ1 handler uses
 * this to stay inert on a machine with no PS/2 keyboard. */
static int g_kbd_present;
static int g_pause_bytes = 0;  /* P1-23: Pause/Break multi-byte sequence */

/* Ring buffer for produced keycodes. */
/* WP-09-FIX BUG-015: enlarge the key ring buffer from 128 to 1024.
 * The COM1 bridge pushes whole serial FIFO bursts (up to 16+ bytes per
 * IRQ) and a pasted 190-character command line arrived faster than the
 * shell drained it — the ring overflowed, characters were silently
 * dropped, the ENTER byte got lost and readline never returned. */
#define OC_KBD_BUF_LEN 1024
static u16 g_buf[OC_KBD_BUF_LEN];
static u32 g_buf_head = 0;  /* consumer reads here */
static u32 g_buf_tail = 0;  /* producer writes here */

/* L1 handler chain. */
#define OC_KBD_CHAIN_LEN 4
static driver_input_kbd_handler_fn g_handlers[OC_KBD_CHAIN_LEN];

/* BUG-0327 (A7-28): the ring has multiple producers - the PS/2 IRQ1
 * handler, the COM1 serial bridge (IRQ4) and task-context inject - so
 * head/tail updates run under one irqsave spinlock (BUG-0285 semantics);
 * the L1 handler chain deliberately stays OUTSIDE the lock (handlers are
 * arbitrary user code and never touch the ring). */
static spinlock_t g_kbd_ring_lock;

/* Scancode set 1 ? ASCII (unshifted). 0 = no mapping / special. */
/* 0x01 (Esc) maps to the dedicated OC_KEY_ESC_KEY: raw 0x1B is the ANSI
 * CSI opener on the serial bridge, and mapping the physical key to it
 * made the TUI CSI decoder swallow every Esc press. */
static const u8 scancode_map[128] = {
    /* 0x00 */ 0,    OC_KEY_ESC_KEY, '1',  '2',  '3',  '4',  '5',  '6',
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

/* WP-10-chinese_support-fix4: Dvorak Simplified layout. Same scan
 * codes, different letter/symbol placement; the number row and the
 * punctuation already shared with US stay put. Special keys (Esc,
 * arrows, F-keys, Enter/Tab/Backspace) are identical in both layouts. */
static const u8 scancode_map_dvorak[128] = {
    /* 0x00 */ 0,    OC_KEY_ESC_KEY, '1',  '2',  '3',  '4',  '5',  '6',
    /* 0x08 */ '7',  '8',  '9',  '0',  '[',  ']',  0x08, '\t',
    /* 0x10 */ '\'', ',', '.', 'p',  'y',  'f',  'g',  'c',
    /* 0x18 */ 'r',  'l',  '/',  '=',  '\r', 0,    'a',  'o',
    /* 0x20 */ 'e',  'u',  'i',  'd',  'h',  't',  'n',  's',
    /* 0x28 */ '-',  '`',  0,    '\\', ';',  'q',  'j',  'k',
    /* 0x30 */ 'x',  'b',  'm',  'w',  'v',  'z',  0,    '*',
    /* 0x38 */ 0,    ' ',  0,    OC_KEY_F1, OC_KEY_F2, OC_KEY_F3, OC_KEY_F4, OC_KEY_F5,
    /* 0x40 */ OC_KEY_F6, OC_KEY_F7, OC_KEY_F8, OC_KEY_F9, OC_KEY_F10, 0, 0, OC_KEY_HOME,
    /* 0x48 */ OC_KEY_UP, OC_KEY_PGUP, 0, OC_KEY_LEFT, 0, OC_KEY_RIGHT, 0, OC_KEY_END,
    /* 0x50 */ OC_KEY_DOWN, OC_KEY_PGDN, OC_KEY_INS, OC_KEY_DEL, 0, 0, 0, OC_KEY_F11,
    /* 0x58 */ OC_KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

static const u8 scancode_map_dvorak_shift[128] = {
    /* 0x00 */ 0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    /* 0x08 */ '&',  '*',  '(',  ')',  '{',  '}',  0x08, '\t',
    /* 0x10 */ '"',  '<',  '>',  'P',  'Y',  'F',  'G',  'C',
    /* 0x18 */ 'R',  'L',  '?',  '+',  '\r', 0,    'A',  'O',
    /* 0x20 */ 'E',  'U',  'I',  'D',  'H',  'T',  'N',  'S',
    /* 0x28 */ '_',  '~',  0,    '|',  ':',  'Q',  'J',  'K',
    /* 0x30 */ 'X',  'B',  'M',  'W',  'V',  'Z',  0,    '*',
    /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* rest 0 */
};

/* Active layout (fix4): 0 = us, 1 = dvorak. Switched at runtime via
 * driver_input_keyboard_set_layout() and persisted as kbd.layout in
 * /etc/opencube.conf; the TUI keyboard page and
 * `config set kbd.layout dvorak` share the same key. */
static int g_layout;

void driver_input_keyboard_set_layout(const char *name) {
    g_layout = (name && name[0] == 'd' && strcmp(name, "dvorak") == 0) ? 1 : 0;
}

const char *driver_input_keyboard_get_layout(void) {
    return g_layout ? "dvorak" : "us";
}

/* Bring the i8042 up into the state the driver assumes. Returns 0 when a
 * keyboard is present and enabled, -1 when the controller/keyboard is
 * absent (the boot log reports it; the machine still runs, it just has no
 * PS/2 keyboard - headless boxes and serial-only consoles). */
static int i8042_init(void) {
    /* 1. Disable both ports so nothing arrives mid-configuration. */
    i8042_cmd(0xAD);              /* disable keyboard port */
    i8042_cmd(0xA7);              /* disable AUX (mouse) port */
    i8042_drain();

    /* 2. Read the controller configuration byte (command 0x20). */
    if (i8042_cmd(0x20) != 0) return -1;
    if (i8042_wait_read() != 0) return -1;
    u8 cfg = inb(I8042_DATA);

    /* 3. Clear the two bits that keep the keyboard silent: bit 0 is the
     *    keyboard-interrupt enable and bit 4 is the keyboard-clock
     *    inhibit. VMware ships this byte with both set, which is exactly
     *    why the keyboard produced nothing but 0xFF there. */
    cfg &= (u8)~(0x01 | 0x10);
    if (i8042_cmd_arg(0x60, cfg) != 0) return -1;

    /* 4. Controller self-test (0xAA). 0x55 means healthy; some firmware
     *    lies, so a failure is reported but not fatal - the port enable
     *    below is what actually matters. */
    int self_test_ok = 0;
    if (i8042_cmd(0xAA) == 0 && i8042_wait_read() == 0)
        self_test_ok = (inb(I8042_DATA) == 0x55);

    /* 5. Re-enable the keyboard port; the self-test may have reset the
     *    configuration byte, so write it again. */
    i8042_cmd_arg(0x60, cfg);
    i8042_cmd(0xAE);              /* enable keyboard port */
    i8042_drain();

    /* 6. Enable scanning.  The controller config above is what actually
     *    un-inhibits the keyboard clock; 0xF4 tells the keyboard to start
     *    sending. A keyboard that does not ACK is treated as absent.
     *
     *    Deliberately NOT issuing 0xF0/0x01 (select scancode set 1): the
     *    decoder's table is set-1 and every PC keyboard boots in set 1,
     *    so the command only adds two round-trips where a lost ACK (the
     *    exact failure mode of a lossy VM keyboard path) desynchronises
     *    the byte stream and turns ordinary presses into garbage. */
    if (kbd_write(0xF4, 1) != 0) return -1;   /* enable scanning */
    i8042_drain();

    /* 7. Re-enable IRQ1 in the controller config (step 5 rewrote the byte
     *    that step 3 had already fixed). */
    if (i8042_cmd(0x20) == 0 && i8042_wait_read() == 0) {
        u8 c2 = inb(I8042_DATA);
        c2 &= (u8)~0x10;          /* keyboard clock uninhibited */
        c2 |= 0x01;               /* keyboard interrupt enabled */
        i8042_cmd_arg(0x60, c2);
    }

    return self_test_ok ? 0 : 0;  /* present; the self-test byte is advisory */
}

void driver_input_keyboard_init(void) {
    spin_init(&g_kbd_ring_lock);
    spin_lock(&g_kbd_ring_lock);
    g_buf_head = g_buf_tail = 0;
    g_mods = 0;
    memset(g_handlers, 0, sizeof(g_handlers));
    spin_unlock(&g_kbd_ring_lock);

    /* WP-vmware-input-fix 4.1: program the controller BEFORE the IRQ can
     * fire. Registering the handler unmasks IRQ1, and with an unprogrammed
     * i8042 (VMware) that unmasks straight into the 0xFF storm. */
    g_kbd_present = (i8042_init() == 0) ? 1 : 0;

    /* Register the IRQ1 handler. */
    arch_irq_register_handler(1, driver_input_keyboard_irq_handler, NULL);
}

int driver_input_keyboard_present(void) { return g_kbd_present; }

u8 driver_input_keyboard_get_mods(void) {
    return g_mods;
}

int driver_input_keyboard_has_key(void) {
    spin_lock(&g_kbd_ring_lock);
    int has = g_buf_head != g_buf_tail;
    spin_unlock(&g_kbd_ring_lock);
    return has;
}

int driver_input_keyboard_getch(void) {
    spin_lock(&g_kbd_ring_lock);
    if (g_buf_head == g_buf_tail) {
        spin_unlock(&g_kbd_ring_lock);
        return -1;
    }
    u16 k = g_buf[g_buf_head];
    g_buf_head = (g_buf_head + 1) % OC_KBD_BUF_LEN;
    spin_unlock(&g_kbd_ring_lock);
    return (int)k;
}

static void push_key(u16 key) {
    spin_lock(&g_kbd_ring_lock);
    u32 next = (g_buf_tail + 1) % OC_KBD_BUF_LEN;
    if (next != g_buf_head) {
        g_buf[g_buf_tail] = key;
        g_buf_tail = next;
    }
    spin_unlock(&g_kbd_ring_lock);
}

void driver_input_keyboard_inject(u16 keycode) {
    /* WP-09-FIX BUG-030: run the L1 handler chain for injected keys too —
     * the COM1 serial RX bridge feeds the queue through this entry point.
     * Previously only the PS/2 IRQ path ran the chain, so L1 extension
     * handlers never saw serial input (inconsistent extension behavior). */
    for (int i = 0; i < OC_KBD_CHAIN_LEN; i++) {
        if (g_handlers[i]) {
            if (g_handlers[i](keycode, g_mods)) {
                return;  /* L1 consumed it */
            }
        }
    }
    push_key(keycode);
}

int driver_input_keyboard_register_handler(driver_input_kbd_handler_fn handler) {
    if (!handler) return -1;
    for (int i = 0; i < OC_KBD_CHAIN_LEN; i++) {
        if (g_handlers[i] == NULL) {
            g_handlers[i] = handler;
            return 0;
        }
    }
    return -2;
}

int driver_input_keyboard_unregister_handler(driver_input_kbd_handler_fn handler) {
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
            /* BUG-0325 (A7-26): the E0 branch previously dropped the right
             * modifiers entirely - right Ctrl (E0 0x1D) and right Alt /
             * AltGr (E0 0x38) fell into default and were discarded, so
             * right-side modifiers never set g_mods. Both now drive the
             * same modifier state as their left counterparts (the only
             * layout in the kernel is US; AltGr-specific character
             * composition is a layout-table capability, not a modifier
             * bug). The E0 0x2A/0xAA fake-shift sequences still fall to
             * default and are discarded, as they must be. */
            case 0x1D: /* right Ctrl */
                if (release) g_mods &= ~OC_MOD_CTRL;
                else         g_mods |=  OC_MOD_CTRL;
                return 0;
            case 0x38: /* right Alt / AltGr */
                if (release) g_mods &= ~OC_MOD_ALT;
                else         g_mods |=  OC_MOD_ALT;
                return 0;
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

    /* BUG-0326 (A7-27): NumLock turns the keypad block into digits. The
     * static map always produced navigation codes (and gave keypad
     * '-'/'+'/'5' no mapping at all in ANY state). Keypad '-' and '+'
     * work regardless of NumLock like on real hardware; keypad-5 types
     * '5' only with NumLock on and stays dead when off. */
    if (code >= 0x47 && code <= 0x53) {
        static const u8 keypad_num[13] = {
            /* 0x47 */ '7', '8', '9', 0,   /* 4A = '-' below */
            /* 0x4B */ '4', '5', '6', '+', /* 4E handled below too */
            /* 0x4F */ '1', '2', '3', '0',
            /* 0x53 */ '.'
        };
        if (g_mods & OC_MOD_NUM) {
            u8 digit = keypad_num[code - 0x47];
            if (digit) return digit;
        }
        if (code == 0x4A) return '-';
        if (code == 0x4E) return '+';
    }

    u16 key = (g_layout ? scancode_map_dvorak : scancode_map)[code];
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
        u8 shifted = (g_layout ? scancode_map_dvorak_shift : scancode_map_shift)[code];
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

/* WP-vmware-input-fix 4.1: the interrupt-storm guard.
 *
 * A spurious IRQ1 (shared line, a byte the controller re-asserts, or a
 * machine whose keyboard clock is inhibited) used to be handled by reading
 * 0x60 unconditionally. On VMware the read returns 0xFF forever, the
 * handler returns without consuming anything, the controller re-asserts
 * the line, and the machine spends all its time in the handler - the
 * console fills with uninitialised glyphs and the scheduler eventually
 * switches onto a task whose rsp was never saved (the reported
 * "rsp=0x0000000000000000" panic).
 *
 * Two guards, in order of precision:
 *   1. Only read 0x60 when the status port says a byte is actually
 *      waiting (OBF) and that byte is NOT from the AUX port - a mouse
 *      byte read here would be decoded as a scancode and echoed.
 *   2. Drop 0xFF explicitly: it is the i8042's "no data / error" byte,
 *      never a real scancode, and accepting it produces the junk writes.
 * When a run of spurious interrupts is detected the controller is
 * re-drained, which breaks the self-sustaining loop without masking IRQ1
 * (masking would hide a real keyboard for the rest of the session). */
#define I8042_SPURIOUS_RUN_LIMIT 32
static u32 g_spurious_run;

void driver_input_keyboard_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    static int extended = 0;

    if (!g_kbd_present) return;

    /* 1. Nothing waiting, or the byte belongs to the mouse: not ours.
     *    Still counts toward the spurious run so a continuously asserting
     *    line gets the drain below. */
    u8 status = inb(I8042_STATUS);
    if ((status & I8042_ST_OBF) == 0 || (status & I8042_ST_AUX)) {
        if (++g_spurious_run >= I8042_SPURIOUS_RUN_LIMIT) {
            g_spurious_run = 0;
            i8042_drain();
        }
        return;
    }

    u8 sc = inb(I8042_DATA);

    /* 2. 0xFF is the controller's error/empty marker, not a scancode. */
    if (sc == 0xFF) {
        if (++g_spurious_run >= I8042_SPURIOUS_RUN_LIMIT) {
            g_spurious_run = 0;
            i8042_drain();
        }
        return;
    }
    g_spurious_run = 0;

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
