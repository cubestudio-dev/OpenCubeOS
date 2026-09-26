# Open Cube OS — WP-02 Extension API

WP-02 adds **four** new L0→L1 extension points (on top of WP-01's four). All WP-01 interfaces (`kernel/ext.h`) remain unchanged. WP-02 interfaces are additive.

All function signatures are stable for the WP-02 release.

---

## 5. IRQ handler registration

```c
#include "irq.h"

typedef void (*oc_irq_handler_fn)(void *ctx, oc_irq_frame_t *f);

int oc_irq_register_handler(int irq, oc_irq_handler_fn handler, void *ctx);
int oc_irq_unregister_handler(int irq, oc_irq_handler_fn handler, void *ctx);
```

`irq` is 0..15 (the PIC IRQ number, NOT the IDT vector). The handler is called in IRQ context (interrupts may be off). Handlers must be fast and non-blocking.

Multiple handlers can share the same IRQ (up to 4). All registered handlers run on each IRQ fire. When the first handler is registered for an IRQ, L0 automatically unmasks that IRQ at the PIC. When the last handler is unregistered, L0 masks the IRQ again.

### Usage

```c
static void my_irq7_handler(void *ctx, oc_irq_frame_t *f) {
    /* handle IRQ7 (parallel port) */
}

void init_my_driver(void) {
    oc_irq_register_handler(7, my_irq7_handler, NULL);
}
```

### Signature

| Function | Returns |
|---|---|
| `oc_irq_register_handler(irq, handler, ctx)` | 0 on success, -1 on bad arg, -2 if chain full |
| `oc_irq_unregister_handler(irq, handler, ctx)` | 0 on success, -1 on bad arg, -2 if not found |

---

## 6. Timer callback registration

```c
#include "timer.h"

typedef void (*oc_timer_cb_fn)(void *ctx);

int oc_timer_register_periodic(oc_timer_cb_fn fn, void *ctx, u64 interval_ms);
int oc_timer_register_oneshot(oc_timer_cb_fn fn, void *ctx, u64 delay_ms);
int oc_timer_cancel(int id);
```

Soft timers run off the PIT (IRQ0, 100 Hz). The callback fires in IRQ context. Minimum resolution is 10 ms (one tick).

`oc_timer_register_periodic` returns a timer id ≥ 0. The callback fires every `interval_ms` milliseconds until `oc_timer_cancel(id)` is called.

`oc_timer_register_oneshot` fires once after `delay_ms` milliseconds, then auto-cancels.

### Usage

```c
static void heartbeat(void *ctx) {
    /* blink an LED every second */
}

void start_heartbeat(void) {
    int id = oc_timer_register_periodic(heartbeat, NULL, 1000);
    /* save id somewhere if you want to cancel later */
}
```

### Signature

| Function | Returns |
|---|---|
| `oc_timer_register_periodic(fn, ctx, interval_ms)` | timer id ≥ 0 on success, -1 on bad arg, -2 if table full |
| `oc_timer_register_oneshot(fn, ctx, delay_ms)` | timer id ≥ 0 on success, -1 on bad arg, -2 if table full |
| `oc_timer_cancel(id)` | 0 on success, -1 on bad id, -2 if not in use |

### Time queries

```c
u64 oc_timer_now_ms(void);       /* ms since timer init */
u64 oc_timer_ticks(void);        /* raw tick count (100 Hz) */
void oc_timer_format_hms(u64 ms, char out[16]);  /* "HH:MM:SS.mmm" */
```

---

## 7. Keyboard input handler

```c
#include "keyboard.h"

typedef int (*oc_kbd_handler_fn)(u16 keycode, u8 mods);

int oc_keyboard_register_handler(oc_kbd_handler_fn handler);
int oc_keyboard_unregister_handler(oc_kbd_handler_fn handler);
```

L1 can register a handler that receives every key BEFORE it goes into the input buffer. Return 0 = let L0 enqueue normally; non-zero = L1 consumed it (L0 skips enqueue).

This is the input-method / hotkey / macro hook.

`keycode` is either a printable ASCII byte (0x20..0x7E) or one of the `OC_KEY_*` constants from `keyboard.h` (e.g. `OC_KEY_ENTER`, `OC_KEY_UP`, `OC_KEY_F1`).

`mods` is a bitmask of `OC_MOD_SHIFT|CTRL|ALT|CAPS|NUM|SCROLL`.

### Usage

```c
static int my_hotkeys(u16 key, u8 mods) {
    if (key == 'p' && (mods & OC_MOD_CTRL)) {
        /* Ctrl+P = print something */
        return 1;  /* consume */
    }
    return 0;  /* let L0 handle */
}

void install_hotkeys(void) {
    oc_keyboard_register_handler(my_hotkeys);
}
```

### Reading keys directly

```c
int oc_keyboard_getch(void);       /* -1 if empty */
int oc_keyboard_has_key(void);
u8   oc_keyboard_get_mods(void);
```

---

## 8. Exception handler registration

```c
#include "exceptions.h"

typedef int (*oc_exc_handler_fn)(oc_irq_frame_t *f);

int oc_exc_register_handler(int vector, oc_exc_handler_fn handler);
int oc_exc_unregister_handler(int vector, oc_exc_handler_fn handler);
```

L1 can register a handler for any exception vector (0..31). The handler receives the full interrupt frame (including error code, faulting RIP, register dump).

Return 0 = let L0 print diagnostic and halt. Return non-zero = L1 handled it (L0 resumes execution at the RIP in the frame, which L1 may have modified).

This is the JIT / debugger / fault-recovery hook.

### Usage

```c
static int my_pf_handler(oc_irq_frame_t *f) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    /* check if cr2 is in our demand-paged region; if so, map a page
     * and return 1 to resume. */
    if (in_demand_page_region(cr2)) {
        map_page_for(cr2);
        return 1;  /* handled, resume */
    }
    return 0;  /* let L0 halt */
}

void init_pager(void) {
    oc_exc_register_handler(OC_EXC_PF, my_pf_handler);
}
```

### Signature

| Function | Returns |
|---|---|
| `oc_exc_register_handler(vector, handler)` | 0 on success, -1 on bad arg, -2 if chain full |
| `oc_exc_unregister_handler(vector, handler)` | 0 on success, -1 on bad arg, -2 if not found |

---

## 9. Console input injection

```c
#include "console_in.h"

void oc_console_in_inject(const char *text);
```

L1 can inject a string as if it were typed. Useful for scripts, test harnesses, macros.

---

## 10. Console input hook

```c
#include "console_in.h"

typedef int (*oc_console_in_hook_fn)(const char *line, int len);

int oc_console_in_register_hook(oc_console_in_hook_fn fn);
int oc_console_in_unregister_hook(oc_console_in_hook_fn fn);
```

L1 can register a hook that receives every complete line (after Enter) BEFORE it's returned to `oc_console_in_readline()`. Return 0 = let L0 process normally; non-zero = L1 consumed it (L0 drops it).

This is the command-interpreter / shell-extension hook.

---

## ABI stability

All eight WP-02 functions and their typedefs are frozen. Future WPs may add new functions but will not change existing ones incompatibly.

## Loading model

WP-02 does not yet have a dynamic module loader — L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged.
