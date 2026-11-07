#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Open Cube OS sandbox toolchain environment (no-root extract layout).
export OC_TOOLS=/home/z/opt/extract
export PATH="$OC_TOOLS/usr/bin:$PATH"
export LD_LIBRARY_PATH="$OC_TOOLS/usr/lib/x86_64-linux-gnu:$OC_TOOLS/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
