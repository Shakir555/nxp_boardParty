/*
 * Author: Shakir Salam
 * FRDM-MCXC444 RTC + EEPROM Logger using I2C DMA
 * Features:
 * - DMA-based EEPROM read/write
 * - Ring buffer logging (old logs replaced by new)
 * - Chronological live view
 * - Clear logs
 * - Menu navigation
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "board.h"
#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_rtc.h"
#include "fsl_i2c.h"
#include "fsl_i2c_dma.h"
#include "fsl_dmamux.h"
#include "fsl_dma.h"
#include "fsl_port.h"
#include "fsl_clock.h"

/***************************
 * Definitions
 **************************/
#define I2C_BAUDRATE           100000U
#define EEPROM_I2C_ADDR        0x50
#define EEPROM_PAGE_SIZE       32
#define EEPROM_LOG_START_ADDR  0x0000
#define MAX_LOG_ENTRIES        16

#define I2C_DMA_CHANNEL        0
#define DMA_REQUEST_SRC        kDmaRequestMux0I2C1
#define OSC_WAIT_TIME_MS       1000UL

/***************************
 * Structures
 **************************/
typedef struct
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} rtc_log_t;

#define RTC_LOG_SIZE sizeof(rtc_log_t)

/***************************
 * Globals
 **************************/
i2c_master_config_t masterConfig;
i2c_master_dma_handle_t g_i2cDmaHandle;
dma_handle_t g_dmaHandle;

volatile bool g_i2cDone = false;
uint32_t i2cClock;

uint16_t logIndex = 0;   // Points to next log slot
uint16_t startIndex = 0; // Points to oldest log

/***************************
 * DMA Callback
 **************************/
static void i2c_dma_callback(I2C_Type *base,
                             i2c_master_dma_handle_t *handle,
                             status_t status,
                             void *userData)
{
    if (status == kStatus_Success)
    {
        g_i2cDone = true;
    }
}

/***************************
 * OSC Wait
 **************************/
static void EXAMPLE_WaitOSCReady(uint32_t delay_ms)
{
    uint32_t ticks = SystemCoreClock / 1000U;
    SysTick->LOAD = ticks - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    for (uint32_t i = 0; i < delay_ms; i++)
    {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {}
    }

    SysTick->CTRL = 0;
}

/***************************
 * I2C + DMA Init
 **************************/
void I2C_DMA_Init(void)
{
    CLOCK_EnableClock(kCLOCK_PortC);

    port_pin_config_t pinConfig = {
        kPORT_PullUp,
        kPORT_FastSlewRate,
        kPORT_PassiveFilterDisable,
        kPORT_LowDriveStrength,
        kPORT_MuxAlt2
    };

    PORT_SetPinConfig(PORTC, 1U, &pinConfig); // SDA
    PORT_SetPinConfig(PORTC, 2U, &pinConfig); // SCL

    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = I2C_BAUDRATE;

    i2cClock = CLOCK_GetFreq(I2C1_CLK_SRC);
    I2C_MasterInit(I2C1, &masterConfig, i2cClock);

    DMAMUX_Init(DMAMUX0);
    DMA_Init(DMA0);

    DMAMUX_SetSource(DMAMUX0, I2C_DMA_CHANNEL, DMA_REQUEST_SRC);
    DMAMUX_EnableChannel(DMAMUX0, I2C_DMA_CHANNEL);

    DMA_CreateHandle(&g_dmaHandle, DMA0, I2C_DMA_CHANNEL);

    I2C_MasterTransferCreateHandleDMA(
        I2C1,
        &g_i2cDmaHandle,
        i2c_dma_callback,
        NULL,
        &g_dmaHandle
    );

    PRINTF("I2C DMA Initialized\r\n");
}

/***************************
 * EEPROM DMA Functions
 **************************/
void EEPROM_WriteDMA(uint16_t memAddr, uint8_t *data, uint16_t len)
{
    uint8_t buffer[EEPROM_PAGE_SIZE + 2];
    i2c_master_transfer_t xfer;

    buffer[0] = memAddr >> 8;
    buffer[1] = memAddr & 0xFF;
    memcpy(&buffer[2], data, len);

    memset(&xfer, 0, sizeof(xfer));
    xfer.slaveAddress = EEPROM_I2C_ADDR;
    xfer.direction    = kI2C_Write;
    xfer.data         = buffer;
    xfer.dataSize     = len + 2;
    xfer.flags        = kI2C_TransferDefaultFlag;

    g_i2cDone = false;
    I2C_MasterTransferDMA(I2C1, &g_i2cDmaHandle, &xfer);
    while (!g_i2cDone) {}
}

void EEPROM_ReadDMA(uint16_t memAddr, uint8_t *data, uint16_t len)
{
    i2c_master_transfer_t xfer;

    memset(&xfer, 0, sizeof(xfer));
    xfer.slaveAddress   = EEPROM_I2C_ADDR;
    xfer.direction      = kI2C_Read;
    xfer.subaddress     = memAddr;
    xfer.subaddressSize = 2;
    xfer.data           = data;
    xfer.dataSize       = len;
    xfer.flags          = kI2C_TransferDefaultFlag;

    g_i2cDone = false;
    I2C_MasterTransferDMA(I2C1, &g_i2cDmaHandle, &xfer);
    while (!g_i2cDone) {}
}

/***************************
 * EEPROM Utilities
 **************************/
void EEPROM_ClearAllLogs(void)
{
    rtc_log_t empty = {0};

    for (uint16_t i = 0; i < MAX_LOG_ENTRIES; i++)
    {
        EEPROM_WriteDMA(
            EEPROM_LOG_START_ADDR + i * RTC_LOG_SIZE,
            (uint8_t *)&empty,
            RTC_LOG_SIZE
        );
        SDK_DelayAtLeastUs(5000U, SystemCoreClock);
    }

    logIndex = 0;
    startIndex = 0;
    PRINTF("EEPROM logs cleared!\r\n");
}

/***************************
 * RTC Log Functions
 **************************/
void EEPROM_SaveRTC(rtc_datetime_t *dt)
{
    rtc_log_t log = {
        dt->year, dt->month, dt->day,
        dt->hour, dt->minute, dt->second
    };

    EEPROM_WriteDMA(
        EEPROM_LOG_START_ADDR + logIndex * RTC_LOG_SIZE,
        (uint8_t *)&log,
        RTC_LOG_SIZE
    );

    // Update ring buffer indices
    logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
    if (logIndex == startIndex)
    {
        startIndex = (startIndex + 1) % MAX_LOG_ENTRIES; // overwrite oldest
    }
}

void EEPROM_ReadRTC(uint16_t index, rtc_log_t *log)
{
    EEPROM_ReadDMA(
        EEPROM_LOG_START_ADDR + index * RTC_LOG_SIZE,
        (uint8_t *)log,
        RTC_LOG_SIZE
    );
}

/***************************
 * Menu
 **************************/
void ShowMenu(void)
{
    PRINTF("\r\n===== MENU =====\r\n");
    PRINTF("1 - Show current time (log it)\r\n");
    PRINTF("2 - View EEPROM logs (chronological)\r\n");
    PRINTF("3 - Clear all EEPROM logs\r\n");
    PRINTF("> ");
}

/***************************
 * Main
 **************************/
int main(void)
{
    rtc_config_t rtcConfig;
    rtc_datetime_t now;
    rtc_log_t log;
    char cmd;

    BOARD_InitHardware();
    I2C_DMA_Init();

    RTC_GetDefaultConfig(&rtcConfig);
    RTC_Init(RTC, &rtcConfig);

#if !(defined(FSL_FEATURE_RTC_HAS_NO_CR_OSCE) && FSL_FEATURE_RTC_HAS_NO_CR_OSCE)
    if (!(RTC->CR & RTC_CR_OSCE_MASK))
    {
        RTC_SetClockSource(RTC);
        EXAMPLE_WaitOSCReady(OSC_WAIT_TIME_MS);
    }
#endif

    rtc_datetime_t startTime = {2026, 2, 4, 3, 0, 0};
    RTC_SetDatetime(RTC, &startTime);
    RTC_StartTimer(RTC);

    PRINTF("\r\nRTC + EEPROM DMA LOGGER READY\r\n");

    while (1)
    {
        ShowMenu();
        cmd = GETCHAR();
        PRINTF("%c\r\n", cmd);

        /* OPTION 1: Log current time */
        if (cmd == '1')
        {
            RTC_GetDatetime(RTC, &now);
            PRINTF("Current: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                   now.year, now.month, now.day,
                   now.hour, now.minute, now.second);

            EEPROM_SaveRTC(&now);
        }

        /* OPTION 2: View logs live */
        else if (cmd == '2')
        {
            PRINTF("\r\nView EEPROM Logs (ESC or 'q' + ENTER to return)\r\n");

            while (1)
            {
                PRINTF("\033[2J\033[H"); // Clear screen

                PRINTF("---- EEPROM LOGS (CHRONOLOGICAL) ----\r\n");

                for (uint16_t i = 0; i < MAX_LOG_ENTRIES; i++)
                {
                    uint16_t index = (startIndex + i) % MAX_LOG_ENTRIES;
                    EEPROM_ReadRTC(index, &log);

                    // Skip empty logs
                    if (log.year == 0 && log.month == 0) continue;

                    PRINTF("[%02d] %04d-%02d-%02d %02d:%02d:%02d\r\n",
                           index,
                           log.year, log.month, log.day,
                           log.hour, log.minute, log.second);
                }

                PRINTF("\r\nPress ESC or 'q' + ENTER to return...\r\n");

                cmd = GETCHAR();
                if (cmd == 'q' || cmd == 27)
                {
                    PRINTF("\r\nExit log view\r\n");
                    break;
                }

                SDK_DelayAtLeastUs(500000U, SystemCoreClock); // 0.5s refresh
            }
        }

        /* OPTION 3: Clear all logs */
        else if (cmd == '3')
        {
            EEPROM_ClearAllLogs();
            PRINTF("Returning to menu...\r\n");
        }
    }
}