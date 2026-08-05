#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	bool ssd1306_init(void);

	void ssd1306_clear(void);

	void ssd1306_text(uint8_t x, uint8_t row, const char *s);

	void ssd1306_pixel(int x, int y, bool on);
	void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
	void ssd1306_rect(int x, int y, int w, int h, bool on);
	int ssd1306_text_scaled(int x, int y, const char *s, uint8_t scale, bool on);

	void ssd1306_show(void);

#ifdef __cplusplus
}
#endif
