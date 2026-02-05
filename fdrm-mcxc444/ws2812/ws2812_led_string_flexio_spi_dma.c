/*
 * WS2810 FlexIO SPI DMA
 * Author: Shakir Salam
 * MOSI = PTD7
 */

// Libraries
#include "fsl_debug_console.h"
#include "fsl_spi.h"
#include "fsl_flexio_spi_dma.h"
#include "fsl_dmamux.h"
#include "fsl_dma.h"
#include "board.h"
#include "app.h"
#include <string.h>
#include <stdbool.h>

// Definition
#define TRANSFER_SIZE			600U
#define	WS2812_SPI_BAUD			3200000U		/* 3.2 MHz */
#define LED_COUNT				24				// 24 LED

// Function Prototypes
void FLEXIO_SPI_MasterUserCallback(FLEXIO_SPI_Type *base,
								   flexio_spi_master_dma_handle_t *handle,
								   status_t status,
								   void *userData);
void FLEXIO_SPI_DMA_Config(void);
void ledDelay(void);
void ledCycleTest(void);
void ledBreathing(uint8_t red, uint8_t green, uint8_t blue);

// Variables
uint8_t masterTxData[TRANSFER_SIZE];
FLEXIO_SPI_Type spiDev;

flexio_spi_master_dma_handle_t g_m_handle;
flexio_spi_master_config_t masterConfig;
flexio_spi_transfer_t masterXfer;

dma_handle_t txHandle;
dma_handle_t rxHandle;

volatile bool isTransferCompleted = false;

// LED Index
uint32_t idx;

/*
 * WS2812 SPI Encoder (3-bit method)
 * WS2812 '0' -> 100
 * WS2812 '1' -> 110
 */
void ws2812_encode_byte(uint8_t byte, uint8_t *buf, uint32_t *idx)
{
	for (int i = 7; i >= 0; i--)
	{
		if (byte & (1 << i))
		{
			buf[(*idx)++] = 0x06;				//110
		}
		else
		{
			buf[(*idx)++] = 0x04;				//100
		}
	}
}

// Function FlexIO SPI Callback
void FLEXIO_SPI_MasterUserCallback(FLEXIO_SPI_Type *base,
								   flexio_spi_master_dma_handle_t *handle,
								   status_t status,
								   void *userData)
{
	if (status == kStatus_Success)
	{
		isTransferCompleted = true;
	}
}

// FlexIO Master SPI DMA COnfig Function
void FLEXIO_SPI_DMA_Config(void)
{


	// FlexIO SPI Master Config
	FLEXIO_SPI_MasterGetDefaultConfig(&masterConfig);
	masterConfig.baudRate_Bps = WS2812_SPI_BAUD;

	spiDev.flexioBase		= BOARD_FLEXIO_BASE;
	spiDev.SDOPinIndex		= FLEXIO_SPI_MOSI_PIN;
	spiDev.SDIPinIndex		= FLEXIO_SPI_MISO_PIN;
	spiDev.SCKPinIndex		= FLEXIO_SPI_SCK_PIN;
	spiDev.CSnPinIndex		= FLEXIO_SPI_PCS0_PIN;
	spiDev.shifterIndex[0]  = FLEXIO_TX_SHIFTER_INDEX;
	spiDev.shifterIndex[1]  = FLEXIO_RX_SHIFTER_INDEX;
	spiDev.timerIndex[0]	= 0U;
	spiDev.timerIndex[1]    = 1U;

	FLEXIO_SPI_MasterInit(&spiDev, &masterConfig, FLEXIO_CLOCK_FREQUENCY);

	// DMA Init
	DMAMUX_Init(EXAMPLE_FLEXIO_SPI_DMAMUX_BASEADDR);
	DMA_Init(EXAMPLE_FLEXIO_SPI_DMA_BASEADDR);

	DMAMUX_SetSource(EXAMPLE_FLEXIO_SPI_DMAMUX_BASEADDR,
					 FLEXIO_SPI_TX_DMA_CHANNEL,
					 (dma_request_source_t)EXAMPLE_TX_DMA_SOURCE);

	DMAMUX_EnableChannel(EXAMPLE_FLEXIO_SPI_DMAMUX_BASEADDR,
						 FLEXIO_SPI_TX_DMA_CHANNEL);

	// DMA TX Handle
	DMA_CreateHandle(&txHandle,
					 EXAMPLE_FLEXIO_SPI_DMA_BASEADDR,
					 FLEXIO_SPI_TX_DMA_CHANNEL);

	// RX Handle required by API
	DMA_CreateHandle(&rxHandle,
					 EXAMPLE_FLEXIO_SPI_DMA_BASEADDR,
					 FLEXIO_SPI_RX_DMA_CHANNEL);

	FLEXIO_SPI_MasterTransferCreateHandleDMA(&spiDev,
											 &g_m_handle,
											 FLEXIO_SPI_MasterUserCallback,
											 NULL,
											 &txHandle,
											 &rxHandle);
}

void ws2812_send_frame(uint8_t activeLed, uint8_t r, uint8_t g, uint8_t b)
{
	idx = 0;

	for (uint8_t i = 0; i < LED_COUNT; i++)
	{
		if (i == activeLed)
		{
			// ws2812 order = G R B
			ws2812_encode_byte(g, masterTxData, &idx);
			ws2812_encode_byte(r, masterTxData, &idx);
			ws2812_encode_byte(b, masterTxData, &idx);
		}
		else
		{
			// LED OFF
			ws2812_encode_byte(0, masterTxData, &idx);
			ws2812_encode_byte(0, masterTxData, &idx);
			ws2812_encode_byte(0, masterTxData, &idx);
		}
	}

	masterXfer.txData	= masterTxData;
	masterXfer.rxData   = NULL;
	masterXfer.dataSize = idx;
	masterXfer.flags 	= kFLEXIO_SPI_8bitMsb;

	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// Reset latch (>50 us low)
	memset(masterTxData, 0x00, 64);
	masterXfer.dataSize = 64;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}
}

void ledFillCW(void)
{
	for (uint8_t step = 0; step < LED_COUNT; step++)
	{
		// Red Dot Moving Clockwise
		ws2812_send_frame(step, 255, 0, 0);
		ledDelay();
	}
}

void ledFillCCW(void)
{
	for (uint8_t step = LED_COUNT - 1; step >= 0; step--)
	{
		// Red Dot moving Clockwise
		ws2812_send_frame(step, 255, 0, 0);
		ledDelay();
	}
}

/*************************************************************************
 * LED Breathing Effect (ALL LEDs)
 * Min step = 2
 * Max step = 10
 *************************************************************************/
void ledBreathing(uint8_t red, uint8_t green, uint8_t blue)
{
	static uint8_t breath = 2;
	static int8_t direction = 1;

	const uint8_t maxStep = 30;

	// Update breathing step
	breath += direction;

	if (breath >= maxStep)
		direction = -1;
	else if (breath <= 2)
		direction = 1;

	// Scale colors
	uint8_t r = (red   * breath) / maxStep;
	uint8_t g = (green * breath) / maxStep;
	uint8_t b = (blue  * breath) / maxStep;

	// Send to ALL LEDs
	idx = 0;
	for (uint8_t i = 0; i < LED_COUNT; i++)
	{
		ws2812_encode_byte(g, masterTxData, &idx);
		ws2812_encode_byte(r, masterTxData, &idx);
		ws2812_encode_byte(b, masterTxData, &idx);
	}

	masterXfer.txData   = masterTxData;
	masterXfer.rxData   = NULL;
	masterXfer.dataSize = idx;
	masterXfer.flags    = kFLEXIO_SPI_8bitMsb;

	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// WS2812 reset latch (>50 µs)
	memset(masterTxData, 0x00, 64);
	masterXfer.dataSize = 64;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	ledDelay();   // controls breathing speed
}


void ledDelay(void)
{
	for (volatile uint32_t d = 0; d < 500000; d++) __NOP();
}

void ledCycleTest(void)
{
	/* ========= RED ========= */
	idx = 0;
	ws2812_encode_byte(0, 	masterTxData, &idx);			// G
	ws2812_encode_byte(255, masterTxData, &idx);			// R
	ws2812_encode_byte(0,	masterTxData, &idx);			// B

	masterXfer.txData	= masterTxData;
	masterXfer.rxData	= NULL;
	masterXfer.dataSize = idx;
	masterXfer.flags 	= kFLEXIO_SPI_8bitMsb;

	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// Reset
	memset(masterTxData, 0x00, 64);
	masterXfer.dataSize = 64;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// LED Delay
	ledDelay();

	/* ========= GREEN ========= */
	idx = 0;
	ws2812_encode_byte(255, masterTxData, &idx);		// G
	ws2812_encode_byte(0,   masterTxData, &idx);		// R
	ws2812_encode_byte(0, 	masterTxData, &idx);		// B

	masterXfer.dataSize = idx;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	memset(masterTxData, 0x00, 64);
	masterXfer.dataSize = 64;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// LED Delay
	ledDelay();

	/* ========= BLUE ========= */
	idx = 0;
	ws2812_encode_byte(0,	masterTxData, &idx);		// G
	ws2812_encode_byte(0,	masterTxData, &idx);		// R
	ws2812_encode_byte(255, masterTxData, &idx);		// B

	masterXfer.dataSize = idx;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	memset(masterTxData, 0x00, 64);
	masterXfer.dataSize = 64;
	isTransferCompleted = false;
	FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
	while (!isTransferCompleted) {}

	// LED Delay
	ledDelay();

}

// Main Function
int main(void)
{
	BOARD_InitHardware();
	PRINTF("WS2812 SPI DMA - DYNAMIC TEST  (24 LED)\r\n");


	// FLEXIO SPI DMA Config
	FLEXIO_SPI_DMA_Config();

	while (1)
	{
//		ledFillCW();
		// Red breathing
//		ledBreathing(255, 0, 0);
		ledFillCW();

	}
}
