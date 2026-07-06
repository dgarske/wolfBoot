/* zynqmp_psu_shim.c
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

/* Delay primitives backing the <sleep.h> shim used by the board-supplied
 * psu_init_gpl.c when wolfBoot runs as the ZynqMP FSBL replacement.
 *
 * These use the ARMv8 generic timer (CNTPCT_EL0 / CNTFRQ_EL0). The EL3 startup
 * (boot_aarch64_start.S) programs CNTFRQ_EL0 and the system counter runs from
 * the PS reference clock out of reset, so these are usable from the very first
 * psu_init() stage, before the PLLs/clocks have been reprogrammed. */

#include "sleep.h"
#include "xil_io.h"

/* Xilinx BSP logging shim. The board psu_init_gpl.c calls xil_printf() on a few
 * error paths; psu_init runs before the UART is initialized, so this is a
 * no-op. Route to wolfBoot_printf() once the UART is up if you need the
 * diagnostics. */
int xil_printf(const char* ctrl1, ...)
{
    (void)ctrl1;
    return 0;
}

/* ZynqMP IOU system timestamp counter (drives CNTPCT_EL0) and its fixed
 * timestamp clock frequency (XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ = 100MHz,
 * set by psu_clock_init_data's TIMESTAMP_REF_CTRL). */
#define IOU_SCNTRS_CNT_CONTROL   0xFF260000UL
#define IOU_SCNTRS_FREQ_REG      0xFF260020UL
#define IOU_SCNTRS_EN            0x00000001UL
#ifndef ZYNQMP_TIMESTAMP_HZ
#define ZYNQMP_TIMESTAMP_HZ      100000000UL
#endif

/* Equivalent of the Xilinx BSP XTime_StartTimer(): enable the IOU_SCNTRS
 * system counter (so CNTPCT_EL0 increments) if it is not already enabled. The
 * BootROM/early psu_init leaves it DISABLED -- the generated psu_init only
 * enables it in psu_peripherals_init_data, which runs AFTER DDR training --
 * yet psu_ddr_phybringup_data needs working usleep delays. The Xilinx BSP
 * usleep calls this on every delay for exactly this reason. */
static void zynqmp_start_timer(void)
{
    if ((Xil_In32(IOU_SCNTRS_CNT_CONTROL) & IOU_SCNTRS_EN) != IOU_SCNTRS_EN) {
        Xil_Out32(IOU_SCNTRS_FREQ_REG, ZYNQMP_TIMESTAMP_HZ);
        Xil_Out32(IOU_SCNTRS_CNT_CONTROL, IOU_SCNTRS_EN);
    }
}

static unsigned long psu_timer_count(void)
{
    unsigned long cntpct;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
    return cntpct;
}

void usleep(unsigned long useconds)
{
    unsigned long start;
    unsigned long ticks;
    unsigned long freq = ZYNQMP_TIMESTAMP_HZ;

#ifdef WOLFBOOT_ZYNQMP_FSBL
    /* Match the Xilinx BSP: ensure the system counter is running before use. */
    zynqmp_start_timer();
#endif
#ifdef ZYNQMP_USLEEP_SCALE
    useconds *= (unsigned long)(ZYNQMP_USLEEP_SCALE);
#endif
    start = psu_timer_count();
    /* ticks = useconds * freq / 1e6, split to avoid 64-bit overflow for the
     * small delays psu_init uses. */
    ticks = (useconds / 1000000UL) * freq
          + ((useconds % 1000000UL) * freq) / 1000000UL;

    while ((psu_timer_count() - start) < ticks) {
        /* busy-wait */
    }
}

unsigned int sleep(unsigned int seconds)
{
    unsigned int i;
    for (i = 0; i < seconds; i++) {
        usleep(1000000UL);
    }
    return 0;
}

#ifdef WOLFBOOT_ZYNQMP_FSBL

/* Board psu_init sub-stages (non-static in the XSA-generated psu_init_gpl.c).
 * Declared here so we do not pull in the huge board psu_init_gpl.h. */
extern unsigned long psu_mio_init_data(void);
extern unsigned long psu_peripherals_pre_init_data(void);
extern unsigned long psu_pll_init_data(void);
extern unsigned long psu_clock_init_data(void);
extern unsigned long psu_ddr_init_data(void);
extern unsigned long psu_ddr_phybringup_data(void);
extern unsigned long psu_peripherals_init_data(void);
extern unsigned long psu_resetin_init_data(void);
extern unsigned long psu_serdes_init_data(void);
extern unsigned long psu_resetout_init_data(void);
/* serdes signal-integrity calibration. serdes_fixcal_code() is non-static in
 * the board file; serdes_enb_coarse_saturation() is static (inlined below). */
extern int serdes_fixcal_code(void);
extern unsigned long psu_peripherals_powerdwn_data(void);
extern unsigned long psu_afi_config(void);
extern unsigned long psu_ddr_qos_init_data(void);

/* Diagnostic capture of the early (BootROM) timer/clock state, printed by
 * hal_init() once the UART is up. Only CRL_APB registers are readable this
 * early (the IOU_SCNTRS block is not yet clocked). */
unsigned long zynqmp_dbg_cntfrq_boot = 0;   /* CNTFRQ_EL0 left by the BootROM */
unsigned long zynqmp_dbg_cntfrq_used = 0;   /* value usleep() ends up using   */
unsigned int  zynqmp_dbg_ts_ctrl = 0;       /* CRL TIMESTAMP_REF_CTRL 0xFF5E0128 */
unsigned int  zynqmp_dbg_iopll_ctrl = 0;    /* CRL IOPLL_CTRL        0xFF5E0020 */

static void zynqmp_fix_timer_freq(void)
{
    unsigned long f = 0;

    /* Capture the BootROM timer/clock state BEFORE psu_init reprograms it.
     * Only CRL_APB registers (always clocked) are safe to read this early;
     * the IOU_SCNTRS block (0xFF260000) is NOT accessible until psu_init
     * clocks it and reading it here bus-stalls the core. */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    zynqmp_dbg_cntfrq_boot = f;
    zynqmp_dbg_ts_ctrl    = Xil_In32(0xFF5E0128UL);
    zynqmp_dbg_iopll_ctrl = Xil_In32(0xFF5E0020UL);

    /* The IOU_SCNTRS system counter runs at the fixed 100MHz timestamp clock
     * (set by psu_clock_init_data) once enabled. usleep() enables it and uses
     * 100MHz directly; set CNTFRQ_EL0 to match so hal_get_timer_us()/
     * hal_timer_ms() are also correct. */
    f = ZYNQMP_TIMESTAMP_HZ;
    __asm__ volatile("msr cntfrq_el0, %0\n\t isb" : : "r"(f));
    zynqmp_dbg_cntfrq_used = f;
}

/* Replacement for the board file's psu_init(): mirrors its call sequence (see
 * psu_init_gpl.c) but calls zynqmp_fix_timer_freq() right after clock init so
 * DDR training settle delays are accurate. The static init_serdes()/
 * init_peripheral() are inlined via their non-static sub-stages. Returns 0 on
 * success.
 *
 * PSU_STAGE(n) writes a progress marker to PMU_GLOBAL GEN_STORAGE0
 * (0xFFD80030) before each sub-stage; after a cold-boot hang, read it over
 * JTAG to see the last sub-stage that STARTED. */
#define ZYNQMP_PSU_STAGE_REG  0xFFD80030UL
#define PSU_STAGE(n)          Xil_Out32(ZYNQMP_PSU_STAGE_REG, (u32)(n))

int zynqmp_psu_init(void)
{
    int status = 1;
    u32 smmu;

    PSU_STAGE(0xA000);
    /* Correct CNTFRQ_EL0 up front so EVERY psu_init stage (PLL/clock/DDR) gets
     * accurate usleep settle delays. The BootROM leaves the system counter at
     * the ~1.5 GHz pre-divider rate for the whole psu_init sequence (the /15
     * divisor never re-latches the running counter), so this single correction
     * holds across all stages. */
    zynqmp_fix_timer_freq();

    PSU_STAGE(0xA001); status &= psu_mio_init_data();
    PSU_STAGE(0xA002); status &= psu_peripherals_pre_init_data();
    PSU_STAGE(0xA003); status &= psu_pll_init_data();
    PSU_STAGE(0xA004); status &= psu_clock_init_data();

    PSU_STAGE(0xA005); status &= psu_ddr_init_data();
    PSU_STAGE(0xA006); status &= psu_ddr_phybringup_data();
    PSU_STAGE(0xA007);

    PSU_STAGE(0xA008); status &= psu_peripherals_init_data();

    /* init_serdes() equivalent: PS-GTR (USB/SATA/PCIe/DP) lane bringup. Not
     * needed to boot QSPI/SD -> DDR -> Linux (Linux re-inits the PS-GTR), so
     * skipped by default; define ZYNQMP_PSU_INIT_SERDES to run it -- required
     * if the kernel drives a PS-GTR peripheral (e.g. USB3 dwc3, whose probe
     * else hangs on an unclocked PHY). Replicates the board init_serdes() order:
     * the calibration helpers must run first or the SERDES PLLs never lock.
     * serdes_fixcal_code() is non-static (called); serdes_enb_coarse_saturation()
     * is static so its four writes are inlined; serdes_illcalib() runs inside
     * psu_serdes_init_data(). */
#ifdef ZYNQMP_PSU_INIT_SERDES
    PSU_STAGE(0xA009);
    status &= psu_resetin_init_data();
    PSU_STAGE(0xB000);
    status &= serdes_fixcal_code();
    /* serdes_enb_coarse_saturation(): enable PLL coarse-code saturation logic */
    Xil_Out32(0xFD402094UL, 0x00000010UL);
    Xil_Out32(0xFD406094UL, 0x00000010UL);
    Xil_Out32(0xFD40A094UL, 0x00000010UL);
    Xil_Out32(0xFD40E094UL, 0x00000010UL);
    PSU_STAGE(0xB001); status &= psu_serdes_init_data();
    PSU_STAGE(0xB002); status &= psu_resetout_init_data();
    PSU_STAGE(0xB003);
#endif

    /* init_peripheral(): SMMU interrupt enable (read-modify-write set of
     * bits 0x8000001F, matching the board PSU_Mask_Write). This is NOT part of
     * init_serdes(), so it runs even when the PS-GTR serdes block is skipped. */
    smmu = Xil_In32(0xFD5F0018UL);
    smmu &= ~0x8000001FUL;
    smmu |= 0x8000001FUL;
    Xil_Out32(0xFD5F0018UL, smmu);
    PSU_STAGE(0xB004);

    PSU_STAGE(0xA00A);
    status &= psu_peripherals_powerdwn_data();
    status &= psu_afi_config();
    psu_ddr_qos_init_data();

    PSU_STAGE(0xA0DD);   /* psu_init fully complete */
    if (status == 0)
        return 1;
    return 0;
}

#endif /* WOLFBOOT_ZYNQMP_FSBL */
