#include "led.h"
#include "config.h"

#ifdef BOARD_LEDS_PIN

#include "hardware/pio.h"
#include "ws2812.pio.h"

#define LEDS_HZ 800000.0f
#define LEDS_IS_RGBW false

static PIO s_pio = pio0;
static uint s_sm = 0;
static bool s_ready = false;

bool leds_init(void)
{
	PIO candidates[2] = {pio0, pio1};
	for (int i = 0; i < 2; i++)
	{
		PIO pio = candidates[i];
		if (!pio_can_add_program(pio, &ws2812_program))
			continue;
		int sm = pio_claim_unused_sm(pio, false);
		if (sm < 0)
			continue;
		uint offset = pio_add_program(pio, &ws2812_program);
		ws2812_program_init(pio, (uint)sm, offset, BOARD_LEDS_PIN, LEDS_HZ, LEDS_IS_RGBW);
		s_pio = pio;
		s_sm = (uint)sm;
		s_ready = true;
		return true;
	}
	return false;
}

static inline void put_pixel(uint32_t grb)
{
	pio_sm_put_blocking(s_pio, s_sm, grb << 8u);
}

void leds_set_all(uint8_t r, uint8_t g, uint8_t b)
{
	if (!s_ready)
		return;
	uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
	for (int i = 0; i < LED_COUNT; i++)
		put_pixel(grb);
}

#else

bool leds_init(void) { return false; }
void leds_set_all(uint8_t r, uint8_t g, uint8_t b)
{
	(void)r;
	(void)g;
	(void)b;
}

#endif
