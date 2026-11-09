#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/oc_cc.sh
# Purpose: content-addressed compile cache wrapper (WP-13 spec 5.6).
#
# The kernel build calls this instead of gcc directly:
#     $(OC_CC) $(CFLAGS) -c foo.c -o build/foo.o
#
# Cache key = SHA-256 over:
#     1. the exact compiler identity (path + version string),
#     2. the full argument list (flags, -D values, include paths),
#     3. the SHA-256 of the source file,
#     4. the SHA-256 of every header the source depends on
#        (from the compiler's own -MM output - the same dependency
#        data make's .d files use, so a touched-but-unchanged header
#        still hits the cache, and an unchanged-mtime-but-changed
#        header still misses it: the cache is content-based).
#
# On a hit the .o AND the .d are restored; on a miss the real compile
# runs and both artifacts are stored.  Hits produce byte-identical
# objects (same compiler, same inputs), so reproducible-check passes
# with the cache enabled.
#
# Environment:
#     OC_CACHE_DIR   cache root (default: ~/.cache/opencube-cc)
#     OC_CACHE_OFF   set to any value to bypass the cache entirely
#
# Cache statistics (tools/build.sh cache-stats / make cache-stats) read
# the counters this wrapper appends to $OC_CACHE_DIR/STATS.

set -u

if [ -n "${OC_CACHE_OFF:-}" ]; then
    # pass through untouched
    exec "$@"
fi

CACHE_DIR="${OC_CACHE_DIR:-$HOME/.cache/opencube-cc}"
mkdir -p "$CACHE_DIR"

# ---- split arguments: everything up to -c is flags; -o out; last src ----
compiler="$1"; shift
args=("$@")
src=""; out=""; flags=()
i=0
while [ $i -lt ${#args[@]} ]; do
    a="${args[$i]}"
    case "$a" in
        -o)  out="${args[$((i+1))]}"; i=$((i+2)); continue ;;
        -*)  flags+=("$a") ;;
        *)   src="$a" ;;          # the only free argument is the source
    esac
    i=$((i+1))
done
if [ -z "$src" ] || [ -z "$out" ]; then
    echo "oc_cc.sh: cannot parse compile arguments" >&2
    exit 2
fi

hash_file() { sha256sum "$1" | cut -d' ' -f1; }

# ---- key material -----------------------------------------------------
comp_id="$(command -v "$compiler")#$("$compiler" --version 2>/dev/null | head -1)"
src_hash="$(hash_file "$src")"

# dependency list from the compiler itself (-MM: user headers only, which
# is exactly right for this freestanding tree: every header comes from -I).
# BUG FOUND LIVE (WP-13): the Makefile CFLAGS carry -MMD -MP, and gcc
# given -MMD -MP together with -MM -MG emits NO stdout dependency list,
# which silently emptied dep_list and dropped every header from the key
# (a header-only change then hit a stale object: a CONFIG_TUI=n build
# linked the y-config objects).  Strip the dep-file flags for the probe.
mm_flags=()
for f in "${flags[@]}"; do
    case "$f" in -MMD|-MP|-MF|-MT|-MQ) ;; *) mm_flags+=("$f") ;; esac
done
dep_list="$("$compiler" "${mm_flags[@]}" -MM -MG "$src" 2>/dev/null \
           | tr -d '\\' | tr ' ' '\n' | sed '/:$/d; /^$/d' | sort -u)"
dep_hashes=""
for d in $dep_list; do
    [ -f "$d" ] && dep_hashes+="$d $(hash_file "$d")"$'\n'
done

key="$( { echo "$comp_id"; printf '%s\n' "${flags[@]}"; echo "SRC $src_hash"; echo "$dep_hashes"; } \
        | sha256sum | cut -d' ' -f1)"

entry="$CACHE_DIR/$key"

if [ -f "$entry.o" ] && [ -f "$entry.d" ]; then
    cp "$entry.o" "$out"
    # regenerate the .d next to the object (the stored .d has the
    # cache-internal name; rewrite the target name to the real output)
    sed "s|^[^:]*:|$out:|" "$entry.d" > "${out%.o}.d" 2>/dev/null || true
    echo "HIT" >> "$CACHE_DIR/STATS"
    exit 0
fi

# ---- miss: real compile -------------------------------------------------
"$compiler" "${flags[@]}" -c "$src" -o "$out"
rc=$?
if [ $rc -ne 0 ]; then exit $rc; fi

cp "$out" "$entry.o" 2>/dev/null || true
if [ -f "${out%.o}.d" ]; then
    cp "${out%.o}.d" "$entry.d" 2>/dev/null || true
fi
echo "MISS" >> "$CACHE_DIR/STATS"
exit 0
