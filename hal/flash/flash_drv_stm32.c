/* flash_drv_stm32.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * Unified STM32 Flash Driver Implementation
 * Follows the same pattern as spi_drv_stm32.c
 *
 * Platform-specific configuration is provided via flash_drv_stm32.h
 */

#include <stdint.h>
#include "hal.h"
#include "hal/flash/flash_drv_stm32.h"

/* Register definitions are provided by HAL files via flash_drv_stm32.h macros */
/* When compiling separately, we need to ensure registers are accessible */
/* The header defines accessor macros that reference FLASH_CR, FLASH_SR, etc. */
/* These must be defined in the HAL files before including this header */

/* RAMFUNCTION is defined in target.h (included via hal.h) */
#ifndef RAMFUNCTION
#define RAMFUNCTION
#endif

/* Helper: Wait for flash operation to complete */
static RAMFUNCTION void flash_wait_complete(void)
{
#ifdef TARGET_stm32wb
    /* WB checks both BSY and CFGBSY */
    while ((FLASH_SR_READ() & (FLASH_SR_BSY | FLASH_SR_CFGBSY)) != 0)
        ;
#elif defined(TARGET_stm32g0)
    /* G0 checks BSY1, BSY2, and CFGBSY */
    while ((FLASH_SR_READ() & (FLASH_SR_BSY | FLASH_SR_BSY2 | FLASH_SR_CFGBSY)) != 0)
        ;
#else
    /* C0/L4 use simple BSY check */
    while ((FLASH_SR_READ() & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#endif
}

/* Helper: Clear error flags */
static RAMFUNCTION void flash_clear_errors(void)
{
    FLASH_SR_WRITE(FLASH_SR_ERROR_MASK);
}

/* Main Flash Write Function */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    flash_clear_errors();

    /* Set program bit - WB needs FSTPG cleared first */
#ifdef TARGET_stm32wb
    {
        uint32_t reg = FLASH_CR_READ() & (~FLASH_CR_FSTPG);
        FLASH_CR_WRITE(reg | FLASH_CR_PG);
    }
#else
    FLASH_CR_WRITE(FLASH_CR_READ() | FLASH_CR_PG);
#endif

#if FLASH_WRITE_ALIGNMENT == 8
    /* 8-byte aligned write (64-bit) - C0/G0/L4/WB */
    while (i < len) {
        flash_clear_errors();

        /* Check if we can do aligned 8-byte write */
        if ((len - i >= 8) &&
            (((address + i) & 0x07) == 0) &&
            ((((uint32_t)data) + i) & 0x07) == 0) {

            /* Aligned 8-byte write */
            src = (uint32_t *)data;
            dst = (uint32_t *)FLASH_ADDRESS_REMAP(address);
            flash_wait_complete();
            dst[i >> 2] = src[i >> 2];
            dst[(i >> 2) + 1] = src[(i >> 2) + 1];
            flash_wait_complete();
            i += 8;
        } else {
            /* Unaligned write - read-modify-write */
            uint32_t val[2];
            uint8_t *vbytes = (uint8_t *)(val);
            int off = (address + i) - (((address + i) >> 3) << 3);
            uint32_t base_addr = address & (~0x07); /* aligned to 64 bit */
            int u32_idx = (i >> 2);
            dst = (uint32_t *)FLASH_ADDRESS_REMAP(base_addr);
            val[0] = dst[u32_idx];
            val[1] = dst[u32_idx + 1];
            while ((off < 8) && (i < len))
                vbytes[off++] = data[i++];
            dst[u32_idx] = val[0];
            dst[u32_idx + 1] = val[1];
            flash_wait_complete();
        }
    }

    /* Common cleanup */
    /* Check for PROGERR (L4 variant returns -1 on error) */
    int ret = 0;
#ifdef TARGET_stm32l4
    if ((FLASH_SR_READ() & FLASH_SR_PROGERR) != FLASH_SR_PROGERR) {
        ret = 0;
    } else {
        ret = -1;
    }
#endif

    /* Clear EOP flag */
    if ((FLASH_SR_READ() & FLASH_SR_EOP) == FLASH_SR_EOP) {
        FLASH_SR_WRITE(FLASH_SR_EOP);
    }

    FLASH_CR_WRITE(FLASH_CR_READ() & ~FLASH_CR_PG);

    return ret;

#else
    #error "Unsupported FLASH_WRITE_ALIGNMENT value"
#endif
}

/* Unlock Flash */
void RAMFUNCTION hal_flash_unlock(void)
{
#ifdef FLASH_USE_HAL_LIBRARY
    /* L4 uses HAL library */
    HAL_FLASH_Unlock();
#else
    /* C0/G0/WB use direct register access */
    flash_wait_complete();
    if ((FLASH_CR_READ() & FLASH_CR_LOCK) != 0) {
        FLASH_KEY = FLASH_KEY1;
        DMB();
        FLASH_KEY = FLASH_KEY2;
        DMB();
        while ((FLASH_CR_READ() & FLASH_CR_LOCK) != 0)
            ;
    }
#endif
}

/* Lock Flash */
void RAMFUNCTION hal_flash_lock(void)
{
#ifdef FLASH_USE_HAL_LIBRARY
    /* L4 uses HAL library */
    HAL_FLASH_Lock();
#else
    /* C0/G0/WB use direct register access */
    flash_wait_complete();
    if ((FLASH_CR_READ() & FLASH_CR_LOCK) == 0)
        FLASH_CR_WRITE(FLASH_CR_READ() | FLASH_CR_LOCK);
#endif
}

/* Erase Flash */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
#ifdef FLASH_USE_HAL_LIBRARY
    /* L4 uses HAL library - this function should be overridden in stm32l4.c */
    /* This is a fallback that should not be called */
    (void)address;
    (void)len;
    return -1;
#else
    uint32_t end_address;
    uint32_t p;

    if (len == 0)
        return -1;

    end_address = address + len - 1;
    flash_wait_complete();

    for (p = address; p < end_address; p += FLASH_ERASE_PAGE_SIZE) {
        uint32_t page;

#ifdef TARGET_stm32g0
        /* G0 erase: address needs FLASHMEM_ADDRESS_SPACE subtracted */
        page = (((p - FLASHMEM_ADDRESS_SPACE) >> FLASH_ERASE_PAGE_SHIFT) & FLASH_CR_PNB_MASK);

        /* G0 erase with bank selection */
        while (FLASH_SR_READ() & (FLASH_SR_BSY1 | FLASH_SR_BSY2))
            ;
        flash_clear_errors();
        while (FLASH_SR_READ() & FLASH_SR_CFGBSY)
            ;
        {
            uint32_t p_addr = (page << FLASH_ERASE_PAGE_SHIFT);
            uint32_t reg = FLASH_CR_READ() & (~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT));
            reg &= ~(FLASH_CR_BKER_BITMASK);
            if (p_addr >= BANK_SIZE) {
                reg |= FLASH_CR_BKER;
            }
            FLASH_CR_WRITE(reg | ((page) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER);
            DMB();
            FLASH_CR_WRITE(FLASH_CR_READ() | FLASH_CR_STRT);
            flash_wait_complete();
            FLASH_CR_WRITE(FLASH_CR_READ() & ~FLASH_CR_PER);
        }

#elif defined(TARGET_stm32wb)
        /* WB erase: address needs FLASHMEM_ADDRESS_SPACE subtracted */
        page = (((p - FLASHMEM_ADDRESS_SPACE) >> FLASH_ERASE_PAGE_SHIFT) & FLASH_CR_PNB_MASK);

        flash_clear_errors();
        {
            uint32_t reg = FLASH_CR_READ() & ~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_FSTPG | FLASH_CR_PG);
            FLASH_CR_WRITE(reg | ((page) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER);
            DMB();
            FLASH_CR_WRITE(FLASH_CR_READ() | FLASH_CR_STRT);
            DMB();
            flash_wait_complete();
            FLASH_CR_WRITE(FLASH_CR_READ() & ~FLASH_CR_PER);
        }

#else /* TARGET_stm32c0 */
        /* C0 erase: address is used directly */
        page = ((p >> FLASH_ERASE_PAGE_SHIFT) & FLASH_CR_PNB_MASK);

        flash_clear_errors();
        {
            uint32_t reg = FLASH_CR_READ() & (~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT));
            FLASH_CR_WRITE(reg | ((page) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER);
            DMB();
            FLASH_CR_WRITE(FLASH_CR_READ() | FLASH_CR_STRT);
            flash_wait_complete();
            FLASH_CR_WRITE(FLASH_CR_READ() & ~FLASH_CR_PER);
        }
#endif
    }

    return 0;
#endif /* FLASH_USE_HAL_LIBRARY */
}
