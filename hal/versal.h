/* versal.h
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
 * AMD Versal ACAP HAL definitions for wolfBoot
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 */

#ifndef _VERSAL_H_
#define _VERSAL_H_

/* Only include C headers when compiling C code, not assembly */
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif /* __ASSEMBLER__ */

/* ============================================================================
 * Exception Level Configuration
 * ============================================================================
 * Versal PLM (Platform Loader Manager) can hand off at EL3, EL2, or EL1
 * depending on configuration. Default is EL2 (hypervisor mode).
 */
#ifndef USE_BUILTIN_STARTUP

#ifndef EL3_SECURE
#define EL3_SECURE     0
#endif
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR 1
#endif
#ifndef EL1_NONSECURE
#define EL1_NONSECURE  0
#endif

#ifndef HYP_GUEST
#define HYP_GUEST      0
#endif

#ifndef FPU_TRAP
#define FPU_TRAP       0
#endif

/* ARM Errata */
#define CONFIG_ARM_ERRATA_855873 1

#endif /* USE_BUILTIN_STARTUP */


/* ============================================================================
 * Memory Map
 * ============================================================================
 * Versal memory map (simplified):
 *   0x0000_0000 - 0x7FFF_FFFF : DDR Low (2GB)
 *   0x8_0000_0000 - ...       : DDR High (extended)
 *   0xF000_0000 - 0xFFFF_FFFF : LPD/FPD Peripherals
 */

/* DDR Memory */
#define VERSAL_DDR_0_BASE       0x00000000UL
#define VERSAL_DDR_0_HIGH       0x7FFFFFFFUL
#define VERSAL_DDR_1_BASE       0x800000000ULL
#define VERSAL_DDR_1_HIGH       0x87FFFFFFFULL


/* ============================================================================
 * UART (Cadence UART - same IP as ZynqMP)
 * ============================================================================
 * Versal has 2 UART controllers in the LPD (Low Power Domain)
 */

#define VERSAL_UART0_BASE       0xFF000000UL
#define VERSAL_UART1_BASE       0xFF010000UL

/* Select UART based on DEBUG_UART_NUM */
#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    #define DEBUG_UART_BASE     VERSAL_UART1_BASE
#else
    #define DEBUG_UART_BASE     VERSAL_UART0_BASE
#endif

/* UART Register Offsets */
#define UART_CR_OFFSET          0x00    /* Control Register */
#define UART_MR_OFFSET          0x04    /* Mode Register */
#define UART_IER_OFFSET         0x08    /* Interrupt Enable */
#define UART_IDR_OFFSET         0x0C    /* Interrupt Disable */
#define UART_IMR_OFFSET         0x10    /* Interrupt Mask */
#define UART_ISR_OFFSET         0x14    /* Interrupt Status */
#define UART_BAUDGEN_OFFSET     0x18    /* Baud Rate Generator */
#define UART_RXTOUT_OFFSET      0x1C    /* Receiver Timeout */
#define UART_RXWM_OFFSET        0x20    /* Receiver FIFO Trigger Level */
#define UART_MODEMCR_OFFSET     0x24    /* Modem Control */
#define UART_MODEMSR_OFFSET     0x28    /* Modem Status */
#define UART_SR_OFFSET          0x2C    /* Channel Status */
#define UART_FIFO_OFFSET        0x30    /* TX/RX FIFO */
#define UART_BAUDDIV_OFFSET     0x34    /* Baud Rate Divider */
#define UART_FLOWDEL_OFFSET     0x38    /* Flow Control Delay */
#define UART_TXWM_OFFSET        0x44    /* Transmitter FIFO Trigger Level */

/* UART Register Access Macros */
#define UART_REG(offset)        (*((volatile uint32_t*)(DEBUG_UART_BASE + (offset))))

#define UART_CR                 UART_REG(UART_CR_OFFSET)
#define UART_MR                 UART_REG(UART_MR_OFFSET)
#define UART_IDR                UART_REG(UART_IDR_OFFSET)
#define UART_ISR                UART_REG(UART_ISR_OFFSET)
#define UART_SR                 UART_REG(UART_SR_OFFSET)
#define UART_FIFO               UART_REG(UART_FIFO_OFFSET)
#define UART_BAUDGEN            UART_REG(UART_BAUDGEN_OFFSET)
#define UART_BAUDDIV            UART_REG(UART_BAUDDIV_OFFSET)
#define UART_RXTOUT             UART_REG(UART_RXTOUT_OFFSET)
#define UART_RXWM               UART_REG(UART_RXWM_OFFSET)
#define UART_TXWM               UART_REG(UART_TXWM_OFFSET)

/* UART Control Register bits */
#define UART_CR_STOPBRK         (1UL << 8)  /* Stop TX break */
#define UART_CR_STARTBRK        (1UL << 7)  /* Start TX break */
#define UART_CR_TORST           (1UL << 6)  /* Restart RX timeout counter */
#define UART_CR_TX_DIS          (1UL << 5)  /* TX disable */
#define UART_CR_TX_EN           (1UL << 4)  /* TX enable */
#define UART_CR_RX_DIS          (1UL << 3)  /* RX disable */
#define UART_CR_RX_EN           (1UL << 2)  /* RX enable */
#define UART_CR_TXRST           (1UL << 1)  /* TX logic reset */
#define UART_CR_RXRST           (1UL << 0)  /* RX logic reset */

/* UART Mode Register bits */
#define UART_MR_CHMODE_MASK     (3UL << 8)
#define UART_MR_CHMODE_NORM     (0UL << 8)  /* Normal mode */
#define UART_MR_CHMODE_ECHO     (1UL << 8)  /* Auto echo mode */
#define UART_MR_CHMODE_LLOOP    (2UL << 8)  /* Local loopback */
#define UART_MR_CHMODE_RLOOP    (3UL << 8)  /* Remote loopback */
#define UART_MR_NBSTOP_MASK     (3UL << 6)
#define UART_MR_NBSTOP_1        (0UL << 6)  /* 1 stop bit */
#define UART_MR_NBSTOP_1_5      (1UL << 6)  /* 1.5 stop bits */
#define UART_MR_NBSTOP_2        (2UL << 6)  /* 2 stop bits */
#define UART_MR_PAR_MASK        (7UL << 3)
#define UART_MR_PAR_EVEN        (0UL << 3)  /* Even parity */
#define UART_MR_PAR_ODD         (1UL << 3)  /* Odd parity */
#define UART_MR_PAR_SPACE       (2UL << 3)  /* Space parity */
#define UART_MR_PAR_MARK        (3UL << 3)  /* Mark parity */
#define UART_MR_PAR_NONE        (4UL << 3)  /* No parity */
#define UART_MR_CHRL_MASK       (3UL << 1)
#define UART_MR_CHRL_8          (0UL << 1)  /* 8 data bits */
#define UART_MR_CHRL_7          (2UL << 1)  /* 7 data bits */
#define UART_MR_CHRL_6          (3UL << 1)  /* 6 data bits */
#define UART_MR_CLKSEL          (1UL << 0)  /* Clock select */

/* UART Status Register bits */
#define UART_SR_TNFUL           (1UL << 14) /* TX FIFO nearly full */
#define UART_SR_TTRIG           (1UL << 13) /* TX FIFO trigger */
#define UART_SR_FLOWDEL         (1UL << 12) /* Flow delay trigger */
#define UART_SR_TACTIVE         (1UL <<  11)/* TX active */
#define UART_SR_RACTIVE         (1UL << 10) /* RX active */
#define UART_SR_TXFULL          (1UL << 4)  /* TX FIFO full */
#define UART_SR_TXEMPTY         (1UL << 3)  /* TX FIFO empty */
#define UART_SR_RXFULL          (1UL << 2)  /* RX FIFO full */
#define UART_SR_RXEMPTY         (1UL << 1)  /* RX FIFO empty */
#define UART_SR_RTRIG           (1UL << 0)  /* RX FIFO trigger */

/* UART ISR bits (for clearing/masking) */
#define UART_ISR_MASK           0x3FFFU

/* UART Configuration */
#ifndef UART_CLK_REF
    #define UART_CLK_REF        100000000UL  /* 100 MHz reference clock */
#endif

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
#endif


/* ============================================================================
 * System Timer (ARM Generic Timer)
 * ============================================================================
 * Versal uses ARM Generic Timer accessed via system registers
 */

/* Timer frequency (typically configured by PLM) */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ          100000000UL  /* 100 MHz default */
#endif


/* ============================================================================
 * GIC (Generic Interrupt Controller)
 * ============================================================================
 */
#define VERSAL_GIC_BASE         0xF9000000UL
#define VERSAL_GICD_BASE        (VERSAL_GIC_BASE + 0x00000)  /* Distributor */
#define VERSAL_GICC_BASE        (VERSAL_GIC_BASE + 0x40000)  /* CPU Interface */
#define VERSAL_GICH_BASE        (VERSAL_GIC_BASE + 0x60000)  /* Virtual Interface Control */
#define VERSAL_GICV_BASE        (VERSAL_GIC_BASE + 0x80000)  /* Virtual CPU Interface */


/* ============================================================================
 * Clock and Reset (CRL/CRF)
 * ============================================================================
 */
#define VERSAL_CRL_BASE         0xFF5E0000UL  /* Clock and Reset LPD */
#define VERSAL_CRF_BASE         0xFD1A0000UL  /* Clock and Reset FPD */


/* ============================================================================
 * PMC (Platform Management Controller)
 * ============================================================================
 * The PMC is the security controller in Versal (replaces CSU from ZynqMP)
 */
#define VERSAL_PMC_GLOBAL_BASE  0xF1110000UL
#define VERSAL_PMC_TAP_BASE     0xF11A0000UL


/* ============================================================================
 * OSPI (Octal SPI) Flash Controller
 * ============================================================================
 * Versal uses OSPI instead of QSPI (though QSPI mode is supported)
 * Stub definitions - to be implemented
 */
#define VERSAL_OSPI_BASE        0xF1010000UL


/* ============================================================================
 * SD/eMMC Controller (SDHCI)
 * ============================================================================
 * Versal has 2 SD/eMMC controllers
 */
#define VERSAL_SD0_BASE         0xF1040000UL
#define VERSAL_SD1_BASE         0xF1050000UL


/* ============================================================================
 * Helper Functions (C code only)
 * ============================================================================
 */
#ifndef __ASSEMBLER__

/* Get current exception level */
static inline unsigned int current_el(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (el));
    return (unsigned int)((el >> 2) & 0x3);
}

/* Memory barrier */
static inline void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

#endif /* __ASSEMBLER__ */

#endif /* _VERSAL_H_ */

