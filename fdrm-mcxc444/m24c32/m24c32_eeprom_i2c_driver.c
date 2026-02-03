/*
 * Author: Shakir Salam
 * M24C32 EEPROM I2C Driver
 */

// Libraries
#include <string.h>
#include <stdio.h>
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_i2c.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "app.h"

// Definitions
#define I2C_BAUDRATE		100000U
#define EEPROM_I2C_ADDR		0x50
#define	EEPROM_PAGE_SIZE	32

// I2C Globals
i2c_master_config_t masterConfig;
uint32_t i2c_sourceClock;

// I2C COnfiguration
void I2C_Configuration(void)
{
	// Enable Clock on PORT C
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

	PRINTF("I2C pins configured\r\n");
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
// Write Bytes to EEPROM
status_t EEPROM_Write(uint8_t addr, uint16_t memAddr, uint8_t *data, uint16_t length)
{
	uint8_t buffer[EEPROM_PAGE_SIZE + 2];
	i2c_master_transfer_t xfer;

	buffer[0] = (uint8_t)(memAddr >> 8);		// MSB
	buffer[1] = (uint8_t)(memAddr & 0xFF);		// LSB
	memcpy(&buffer[2], data, length);

	memset(&xfer, 0, sizeof(xfer));
	xfer.slaveAddress		= addr;
	xfer.direction			= kI2C_Write;
	xfer.data           	= buffer;
	xfer.dataSize			= length + 2;
	xfer.flags				= kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &xfer);
}

// Read Bytes to EEPROM
status_t EEPROM_Read(uint8_t addr, uint16_t memAddr, uint8_t *data, uint16_t length)
{
	i2c_master_transfer_t xfer;

	memset(&xfer, 0, sizeof(xfer));
	xfer.slaveAddress		= addr;
	xfer.direction			= kI2C_Read;
	xfer.subaddress			= memAddr;
	xfer.subaddressSize		= 2;				// 16-bit address
	xfer.data				= data;
	xfer.dataSize			= length;
	xfer.flags				= kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &xfer);
}

// ACK polling (wait until write completes)
void EEPROM_WaitReady(uint8_t addr)
{
	i2c_master_transfer_t xfer;
	memset(&xfer, 0, sizeof(xfer));

	xfer.slaveAddress		= addr;
	xfer.direction			= kI2C_Write;
	xfer.data 				= NULL;
	xfer.dataSize			= 0;

	while (I2C_MasterTransferBlocking(I2C1, &xfer) != kStatus_Success);
}

int main(void)
{
	BOARD_InitHardware();
	PRINTF("M24C32 EEPROM I2C Write and Read Test\r\n");

	I2C_Init();

	// Send Dummy Data
	uint8_t txData[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
	uint8_t rxData[8];
	uint16_t memAddr = 0x0010;

	// Write to EEPROM
	if (EEPROM_Write(EEPROM_I2C_ADDR, memAddr, txData, sizeof(txData)) == kStatus_Success)
	{
		PRINTF("Write OK\r\n");
		EEPROM_WaitReady(EEPROM_I2C_ADDR);
	}
	else
	{
		PRINTF("Write Failed\r\n");
	}

	// Read Back from EEPROM
	if (EEPROM_Read(EEPROM_I2C_ADDR, memAddr, rxData, sizeof(rxData)) == kStatus_Success)
	{
		PRINTF("Read Data: ");
		for (int i = 0; i < sizeof(rxData); i++)
		{
			PRINTF("0x%02X ", rxData[i]);
		}
		PRINTF("\r\n");
	}
	else
	{
		PRINTF("Read Failed\r\n");
	}

	while(1);
}



