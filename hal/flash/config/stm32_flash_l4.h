/* stm32_flash_l4.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * Configuration header for STM32L4 flash operations.
 * Include this before including stm32_flash.c
 *
 * Note: L4 uses HAL library for unlock/lock/erase, but direct register access for write
 */

#ifndef STM32_FLASH_L4_CONFIG_H
#define STM32_FLASH_L4_CONFIG_H

/* HAL header should be included in the HAL file (stm32l4.c) before this config */

/* Register Access Macros - L4 uses struct-based access (FLASH->SR, FLASH->CR) */
#define STM32_FLASH_SR_READ()          FLASH->SR
#define STM32_FLASH_CR_READ()          FLASH->CR
#define STM32_FLASH_CR_WRITE(val)      do { FLASH->CR = (val); } while(0)
#define STM32_FLASH_SR_WRITE(val)      do { FLASH->SR |= (val); } while(0)

/* Busy Bit */
#define STM32_FLASH_SR_BSY             FLASH_SR_BSY

/* Error Bits */
#define STM32_FLASH_SR_EOP              FLASH_SR_EOP
#define STM32_FLASH_SR_ERROR_MASK      (FLASH_SR_PROGERR | FLASH_SR_WRPERR | \
                                        FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
                                        FLASH_SR_PGSERR)

/* Control Bits */
#define STM32_FLASH_CR_PG              FLASH_CR_PG
#define STM32_FLASH_CR_LOCK            FLASH_CR_LOCK

/* Write Configuration */
#define STM32_FLASH_WRITE_ALIGNMENT    8
/* L4 doesn't use FLASHMEM_ADDRESS_SPACE offset - address is used directly */
#define STM32_FLASH_ADDRESS_REMAP(addr) (addr)

/* Unlock Sequence - L4 uses HAL function */
#define STM32_FLASH_UNLOCK_SEQUENCE() \
    do { \
        HAL_FLASH_Unlock(); \
    } while(0)

/* Lock Sequence - L4 uses HAL function */
#define STM32_FLASH_LOCK_SEQUENCE() \
    do { \
        HAL_FLASH_Lock(); \
    } while(0)

/* Erase Configuration - L4 uses HAL_FLASHEx_Erase which is complex */
/* We'll need to keep erase function separate or create a wrapper */
/* For now, define macros but note that erase needs special handling */
#define STM32_FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
/* L4 erase uses HAL_FLASHEx_Erase with GetPage/GetBank helpers */
/* This is too complex for a simple macro, so we'll keep the erase function in the HAL file */

#endif /* STM32_FLASH_L4_CONFIG_H */
