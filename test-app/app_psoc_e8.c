/* app_psoc_e8.c
 *
 * Test bare-metal application for Infineon PSOC Edge E8.
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

#include <stdint.h>
#include "hal.h"
#include "hal/psoc_e8.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#ifdef WOLFCRYPT_TZ_WOLFHSM
#include "wcs/wolfhsm_test.h"
#endif
#ifdef WOLFCRYPT_TEST
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif
#ifdef WOLFCRYPT_BENCHMARK
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

/* Application Interrupt and Reset Control. Several test applications carry
 * their own copy of these; they are duplicated rather than shared because the
 * apps that already define them do not all include a common header. */
#define AIRCR             (*(volatile uint32_t *)(0xE000ED0C))
#define AIRCR_VKEY        (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

/* With TrustZone the application is Non-secure and the update API is a set
 * of Secure gateway calls. */
#ifdef TZEN
#define APP_FW_VERSION()     wolfBoot_nsc_current_firmware_version()
#define APP_SUCCESS()        wolfBoot_nsc_success()
#define APP_UPDATE_TRIGGER() wolfBoot_nsc_update_trigger()
#else
#define APP_FW_VERSION()     wolfBoot_current_firmware_version()
#define APP_SUCCESS()        wolfBoot_success()
#define APP_UPDATE_TRIGGER() wolfBoot_update_trigger()
#endif

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int len);

#ifdef WOLFCRYPT_TZ_WOLFHSM
/* newlib stdout for the wolfHSM client tests. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    uart_write(ptr, (unsigned int)len);
    return len;
}
#endif

static void uart_print(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void print_dec(uint32_t v)
{
    char tmp[12];
    char num[12];
    int t = 0;
    int n = 0;

    if (v == 0) {
        num[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            num[n++] = tmp[--t];
    }
    uart_write(num, (unsigned int)n);
}

/* Request a system reset, preserving the configured priority grouping. */
static void system_reset(void)
{
    uint32_t prigroup = AIRCR & 0x0700U;

    AIRCR = AIRCR_VKEY | prigroup | AIRCR_SYSRESETREQ;
    while (1)
        ;
}

static void busy_delay(volatile uint32_t ticks)
{
    while (ticks > 0)
        ticks--;
}

void main(void)
{
    uint32_t version;
    int i;

    uart_init();
    uart_print("TEST APP\r\n");

    version = APP_FW_VERSION();
    uart_print("App version: ");
    print_dec(version);
    uart_print("\r\n");

#ifdef WOLFCRYPT_TZ_WOLFHSM
    uart_print("Running wolfHSM client tests...\r\n");
    (void)cmd_wolfhsm_test(NULL);
#endif
#ifdef WOLFCRYPT_TEST
    uart_print("Running wolfCrypt tests...\r\n");
    wolfCrypt_Init();
    wolfcrypt_test(NULL);
    wolfCrypt_Cleanup();
    uart_print("Tests complete.\r\n");
#endif
#ifdef WOLFCRYPT_BENCHMARK
    uart_print("Running wolfCrypt benchmarks...\r\n");
    wolfCrypt_Init();
    benchmark_test(NULL);
    wolfCrypt_Cleanup();
    uart_print("Benchmarks complete.\r\n");
#endif

    /* v1 asks for an update and resets so wolfBoot performs the swap.
     * v2 and later confirm the update so it is not rolled back. */
    if (version >= 2) {
        APP_SUCCESS();
        uart_print("update OK -- success confirmed\r\n");
        while (1)
            busy_delay(1000000);
    }

    /* Pause before asking for the update. Without a window here the reset
     * that follows comes round fast enough to interrupt a debugger trying to
     * program the update partition. */
    for (i = 0; i < 5; i++) {
        busy_delay(4000000);
        uart_print(".");
    }
    uart_print("\r\ntriggering update -> reset\r\n");
    APP_UPDATE_TRIGGER();
    system_reset();
}
