/*
 * Author: Shakir Salam
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

// PCA9548A Registers
#define PCA9548A_ADDR		0x70

// I2C Definition
#define I2C_BAUDRATE		100000U
// I2c Configuration
i2c_master_config_t masterConfig;
uint32_t i2c_sourceClock;

void I2C_Configuration(void)
{
	// Configure I2C Pins
	// PTC1 - SDA
	CLOCK_EnableClock(kCLOCK_PortC);
	const port_pin_config_t	ptc1_config = {
			kPORT_PullUp,
			kPORT_FastSlewRate,
			kPORT_PassiveFilterDisable,
			kPORT_LowDriveStrength,
			kPORT_MuxAlt2
	};

	PORT_SetPinConfig(PORTC, 1U, &ptc1_config);

	// PTC2 - SCL
	const port_pin_config_t ptc2_config = {
			kPORT_PullUp,
			kPORT_FastSlewRate,
			kPORT_PassiveFilterDisable,
			kPORT_LowDriveStrength,
			kPORT_MuxAlt2
	};

	PORT_SetPinConfig(PORTC, 2U, &ptc2_config);
	PRINTF("I2C Configuration");
}

void I2C_init(void)
{
	I2C_Configuration();
	I2C_MasterGetDefaultConfig(&masterConfig);
	masterConfig.baudRate_Bps = I2C_BAUDRATE;
	i2c_sourceClock = CLOCK_GetFreq(I2C1_CLK_SRC);
	I2C_MasterInit(I2C1, &masterConfig, i2c_sourceClock);
	PRINTF("I2C Initialization");
	PRINTF("I2C1 initialized at %d Hz\r\n", I2C_BAUDRATE);
}

status_t PCA9548A_SelectChannel(uint8_t channel)
{
	if (channel > 7)
		return kStatus_InvalidArgument;

	uint8_t data = (1 << channel);

	i2c_master_transfer_t xfer;
	memset(&xfer, 0, sizeof(xfer));

	xfer.slaveAddress = PCA9548A_ADDR;
	xfer.direction 	  = kI2C_Write;
	xfer.data 		  = &data;
	xfer.dataSize 	  = 1;
	xfer.flags		  = kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &xfer);
}

int main(void)
{
	BOARD_InitHardware();
	PRINTF("\r\nPCA9548A I2C Test (FRDM-MCXC444)\r\n");

	//Configure I2C Pins
	I2C_Configuration();
	//I2C Initialize
	I2C_init();

	while(1)
	{
		for (uint8_t ch = 0; ch < 8; ch++)
		{
			if (PCA9548A_SelectChannel(ch) == kStatus_Success)
			{
				PRINTF("PCA9548A: Channel %d enabled\r\n", ch);
			}
			else
			{
				PRINTF("PCA9548A: Channel %d FAILED\r\n", ch);
			}
			for (volatile int i = 0; i < 600000; i++);
		}
	}
}






