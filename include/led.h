#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	// No-ops if BOARD_LEDS_PIN / LED_COUNT aren't defined in config.h.
	bool leds_init(void);

	void leds_set_all(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
