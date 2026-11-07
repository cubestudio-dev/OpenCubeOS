/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: include/ocbl_state.h
 * Purpose: the WP07a boot-state monitoring contract, shared by the
 *          three independent builds that must agree on it byte for
 *          byte:
 *
 *            ocbl/       (OCBL boot loader, BIOS + UEFI flavors)
 *            src/L0/     (OCOS service kernel - the monitored payload)
 *            ocire/      (OCIRE recovery kernel - the post-mortem reader)
 *
 *          Two fixed low-memory blocks implement the mechanism:
 *
 *            STATE AREA    0x98000  what the kernel is doing right
 *                                  now (stage / heartbeat / progress
 *                                  clock).  Written by the kernel
 *                                  (blind - the kernel never reads
 *                                  it back), read+written by OCBL,
 *                                  read by OCIRE.  L1 applications
 *                                  cannot reach it: the pages sit in
 *                                  the supervisor-only part of every
 *                                  user address space.
 *
 *            RESERVED AREA 0x9B000  the post-mortem boot record.
 *                                  Written by OCBL (the failure
 *                                  judgment or the user request),
 *                                  read by OCIRE after the reboot.
 *                                  Survives warm resets (watchdog /
 *                                  8042 pulses keep DRAM contents in
 *                                  QEMU and on mainstream PC
 *                                  firmware); a cold boot or power
 *                                  loss clears it - consumers must
 *                                  always validate the magic.
 *
 *          Detection architecture ("watchdog relay"): after the mb2
 *          handoff OCBL is no longer executing, so the per-phase
 *          timeouts are enforced by the 6300ESB hardware watchdog -
 *          OCBL arms the boot budget before the handoff, the kernel
 *          re-arms the phase budget at every stage advance and pets
 *          the timer from its 1 s tick at runtime.  A hung kernel
 *          stops feeding the dog, the hardware resets the machine,
 *          and OCBL - running again after the reset - reads the state
 *          area, judges the stall from the stage + the RTC silence,
 *          records BOOT_FAILED in the reserved area, tells the user
 *          on screen and pulses the 8042 reset; the next OCBL run
 *          sees the pending record and auto-boots OCIRE, which reads
 *          the record and prints the repair advice.
 *
 *          This header is freestanding-safe for all three builds and
 *          must stay dependency-free (ocst_ prefixed types only - the
 *          builds have conflicting u8/u16/u32 typedefs).
 */
#ifndef OCBL_STATE_H
#define OCBL_STATE_H

/* ---- freestanding base types (prefix to avoid collisions) -------- */
typedef unsigned char  ocst_u8;
typedef unsigned short ocst_u16;
typedef unsigned int   ocst_u32;
typedef unsigned long long ocst_u64;

/* ---- fixed low-memory layout -------------------------------------
 * Placement NOTE (WP07a live-measured in QEMU 10.0.13/SeaBIOS, three
 * independent pattern-probe runs): every system reset in this QEMU
 * wipes a huge deterministic low-memory swath - all probed pages in
 * [0x70000..0x9FC00) read back zero after the reset EXCEPT two
 * stable islands: the pages at 0x98000 and 0x9B000, which survived
 * all three runs (the wipe stops at the EBDA, 0x9FC00, and also
 * covers the OCBL page-table window 0x8F000..0x94FFF which OCBL
 * rewrites by design).  The monitor blocks therefore sit on the two
 * proven-surviving islands: STATE at 0x98000, RESERVED at 0x9B000
 * (they do not need to be adjacent - they are independent areas).
 * Both are also carved out of the multiboot2 memory map as E820
 * reserved so no payload ever allocates them, and the OCOS PMM
 * reserves the whole first MiB anyway. */
#define OCST_STATE_ADDR        0x98000u
#define OCST_STATE_SIZE        0x1000u
#define OCST_RESERVED_ADDR     0x9B000u
#define OCST_RESERVED_SIZE     0x1000u

/* magic values, little-endian byte order in memory: "OCS1" / "OCB1" */
#define OCST_STATE_MAGIC       0x3153434Fu
#define OCST_RESERVED_MAGIC    0x3142434Fu
#define OCST_STATE_VER         1u
#define OCST_RESERVED_VER      1u

/* ---- boot stages (the state area's `stage` field) ------------------
 * Numeric order == boot order.  The kernel only ever moves forward. */
#define OCST_STAGE_RESET            0u  /* no payload progress yet     */
#define OCST_STAGE_KM_AIN_ENTERED   1u  /* kmain() entered             */
#define OCST_STAGE_SCHED_READY      2u  /* scheduler + core mechanisms */
#define OCST_STAGE_DRIVERS_READY    3u  /* driver init complete        */
#define OCST_STAGE_SERVICES_READY   4u  /* root services ready         */
#define OCST_STAGE_L1_LAUNCHED      5u  /* default L1 app started      */
#define OCST_STAGE_L1_READY         6u  /* ush served (first syscall)  */
#define OCST_STAGE_COUNT            7u
#define OCST_STAGE_RUNTIME          255u /* marker: hung while RUNNING */

/* stage names, exactly as the failure message prints them */
#define OCST_STAGE0_NAME "BOOT"              /* pre-kmain handoff     */

/* ---- state area kernel flags (`kflags`) --------------------------- */
#define OCST_KFLAG_CLEAN       0x01u  /* kernel rebooted cleanly      */
#define OCST_KFLAG_JUDGED      0x02u  /* OCBL already recorded the
                                       * failure (written by OCBL right
                                       * before the 8042 pulse so the
                                       * next boot routes instead of
                                       * re-judging the same stall)    */

/* ---- reserved-area record kinds ----------------------------------- */
#define OCST_KIND_NONE          0u
#define OCST_KIND_BOOT_FAILED   1u   /* watchdog/judged boot failure   */
#define OCST_KIND_USER_REQUEST  2u   /* user chose OCIRE in the menu   */

/* ---- phase timeout budgets, seconds --------------------------------
 * The table the whole chain enforces (the requirement's default
 * values; real QEMU boots complete every phase well inside them):
 *   boot start  -> KM_AIN_ENTERED   30 s  (armed by OCBL pre-handoff)
 *   KM_AIN_ENTERED -> SCHED_READY   10 s
 *   SCHED_READY    -> DRIVERS_READY 10 s
 *   DRIVERS_READY  -> SERVICES      10 s
 *   SERVICES_READY -> L1_LAUNCHED   10 s
 *   L1_LAUNCHED    -> L1_READY      30 s
 *   runtime (post L1_READY)         15 s heartbeat (1 s tick pets)   */
#define OCST_BUDGET_BOOT        30u
#define OCST_BUDGET_PHASE       10u
#define OCST_BUDGET_L1_WAIT     30u
#define OCST_BUDGET_RUNTIME     15u

/* ---- judgment thresholds -------------------------------------------
 * silence = OCBL's RTC now minus the state area's last_progress.
 * A healthy system progresses (or pets) at least once per second, so
 * a manual warm reset measures < ~4 s of silence (pet lag + OCBL's
 * own boot time) while a real watchdog-caught hang measures at least
 * the runtime budget.  8 s splits the two with margin on both sides. */
#define OCST_SILENCE_FAIL_SEC   8u
/* how long the failure notice stays on screen before the 8042 pulse */
#define OCST_FAIL_DWELL_SEC     4u

/* ---- the state area -----------------------------------------------
 * OCBL initializes every field at boot (and re-inits at handoff);
 * the kernel BLIND-WRITES stage / heartbeat / last_progress / kflags
 * only (it never reads the block back - a write-only contract);
 * OCBL and OCIRE read everything. */
typedef struct ocst_state {
    ocst_u32 magic;           /* OCST_STATE_MAGIC                     */
    ocst_u16 ver;             /* OCST_STATE_VER                      */
    ocst_u16 size;            /* sizeof(ocst_state_t)                */
    ocst_u16 seq;             /* boot attempt sequence, OCBL bumps   */
    ocst_u8  stage;           /* OCST_STAGE_*: last reached          */
    ocst_u8  armed;           /* 1 = monitored payload handed off    */
    ocst_u8  kflags;          /* OCST_KFLAG_* (kernel written)       */
    ocst_u8  rsvd0;           /* 0                                    */
    ocst_u32 heartbeat;       /* runtime tick counter                */
    ocst_u32 last_progress;   /* unix sec of the last kernel write   */
} ocst_state_t;               /* 24 bytes, no implicit padding       */

/* ---- the reserved-area boot record ---------------------------------
 * Written by OCBL at the judgment moment (BOOT_FAILED) or when the
 * user picks the OCIRE menu entry (USER_REQUEST); read by OCIRE
 * after the reboot.  `checksum` is the ones-complement sum of the
 * preceding 24-byte payload viewed as little-endian u32 words - the
 * integrity check that separates a real record from reset garbage. */
typedef struct ocst_record {
    ocst_u32 magic;           /* OCST_RESERVED_MAGIC                  */
    ocst_u16 ver;             /* OCST_RESERVED_VER                    */
    ocst_u16 size;            /* sizeof(ocst_record_t)                */
    ocst_u8  kind;            /* OCST_KIND_*                          */
    ocst_u8  stall;           /* stage where the boot hung            */
    ocst_u16 seq;             /* state seq at judgment time           */
    ocst_u32 ts_unix;         /* judgment time, unix seconds (CMOS)   */
    ocst_u32 silence_sec;     /* measured no-progress seconds         */
    ocst_u8  routed;          /* 1 = OCBL already auto-booted OCIRE   */
    ocst_u8  rsvd0;           /* 0                                    */
    ocst_u16 rsvd1;           /* 0                                    */
    ocst_u32 checksum;        /* see above                            */
} ocst_record_t;

/* ---- inline helpers (shared by all three builds) ------------------ */

/* ones-complement checksum over the record payload (all u32 words
 * before the checksum field); stored complemented so an all-zero or
 * all-0xFF garbage block can never validate. */
static inline ocst_u32 ocst_record_payload_sum(const volatile ocst_record_t* r)
{
    const volatile ocst_u32* w = (const volatile ocst_u32*)r;
    ocst_u32 sum = 0;
    for (unsigned i = 0; i < 6; i++)          /* 6 words = 24 bytes  */
        sum += w[i];
    return sum;
}

static inline ocst_u32 ocst_record_checksum(const volatile ocst_record_t* r)
{
    return ~ocst_record_payload_sum(r);
}

static inline int ocst_record_valid(const volatile ocst_record_t* r)
{
    if (r->magic != OCST_RESERVED_MAGIC) return 0;
    if (r->ver != OCST_RESERVED_VER)      return 0;
    if (r->size != sizeof(ocst_record_t)) return 0;
    if (r->kind != OCST_KIND_BOOT_FAILED && r->kind != OCST_KIND_USER_REQUEST)
        return 0;
    return r->checksum == ocst_record_checksum(r);
}

static inline int ocst_state_valid(const volatile ocst_state_t* s)
{
    return s->magic == OCST_STATE_MAGIC && s->ver == OCST_STATE_VER &&
           s->stage < OCST_STAGE_COUNT;
}

/* stage name for the failure message / reports (shared spelling) */
static inline const char* ocst_stage_name(ocst_u8 stage)
{
    switch (stage) {
    case OCST_STAGE_RESET:          return OCST_STAGE0_NAME;
    case OCST_STAGE_KM_AIN_ENTERED: return "KM_AIN_ENTERED";
    case OCST_STAGE_SCHED_READY:    return "SCHED_READY";
    case OCST_STAGE_DRIVERS_READY:  return "DRIVERS_READY";
    case OCST_STAGE_SERVICES_READY: return "SERVICES_READY";
    case OCST_STAGE_L1_LAUNCHED:    return "L1_LAUNCHED";
    case OCST_STAGE_L1_READY:       return "L1_READY";
    default:                        return "UNKNOWN";
    }
}

/* per-stage "seconds allowed until the NEXT stage" (the watchdog
 * budget the kernel arms when it writes `stage`); stage 0's budget
 * is armed by OCBL before the handoff. */
static inline ocst_u32 ocst_stage_budget(ocst_u8 stage)
{
    switch (stage) {
    case OCST_STAGE_RESET:          return OCST_BUDGET_BOOT;
    case OCST_STAGE_KM_AIN_ENTERED: return OCST_BUDGET_PHASE;
    case OCST_STAGE_SCHED_READY:    return OCST_BUDGET_PHASE;
    case OCST_STAGE_DRIVERS_READY:  return OCST_BUDGET_PHASE;
    case OCST_STAGE_SERVICES_READY: return OCST_BUDGET_PHASE;
    case OCST_STAGE_L1_LAUNCHED:    return OCST_BUDGET_L1_WAIT;
    case OCST_STAGE_L1_READY:       return OCST_BUDGET_RUNTIME;
    default:                        return OCST_BUDGET_BOOT;
    }
}

/* layout guards: all three builds must agree byte for byte */
_Static_assert(sizeof(ocst_u32) == 4, "ocst_u32 must be 4 bytes");
_Static_assert(sizeof(ocst_state_t) == 24, "ocst_state_t layout drifted");
_Static_assert(sizeof(ocst_record_t) == 28, "ocst_record_t layout drifted");

#endif /* OCBL_STATE_H */
