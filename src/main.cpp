#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "pio_usb.h"
#include "tusb.h"

#include "config.h"
#include "shared_state.h"
#include "dongle.h"
#include "buttons.h"
#include "ssd1306.h"
#include "led.h"

#include <stdio.h>
#include <string.h>

dongle_state_t g_state;

static void core1_main(void)
{
	pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
	pio_cfg.pin_dp = PIO_USB_DP_PIN;
	tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

	tuh_init(BOARD_TUH_RHPORT);
	dongle_init();

	for (;;)
	{
		tuh_task();
		dongle_task();
	}
}

static void draw_status(bool have_display)
{
	if (!have_display)
		return;
	static uint32_t frame = 0;
	frame++;

	ssd1306_clear();

	ssd1306_fill_rect(0, 0, 128, 13, true);
	ssd1306_text_scaled(4, 3, "DualSense - S5", 1, false);
	ssd1306_rect(0, 0, 128, 64, true);

	// The S5's is in bluetooth mode, so we don't even try to enumerate the DualSense. Just show the status
	if (g_state.bt_enabled)
	{
		ssd1306_text_scaled(10, 22, "BLUETOOTH", 2, true);
		const char *sub = g_state.reports_ready ? "S5 live" : "starting S5";
		int w = (int)strlen(sub) * 6;
		ssd1306_text_scaled((128 - w) / 2, 46, sub, 1, true);
		ssd1306_show();
		return;
	}

	// Waiting
	if (!tud_mounted())
	{
		ssd1306_text_scaled(19, 24, "WAITING", 2, true);
		char dots[5] = {0};
		int n = (int)((frame / 3) % 4);
		for (int i = 0; i < n; i++)
			dots[i] = '.';
		char sub[24];
		snprintf(sub, sizeof(sub), "%s%s", g_state.reports_ready ? "for host" : "dongle init", dots);
		ssd1306_text_scaled(20, 46, sub, 1, true);
		ssd1306_show();
		return;
	}

	ssd1306_text_scaled(7, 17, "PLAYER", 1, true);
	char big[2] = {g_state.player_num ? (char)('0' + g_state.player_num) : '?', 0};
	ssd1306_text_scaled(11, 27, big, 4, true);

	ssd1306_text_scaled(64, 17, g_state.connected_bt ? "BLUETOOTH" : "WIRED", 1, true);

	for (int i = 0; i < 5; i++)
	{
		int x = 64 + i * 11, y = 30;
		if (g_state.player_leds & (1u << i))
			ssd1306_fill_rect(x, y, 8, 8, true);
		else
			ssd1306_rect(x, y, 8, 8, true);
	}

	ssd1306_show();
}

int main(void)
{
	set_sys_clock_khz(120000, true);

	memset(&g_state, 0, sizeof(g_state));

	// Some defaults
	g_state.input12[0] = 0x01;
	g_state.input12[1] = g_state.input12[2] = g_state.input12[3] = g_state.input12[4] = 0x80;
	g_state.led_b = 0x20;
	g_state.led_scale = 0xFF;

	buttons_init();

	sleep_us(2000);

	bool bt_toggle_held = (gpio_get(GPIO_BT_TOGGLE) == 0);
	g_state.bt_enabled = bt_toggle_held ? !(DONGLE_BT_ENABLED_DEFAULT) : (DONGLE_BT_ENABLED_DEFAULT);
	g_state.connected_bt = g_state.bt_enabled;

	bool have_display = ssd1306_init();

	multicore_launch_core1(core1_main);

	// I think we should wait a bit for the dongle
	const uint64_t kArmTimeoutUs = 4000000;
	uint64_t start_wait = time_us_64(), last_wait = 0;
	while (!g_state.reports_ready && (time_us_64() - start_wait) < kArmTimeoutUs)
	{
		uint64_t now = time_us_64();
		if (now - last_wait >= 100000)
		{
			last_wait = now;
			draw_status(have_display);
		}
	}

	leds_init();

	// In wired we should act as a dualsense, but in BT mode the S5 should handle everything
	// TODO: There is still an issue where the S5 will send BT reports to the paired device in wired mode
	if (!g_state.bt_enabled)
		tud_init(BOARD_TUD_RHPORT);

	uint64_t last_draw = 0, last_report = 0;
	uint8_t lr = 0xFF, lg = 0xFF, lb = 0xFF, ls = 0xFF;
	for (;;)
	{
		buttons_task();

		if (!g_state.bt_enabled)
		{
			tud_task();

			if (g_state.hash_ready && tud_hid_ready())
			{
				uint8_t report[64];
				memcpy(report, g_state.signed_report, 64);
				if (tud_hid_report(0, report, sizeof(report)))
				{
					g_state.hash_ready = false;
					last_report = time_us_64();
				}
			}
			else if (tud_hid_ready() && (time_us_64() - last_report) >= 15000)
			{
				uint8_t raw[64];
				memset(raw, 0, sizeof(raw));
				for (int i = 0; i < 12; i++)
					raw[i] = g_state.input12[i];
				raw[0] = 0x01;
				raw[0x21] = 0x80;
				raw[0x25] = 0x80;
				if (tud_hid_report(0, raw, sizeof(raw)))
				{
					last_report = time_us_64();
				}
			}
		}

		uint8_t cr = g_state.led_r, cg = g_state.led_g, cb = g_state.led_b;
		uint8_t cs = g_state.led_scale;
		if (cr != lr || cg != lg || cb != lb || cs != ls)
		{
			leds_set_all((uint8_t)((cr * cs) / 255), (uint8_t)((cg * cs) / 255), (uint8_t)((cb * cs) / 255));
			lr = cr;
			lg = cg;
			lb = cb;
			ls = cs;
		}

		uint64_t now = time_us_64();
		if (now - last_draw >= 100000)
		{
			last_draw = now;
			draw_status(have_display);
		}
	}
}

extern "C" void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
	(void)instance;
	(void)report;
	(void)len;
}

extern "C" void tud_mount_cb(void) {}
extern "C" void tud_umount_cb(void) {}
