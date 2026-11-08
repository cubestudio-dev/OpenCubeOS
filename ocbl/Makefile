# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - ocbl Makefile (NAK-01-tree)
#
# Placeholder build: compiles src/ through the same zero-warning gate
# as the kernel, proving the component builds, without implementing
# any bootloader functionality (that arrives in NAK-02).

CC := gcc

BUILD_DIR := build

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -nostdlib -fno-builtin \
          -fno-pie -fno-pic -fno-stack-protector \
          -MMD -MP

# filesystem scan, no hand-written file lists
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
SRCS := $(call rwildcard,src/,*.c)
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(OBJS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJS:.o=.d)

.DELETE_ON_ERROR:
