/* versal.c
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
 *
 * AMD Versal ACAP HAL implementation for wolfBoot
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 *
 * Features:
 *   - UART driver (Cadence UART)
 *   - ARM Generic Timer
 *   - Flash stubs (OSPI/SD to be implemented)
 */

#ifdef TARGET_versal

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hal/versal.h"
#include "image.h"
#include "printf.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot versal HAL: wrong architecture. Please compile with ARCH=AARCH64."
#endif


/* ============================================================================
 * UART Driver
 * ============================================================================
 * Cadence UART controller (same IP as ZynqMP)
 * Assumes PLM has already configured clocks and pinmux
 */

#ifdef DEBUG_UART

/**
 * Calculate baud rate divisors
 * Baud rate = ref_clk / (CD * (BDIV + 1))
 * where CD is the clock divisor and BDIV is the baud divider
 */
static void uart_calc_baud(uint32_t ref_clk, uint32_t baud,
                           uint32_t *cd, uint32_t *bdiv)
{
    uint32_t calc_baud;
    uint32_t best_error = 0xFFFFFFFF;
    uint32_t best_cd = 1;
    uint32_t best_bdiv = 4;
    uint32_t test_cd, test_bdiv;
    uint32_t error;

    /* Try different divisor combinations */
    for (test_bdiv = 4; test_bdiv < 255; test_bdiv++) {
        test_cd = ref_clk / (baud * (test_bdiv + 1));
        if (test_cd < 1 || test_cd > 65535)
            continue;

        calc_baud = ref_clk / (test_cd * (test_bdiv + 1));
        if (calc_baud > baud)
            error = calc_baud - baud;
        else
            error = baud - calc_baud;

        if (error < best_error) {
            best_error = error;
            best_cd = test_cd;
            best_bdiv = test_bdiv;
        }

        if (error == 0)
            break;
    }

    *cd = best_cd;
    *bdiv = best_bdiv;
}

void uart_init(void)
{
    uint32_t cd, bdiv;

    /* Disable TX and RX */
    UART_CR = UART_CR_TX_DIS | UART_CR_RX_DIS;

    /* Reset TX and RX paths */
    UART_CR = UART_CR_TXRST | UART_CR_RXRST;

    /* Calculate and set baud rate */
    uart_calc_baud(UART_CLK_REF, DEBUG_UART_BAUD, &cd, &bdiv);
    UART_BAUDGEN = cd;
    UART_BAUDDIV = bdiv;

    /* Configure: 8N1 (8 data bits, no parity, 1 stop bit) */
    UART_MR = UART_MR_CHMODE_NORM | UART_MR_NBSTOP_1 |
              UART_MR_PAR_NONE | UART_MR_CHRL_8;

    /* Disable all interrupts */
    UART_IDR = UART_ISR_MASK;

    /* Clear any pending interrupts */
    (void)UART_ISR;

    /* Set RX/TX FIFO trigger levels */
    UART_RXWM = 1;
    UART_TXWM = 1;

    /* Enable TX and RX */
    UART_CR = UART_CR_TX_EN | UART_CR_RX_EN;
}

static void uart_tx(uint8_t c)
{
    /* Wait for TX FIFO to have space */
    while (UART_SR & UART_SR_TXFULL)
        ;

    /* Write character to TX FIFO */
    UART_FIFO = c;
}

void uart_write(const char *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            uart_tx('\r');
        }
        uart_tx((uint8_t)buf[i]);
    }
    /* Wait for transmit to complete */
    while (!(UART_SR & UART_SR_TXEMPTY))
        ;
}

#else
#define uart_init() do {} while(0)
#endif /* DEBUG_UART */


/* ============================================================================
 * Timer Functions (ARM Generic Timer)
 * ============================================================================
 */

/**
 * Get current timer count (physical counter)
 */
static inline uint64_t timer_get_count(void)
{
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    return cntpct;
}

/**
 * Get timer frequency
 */
static inline uint64_t timer_get_freq(void)
{
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    return cntfrq;
}

/**
 * Get current time in milliseconds
 */
uint64_t hal_timer_ms(void)
{
    uint64_t cntpct = timer_get_count();
    uint64_t cntfrq = timer_get_freq();

    if (cntfrq == 0)
        cntfrq = TIMER_CLK_FREQ;

    /* Convert to milliseconds: (count * 1000) / freq */
    return (cntpct * 1000ULL) / cntfrq;
}

/**
 * Delay for specified number of microseconds
 */
void hal_delay_us(uint32_t us)
{
    uint64_t cntfrq = timer_get_freq();
    uint64_t start, target;

    if (cntfrq == 0)
        cntfrq = TIMER_CLK_FREQ;

    start = timer_get_count();
    target = start + ((uint64_t)us * cntfrq) / 1000000ULL;

    while (timer_get_count() < target)
        ;
}


/* ============================================================================
 * HAL Public Interface
 * ============================================================================
 */

void hal_init(void)
{
    const char *banner = "\n"
        "========================================\n"
        "wolfBoot Secure Boot - AMD Versal\n"
        "========================================\n";

#ifdef DEBUG_UART
    uart_init();
#endif

    wolfBoot_printf("%s", banner);
    wolfBoot_printf("Current EL: %d\n", current_el());
    wolfBoot_printf("Timer Freq: %lu Hz\n", (unsigned long)timer_get_freq());

    /* TODO: Initialize flash controller (OSPI/SD) */
}

void hal_prepare_boot(void)
{
    /* Flush any pending UART output */
#ifdef DEBUG_UART
    while (!(UART_SR & UART_SR_TXEMPTY))
        ;
#endif

    /* Memory barriers before jumping to application */
    dsb();
    isb();
}

#ifdef MMU
/**
 * Get the Device Tree address for the boot partition
 * Returns the DTS load address in RAM
 */
void* hal_get_dts_address(void)
{
#ifdef WOLFBOOT_LOAD_DTS_ADDRESS
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
#else
    return NULL;
#endif
}

/**
 * Get the update Device Tree address
 */
void* hal_get_dts_update_address(void)
{
#ifdef WOLFBOOT_DTS_UPDATE_ADDRESS
    return (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
#else
    return NULL;
#endif
}
#endif /* MMU */


/* ============================================================================
 * Flash Functions (STUBS)
 * ============================================================================
 * These are placeholder implementations.
 * Real implementation will depend on boot media:
 *   - OSPI flash (VERSAL_OSPI_BASE)
 *   - SD/eMMC via SDHCI (VERSAL_SD0_BASE / VERSAL_SD1_BASE)
 */

void RAMFUNCTION hal_flash_unlock(void)
{
    /* Stub - no-op for now */
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* Stub - no-op for now */
}

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    /* Stub - flash write not implemented */
    wolfBoot_printf("hal_flash_write: STUB (addr=0x%lx, len=%d)\n",
                    (unsigned long)address, len);
    return -1;
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;

    /* Stub - flash erase not implemented */
    wolfBoot_printf("hal_flash_erase: STUB (addr=0x%lx, len=%d)\n",
                    (unsigned long)address, len);
    return -1;
}


/* ============================================================================
 * External Flash Support (STUBS)
 * ============================================================================
 */

#ifdef EXT_FLASH

void ext_flash_lock(void)
{
    /* Stub */
}

void ext_flash_unlock(void)
{
    /* Stub */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    wolfBoot_printf("ext_flash_write: STUB\n");
    return -1;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    wolfBoot_printf("ext_flash_read: STUB\n");
    return -1;
}

int ext_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;

    wolfBoot_printf("ext_flash_erase: STUB\n");
    return -1;
}

#endif /* EXT_FLASH */


#endif /* TARGET_versal */

