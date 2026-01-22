/*
 * Author: Shakir Salam
 */

// Libraries
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "app.h"

// Definition
// External LED on PTA 1
#define EXT_LED_GPIO		GPIOA
#define EXT_LED_GPIO_PIN	1U

void delay(void)
{
	volatile uint32_t i = 0;
	for (i = 0; i < 800000; i++)
	{
		__asm("NOP");
	}
}

int main(void)
{
	// On-Board LED Config
	gpio_pin_config_t led_config1 = {
			kGPIO_DigitalOutput,
			0
	};

	// External LED Config
	gpio_pin_config_t led_config2 = {
			kGPIO_DigitalOutput,
			0
	};

	BOARD_InitHardware();

	PRINTF("\r\n GPIO Driver Example\r\n");
	PRINTF("\r\n On-board LED + PTA1 External LED Blinking.\r\n");

	CLOCK_EnableClock(kCLOCK_PortA);
	PORT_SetPinMux(PORTA, 1U, kPORT_MuxAsGpio);

	// Init on-board LED
	GPIO_PinInit(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, &led_config1);

	// Init external LED on PTA1
	GPIO_PinInit(EXT_LED_GPIO, EXT_LED_GPIO_PIN, &led_config2);

	while(1)
	{
		delay();
		// Toggle On-Board LED
		GPIO_PortToggle(BOARD_LED_GPIO, (1u << BOARD_LED_GPIO_PIN));
		// Toggle PTA1 External LED
		GPIO_PortToggle(EXT_LED_GPIO, (1u << EXT_LED_GPIO_PIN));
	}
}

