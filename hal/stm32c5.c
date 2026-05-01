/* stm32c5.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* STM32C5 family (e.g. STM32C5A3ZGT6 on NUCLEO-C5A3ZG). Cortex-M33
 * without TrustZone in this configuration.  Dual-bank 1 MB flash on
 * the -ZG variant (2 x 512 KB), 8 KB pages, 64-bit (double-word) write
 * quantum.  Default sysclk after reset is HSI = 48 MHz (HSIS / 1).
 * This minimal port keeps the default clock and only configures
 * peripherals.
 */

#include <stdint.h>
#include <string.h>
#include <image.h>
#include "hal/stm32c5.h"
#include "hal.h"
#include "printf.h"


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates) {
        FLASH_ACR = (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
        while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != waitstates)
            ;
    }
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & (FLASH_SR_BSY | FLASH_SR_WBNE | FLASH_SR_DBNE)) != 0)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* On STM32C5, error flags are cleared via the dedicated FLASH_CCR
     * register (write 1 to clear).  Always clear all known flags so a
     * stale error from a prior cycle does not block the next operation.
     */
    FLASH_CCR = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR |
                FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |
                FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPTCHANGEERR;
}

/* C5 flash programming requires 128-bit (quad-word, 16-byte) writes
 * to produce valid ECC.  Partial writes leave the per-quad-word ECC
 * undefined and reads come back with bit-flipped \"corrected\" data.
 * For unaligned heads/tails we read the existing flash content and
 * merge so that every written quad-word is a complete ECC block.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t qword[4];
    uint8_t *qword_bytes = (uint8_t *)qword;
    uint32_t *src, *dst;

    src = (uint32_t *)data;
    dst = (uint32_t *)address;

    while (i < len) {
        uint32_t cur_addr = (uint32_t)dst + i;
        uint32_t *dst_aligned = (uint32_t *)(cur_addr & 0xFFFFFFF0U);
        int byte_offset = cur_addr - (uint32_t)dst_aligned;
        int i_aligned = i - byte_offset;
        int j;

        if (byte_offset == 0 && i + 16 <= len) {
            /* Full aligned 128 bits from caller buffer. */
            for (j = 0; j < 4; j++) {
                qword[j] = src[((unsigned int)i >> 2) + j];
            }
        } else {
            /* Unaligned head, partial tail, or both.  Fill the 16-byte
             * window from existing flash for bytes outside [i, i+len).
             */
            for (j = 0; j < 16; j++) {
                if (j < byte_offset || i_aligned + j >= len)
                    qword_bytes[j] = ((uint8_t *)dst)[i_aligned + j];
                else
                    qword_bytes[j] = ((uint8_t *)src)[i_aligned + j];
            }
        }

        flash_wait_complete();
        flash_clear_errors();

        FLASH_CR |= FLASH_CR_PG;
        for (j = 0; j < 4; j++) {
            dst_aligned[j] = qword[j];
            ISB();
        }
        flash_wait_complete();

        if ((FLASH_SR & FLASH_SR_EOP) != 0)
            FLASH_CCR = FLASH_CCR_CLR_EOP;

        FLASH_CR &= ~FLASH_CR_PG;
        i = i_aligned + 16;
        DSB();
    }
    hal_cache_invalidate();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR = FLASH_KEY1;
        DMB();
        FLASH_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_opt_lock(void)
{
    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    flash_wait_complete();
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) == 0)
        FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
}

/* Page erase.  PNB[5:0] selects the page within the bank; BKSEL (bit 31)
 * selects bank 2 when the address falls into the upper half of flash.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    flash_clear_errors();
    if (len == 0)
        return -1;
    if (address < ARCH_FLASH_OFFSET)
        return -1;

    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t bksel = 0;
        uint32_t base;

        if (p > FLASH_TOP) {
            FLASH_CR &= ~FLASH_CR_PER;
            return 0;
        }

        if (p >= FLASH_BANK2_BASE) {
            bksel = FLASH_CR_BKSEL;
            base = FLASH_BANK2_BASE;
        } else {
            base = FLASHMEM_ADDRESS_SPACE;
        }

        reg = FLASH_CR & ~((uint32_t)(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) |
                           FLASH_CR_BKSEL);
        reg |= (((p - base) / FLASH_PAGE_SIZE) << FLASH_CR_PNB_SHIFT) |
               FLASH_CR_PER | bksel;
        FLASH_CR = reg;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete();
    }
    FLASH_CR &= ~FLASH_CR_PER;
    hal_cache_invalidate();
    return 0;
}

/* --- UART: USART2 on PA2 (TX) / PA3 (RX), AF7 (NUCLEO-C5A3ZG VCP).
 * Register/peripheral macros live in stm32c5.h; the values below are
 * board/clock specific and stay here.
 */

#define UART_TX_PIN         (2)
#define UART_RX_PIN         (3)
#define UART_PIN_AF         (7)

#define USART2_PCLK         (48000000U)

#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

static void uart2_pins_setup(void)
{
    uint32_t reg;

    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    reg = RCC_AHB2ENR;
    (void)reg;

    reg = GPIOA_MODER & ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_TX_PIN * 2));
    reg = GPIOA_MODER & ~(0x3u << (UART_RX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_RX_PIN * 2));

    reg = GPIOA_AFRL & ~(0xFu << (UART_TX_PIN * 4));
    GPIOA_AFRL = reg | (UART_PIN_AF << (UART_TX_PIN * 4));
    reg = GPIOA_AFRL & ~(0xFu << (UART_RX_PIN * 4));
    GPIOA_AFRL = reg | (UART_PIN_AF << (UART_RX_PIN * 4));

    GPIOA_PUPDR &= ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_PUPDR &= ~(0x3u << (UART_RX_PIN * 2));
}

void uart_init(void)
{
    uint32_t reg;

    uart2_pins_setup();

    RCC_APB1LENR |= RCC_APB1LENR_USART2EN;
    reg = RCC_APB1LENR;
    (void)reg;

    USART2_CR1 &= ~UART_CR1_UE;
    USART2_BRR = USART2_PCLK / 115200;
    USART2_CR1 |= UART_CR1_TE | UART_CR1_RE | UART_CR1_UE;
}

void uart_write(const char *buf, unsigned int sz)
{
    while (sz-- > 0) {
        while ((USART2_ISR & UART_ISR_TXE) == 0)
            ;
        USART2_TDR = *buf++;
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */

/* Default clock: HSIS at 48 MHz remains active after reset.  We only
 * make sure flash latency is set conservatively for that frequency
 * before any high-speed accesses.  PLL/HSE bring-up is intentionally
 * deferred to a future commit.
 */
static void clock_init(void)
{
    flash_set_waitstates(1);
    FLASH_ACR |= FLASH_ACR_PRFTEN;
}

void hal_init(void)
{
    clock_init();
    hal_cache_enable(1);

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
#endif
}

void hal_prepare_boot(void)
{
}

void RAMFUNCTION hal_cache_enable(int way)
{
    ICACHE_CR |= (way ? ICACHE_CR_2WAYS : ICACHE_CR_1WAY);
    ICACHE_CR |= ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_disable(void)
{
    ICACHE_CR &= ~ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    if ((ICACHE_CR & ICACHE_CR_CEN) == 0)
        return;
    if ((ICACHE_SR & ICACHE_SR_BUSYF) == 0)
        ICACHE_CR |= ICACHE_CR_CACHEINV;
    while ((ICACHE_SR & ICACHE_SR_BSYENDF) == 0)
        ;
    ICACHE_SR |= ICACHE_SR_BSYENDF;
}
