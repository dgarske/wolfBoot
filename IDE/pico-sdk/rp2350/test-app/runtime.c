#include <stdint.h>
typedef void (*preinit_fn_t)(void);

void runtime_init_cpasr(void)
{
    volatile uint32_t *cpasr_ns = (volatile uint32_t*) 0xE000ED88;
    *cpasr_ns |= 0xFF;
}

preinit_fn_t __attribute__((section(".nonsecure_preinit_array"))) nonsecure_preinit[] = 
             { &runtime_init_cpasr };
