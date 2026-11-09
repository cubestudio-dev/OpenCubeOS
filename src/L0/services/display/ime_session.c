/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - the system IME session engine (body).
 *
 * See ime_session.h for the contract and the key protocol.  Candidate
 * assembly order (long words first, GB2312 frequency inside):
 *   1. whole-string PHRASE matches (all homophones) - "zhongguo" ->
 *      中国
 *   2. ONE smart-compose candidate: a 2-3 segment EXACT-syllable
 *      split, each segment's first (most frequent) char joined -
 *      "wenjian" -> 文件 even when not in the phrase list
 *   3. single characters for the longest matching syllable (the
 *      v2.2.0 ime_match semantics, the ime-test contract)
 * Nine per page; PageUp/PageDown (or - / =) page; digits 1-9 pick;
 * space picks the first; Enter commits the raw letters; Esc clears;
 * any other printable commits the first candidate then passes the
 * key through (action 3) so punctuation flows naturally. */
#include "types.h"
#include "lib_string.h"
#include "ime_engine.h"
#include "ime_session.h"
#include "encoding.h"
#include "driver_input_keyboard.h"

/* ---- session state (ONE console, ONE session) ---------------------- */
#define IME_MAX_PY     14
#define IME_MAX_CANDS  64
#define IME_PAGE_SIZE  9

static char s_py[IME_MAX_PY + 1];
static int  s_py_len;
static char s_cand[IME_MAX_CANDS][IME_CAND_MAX_BYTES];
static int  s_cand_len[IME_MAX_CANDS];
static int  s_ncand;
static int  s_page;

void ime_session_reset(void) {
    s_py_len = 0;
    s_py[0] = 0;
    s_ncand = 0;
    s_page = 0;
}

int ime_session_active(void) { return s_py_len > 0; }
const char *ime_session_py(void) { return s_py; }
int ime_session_py_len(void) { return s_py_len; }

/* ---- small local helpers -------------------------------------------- */
static int cand_push(const char *utf8, int len) {
    if (s_ncand >= IME_MAX_CANDS || len <= 0 ||
        len > IME_CAND_MAX_BYTES - 1)
        return 0;
    for (int i = 0; i < s_ncand; i++)
        if (s_cand_len[i] == len &&
            memcmp(s_cand[i], utf8, (usize)len) == 0)
            return 0;
    memcpy(s_cand[s_ncand], utf8, (usize)len);
    s_cand[s_ncand][len] = 0;
    s_cand_len[s_ncand] = len;
    s_ncand++;
    return 1;
}

static int exact_syllable(const char *s, int len) {
    /* an EXACT (not prefix) syllable match, or -1 */
    char t[8];
    int m = ime_match(s, len);
    if (m < 0) return -1;
    ime_syllable_str(m, t, sizeof(t));
    if ((int)strlen(t) != len || strncmp(t, s, (usize)len) != 0)
        return -1;
    return m;
}

/* ---- candidate assembly -------------------------------------------- */

/* 1. whole-string phrase matches (all homophones, library order). */
static void cand_phrases(const char *py, int len) {
    int lo = 0, hi = (int)PINYIN_PHRASE_COUNT - 1, first = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *fp = phrase_py(&PINYIN_PHRASES[mid]);
        int c = strncmp(fp, py, (usize)len);
        if (c == 0) {
            /* tie on the first len bytes: fp[len] decides - 0 means
             * EXACT, > 0 means fp is LONGER than py (fp > py, search
             * right).  (The old code fell into the hi=mid-1 branch
             * here and could miss every entry to the right - the
             * host probe caught nihao -> 你号 instead of 你好.) */
            c = (unsigned char)fp[len];
        }
        if (c == 0) {
            first = mid;
            hi = mid - 1;               /* keep hunting the FIRST */
        } else if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (first < 0) return;
    for (int i = first; i < (int)PINYIN_PHRASE_COUNT; i++) {
        const char *fp = phrase_py(&PINYIN_PHRASES[i]);
        if (strncmp(fp, py, (usize)len) != 0 || fp[len] != 0)
            break;
        cand_push(phrase_utf8(&PINYIN_PHRASES[i]),
                  PINYIN_PHRASES[i].bytes);
    }
}

/* 2. smart compose: the FIRST 2-3 segment exact-syllable split, each
 * segment's first (frequency-first) candidate char joined.  Greedy
 * longest-first over segment 1, then segment 2 (then 3). */
static void cand_compose(const char *py, int len) {
    u32 part_cps[3];
    int  parts = 0;
    char seg[8];
    /* The l1 upper bound is min(6, len-1): segment 1 must leave at
     * least one character for segment 2.  (A plain `l1 = 6` start
     * NEVER enters the loop when len <= 6 - the for condition is
     * checked before the body and the decrement only runs after it;
     * caught live by the ime-test S2 compose check.) */
    int l1_max = (len - 1 < 6) ? len - 1 : 6;
    for (int l1 = l1_max; l1 >= 1; l1--) {
        for (int a = 0; a < l1; a++) seg[a] = py[a];
        seg[l1] = 0;
        int s1 = exact_syllable(seg, l1);
        if (s1 < 0) continue;
        for (int l2 = 6; l2 >= 1; l2--) {
            int rest = len - l1 - l2;
            if (rest < 0) continue;
            if (rest != 0 && (rest < 1 || rest > 6)) continue;
            for (int b = 0; b < l2; b++) seg[b] = py[l1 + b];
            seg[l2] = 0;
            int s2 = exact_syllable(seg, l2);
            if (s2 < 0) continue;
            if (rest == 0) {
                u32 c1[1], c2[1];
                if (ime_candidates(s1, c1, 1) == 1 &&
                    ime_candidates(s2, c2, 1) == 1) {
                    part_cps[0] = c1[0];
                    part_cps[1] = c2[0];
                    parts = 2;
                    goto emit;
                }
                continue;
            }
            for (int c = 0; c < rest; c++) seg[c] = py[l1 + l2 + c];
            seg[rest] = 0;
            int s3 = exact_syllable(seg, rest);
            if (s3 < 0) continue;
            {
                u32 c1[1], c2[1], c3[1];
                if (ime_candidates(s1, c1, 1) == 1 &&
                    ime_candidates(s2, c2, 1) == 1 &&
                    ime_candidates(s3, c3, 1) == 1) {
                    part_cps[0] = c1[0];
                    part_cps[1] = c2[0];
                    part_cps[2] = c3[0];
                    parts = 3;
                    goto emit;
                }
            }
        }
    }
    return;
emit:;
    char buf[IME_CAND_MAX_BYTES];
    int n = 0;
    for (int i = 0; i < parts; i++) {
        u8 enc[4];
        u32 produced = 0;
        if (encoding_utf8_encode(part_cps[i], enc, 4, &produced) != 0 ||
            n + (int)produced > IME_CAND_MAX_BYTES - 1)
            return;
        for (u32 b = 0; b < produced; b++) buf[n++] = (char)enc[b];
    }
    cand_push(buf, n);
}

/* 3. single characters for the longest matching syllable (the v2.2.0
 * ime_match semantics: exact first, then the first prefix syllable). */
static void cand_singles(const char *py, int len) {
    int syl = ime_match(py, len);
    if (syl < 0) return;
    u32 cps[18];
    int n = ime_candidates(syl, cps, 18);
    for (int i = 0; i < n; i++) {
        u8 enc[4];
        u32 produced = 0;
        if (encoding_utf8_encode(cps[i], enc, 4, &produced) != 0) continue;
        char buf[5];
        for (u32 b = 0; b < produced; b++) buf[b] = (char)enc[b];
        cand_push(buf, (int)produced);
    }
}

static void recalc(void) {
    s_ncand = 0;
    s_page = 0;
    if (s_py_len == 0) return;
    /* Priority order (live-caught by the editor battery: the smart
     * compose 'ha'+'o' = 哈哦 was outranking the real character 好):
     *   1. phrase-library words (true vocabulary - always first)
     *   2. single characters for the matching syllable (the user's
     *      most likely pick when no word matches)
     *   3. the smart-compose JOIN last (it is a guess - useful when
     *      nothing else covers the whole string, never ahead of the
     *      real characters) */
    cand_phrases(s_py, s_py_len);
    cand_singles(s_py, s_py_len);
    cand_compose(s_py, s_py_len);
}

/* ---- compose strip rendering ----------------------------------------
 * "[pinyin] 1字 2字 ... (page/pages)" - hand-built (no printf in
 * freestanding L0); every candidate is UTF-8, the digits/brackets
 * ASCII. */
static int append_str(char *buf, int cap, int n, const char *s, int len) {
    for (int i = 0; i < len && n < cap - 1; i++) buf[n++] = s[i];
    return n;
}
static int append_u32(char *buf, int cap, int n, u32 v) {
    char t[12];
    int m = 0;
    if (v == 0) t[m++] = '0';
    while (v > 0 && m < 11) { t[m++] = (char)('0' + v % 10); v /= 10; }
    char r[12];
    for (int i = 0; i < m; i++) r[i] = t[m - 1 - i];
    return append_str(buf, cap, n, r, m);
}

int ime_session_compose_line(char *buf, int cap) {
    if (cap < 4) { if (cap > 0) buf[0] = 0; return 0; }
    if (s_py_len == 0) { buf[0] = 0; return 0; }
    int n = 0;
    buf[n++] = '[';
    n = append_str(buf, cap, n, s_py, s_py_len);
    buf[n++] = ']';
    buf[n++] = ' ';
    int pages = (s_ncand + IME_PAGE_SIZE - 1) / IME_PAGE_SIZE;
    int base = s_page * IME_PAGE_SIZE;
    int shown = 0;
    for (int i = base; i < s_ncand && i < base + IME_PAGE_SIZE; i++) {
        if (shown > 0) buf[n++] = ' ';
        n = append_u32(buf, cap, n, (u32)(i - base + 1));
        n = append_str(buf, cap, n, s_cand[i], s_cand_len[i]);
        shown++;
    }
    if (pages > 1) {
        buf[n++] = ' ';
        buf[n++] = '(';
        n = append_u32(buf, cap, n, (u32)(s_page + 1));
        buf[n++] = '/';
        n = append_u32(buf, cap, n, (u32)pages);
        buf[n++] = ')';
    }
    buf[n] = 0;
    return n;
}

/* ---- key handling ---------------------------------------------------- */
static int emit_update(char *out, int cap) {
    out[0] = '1'; out[1] = '\n'; out[2] = 0;
    char line[160];
    int n = ime_session_compose_line(line, sizeof(line));
    int room = cap - 3;
    if (n > room) n = room;
    if (n > 0) memcpy(out + 2, line, (usize)n);
    out[2 + (n > 0 ? n : 0)] = 0;
    return 1;
}

/* The documented protocol line "A\ntext" for actions 2 and 3 (the
 * commit paths) - emit_update already carries the prefix for action
 * 1; every path through ime_session_key returns the SAME shape. */
static int emit_commit(int action, const char *text, char *out, int cap) {
    int n = 0;
    while (text[n]) n++;
    if (n > cap - 4) n = cap - 4;
    out[0] = (char)('0' + action);
    out[1] = '\n';
    if (n > 0) memcpy(out + 2, text, (usize)n);
    out[2 + n] = 0;
    return action;
}

static int commit_first(char *out, int cap) {
    if (s_ncand > 0) {
        char keep[IME_CAND_MAX_BYTES];
        int len = s_cand_len[0];
        for (int i = 0; i < len && i < (int)sizeof(keep) - 1; i++)
            keep[i] = s_cand[0][i];
        keep[len < (int)sizeof(keep) - 1 ? len : (int)sizeof(keep) - 1] = 0;
        ime_session_reset();
        return emit_commit(2, keep, out, cap);
    }
    char keep[IME_MAX_PY + 1];
    for (int i = 0; i < s_py_len; i++) keep[i] = s_py[i];
    keep[s_py_len] = 0;
    ime_session_reset();
    return emit_commit(2, keep, out, cap);
}

int ime_session_key(int key, char *out, int cap) {
    if (!out || cap < 8) return 0;
    out[0] = 0;

    if (s_py_len > 0) {
        /* ---- composing -------------------------------------------- */
        if (key >= 'a' && key <= 'z') {
            if (s_py_len < IME_MAX_PY) {
                s_py[s_py_len++] = (char)key;
                s_py[s_py_len] = 0;
                recalc();
            }
            return emit_update(out, cap);
        }
        if (key >= '1' && key <= '9') {
            int pick = s_page * IME_PAGE_SIZE + (key - '1');
            if (pick < s_ncand) {
                char keep[IME_CAND_MAX_BYTES];
                int len = s_cand_len[pick] < (int)sizeof(keep) - 1
                              ? s_cand_len[pick] : (int)sizeof(keep) - 1;
                for (int i = 0; i < len; i++) keep[i] = s_cand[pick][i];
                keep[len] = 0;
                ime_session_reset();
                return emit_commit(2, keep, out, cap);
            }
            return emit_update(out, cap);   /* beyond the page: ignore */
        }
        if (key == ' ')
            return commit_first(out, cap);
        if (key == OC_KEY_BACKSPACE || key == 0x08 || key == 0x7F) {
            s_py[--s_py_len] = 0;
            recalc();
            return emit_update(out, cap);   /* empty strip = clear it */
        }
        if (key == OC_KEY_PGUP || key == '-' || key == '_' ||
            key == OC_KEY_PGDN || key == '=' || key == '+') {
            int pages = (s_ncand + IME_PAGE_SIZE - 1) / IME_PAGE_SIZE;
            if (pages > 1) {
                if (key == OC_KEY_PGUP || key == '-' || key == '_')
                    s_page = (s_page > 0) ? s_page - 1 : 0;
                else
                    s_page = (s_page < pages - 1) ? s_page + 1 : pages - 1;
            }
            return emit_update(out, cap);
        }
        if (key == 0x1B || key == OC_KEY_ESC_KEY) {
            ime_session_reset();
            return emit_update(out, cap);
        }
        if (key == '\n' || key == '\r') {
            /* Enter commits the RAW letters (the classic behavior) */
            char keep[IME_MAX_PY + 1];
            for (int i = 0; i < s_py_len; i++) keep[i] = s_py[i];
            keep[s_py_len] = 0;
            ime_session_reset();
            return emit_commit(2, keep, out, cap);
        }
        if (key >= 0x20 && key < 0x7F) {
            /* other printable: commit the first candidate, then the
             * editor processes this key as usual (punctuation in the
             * middle of typing stays natural). */
            (void)commit_first(out, cap);
            return 3;
        }
        if (key >= 0x80 && key <= 0x9B) {
            /* navigation keys mid-compose: commit first, passthrough */
            (void)commit_first(out, cap);
            return 3;
        }
        return emit_update(out, cap);
    }

    /* ---- idle: only letters can OPEN the compose ------------------- */
    if (key >= 'a' && key <= 'z') {
        s_py[s_py_len++] = (char)key;
        s_py[s_py_len] = 0;
        recalc();
        return emit_update(out, cap);
    }
    out[0] = '0'; out[1] = '\n'; out[2] = 0;
    return 0;
}

/* Direct page access for the console compose line (disp_cmds_cn.c). */
int ime_session_page_cands(char out9[][IME_CAND_MAX_BYTES],
                           int *lens, int cap) {
    int base = s_page * IME_PAGE_SIZE;
    int n = 0;
    for (int i = base; i < s_ncand && i < base + IME_PAGE_SIZE && n < cap;
         i++) {
        memcpy(out9[n], s_cand[i], (usize)s_cand_len[i]);
        out9[n][s_cand_len[i]] = 0;
        lens[n] = s_cand_len[i];
        n++;
    }
    return n;
}

int ime_session_pages(void) {
    return (s_ncand + IME_PAGE_SIZE - 1) / IME_PAGE_SIZE;
}

int ime_session_page(void) { return s_page; }
