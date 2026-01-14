/* stm32_flash.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * Generic STM32 Flash Implementation
 *
 * This file provides a generic implementation of STM32 flash operations
 * that can be configured for different STM32 variants via configuration headers.
 *
 * Usage:
 *   1. Include your variant-specific configuration header (e.g., stm32_flash_c0.h)
 *   2. Include this file
 *
 * Example:
 *   #include "hal/flash/config/stm32_flash_c0.h"
 *   #include "hal/flash/stm32_flash.c"
 */

#include <stdint.h>
#include "hal.h"

/* Configuration must be included before this file */
/* Check for required macros */
#ifndef STM32_FLASH_SR_READ
#error "STM32_FLASH_SR_READ must be defined in configuration header"
#endif

#ifndef STM32_FLASH_CR_READ
#error "STM32_FLASH_CR_READ must be defined in configuration header"
#endif

#ifndef STM32_FLASH_CR_WRITE
#error "STM32_FLASH_CR_WRITE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_SR_WRITE
#error "STM32_FLASH_SR_WRITE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_SR_BSY
#error "STM32_FLASH_SR_BSY must be defined in configuration header"
#endif

#ifndef STM32_FLASH_SR_EOP
#error "STM32_FLASH_SR_EOP must be defined in configuration header"
#endif

#ifndef STM32_FLASH_SR_ERROR_MASK
#error "STM32_FLASH_SR_ERROR_MASK must be defined in configuration header"
#endif

#ifndef STM32_FLASH_CR_PG
#error "STM32_FLASH_CR_PG must be defined in configuration header"
#endif

#ifndef STM32_FLASH_CR_LOCK
#error "STM32_FLASH_CR_LOCK must be defined in configuration header"
#endif

#ifndef STM32_FLASH_WRITE_ALIGNMENT
#error "STM32_FLASH_WRITE_ALIGNMENT must be defined in configuration header"
#endif

#ifndef STM32_FLASH_ADDRESS_REMAP
#error "STM32_FLASH_ADDRESS_REMAP must be defined in configuration header"
#endif

#ifndef STM32_FLASH_UNLOCK_SEQUENCE
#error "STM32_FLASH_UNLOCK_SEQUENCE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_LOCK_SEQUENCE
#error "STM32_FLASH_LOCK_SEQUENCE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_ERASE_PAGE_SIZE
#error "STM32_FLASH_ERASE_PAGE_SIZE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_ERASE_CALC_PAGE
#error "STM32_FLASH_ERASE_CALC_PAGE must be defined in configuration header"
#endif

#ifndef STM32_FLASH_ERASE_PAGE
#error "STM32_FLASH_ERASE_PAGE must be defined in configuration header"
#endif

/* Helper: Wait for flash operation to complete */
static RAMFUNCTION void flash_wait_complete(void)
{
    /* Check for CFGBSY if defined (WB variant) */
#ifdef FLASH_SR_CFGBSY
    while ((STM32_FLASH_SR_READ() & (STM32_FLASH_SR_BSY | FLASH_SR_CFGBSY)) != 0)
        ;
#else
    while ((STM32_FLASH_SR_READ() & STM32_FLASH_SR_BSY) == STM32_FLASH_SR_BSY)
        ;
#endif
}

/* Helper: Clear error flags */
static RAMFUNCTION void flash_clear_errors(void)
{
    STM32_FLASH_SR_WRITE(STM32_FLASH_SR_ERROR_MASK);
}

/* Main Flash Write Function */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    flash_clear_errors();
    /* Clear FSTPG if defined (WB variant) */
#ifdef FLASH_CR_FSTPG
    STM32_FLASH_CR_WRITE((STM32_FLASH_CR_READ() & ~FLASH_CR_FSTPG) | STM32_FLASH_CR_PG);
#else
    STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
#endif

#if STM32_FLASH_WRITE_ALIGNMENT == 8
    /* 8-byte aligned write (64-bit) - C0/G0/L4/WB/L5/U5 */
    while (i < len) {
        flash_clear_errors();

        /* Check if we can do aligned 8-byte write */
        if ((len - i >= 8) &&
            (((address + i) & 0x07) == 0) &&
            ((((uint32_t)data) + i) & 0x07) == 0) {

            /* Aligned 8-byte write */
            src = (uint32_t *)data;
            dst = (uint32_t *)STM32_FLASH_ADDRESS_REMAP(address);
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
            dst = (uint32_t *)STM32_FLASH_ADDRESS_REMAP(base_addr);
            val[0] = dst[u32_idx];
            val[1] = dst[u32_idx + 1];
            while ((off < 8) && (i < len))
                vbytes[off++] = data[i++];
            dst[u32_idx] = val[0];
            dst[u32_idx + 1] = val[1];
            flash_wait_complete();
        }
    }

#elif STM32_FLASH_WRITE_ALIGNMENT == 16
    /* 16-byte aligned write (128-bit) - H5 */
    while (i < len) {
        flash_clear_errors();

        /* Check if we can do aligned 16-byte write */
        if ((len - i >= 16) &&
            (((address + i) & 0x0F) == 0) &&
            ((((uint32_t)data) + i) & 0x0F) == 0)) {

            /* Aligned 16-byte write */
            src = (uint32_t *)data;
            dst = (uint32_t *)STM32_FLASH_ADDRESS_REMAP(address);
            flash_wait_complete();
            dst[(i >> 2) + 0] = src[(i >> 2) + 0];
            dst[(i >> 2) + 1] = src[(i >> 2) + 1];
            dst[(i >> 2) + 2] = src[(i >> 2) + 2];
            dst[(i >> 2) + 3] = src[(i >> 2) + 3];
            flash_wait_complete();
            i += 16;
        } else {
            /* Unaligned write - read-modify-write */
            uint32_t val[4];
            uint8_t *vbytes = (uint8_t *)(val);
            int off = (address + i) & 0x0F;
            uint32_t base_addr = address & (~0x0F); /* aligned to 128 bit */
            int u32_idx = (i >> 2);
            dst = (uint32_t *)STM32_FLASH_ADDRESS_REMAP(base_addr);
            val[0] = dst[u32_idx];
            val[1] = dst[u32_idx + 1];
            val[2] = dst[u32_idx + 2];
            val[3] = dst[u32_idx + 3];
            while ((off < 16) && (i < len))
                vbytes[off++] = data[i++];
            dst[u32_idx] = val[0];
            dst[u32_idx + 1] = val[1];
            dst[u32_idx + 2] = val[2];
            dst[u32_idx + 3] = val[3];
            flash_wait_complete();
        }
    }

#elif STM32_FLASH_WRITE_ALIGNMENT == 2
    /* 16-bit aligned write - F1 */
    const uint8_t *src_bytes = data;
    const uint8_t * const end = src_bytes + len;
    volatile uint16_t *dst_16 = (volatile uint16_t*)(STM32_FLASH_ADDRESS_REMAP(address) & ~1);

    flash_wait_complete();

    /* Handle unaligned initial write */
    if (address & 1) {
        uint16_t tmp = (*dst_16 & 0x00FF) | (data[0] << 8);
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
        *dst_16 = tmp;
        flash_wait_complete();
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() & ~STM32_FLASH_CR_PG);
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
        dst_16++;
        src_bytes++;
    }

    /* Main write loop */
    while (src_bytes < end) {
        /* Check for unaligned last write */
        if (src_bytes + 1 == end) {
            uint16_t tmp = (*dst_16 & 0xFF00) | *src_bytes;
            STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
            *dst_16 = tmp;
            flash_wait_complete();
            STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() & ~STM32_FLASH_CR_PG);
            break;
        }
        /* Regular 16-bit write */
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
        *dst_16 = *(const uint16_t*)src_bytes;
        flash_wait_complete();
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() & ~STM32_FLASH_CR_PG);
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
        src_bytes += 2;
        dst_16++;
    }
    i = len; /* Set i to len to skip the common cleanup below */

#elif STM32_FLASH_WRITE_ALIGNMENT == 1
    /* Byte-by-byte write - F4/F7 */
    uint32_t addr = STM32_FLASH_ADDRESS_REMAP(address);
    flash_wait_complete();
    flash_clear_errors();
    /* Set 8-bit write mode (clear PSIZE bits) */
    STM32_FLASH_CR_WRITE((STM32_FLASH_CR_READ() & ~(0x03 << 8)) | STM32_FLASH_CR_PG);

    for (i = 0; i < len; i++) {
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() | STM32_FLASH_CR_PG);
        *(uint8_t*)(addr + i) = data[i];
        flash_wait_complete();
        STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() & ~STM32_FLASH_CR_PG);
    }
    i = len; /* Set i to len to skip the common cleanup below */

#else
    #error "Unsupported STM32_FLASH_WRITE_ALIGNMENT value"
#endif

    /* Common cleanup (skip for byte-by-byte and 16-bit writes) */
#if STM32_FLASH_WRITE_ALIGNMENT != 1 && STM32_FLASH_WRITE_ALIGNMENT != 2
    /* Check for PROGERR (L4 variant returns -1 on error) */
    int ret = 0;
#ifdef FLASH_SR_PROGERR
    /* L4-style: return -1 if PROGERR is set, 0 otherwise */
    if ((STM32_FLASH_SR_READ() & FLASH_SR_PROGERR) == FLASH_SR_PROGERR) {
        ret = -1;
    }
#endif

    /* Clear EOP flag */
    if ((STM32_FLASH_SR_READ() & STM32_FLASH_SR_EOP) == STM32_FLASH_SR_EOP) {
        STM32_FLASH_SR_WRITE(STM32_FLASH_SR_EOP);
    }

    STM32_FLASH_CR_WRITE(STM32_FLASH_CR_READ() & ~STM32_FLASH_CR_PG);

    return ret;
#else
    return 0;
#endif
}

/* Unlock Flash */
/* Note: L4 overrides this to use HAL_FLASH_Unlock() */
#ifndef STM32_FLASH_L4_HAL_OVERRIDE
void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    STM32_FLASH_UNLOCK_SEQUENCE();
}

/* Lock Flash */
void RAMFUNCTION hal_flash_lock(void)
{
    STM32_FLASH_LOCK_SEQUENCE();
}
#endif

/* Erase Flash */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    if (len == 0)
        return -1;

    /* For erase, use address directly (address remapping handled in CALC_PAGE if needed) */
    end_address = address + len - 1;
    flash_wait_complete();

    for (p = address; p < end_address; p += STM32_FLASH_ERASE_PAGE_SIZE) {
        uint32_t page = STM32_FLASH_ERASE_CALC_PAGE(p);
        flash_clear_errors();
        STM32_FLASH_ERASE_PAGE(page);
    }

    return 0;
}
