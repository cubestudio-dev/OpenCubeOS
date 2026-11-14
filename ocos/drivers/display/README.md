# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#
# ocos/drivers/display/ - display-class hardware drivers.
#
# NAK reserves this class folder for display adapter drivers (GPU /
# display controller bring-up).  The current display pipeline is the
# firmware-framebuffer console in the kernel core
# (ocos/kernel/core_console_fb.c) - no discrete display-class driver
# exists yet, so this folder holds only this marker.  The first
# display driver lands here as ocos/drivers/display/drv_display_<chip>.c
# with an OC_DRIVER_DECL("display", ...) line - nothing else changes.
