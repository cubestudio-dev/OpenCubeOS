# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - ocos main system Makefile (NAK-01-tree)
#
# Scans kernel/, drivers/ and services/ for every .c file, compiles
# under the zero-warning gate (-Wall -Wextra -Werror), and links the
# minimal freestanding kernel ELF (ocos.elf). NAK-01-tree scope: the
# kernel stub only; the ELF must link, not boot.
#
# Products land in build/ and ocos.elf; `make clean` removes both.
# No product is ever committed to Git.

CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pie
LDFLAGS := -nostdlib -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-e,kmain

SRCS := $(wildcard kernel/*.c drivers/*.c services/*.c)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))

ELF := ocos.elf

.PHONY: all clean

all: $(ELF)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

clean:
	@rm -rf build
	@rm -f $(ELF)
