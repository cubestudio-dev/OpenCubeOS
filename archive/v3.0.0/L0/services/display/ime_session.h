/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - the system IME session interface.
 *
 * ONE engine + ONE session state live in services/display (L0);
 * editors (nano/vi/edit) and the interactive console compose line
 * ("ime -i") both drive THIS engine - nobody embeds a copy.  The
 * L1SVC display ops IME_BEGIN/IME_KEY/IME_END are the ring-3 face
 * (see disp_l1.c); the direct calls below serve the console compose
 * line and the in-kernel tests.
 *
 * The IME_KEY protocol (one text line in the L1SVC out buffer):
 *   "0\\n"        passthrough - not IME business; the editor handles
 *               the key as if no IME existed
 *   "1\\n<line>"  compose-update - redraw the strip with <line>
 *   "2\\n<text>"  commit - insert <text> (UTF-8) at the cursor
 *   "3\\n<text>"  commit-then-passthrough - insert <text>, then the
 *               editor processes the same key as usual
 */
#ifndef OC_IME_SESSION_H
#define OC_IME_SESSION_H

#include "types.h"
#include "pinyin_phrase.h"

void ime_session_reset(void);
int  ime_session_active(void);          /* composing right now? */
const char *ime_session_py(void);       /* the typed letters */
int  ime_session_py_len(void);

/* One key through the engine.  Returns the action (0..3, also
 * encoded as the first line of out). */
int  ime_session_key(int key, char *out, int cap);

/* The rendered compose strip "[pinyin] 1字 2字 ... (page/pages)" -
 * UTF-8, ready to print.  Returns the length. */
int  ime_session_compose_line(char *buf, int cap);

/* Direct page access for the console compose line. */
/* Pooled layout: rows reference the pool, so the direct-page helper
 carries its own per-candidate byte cap (the pool string is at most
  PINYIN_PHRASE_MAX_BYTES-equivalent: 4 hanzi = 12 bytes + NUL). */
#define IME_CAND_MAX_BYTES 16
int  ime_session_page_cands(char out9[][IME_CAND_MAX_BYTES],
                            int *lens, int cap);
int  ime_session_pages(void);
int  ime_session_page(void);

#endif /* OC_IME_SESSION_H */
