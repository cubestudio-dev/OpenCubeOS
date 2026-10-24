/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/tui_cn_hw.c
 * Purpose: Hardware configuration TUI with Chinese labels - same real
 *          driver data sources as tui/tui_hw.c, Chinese descriptions,
 *          and view/config/save operations.
 */
#include "tui_chinese.h"
#include "../core/lib/lib_string.h"
#include "../core/lib/lib_config.h"
#include "../core/mem/mem_pmm.h"
#include "../arch/x86_64/arch_multiboot2.h"
#include "../drivers/block/driver_block_blk.h"
#include "../drivers/nic/driver_nic.h"
#include "../drivers/snd/driver_snd.h"
#include "../drivers/usb/driver_usb.h"
#include "../drivers/pci/driver_pcie.h"
#include "../drivers/camera/driver_camera.h"
#include "../drivers/wireless/driver_wireless.h"
#include "../drivers/time/driver_tsc.h"
#include "../drivers/input/driver_input_keyboard.h"
#include "../core/core_timer.h"
#include "../drivers/display/screen_fb.h"

#define CN_PAGE_MAX_LINES 34

typedef struct {
    char lines[CN_PAGE_MAX_LINES][100];
    int  count;
} cn_page_t;

static void cn_add(cn_page_t *pg, const char *fmt, ...) {
    if (pg->count >= CN_PAGE_MAX_LINES) return;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    char *dst = pg->lines[pg->count];
    int n = 0;
    char num[24];
    for (const char *p = fmt; *p && n < 96; p++) {
        if (*p != '%') { dst[n++] = *p; continue; }
        p++;
        if (*p == 0) break;
        /* fix3: optional zero-padded width ("%02x" / "%04x"), so the
         * PCI page shows real vendor:device ids instead of the
         * literal "2x:2x" garbage the width-less formatter printed.
         * WP-10-tui-qa: "%-Ns" (left-justified minimum width) is now
         * supported too - the storage/network pages have used it
         * since fix2, but the formatter silently printed a literal
         * "8s" and skipped the va_arg, misaligning every following
         * argument on those two pages. */
        int pad = -1;
        int left_just = 0;
        if (*p == '-') {
            left_just = 1;
            p++;
        }
        if (*p == '0' && !left_just) {
            pad = 0;
            p++;
            while (*p >= '0' && *p <= '9') { pad = pad * 10 + (*p - '0'); p++; }
            if (pad > 16) pad = 16;
        } else if (*p >= '1' && *p <= '9') {
            pad = 0;
            while (*p >= '0' && *p <= '9') { pad = pad * 10 + (*p - '0'); p++; }
            if (pad > 40) pad = 40;
        }
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            int slen = 0;
            while (s && s[slen] && slen < 96) slen++;
            int spaces = (pad > slen) ? pad - slen : 0;
            if (left_just) {
                for (int i = 0; i < slen && n < 96; i++) dst[n++] = s[i];
                for (int i = 0; i < spaces && n < 96; i++) dst[n++] = ' ';
            } else {
                for (int i = 0; i < spaces && n < 96; i++) dst[n++] = ' ';
                for (int i = 0; i < slen && n < 96; i++) dst[n++] = s[i];
            }
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

static void cn_capacity_str(u64 bytes, char out[24]) {
    u64 mib = bytes >> 20;
    u64 whole = mib >> 10;
    u64 frac = ((mib & 1023) * 10 + 512) >> 10;
    /* WP-10-tui-qa: rounding can push frac to 10 (mib & 1023 >= 973);
     * the old code printed only the first digit, so a 2047 MiB disk
     * read "1.1GiB" instead of "2.0GiB".  Carry into the whole part. */
    if (frac >= 10) { frac = 0; whole++; }
    char a[20], b[4];
    u64_to_str(whole, a);
    u64_to_str(frac, b);
    int n = 0;
    for (int i = 0; a[i]; i++) out[n++] = a[i];
    out[n++] = '.';
    out[n++] = b[0];
    out[n++] = 'G';
    out[n++] = 'i';
    out[n++] = 'B';
    out[n] = 0;
}

/* ---- per-domain pages (same real data sources as tui_hw.c) ---------- */

static void cn_page_cpu(cn_page_t *pg) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    char vendor[13];
    char *v = vendor;
    for (int i = 0; i < 4; i++) *v++ = (char)((b >> (i * 8)) & 0xFF);
    for (int i = 0; i < 4; i++) *v++ = (char)((d >> (i * 8)) & 0xFF);
    for (int i = 0; i < 4; i++) *v++ = (char)((c >> (i * 8)) & 0xFF);
    *v = 0;
    cn_add(pg, "处理器厂商       : %s", vendor);
    u32 max_leaf = a;
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
    cn_add(pg, "处理器型号       : %s", brand);
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    cn_add(pg, "家族/型号        : fam %d model %d stepping %d",
           (int)((a >> 8) & 0xF), (int)(((a >> 4) & 0xF) | ((a >> 16) & 0xF0)),
           (int)(a & 0xF));
    cn_add(pg, "指令特性         : %s%s%s%s",
           (d & (1 << 25)) ? "SSE " : "",
           (c & (1 << 0)) ? "SSE3 " : "",
           (c & (1 << 20)) ? "SSE4.2 " : "",
           (c & (1 << 28)) ? "AVX " : "");
    cn_add(pg, "虚拟化           : %s",
           (c & (1 << 5)) ? "VMX 支持" : "无");
    cn_add(pg, "TSC 计数器       : %lu", driver_tsc_read());
}

static void cn_page_mem(cn_page_t *pg) {
    mem_pmm_stats_t st;
    mem_pmm_get_stats(&st);
    char total[24], used[24], freeb[24];
    cn_capacity_str(st.total_bytes, total);
    cn_capacity_str(st.used_bytes, used);
    cn_capacity_str(st.free_bytes, freeb);
    cn_add(pg, "内存总量         : %s", total);
    cn_add(pg, "内存已用         : %s", used);
    cn_add(pg, "内存可用         : %s", freeb);
    cn_add(pg, "物理页           : %lu 总 / %lu 已用 / %lu 空闲",
           st.total_pages, st.used_pages, st.free_pages);
    cn_add(pg, "空闲段数         : %lu（碎片指标）", st.free_fragments);
    cn_add(pg, "分配计数         : %lu 分配 / %lu 释放 / %lu 失败",
           st.total_allocs, st.total_frees, st.alloc_failures);
    cn_add(pg, "内核堆           : kmalloc 服务（首次适配 + 合并）");
    const u8 *self_data = 0; u64 self_size = 0;
    if (arch_multiboot2_get_kernel_self(&self_data, &self_size) == 0)
        cn_add(pg, "内核自镜像       : %lu 字节", self_size);
    else
        cn_add(pg, "内核自镜像       : 未加载（磁盘启动）");
}

static void cn_page_storage(cn_page_t *pg) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
        driver_block_device_t *d = driver_block_get_device(i);
        if (!d) continue;
        n++;
        char cap[24];
        cn_capacity_str(d->sectors * d->sector_size, cap);
        cn_add(pg, "%-8s 容量 %s  扇区 %lu x %d",
               d->name, cap, d->sectors, d->sector_size);
    }
    if (!n) cn_add(pg, "无块设备注册");
    cn_add(pg, "");
    cn_add(pg, "文件系统         : ramfs( / ) fat32 exfat ext4(只读)");
    cn_add(pg, "挂载信息         : 见 mounts 命令（VFS 表）");
    cn_add(pg, "分区/格式化操作  : parted / mkfs.fat32 / mkfs.exfat / fsck");
    cn_add(pg, "                 仍为 shell 命令（破坏性操作）");
}

static void cn_page_network(cn_page_t *pg) {
    int n = driver_nic_num_devices();
    cn_add(pg, "网卡数量         : %d", n);
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
        cn_add(pg, "%-8s MAC %s  %s", d->name, mac,
               driver_nic_link_status(d) ? "链路连通" : "链路断开");
    }
    cn_add(pg, "IP 配置          : dhcp / ifconfig / route / dns 命令");
    cn_add(pg, "防火墙           : firewall 命令（netfilter 三链）");
    cn_add(pg, "套接字           : ring3 socket API（SYS 103-109）");
}

static void cn_page_sound(cn_page_t *pg) {
    cn_add(pg, "驱动家族         : hda ac97 sb16 es1370 virtio usb_audio");
    driver_snd_device_t *dev = driver_snd_get_device(0);
    if (!dev) {
        cn_add(pg, "无声卡设备（探测结果如实报告）");
        return;
    }
    driver_snd_caps_t caps;
    if (driver_snd_get_caps(dev, &caps) == 0) {
        cn_add(pg, "设备             : %s", dev->name);
        cn_add(pg, "声道             : %d..%d  16 位 %s",
               caps.min_channels, caps.max_channels,
               caps.bits16 ? "是" : "否");
    }
    cn_add(pg, "音量             : %lu/100（各驱动真实硬件路径）",
           dev->volume);
    cn_add(pg, "采样率           : %lu Hz", dev->rate);
}

static void cn_page_usb(cn_page_t *pg) {
    int n = 0;
    for (int i = 0; i < 16; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d) continue;
        n++;
    }
    /* fix2: driver_usb_print_state() wrote its English report straight
     * to the console, scrolling text over the Chinese page while the
     * user was inside the TUI. The page now queries the same state and
     * renders it as page lines. */
    int hubs = 0, msc = 0, hid = 0, audio = 0, serial = 0, other = 0;
    for (int i = 0; i < 16; i++) {
        driver_usb_dev_t *d = driver_usb_get_device(i);
        if (!d) continue;
        switch (d->class) {
        case 0x09: hubs++; break;
        case 0x08: msc++; break;
        case 0x03: hid++; break;
        case 0x01: audio++; break;
        case 0x02: serial++; break;
        default: other++; break;
        }
    }
    cn_add(pg, "USB 设备         : 已枚举 %d 台（轮询线程常驻）", n);
    if (n > 0) {
        cn_add(pg, "集线器 %d  存储 %d  人机接口 %d  音频 %d  串口 %d  其他 %d",
               hubs, msc, hid, audio, serial, other);
        for (int i = 0; i < 16 && pg->count < CN_PAGE_MAX_LINES - 2; i++) {
            driver_usb_dev_t *d = driver_usb_get_device(i);
            if (!d) continue;
            if (d->product[0])
                cn_add(pg, "  %d: %s（供应商 0x%04x 产品 0x%04x 类 0x%02x）",
                       i + 1, d->product, d->vid, d->pid, d->class);
            else
                cn_add(pg, "  %d: 供应商 0x%04x 产品 0x%04x 类 0x%02x",
                       i + 1, d->vid, d->pid, d->class);
        }
    }
    cn_add(pg, "类驱动           : hid msc serial audio (uac1)");
}

static void cn_page_pci(cn_page_t *pg) {
    int n = driver_pcie_count();
    cn_add(pg, "PCIe 设备        : 扫描到 %d 台", n);
    for (int i = 0; i < n && pg->count < CN_PAGE_MAX_LINES - 2; i++) {
        const driver_pcie_dev_t *d = driver_pcie_get(i);
        if (!d || !d->valid) continue;
        cn_add(pg, "%02x:%02x.%d  %04x:%04x  类别 %02x%02x  MSI %s",
               d->bus, d->dev, d->fn, d->vendor, d->device,
               d->base_class, d->sub_class,
               d->cap_msi ? "支持" : "无");
    }
}

static void cn_page_input(cn_page_t *pg) {
    cn_add(pg, "键盘             : PS/2 端口 0x60（IRQ1）+ USB HID + COM1");
    cn_add(pg, "触摸板           : 启动时探测 I2C-HID（QEMU 无此模型；");
    cn_add(pg, "                 真实硬件经 i2ctest 上报）");
    cn_add(pg, "鼠标             : 触摸板事件 → TUI 鼠标 API");
}

static void cn_page_camera(cn_page_t *pg) {
    int n = camera_count();
    cn_add(pg, "摄像头数量       : %d", n);
    for (int i = 0; i < n && i < 4; i++) {
        camera_dev_t *c = camera_get(i);
        if (!c) continue;
        cn_add(pg, "摄像头 %d        : %s", i, c->name);
    }
    if (!n) cn_add(pg, "UVC 启动时探测；QEMU 无该模型（如实报告）");
}

static void cn_page_wireless(cn_page_t *pg) {
    int n = wl_device_count();
    cn_add(pg, "无线设备         : %d", n);
    for (int i = 0; i < n && i < 4; i++) {
        wl_device_t *w = wl_device_get(i);
        if (!w) continue;
        static const char *states[] = { "关闭", "就绪", "扫描中",
                                        "认证中", "关联中", "已关联" };
        cn_add(pg, "wl%d             : %s  SSID %s  信道 %d",
               i, states[w->state], w->ssid, w->channel);
    }
    if (!n) cn_add(pg, "802.11 家族启动时探测；QEMU 无该模型");
    cn_add(pg, "无线中文界面     : tui-cn 第 2 项（扫描/连接/状态）");
}

static void cn_page_timer(cn_page_t *pg) {
    cn_add(pg, "PIT              : IRQ0 基准节拍（100 Hz）");
    cn_add(pg, "RTC 纪元毫秒     : %lu", core_timer_now_ms());
    cn_add(pg, "HPET             : 经 ACPI HPET 表读计数器");
    cn_add(pg, "PM 定时器        : FADT PM_TMR（PIT 校准）");
    cn_add(pg, "TSC              : %lu（自动校准）", driver_tsc_read());
    cn_add(pg, "在线探测         : rtc / hpet / pmtimer / tsc 命令");
}

/* ---- page dispatch --------------------------------------------------- */

typedef void (*cn_page_fn)(cn_page_t *);

static void cn_page_display(cn_page_t *pg) {
    const screen_fb_info_t *fb = screen_fb_get_info();
    if (fb) {
        cn_add(pg, "帧缓冲        ：%lu×%lu，%d bpp，行距 %lu",
               (u64)fb->width, (u64)fb->height, (int)fb->bpp, (u64)fb->pitch);
        cn_add(pg, "通道掩码      ：红@%d/%d 绿@%d/%d 蓝@%d/%d",
               (int)fb->red_pos, (int)fb->red_size,
               (int)fb->green_pos, (int)fb->green_size,
               (int)fb->blue_pos, (int)fb->blue_size);
    } else {
        cn_add(pg, "帧缓冲        ：未初始化");
    }
    cn_add(pg, "单元格控制台  ：8×16（screen_console）");
    cn_add(pg, "ASCII 字体    ：内置 8×16，256 字形");
    cn_add(pg, "中文字体      ：内嵌 Unifont 16×16，6763 汉字 + 682 符号");
    cn_add(pg, "中文渲染器    ：console_cn（UTF-8），TUI 宽格感知");
    cn_add(pg, "终端后端      ：见 tui.backend 配置键（fb | serial）");
    cn_add(pg, "自测          ：`font-test` / `chinesetest` 打印样例表");
}

static const struct {
    const char *cn_name;      /* Chinese page label */
    const char *en_name;      /* ASCII page key (E2E/log use) */
    cn_page_fn  fn;
} CN_HW_PAGES[] = {
    { "处理器", "CPU",      cn_page_cpu },
    { "内存",   "Memory",   cn_page_mem },
    { "存储",   "Storage",  cn_page_storage },
    { "网络",   "Network",  cn_page_network },
    { "声卡",   "Sound",    cn_page_sound },
    { "USB",    "USB",      cn_page_usb },
    { "PCI",    "PCI",      cn_page_pci },
    { "输入",   "Input",    cn_page_input },
    { "摄像头", "Camera",   cn_page_camera },
    { "无线",   "Wireless", cn_page_wireless },
    { "定时器", "Timers",   cn_page_timer },
    { "显示",   "Display",  cn_page_display },   /* WP-10-tui-native: was missing from the twin */
};
#define CN_HW_PAGE_COUNT (int)(sizeof(CN_HW_PAGES) / sizeof(CN_HW_PAGES[0]))

static int g_cn_hw_page;   /* current page for cn_hw_render */

static void cn_hw_render(tui_window_t *win) {
    cn_page_t pg;
    pg.count = 0;
    CN_HW_PAGES[g_cn_hw_page].fn(&pg);
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "硬件配置 - ");
    tui_print(CN_HW_PAGES[g_cn_hw_page].cn_name);
    tui_print("  [Tab 下一页  S 保存  L 加载  q 退出]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    int max = win->h - 2;
    if (pg.count < max) max = pg.count;
    for (int i = 0; i < max; i++) {
        tui_print_at(i + 1, 1, pg.lines[i]);
    }
    tui_window_refresh(win);
}

/* WP-10-tui-native: run ONE Chinese twin page by its English page key
 * (the desktop's direct hw-page entries dispatch here when
 * shell.lang == zh).  Renders the single page once and waits for a key.
 * Returns 0 ok, -1 bad args/window, -3 no such page in the twin. */
int tui_cn_hw_page(const char *en_name) {
    if (!en_name || !en_name[0]) return -1;
    int idx = -1;
    for (int i = 0; i < CN_HW_PAGE_COUNT; i++) {
        if (strcmp(CN_HW_PAGES[i].en_name, en_name) == 0) { idx = i; break; }
    }
    if (idx < 0) return -3;
    if (tui_chinese_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    g_cn_hw_page = idx;
    cn_hw_render(win);
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == 'q' || k == TUI_KEY_F(10)) break;
        if (k == TUI_KEY_TAB) {          /* parity with the full app */
            g_cn_hw_page = (g_cn_hw_page + 1) % CN_HW_PAGE_COUNT;
            cn_hw_render(win);
        }
    }
    tui_window_free(win);
    return 0;
}

int tui_cn_hw_main(void) {
    if (tui_chinese_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "硬件配置（中文）");
    for (;;) {
        cn_hw_render(win);
        int k = tui_getch();
        if (k == TUI_KEY_TAB) {
            g_cn_hw_page = (g_cn_hw_page + 1) % CN_HW_PAGE_COUNT;
        } else if (k == 's' || k == 'S') {
            /* fix1: the English hw app had Save/Load config; the
             * Chinese twin lacked them - same lib_config keys, Chinese
             * status line. */
            tui_hw_save();
            tui_status_line("配置已保存到 /etc/opencube.conf");
        } else if (k == 'l' || k == 'L') {
            tui_status_line(tui_hw_load() == 0 ?
                            "配置已加载" : "尚无已保存的配置");
        } else if (k == TUI_KEY_ESC || k == 'q' || k == TUI_KEY_F(10)) {
            break;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return CN_HW_PAGE_COUNT;
}
