/* flash_drv_stm32.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * Platform-specific configuration for STM32 flash driver.
 * Similar to spi_drv_stm32.h pattern.
 */

#ifndef FLASH_DRV_STM32_H_INCLUDED
#define FLASH_DRV_STM32_H_INCLUDED

#include <stdint.h>

/* Platform-specific register access and configuration macros */

#ifdef TARGET_stm32c0

/* Register definitions - only define if not already defined (from HAL file) */
#ifndef FLASH_BASE
#define FLASH_BASE          (0x40022000)
#endif
#ifndef FLASH_SR
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#endif
#ifndef FLASH_CR
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14))
#endif
#ifndef FLASH_KEY
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#endif
#ifndef FLASHMEM_ADDRESS_SPACE
#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE        (0x800) /* 2KB */
#endif
#ifndef FLASH_PAGE_SIZE_SHIFT
#define FLASH_PAGE_SIZE_SHIFT  11
#endif
#ifndef FLASH_SR_BSY1
#define FLASH_SR_BSY1         (1 << 16)
#endif
#ifndef FLASH_SR_SIZERR
#define FLASH_SR_SIZERR       (1 << 6)
#endif
#ifndef FLASH_SR_PGAERR
#define FLASH_SR_PGAERR       (1 << 5)
#endif
#ifndef FLASH_SR_WRPERR
#define FLASH_SR_WRPERR       (1 << 4)
#endif
#ifndef FLASH_SR_PROGERR
#define FLASH_SR_PROGERR      (1 << 3)
#endif
#ifndef FLASH_SR_EOP
#define FLASH_SR_EOP          (1 << 0)
#endif
#ifndef FLASH_CR_LOCK
#define FLASH_CR_LOCK         (1UL << 31)
#endif
#ifndef FLASH_CR_STRT
#define FLASH_CR_STRT         (1 << 16)
#endif
#ifndef FLASH_CR_PER
#define FLASH_CR_PER          (1 << 1)
#endif
#ifndef FLASH_CR_PG
#define FLASH_CR_PG           (1 << 0)
#endif
#ifndef FLASH_CR_PNB_SHIFT
#define FLASH_CR_PNB_SHIFT    3
#endif
#ifndef FLASH_CR_PNB_MASK
#define FLASH_CR_PNB_MASK     0x7f
#endif
#ifndef FLASH_KEY1
#define FLASH_KEY1            (0x45670123)
#endif
#ifndef FLASH_KEY2
#define FLASH_KEY2            (0xCDEF89AB)
#endif
#ifndef DMB
#define DMB()                 __asm__ volatile ("dmb")
#endif

/* Register Access - C0 uses direct register access */
#define FLASH_SR_READ()          FLASH_SR
#define FLASH_CR_READ()          FLASH_CR
#define FLASH_CR_WRITE(val)      do { FLASH_CR = (val); } while(0)
#define FLASH_SR_WRITE(val)      do { FLASH_SR |= (val); } while(0)

/* Busy Bit - C0 uses BSY1 */
#define FLASH_SR_BSY             FLASH_SR_BSY1

/* Error Bits */
#define FLASH_SR_ERROR_MASK      (FLASH_SR_SIZERR | FLASH_SR_PGAERR | \
                                  FLASH_SR_WRPERR | FLASH_SR_PROGERR)

/* Write Configuration */
#define FLASH_WRITE_ALIGNMENT    8
#define FLASH_ADDRESS_REMAP(addr) ((addr) + FLASHMEM_ADDRESS_SPACE)

/* Erase Configuration */
#define FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
#define FLASH_ERASE_PAGE_SHIFT   FLASH_PAGE_SIZE_SHIFT  /* 11 for C0 */

#endif /* TARGET_stm32c0 */


#ifdef TARGET_stm32g0

/* Register definitions - only define if not already defined (from HAL file) */
#ifndef FLASH_BASE
#define FLASH_BASE          (0x40022000)
#endif
#ifndef FLASH_SR
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#endif
#ifndef FLASH_CR
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14))
#endif
#ifndef FLASH_KEY
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#endif
#ifndef FLASHMEM_ADDRESS_SPACE
#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE     (0x800) /* 2KB */
#endif
#ifndef FLASH_SR_CFGBSY
#define FLASH_SR_CFGBSY     (1 << 18)
#endif
#ifndef FLASH_SR_BSY2
#define FLASH_SR_BSY2       (1 << 17)
#endif
#ifndef FLASH_SR_BSY1
#define FLASH_SR_BSY1       (1 << 16)
#endif
#ifndef FLASH_SR_SIZERR
#define FLASH_SR_SIZERR     (1 << 6)
#endif
#ifndef FLASH_SR_PGAERR
#define FLASH_SR_PGAERR     (1 << 5)
#endif
#ifndef FLASH_SR_WRPERR
#define FLASH_SR_WRPERR     (1 << 4)
#endif
#ifndef FLASH_SR_PROGERR
#define FLASH_SR_PROGERR    (1 << 3)
#endif
#ifndef FLASH_SR_EOP
#define FLASH_SR_EOP        (1 << 0)
#endif
#ifndef FLASH_CR_LOCK
#define FLASH_CR_LOCK       (1UL << 31)
#endif
#ifndef FLASH_CR_STRT
#define FLASH_CR_STRT       (1 << 16)
#endif
#ifndef FLASH_CR_BKER
#define FLASH_CR_BKER       (1 << 13)
#endif
#ifndef FLASH_CR_BKER_BITMASK
#define FLASH_CR_BKER_BITMASK 0x2000
#endif
#ifndef FLASH_CR_PER
#define FLASH_CR_PER        (1 << 1)
#endif
#ifndef FLASH_CR_PG
#define FLASH_CR_PG         (1 << 0)
#endif
#ifndef FLASH_CR_PNB_SHIFT
#define FLASH_CR_PNB_SHIFT  3
#endif
#ifndef FLASH_CR_PNB_MASK
#define FLASH_CR_PNB_MASK   0x7f
#endif
#ifndef FLASH_KEY1
#define FLASH_KEY1          (0x45670123)
#endif
#ifndef FLASH_KEY2
#define FLASH_KEY2          (0xCDEF89AB)
#endif
#ifndef BANK_SIZE
#define BANK_SIZE           (0x40000)
#endif
#ifndef DMB
#define DMB()               __asm__ volatile ("dmb")
#endif

/* Register Access - G0 uses direct register access */
#define FLASH_SR_READ()          FLASH_SR
#define FLASH_CR_READ()          FLASH_CR
#define FLASH_CR_WRITE(val)      do { FLASH_CR = (val); } while(0)
#define FLASH_SR_WRITE(val)      do { FLASH_SR |= (val); } while(0)

/* Busy Bit - G0 checks BSY1, BSY2, and CFGBSY */
#define FLASH_SR_BSY             FLASH_SR_BSY1

/* Error Bits */
#define FLASH_SR_ERROR_MASK      (FLASH_SR_SIZERR | FLASH_SR_PGAERR | \
                                  FLASH_SR_WRPERR | FLASH_SR_PROGERR)

/* Write Configuration */
#define FLASH_WRITE_ALIGNMENT    8
#define FLASH_ADDRESS_REMAP(addr) (addr)  /* G0 doesn't use offset */

/* Erase Configuration */
#define FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
#define FLASH_ERASE_PAGE_SHIFT   11  /* 2KB pages */
#define FLASH_ERASE_NEEDS_BANK_SELECT  1  /* G0 has bank selection */

#endif /* TARGET_stm32g0 */


#ifdef TARGET_stm32wb

/* Register definitions - only define if not already defined (from HAL file) */
#ifndef FLASH_BASE
#define FLASH_BASE          (0x58004000)
#endif
#ifndef FLASH_SR
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#endif
#ifndef FLASH_CR
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14))
#endif
#ifndef FLASH_KEY
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#endif
#ifndef FLASHMEM_ADDRESS_SPACE
#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE     (0x1000) /* 4KB */
#endif
#ifndef FLASH_SR_BSY
#define FLASH_SR_BSY        (1UL << 16)
#endif
#ifndef FLASH_SR_CFGBSY
#define FLASH_SR_CFGBSY     (1UL << 18)
#endif
#ifndef FLASH_SR_SIZERR
#define FLASH_SR_SIZERR     (1UL << 6)
#endif
#ifndef FLASH_SR_PGAERR
#define FLASH_SR_PGAERR     (1UL << 5)
#endif
#ifndef FLASH_SR_WRPERR
#define FLASH_SR_WRPERR     (1UL << 4)
#endif
#ifndef FLASH_SR_PROGERR
#define FLASH_SR_PROGERR    (1UL << 3)
#endif
#ifndef FLASH_SR_EOP
#define FLASH_SR_EOP        (1UL << 0)
#endif
#ifndef FLASH_CR_LOCK
#define FLASH_CR_LOCK       (1UL << 31)
#endif
#ifndef FLASH_CR_STRT
#define FLASH_CR_STRT       (1UL << 16)
#endif
#ifndef FLASH_CR_FSTPG
#define FLASH_CR_FSTPG      (1UL << 18)
#endif
#ifndef FLASH_CR_PER
#define FLASH_CR_PER        (1UL << 1)
#endif
#ifndef FLASH_CR_PG
#define FLASH_CR_PG         (1UL << 0)
#endif
#ifndef FLASH_CR_PNB_SHIFT
#define FLASH_CR_PNB_SHIFT  3
#endif
#ifndef FLASH_CR_PNB_MASK
#define FLASH_CR_PNB_MASK   0xFFUL
#endif
#ifndef FLASH_KEY1
#define FLASH_KEY1          (0x45670123UL)
#endif
#ifndef FLASH_KEY2
#define FLASH_KEY2          (0xCDEF89ABUL)
#endif
#ifndef DMB
#define DMB()               __asm__ volatile ("dmb")
#endif

/* Register Access - WB uses direct register access */
#define FLASH_SR_READ()          FLASH_SR
#define FLASH_CR_READ()          FLASH_CR
#define FLASH_CR_WRITE(val)      do { FLASH_CR = (val); } while(0)
#define FLASH_SR_WRITE(val)      do { FLASH_SR |= (val); } while(0)

/* Busy Bit - WB uses FLASH_SR_BSY and checks CFGBSY */
/* FLASH_SR_BSY is already defined above */

/* Error Bits */
#define FLASH_SR_ERROR_MASK      (FLASH_SR_SIZERR | FLASH_SR_PGAERR | \
                                  FLASH_SR_WRPERR | FLASH_SR_PROGERR)

/* Write Configuration */
#define FLASH_WRITE_ALIGNMENT    8
#define FLASH_ADDRESS_REMAP(addr) (addr)  /* WB doesn't use offset */

/* Erase Configuration */
#define FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
#define FLASH_ERASE_PAGE_SHIFT   12  /* 4KB pages */

#endif /* TARGET_stm32wb */


#ifdef TARGET_stm32l4

/* Register Access - L4 uses struct-based access */
#define FLASH_SR_READ()          FLASH->SR
#define FLASH_CR_READ()          FLASH->CR
#define FLASH_CR_WRITE(val)      do { FLASH->CR = (val); } while(0)
#define FLASH_SR_WRITE(val)      do { FLASH->SR |= (val); } while(0)

/* Busy Bit - FLASH_SR_BSY is already defined in HAL file */

/* Error Bits - FLASH_SR_EOP and FLASH_SR_PROGERR are already defined in HAL file */
#define FLASH_SR_ERROR_MASK      (FLASH_SR_PROGERR | FLASH_SR_WRPERR | \
                                  FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
                                  FLASH_SR_PGSERR)

/* Control Bits - FLASH_CR_PG and FLASH_CR_LOCK are already defined in HAL files */

/* Write Configuration */
#define FLASH_WRITE_ALIGNMENT    8
#define FLASH_ADDRESS_REMAP(addr) (addr)  /* L4 doesn't use offset */

/* Erase Configuration - L4 uses HAL library */
#define FLASH_ERASE_PAGE_SIZE    FLASH_PAGE_SIZE
#define FLASH_USE_HAL_LIBRARY    1  /* L4 uses HAL_FLASHEx_Erase */

#endif /* TARGET_stm32l4 */


/* Common macros that work for all variants */
#ifndef FLASH_SR_READ
#error "FLASH_SR_READ not defined for this target"
#endif

#ifndef FLASH_CR_READ
#error "FLASH_CR_READ not defined for this target"
#endif

#ifndef FLASH_CR_WRITE
#error "FLASH_CR_WRITE not defined for this target"
#endif

#ifndef FLASH_SR_WRITE
#error "FLASH_SR_WRITE not defined for this target"
#endif

#ifndef FLASH_SR_BSY
#error "FLASH_SR_BSY not defined for this target"
#endif

/* These should be defined in the HAL files */
#ifndef FLASH_SR_EOP
#error "FLASH_SR_EOP not defined for this target"
#endif

#ifndef FLASH_SR_ERROR_MASK
#error "FLASH_SR_ERROR_MASK not defined for this target"
#endif

/* FLASH_CR_PG and FLASH_CR_LOCK should be defined in HAL files */

#ifndef FLASH_WRITE_ALIGNMENT
#error "FLASH_WRITE_ALIGNMENT not defined for this target"
#endif

#ifndef FLASH_ADDRESS_REMAP
#error "FLASH_ADDRESS_REMAP not defined for this target"
#endif

#ifndef FLASH_ERASE_PAGE_SIZE
#error "FLASH_ERASE_PAGE_SIZE not defined for this target"
#endif

#ifndef FLASH_ERASE_PAGE_SHIFT
#error "FLASH_ERASE_PAGE_SHIFT not defined for this target"
#endif

#endif /* FLASH_DRV_STM32_H_INCLUDED */
