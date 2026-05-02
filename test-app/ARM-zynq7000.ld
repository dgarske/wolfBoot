OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

/* App is staged by wolfBoot to DDR at WOLFBOOT_LOAD_ADDRESS=0x10000000.
 * Stack carved out from the half-MB region just above. */
MEMORY
{
    DDR_MEM(rwx)  : ORIGIN = 0x10000000, LENGTH = 0x00080000  /* 512 KB code/data/bss */
    STACK_MEM(rw) : ORIGIN = 0x10080000, LENGTH = 0x00080000  /* 512 KB stack */
}

ENTRY(reset_vector_entry)
SECTIONS
{
    .text : AT (ORIGIN(DDR_MEM)) {
        _start_text = .;
        *(.iv)
        *(.text)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        *(.glue_7)
        . = ALIGN(4);
        *(.eh_frame)
        . = ALIGN(4);
        _end_text = .;
    }

    . = ALIGN(4);
    .dummy : {
        _edummy = .;
    }

    .data : AT (LOADADDR(.dummy)) {
        _start_data = .;
        *(.vectors)
        *(.data)
        _end_data = .;
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _start_bss = .;
        *(.bss)
        _end_bss = .;
    }
}
_romsize = _end_data - _start_text;
_sramsize = _end_bss - _start_text;
END_STACK = _start_text;
_stack_top = ORIGIN(STACK_MEM) + LENGTH(STACK_MEM);
end = .;
