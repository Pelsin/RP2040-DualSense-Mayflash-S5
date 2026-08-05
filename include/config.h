// Change these values to match your board's pinout, i've used the defaults as the haute42 S13
#pragma once

#include <stdint.h>

#define PIO_USB_DP_PIN 23

// Can be removed if you dont have leds
#define BOARD_LEDS_PIN 28
#define LED_COUNT 16

#define DISPLAY_I2C i2c0
#define DISPLAY_SDA_PIN 0
#define DISPLAY_SCL_PIN 1
#define DISPLAY_I2C_ADDR 0x3C
#define DISPLAY_I2C_HZ 1000000

#define GPIO_UP 2
#define GPIO_DOWN 3
#define GPIO_RIGHT 4
#define GPIO_LEFT 5
#define GPIO_B1 6 // Cross
#define GPIO_B2 7 // Circle
#define GPIO_R2 8
#define GPIO_L2 9
#define GPIO_B3 10 // Square
#define GPIO_B4 11 // Triangle
#define GPIO_R1 12
#define GPIO_L1 13
#define GPIO_S1 16 // Select / Share
#define GPIO_S2 17 // Start  (also: HOLD => BOOTSEL)
#define GPIO_L3 18
#define GPIO_R3 19
#define GPIO_A1 20 // PS / Home
#define GPIO_A2 21

#define GPIO_UP_ALT 27

// How long S2 must be held to trigger BOOTSEL
#define BOOTSEL_HOLD_US (1000u * 1000u)

#define DONGLE_BT_ENABLED_DEFAULT 0
#define GPIO_BT_TOGGLE GPIO_S1

// Hmm, perhaps this should be in buttons.cpp
static const uint8_t kButtonPins[] = {
		GPIO_UP,
		GPIO_DOWN,
		GPIO_RIGHT,
		GPIO_LEFT,
		GPIO_B1,
		GPIO_B2,
		GPIO_R2,
		GPIO_L2,
		GPIO_B3,
		GPIO_B4,
		GPIO_R1,
		GPIO_L1,
		GPIO_S1,
		GPIO_S2,
		GPIO_L3,
		GPIO_R3,
		GPIO_A1,
		GPIO_A2,
		GPIO_UP_ALT,
};
