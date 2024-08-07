#!/bin/bash

set -euxo pipefail

TARGET_H_TEMPLATE=../../../include/target.h.in
TARGET_H_FILE=../../../include/target.h

WOLFBOOT_SECTOR_SIZE=0x4000
WOLFBOOT_PARTITION_SIZE=0x180000
WOLFBOOT_PARTITION_BOOT_ADDRESS=0xA0300000
WOLFBOOT_PARTITION_UPDATE_ADDRESS=0xA0480000
WOLFBOOT_PARTITION_SWAP_ADDRESS=0xA0020000
WOLFBOOT_DTS_BOOT_ADDRESS=
WOLFBOOT_DTS_UPDATE_ADDRESS=
WOLFBOOT_LOAD_ADDRESS=
WOLFBOOT_LOAD_DTS_ADDRESS=

cat $TARGET_H_TEMPLATE | \
	sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$WOLFBOOT_PARTITION_SIZE/g" | \
	sed -e "s/@WOLFBOOT_SECTOR_SIZE@/$WOLFBOOT_SECTOR_SIZE/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$WOLFBOOT_PARTITION_BOOT_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$WOLFBOOT_PARTITION_UPDATE_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$WOLFBOOT_PARTITION_SWAP_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_DTS_BOOT_ADDRESS@/$WOLFBOOT_DTS_BOOT_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_DTS_UPDATE_ADDRESS@/$WOLFBOOT_DTS_UPDATE_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$WOLFBOOT_LOAD_ADDRESS/g" | \
	sed -e "s/@WOLFBOOT_LOAD_DTS_ADDRESS@/$WOLFBOOT_LOAD_DTS_ADDRESS/g" \
		> $TARGET_H_FILE

