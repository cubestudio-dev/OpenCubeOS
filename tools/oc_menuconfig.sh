#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/oc_menuconfig.sh
# Purpose: dependency-free interactive configuration editor for the
#          WP-13 multi-config options (spec 5.7).  Reads .config if
#          present, toggles values, writes .config on save.  The same
#          file `make defconfig` writes and the Makefile -includes.
#
# Keys: <number>  toggle that option
#       s         save .config and exit
#       q         quit WITHOUT saving
#       r         reset to defaults (all y)

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$ROOT/.config"

# option id | description | default
OPTIONS='CONFIG_TUI|Native TUI desktop + tui* commands (9)|y
CONFIG_CHINESE|Chinese renderer + IME + chinese* commands (8; forces TUI=n when off)|y
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

draw() {
    clear 2>/dev/null || true
    echo "Open Cube OS configuration (v1.0.0)  ->  $CFG"
    echo "========================================================="
    n=1
    while IFS='|' read -r id desc def; do
        [ -z "$id" ] && continue
        mark="[y] "; [ "${VAL[$id]}" = "n" ] && mark="[n] "
        note=""
        if [ "$id" = "CONFIG_CHINESE" ] && [ "${VAL[CONFIG_CHINESE]}" = "n" ]; then
            note="  (TUI forced off)"
        fi
        echo "  $n. $mark$id$note"
        echo "       $desc"
        n=$((n+1))
    done <<< "$OPTIONS"
    echo "========================================================="
    echo "  <n> toggle   s save+exit   q quit   r reset   d defaults"
}

draw
while read -rsn1 key; do
    case "$key" in
        [1-5])
            idx=$(( ${key}0 - 1 ))
            id="${IDS[$idx]}"
            [ -z "$id" ] && continue
            if [ "${VAL[$id]}" = "y" ]; then VAL[$id]="n"; else VAL[$id]="y"; fi
            # dependency: CHINESE=n forces TUI=n (display only; the
            # Makefile enforces the real rule at parse time too)
            if [ "$id" = "CONFIG_CHINESE" ] && [ "${VAL[$id]}" = "n" ]; then
                VAL[CONFIG_TUI]="n"
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
