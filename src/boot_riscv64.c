/* boot_riscv64.c
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

/* RISC-V 64-bit boot code for PolarFire SoC MPFS250 */

#include <stdint.h>

#include "image.h"
#include "loader.h"

extern void trap_entry(void);
extern void trap_exit(void);

extern uint64_t  _start_vector;
extern uint64_t  _stored_data;
extern uint64_t  _start_data;
extern uint64_t  _end_data;
extern uint64_t  _start_bss;
extern uint64_t  _end_bss;
extern uint64_t  _end_stack;
extern uint64_t  _start_heap;
extern uint64_t  _global_pointer;
extern void (* const IV[])(void);

extern void main(void);

void RAMFUNCTION reloc_iv(const uint32_t *address)
{
    /* Set machine trap vector to address */
    asm volatile("csrw mtvec, %0":: "r"(address + 1));
}

void __attribute__((naked,section(".init"))) _reset(void) {
    register uint64_t *src, *dst;

    /* Initialize global pointer for RISC-V */
    asm volatile("la gp, _global_pointer");

    /* Initialize stack pointer */
    asm volatile("la sp, _end_stack");

    /* Set up vectored interrupt, with IV starting at offset */
    asm volatile("csrw mtvec, %0":: "r"((uint8_t *)(&_start_vector) + 1));

    /* Copy the .data section from flash to RAM */
    src = (uint64_t *) &_stored_data;
    dst = (uint64_t *) &_start_data;
    while (dst < (uint64_t *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (uint64_t *)&_end_bss) {
        *dst = 0UL;
        dst++;
    }

    /* Run wolfboot */
    main();

    /* Should never return */
    wolfBoot_panic();
}

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
    /* Relocate interrupt vector to application */
    reloc_iv(app_offset);

    /* Jump to application entry point */
    asm volatile("jr %0":: "r"((uint8_t *)(app_offset)));
}

void isr_empty(void)
{
    /* Empty interrupt handler */
}

#ifdef RAM_CODE
/* TODO: Add reboot implementation for PolarFire SoC if needed */
void RAMFUNCTION arch_reboot(void)
{
    /* TODO: Implement reboot using watchdog or system reset */
    while(1)
        ;
    wolfBoot_panic();
}
#endif /* RAM_CODE */

