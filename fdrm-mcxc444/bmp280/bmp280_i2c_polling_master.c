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

// BMP280 Registers
#define BMP280_REG_ID			0xD0
#define BMP280_REG_CALIB_START	0x88
#define BMP280_REG_CTRL_MEAS	0xF4
#define BMP280_REG_CONFIG		0xF5
#define BMP280_REG_PRESS_MSB	0xF7

// I2C Definition
#define I2C_BAUDRATE		100000U
// I2C Configuration
i2c_master_config_t masterConfig;
uint32_t i2c_sourceClock;


void I2C_Configuration(void)
{
	// Configure I2C Pins
	// PTC1 - SDA
	CLOCK_EnableClock(kCLOCK_PortC);
	const port_pin_config_t ptc1_config = {
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

// BMP280 I2C Status Functions
// Write Register
status_t BMP280_WriteRegister(uint8_t addr, uint8_t reg, uint8_t value)
{
	i2c_master_transfer_t masterXfer;
	memset(&masterXfer, 0, sizeof(masterXfer));
	uint8_t data[2] = { reg, value };

	masterXfer.slaveAddress		= addr;
	masterXfer.direction		= kI2C_Write;
	masterXfer.subaddress		= 0;
	masterXfer.subaddressSize   = 0;
	masterXfer.data 			= data;
	masterXfer.dataSize			= 2;
	masterXfer.flags			= kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &masterXfer);
}

// Read Register
status_t BMP280_ReadRegister(uint8_t addr, uint8_t reg, uint8_t *data, uint32_t length)
{
	i2c_master_transfer_t masterXfer;
	memset(&masterXfer, 0, sizeof(masterXfer));

	masterXfer.slaveAddress		= addr;
	masterXfer.direction		= kI2C_Read;
	masterXfer.subaddress		= reg;
	masterXfer.subaddressSize	= 1;
	masterXfer.data				= data;
	masterXfer.dataSize 		= length;
	masterXfer.flags			= kI2C_TransferDefaultFlag;

	return I2C_MasterTransferBlocking(I2C1, &masterXfer);
}

// BMP280 Calibration Structure
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_t;

bmp280_calib_t calib;
int32_t t_fine;

// Read calibration data from BMP280
void BMP280_ReadCalibration(uint8_t addr)
{
    uint8_t data[24];
    if (BMP280_ReadRegister(addr, BMP280_REG_CALIB_START, data, 24) == kStatus_Success)
    {
        calib.dig_T1 = (uint16_t)(data[1] << 8 | data[0]);
        calib.dig_T2 = (int16_t)(data[3] << 8 | data[2]);
        calib.dig_T3 = (int16_t)(data[5] << 8 | data[4]);

        calib.dig_P1 = (uint16_t)(data[7] << 8 | data[6]);
        calib.dig_P2 = (int16_t)(data[9] << 8 | data[8]);
        calib.dig_P3 = (int16_t)(data[11] << 8 | data[10]);
        calib.dig_P4 = (int16_t)(data[13] << 8 | data[12]);
        calib.dig_P5 = (int16_t)(data[15] << 8 | data[14]);
        calib.dig_P6 = (int16_t)(data[17] << 8 | data[16]);
        calib.dig_P7 = (int16_t)(data[19] << 8 | data[18]);
        calib.dig_P8 = (int16_t)(data[21] << 8 | data[20]);
        calib.dig_P9 = (int16_t)(data[23] << 8 | data[22]);

        PRINTF("Calibration data read:\r\n");
        PRINTF("dig_P1=%u dig_P2=%d dig_P3=%d dig_P4=%d dig_P5=%d dig_P6=%d dig_P7=%d dig_P8=%d dig_P9=%d\r\n",
               calib.dig_P1, calib.dig_P2, calib.dig_P3, calib.dig_P4, calib.dig_P5,
               calib.dig_P6, calib.dig_P7, calib.dig_P8, calib.dig_P9);
    }
}

// Temperature compensation (0.01 °C)
int32_t BMP280_CompensateTemp(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >> 12) * ((int32_t)calib.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8; // 0.01 °C
}

// Pressure compensation (hPa)
float BMP280_CompensatePress(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)calib.dig_P1)) >> 33;

    if (var1 == 0) return 0.0f; // avoid divide by zero

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8);

    return (float)p / 256.0f / 100.0f; // hPa
}

int main(void)
{
	BOARD_InitHardware();
	PRINTF("BMP280 I2C\r\n");

	//Configure I2C Pins
	I2C_Configuration();
	//I2C Initialize
	I2C_init();

	uint8_t bmp_addr = 0;
	static uint8_t calib_done = 0;

	while(1)
	{
		uint8_t id;
		// Detect Sensor
		if (BMP280_ReadRegister(0x76, BMP280_REG_ID, &id, 1) == kStatus_Success)
		{
			bmp_addr = 0x76;
		}
		else if (BMP280_ReadRegister(0x77, BMP280_REG_ID, &id, 1) == kStatus_Success)
		{
			bmp_addr = 0x77;
		}
		else
		{
			PRINTF("BMP280 not detected! Retrying...\r\n");
			for (volatile int i = 0; i < 500000; i++);
			continue;
		}

		PRINTF("Detected BMP280 at 0x%02X, ID=0x%02X\r\n", bmp_addr, id);

		// Read Calibration Once
		if (!calib_done)
		{
			BMP280_ReadCalibration(bmp_addr);
			calib_done = 1;
		}

		// Configure Sensor (Normal Mode, Oversampling x1)
		BMP280_WriteRegister(bmp_addr, BMP280_REG_CTRL_MEAS, 0x27);
		for (volatile int i = 0; i < 200000; i++);

		// Read raw temperature and pressure
		uint8_t raw[6];
		if (BMP280_ReadRegister(bmp_addr, BMP280_REG_PRESS_MSB, raw, 6) == kStatus_Success)
		{
			int32_t rawPress = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | ((raw[2] >> 4) & 0x0F);
			int32_t rawTemp  = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | ((raw[5] >> 4) & 0x0F);

			int32_t tempC_x100 = BMP280_CompensateTemp(rawTemp);
			float press_hPa   = BMP280_CompensatePress(rawPress);

			PRINTF("Temperature: %d.%02d °C, Pressure: %.2f hPa\r\n",
			      tempC_x100 / 100, tempC_x100 % 100, press_hPa);
		}
		else
		{
			PRINTF("Failed to read BMP280 data!\r\n");
		}

		for (volatile int i = 0; i < 1000000; i++);
	}
}









