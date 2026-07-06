/* sleep.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

/* Minimal Xilinx-compatible <sleep.h> shim for the board-supplied
 * psu_init_gpl.c. Implemented in hal/zynqmp_psu_shim.c using the ARMv8
 * generic timer (CNTPCT/CNTFRQ), matching how the Xilinx FSBL provides
 * usleep()/sleep() during early init. */

#ifndef WOLFBOOT_ZYNQMP_SLEEP_H
#define WOLFBOOT_ZYNQMP_SLEEP_H

void usleep(unsigned long useconds);
unsigned int sleep(unsigned int seconds);

#endif /* WOLFBOOT_ZYNQMP_SLEEP_H */
