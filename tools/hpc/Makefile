# SPDX-License-Identifier: MIT
# Copyright (C) 2025 Adam Henault <contact@adamhlt.com>
# Copyright (C) 2025 Lab-STICC
CROSS_COMPILE ?= riscv64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall
LDFLAGS ?= -static

hpc: hpc.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f hpc

.PHONY: clean
