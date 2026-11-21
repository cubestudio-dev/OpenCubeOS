#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# WP-runtime-hardening 4.7: CONFIG_TUI is TRI-STATE (y / h / n)
# File: tools/oc_menuconfig.sh
# Purpose: dependency-free interactive configuration editor for the
#          WP-13 multi-config options (spec 5.7).  Reads .config if
#          present, toggles values, writes .config on save.  The same
#          file `make defconfig` writes and the Makefile -includes.
#
# Keys: <number>  toggle that option (CONFIG_TUI cycles y -> h -> n -> y)
#       s         save .config and exit
#       q         quit WITHOUT saving
#       r         reset to defaults (TUI=n, block drivers y)

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$ROOT/.config"

# option id | description | default
# WP-runtime-hardening 4.7: the 2.x CONFIG_CHINESE knob is gone (the
# CJK pipeline is always compiled in since WP-03); CONFIG_TUI replaces
# the old boolean with the tri-state below.
OPTIONS='CONFIG_TUI|TUI plane: y=full (9 tui* cmds) / h=compiled+hidden (direct calls blocked) / n=not built|n
CONFIG_NVME|NVMe block driver|y
CONFIG_AHCI|SATA AHCI block driver|y
CONFIG_VIRTIO_BLK|virtio-blk driver|y'

declare -A VAL
while IFS='|' read -r id desc def; do
    [ -z "$id" ] && continue
    VAL[$id]="$def"
done <<< "$OPTIONS"

if [ -f "$CFG" ]; then
    while IFS='=' read -r id v; do
        case "$id" in
            CONFIG_*) [ -n "${VAL[$id]:-}" ] && VAL[$id]="$v" ;;
        esac
    done < "$CFG"
fi

i=0
declare -a IDS
while IFS='|' read -r id desc def; do
    [ -z "$id" ] && continue
    IDS[i]="$id"; i=$((i+1))
done <<< "$OPTIONS"

# CONFIG_TUI tri-state: one keypress cycles y -> h -> n -> y.
tui_next() {
    case "$1" in
        y) echo "h" ;;
        h) echo "n" ;;
        *) echo "y" ;;
    esac
}

draw() {
    clear 2>/dev/null || true
    echo "Open Cube OS configuration (v1.0.0)  ->  $CFG"
    echo "========================================================="
    n=1
    while IFS='|' read -r id desc def; do
        [ -z "$id" ] && continue
        mark="[${VAL[$id]}] "
        echo "  $n. $mark$id"
        echo "       $desc"
        n=$((n+1))
    done <<< "$OPTIONS"
    echo "========================================================="
    echo "  <n> toggle/cycle   s save+exit   q quit   r reset   d defaults"
}

draw
while read -rsn1 key; do
    case "$key" in
        [1-4])
            # v1.0.0 live-catch: the old `idx=$(( ${key}0 - 1 ))`
            # expanded to 9/19/29/39 - every index was out of range
            # and NO key ever toggled anything.  Plain key-1 it is.
            idx=$(( key - 1 ))
            id="${IDS[$idx]}"
            [ -z "$id" ] && continue
            if [ "$id" = "CONFIG_TUI" ]; then
                VAL[$id]="$(tui_next "${VAL[$id]}")"
            else
                if [ "${VAL[$id]}" = "y" ]; then VAL[$id]="n"; else VAL[$id]="y"; fi
            fi
            ;;
        s)
            {
                echo "# Open Cube OS build configuration (WP-13)"
                echo "# Written by tools/oc_menuconfig.sh (make menuconfig)"
                for oid in "${IDS[@]}"; do
                    echo "$oid=${VAL[$oid]}"
                done
            } > "$CFG"
            echo "saved $CFG"
            exit 0
            ;;
        q) echo "quit without saving"; exit 0 ;;
        r|d)
            while IFS='|' read -r id desc def; do
                [ -z "$id" ] && continue
                VAL[$id]="$def"
            done <<< "$OPTIONS"
            ;;
    esac
    draw
done
