/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/tcp_cc.h
 * Purpose: CUBIC congestion control (RFC 8312) as pure functions, so the
 *          window logic is unit-testable on the host with fixed vectors.
 *          All windows are in BYTES; time is in kernel ticks (100 Hz).
 *          Pure: no kernel state touched by these functions.
 */
#ifndef OC_TCP_CC_H
#define OC_TCP_CC_H

#include "types.h"

/* RFC 8312 constants */
#define CC_CUBIC_C          410  /* scaled C = 0.4 << 10 */
#define CC_CUBIC_BETA       7    /* beta = 0.7 (scaled by 10) */
#define CC_CUBIC_BETA_DEN   10
#define CC_SCALE            10   /* fixed point shift for beta math */

typedef struct {
    u32 w_max;        /* pre-congestion window (bytes) */
    u32 w_last_max;   /* previous W_max for fast convergence (bytes) */
    u64 epoch_start;  /* tick when the current epoch began (0 = start now) */
    u32 k_ticks;      /* time to reach w_max after the epoch starts */
    u32 tcp_cwnd;     /* Reno-friendly window estimate (bytes) */
    u32 tcp_acks;     /* ACK byte accumulator for Reno-friendly growth */
} cubic_state_t;

/* Integer cube root: floor(x^(1/3)) for x < 2^32. */
u32 integer_cbrt(u32 x);

/* beta * x for x a byte count (RFC 8312 multiplication decrease). */
u32 cc_beta(u32 x);

/* Reset the state for a new connection. */
void cc_init(cubic_state_t *s);

/* Recompute K (ticks) for a given W_max (bytes) and MSS. */
void cc_compute_k(cubic_state_t *s, u32 w_max_bytes, u32 mss);

/* Called once per valid ACK while in congestion avoidance.
 *   cwnd_bytes: current congestion window
 *   ssthresh_bytes: slow-start threshold (bytes)
 *   now: current tick
 *   mss: maximum segment size
 *   bytes_acked: how many bytes this ACK acknowledges (usually mss)
 * Returns the new congestion window (bytes). Handles the slow-start ->
 * congestion-avoidance transition internally (caller checks ssthresh). */
u32 cc_on_ack(cubic_state_t *s, u32 cwnd_bytes, u32 ssthresh_bytes,
              u64 now, u32 mss, u32 bytes_acked);

/* Called when the RTO fires: W_max = cwnd (fast convergence applied),
 * returns new ssthresh (bytes); the caller sets cwnd = 1 MSS (RFC 8312
 * RTO behavior re-enters slow start). */
u32 cc_on_rto(cubic_state_t *s, u32 cwnd_bytes);

/* Called when 3 duplicate ACKs trigger fast retransmit: returns the new
 * ssthresh (bytes); the caller inflates cwnd = ssthresh + 3*MSS during
 * fast recovery and deflates to ssthresh when the recovery ends. */
u32 cc_on_fast_recovery_enter(cubic_state_t *s, u32 cwnd_bytes);

#endif /* OC_TCP_CC_H */
