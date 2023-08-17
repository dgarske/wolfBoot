/* imx-rt.c
 *
 * Custom HAL implementation. Defines the
 * functions used by wolfboot for a specific target.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stdint.h>
#include <target.h>
#include "image.h"
#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include "fsl_nor_flash.h"
#include "fsl_flexspi.h"

#ifdef CPU_MIMXRT1062DVL6A
#include "evkmimxrt1060_flexspi_nor_config.h"
#define USE_GET_CONFIG
#endif
#ifdef CPU_MIMXRT1052DVJ6B
#include "evkbimxrt1050_flexspi_nor_config.h"
#endif
#ifdef CPU_MIMXRT1064DVL6A
#include "evkmimxrt1064_flexspi_nor_config.h"
#define USE_GET_CONFIG
#endif

#include "xip/fsl_flexspi_nor_boot.h"

#ifdef __WOLFBOOT

/** Built-in ROM API for bootloaders **/

typedef void rtwdog_config_t;
typedef void wdog_config_t;

/* Watchdog structures */
typedef struct
{
    void (*RTWDOG_GetDefaultConfig)(rtwdog_config_t *config);
    void (*RTWDOG_Init)(RTWDOG_Type *base, const rtwdog_config_t *config);
    void (*RTWDOG_Deinit)(RTWDOG_Type *base);
    void (*RTWDOG_Enable)(RTWDOG_Type *base);
    void (*RTWDOG_Disable)(RTWDOG_Type *base);
    void (*RTWDOG_EnableInterrupts)(RTWDOG_Type *base, uint32_t mask);
    void (*RTWDOG_DisableInterrupts)(RTWDOG_Type *base, uint32_t mask);
    uint32_t (*RTWDOG_GetStatusFlags)(RTWDOG_Type *base);
    void (*RTWDOG_ClearStatusFlags)(RTWDOG_Type *base, uint32_t mask);
    void (*RTWDOG_SetTimeoutValue)(RTWDOG_Type *base, uint16_t timeoutCount);
    void (*RTWDOG_SetWindowValue)(RTWDOG_Type *base, uint16_t windowValue);
    void (*RTWDOG_Unlock)(RTWDOG_Type *base);
    void (*RTWDOG_Refresh)(RTWDOG_Type *base);
    uint16_t (*RTWDOG_GetCounterValue)(RTWDOG_Type *base);
} rtwdog_driver_interface_t;

typedef struct
{
    void (*WDOG_GetDefaultConfig)(wdog_config_t *config);
    void (*WDOG_Init)(WDOG_Type *base, const wdog_config_t *config);
    void (*WDOG_Deinit)(WDOG_Type *base);
    void (*WDOG_Enable)(WDOG_Type *base);
    void (*WDOG_Disable)(WDOG_Type *base);
    void (*WDOG_EnableInterrupts)(WDOG_Type *base, uint16_t mask);
    uint16_t (*WDOG_GetStatusFlags)(WDOG_Type *base);
    void (*WDOG_ClearInterruptStatus)(WDOG_Type *base, uint16_t mask);
    void (*WDOG_SetTimeoutValue)(WDOG_Type *base, uint16_t timeoutCount);
    void (*WDOG_SetInterrputTimeoutValue)(WDOG_Type *base, uint16_t timeoutCount);
    void (*WDOG_DisablePowerDownEnable)(WDOG_Type *base);
    void (*WDOG_Refresh)(WDOG_Type *base);
} wdog_driver_interface_t;

/* Flex SPI op */
typedef enum _FlexSPIOperationType
{
    kFlexSpiOperation_Command,
    kFlexSpiOperation_Config,
    kFlexSpiOperation_Write,
    kFlexSpiOperation_Read,
    kFlexSpiOperation_End = kFlexSpiOperation_Read,
} flexspi_operation_t;

/* FLEX SPI Xfer */
typedef struct _FlexSpiXfer
{
    flexspi_operation_t operation;
    uint32_t baseAddress;
    uint32_t seqId;
    uint32_t seqNum;
    bool isParallelModeEnable;
    uint32_t *txBuffer;
    uint32_t txSize;
    uint32_t *rxBuffer;
    uint32_t rxSize;
} flexspi_xfer_t;

/* Serial NOR config option */
typedef struct _serial_nor_config_option
{
    union
    {
        struct
        {
            uint32_t max_freq : 4; /*  Maximum supported Frequency */
            uint32_t misc_mode : 4; /* miscellaneous mode */
            uint32_t quad_mode_setting : 4; /* Quad mode setting */
            uint32_t cmd_pads : 4;  /* Command pads */
            uint32_t query_pads : 4;  /* SFDP read pads */
            uint32_t device_type : 4; /* Device type */
            uint32_t option_size : 4; /* Option size, in terms of uint32_t, size = (option_size + 1) * 4 */
            uint32_t tag : 4; /* Tag, must be 0x0E */
        } B;
        uint32_t U;
    } option0;
    union
    {
        struct
        {
            uint32_t dummy_cycles : 8;  /* Dummy cycles before read */
            uint32_t reserved0 : 8;     /* Reserved for future use */
            uint32_t pinmux_group : 4;  /* The pinmux group selection */
            uint32_t reserved1 : 8;     /* Reserved for future use */
            uint32_t flash_connection : 4; /* Flash connection option: 0 - Single Flash connected to port A */
        } B;
        uint32_t U;
    } option1;
} serial_nor_config_option_t;

/* NOR flash API */
typedef struct
{
    uint32_t version;
    status_t (*init)(uint32_t instance, flexspi_nor_config_t *config);
    status_t (*program)(uint32_t instance, flexspi_nor_config_t *config, uint32_t
            dst_addr, const uint32_t *src);
    status_t (*erase_all)(uint32_t instance, flexspi_nor_config_t *config);
    status_t (*erase)(uint32_t instance, flexspi_nor_config_t *config, uint32_t start,
            uint32_t lengthInBytes);
    status_t (*read)(
            uint32_t instance, flexspi_nor_config_t *config, uint32_t *dst, uint32_t addr,
            uint32_t lengthInBytes);
    void (*clear_cache)(uint32_t instance);
    status_t (*xfer)(uint32_t instance, flexspi_xfer_t *xfer);
    status_t (*update_lut)(uint32_t instance, uint32_t seqIndex, const uint32_t
            *lutBase, uint32_t seqNumber);
    status_t (*get_config)(uint32_t instance, flexspi_nor_config_t *config,
            serial_nor_config_option_t *option);
} flexspi_nor_driver_interface_t;


/* Root pointer */
typedef struct
{
    const uint32_t version;                 /* Bootloader version number */
    const char *copyright;                  /* Bootloader Copyright */
    void (*runBootloader)(void *arg);       /* Function to start the bootloader executing */
    const uint32_t *reserved0;              /* Reserved */
    const flexspi_nor_driver_interface_t *flexSpiNorDriver; /* FlexSPI NOR Flash API */
    const uint32_t *reserved1;              /* Reserved */
    const rtwdog_driver_interface_t *rtwdogDriver;
    const wdog_driver_interface_t *wdogDriver;
    const uint32_t *reserved2;
} bootloader_api_entry_t;

bootloader_api_entry_t *g_bootloaderTree;
#ifdef USE_GET_CONFIG
    flexspi_nor_config_t flexspi_config;
    #define FLEXSPI_CONFIG &flexspi_config
#else
    #define FLEXSPI_CONFIG (flexspi_nor_config_t*)&qspiflash_config
#endif


/* FlexSPI LUT Sequence Instruction index offset */
#define LUT_SEQ_INS_0_1                   (0x00U)
#define LUT_SEQ_INS_2_3                   (0x01U)
#define LUT_SEQ_INS_4_5                   (0x02U)
#define LUT_SEQ_INS_6_7                   (0x03U)

/* FlexSPI LUT Sequence index offset (NOR) */
#define LUT_SEQ_IDX_0                     (0x00U)   /* Read command */
#define LUT_SEQ_IDX_1                     (0x04U)   /* Read Status command */
#define LUT_SEQ_IDX_2                     (0x08U)   /* RESERVED */
#define LUT_SEQ_IDX_3                     (0x0CU)   /* Write Enable command */
#define LUT_SEQ_IDX_4                     (0x10U)   /* RESERVED - Custom QE Enable */
#define LUT_SEQ_IDX_5                     (0x14U)   /* Erase Sector command */
#define LUT_SEQ_IDX_6                     (0x18U)   /* RESERVED */
#define LUT_SEQ_IDX_7                     (0x1CU)   /* RESERVED */
#define LUT_SEQ_IDX_8                     (0x20U)   /* RESERVED */
#define LUT_SEQ_IDX_9                     (0x24U)   /* Page Program command */
#define LUT_SEQ_IDX_10                    (0x28U)   /* RESERVED */
#define LUT_SEQ_IDX_11                    (0x2CU)   /* Full Chip Erase */
#define LUT_SEQ_IDX_12                    (0x30U)   /* RESERVED */
#define LUT_SEQ_IDX_13                    (0x34U)   /* SFDP */
#define LUT_SEQ_IDX_14                    (0x38U)   /* RESERVED */
#define LUT_SEQ_IDX_15                    (0x3CU)   /* Dummy */

/** Flash configuration in the .flash_config section of flash **/
#ifdef CPU_MIMXRT1064DVL6A
    #define CONFIG_FLASH_SIZE              (4 * 1024 * 1024) /* 4MBytes   */
    #define CONFIG_FLASH_PAGE_SIZE         256UL             /* 256Bytes  */
    #define CONFIG_FLASH_SECTOR_SIZE       (4 * 1024)        /* 4KBytes   */
    #define CONFIG_FLASH_BLOCK_SIZE        (64 * 1024)       /* 64KBytes  */
    #define CONFIG_FLASH_UNIFORM_BLOCKSIZE false
    #define CONFIG_SERIAL_CLK_FREQ         kFlexSpiSerialClk_100MHz
const flexspi_nor_config_t __attribute__((section(".flash_config"))) qspiflash_config = {
    .memConfig =
    {
        .tag              = FLEXSPI_CFG_BLK_TAG,
        .version          = FLEXSPI_CFG_BLK_VERSION,
        .readSampleClkSrc = kFlexSPIReadSampleClk_LoopbackFromDqsPad,
        .csHoldTime       = 3u,
        .csSetupTime      = 3u,
        .sflashPadType    = kSerialFlash_4Pads,
        .serialClkFreq    = CONFIG_SERIAL_CLK_FREQ,
        .sflashA1Size     = CONFIG_FLASH_SIZE,
        .lookupTable = {
            FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0xEB, RADDR_SDR, FLEXSPI_4PAD, 0x18),
            FLEXSPI_LUT_SEQ(DUMMY_SDR, FLEXSPI_4PAD, 0x06, READ_SDR, FLEXSPI_4PAD, 0x04),
        },
    },
    .pageSize           = CONFIG_FLASH_PAGE_SIZE,
    .sectorSize         = CONFIG_FLASH_SECTOR_SIZE,
    .blockSize          = CONFIG_FLASH_BLOCK_SIZE,
    .isUniformBlockSize = CONFIG_FLASH_UNIFORM_BLOCKSIZE,
};
#endif


/** Flash configuration in the .flash_config section of flash **/
#ifdef CPU_MIMXRT1062DVL6A
    #define CONFIG_FLASH_SIZE              (8 * 1024 * 1024) /* 8MBytes   */
    #define CONFIG_FLASH_PAGE_SIZE         256UL             /* 256Bytes  */
    #define CONFIG_FLASH_SECTOR_SIZE       (4 * 1024)        /* 4KBytes   */
    #define CONFIG_FLASH_BLOCK_SIZE        (64 * 1024)       /* 64KBytes  */
    #define CONFIG_FLASH_UNIFORM_BLOCKSIZE false
    #define CONFIG_SERIAL_CLK_FREQ         kFlexSpiSerialClk_100MHz
const flexspi_nor_config_t __attribute__((section(".flash_config"))) qspiflash_config = {
    .memConfig =
    {
        .tag              = FLEXSPI_CFG_BLK_TAG,
        .version          = FLEXSPI_CFG_BLK_VERSION,
        .readSampleClkSrc = kFlexSPIReadSampleClk_LoopbackFromDqsPad,
        .csHoldTime       = 3u,
        .csSetupTime      = 3u,
        .sflashPadType    = kSerialFlash_4Pads,
        .serialClkFreq    = CONFIG_SERIAL_CLK_FREQ,
        .sflashA1Size     = CONFIG_FLASH_SIZE,
        .lookupTable = {
            FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0xEB, RADDR_SDR, FLEXSPI_4PAD, 0x18),
            FLEXSPI_LUT_SEQ(DUMMY_SDR, FLEXSPI_4PAD, 0x06, READ_SDR, FLEXSPI_4PAD, 0x04),
        },
    },
    .pageSize           = CONFIG_FLASH_PAGE_SIZE,
    .sectorSize         = CONFIG_FLASH_SECTOR_SIZE,
    .blockSize          = CONFIG_FLASH_BLOCK_SIZE,
    .isUniformBlockSize = CONFIG_FLASH_UNIFORM_BLOCKSIZE,
};
#endif

/** Flash configuration in the .flash_config section of flash **/
#ifdef CPU_MIMXRT1052DVJ6B
    #define CONFIG_FLASH_SIZE              (8 * 1024 * 1024) /* 8MBytes  */
    #define CONFIG_FLASH_PAGE_SIZE         256UL             /* 256Bytes  */
    #define CONFIG_FLASH_SECTOR_SIZE       (4 * 1024)        /* 4Bytes */
    #define CONFIG_FLASH_BLOCK_SIZE        (64 * 1024)       /* 64KBytes */
    #define CONFIG_FLASH_UNIFORM_BLOCKSIZE false
    #define CONFIG_SERIAL_CLK_FREQ         kFlexSpiSerialClk_100MHz
    #define CONFIG_FLASH_ADDR_WIDTH        24u /* Width of flash addresses (either 24 or 32) */
    #define CONFIG_FLASH_QE_ENABLE         1

    #ifdef CONFIG_FLASH_W25Q64JV /* Winbond W25Q64JV */
        #define WRITE_STATUS_CMD 0x31
        #define QE_ENABLE        0x02 /* S9 */
    #else /* Default - ISSI IS25WP064A (on EVKB) */
        #define WRITE_STATUS_CMD 0x1
        #define QE_ENABLE        0x40 /* S6 */
    #endif

    /* Note: By default the RT1050-EVKB uses HyperFlex.
     *       To use QSPI flash a rework is required. See AN12183
     */

    /* QSPI boot header */
    /* Credit to: https://community.nxp.com/t5/i-MX-RT/RT1050-Debugging-with-QSPI-flash-on-secondary-pinmux/m-p/934745 */
    const flexspi_nor_config_t __attribute__((section(".flash_config"))) qspiflash_config = {
        .memConfig = {
            .tag                  = FLEXSPI_CFG_BLK_TAG,
            .version              = FLEXSPI_CFG_BLK_VERSION,
            .readSampleClkSrc     = kFlexSPIReadSampleClk_LoopbackFromDqsPad,
            .deviceType           = kFlexSpiDeviceType_SerialNOR,
            .sflashPadType        = kSerialFlash_4Pads,
            .serialClkFreq        = CONFIG_SERIAL_CLK_FREQ,
            .sflashA1Size         = CONFIG_FLASH_SIZE,
            .csHoldTime           = 3,
            .csSetupTime          = 3,
            .controllerMiscOption = (1u << kFlexSpiMiscOffset_SafeConfigFreqEnable),
            .columnAddressWidth   = 0,
            .waitTimeCfgCommands  = 0, /* 0=use read status (or #*100us) wait instead */
        #if CONFIG_FLASH_QE_ENABLE == 1
            .deviceModeCfgEnable  = 1,
            .deviceModeType       = kDeviceConfigCmdType_QuadEnable,
            .deviceModeSeq.seqNum = 2, /* issue 2 commands starting at index 3 */
            .deviceModeSeq.seqId  = 3, /* issue write enable and write status */
            .deviceModeArg        = QE_ENABLE,
        #endif
            .lutCustomSeqEnable   = 0,
            .dataValidTime        = {16u, 16u},
            .busyOffset           = 0, /* WIP/Busy bit=0 (read status busy bit offset */
            .busyBitPolarity      = 0, /* 0 – Busy bit=1 (device is busy) */
            .lookupTable = {
            #if CONFIG_FLASH_QE_ENABLE == 1
                /* Quad Input/output read sequence - with optimized XIP support */
                [LUT_SEQ_IDX_0 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0xEB,
                    RADDR_SDR, FLEXSPI_4PAD, CONFIG_FLASH_ADDR_WIDTH),
                [LUT_SEQ_IDX_0 + LUT_SEQ_INS_2_3] = FLEXSPI_LUT_SEQ(
                    MODE8_SDR, FLEXSPI_4PAD, 0xA0 /* continuous read mode - 2 dummy cycles */,
                    DUMMY_SDR, FLEXSPI_4PAD, 0x04  /* 4 dummy cycles (6 total) */ ),
                [LUT_SEQ_IDX_0 + LUT_SEQ_INS_4_5] = FLEXSPI_LUT_SEQ(
                    READ_SDR,  FLEXSPI_4PAD, 0x04  /* any non-zero value */,
                    JMP_ON_CS, FLEXSPI_1PAD, 0x01),
            #else
                /* Quad Input/output read sequence */
                [LUT_SEQ_IDX_0 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0xEB,
                    RADDR_SDR, FLEXSPI_4PAD, CONFIG_FLASH_ADDR_WIDTH),
                [LUT_SEQ_IDX_0 + LUT_SEQ_INS_2_3] = FLEXSPI_LUT_SEQ(
                    DUMMY_SDR, FLEXSPI_4PAD, 0x06  /* 6 dummy cycles */,
                    READ_SDR,  FLEXSPI_4PAD, 0x04  /* any non-zero value */ ),
            #endif
                /* Read Status */
                [LUT_SEQ_IDX_1 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x05,
                    READ_SDR,  FLEXSPI_1PAD, 0x04 /* Read 4 bytes */ ),
                /* Write Enable */
                [LUT_SEQ_IDX_3 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x06,
                    STOP,      FLEXSPI_1PAD, 0x00),
                /* Write Status - Custom LUT (QE Enable) */
                [LUT_SEQ_IDX_4 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, WRITE_STATUS_CMD,
                    WRITE_SDR, FLEXSPI_1PAD, 0x01 /* Write 1 byte */ ),
                /* Erase Sector */
                [LUT_SEQ_IDX_5 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x20 /* Sector Erase, 1-bit */,
                    RADDR_SDR, FLEXSPI_1PAD, CONFIG_FLASH_ADDR_WIDTH),
                /* Erase Block - Custom LUT */
                [LUT_SEQ_IDX_8 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0xD8 /* Block Erase, 1-bit */,
                    RADDR_SDR, FLEXSPI_1PAD, CONFIG_FLASH_ADDR_WIDTH),
            #if CONFIG_FLASH_QE_ENABLE == 1
                /* Quad Page Program */
                [LUT_SEQ_IDX_9 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x32,
                    RADDR_SDR, FLEXSPI_1PAD, CONFIG_FLASH_ADDR_WIDTH),
                [LUT_SEQ_IDX_9 + LUT_SEQ_INS_2_3] = FLEXSPI_LUT_SEQ(
                    WRITE_SDR, FLEXSPI_4PAD, 0x04 /* any non-zero value */,
                    STOP,      FLEXSPI_1PAD, 0x00),
            #else
                /* Page Program */
                [LUT_SEQ_IDX_9 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x02,
                    RADDR_SDR, FLEXSPI_1PAD, CONFIG_FLASH_ADDR_WIDTH),
                [LUT_SEQ_IDX_9 + LUT_SEQ_INS_2_3] = FLEXSPI_LUT_SEQ(
                    WRITE_SDR, FLEXSPI_1PAD, 0x04 /* any non-zero value */,
                    STOP,      FLEXSPI_1PAD, 0x00),
            #endif
                /* Chip Erase */
                [LUT_SEQ_IDX_11 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x60,
                    STOP,      FLEXSPI_1PAD, 0x00),
                /* SFDP - Required for get_config */
                [LUT_SEQ_IDX_13 + LUT_SEQ_INS_0_1] = FLEXSPI_LUT_SEQ(
                    CMD_SDR,   FLEXSPI_1PAD, 0x5A,
                    RADDR_SDR, FLEXSPI_1PAD, CONFIG_FLASH_ADDR_WIDTH ),
                [LUT_SEQ_IDX_13 + LUT_SEQ_INS_2_3] = FLEXSPI_LUT_SEQ(
                    DUMMY_SDR, FLEXSPI_1PAD, 0x08  /* 8 dummy cycles */,
                    READ_SDR,  FLEXSPI_4PAD, 0xFF /* Read 255 bytes */),
            },
        },
        .pageSize           = CONFIG_FLASH_PAGE_SIZE,
        .sectorSize         = CONFIG_FLASH_SECTOR_SIZE,
        .blockSize          = CONFIG_FLASH_BLOCK_SIZE,
        .isUniformBlockSize = CONFIG_FLASH_UNIFORM_BLOCKSIZE,
        .ipcmdSerialClkFreq = 0,
    };
#endif /* CPU_MIMXRT1052DVJ6B */

#ifndef __FLASH_BASE

#if defined(CPU_MIMXRT1052DVJ6B) || defined(CPU_MIMXRT1062DVL6A)
#define __FLASH_BASE 0x60000000
#elif defined(CPU_MIMXRT1064DVL6A)
#define __FLASH_BASE 0x70000000
#else
#error "Please define MCUXPRESSO SDK CPU variant macro (e.g. CPU_MIMXRT1062DVL6A)"
#endif

#endif


#ifndef FLASH_BASE
#define FLASH_BASE __FLASH_BASE
#define PLUGIN_FLAG 0x0UL
#endif

const uint8_t dcd_data[1] = {0};
extern void isr_reset(void);


const BOOT_DATA_T __attribute__((section(".boot_data"))) boot_data = {
  FLASH_BASE,                 /* boot start location */
  CONFIG_FLASH_SIZE,          /* size */
  PLUGIN_FLAG,                /* Plugin flag*/
  0xFFFFFFFF                  /* empty - extra data word */
};

const ivt __attribute__((section(".image_vt"))) image_vector_table = {
  IVT_HEADER,                         /* IVT Header */
  (uint32_t)isr_reset,               /* Image Entry Function */
  IVT_RSVD,                           /* Reserved = 0 */
  (uint32_t)dcd_data,                 /* Address where DCD information is stored */
  (uint32_t)&boot_data,               /* Address where BOOT Data Structure is stored */
  (uint32_t)&image_vector_table,      /* Pointer to IVT Self (absolute address */
  (uint32_t)CSF_ADDRESS,              /* Address where CSF file is stored */
  IVT_RSVD                            /* Reserved = 0 */
};

/*******************************************************************************
 * Variables for BOARD_BootClockRUN configuration
 ******************************************************************************/
const clock_arm_pll_config_t armPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 100,                       /* PLL loop divider, Fout = Fin * 50 */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_sys_pll_config_t sysPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 1,                         /* PLL loop divider, Fout = Fin * ( 20 + loopDivider*2 + numerator / denominator ) */
        .numerator = 0,                           /* 30 bit numerator of fractional loop divider */
        .denominator = 1,                         /* 30 bit denominator of fractional loop divider */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_usb_pll_config_t usb1PllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 0,                         /* PLL loop divider, Fout = Fin * 20 */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_video_pll_config_t videoPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 31,                        /* PLL loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .postDivider = 8,                         /* Divider after PLL */
        .numerator = 0,                           /* 30 bit numerator of fractional loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .denominator = 1,                         /* 30 bit denominator of fractional loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };

static void clock_init(void)
{
    if (CCM_ANALOG->PLL_ARM & CCM_ANALOG_PLL_ARM_BYPASS_MASK)
    {
        /* Configure ARM_PLL */
        CCM_ANALOG->PLL_ARM =
            CCM_ANALOG_PLL_ARM_BYPASS(1) | CCM_ANALOG_PLL_ARM_ENABLE(1) | CCM_ANALOG_PLL_ARM_DIV_SELECT(24);
        /* Wait Until clock is locked */
        while ((CCM_ANALOG->PLL_ARM & CCM_ANALOG_PLL_ARM_LOCK_MASK) == 0)
        {
        }

        /* Configure PLL_SYS */
        CCM_ANALOG->PLL_SYS &= ~CCM_ANALOG_PLL_SYS_POWERDOWN_MASK;
        /* Wait Until clock is locked */
        while ((CCM_ANALOG->PLL_SYS & CCM_ANALOG_PLL_SYS_LOCK_MASK) == 0)
        {
        }

        /* Configure PFD_528 */
        CCM_ANALOG->PFD_528 = CCM_ANALOG_PFD_528_PFD0_FRAC(24) | CCM_ANALOG_PFD_528_PFD1_FRAC(24) |
            CCM_ANALOG_PFD_528_PFD2_FRAC(19) | CCM_ANALOG_PFD_528_PFD3_FRAC(24);

        /* Configure USB1_PLL */
        CCM_ANALOG->PLL_USB1 =
            CCM_ANALOG_PLL_USB1_DIV_SELECT(0) | CCM_ANALOG_PLL_USB1_POWER(1) | CCM_ANALOG_PLL_USB1_ENABLE(1);
        while ((CCM_ANALOG->PLL_USB1 & CCM_ANALOG_PLL_USB1_LOCK_MASK) == 0)
        {
        }
        CCM_ANALOG->PLL_USB1 &= ~CCM_ANALOG_PLL_USB1_BYPASS_MASK;

        /* Configure PFD_480 */
        CCM_ANALOG->PFD_480 = CCM_ANALOG_PFD_480_PFD0_FRAC(35) | CCM_ANALOG_PFD_480_PFD1_FRAC(35) |
            CCM_ANALOG_PFD_480_PFD2_FRAC(26) | CCM_ANALOG_PFD_480_PFD3_FRAC(15);

        /* Configure Clock PODF */
        CCM->CACRR = CCM_CACRR_ARM_PODF(1);

        CCM->CBCDR = (CCM->CBCDR & (~(CCM_CBCDR_SEMC_PODF_MASK | CCM_CBCDR_AHB_PODF_MASK | CCM_CBCDR_IPG_PODF_MASK))) |
            CCM_CBCDR_SEMC_PODF(2) | CCM_CBCDR_AHB_PODF(2) | CCM_CBCDR_IPG_PODF(2);

#if defined(CPU_MIMXRT1062DVL6A) || defined(CPU_MIMXRT1064DVL6A)
        /* Configure FLEXSPI2 CLOCKS */
        CCM->CBCMR =
            (CCM->CBCMR &
             (~(CCM_CBCMR_PRE_PERIPH_CLK_SEL_MASK | CCM_CBCMR_FLEXSPI2_CLK_SEL_MASK | CCM_CBCMR_FLEXSPI2_PODF_MASK))) |
            CCM_CBCMR_PRE_PERIPH_CLK_SEL(3) | CCM_CBCMR_FLEXSPI2_CLK_SEL(1) | CCM_CBCMR_FLEXSPI2_PODF(7);
#endif

        /* Configure FLEXSPI CLOCKS */
        CCM->CSCMR1 = ((CCM->CSCMR1 &
                    ~(CCM_CSCMR1_FLEXSPI_CLK_SEL_MASK | CCM_CSCMR1_FLEXSPI_PODF_MASK | CCM_CSCMR1_PERCLK_PODF_MASK |
                        CCM_CSCMR1_PERCLK_CLK_SEL_MASK)) |
                CCM_CSCMR1_FLEXSPI_CLK_SEL(3) | CCM_CSCMR1_FLEXSPI_PODF(7) | CCM_CSCMR1_PERCLK_PODF(1));

        /* Finally, Enable PLL_ARM, PLL_SYS and PLL_USB1 */
        CCM_ANALOG->PLL_ARM &= ~CCM_ANALOG_PLL_ARM_BYPASS_MASK;
        CCM_ANALOG->PLL_SYS &= ~CCM_ANALOG_PLL_SYS_BYPASS_MASK;
        CCM_ANALOG->PLL_USB1 &= ~CCM_ANALOG_PLL_USB1_BYPASS_MASK;
    }
}


extern void ARM_MPU_Disable(void);
extern int wc_dcp_init(void);
static int hal_flash_init(void);

void hal_init(void)
{
#ifdef WOLFSSL_IMXRT_DCP
    wc_dcp_init();
#endif
    ARM_MPU_Disable();
    clock_init();
    hal_flash_init();
}

void hal_prepare_boot(void)
{
}

#endif /* __WOLFBOOT */

static int hal_flash_init(void)
{
#ifdef USE_GET_CONFIG
    serial_nor_config_option_t flexspi_cfg_option;
#endif
    if (g_bootloaderTree == NULL) {
        g_bootloaderTree = (bootloader_api_entry_t *)*(uint32_t *)0x0020001c;
    #ifdef USE_GET_CONFIG
        memset(&flexspi_cfg_option, 0, sizeof(flexspi_cfg_option));
        flexspi_cfg_option.option0.U = 0xC0000007; /* QuadSPI-NOR, f = default */
        g_bootloaderTree->flexSpiNorDriver->get_config(0,
            &flexspi_config,
            &flexspi_cfg_option);
        g_bootloaderTree->flexSpiNorDriver->init(0, &flexspi_config);
        g_bootloaderTree->flexSpiNorDriver->clear_cache(0);
    #endif
    }
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    status_t status;
    uint32_t wbuf[CONFIG_FLASH_PAGE_SIZE / sizeof(uint32_t)];
    int i;
    hal_flash_init(); /* make sure g_bootloaderTree is set */
    for (i = 0; i < len; i+= CONFIG_FLASH_PAGE_SIZE) {
        memcpy(wbuf, data + i, CONFIG_FLASH_PAGE_SIZE);
        status = g_bootloaderTree->flexSpiNorDriver->program(0, FLEXSPI_CONFIG,
            (address + i) - FLASH_BASE, wbuf);
        if (status != kStatus_Success)
            return -1;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    status_t status;
    hal_flash_init(); /* make sure g_bootloaderTree is set */
    status = g_bootloaderTree->flexSpiNorDriver->erase(0, FLEXSPI_CONFIG,
        address - FLASH_BASE, len);
    if (status != kStatus_Success)
        return -1;
    return 0;
}
