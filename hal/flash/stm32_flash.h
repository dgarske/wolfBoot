/* stm32_flash.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef STM32_FLASH_H
#define STM32_FLASH_H

#include <stdint.h>
#include "hal.h"

/* Platform-specific configuration must be included before stm32_flash.c */
/* The configuration header must define the following macros: */

/* Register Access Macros */
/* STM32_FLASH_SR_READ() - Read status register */
/* STM32_FLASH_CR_READ() - Read control register */
/* STM32_FLASH_CR_WRITE(val) - Write control register */
/* STM32_FLASH_SR_WRITE(val) - Write status register (typically |=) */

/* Bit Definitions */
/* STM32_FLASH_SR_BSY - Busy bit mask */
/* STM32_FLASH_SR_EOP - End of program bit mask */
/* STM32_FLASH_SR_ERROR_MASK - All error bits mask */
/* STM32_FLASH_CR_PG - Program bit mask */
/* STM32_FLASH_CR_LOCK - Lock bit mask */

/* Write Configuration */
/* STM32_FLASH_WRITE_ALIGNMENT - 1, 2, 8, or 16 bytes */
/* STM32_FLASH_ADDRESS_REMAP(addr) - Address remapping function */

/* Unlock/Lock Sequences */
/* STM32_FLASH_UNLOCK_SEQUENCE() - Unlock macro */
/* STM32_FLASH_LOCK_SEQUENCE() - Lock macro */

/* Erase Configuration */
/* STM32_FLASH_ERASE_PAGE_SIZE - Page size in bytes */
/* STM32_FLASH_ERASE_CALC_PAGE(addr) - Calculate page number from address */
/* STM32_FLASH_ERASE_PAGE(page) - Erase page macro */

/* Function Declarations */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len);
int RAMFUNCTION hal_flash_erase(uint32_t address, int len);
void RAMFUNCTION hal_flash_unlock(void);
void RAMFUNCTION hal_flash_lock(void);

#endif /* STM32_FLASH_H */
