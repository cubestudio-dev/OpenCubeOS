/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/tcp_cc.c
 * Purpose: CUBIC congestion control (RFC 8312), pure-function core.
 *
 * Units:
 *   - windows in BYTES, time in kernel ticks (100 Hz -> 100 ticks = 1 s)
 *   - W_cubic(t) = C*(t + K)^3 + W_max  (MSS units, t,K in seconds)
 *   - with t in ticks: W_cubic = C * (t_ticks + K_ticks)^3 / 1e6 + W_max
 *     (1e6 = 100^3 converts ticks^3 to s^3; C = 0.4)
 *   - K_ticks = cbrt(W_max_mss * (1-beta)/C) * 100 = cbrt(W_max_mss * 3/4) * 100
 *   - beta = 0.7 (multiplication decrease factor, RFC 8312 4.5/4.6)
 */
#include "tcp_cc.h"

u32 integer_cbrt(u32 x) {
    if (x == 0) return 0;
    /* Newton iteration from an initial power-of-two guess. */
    u32 r = 1;
    while ((u64)r * r * r < (u64)x) r <<= 1;
    for (int i = 0; i < 40; i++) {
        u64 nr = (2 * (u64)r + (u64)x / ((u64)r * r)) / 3;
        if (nr == 0) nr = 1;
        if (nr == (u64)r) break;
        r = (u32)nr;
    }
    /* floor correction (Newton may overshoot by one) */
    while ((u64)r * r * r > (u64)x) r--;
    while ((u64)(r + 1) * (r + 1) * (r + 1) <= (u64)x) r++;
    return r;
}

u32 cc_beta(u32 x) {
    return (x / 10) * 7 + ((x % 10) * 7) / 10;   /* beta = 0.7, rounded down */
}

void cc_init(cubic_state_t *s) {
    s->w_max = 0;
    s->w_last_max = 0;
    s->epoch_start = 0;
    s->k_ticks = 0;
    s->tcp_cwnd = 0;
    s->tcp_acks = 0;
}

void cc_compute_k(cubic_state_t *s, u32 w_max_bytes, u32 mss) {
    u64 w_mss = (u64)(w_max_bytes / (mss ? mss : 1));
    /* (1-beta)/C = 0.3/0.4 = 3/4; millisecond precision:
     * K_ms = cbrt(target*1000); K_ticks = K_ms/10 */
    u64 target = (w_mss * 3) / 4;
    s->k_ticks = integer_cbrt((u32)(target * 1000)) * 10;
}

u32 cc_on_rto(cubic_state_t *s, u32 cwnd_bytes) {
    /* RFC 8312 4.6: record the pre-shrink window, apply fast convergence. */
    if (s->w_max < cwnd_bytes) s->w_max = cwnd_bytes;
    if (s->w_max < s->w_last_max) {
        s->w_max = cc_beta(s->w_max);
    }
    s->w_last_max = s->w_max;
    u32 ss = cc_beta(cwnd_bytes);
    if (ss < 2 * 536) ss = 2 * 536;
    return ss;
}

u32 cc_on_fast_recovery_enter(cubic_state_t *s, u32 cwnd_bytes) {
    if (s->w_max < cwnd_bytes) s->w_max = cwnd_bytes;
    if (s->w_max < s->w_last_max) {
        s->w_max = cc_beta(s->w_max);
    }
    s->w_last_max = s->w_max;
    u32 ss = cc_beta(cwnd_bytes);
    if (ss < 2 * 536) ss = 2 * 536;
    return ss;
}

u32 cc_on_ack(cubic_state_t *s, u32 cwnd_bytes, u32 ssthresh_bytes,
              u64 now, u32 mss, u32 bytes_acked) {
    if (mss == 0) mss = 536;
    /* Slow start is the caller's job; here we only run CUBIC once the
     * window has reached the threshold. */
    if (cwnd_bytes < ssthresh_bytes) {
        return cwnd_bytes + bytes_acked;
    }

    /* Start a new epoch on the first congestion-avoidance ACK. */
    if (s->epoch_start == 0) {
        s->epoch_start = now;
        if (s->w_max < cwnd_bytes) s->w_max = cwnd_bytes;
        cc_compute_k(s, s->w_max, mss);
        s->tcp_cwnd = cwnd_bytes;
        s->tcp_acks = 0;
    }

    /* --- CUBIC part: W = (C*(t+K)^3/1e6 + W_max_mss) * mss bytes --- */
    u64 tk = (now - s->epoch_start) + s->k_ticks;
    u64 cube = tk * tk * tk;                       /* ticks^3, u64-safe */
    u64 w_cubic_mss = cube / 1000000 * 4 / 10;     /* C=0.4 -> *4/10 */
    w_cubic_mss += (u64)(s->w_max / mss);
    u32 w_cubic = (u32)(w_cubic_mss * (u64)mss);

    /* --- Reno-friendly part (RFC 8312 4.1): ~1 MSS per RTT of ACKs --- */
    s->tcp_acks += bytes_acked;
    while (s->tcp_acks >= cwnd_bytes && cwnd_bytes > 0) {
        s->tcp_cwnd += mss;
        s->tcp_acks -= cwnd_bytes;
    }
    u32 reno_wnd = s->tcp_cwnd;

    u32 new_cwnd = (w_cubic > reno_wnd) ? w_cubic : reno_wnd;
    if (new_cwnd < cwnd_bytes) new_cwnd = cwnd_bytes; /* never shrink on ACK */
    return new_cwnd;
}
