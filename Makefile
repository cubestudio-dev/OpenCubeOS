# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - root Makefile (WP-01-tree)
#
# The root Makefile owns no per-component build logic. Its only jobs
# are to recurse into the component Makefiles - ocos/ (main system),
# ocbl/ (bootloader), ocse/ (install environment), ocire/ (recovery
# kernel) - and to produce the source archive shipped as a release
# asset.
#
# Targets:
#   all     - build every component (default)
#   clean   - remove every build product (component trees + build/)
#   srczip  - build/opencubenak-WP-02-ocbl-src.zip from HEAD
#
# The source zip is produced with `git archive`, so it contains
# exactly the committed tree: ocos/ ocbl/ ocse/ ocire/ and the root
# files. archive/ is excluded through the export-ignore attribute in
# .gitattributes, and nothing under any build/ directory can ever be
# inside because build products are untracked. Run `srczip` after
# committing the tree you want to archive.

BUILD_DIR := build
SRC_ZIP   := $(BUILD_DIR)/opencubenak-WP-02-ocbl-src.zip
ZIP_PREFIX := opencubenak-WP-02-ocbl-src/

.PHONY: all clean srczip

all:
	$(MAKE) -C ocos all
	$(MAKE) -C ocbl all
	$(MAKE) -C ocse all
	$(MAKE) -C ocire all

srczip:
	@mkdir -p $(BUILD_DIR)
	git archive --format=zip --prefix=$(ZIP_PREFIX) -o $(SRC_ZIP) HEAD
	@echo "wrote $(SRC_ZIP)"

clean:
	$(MAKE) -C ocos clean
	$(MAKE) -C ocbl clean
	$(MAKE) -C ocse clean
	$(MAKE) -C ocire clean
	rm -rf $(BUILD_DIR)
