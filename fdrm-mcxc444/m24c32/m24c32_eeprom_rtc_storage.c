/*
 * Author: Shakir Salam
 * M24C32 EEPROM I2C Driver + RTC Storage
 */

// Libraries
#include <string.h>
#include <stdio.h>
#include "board.h"
#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_i2c.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "fsl_rtc.h"

// Definitions
#define I2C_BAUDRATE		100000U
#define EEPROM_I2C_ADDR		0x50
#define	EEPROM_PAGE_SIZE	32
// EEPROM Memory Address to store RTC
#define	RTC_EEPROM_ADDR		0x0100

// I2C Globals
i2c_master_config_t masterConfig;
uint32_t i2c_sourceClock;

// RTC Structure
typedef struct {
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
	uint8_t day;
	uint8_t month;
	uint16_t year;
} rtc_time_t;

// I2C Config
void I2C_Configuration(void)
{
	CLOCK_EnableClock(kCLOCK_PortC);

	const port_pin_config_t i2c_pin = {
			kPORT_PullUp,
			kPORT_FastSlewRate,
			kPORT_PassiveFilterDisable,
			kPORT_LowDriveStrength,
			kPORT_MuxAlt2
	};

	// PTC1 SDA, PTC2 SCL
	PORT_SetPinConfig(PORTC, 1U, &i2c_pin);
	PORT_SetPinConfig(PORTC, 2U, &i2c_pin);

	PRINTF("I2C Pins Configured\r\n");
}

// I2C Initialization
void I2C_Init(void)
{
	I2C_Configuration();

	I2C_MasterGetDefaultConfig(&masterConfig);
	masterConfig.baudRate_Bps = I2C_BAUDRATE;

	i2c_sourceClock = CLOCK_GetFreq(I2C1_CLK_SRC);
	I2C_MasterInit(I2C1, &masterConfig, i2c_sourceClock);

	PRINTF("I2C1 Initialized at %d Hz\r\n", I2C_BAUDRATE);
}

// EEPROM Functions
// EEPROM Write
status_t EEPROM_Write(uint32_t addr, uint16_t memAddr, uint8_t *data, uint16_t length)
{
	uint8_t buffer[EEPROM_PAGE_SIZE + 2];
	i2c_master_transfer_t xfer;

	buffer[0] = (uint8_t)(memAddr >> 8);
	buffer[1] = (uint8_t)(memAddr & 0xFF);
	memcpy(&buffer[2], data, length);

	memset(&xfer, 0, sizeof(xfer));
	xfer.slaveAddress = addr;
	xfer.direction 	  = kI2C_Write;
	xfer.data         = buffer;
	xfer.dataSize     = length + 2;
	xfer.flags        = kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &xfer);
}

// EEPROM Read
status_t EEPROM_Read(uint8_t addr, uint16_t memAddr, uint8_t *data, uint16_t length)
{
	i2c_master_transfer_t xfer;

	memset(&xfer, 0, sizeof(xfer));
	xfer.slaveAddress   = addr;
	xfer.direction 	    = kI2C_Read;
	xfer.subaddress	    = memAddr;
	xfer.subaddressSize = 2;
	xfer.data			= data;
	xfer.dataSize 		= length;
	xfer.flags 			= kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &xfer);
}

// EEPROM Wait
void EEPROM_WaitReady(uint8_t addr)
{
	i2c_master_transfer_t xfer;

	memset(&xfer, 0, sizeof(xfer));
	xfer.slaveAddress = addr;
	xfer.direction	  = kI2C_Write;
	xfer.data		  = NULL;
	xfer.dataSize 	  = 0;

	while (I2C_MasterTransferBlocking(I2C1, &xfer) != kStatus_Success);
}

// RTC to EEPROM
void RTC_ToBytes(rtc_time_t *rtc, uint8_t *data)
{
    data[0] = rtc->hours;
    data[1] = rtc->minutes;
    data[2] = rtc->seconds;
    data[3] = rtc->day;
    data[4] = rtc->month;
    data[5] = (rtc->year >> 8) & 0xFF;
    data[6] = rtc->year & 0xFF;
}

void EEPROM_StoreRTC(rtc_datetime_t *rtcDate)
{
    rtc_time_t rtc;
    uint8_t rtcData[7];

    rtc.hours = rtcDate->hour;
    rtc.minutes = rtcDate->minute;
    rtc.seconds = rtcDate->second;
    rtc.day = rtcDate->day;
    rtc.month = rtcDate->month;
    rtc.year = rtcDate->year;

    RTC_ToBytes(&rtc, rtcData);

    if (EEPROM_Write(EEPROM_I2C_ADDR, RTC_EEPROM_ADDR, rtcData, sizeof(rtcData)) == kStatus_Success)
    {
        EEPROM_WaitReady(EEPROM_I2C_ADDR);
        PRINTF("RTC Stored to EEPROM\r\n");
    }
    else
    {
        PRINTF("RTC Store Failed\r\n");
    }
}

int main(void)
{
    BOARD_InitHardware();
    PRINTF("M24C32 EEPROM + RTC Example\r\n");

    // Initialize I2C for EEPROM
    I2C_Init();

    // Initialize RTC
    rtc_config_t rtcConfig;
    RTC_GetDefaultConfig(&rtcConfig);
    RTC_Init(RTC, &rtcConfig);

    rtc_datetime_t date;
    date.year = 2026;
    date.month = 2;
    date.day = 3;
    date.hour = 4;
    date.minute = 24;
    date.second = 0;

    RTC_StopTimer(RTC);
    RTC_SetDatetime(RTC, &date);
    RTC_StartTimer(RTC);

    // Infinite loop: read RTC and store to EEPROM
    while (1)
    {
        // Read current RTC
        RTC_GetDatetime(RTC, &date);

        PRINTF("Current RTC: %04d-%02d-%02d %02d:%02d:%02d\r\n",
               date.year, date.month, date.day,
               date.hour, date.minute, date.second);

        // Store RTC to EEPROM
        EEPROM_StoreRTC(&date);

        // Optional delay: store once per second
        for (volatile uint32_t i = 0; i < 3000000; i++); // crude delay
    }
}
