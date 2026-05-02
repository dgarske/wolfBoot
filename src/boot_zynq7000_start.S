/* boot_zynq7000_start.S
 *
 * Cortex-A9 (ARMv7-A 32-bit) startup for Xilinx Zynq-7000.
 *
 * Replaces src/boot_arm32_start.S for the zynq7000 target. Performs the
 * minimum CPU setup that every Zynq-7000 standalone image needs before
 * running C code, modeled after the Xilinx standalone BSP boot.S
 * (data/embeddedsw/lib/bsp/standalone_v9_4/src/arm/cortexa9/gcc/boot.S).
 *
 *   1. mask IRQ + FIQ
 *   2. set VBAR to wolfBoot's vector table (so aborts route to us, not
 *      to whatever FSBL/BootROM left vectors pointing at)
 *   3. disable MMU, L1 I/D caches (we run flat, uncached, deterministic)
 *   4. invalidate TLB, I-cache, branch predictor, L1 D-cache by set/way
 *   5. set up stack pointer for SVC/IRQ/FIQ/ABT/UND modes
 *   6. clear .bss, jump to main()
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

    .arm
    .section start, "ax"

    .globl reset_vector_entry
    .align 4
reset_vector_entry:
_vector_table:
    b   isr_reset           /* 0x00 reset */
    b   isr_undef           /* 0x04 undefined */
    b   isr_swi             /* 0x08 swi */
    b   isr_pabt            /* 0x0C prefetch abort */
    b   isr_dabt            /* 0x10 data abort */
    .word _romsize          /* 0x14 (size word, kept for FSBL header parity) */
    b   isr_irq             /* 0x18 IRQ */
    b   isr_fiq             /* 0x1C FIQ */

isr_undef:  b isr_undef
isr_swi:    b isr_swi
isr_pabt:   b isr_pabt
isr_dabt:   b isr_dabt
isr_irq:    b isr_irq
isr_fiq:    b isr_fiq

isr_reset:
    /* 1. Mask IRQ + FIQ, force SVC mode */
    cpsid   if
    mrs     r0, cpsr
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13           /* SVC mode */
    msr     cpsr_c, r0

    /* 2. Set VBAR to our vector table (the load address). */
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0
    /* Adjust SCTLR: keep MMU (M) ON to inherit FSBL's flat DDR mapping -
     * disabling MMU would treat all memory as Strongly-Ordered, causing
     * unaligned LDR/STR (e.g. newlib memcpy from 2-byte aligned strings)
     * to fault. Clear A (alignment check), V (high vectors), and the
     * cache enables for deterministic behavior. */
    mrc     p15, 0, r1, c1, c0, 0
    bic     r1, r1, #(1 << 13)      /* V bit (high vectors) */
    bic     r1, r1, #(1 << 1)       /* A bit (alignment fault check) */
    bic     r1, r1, #(1 << 2)       /* C bit (D-cache) */
    bic     r1, r1, #(1 << 12)      /* I bit (I-cache) */
    mcr     p15, 0, r1, c1, c0, 0
    dsb
    isb

    /* 4. Invalidate TLB, I-cache, branch predictor. Leave D-cache alone -
     * FSBL left it consistent with DDR; cleaning by set/way here is risky
     * since 'dirty' lines from FSBL must be cleaned, not just invalidated. */
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0   /* TLBIALL */
    mcr     p15, 0, r0, c7, c5, 0   /* ICIALLU */
    mcr     p15, 0, r0, c7, c5, 6   /* BPIALL */
    dsb
    isb

    /* 5. Set stack pointers for IRQ/FIQ/ABT/UND/SVC modes.
     * Each mode gets a 1 KB slice carved out from below _stack_top:
     *   _stack_top      <- top
     *      0x000        SVC (sys/usr) - main wolfBoot stack (largest)
     *      0x800        IRQ
     *      0xC00        FIQ
     *      0x1000       ABT
     *      0x1400       UND
     *      0x1800       reserved
     */
    mrs     r0, cpsr
    bic     r0, r0, #0x1f

    orr     r1, r0, #0x12           /* IRQ */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x800)

    orr     r1, r0, #0x11           /* FIQ */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0xC00)

    orr     r1, r0, #0x17           /* ABT */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x1000)

    orr     r1, r0, #0x1b           /* UND */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x1400)

    orr     r1, r0, #0x13           /* SVC (where main runs) */
    msr     cpsr_c, r1
    ldr     sp, =_stack_top

    /* Save BootROM r4 (unused now but kept for parity with arm32 path). */
    push    {r4}

    /* 6. Copy .data section (LMA -> VMA). LMA == VMA in our linker
     * script, so this loop is a no-op, but keep the symmetry. */
    ldr     r2, =_lp_data
    ldmia   r2, {r1, r3, r4}
1:  cmp     r3, r4
    ldrcc   r2, [r1], #4
    strcc   r2, [r3], #4
    bcc     1b

    /* Zero .bss */
    adr     r2, _lp_bss
    ldmia   r2, {r3, r4}
    mov     r2, #0
1:  cmp     r3, r4
    strcc   r2, [r3], #4
    bcc     1b

    /* Enable async-abort delivery so we get an exception now, not later */
    mrs     r0, cpsr
    bic     r0, r0, #(1 << 8)       /* clear A bit -> async aborts unmasked */
    msr     cpsr_xsf, r0

    /* Jump to main(). */
    ldr     r4, =main
    mov     lr, pc
    bx      r4

_panic:
    b       _panic

/* L1 D-cache clean+invalidate by set/way - ARMv7-A canonical sequence. */
_z7_invalidate_dcache_all:
    push    {r4-r11, lr}
    dmb     sy
    mrc     p15, 1, r0, c0, c0, 1   /* CLIDR */
    ands    r3, r0, #0x07000000
    mov     r3, r3, lsr #23
    beq     2f
    mov     r10, #0
1:  add     r2, r10, r10, lsr #1
    mov     r1, r0, lsr r2
    and     r1, r1, #7
    cmp     r1, #2
    blt     1f
    mcr     p15, 2, r10, c0, c0, 0  /* CSSELR */
    isb
    mrc     p15, 1, r1, c0, c0, 0   /* CCSIDR */
    and     r2, r1, #7
    add     r2, r2, #4              /* line size offset */
    ldr     r4, =0x3FF
    ands    r4, r4, r1, lsr #3      /* assoc */
    clz     r5, r4
    ldr     r7, =0x7FFF
    ands    r7, r7, r1, lsr #13     /* num sets */
3:  mov     r9, r4
4:  orr     r11, r10, r9, lsl r5
    orr     r11, r11, r7, lsl r2
    mcr     p15, 0, r11, c7, c6, 2  /* DCISW */
    subs    r9, r9, #1
    bge     4b
    subs    r7, r7, #1
    bge     3b
1:  add     r10, r10, #2
    cmp     r3, r10
    bgt     1b
2:  mov     r10, #0
    mcr     p15, 2, r10, c0, c0, 0  /* CSSELR */
    dsb     sy
    isb
    pop     {r4-r11, pc}

    .align
_lp_data:
    .word   _start_data
    .word   _end_data
_lp_bss:
    .word   _start_bss
    .word   _end_bss
