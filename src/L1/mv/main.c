/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "mv".
 * Moved verbatim from the ush `mv` builtin: copy argv[1] to argv[2]
 * (O_TRUNC target, BUG-0089 fix) then unlink the source.
 * BUG-36 FIX (R3-H): same-file guard + no half-deleted state.  `mv x x`
 * used to copy the file onto itself (the O_TRUNC open emptied it) and
 * then unlink the source: the file was silently destroyed.  app_main
 * now refuses src/dst pairs that denote the same file BEFORE any file
 * syscall (level 1: raw argument strings equal; level 2: canonical
 * forms equal - cwd anchored, "."/".."/"//" resolved, the lexical
 * rules of the kernel's fspath_resolve_base + fspath_normalize; see
 * cp/main.c for the helper details).  Failure path: the old loop
 * ignored read/write errors and then unlinked the source
 * unconditionally - a failed copy left a short (or empty) destination
 * AND no source.  There is no rename syscall on this kernel (mv is
 * copy+unlink), so the source is now unlinked only after a fully
 * successful copy; any copy or unlink error is reported and exits 1
 * with the source intact. */
#include "_shared/l1_lib.h"

/* ---- BUG-36 FIX (R3-H): same-file guard helpers ---------------------
 * Header-only codebase: every L1 app compiles its own copy and the
 * _shared headers carry no path-normalization helper, so these live
 * here (verbatim copies of the cp/main.c helpers).  Every bail-out
 * passes the RAW input through, which only ever WEAKENS the guard to
 * level 1 - a canonical form can only equal the other side's
 * canonical form or a verbatim spelling of itself, never a different
 * file - so the guard cannot misfire. */

/* Bounded append: keeps d NUL-terminated within cap bytes. */
static void cat_b(char *d, int cap, const char *s) {
    int o = 0;
    while (d[o]) o++;
    for (int i = 0; s[i] && o < cap - 1; i++) d[o++] = s[i];
    d[o] = 0;
}

/* Fallback: copy the raw input through (guard level 1 only). */
static void pass_raw(const char *in, char *out, int cap) {
    int i = 0;
    while (in[i] && i < cap - 1) { out[i] = in[i]; i++; }
    out[i] = 0;
}

/* Lexically canonicalize `in` into out[] (cap bytes): relative paths
 * are anchored at the process cwd (sys_getcwd), then "."/".."/"//"
 * are resolved exactly like the kernel does before an open.  On
 * oddities (no cwd, over-long path, too many components) the raw
 * input is passed through instead. */
static void canon_path(const char *in, char *out, int cap) {
    static char full[528];               /* cwd<=255 + '/' + path<=256 */
    full[0] = 0;
    if (in[0] != '/') {
        if (sys_getcwd(full, (long)sizeof(full) - 1) < 0 || !full[0] ||
            strlen_(full) > 256) { pass_raw(in, out, cap); return; }
        cat_b(full, (int)sizeof(full), "/");
    }
    if (strlen_(in) > 256) { pass_raw(in, out, cap); return; }
    cat_b(full, (int)sizeof(full), in);

    /* Rebuild out[] component-wise; top[] marks where each live
     * component starts so ".." can pop it (pop removes the component
     * AND its leading '/').  Result is always absolute ("/a/b" style),
     * root stays "/". */
    int top[128];
    int ntop = 0;
    int o = 0;
    out[0] = 0;
    const char *p = full;
    while (*p) {
        if (*p == '/') { p++; continue; }              /* collapse "//" */
        const char *s = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - s);
        if (len == 1 && s[0] == '.') continue;                    /* "." */
        if (len == 2 && s[0] == '.' && s[1] == '.') {             /* ".." */
            if (ntop > 0) { o = top[--ntop] - 1; out[o] = 0; }
            continue;             /* ".." at root: root's parent = root */
        }
        if (len > 63) len = 63;        /* kernel VFS_NAME_LEN-1 comp cap */
        if (ntop >= (int)(sizeof(top) / sizeof(top[0])) ||
            o + len + 2 > cap - 1) { pass_raw(in, out, cap); return; }
        top[ntop++] = o + 1;
        out[o++] = '/';
        for (int k = 0; k < len; k++) out[o++] = s[k];
        out[o] = 0;
    }
    if (o == 0) { out[0] = '/'; out[1] = 0; }
}

/* 1 if the two path arguments denote the same file: raw strings equal,
 * or canonical forms equal.  Conservative on purpose. */
static int same_file(const char *a, const char *b) {
    if (streq_(a, b)) return 1;
    static char ca[512], cb[512];
    canon_path(a, ca, (int)sizeof(ca));
    canon_path(b, cb, (int)sizeof(cb));
    return streq_(ca, cb);
}

int app_main(int argc, char **argv) {
    /* ---------- mv <src> <dst> ---------- */
    if (argc < 3) {
        puts_(tr_("usage: mv <src> <dst>\n"));
        return 0;
    }
    /* BUG-36 FIX (R3-H): refuse src == dst before ANY file syscall -
     * the old code truncated the destination first and destroyed the
     * source on `mv x x`. */
    if (same_file(argv[1], argv[2])) {
        puts_(tr_("mv: source and destination are the same file\n"));
        return 1;
    }
    long sfd = sys_open(argv[1], 1);
    if (sfd < 0) {
        puts_(tr_("mv: cannot open '"));
        puts_(argv[1]);
        puts_("'\n");
        return 0;
    }
    /* BUG-0089 FIX (A16-2): O_TRUNC on the move target - an existing
     * longer destination kept its stale tail after a shorter move. */
    long dfd = sys_open(argv[2], 22);
    if (dfd < 0) {
        puts_(tr_("mv: cannot open '"));
        puts_(argv[2]);
        puts_("'\n");
        sys_close(sfd);
        return 0;
    }
    static char mvbuf[4096];
    long n;
    /* BUG-36 FIX (R3-H): track the copy.  A short/failed write must
     * abort the copy AND keep the source; the old code unlinked the
     * source no matter what (half-deleted state on copy failure). */
    int copy_ok = 1;
    while ((n = sys_read(sfd, mvbuf, 4096)) > 0) {
        if (sys_write(dfd, mvbuf, n) != n) { copy_ok = 0; break; }
    }
    if (n < 0) copy_ok = 0;              /* read error, not clean EOF */
    sys_close(sfd);
    sys_close(dfd);
    if (!copy_ok) {
        puts_(tr_("mv: copy failed, source kept '"));
        puts_(argv[1]);
        puts_("'\n");
        return 1;
    }
    if (sys_unlink(argv[1]) != 0) {
        /* Copy succeeded but the source could not be removed: state is
         * complete on both ends (copy semantics), so just report it. */
        puts_(tr_("mv: cannot unlink '"));
        puts_(argv[1]);
        puts_("'\n");
        return 1;
    }
    return 0;
}
