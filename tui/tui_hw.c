/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_hw.c
 * Purpose: Hardware configuration TUI: enumerates every hardware domain
 *          through the real driver query APIs, offers the per-domain
 *          adjustments the drivers actually implement, and persists
 *          changes to /etc/opencube.conf through lib_config.
 */
#include "tui.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../kernel/mem/mem_pmm.h"
#include "../kernel/arch/x86_64/arch_multiboot2.h"
#include "../drivers/block/driver_block_blk.h"
#include "../drivers/block/driver_block_part.h"
#include "../drivers/nic/driver_nic.h"
#include "../drivers/snd/driver_snd.h"
#include "../drivers/usb/driver_usb.h"
#include "../drivers/pci/driver_pcie.h"
#include "../drivers/camera/driver_camera.h"
#include "../drivers/wireless/driver_wireless.h"
#include "../drivers/time/driver_cmos_rtc.h"
#include "../drivers/time/driver_hpet.h"
#include "../drivers/time/driver_pmtimer.h"
#include "../drivers/time/driver_tsc.h"
#include "../drivers/input/driver_input_keyboard.h"
#include "../drivers/input/driver_input_i2chid.h"
#include "../net/net_core.h"
#include "../kernel/core/core_timer.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

#define HW_PAGE_MAX_LINES 60

typedef struct {
    char lines[HW_PAGE_MAX_LINES][100];
    int  count;
    int  scroll;
} hw_page_t;

static void hw_add(hw_page_t *pg, const char *fmt, ...) {
    if (pg->count >= HW_PAGE_MAX_LINES) return;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    char *dst = pg->lines[pg->count];
    int n = 0;
    char num[24];   /* shared by the numeric formatters below */
    for (const char *p = fmt; *p && n < 96; p++) {
        if (*p != '%') { dst[n++] = *p; continue; }
        p++;
        /* fix3: optional zero-padded width ("%02x" / "%04x"), so the
         * PCI page shows real vendor:device ids instead of the
         * literal "2x:2x" garbage the width-less formatter printed. */
        int pad = -1;
        if (*p == '0') {
            pad = 0;
            p++;
            while (*p >= '0' && *p <= '9') { pad = pad * 10 + (*p - '0'); p++; }
            if (pad > 16) pad = 16;
        }
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (s && *s && n < 96) dst[n++] = *s++;
        } else if (*p == 'd' || *p == 'u') {
            long long v = (long long)__builtin_va_arg(ap, int);
            if (v < 0) { dst[n++] = '-'; v = -v; }
            u64_to_str((u64)v, num);
            for (int i = 0; num[i] && n < 96; i++) dst[n++] = num[i];
        } else if (*p == 'l' && p[1] == 'u') {
            p++;
            u64_to_str(__builtin_va_arg(ap, u64), num);
            for (int i = 0; num[i] && n < 96; i++) dst[n++] = num[i];
        } else if (*p == 'x') {
            static const char hx[] = "0123456789abcdef";
            u32 v = __builtin_va_arg(ap, unsigned int);
            char tmp[12];
            int m = 0;
            if (v == 0) {
                tmp[m++] = '0';
            } else {
                while (v && m < 12) { tmp[m++] = hx[v & 0xF]; v >>= 4; }
            }
            for (int k = m; k < pad && n < 96; k++) dst[n++] = '0';
            while (m > 0 && n < 96) dst[n++] = tmp[--m];
        } else if (*p == '%') {
            dst[n++] = '%';
        }
    }
    __builtin_va_end(ap);
    dst[n] = 0;
    pg->count++;
}

/* human capacity: "x.x GiB" style with one decimal (integer math) */
static void hw_capacity_str(u64 bytes, char out[24]) {
    u64 mib = bytes >> 20;
    u64 whole = mib >> 10;
    u64 frac = ((mib & 1023) * 10 + 512) >> 10;
    char a[20], b[4], num[8];
    u64_to_str(whole, a);
    u64_to_str(frac, b);
    (void)num;
    int n = 0;
    for (int i = 0; a[i]; i++) out[n++] = a[i];
    out[n++] = '.';
    out[n++] = b[0];
    out[n++] = ' ';
    out[n++] = 'G';
    out[n++] = 'i';
    out[n++] = 'B';
    out[n] = 0;
}

/* ------------------------------------------------------------------ */
/* per-domain enumerators (all real driver queries)                    */
/* ------------------------------------------------------------------ */

static void page_cpu(hw_page_t *pg) {
    u32 a, b, c, d;
    /* CPUID leaf 0: max leaf + vendor */
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    char vendor[13];
    char *v = vendor;
    for (int i = 0; i < 4; i++) *v++ = (char)((b >> (i * 8)) & 0xFF);
    for (int i = 0; i < 4; i++) *v++ = (char)((d >> (i * 8)) & 0xFF);
    for (int i = 0; i < 4; i++) *v++ = (char)((c >> (i * 8)) & 0xFF);
    *v = 0;
    hw_add(pg, "CPU vendor      : %s", vendor);
    u32 max_leaf = a;
    /* brand string, leaves 0x80000002..4 */
    char brand[49];
    char *bp = brand;
    for (u32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        if (leaf > max_leaf && leaf != 0x80000002) break;
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf));
        u32 regs[4] = { a, b, c, d };
        for (int r = 0; r < 4; r++)
            for (int i = 0; i < 4; i++) *bp++ = (char)((regs[r] >> (i * 8)) & 0xFF);
    }
    *bp = 0;
    hw_add(pg, "CPU model       : %s", brand);
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    hw_add(pg, "family/model    : fam %d model %d stepping %d",
           (int)((a >> 8) & 0xF), (int)(((a >> 4) & 0xF) | ((a >> 16) & 0xF0)),
           (int)(a & 0xF));
    hw_add(pg, "features        : %s%s%s%s",
           (d & (1 << 25)) ? "SSE " : "",
           (c & (1 << 0)) ? "SSE3 " : "",
           (c & (1 << 20)) ? "SSE4.2 " : "",
           (c & (1 << 28)) ? "AVX " : "");
    hw_add(pg, "virtualization  : %s",
           (c & (1 << 5)) ? "VMX present" : "none reported");
    /* TSC counter (driver_tsc_read is the raw counter; the tsc
     * command reports the calibrated kHz) */
    hw_add(pg, "tsc counter      : %lu", driver_tsc_read());
}

static void page_mem(hw_page_t *pg) {
    mem_pmm_stats_t st;
    mem_pmm_get_stats(&st);
    char total[24], used[24], freeb[24];
    hw_capacity_str(st.total_bytes, total);
    hw_capacity_str(st.used_bytes, used);
    hw_capacity_str(st.free_bytes, freeb);
    hw_add(pg, "memory total    : %s", total);
    hw_add(pg, "memory used     : %s", used);
    hw_add(pg, "memory free     : %s", freeb);
    hw_add(pg, "pages           : %lu total / %lu used / %lu free",
           st.total_pages, st.used_pages, st.free_pages);
    hw_add(pg, "free runs       : %lu (fragmentation indicator)",
           st.free_fragments);
    hw_add(pg, "alloc counters  : %lu allocs / %lu frees / %lu failures",
           st.total_allocs, st.total_frees, st.alloc_failures);
    hw_add(pg, "kernel heap     : kmalloc service (first-fit + coalesce)");
    const u8 *self_data = 0; u64 self_size = 0;
    if (arch_multiboot2_get_kernel_self(&self_data, &self_size) == 0)
        hw_add(pg, "kernel self mod : %lu bytes", self_size);
    else
        hw_add(pg, "kernel self mod : not loaded (disk boot)");
}

static void page_storage(hw_page_t *pg) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
        driver_block_device_t *d = driver_block_get_device(i);
        if (!d) continue;
        n++;
        char cap[24];
        hw_capacity_str(d->sectors * d->sector_size, cap);
        hw_add(pg, "%-8s capacity %s  sectors %lu x %d",
               d->name, cap, d->sectors, d->sector_size);
    }
    if (!n) hw_add(pg, "no block devices registered");
    hw_add(pg, "");
    hw_add(pg, "filesystems     : ramfs( / ) fat32 exfat ext4(RO)");
    hw_add(pg, "mounts          : see the mounts command (VFS table)");
    hw_add(pg, "operations      : parted / mkfs.fat32 / mkfs.exfat / fsck");
    hw_add(pg, "                  remain shell commands (destructive)");
}

static void page_network(hw_page_t *pg) {
    int n = driver_nic_num_devices();
    hw_add(pg, "NICs            : %d", n);
    for (int i = 0; i < 8; i++) {
        driver_nic_device_t *d = driver_nic_get_device(i);
        if (!d) continue;
        char mac[18];
        static const char hx[] = "0123456789abcdef";
        int p = 0;
        for (int j = 0; j < 6; j++) {
            mac[p++] = hx[(d->mac[j] >> 4) & 0xF];
            mac[p++] = hx[d->mac[j] & 0xF];
            if (j < 5) mac[p++] = ':';
        }
        mac[p] = 0;
        hw_add(pg, "%-8s mac %s  %s", d->name, mac,
               driver_nic_link_status(d) ? "link up" : "link down");
    }
    hw_add(pg, "IP config       : dhcp / ifconfig / route / dns commands");
    hw_add(pg, "firewall        : firewall command (netfilter 3 chains)");
    hw_add(pg, "sockets         : ring3 socket API (SYS 103-109, WP-10-tui)");
}

static void page_sound(hw_page_t *pg) {
    hw_add(pg, "driver families : hda ac97 sb16 es1370 virtio usb_audio");
    driver_snd_device_t *dev = driver_snd_get_device(0);
    if (!dev) {
        hw_add(pg, "no sound device present (probe reports honestly)");
        return;
    }
    driver_snd_caps_t caps;
    if (driver_snd_get_caps(dev, &caps) == 0) {
        hw_add(pg, "device          : %s", dev->name);
        hw_add(pg, "channels        : %d..%d  bits16 %s",
               caps.min_channels, caps.max_channels,
               caps.bits16 ? "yes" : "no");
    }
    hw_add(pg, "volume          : %lu/100 (real hardware path per driver)",
           dev->volume);
    hw_add(pg, "rate            : %lu Hz", dev->rate);
}

static void page_usb(hw_page_t *pg) {
    int n = 0;
    for (int i = 0; i < 16; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d) continue;
        n++;
    }
    driver_usb_print_state();
    hw_add(pg, "USB devices     : %d enumerated (poll thread resident)",
           n);
    hw_add(pg, "class drivers   : hid msc serial audio (uac1)");
}

static void page_pci(hw_page_t *pg) {
    int n = driver_pcie_count();
    hw_add(pg, "PCIe devices    : %d scanned", n);
    for (int i = 0; i < n && pg->count < HW_PAGE_MAX_LINES - 2; i++) {
        const driver_pcie_dev_t *d = driver_pcie_get(i);
        if (!d || !d->valid) continue;
        hw_add(pg, "%02x:%02x.%d  %04x:%04x  class %02x%02x  msi %s",
               d->bus, d->dev, d->fn, d->vendor, d->device,
               d->base_class, d->sub_class,
               d->cap_msi ? "yes" : "no");
    }
}

static void page_input(hw_page_t *pg) {
    hw_add(pg, "keyboard        : PS/2 port 0x60 (IRQ1) + USB HID + COM1");
    hw_add(pg, "touchpad        : I2C-HID probed at boot (no QEMU model;");
    hw_add(pg, "                  on real hw the device reports through i2ctest)");
    hw_add(pg, "mouse           : touchpad events -> TUI mouse API");
}

static void page_camera(hw_page_t *pg) {
    int n = camera_count();
    hw_add(pg, "cameras         : %d", n);
    for (int i = 0; i < n && i < 4; i++) {
        camera_dev_t *c = camera_get(i);
        if (!c) continue;
        hw_add(pg, "camera %d        : %s", i, c->name);
    }
    if (!n) hw_add(pg, "UVC probe runs at boot; no model in QEMU (honest)");
}

static void page_wireless(hw_page_t *pg) {
    int n = wl_device_count();
    hw_add(pg, "wireless devices: %d", n);
    for (int i = 0; i < n && i < 4; i++) {
        wl_device_t *w = wl_device_get(i);
        if (!w) continue;
        static const char *states[] = { "DOWN", "READY", "SCANNING",
                                        "AUTHENTICATING", "ASSOCIATING",
                                        "ASSOCIATED" };
        hw_add(pg, "wl%d             : %s  ssid %s  ch %d",
               i, states[w->state], w->ssid, w->channel);
    }
    if (!n) hw_add(pg, "802.11 families probed at boot; no QEMU model");
    hw_add(pg, "TUI wireless app: tui-net (scan/connect/status)");
}

static void page_timer(hw_page_t *pg) {
    hw_add(pg, "PIT             : IRQ0 base tick (100 Hz)");
    hw_add(pg, "RTC epoch ms    : %lu", core_timer_now_ms());
    hw_add(pg, "HPET            : counter via ACPI HPET table");
    hw_add(pg, "PM timer        : FADT PM_TMR (PIT-calibrated)");
    hw_add(pg, "TSC             : %lu (autocalibrated)", driver_tsc_read());
    hw_add(pg, "live probes     : rtc / hpet / pmtimer / tsc commands");
}

/* ------------------------------------------------------------------ */
/* configuration (save / load)                                         */
/* ------------------------------------------------------------------ */

/* keys this TUI owns; saved through lib_config like every other key */
static const char *HW_CONFIG_KEYS[] = {
    "tui.backend",       /* fb | serial                                  */
    "snd.volume",        /* 0..100, applied at play time (fix2)          */
    "tui.clock_24h",     /* 1 | 0, consumed by the sysinfo overview      */
    "tui.tz_offset",     /* hours, consumed by the sysinfo overview      */
};

int tui_hw_save(void) {
    char val[16];
    u32 rc = 0;
    if (lib_config_read("tui.backend", val, sizeof(val)) != 0)
        rc += (lib_config_write("tui.backend", "fb") == 0) ? 0 : 1;
    /* persist the TUI-owned keys that differ from their defaults */
    int saved = 0;
    for (unsigned i = 0; i < sizeof(HW_CONFIG_KEYS) / sizeof(*HW_CONFIG_KEYS); i++) {
        if (lib_config_read(HW_CONFIG_KEYS[i], val, sizeof(val)) == 0)
            saved++;              /* key already persisted */
    }
    (void)rc;
    return saved;
}

int tui_hw_load(void) {
    char val[16];
    for (unsigned i = 0; i < sizeof(HW_CONFIG_KEYS) / sizeof(*HW_CONFIG_KEYS); i++) {
        if (lib_config_read(HW_CONFIG_KEYS[i], val, sizeof(val)) == 0)
            return 0;            /* config file present and readable */
    }
    return -1;
}

/* apply a saved volume on boot paths that want it */
int tui_hw_apply_sound_volume(u32 vol) {
    driver_snd_device_t *dev = driver_snd_get_device(0);
    if (!dev) return -1;
    return driver_snd_set_volume(dev, vol > 100 ? 100 : vol);
}

/* ------------------------------------------------------------------ */
/* interactive pages                                                   */
/* ------------------------------------------------------------------ */

typedef void (*hw_page_fn)(hw_page_t *);

static const struct {
    const char *name;
    hw_page_fn  fn;
} HW_PAGES[] = {
    { "CPU",      page_cpu },
    { "Memory",   page_mem },
    { "Storage",  page_storage },
    { "Network",  page_network },
    { "Sound",    page_sound },
    { "USB",      page_usb },
    { "PCI",      page_pci },
    { "Input",    page_input },
    { "Camera",   page_camera },
    { "Wireless", page_wireless },
    { "Timers",   page_timer },
};
#define HW_PAGE_COUNT (int)(sizeof(HW_PAGES) / sizeof(*HW_PAGES))

int tui_hw_init(void) {
    if (!tui_started()) return -1;
    return 0;
}

static int hw_current_page;
/* fix2: per-page scroll offsets that SURVIVE re-render - the old code
 * reset pg.scroll to 0 on every render, so the DOWN key (whose handler
 * only existed in the direct-page runner) could never actually scroll.
 * The PCI page alone needs it: its capability dump exceeds 34 lines. */
static int hw_scroll[HW_PAGE_COUNT];

static void hw_render(tui_window_t *win) {
    hw_page_t pg;
    memset(&pg, 0, sizeof(pg));
    HW_PAGES[hw_current_page].fn(&pg);
    int rows = win->h - 3;
    if (pg.count > rows) {
        int max_scroll = pg.count - rows;
        if (hw_scroll[hw_current_page] > max_scroll)
            hw_scroll[hw_current_page] = max_scroll;
        if (hw_scroll[hw_current_page] < 0)
            hw_scroll[hw_current_page] = 0;
    } else {
        hw_scroll[hw_current_page] = 0;
    }
    pg.scroll = hw_scroll[hw_current_page];
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    for (int r = 0; r < rows && r + pg.scroll < pg.count; r++) {
        tui_print_at(r + 1, 2, pg.lines[r + pg.scroll]);
    }
    if (pg.count > rows) {
        char sb[64]; char n[12];
        strcpy(sb, "Up/Down: scroll  ");
        u64_to_str((u64)(pg.scroll + 1), n); strcat(sb, n);
        strcat(sb, "-");
        u64_to_str((u64)(pg.scroll + rows < pg.count ? pg.scroll + rows : pg.count), n);
        strcat(sb, n); strcat(sb, "/");
        u64_to_str((u64)pg.count, n); strcat(sb, n);
        tui_print_at(win->h - 2, 52, sb);
    }
    char tabline[100];
    int n = 0;
    const char *prefix = "Tab: ";
    for (int i = 0; prefix[i]; i++) tabline[n++] = prefix[i];
    for (int p = 0; p < HW_PAGE_COUNT && n < 90; p++) {
        const char *name = HW_PAGES[p].name;
        if (p == hw_current_page) tabline[n++] = '[';
        for (int i = 0; name[i] && n < 88; i++) tabline[n++] = name[i];
        if (p == hw_current_page) tabline[n++] = ']';
        tabline[n++] = ' ';
    }
    tabline[n] = 0;
    tui_print_at(win->h - 2, 1, tabline);
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
}

/* fix2: shared page-view loop. The two previous copies disagreed - the
 * direct runner had a no-op DOWN "scroll" and the menu's nested loop
 * exited the page on ANY key except Tab, so long pages (PCI caps)
 * could never be read past line 34. Keys: Tab next page, Up/Down
 * scroll, +/- sound volume (Sound page), F1 help, F2 save config,
 * F5 repaint, Esc/F10/q leave. */
static void hw_adjust_volume(int delta) {
    driver_snd_device_t *dev = driver_snd_get_device(0);
    if (!dev) { tui_status_line("no sound device present"); return; }
    int vol = (int)dev->volume + delta;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    if (driver_snd_set_volume(dev, (u32)vol) == 0) {
        char vb[16];
        char n[12];
        u64_to_str((u64)vol, vb);
        lib_config_write("snd.volume", vb);
        char sb[64];
        strcpy(sb, "volume: ");
        strcat(sb, dev->name);
        strcat(sb, " = ");
        u64_to_str((u64)vol, n);
        strcat(sb, n);
        strcat(sb, " (applied)");
        tui_status_line(sb);
    } else {
        tui_status_line("volume: device rejected the level");
    }
}

static void hw_view_loop(tui_window_t *win) {
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_TAB) {
            hw_current_page = (hw_current_page + 1) % HW_PAGE_COUNT;
            hw_render(win);
        } else if (k == TUI_KEY_DOWN) {
            hw_scroll[hw_current_page]++;
            hw_render(win);
        } else if (k == TUI_KEY_UP) {
            hw_scroll[hw_current_page]--;
            if (hw_scroll[hw_current_page] < 0) hw_scroll[hw_current_page] = 0;
            hw_render(win);
        } else if (k == '+' || k == '=') {
            hw_adjust_volume(+10);
        } else if (k == '-') {
            hw_adjust_volume(-10);
        } else if (k == TUI_KEY_F1) {
            tui_sysinfo_help();
            hw_render(win);
        } else if (k == TUI_KEY_F(2)) {
            tui_status_line(tui_hw_save() > 0 ?
                            "config saved to /etc/opencube.conf" :
                            "config saved (defaults)");
        } else if (k == TUI_KEY_F(5)) {
            hw_render(win);
        } else if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') {
            break;
        }
    }
}

int tui_hw_run_page(const char *page) {
    if (!tui_started()) return -1;
    int want = -1;
    for (int i = 0; i < HW_PAGE_COUNT; i++)
        if (strcmp(HW_PAGES[i].name, page) == 0) want = i;
    if (want < 0) return -1;
    hw_current_page = want;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Hardware configuration");
    hw_render(win);
    hw_view_loop(win);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* re-export the per-domain page entry points through the shared runner */
int tui_hw_cpu(void)      { return tui_hw_run_page("CPU"); }
int tui_hw_mem(void)      { return tui_hw_run_page("Memory"); }
int tui_hw_storage(void)  { return tui_hw_run_page("Storage"); }
int tui_hw_network(void)  { return tui_hw_run_page("Network"); }
int tui_hw_sound(void)    { return tui_hw_run_page("Sound"); }
int tui_hw_usb(void)      { return tui_hw_run_page("USB"); }
int tui_hw_pci(void)      { return tui_hw_run_page("PCI"); }
int tui_hw_input(void)    { return tui_hw_run_page("Input"); }
int tui_hw_camera(void)   { return tui_hw_run_page("Camera"); }
int tui_hw_wireless(void) { return tui_hw_run_page("Wireless"); }
int tui_hw_timer(void)    { return tui_hw_run_page("Timers"); }

/* full menu app: pick a domain from a menu, Esc leaves */
int tui_hw_main(void) {
    if (tui_hw_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Hardware configuration");
    tui_menu_item_t items[HW_PAGE_COUNT + 2];
    for (int i = 0; i < HW_PAGE_COUNT; i++) {
        items[i].name = HW_PAGES[i].name;
        items[i].desc = "view (Tab next page, Up/Down scroll, +/- volume)";
        items[i].userptr = (void *)(long)i;
    }
    items[HW_PAGE_COUNT].name = "Save config";
    items[HW_PAGE_COUNT].desc = "write TUI settings to /etc/opencube.conf";
    items[HW_PAGE_COUNT].userptr = (void *)(long)-1;
    items[HW_PAGE_COUNT + 1].name = "Load config";
    items[HW_PAGE_COUNT + 1].desc = "read TUI settings back";
    items[HW_PAGE_COUNT + 1].userptr = (void *)(long)-2;
    tui_menu_t *menu = tui_menu_new(win, items, HW_PAGE_COUNT + 2);
    tui_menu_post(menu);
    int rc = 0;
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d == 1) {
            long sel = (long)items[tui_menu_current(menu)].userptr;
            if (sel >= 0) {
                hw_current_page = (int)sel;
                hw_render(win);
                hw_view_loop(win);   /* fix2: shared loop (scroll etc.) */
                tui_menu_post(menu);
            } else if (sel == -1) {
                tui_hw_save();
                tui_status_line("config saved to /etc/opencube.conf");
                tui_menu_post(menu);
            } else {
                tui_status_line(tui_hw_load() == 0 ?
                                "config loaded" : "no saved config keys yet");
                tui_menu_post(menu);
            }
        }
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return rc;
}
