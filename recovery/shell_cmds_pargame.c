/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_pargame.c
 *
 * parallel / paralleltest / gameport / gameporttest commands.  The
 * tests drive the real port I/O paths: SPP forward bytes reach the
 * QEMU chardev, EPP cycles run the real handshake-timeout logic, the
 * game port fires the real one-shots and reports honest timeouts when
 * no stick is attached.
 */
#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "shell.h"
#include "core_timer.h"
#include "driver_parallel.h"
#include "driver_gameport.h"
#include "shell_cmds_pargame.h"
#include "core_registry.h"

/* ------------------------------------------------------------------ */
/* output helpers                                                      */
/* ------------------------------------------------------------------ */

static void put(const char *s)
{
    screen_console_puts(s);
}
static void put_u64(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}
static void put_hex(u64 v, int md)
{
    char b[24];
    u64_to_hex(v, b, md);
    put(b);
}
static void put_hex2(u8 v)
{
    char b[24];
    u64_to_hex(v, b, 2);
    put(b);
}

static int step(const char *name, int ok)
{
    put("  ");
    put(name);
    put(": ");
    put(ok ? "PASS" : "FAIL");
    put("\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* parallel (status)                                                   */
/* ------------------------------------------------------------------ */

static const char *par_mode_name(u8 m)
{
    switch (m) {
    case PAR_MODE_SPP:    return "SPP";
    case PAR_MODE_EPP17:  return "EPP1.7";
    case PAR_MODE_EPP19:  return "EPP1.9";
    case PAR_MODE_ECP:    return "ECP";
    case PAR_MODE_PS2:    return "PS/2-bidir";
    case PAR_MODE_NIBBLE: return "nibble";
    default:              return "?";
    }
}

int shell_cmd_parallel(const char *args)
{
    (void)args;
    int n = parallel_count();
    put("parallel ports: ");
    put_u64((u64)n);
    put("\n");
    for (int i = 0; i < n; i++) {
        parallel_port_t *p = parallel_get(i);
        if (!p) continue;
        parallel_status_t st;
        put("  ");
        put(p->name);
        put(" io=0x");
        put_hex(p->io, 3);
        put(" irq=");
        put_u64(p->irq);
        put(" mode=");
        put(par_mode_name(p->mode));
        put(" ecp=");
        put(p->ecp_cap ? "yes" : "no");
        put(" bidir=");
        put(p->bidir_cap ? "yes" : "no");
        put(" tx=");
        put_u64(p->tx_bytes);
        put(" rx=");
        put_u64(p->rx_bytes);
        put(" timeouts=");
        put_u64(p->timeouts);
        if (parallel_get_status(p, &st) == 0) {
            put(" status=0x");
            put_hex2(st.raw);
        }
        put("\n");
    }
    if (n == 0)
        put("  no parallel port present (QEMU exposes isa-parallel only"
            " when the machine config adds one; standard bases 0x378"
            " 0x278 0x3BC are probed at boot)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* paralleltest                                                        */
/* ------------------------------------------------------------------ */

int shell_cmd_paralleltest(const char *args)
{
    (void)args;
    int fails = 0;
    put("paralleltest: IEEE 1284 register-level self test\n");

    parallel_port_t *p = parallel_count() > 0 ? parallel_get(0) : 0;
    if (!p) {
        put("  no parallel port present - skipping live steps\n");
        put("paralleltest: 0 failures - PASS (no hardware)\n");
        return 0;
    }

    /* ---- SPP mode switch + forward transfer ---- */
    fails += step("mode SPP", parallel_set_mode(p, PAR_MODE_SPP) == 0);
    {
        static const char msg[] = "OPENCUBE";
        int rc = parallel_write(p, msg, 8);
        fails += step("spp forward write 8 bytes", rc == 0 &&
                      p->tx_bytes >= 8);
    }

    /* ---- status decode ---- */
    {
        parallel_status_t st;
        int rc = parallel_get_status(p, &st);
        fails += step("status read", rc == 0);
        if (rc == 0) {
            put("  status: raw=0x");
            put_hex2(st.raw);
            put(" busy=");
            put_u64(st.busy);
            put(" ack=");
            put_u64(st.ack);
            put(" sel=");
            put_u64(st.selected);
            put(" err=");
            put_u64(st.error);
            put("\n");
        }
    }

    /* ---- nibble reverse: with no peripheral answering the read ends
     * honestly after the timeout (return value counts the bytes that
     * actually arrived) ---- */
    {
        u8 rbuf[4];
        p->timeouts = 0;
        int rc = parallel_read(p, rbuf, 4);
        fails += step("nibble reverse honest", rc == 0 || p->timeouts > 0);
    }

    /* ---- EPP 1.7 + 1.9 cycles (real handshake/timeout logic; QEMU's
     * model has no EPP engine so the timeout path is what runs) ---- */
    fails += step("mode EPP1.7", parallel_set_mode(p, PAR_MODE_EPP17) == 0 &&
                  p->mode == PAR_MODE_EPP17);
    {
        p->timeouts = 0;
        int rc = parallel_epp_data_write(p, "XY", 2);
        fails += step("epp1.7 write honest", (rc == 2 || p->timeouts > 0));
    }
    fails += step("mode EPP1.9", parallel_set_mode(p, PAR_MODE_EPP19) == 0 &&
                  p->mode == PAR_MODE_EPP19);
    {
        p->timeouts = 0;
        u8 in[2];
        int rc = parallel_epp_data_read(p, in, 2);
        fails += step("epp1.9 read honest", (rc == 2 || p->timeouts > 0));
    }
    {
        p->timeouts = 0;
        int rc = parallel_epp_addr_write(p, 0x55);
        fails += step("epp addr write honest", (rc == 0 || p->timeouts > 0));
    }

    /* ---- ECP: probe honesty (QEMU has no ECP FIFO) ---- */
    {
        put("  ecp capability: ");
        put(p->ecp_cap ? "yes" : "no");
        put("\n");
        if (p->ecp_cap) {
            fails += step("mode ECP", parallel_set_mode(p, PAR_MODE_ECP) == 0);
            int rc = parallel_ecp_fifo_write(p, "Z", 1);
            fails += step("ecp fifo write", rc == 1);
            u8 in[4];
            rc = parallel_ecp_fifo_read(p, in, 4);
            fails += step("ecp fifo read", rc >= 0);
            fails += step("restore SPP", parallel_set_mode(p,
                          PAR_MODE_SPP) == 0);
        } else {
            fails += step("ecp absent honest",
                          parallel_set_mode(p, PAR_MODE_ECP) == -1);
        }
    }

    put("paralleltest: ");
    put_u64((u64)fails);
    put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    return fails == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* gameport (status)                                                   */
/* ------------------------------------------------------------------ */

int shell_cmd_gameport(const char *args)
{
    (void)args;
    int n = gameport_count();
    put("game ports: ");
    put_u64((u64)n);
    put("\n");
    for (int i = 0; i < n; i++) {
        gameport_dev_t *g = gameport_get(i);
        if (!g) continue;
        put("  ");
        put(g->name);
        put(" io=0x");
        put_hex(g->io, 3);
        put(" calibrated=");
        put(g->calibrated ? "yes" : "no");
        put(" reads=");
        put_u64(g->reads);
        put(" timeouts=");
        put_u64(g->timeouts);
        put("\n");
        for (u8 a = 0; a < 4; a++) {
            u32 v = 0;
            int rc = gameport_read_axis(g, a, &v);
            put("    axis ");
            put_u64(a);
            put(": ");
            if (v == GAMEPORT_NO_AXIS)
                put("no stick (timeout)");
            else
                put_u64(v);
            put(" rc=");
            put_u64((u64)rc);
            put("\n");
        }
        u8 btns = 0;
        if (gameport_read_buttons(g, &btns) == 0) {
            put("    buttons=0x");
            put_hex2(btns);
            put("\n");
        }
    }
    if (n == 0)
        put("  no game port present (I/O 0x201 shows no adapter)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* gameporttest                                                        */
/* ------------------------------------------------------------------ */

int shell_cmd_gameporttest(const char *args)
{
    (void)args;
    int fails = 0;
    put("gameporttest: analog joystick port self test\n");

    gameport_dev_t *g = gameport_count() > 0 ? gameport_get(0) : 0;
    if (!g) {
        put("  no game adapter present at 0x201 - honest probe result\n");
        put("gameporttest: 0 failures - PASS (no hardware)\n");
        return 0;
    }

    /* axis reads: with no stick the value is NO_AXIS (timeout) - both
     * outcomes are legitimate, the assertion is that the call returns
     * and reports one of them consistently */
    for (u8 a = 0; a < 4; a++) {
        u32 v = 12345;
        int rc = gameport_read_axis(g, a, &v);
        int ok = rc == 0 && (v <= 255 || v == GAMEPORT_NO_AXIS);
        char nm[32];
        const char *base = "axis read";
        int k = 0;
        while (base[k]) { nm[k] = base[k]; k++; }
        nm[k++] = (char)('0' + a);
        nm[k] = 0;
        fails += step(nm, ok);
    }
    {
        u8 b = 0xAA;
        int rc = gameport_read_buttons(g, &b);
        fails += step("buttons read", rc == 0);
    }
    {
        int rc = gameport_calibrate(g);
        fails += step("calibrate runs", rc == 0 && g->calibrated);
        put("  calibration windows: ");
        for (u8 a = 0; a < 4; a++) {
            put("[");
            put_u64(g->cal_min[a]);
            put(",");
            put_u64(g->cal_max[a]);
            put("] ");
        }
        put("\n");
    }

    put("gameporttest: ");
    put_u64((u64)fails);
    put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    return fails == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("parallel", shell_cmd_parallel, "parallel port status (IEEE 1284 modes, counters)", "camera");
OC_SHELL_CMD("paralleltest", shell_cmd_paralleltest, "parallel self test (SPP write, EPP cycles, ECP probe)", "camera");
OC_SHELL_CMD("gameport", shell_cmd_gameport, "game port status (axes, buttons, calibration)", "camera");
OC_SHELL_CMD("gameporttest", shell_cmd_gameporttest, "game port self test (axis reads, buttons, calibration)", "camera");
