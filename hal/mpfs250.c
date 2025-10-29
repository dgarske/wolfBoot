/* mpfs250.c
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

/* Microchip PolarFire SoC MPFS250T HAL for wolfBoot */
/* Supports:
 *   RISC-V 64-bit architecture
 *   External flash operations
 *   UART communication
 *   System initialization
 */

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"

/* TODO: Add PolarFire SoC register definitions */
/* TODO: Add UART register definitions */
/* TODO: Add Flash/SPI register definitions */
/* TODO: Add clock/PLL register definitions */

/* Placeholder functions - to be implemented */
void hal_init(void)
{
    /* TODO: Initialize PolarFire SoC
     * - Configure clocks
     * - Initialize UART
     * - Initialize flash interface
     * - Other hardware initialization
     */
}

void hal_prepare_boot(void)
{
    /* TODO: Prepare system for booting application
     * - Disable interrupts if needed
     * - Cleanup bootloader state
     */
}

void RAMFUNCTION hal_flash_unlock(void)
{
    /* TODO: Unlock flash for writes if required */
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* TODO: Lock flash after writes if required */
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /* TODO: Implement flash write
     * - Write data to flash at address
     * - Handle page boundaries
     * - Return 0 on success, negative on error
     */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    /* TODO: Implement flash erase
     * - Erase flash sectors starting at address
     * - Erase len bytes worth of sectors
     * - Return 0 on success, negative on error
     */
    (void)address;
    (void)len;
    return 0;
}

#ifdef EXT_FLASH
/* External flash support */
void ext_flash_lock(void)
{
    /* TODO: Lock external flash */
}

void ext_flash_unlock(void)
{
    /* TODO: Unlock external flash */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* TODO: Write to external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    /* TODO: Read from external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* TODO: Erase external flash sectors */
    (void)address;
    (void)len;
    return 0;
}
#endif /* EXT_FLASH */

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif

#ifdef DEBUG_UART
void uart_init(void)
{
    /* TODO: Initialize UART for debug output */
}

void uart_write(const char* buf, unsigned int sz)
{
    /* TODO: Write buffer to UART */
    (void)buf;
    (void)sz;
}
#endif /* DEBUG_UART */

