/*
 * WS2812 FlexIO SPI EDMA - LED Chase Cycle
 * Author: Shakir Salam
 * MOSI = PTD7 (adjust pins in board.h if needed)
 */

#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "fsl_flexio_spi_edma.h"
#include "fsl_edma.h"
#include "board.h"
#include "app.h"
#include <string.h>
#include <stdbool.h>

// ================= Definitions =================
#define TRANSFER_SIZE   1024U        // buffer size for 24 LEDs
#define WS2812_SPI_BAUD 3200000U
#define LED_COUNT       24           // number of LEDs
#define RESET_BYTES     128          // reset pulse (>50us)

// ================= Prototypes =================
void FLEXIO_SPI_MasterUserCallback(FLEXIO_SPI_Type *base,
                                   flexio_spi_master_edma_handle_t *handle,
                                   status_t status,
                                   void *userData);
void FLEXIO_SPI_EDMA_Config(void);
void ws2812_encode_byte(uint8_t byte, uint8_t *buf, uint32_t *idx);
void ws2812_send_frame(uint8_t r, uint8_t g, uint8_t b);
void ledChaseCycle(void);

// ================= Variables =================
uint8_t masterTxData[TRANSFER_SIZE];
FLEXIO_SPI_Type spiDev;

flexio_spi_master_edma_handle_t g_m_handle;
flexio_spi_master_config_t masterConfig;
flexio_spi_transfer_t masterXfer;

edma_config_t config;
dma_request_source_t dma_request_source_tx;
dma_request_source_t dma_request_source_rx;

edma_handle_t txHandle;
edma_handle_t rxHandle;

uint32_t idx;
volatile bool isTransferCompleted = false;

// ================= WS2812 Encoding =================
void ws2812_encode_byte(uint8_t byte, uint8_t *buf, uint32_t *idx)
{
    for (int i = 7; i >= 0; i--)
    {
        if (byte & (1 << i))
            buf[(*idx)++] = 0x06;    // '1' -> 110
        else
            buf[(*idx)++] = 0x04;    // '0' -> 100
    }
}

// ================= FlexIO SPI Callback =================
void FLEXIO_SPI_MasterUserCallback(FLEXIO_SPI_Type *base,
                                   flexio_spi_master_edma_handle_t *handle,
                                   status_t status,
                                   void *userData)
{
    if (status == kStatus_Success)
        isTransferCompleted = true;
}

// ================= FlexIO SPI EDMA Config =================
void FLEXIO_SPI_EDMA_Config(void)
{
    FLEXIO_SPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = WS2812_SPI_BAUD;

    spiDev.flexioBase   = BOARD_FLEXIO_BASE;
    spiDev.SDOPinIndex  = FLEXIO_SPI_MOSI_PIN;
    spiDev.SDIPinIndex  = FLEXIO_SPI_MISO_PIN;
    spiDev.SCKPinIndex  = FLEXIO_SPI_SCK_PIN;
    spiDev.CSnPinIndex  = FLEXIO_SPI_CSn_PIN;
    spiDev.shifterIndex[0] = FLEXIO_TX_SHIFTER_INDEX;
    spiDev.shifterIndex[1] = FLEXIO_RX_SHIFTER_INDEX;
    spiDev.timerIndex[0] = 0U;
    spiDev.timerIndex[1] = 1U;

    dma_request_source_tx = (dma_request_source_t)EXAMPLE_TX_DMA_SOURCE;
    dma_request_source_rx = (dma_request_source_t)EXAMPLE_RX_DMA_SOURCE;

#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
    DMAMUX_Init(EXAMPLE_FLEXIO_SPI_TX_DMAMUX_BASEADDR);
    DMAMUX_Init(EXAMPLE_FLEXIO_SPI_RX_DMAMUX_BASEADDR);
    DMAMUX_SetSource(EXAMPLE_FLEXIO_SPI_TX_DMAMUX_BASEADDR, FLEXIO_SPI_TX_DMAMUX_CHANNEL, dma_request_source_tx);
    DMAMUX_SetSource(EXAMPLE_FLEXIO_SPI_RX_DMAMUX_BASEADDR, FLEXIO_SPI_RX_DMAMUX_CHANNEL, dma_request_source_rx);
    DMAMUX_EnableChannel(EXAMPLE_FLEXIO_SPI_TX_DMAMUX_BASEADDR, FLEXIO_SPI_TX_DMAMUX_CHANNEL);
    DMAMUX_EnableChannel(EXAMPLE_FLEXIO_SPI_RX_DMAMUX_BASEADDR, FLEXIO_SPI_RX_DMAMUX_CHANNEL);
#endif

    EDMA_GetDefaultConfig(&config);
    EDMA_Init(EXAMPLE_FLEXIO_SPI_DMA_BASEADDR, &config);
    EDMA_CreateHandle(&txHandle, EXAMPLE_FLEXIO_SPI_DMA_BASEADDR, FLEXIO_SPI_TX_DMA_CHANNEL);
    EDMA_CreateHandle(&rxHandle, EXAMPLE_FLEXIO_SPI_DMA_BASEADDR, FLEXIO_SPI_RX_DMA_CHANNEL);

#if defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && FSL_FEATURE_EDMA_HAS_CHANNEL_MUX
    EDMA_SetChannelMux(EXAMPLE_FLEXIO_SPI_DMA_BASEADDR, FLEXIO_SPI_TX_DMA_CHANNEL, dma_request_source_tx);
    EDMA_SetChannelMux(EXAMPLE_FLEXIO_SPI_DMA_BASEADDR, FLEXIO_SPI_RX_DMA_CHANNEL, dma_request_source_rx);
#endif

    FLEXIO_SPI_MasterInit(&spiDev, &masterConfig, FLEXIO_CLOCK_FREQUENCY);
    FLEXIO_SPI_MasterTransferCreateHandleEDMA(&spiDev, &g_m_handle,
                                              FLEXIO_SPI_MasterUserCallback,
                                              NULL,
                                              &txHandle,
                                              &rxHandle);
}

// ================= Send Frame =================
void ws2812_send_frame(uint8_t r, uint8_t g, uint8_t b)
{
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
    FLEXIO_SPI_MasterTransferEDMA(&spiDev, &g_m_handle, &masterXfer);
    while(!isTransferCompleted) {}

    // Reset latch (>50us)
    memset(masterTxData, 0x00, RESET_BYTES);
    masterXfer.dataSize = RESET_BYTES;
    isTransferCompleted = false;
    FLEXIO_SPI_MasterTransferEDMA(&spiDev, &g_m_handle, &masterXfer);
    while(!isTransferCompleted) {}
}

// ================= LED Chase Cycle =================
void ledChaseCycle(void)
{
    for (uint8_t led = 0; led < LED_COUNT; led++)
    {
        idx = 0;
        for (uint8_t i = 0; i < LED_COUNT; i++)
        {
            if (i == led)
            {
                ws2812_encode_byte(0,   masterTxData, &idx);  // G
                ws2812_encode_byte(255, masterTxData, &idx);  // R
                ws2812_encode_byte(0,   masterTxData, &idx);  // B
            }
            else
            {
                ws2812_encode_byte(0, masterTxData, &idx);
                ws2812_encode_byte(0, masterTxData, &idx);
                ws2812_encode_byte(0, masterTxData, &idx);
            }
        }
        masterXfer.txData   = masterTxData;
        masterXfer.rxData   = NULL;
        masterXfer.dataSize = idx;
        masterXfer.flags    = kFLEXIO_SPI_8bitMsb;

        isTransferCompleted = false;
        FLEXIO_SPI_MasterTransferEDMA(&spiDev, &g_m_handle, &masterXfer);
        while(!isTransferCompleted) {}

        // Reset latch
        memset(masterTxData, 0x00, RESET_BYTES);
        masterXfer.dataSize = RESET_BYTES;
        isTransferCompleted = false;
        FLEXIO_SPI_MasterTransferEDMA(&spiDev, &g_m_handle, &masterXfer);
        while(!isTransferCompleted) {}

        // Delay for visible movement
        for (volatile uint32_t d = 0; d < 100000; d++) __NOP();
    }
}

// ================= Main =================
int main(void)
{
    BOARD_InitHardware();
    PRINTF("WS2812 SPI DMA - 24 LEDs Chase\r\n");

    FLEXIO_SPI_EDMA_Config();

    while(1)
    {
        ledChaseCycle(); // run LED chase cycle
    }
}
