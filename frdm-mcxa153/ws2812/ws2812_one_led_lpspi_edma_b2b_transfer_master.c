// Libraries
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "board.h"
#include "app.h"
#include <string.h>

// WS2812 Definition
#define WS2812_SPI_BAUD     3200000U
#define WS2812_SPI_BYTES     9

uint8_t spiBuffer[WS2812_SPI_BYTES];

// Encode one WS2812 bit into 3 SPI bits
// 1' = 110
// 0' = 100
static void WS2812_WriteBit(uint8_t bit, uint8_t *buffer, uint32_t *bitPos)
{
    uint8_t pattern = bit ? 0b110 : 0b100;

    for (int i = 2; i >= 0; i--)
    {
        uint32_t byteIndex = (*bitPos) / 8;
        uint32_t bitIndex  = 7 - ((*bitPos) % 8);

        if (pattern & (1 << i))
            buffer[byteIndex] |= (1 << bitIndex);
        else
            buffer[byteIndex] &= ~(1 << bitIndex);

        (*bitPos)++;
    }
}

// Encode one byte (8 bits)
static void WS2812_EncodeByte(uint8_t byte, uint8_t *buffer, uint32_t *bitPos)
{
    for (int i = 7; i >= 0; i--)
    {
        WS2812_WriteBit((byte >> i) & 0x01, buffer, bitPos);
    }
}

// Prepare 1 LED data (GRB format)
void WS2812_PrepareLED(uint8_t *buffer, uint8_t red, uint8_t green, uint8_t blue)
{
    memset(buffer, 0, WS2812_SPI_BYTES);

    uint32_t bitPos = 0;

    /* WS2812 uses GRB order */
    WS2812_EncodeByte(green, buffer, &bitPos);
    WS2812_EncodeByte(red,   buffer, &bitPos);
    WS2812_EncodeByte(blue,  buffer, &bitPos);
}

// Main
int main(void)
{
    lpspi_master_config_t masterConfig;
    lpspi_transfer_t masterXfer;

    BOARD_InitHardware();

    PRINTF("WS2812B RGB Cycle Test\r\n");

    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = WS2812_SPI_BAUD;
    masterConfig.whichPcs = kLPSPI_Pcs0;

    LPSPI_MasterInit(EXAMPLE_LPSPI_MASTER_BASEADDR,
                     &masterConfig,
                     LPSPI_MASTER_CLK_FREQ);

    while (1)
    {
        /* RGB Cycle */
        WS2812_PrepareLED(spiBuffer, 0xFF, 0x00, 0x00); // Red
        masterXfer.txData = spiBuffer;
        masterXfer.rxData = NULL;
        masterXfer.dataSize = WS2812_SPI_BYTES;
        masterXfer.configFlags = kLPSPI_MasterPcs0 | kLPSPI_MasterPcsContinuous;
        LPSPI_MasterTransferBlocking(EXAMPLE_LPSPI_MASTER_BASEADDR, &masterXfer);
        SDK_DelayAtLeastUs(500000, SystemCoreClock);

        WS2812_PrepareLED(spiBuffer, 0x00, 0xFF, 0x00); // Green
        LPSPI_MasterTransferBlocking(EXAMPLE_LPSPI_MASTER_BASEADDR, &masterXfer);
        SDK_DelayAtLeastUs(500000, SystemCoreClock);

        WS2812_PrepareLED(spiBuffer, 0x00, 0x00, 0xFF); // Blue
        LPSPI_MasterTransferBlocking(EXAMPLE_LPSPI_MASTER_BASEADDR, &masterXfer);
        SDK_DelayAtLeastUs(500000, SystemCoreClock);
    }
}
