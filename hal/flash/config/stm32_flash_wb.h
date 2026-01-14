/* stm32_flash_wb.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * Configuration header for STM32WB flash operations.
 * Include this before including stm32_flash.c
 *
 * Note: WB is similar to C0/G0 but uses FLASH_SR_BSY (not BSY1) and has CFGBSY check
 */

#ifndef STM32_FLASH_WB_CONFIG_H
#define STM32_FLASH_WB_CONFIG_H

/* Include variant-specific register definitions */
/* These should be defined in the HAL file (e.g., stm32wb.c) */
/* We rely on: FLASH_SR, FLASH_CR, FLASH_KEY, FLASH_KEY1, FLASH_KEY2, etc. */

/* Register Access Macros */
#define STM32_FLASH_SR_READ()          FLASH_SR
#define STM32_FLASH_CR_READ()          FLASH_CR
#define STM32_FLASH_CR_WRITE(val)      do { FLASH_CR = (val); } while(0)
#define STM32_FLASH_SR_WRITE(val)      do { FLASH_SR |= (val); } while(0)

/* Busy Bit - WB uses FLASH_SR_BSY (not BSY1) */
/* CFGBSY is handled in flash_wait_complete via template check */
#define STM32_FLASH_SR_BSY             FLASH_SR_BSY

/* Error Bits */
#define STM32_FLASH_SR_EOP              FLASH_SR_EOP
#define STM32_FLASH_SR_ERROR_MASK      (FLASH_SR_SIZERR | FLASH_SR_PGAERR | \
                                        FLASH_SR_WRPERR | FLASH_SR_PROGERR)

/* Control Bits */
#define STM32_FLASH_CR_PG              FLASH_CR_PG
#define STM32_FLASH_CR_LOCK            FLASH_CR_LOCK

/* Write Configuration */
#define STM32_FLASH_WRITE_ALIGNMENT    8
/* WB doesn't use FLASHMEM_ADDRESS_SPACE offset for writes - address is used directly */
#define STM32_FLASH_ADDRESS_REMAP(addr) (addr)

/* Unlock Sequence */
#define STM32_FLASH_UNLOCK_SEQUENCE() \
    do { \
        if ((FLASH_CR & FLASH_CR_LOCK) != 0) { \
            FLASH_KEY = FLASH_KEY1; \
            DMB(); \
            FLASH_KEY = FLASH_KEY2; \
            DMB(); \
            while ((FLASH_CR & FLASH_CR_LOCK) != 0) \
                ; \
        } \
    } while(0)

/* Lock Sequence */
#define STM32_FLASH_LOCK_SEQUENCE() \
    do { \
        flash_wait_complete(); \
        if ((FLASH_CR & FLASH_CR_LOCK) == 0) \
            FLASH_CR |= FLASH_CR_LOCK; \
    } while(0)

/* Erase Configuration */
#define STM32_FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
/* WB erase: address needs FLASHMEM_ADDRESS_SPACE subtracted, then calculate page (4KB pages, shift 12) */
#define STM32_FLASH_ERASE_CALC_PAGE(addr) ((((addr) - FLASHMEM_ADDRESS_SPACE) >> 12) & FLASH_CR_PNB_MASK)
/* WB erase - address passed to macro already has FLASHMEM_ADDRESS_SPACE subtracted */
#define STM32_FLASH_ERASE_PAGE(page) \
    do { \
        flash_clear_errors(); \
        uint32_t reg = FLASH_CR & ~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_FSTPG | FLASH_CR_PG); \
        FLASH_CR = reg | ((page) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER; \
        DMB(); \
        FLASH_CR |= FLASH_CR_STRT; \
        DMB(); \
        flash_wait_complete(); \
        FLASH_CR &= ~FLASH_CR_PER; \
    } while(0)

#endif /* STM32_FLASH_WB_CONFIG_H */
