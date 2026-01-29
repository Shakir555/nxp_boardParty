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
#define TRANSFER_SIZE			256U
#define	WS2812_SPI_BAUD			3200000U		/* 3.2 MHz */

// Function Prototypes
void FLEXIO_SPI_MasterUserCallback(FLEXIO_SPI_Type *base,
								   flexio_spi_master_dma_handle_t *handle,
								   status_t status,
								   void *userData);
void FLEXIO_SPI_DMA_Config(void);

// Variables
uint8_t masterTxData[TRANSFER_SIZE];
FLEXIO_SPI_Type spiDev;

flexio_spi_master_dma_handle_t g_m_handle;
flexio_spi_master_config_t masterConfig;
flexio_spi_transfer_t masterXfer;

dma_handle_t txHandle;
dma_handle_t rxHandle;

volatile bool isTransferCompleted = false;

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

// Main Function
int main(void)
{
	BOARD_InitHardware();
	PRINTF("WS2812 SPI DMA - DYNAMIC TEST (1 LED)\r\n");


	// FLEXIO SPI DMA Config
	FLEXIO_SPI_DMA_Config();

	// WS2812 Cycle Test
	uint32_t idx;

	while (1)
	{
		/* ========= RED ========= */
		idx = 0;
		ws2812_encode_byte(0, 	masterTxData, &idx);		// G
		ws2812_encode_byte(255, masterTxData, &idx);		// R
		ws2812_encode_byte(0, 	masterTxData, &idx);		// B

        masterXfer.txData   = masterTxData;
		masterXfer.rxData 	= NULL;
		masterXfer.dataSize = idx;
		masterXfer.flags 	= kFLEXIO_SPI_8bitMsb;

		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		// Reset
		memset(masterTxData, 0x00, 64);
		masterXfer.dataSize	= 64;
		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		for (volatile uint32_t d = 0; d < 500000; d++) __NOP();

		/* ========= GREEN ========= */
		idx = 0;
		ws2812_encode_byte(255, masterTxData, &idx);		// G
		ws2812_encode_byte(0,   masterTxData, &idx);		// R
		ws2812_encode_byte(0,	masterTxData, &idx);		// B

		masterXfer.dataSize = idx;
		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		memset(masterTxData, 0x00, 64);
		masterXfer.dataSize = 64;
		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		for (volatile uint32_t d = 0; d < 500000; d++) __NOP();

		/* ========= BLUE ========= */
		idx = 0;
		ws2812_encode_byte(0, 	masterTxData, &idx);		// G
		ws2812_encode_byte(0, 	masterTxData, &idx);		// R
		ws2812_encode_byte(255, masterTxData, &idx); 		// B

		masterXfer.dataSize = idx;
		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		memset(masterTxData, 0x00, 64);
		masterXfer.dataSize = 64;
		isTransferCompleted = false;
		FLEXIO_SPI_MasterTransferDMA(&spiDev, &g_m_handle, &masterXfer);
		while (!isTransferCompleted) {}

		for (volatile uint32_t d = 0; d < 500000; d++) __NOP();
	}
}




























