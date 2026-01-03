/* versal.ld
 *
 * Linker script for wolfBoot on AMD Versal ACAP
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 *
 * Memory Layout:
 *   DDR Low:  0x0000_0000 - 0x7FFF_FFFF (2GB)
 *   DDR High: 0x8_0000_0000 - ...       (extended)
 *
 * wolfBoot is loaded by PLM to DDR at WOLFBOOT_ORIGIN
 *
 * Copyright (C) 2025 wolfSSL Inc.
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_vector_table)

/* Stack and heap sizes */
_STACK_SIZE = DEFINED(_STACK_SIZE) ? _STACK_SIZE : 0x10000;
_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x10000;

_EL0_STACK_SIZE = DEFINED(_EL0_STACK_SIZE) ? _EL0_STACK_SIZE : 1024;
_EL1_STACK_SIZE = DEFINED(_EL1_STACK_SIZE) ? _EL1_STACK_SIZE : 2048;
_EL2_STACK_SIZE = DEFINED(_EL2_STACK_SIZE) ? _EL2_STACK_SIZE : 1024;

/* Memory regions */
MEMORY
{
    /* DDR memory - wolfBoot loaded at 1MB offset */
    /* Reserve 2MB for wolfBoot */
    DDR (rwx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = 2M
}

/* Sections */
SECTIONS
{
    /* Code section */
    .text : {
        _start_text = .;

        /* Vector table and startup code */
        KEEP (*(.vectors))
        *(.boot)
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        *(.plt)
        *(.gnu_warning)
        *(.gcc_execpt_table)
        *(.glue_7)
        *(.glue_7t)
        *(.ARM.extab)
        *(.gnu.linkonce.armextab.*)

        . = ALIGN(8);
        _end_text = .;
    } > DDR

    .init (ALIGN(64)) : {
        KEEP (*(.init))
    } > DDR

    .fini (ALIGN(64)) : {
        KEEP (*(.fini))
    } > DDR

    /* Read-only data */
    .rodata : {
        . = ALIGN(64);
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        __rodata_end = .;
    } > DDR

    /* Initialized data */
    .data : {
        . = ALIGN(64);
        _start_data = .;
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        *(.jcr)
        *(.got)
        *(.got.plt)
        _end_data = .;
        __data_end = .;
    } > DDR

    .got : {
        *(.got)
    } > DDR

    .got1 : {
        *(.got1)
    } > DDR

    .got2 : {
        *(.got2)
    } > DDR

    /* MMU tables (if needed) */
    .mmu_tbl0 (ALIGN(4096)) : {
        __mmu_tbl0_start = .;
        *(.mmu_tbl0)
        __mmu_tbl0_end = .;
    } > DDR

    .mmu_tbl1 (ALIGN(4096)) : {
        __mmu_tbl1_start = .;
        *(.mmu_tbl1)
        __mmu_tbl1_end = .;
    } > DDR

    .mmu_tbl2 (ALIGN(4096)) : {
        __mmu_tbl2_start = .;
        *(.mmu_tbl2)
        __mmu_tbl2_end = .;
    } > DDR

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        *(.gnu.linkonce.armexidix.*.*)
        __exidx_end = .;
    } > DDR

    /* Small data */
    .sdata : {
        . = ALIGN(64);
        __sdata_start = .;
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.s.*)
        __sdata_end = .;
    } > DDR

    /* RAM functions section */
    .ramcode : {
        . = ALIGN(8);
        _start_ramcode = .;
        *(.ramcode)
        *(.ramcode.*)
        . = ALIGN(8);
        _end_ramcode = .;
    } > DDR

    /* Small BSS */
    .sbss (NOLOAD) : {
        . = ALIGN(64);
        __sbss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.sb.*)
        . = ALIGN(64);
        __sbss_end = .;
    } > DDR

    /* Uninitialized data (BSS) */
    .bss (NOLOAD) : {
        . = ALIGN(64);
        __bss_start__ = .;
        _start_bss = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(64);
        __bss_end__ = .;
        _end_bss = .;
    } > DDR

    /* Heap */
    .heap (NOLOAD) : {
        . = ALIGN(64);
        _heap = .;
        __heap_start = .;
        . += _HEAP_SIZE;
        __heap_end = .;
    } > DDR

    /* Stack - separate stacks for each exception level */
    .stack (NOLOAD) : {
        . = ALIGN(64);
        _el3_stack_end = .;
        . += _STACK_SIZE;
        __el3_stack = .;
        _el2_stack_end = .;
        . += _EL2_STACK_SIZE;
        . = ALIGN(64);
        __el2_stack = .;
        _el1_stack_end = .;
        . += _EL1_STACK_SIZE;
        . = ALIGN(64);
        __el1_stack = .;
        _el0_stack_end = .;
        . += _EL0_STACK_SIZE;
        . = ALIGN(64);
        __el0_stack = .;
    } > DDR

    _end = .;
}

/* Provide symbols for startup code */
PROVIDE(_start_vector = _start_text);
PROVIDE(__stack = __el3_stack);
PROVIDE(_stack_base = .);
