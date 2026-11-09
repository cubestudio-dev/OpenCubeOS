/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Host unit test for the CUBIC congestion control core (RFC 8312).
 * Build (host):
 *   gcc -I kernel -o /tmp/cc_test tests/host_tcpcc_test.c kernel/net_tcp_cc.c
 */
#include <stdio.h>
#include "types.h"
#include "net_tcp_cc.h"

static int g_fail = 0;

static void check(const char *name, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

int main(void) {
    printf("CUBIC (RFC 8312) host unit tests:\n");

    /* ---- integer_cbrt ---- */
    check("cbrt(0)=0", integer_cbrt(0) == 0);
    check("cbrt(1)=1", integer_cbrt(1) == 1);
    check("cbrt(8)=2", integer_cbrt(8) == 2);
    check("cbrt(27)=3", integer_cbrt(27) == 3);
    check("cbrt(1000)=10", integer_cbrt(1000) == 10);
    check("cbrt(27000)=30", integer_cbrt(27000) == 30);
    check("cbrt(45)=3 (floor)", integer_cbrt(45) == 3);
    check("cbrt(46)=3 (floor)", integer_cbrt(46) == 3);
    check("cbrt(64)=4", integer_cbrt(64) == 4);
    check("cbrt(2^31-1)=1290", integer_cbrt(2147483647u) == 1290);
    check("cbrt(999999999)=999", integer_cbrt(999999999u) == 999);

    /* ---- beta decrease ---- */
    check("beta(10000)=7000", net_tcp_cc_beta(10000) == 7000);
    check("beta(536)=375", net_tcp_cc_beta(536) == 375);
    check("beta(1072)=750", net_tcp_cc_beta(1072) == 750);

    /* ---- slow start passthrough (caller's job) ---- */
    cubic_state_t s;
    net_tcp_cc_init(&s);
    u32 w = net_tcp_cc_on_ack(&s, 1072, 21500, 0, 536, 536);
    check("slow start: cwnd<ss -> +bytes_acked", w == 1072 + 536);

    /* ---- CUBIC curve: W_max=60 MSS, mss=536 ----
     * K = cbrt(60*3/4)*100 = cbrt(45)*100 = 355 ticks (3.55s)
     * At t=100 ticks (1 s): W = 0.4*(1+3.55)^3 + 60 = 97.67 MSS
     * Integer path: tk=455, cube=455^3=94,196,375,
     *   w_mss = 94,196,375/1e6*4/10 = 94*4/10 = 37 -> (37+60)*536 = 51,992
     * RFC value 97.67 MSS = 52,351 B -> tolerance 2 MSS. */
    net_tcp_cc_init(&s);
    s.w_max = 60 * 536;
    net_tcp_cc_compute_k(&s, 60 * 536, 536);
    s.epoch_start = 1;   /* non-zero: avoids the epoch-start sentinel */
    s.net_tcp_cwnd = 60 * 536;
    s.net_tcp_acks = 0;
    u32 now = net_tcp_cc_on_ack(&s, 60 * 536, 60 * 536, 101, 536, 536);
    u32 got_mss = now / 536;
    check("cubic curve t=1s -> 96..99 MSS", got_mss >= 96 && got_mss <= 99);

    /* t=10s (1000 ticks): W = 0.4*(10+3.5)^3+60 = 1055 MSS */
    net_tcp_cc_init(&s);
    s.w_max = 60 * 536;
    net_tcp_cc_compute_k(&s, 60 * 536, 536);
    s.epoch_start = 1;
    s.net_tcp_cwnd = 60 * 536;
    u32 now10 = net_tcp_cc_on_ack(&s, 60 * 536, 60 * 536, 1001, 536, 536);
    u32 got10 = now10 / 536;
    check("cubic curve t=10s -> ~1055 MSS (>=500)", got10 >= 500 && got10 <= 1100);

    /* ---- RTO: ssthresh = 0.7*cwnd, floor 2*536 ---- */
    net_tcp_cc_init(&s);
    u32 ss = net_tcp_cc_on_rto(&s, 20000);
    check("rto: ssthresh = 0.7*cwnd", ss == 14000);
    check("rto: w_max recorded", s.w_max == 20000);
    /* fast convergence: w_max shrinks when below W_last_max */
    net_tcp_cc_init(&s);
    s.w_max = 30000; s.w_last_max = 50000;
    ss = net_tcp_cc_on_rto(&s, 20000);
    check("rto fast convergence: w_max shrunk to 21000", s.w_max == 21000);
    check("rto floor: 2*536 minimum", net_tcp_cc_on_rto(&s, 1000) == 2 * 536);

    /* ---- fast recovery enter ---- */
    net_tcp_cc_init(&s);
    ss = net_tcp_cc_on_fast_recovery_enter(&s, 20000);
    check("fast recovery: ssthresh = 0.7*cwnd = 14000", ss == 14000);
    check("fast recovery: w_max = cwnd", s.w_max == 20000);

    /* ---- Reno-friendly growth: 1 MSS per cwnd of ACKs ---- */
    net_tcp_cc_init(&s);
    s.w_max = 100 * 536;
    s.epoch_start = 0;
    s.net_tcp_cwnd = 100 * 536;
    s.net_tcp_acks = 0;
    s.k_ticks = 0;
    u32 w2 = 100 * 536;
    for (int i = 0; i < 100; i++) {
        w2 = net_tcp_cc_on_ack(&s, w2, 100 * 536, 0, 536, 536);
    }
    /* 100 ACKs x 536B = 53600B acked >= cwnd -> +1 MSS (cubic may add more) */
    check("reno-friendly: growth >= 1 MSS after 1 RTT of ACKs",
          w2 >= 100 * 536 + 536);

    printf("%s: %s (%d failures)\n", __FILE__,
           g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
