/* zynqmp_atf.h
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

/* ARM Trusted Firmware (BL31) handoff for wolfBoot running as the ZynqMP FSBL
 * replacement (Milestone 1). wolfBoot loads + verifies BL31 and the next
 * normal-world image (BL33: U-Boot or the Linux kernel) with its own keys,
 * then hands off to BL31 at EL3. BL31 stays resident as the EL3 monitor and
 * drops BL33 to the requested lower exception level. */

#ifndef ZYNQMP_ATF_H
#define ZYNQMP_ATF_H

#include <stdint.h>

/* Target exception level for the BL33 (normal-world) image. */
#define ZYNQMP_ATF_EL1  1U
#define ZYNQMP_ATF_EL2  2U

/* Build the BL31 handoff parameters for the given normal-world (BL33) entry
 * point and exception level, publish the parameter-block address to the
 * PMU_GLOBAL scratch register that BL31 reads, and hand off to BL31 at EL3.
 * Does not return.
 *
 * bl31_entry: EL3 entry point of the loaded BL31 image (from its ELF entry).
 * bl33_entry: entry point of the next normal-world image BL31 will start.
 * dts_addr:   device tree address for BL33. The standard ATF handoff block has
 *             no argument fields and stock ZynqMP TF-A enters BL33 with x0=0,
 *             so for a direct (no U-Boot) Linux BL33 wolfBoot also publishes
 *             dts_addr in PMU_GLOBAL.GLOBAL_GEN_STORAGE5. A small TF-A patch
 *             must read that register into the BL33 entrypoint x0; pass 0 when
 *             BL33 finds its own DTB (e.g. U-Boot). See docs.
 * bl33_el:    ZYNQMP_ATF_EL1 or ZYNQMP_ATF_EL2. */
void zynqmp_atf_handoff(uintptr_t bl31_entry, uintptr_t bl33_entry,
    uintptr_t dts_addr, uint32_t bl33_el);

#endif /* ZYNQMP_ATF_H */
