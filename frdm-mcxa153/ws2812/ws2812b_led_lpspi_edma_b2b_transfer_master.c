// WS2812B - 24 LEDs Single Red Moving (Chase Animation)

// Libraries
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "board.h"
#include "app.h"
#include <string.h>

// WS2812B Definition
#define WS2812_SPI_BAUD     3200000U
#define NUM_LEDS            24
#define BITS_PER_LED        24
#define SPI_BYTES           ((NUM_LEDS * BITS_PER_LED * 8) / 8)

uint8_t spiBuffer[SPI_BYTES];

// Encoding: 1 → 1110, 0 → 1000
static void WS2812_WriteBit(uint8_t bit, uint8_t *buffer, uint32_t *bitPos)
{
    uint8_t pattern = bit ? 0b1110 : 0b1000;

    for (int i = 7; i >= 0; i--)
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

// WS2812 Encode Byte
static void WS2812_EncodeByte(uint8_t byte, uint8_t *buffer, uint32_t *bitPos)
{
    for (int i = 7; i >= 0; i--)
    {
        WS2812_WriteBit((byte >> i) & 0x01, buffer, bitPos);
    }
}

// Build LED frame with ONE Red LED
void WS2812_PrepareChase(uint8_t *buffer, int activeLED)
{
    memset(buffer, 0, SPI_BYTES);

    uint32_t bitPos = 0;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i == activeLED)
        {
            // ON
            WS2812_EncodeByte(0x00, buffer, &bitPos); // Green
            WS2812_EncodeByte(0xFF, buffer, &bitPos); // Red
            WS2812_EncodeByte(0x00, buffer, &bitPos); // Blue
        }
        else
        {
            // OFF
            WS2812_EncodeByte(0x00, buffer, &bitPos);
            WS2812_EncodeByte(0x00, buffer, &bitPos);
            WS2812_EncodeByte(0x00, buffer, &bitPos);
        }
    }
}

// Main
int main(void)
{
    lpspi_master_config_t masterConfig;
    lpspi_transfer_t masterXfer;

    BOARD_InitHardware();

    PRINTF("WS2812 - Red Chase Animation\r\n");

    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = WS2812_SPI_BAUD;
    masterConfig.whichPcs = kLPSPI_Pcs0;

    LPSPI_MasterInit(EXAMPLE_LPSPI_MASTER_BASEADDR,
                     &masterConfig,
                     LPSPI_MASTER_CLK_FREQ);

    int currentLED = 0;

    while (1)
    {
        WS2812_PrepareChase(spiBuffer, currentLED);

        masterXfer.txData = spiBuffer;
        masterXfer.rxData = NULL;
        masterXfer.dataSize = SPI_BYTES;
        masterXfer.configFlags = kLPSPI_MasterPcs0 | kLPSPI_MasterPcsContinuous;

        LPSPI_MasterTransferBlocking(EXAMPLE_LPSPI_MASTER_BASEADDR, &masterXfer);

        // Latch/reset
        SDK_DelayAtLeastUs(80, SystemCoreClock);

        // Move to next LED
        currentLED++;
        if (currentLED >= NUM_LEDS)
            currentLED = 0;

        // Speed control
        SDK_DelayAtLeastUs(100000, SystemCoreClock);
    }
}
