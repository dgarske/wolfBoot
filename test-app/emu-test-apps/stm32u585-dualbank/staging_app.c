/* staging_app.c
 *
 * Test application for the STM32U585 DUALBANK_SWAP fallback scenario.
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

/* This image is signed as version 2 and pre-loaded into the UPDATE
 * partition, with the BOOT partition empty. The test then exercises the
 * hardware-assisted bank swap and the fallback path in a single
 * emulator session:
 *
 *  boot 1: wolfBoot selects the update (v2 vs empty), verifies it and
 *          activates SWAP_BANK, then reboots. This app now sits in the
 *          logical BOOT partition, mapped to physical bank 2.
 *  boot 2: wolfBoot boots this app. First run (marker empty): program a
 *          fake v3 image header (valid magic/size/version, no integrity
 *          TLVs) into the logical UPDATE partition, set the marker and
 *          reboot. This simulates staging a corrupted update while
 *          running from bank 2.
 *  boot 3: wolfBoot picks the fake v3 update, verification fails, and it
 *          must erase the CORRUPT UPDATE image (physical bank 1) and
 *          fall back to this image (physical bank 2). Second run of this
 *          app (marker set): report success with bkpt 0x7f.
 *
 * With the pre-fix hal_flash_erase (BKER derived from the logical
 * address only), boot 3 erases the healthy image instead and the device
 * bricks: bkpt 0x7f is never reached.
 */

#include <stdint.h>

/* Defaults match config/examples/stm32u5-nonsecure-dualbank.config; the
 * Makefile overrides them from the current .config. */
#ifndef UPDATE_BASE
#define UPDATE_BASE  0x08110000u /* WOLFBOOT_PARTITION_UPDATE_ADDRESS */
#endif
#ifndef MARKER_ADDR
#define MARKER_ADDR  0x08040000u /* BOOT partition end: unused flash */
#endif

#define FLASH_REGS   0x40022000u
#define FLASH_NSKEYR (*(volatile uint32_t *)(FLASH_REGS + 0x008u))
#define FLASH_NSCR   (*(volatile uint32_t *)(FLASH_REGS + 0x028u))

#define FLASH_KEY1   0x45670123u
#define FLASH_KEY2   0xCDEF89ABu
#define FLASH_CR_PG  (1u << 0)

#define AIRCR        (*(volatile uint32_t *)0xE000ED0Cu)
#define AIRCR_RESET  0x05FA0004u

#define MARKER_VALUE 0xDEADC0DEu

extern uint32_t _estack;

static void bkpt_ok(void)
{
    __asm volatile("bkpt #0x7f");
    while (1) { }
}

static void flash_program_word(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

void app_main(void)
{
    if (*(volatile uint32_t *)MARKER_ADDR == MARKER_VALUE) {
        /* Second run: wolfBoot erased the corrupt update and fell back
         * to this image. */
        bkpt_ok();
    }

    /* First run: stage a fake v3 image in the UPDATE partition. The
     * target area is empty flash, so plain programming is enough.
     * Header: magic 'WOLF', size 0x400, version TLV (tag 0x0001, len 4,
     * value 3). No SHA/signature TLVs, so verification must fail. */
    FLASH_NSKEYR = FLASH_KEY1;
    FLASH_NSKEYR = FLASH_KEY2;
    FLASH_NSCR |= FLASH_CR_PG;

    flash_program_word(UPDATE_BASE + 0u, 0x464C4F57u);
    flash_program_word(UPDATE_BASE + 4u, 0x00000400u);
    flash_program_word(UPDATE_BASE + 8u, 0x00040001u);
    flash_program_word(UPDATE_BASE + 12u, 0x00000003u);

    flash_program_word(MARKER_ADDR, MARKER_VALUE);

    FLASH_NSCR &= ~FLASH_CR_PG;

    AIRCR = AIRCR_RESET;
    while (1) { }
}

void Reset_Handler(void)
{
    app_main();
    while (1) { }
}

static void spin(void)
{
    while (1) { }
}

__attribute__((section(".isr_vector"), used))
const void * const vector_table[16] = {
    (void *)&_estack,
    (void *)Reset_Handler,
    (void *)spin, (void *)spin, (void *)spin, (void *)spin,
    (void *)spin, (void *)spin, (void *)spin, (void *)spin,
    (void *)spin, (void *)spin, (void *)spin, (void *)spin,
    (void *)spin, (void *)spin,
};
