/* stm32g0.c
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

#include <stdint.h>
#include <image.h>

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot STM32G0 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

/* STM32 G0 register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")


/*** RCC ***/
#define RCC_BASE (0x40021000)
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0444 - 5.4.1 */
#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x0C))  /* RM0444 - 5.4.4 */
#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x08))  /* RM0444 - 5.4.3 */
#define RCC_IOPENR          (*(volatile uint32_t *)(RCC_BASE + 0x2C))  /* RM0444 - 5.4.10 - GPIO clock enable */
#define APB1_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x3C))
#define APB2_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x40))


#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSIRDY               (1 << 10)
#define RCC_CR_HSION                (1 << 8)

#define RCC_CFGR_SW_HSISYS          0x0
#define RCC_CFGR_SW_PLL             0x2
#define RCC_PLLCFGR_PLLR_EN       (1 << 28) /* RM0444 - 5.4.3 */

#define RCC_PLLCFGR_PLLSRC_HSI16  2


/*** APB PRESCALER ***/
#define RCC_PRESCALER_DIV_NONE 0

/*** FLASH ***/
#define PWR_APB1_CLOCK_ER_VAL       (1 << 28)
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) /* RM0444 - 5.4.15 - RCC_APBENR2 - SYSCFGEN */

#define FLASH_BASE          (0x40022000)  /*FLASH_R_BASE = 0x40000000UL + 0x00020000UL + 0x00002000UL */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) /* RM0444 - 3.7.1 - FLASH_ACR */
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08)) /* RM0444 - 3.7.2 - FLASH_KEYR */
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10)) /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14)) /* RM0444 - 3.7.5 - FLASH_CR */
#define FLASH_SECR          (*(volatile uint32_t *)(FLASH_BASE + 0x80)) /* RM0444 - 3.7.12 - FLASH_SECR */

#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#define FLASH_PAGE_SIZE     (0x800) /* 2KB */

/* Register values */
#define FLASH_SR_CFGBSY                       (1 << 18) /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_BSY2                         (1 << 17) /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_BSY1                         (1 << 16) /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_SIZERR                       (1 << 6)  /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_PGAERR                       (1 << 5)  /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_WRPERR                       (1 << 4)  /* RM0444 - 3.7.4 - FLASH_SR */
#define FLASH_SR_PROGERR                      (1 << 3)
#define FLASH_SR_EOP                          (1 << 0)  /* RM0444 - 3.7.4 - FLASH_SR */

#define FLASH_CR_LOCK                         (1UL << 31) /* RM0444 - 3.7.5 - FLASH_CR */
#define FLASH_CR_STRT                         (1 << 16) /* RM0444 - 3.7.5 - FLASH_CR */

#define FLASH_CR_PER                          (1 << 1)  /* RM0444 - 3.7.5 - FLASH_CR */
#define FLASH_CR_PG                           (1 << 0)  /* RM0444 - 3.7.5 - FLASH_CR */
#define FLASH_CR_SEC_PROT                     (1 << 28) /* RM0444 - 3.7.5 - FLASH_CR */

#define FLASH_CR_PNB_SHIFT                     3        /* RM0444 - 3.7.5 - FLASH_CR - PNB bits 9:3 */
#define FLASH_CR_PNB_MASK                      0x7f     /* RM0444 - 3.7.5 - FLASH_CR - PNB bits 9:3 - 7 bits */

#define FLASH_CR_BKER                         (1 << 13)
#define FLASH_CR_BKER_BITMASK                 0x2000
#define BANK_SIZE                             (0x40000)

#define FLASH_SECR_SEC_SIZE_POS               (0U)
#define FLASH_SECR_SEC_SIZE_MASK              (0xFF)

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & 0x03) != waitstates)
        FLASH_ACR =  (reg & ~0x03) | waitstates ;
}

/* Include flash driver header (implementation compiled separately) */
#include "hal/flash/flash_drv_stm32.h"

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

/* This implementation will setup HSI RC 16 MHz as PLL Source Mux, PLLCLK as
 * System Clock Source */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t cpu_freq, plln, pllm, pllq, pllp, pllr, hpre, ppre, flash_waitstates;

    /* Enable Power controller */
    APB1_CLOCK_ER |= PWR_APB1_CLOCK_ER_VAL;

    /* Select clock parameters (CPU Speed = 64MHz) */
    cpu_freq = 64000000;
    pllm = 1;
    plln = 8;
    pllp = 2;
    pllq = 2;
    pllr = 2;
    hpre  = RCC_PRESCALER_DIV_NONE;
    ppre = RCC_PRESCALER_DIV_NONE;
    flash_waitstates = 2;
    (void)cpu_freq; /* not used */
    (void)pllq; /* not used */

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();

    /* Disable PLL */
    RCC_CR &= ~RCC_CR_PLLON;

    /* Set prescalers for AHB, ADC, ABP1, ABP2. */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF0); /* don't change bits [0-3] that were previously set */
    RCC_CFGR = (reg32 | (hpre << 8)); /* RM0444 - 5.4.3 - RCC_CFGR */
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x1C00); /* don't change bits [0-14] */
    RCC_CFGR = (reg32 | (ppre << 12));  /* RM0444 - 5.4.3 - RCC_CFGR */
    DMB();

    /* Set PLL config */
    reg32 = RCC_PLLCFGR;
    reg32 |= RCC_PLLCFGR_PLLSRC_HSI16;
    reg32 |= ((pllm - 1) << 4);
    reg32 |= plln << 8;
    reg32 |= ((pllp - 1) << 17);
    reg32 |= ((pllr - 1) << 29);
    RCC_PLLCFGR = reg32;

    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_PLLCFGR |= RCC_PLLCFGR_PLLR_EN;
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* SYSCFG, COMP and VREFBUF clock enable */
    APB2_CLOCK_ER |= SYSCFG_APB2_CLOCK_ER_VAL;
}

/* Include flash driver header (implementation compiled separately) */
#include "hal/flash/flash_drv_stm32.h"

#ifdef DEBUG_UART

/* UART base addresses */
#define UART1_BASE            (0x40013800)
#define UART2_BASE            (0x40004400)

/* UART register offsets - parameterized by base address */
#define UART_CR1(base)        (*(volatile uint32_t *)((base) + 0x00))
#define UART_CR2(base)        (*(volatile uint32_t *)((base) + 0x04))
#define UART_CR3(base)        (*(volatile uint32_t *)((base) + 0x08))
#define UART_BRR(base)        (*(volatile uint32_t *)((base) + 0x0C))
#define UART_ISR(base)        (*(volatile uint32_t *)((base) + 0x1C))
#define UART_ICR(base)        (*(volatile uint32_t *)((base) + 0x20))
#define UART_RDR(base)        (*(volatile uint32_t *)((base) + 0x24))
#define UART_TDR(base)        (*(volatile uint32_t *)((base) + 0x28))
#define UART_PRESC(base)      (*(volatile uint32_t *)((base) + 0x2C))

/* Select which UART to use (UART2 = ST-Link VCP on Nucleo boards) */
#define UART_BASE             UART2_BASE

/* UART CR1 register bits */
#define UART_CR1_UART_ENABLE    (1 << 0)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_SYMBOL_LEN     (1 << 12)  /* M0 bit: word length */
#define UART_CR1_OVER8          (1 << 15)  /* 0=16x oversampling, 1=8x */

/* UART CR2/ISR register bits */
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_ISR_TX_EMPTY       (1 << 7)
#define UART_ISR_RX_NOTEMPTY    (1 << 5)

/* GPIOA for UART pins */
#define GPIOA_BASE             (0x50000000)
#define GPIOA_MODE             (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFL              (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

#define GPIO_MODE_AF           (2)
#define UART2_PIN_AF           1  /* AF1 for UART2 on G0 */
#define UART2_RX_PIN           3  /* PA3 */
#define UART2_TX_PIN           2  /* PA2 */

#define IOPAEN                 (1 << 0)
#define UART2_APB1_CLOCK_ER_VAL (1 << 17)

/* Clock configuration for UART
 * - G0 uses PLL at 64 MHz (HSI16 * 8 / 2 = 64MHz)
 * - APB prescaler = 1 (no division), so PCLK1 = SYSCLK = 64 MHz
 * - UART2 uses PCLK1
 * - BRR = PCLK1 / bitrate (for 16x oversampling, handled by hardware)
 */
#ifndef PCLK1_FREQ
#define PCLK1_FREQ            (64000000)  /* 64 MHz */
#endif
#ifndef CLOCK_SPEED
#define CLOCK_SPEED           PCLK1_FREQ  /* Alias for compatibility */
#endif

/* Forward declaration - uart_write is called from src/string.c so must be non-static */
void uart_write(const char *buf, unsigned int len);

static void uart2_pins_setup(void)
{
    uint32_t reg;
    /* Enable GPIOA clock */
    RCC_IOPENR |= IOPAEN;

    /* Set mode = AF for RX and TX pins */
    reg = GPIOA_MODE & ~(0x03 << (UART2_RX_PIN * 2));
    GPIOA_MODE = reg | (GPIO_MODE_AF << (UART2_RX_PIN * 2));
    reg = GPIOA_MODE & ~(0x03 << (UART2_TX_PIN * 2));
    GPIOA_MODE = reg | (GPIO_MODE_AF << (UART2_TX_PIN * 2));

    /* Alternate function: use low pins (2 and 3) */
    reg = GPIOA_AFL & ~(0xf << (UART2_TX_PIN * 4));
    GPIOA_AFL = reg | (UART2_PIN_AF << (UART2_TX_PIN * 4));
    reg = GPIOA_AFL & ~(0xf << (UART2_RX_PIN * 4));
    GPIOA_AFL = reg | (UART2_PIN_AF << (UART2_RX_PIN * 4));
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;

    /* Enable pins and configure for AF */
    uart2_pins_setup();

    /* Enable UART2 clock */
    APB1_CLOCK_ER |= UART2_APB1_CLOCK_ER_VAL;

    /* Disable UART before configuration */
    UART_CR1(UART_BASE) &= ~UART_CR1_UART_ENABLE;

    /* Configure UART prescaler to DIV1 (no division) */
    UART_PRESC(UART_BASE) = 0;

    /* Configure 16x oversampling (OVER8 = 0) */
    UART_CR1(UART_BASE) &= ~UART_CR1_OVER8;

    /* BRR = PCLK1 / bitrate (16x oversampling handled by hardware) */
    UART_BRR(UART_BASE) = (uint16_t)(CLOCK_SPEED / bitrate) & 0xFFF0;

    /* Configure data bits */
    if (data == 8)
        UART_CR1(UART_BASE) &= ~UART_CR1_SYMBOL_LEN;
    else
        UART_CR1(UART_BASE) |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART_CR1(UART_BASE) |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART_CR1(UART_BASE) |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART_CR1(UART_BASE) &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }

    /* Set stop bits */
    reg = UART_CR2(UART_BASE) & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART_CR2(UART_BASE) = reg | (2 << 12);
    else
        UART_CR2(UART_BASE) = reg;

    /* Configure for TX + RX, turn on */
    UART_CR1(UART_BASE) |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE |
                            UART_CR1_UART_ENABLE);

    return 0;
}

void uart_write(const char *buf, unsigned int len)
{
    while (len--) {
        /* Wait for TX empty */
        while ((UART_ISR(UART_BASE) & UART_ISR_TX_EMPTY) == 0)
            ;
        UART_TDR(UART_BASE) = *buf;
        buf++;
    }
}
#endif /* DEBUG_UART */

void hal_init(void)
{
    clock_pll_on(0);

#ifdef DEBUG_UART
    uart_init(115200, 8, 'N', 1);
    uart_write("wolfBoot Init\n", 14);
#endif
}

#ifdef FLASH_SECURABLE_MEMORY_SUPPORT
static void RAMFUNCTION do_secure_boot(void)
{
    uint32_t sec_size = (FLASH_SECR & FLASH_SECR_SEC_SIZE_MASK);

    /* The "SEC_SIZE" is the number of pages (2KB) to extend from base 0x8000000
     * and it is programmed using the STM32CubeProgrammer option bytes.
     * Example: STM32_Programmer_CLI -c port=swd mode=hotplug -ob SEC_SIZE=  */
#ifndef NO_FLASH_SEC_SIZE_CHECK
    /* Make sure at least the first sector is protected and the size is not
     * larger than boot partition */
    if (sec_size <= 1 ||
        sec_size > (WOLFBOOT_PARTITION_BOOT_ADDRESS / WOLFBOOT_SECTOR_SIZE)) {
        /* panic: invalid sector size */
        while(1)
            ;
    }
#endif

    /* TODO: Add checks for WRP, RDP and BootLock. Add warning to help lock down
     *       target in production */

    /* unlock flash to access FLASH_CR write */
    hal_flash_unlock();

    ISB();

    /* Activate secure user memory */
    /* secure code to make sure SEC_PROT gets set (based on reference code) */
    do {
        FLASH_CR |= FLASH_CR_SEC_PROT;
    } while ((FLASH_CR & FLASH_CR_SEC_PROT) == 0);

    DSB();
}
#endif

void RAMFUNCTION hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_flash_release();
#endif
#ifdef WOLFBOOT_RESTORE_CLOCK
    clock_pll_off();
#endif
#ifdef FLASH_SECURABLE_MEMORY_SUPPORT
    do_secure_boot();
#endif
}
