#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PS5_SET_QUEUE_DEPTH 8
	typedef struct
	{
		uint8_t report_id;
		uint16_t len;
		uint8_t data[64];
	} ps5_pending_set_t;

	typedef struct
	{
		volatile uint8_t input12[12];

		uint8_t signed_report[64];

		volatile bool dongle_mounted;
		volatile bool dongle_ready;
		volatile bool reports_ready;
		volatile bool unlocked;

		volatile bool hash_pending;
		volatile bool hash_ready;

		uint8_t out_buffer[64];
		uint8_t unlock_buffer[64];
		uint8_t sign_buffer[64];
		uint8_t feat_buffer[64];

		uint8_t calibration_report[40];
		uint8_t firmware_report[63];
		uint8_t mac_pair_report[15];

		// Not sure if the led brightness is correct
		volatile uint8_t led_r, led_g, led_b;
		volatile uint8_t led_scale;
		volatile uint8_t player_leds;
		volatile uint8_t player_num;

		volatile bool connected_bt;
		volatile bool bt_enabled;
		volatile bool have_output;

		// PS5 feature-write relay: core0 enqueues the console's 0x08/0x0A/0xF0 writes,
		// core1 drains them to the dongle verbatim
		ps5_pending_set_t set_queue[PS5_SET_QUEUE_DEPTH];
		volatile uint8_t set_q_head;
		volatile uint8_t set_q_tail;
		volatile bool set_inflight;

		// Native auth (F0/F1/F2) relay: after the last F0 chunk core1 polls F2 then
		// caches the 4 F1 chunks here; core0 serves them, reporting F2 ready only once
		// all 4 F1 chunks are cached
		volatile bool auth_ready_to_poll;
		volatile uint8_t auth_type;
		uint8_t auth_f2[16];
		uint8_t auth_f2_notready[16];
		volatile bool auth_f2_have_notready;
		uint8_t auth_f1[4][64];
		volatile uint8_t auth_f1_read_idx;
		volatile bool auth_f1_ready;
	} dongle_state_t;

	extern dongle_state_t g_state;

#ifdef __cplusplus
}
#endif
