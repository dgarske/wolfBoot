# Makefile
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

-include ../../../.config

WOLFBOOT_PARTITION_BOOT_ADDRESS?=0x08010000
WOLFBOOT_PARTITION_UPDATE_ADDRESS?=0x08110000
WOLFBOOT_PARTITION_SIZE?=0x30000
IMAGE_HEADER_SIZE?=1024

APP_ORIGIN=$(shell printf '0x%08x' $$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) + $(IMAGE_HEADER_SIZE) )))
APP_SIZE=$(shell printf '0x%08x' $$(( $(WOLFBOOT_PARTITION_SIZE) - $(IMAGE_HEADER_SIZE) )))
MARKER_ADDR=$(shell printf '0x%08x' $$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) + $(WOLFBOOT_PARTITION_SIZE) )))

CC=arm-none-eabi-gcc
OBJCOPY?=arm-none-eabi-objcopy

CFLAGS:=-mcpu=cortex-m33 -mthumb -Os -ffreestanding -fdata-sections -ffunction-sections -g -ggdb
CFLAGS+=-DUPDATE_BASE=$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)u
CFLAGS+=-DMARKER_ADDR=$(MARKER_ADDR)u
LDFLAGS:=-nostdlib -T target.ld -Wl,-gc-sections

all: app.bin

target.ld: target.ld.in
	sed -e "s/@FLASH_ORIGIN@/$(APP_ORIGIN)/g" \
	    -e "s/@FLASH_SIZE@/$(APP_SIZE)/g" $< > $@

app.elf: staging_app.c target.ld
	$(CC) $(CFLAGS) staging_app.c $(LDFLAGS) -o $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f app.elf app.bin image.bin image_v*_signed.bin target.ld

.PHONY: all clean
