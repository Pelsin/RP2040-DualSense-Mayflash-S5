# Mayflash S5 → DualSense passthrough (RP2040)

Turn a RP2040 board + a **Mayflash S5** into a DualSense controller that works on all games on the PS5. Handles both USB and Bluetooth.

This project is a minimal example of getting the **Mayflash S5** to work with some caveats, read more below. There might be bugs.

## Two modes
- **Wired** (default): the RP2040 shows up as a USB DualSense. Works on a PS5
  (with Bluetooth turned off on the console) or on a PC.
- **Bluetooth**: hold **SELECT** while powering on. The RP2040 stays off USB and the S5 handles all the Bluetooth handling. It only connects to a host that has already paired the **Mayflash S5** (pairing is done on the host side).

## Extras

- **LEDs**: 16 WS2812s copy the PS5 lightbar color/brightness. Remove
  `BOARD_LEDS_PIN` in `config.h` to build without them.
- **OLED**: small SSD1306 shows the player number and wired/BT mode.

## Build

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build
cmake ..
make
```

After flashing once you can hold **Start (S2)** ~1s to jump into BOOTSEL

## Config

Config live in [`include/config.h`](include/config.h). Edit them for your board (defaults are based on the Haute42 S13)

# CAVEATS

- Haven't figured out how to not broadcast inputs over BT if the S5 is paired and powered
This can result in controlling a paired device over BT and a device connected over USB

- Only works with the firmware version 1.08+ on the Mayflash S5

## Credits & Thanks

- Based on some of the work by **TheTrain** and **Lucipher** from the OpenStickCommunity.

- Thanks to Mayflash for providing the secret to authenticate against the **Mayflash S5**
