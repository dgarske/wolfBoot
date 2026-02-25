/* startup_riscv.c
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
#include "hal/riscv.h"

extern void trap_entry(void);
extern void trap_exit(void);

extern uint32_t  _start_vector;
extern uint32_t  _stored_data;
extern uint32_t  _start_data;
extern uint32_t  _end_data;
extern uint32_t  _start_bss;
extern uint32_t  _end_bss;
extern uint32_t  _end_stack;
extern uint32_t  _start_heap;
extern uint32_t  _global_pointer;
extern void (* const IV[])(void);

extern void main(void);
void __attribute__((naked,section(".init"))) _reset(void) {
    register uint32_t *src, *dst;
    asm volatile("la gp, _global_pointer");
    asm volatile("la sp, _end_stack");

    /* Direct UART diagnostic: write "!\r\n" to confirm test-app is running.
     * MPFS MMUART: THR at offset 0x100, LSR at offset 0x14, THRE = bit 5. */
    asm volatile(
        "li a0, 0x20000000\n"    /* UART0 base */
        /* write '!' */
        "1: lbu a1, 0x14(a0)\n"  /* read LSR */
        "andi a1, a1, 0x20\n"    /* check THRE (bit 5) */
        "beqz a1, 1b\n"
        "li a2, 0x21\n"          /* '!' */
        "sb a2, 0x100(a0)\n"     /* write to THR */
        /* write '\r' */
        "2: lbu a1, 0x14(a0)\n"
        "andi a1, a1, 0x20\n"
        "beqz a1, 2b\n"
        "li a2, 0x0d\n"          /* '\r' */
        "sb a2, 0x100(a0)\n"
        /* write '\n' */
        "3: lbu a1, 0x14(a0)\n"
        "andi a1, a1, 0x20\n"
        "beqz a1, 3b\n"
        "li a2, 0x0a\n"          /* '\n' */
        "sb a2, 0x100(a0)\n"
        ::: "a0", "a1", "a2"
    );

    /* Set up M-mode vectored interrupt table.
     * wolfBoot M-mode does a direct jr (no enter_smode), so payload runs in M-mode.
     * Use mtvec. The +1 sets MODE=1 (vectored). */
    asm volatile("csrw mtvec, %0":: "r"((uint8_t *)(&_start_vector) + 1));

    src = (uint32_t *) &_stored_data;
    dst = (uint32_t *) &_start_data;
    /* Copy the .data section from flash to RAM. */
    while (dst < (uint32_t *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (uint32_t *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfboot */
    main();
    while(1)
        ;
}

void do_boot(const uint32_t *app_offset)
{

}

static uint32_t synctrap_cause = 0;
void __attribute__((naked)) isr_synctrap(void)
{
    /* Use mcause: payload runs in M-mode (wolfBoot does M-mode direct jump) */
    asm volatile("csrr %0, mcause" : "=r"(synctrap_cause));
}

void isr_empty(void)
{

}
