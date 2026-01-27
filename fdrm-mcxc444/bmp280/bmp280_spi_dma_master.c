/*
 * BMP280 SPI + DMA for FRDM-MCXC444
 * Author: Shakir Salam
 * CS = PTD4, SCK = PTD5, MISO = PTD6, MOSI = PTD7
 */

// Libraries
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_spi_dma.h"
#include "fsl_dmamux.h"
#include "fsl_dma.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "app.h"
#include <stdbool.h>

// SPI Definition
#define BMP280_SPI 			SPI1
#define SPI_BAUDRATE 		1000000U

// BMP280 CS Definition
#define BMP280_CS_PORT 		GPIOD
#define BMP280_CS_PIN		4U

// BMP280 Registers
#define BMP280_REG_ID				0xD0
#define BMP280_REG_CALIB_START		0x88
#define BMP280_REG_CTRL_MEAS		0xF4
#define BMP280_REG_PRESS_MSB		0xF7

// Calibration Structure
typedef struct {
	uint16_t dig_T1;
	int16_t dig_T2, dig_T3;
	uint16_t dig_P1;
	int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t calib;
static int32_t t_fine;

/* CS Control */
static void BMP280_CS_Low(void)
{
	GPIO_PinWrite(BMP280_CS_PORT, BMP280_CS_PIN, 0);
}

static void BMP280_CS_High(void)
{
	GPIO_PinWrite(BMP280_CS_PORT, BMP280_CS_PIN, 1);
}

void BMP280_CS_Config(void)
{
	gpio_pin_config_t cfg = {
			kGPIO_DigitalOutput, 1
	};
	CLOCK_EnableClock(kCLOCK_PortD);
	PORT_SetPinMux(PORTD, BMP280_CS_PIN, kPORT_MuxAsGpio);
	GPIO_PinInit(BMP280_CS_PORT, BMP280_CS_PIN, &cfg);
	BMP280_CS_High();
}

// DMA and SPI Handles
static spi_dma_handle_t spiHandle;
static dma_handle_t txHandle, rxHandle;
static volatile bool spiFinished = false;

// SPI Callback
static void SPI_Callback(SPI_Type *base, spi_dma_handle_t *handle, status_t status, void *userData)
{
	spiFinished = true;
}

// DMAMUX Configuration for SPI1 TX/RX
void SPI1_DMA_Config(void)
{
	CLOCK_EnableClock(kCLOCK_Dmamux0);
	DMAMUX_Init(DMAMUX0);

	// Choose DMA Channels 0 for TX, 1 for RX (Check SDK if different)
	DMAMUX_SetSource(DMAMUX0, 0, kDmaRequestMux0SPI1Tx);
	DMAMUX_EnableChannel(DMAMUX0, 0);
	DMAMUX_SetSource(DMAMUX0, 1, kDmaRequestMux0SPI1Rx);
	DMAMUX_EnableChannel(DMAMUX0, 1);

	CLOCK_EnableClock(kCLOCK_Dma0);
}

// SPI + DMA Initialization
void BMP280_SPI_Init(void)
{
	CLOCK_EnableClock(kCLOCK_PortD);
	PORT_SetPinMux(PORTD, 5U, kPORT_MuxAlt2); 	// SCK
	PORT_SetPinMux(PORTD, 6U, kPORT_MuxAlt2);	// MISO
	PORT_SetPinMux(PORTD, 7U, kPORT_MuxAlt2);   // MOSI

	CLOCK_EnableClock(kCLOCK_Spi1);

	SPI1_DMA_Config();

	DMA_CreateHandle(&txHandle, DMA0, 0);
	DMA_CreateHandle(&rxHandle, DMA0, 1);

	spi_master_config_t spiCfg;
	SPI_MasterGetDefaultConfig(&spiCfg);
	spiCfg.baudRate_Bps = SPI_BAUDRATE;
	SPI_MasterInit(BMP280_SPI, &spiCfg, CLOCK_GetFreq(kCLOCK_BusClk));

	SPI_MasterTransferCreateHandleDMA(BMP280_SPI, &spiHandle, SPI_Callback, NULL, &txHandle, &rxHandle);

	PRINTF("SPI + DMA Initialized\n");
}

// DMA Write
status_t BMP280_Write_DMA(uint8_t reg, uint8_t val)
{
	uint8_t tx[2] = {
			reg & 0x7F, val
	};

	uint8_t rx[2] = {0};

	spi_transfer_t xfer = {
			tx, rx, 2
	};

	spiFinished = false;

	BMP280_CS_Low();

	SPI_MasterTransferDMA(BMP280_SPI, &spiHandle, &xfer);

	while (!spiFinished);

	BMP280_CS_High();

	return kStatus_Success;
}

// DMA Read
status_t BMP280_Read_DMA(uint8_t reg, uint8_t *buf, uint32_t len)
{
	uint8_t tx[len + 1];

	uint8_t rx[len + 1];

	memset(tx, 0xFF, len + 1);

	tx[0] = reg | 0x80;

	spi_transfer_t xfer = {
			tx, rx, len+1
	};

	spiFinished = false;

	BMP280_CS_Low();

	SPI_MasterTransferDMA(BMP280_SPI, &spiHandle, &xfer);

	while (!spiFinished);

	BMP280_CS_High();

	memcpy(buf, &rx[1], len);

	return kStatus_Success;
}

// Read Calibration
void BMP280_ReadCalibration(void)
{
	uint8_t data[24];
	BMP280_Read_DMA(BMP280_REG_CALIB_START, data, 24);

	calib.dig_T1 = (data[1]<<8)|data[0];
	calib.dig_T2 = (int16_t)((data[3]<<8)|data[2]);
	calib.dig_T3 = (int16_t)((data[5]<<8)|data[4]);
	calib.dig_P1 = (data[7]<<8)|data[6];
	calib.dig_P2 = (int16_t)((data[9]<<8)|data[8]);
	calib.dig_P3 = (int16_t)((data[11]<<8)|data[10]);
	calib.dig_P4 = (int16_t)((data[13]<<8)|data[12]);
	calib.dig_P5 = (int16_t)((data[15]<<8)|data[14]);
	calib.dig_P6 = (int16_t)((data[17]<<8)|data[16]);
	calib.dig_P7 = (int16_t)((data[19]<<8)|data[18]);
	calib.dig_P8 = (int16_t)((data[21]<<8)|data[20]);
	calib.dig_P9 = (int16_t)((data[23]<<8)|data[22]);
}

// Temperature Compensation
int32_t BMP280_CompensateTemp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >> 12) *
              ((int32_t)calib.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8; // temperature in 0.01 °C
}

// Pressure Compensation
float BMP280_CompensatePress(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 += (var1 * (int64_t)calib.dig_P5) << 17;
    var2 += ((int64_t)calib.dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1)<<47)+var1) * (int64_t)calib.dig_P1) >> 33;
    if (var1 == 0) return 0;

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)calib.dig_P9 * (p>>13) * (p>>13)) >> 25;
    var2 = ((int64_t)calib.dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8);

    return (float)p / 25600.0f; // pressure in hPa
}

// BMP280 Initialization
void BMP280_Init(void)
{
    BMP280_CS_Config();
    BMP280_SPI_Init();

    uint8_t id = 0;
    BMP280_Read_DMA(BMP280_REG_ID, &id, 1);
    if (id != 0x58) {
        PRINTF("BMP280 not detected! ID=0x%02X\r\n", id);
        while (1);
    }
    PRINTF("\rBMP280 ID: 0x%02X\r\n", id);

    BMP280_ReadCalibration();
    BMP280_Write_DMA(BMP280_REG_CTRL_MEAS, 0x27); // normal mode, oversampling x1
}

int main(void)
{
	BOARD_InitHardware();
	PRINTF("BMP280 SPI + DMA Example\r\n");

	BMP280_Init();

	while(1)
	{
        uint8_t raw[6];
        SDK_DelayAtLeastUs(7500, CLOCK_GetFreq(kCLOCK_CoreSysClk)); // wait for measurement

        BMP280_Read_DMA(BMP280_REG_PRESS_MSB, raw, 6);

        int32_t rawPress = ((int32_t)raw[0]<<12)|((int32_t)raw[1]<<4)|(raw[2]>>4);
        int32_t rawTemp  = ((int32_t)raw[3]<<12)|((int32_t)raw[4]<<4)|(raw[5]>>4);

        int32_t temp_x100 = BMP280_CompensateTemp(rawTemp);
        float press_hPa = BMP280_CompensatePress(rawPress);

        PRINTF("Temp: %d.%02d C, Pressure: %.2f hPa\r\n",
               temp_x100/100, temp_x100%100, press_hPa);

        SDK_DelayAtLeastUs(1000000, CLOCK_GetFreq(kCLOCK_CoreSysClk)); // 1s delay
	}

}























