# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - root Makefile (WP-01-tree)
#
# The root Makefile owns no build logic. Its only job is to recurse
# into the component Makefiles: ocos/ (main system), ocbl/ (bootloader),
# ocse/ (install environment), ocire/ (recovery kernel). Every
# component is self-contained; no legacy build script is referenced.

.PHONY: all clean

all:
	$(MAKE) -C ocos all
	$(MAKE) -C ocbl all
	$(MAKE) -C ocse all
	$(MAKE) -C ocire all

clean:
	$(MAKE) -C ocos clean
	$(MAKE) -C ocbl clean
	$(MAKE) -C ocse clean
	$(MAKE) -C ocire clean
